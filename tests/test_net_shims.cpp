// Host-side checks for the network shims in src/abi/SyscallShim.cpp that a guest reaches by
// name: socketpair, vectored I/O, ppoll/pselect, dup3, getnameinfo, gethostbyname_r. The
// Linux host exercises each shim's contract; the Darwin-only translations they apply on
// device are covered by test_socket_abi.
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

extern "C" int bionic_socketpair(int domain, int type, int protocol, int sv[2]);
extern "C" ssize_t bionic_readv(int fd, const struct iovec* iov, int iovcnt);
extern "C" ssize_t bionic_writev(int fd, const struct iovec* iov, int iovcnt);
extern "C" ssize_t bionic_read(int fd, void* buf, size_t count);
extern "C" ssize_t bionic_write(int fd, const void* buf, size_t count);
extern "C" int bionic_poll(struct pollfd* fds, unsigned long nfds, int timeout);
extern "C" int bionic_ppoll(struct pollfd* fds, unsigned long nfds, const struct timespec* tmo,
                            const void* sigmask);
extern "C" int bionic_pselect(int nfds, void* r, void* w, void* e, const struct timespec* tmo,
                              const void* sigmask);
extern "C" int bionic_dup3(int oldfd, int newfd, int flags);
extern "C" int bionic_close(int fd);
extern "C" int bionic_getnameinfo(const struct sockaddr* sa, socklen_t salen, char* host,
                                  socklen_t hostlen, char* serv, socklen_t servlen, int flags);
extern "C" int bionic_gethostbyname_r(const char* name, struct hostent* ret, char* buf,
                                      size_t buflen, struct hostent** result, int* h_errnop);
extern "C" int bionic_ioctl(int fd, unsigned long request, void* arg);
extern "C" int bionic_close(int fd);

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what);
    if (!ok) ++g_failures;
}

double Now() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

void test_socketpair_and_vectored_io() {
    std::printf("[net] socketpair and vectored I/O\n");
    int sv[2] = {-1, -1};
    // Linux SOCK_NONBLOCK | SOCK_CLOEXEC folded into the type, as a guest passes them.
    Check(bionic_socketpair(1, 1 | 0x800 | 0x80000, 0, sv) == 0 && sv[0] >= 0 && sv[1] >= 0,
          "socketpair accepts the folded SOCK_* flags");
    char c;
    errno = 0;
    Check(bionic_read(sv[0], &c, 1) == -1 && errno == EAGAIN,
          "SOCK_NONBLOCK applies: an empty read fails with EAGAIN (Linux 11)");
    Check(errno == 11, "the errno a guest compares against is Linux's EAGAIN");
    Check((fcntl(sv[0], F_GETFD) & FD_CLOEXEC) != 0, "SOCK_CLOEXEC applies");

    char a[] = "abc";
    char b[] = "defg";
    struct iovec out[2] = {{a, 3}, {b, 4}};
    Check(bionic_writev(sv[1], out, 2) == 7, "writev writes both buffers");
    char x[2] = {};
    char y[8] = {};
    struct iovec in[2] = {{x, 2}, {y, 8}};
    Check(bionic_readv(sv[0], in, 2) == 7 && std::memcmp(x, "ab", 2) == 0 &&
              std::memcmp(y, "cdefg", 5) == 0,
          "readv scatters across both buffers");
    bionic_close(sv[0]);
    bionic_close(sv[1]);
}

void test_poll_family() {
    std::printf("[net] poll, ppoll, pselect\n");
    int sv[2];
    bionic_socketpair(1, 1, 0, sv);

    struct pollfd p = {sv[0], POLLIN, 0};
    timespec zero = {0, 500000};  // 0.5ms must round up to 1ms, never become a block
    const double t0 = Now();
    Check(bionic_ppoll(&p, 1, &zero, nullptr) == 0, "ppoll times out on an idle socket");
    Check(Now() - t0 < 0.5, "a sub-millisecond ppoll timeout does not block");

    Check(bionic_write(sv[1], "z", 1) == 1, "peer writes");
    p.revents = 0;
    Check(bionic_ppoll(&p, 1, nullptr, nullptr) == 1 && (p.revents & POLLIN) != 0,
          "ppoll with no timeout returns once the socket is readable");
    p.revents = 0;
    Check(bionic_poll(&p, 1, 0) == 1 && (p.revents & POLLIN) != 0, "poll agrees");

    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(sv[0], &rset);
    timespec tmo = {1, 0};
    Check(bionic_pselect(sv[0] + 1, &rset, nullptr, nullptr, &tmo, nullptr) == 1 &&
              FD_ISSET(sv[0], &rset),
          "pselect reports the readable socket");

    char c;
    bionic_read(sv[0], &c, 1);
    FD_ZERO(&rset);
    FD_SET(sv[0], &rset);
    timespec none = {0, 1000000};
    Check(bionic_pselect(sv[0] + 1, &rset, nullptr, nullptr, &none, nullptr) == 0,
          "pselect times out once drained");
    bionic_close(sv[0]);
    bionic_close(sv[1]);
}

void test_dup3() {
    std::printf("[net] dup3\n");
    int sv[2];
    bionic_socketpair(1, 1, 0, sv);
    errno = 0;
    Check(bionic_dup3(sv[0], sv[0], 0) == -1 && errno == EINVAL, "dup3 rejects old == new");
    const int target = 200;
    Check(bionic_dup3(sv[0], target, 0x80000) == target, "dup3 duplicates onto the requested fd");
    Check((fcntl(target, F_GETFD) & FD_CLOEXEC) != 0, "O_CLOEXEC is honored");
    bionic_close(target);
    bionic_close(sv[0]);
    bionic_close(sv[1]);
}

void test_names() {
    std::printf("[net] getnameinfo and gethostbyname_r\n");
    struct sockaddr_in sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
    char host[64] = {}, serv[16] = {};
    Check(bionic_getnameinfo(reinterpret_cast<sockaddr*>(&sa), sizeof(sa), host, sizeof(host),
                             serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV) == 0 &&
              std::string(host) == "127.0.0.1" && std::string(serv) == "8080",
          "getnameinfo formats numeric host and service");

    struct hostent ret;
    struct hostent* result = nullptr;
    char buf[512];
    int herr = 0;
    Check(bionic_gethostbyname_r("localhost", &ret, buf, sizeof(buf), &result, &herr) == 0 &&
              result == &ret && ret.h_addrtype == 2 && ret.h_length == 4 &&
              ret.h_addr_list[0] != nullptr,
          "gethostbyname_r resolves localhost into the caller's buffer");
    Check(result != nullptr && static_cast<unsigned char>(ret.h_addr_list[0][0]) == 127,
          "the address is loopback");
    char tiny[8];
    result = &ret;
    Check(bionic_gethostbyname_r("localhost", &ret, tiny, sizeof(tiny), &result, &herr) == ERANGE &&
              result == nullptr,
          "a too-small buffer reports ERANGE and no result");
    Check(bionic_gethostbyname_r("no-such-host.invalid", &ret, buf, sizeof(buf), &result,
                                 &herr) == 0 && result == nullptr && herr != 0,
          "an unknown name returns no result with a host error");
}

void test_ioctl() {
    std::printf("[net] ioctl\n");
    int sv[2];
    bionic_socketpair(1, 1, 0, sv);
    int pending = -1;
    Check(bionic_ioctl(sv[0], 0x541B, &pending) == 0 && pending == 0, "FIONREAD on an empty socket");
    bionic_write(sv[1], "hello", 5);
    pending = -1;
    Check(bionic_ioctl(sv[0], 0x541B, &pending) == 0 && pending == 5, "FIONREAD counts queued bytes");
    int on = 1;
    Check(bionic_ioctl(sv[0], 0x5421, &on) == 0 && (fcntl(sv[0], F_GETFL) & O_NONBLOCK) != 0,
          "FIONBIO sets non-blocking mode");
    on = 0;
    Check(bionic_ioctl(sv[0], 0x5421, &on) == 0 && (fcntl(sv[0], F_GETFL) & O_NONBLOCK) == 0,
          "FIONBIO clears it");
    errno = 0;
    Check(bionic_ioctl(sv[0], 0x1234, nullptr) == -1 && errno == ENOTTY,
          "an unknown request is ENOTTY");
    bionic_close(sv[0]);
    bionic_close(sv[1]);
}

}  // namespace

int main() {
    std::printf("=== network shims ===\n");
    test_socketpair_and_vectored_io();
    test_poll_family();
    test_dup3();
    test_names();
    test_ioctl();
    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
