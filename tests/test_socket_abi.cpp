// The Linux-to-Darwin socket and fcntl tables. Expected values are written as literals
// taken from each platform's headers, so a wrong entry in the table fails here rather than
// as a connection that "succeeds" and is then reported as failed.
#include "kudroid/abi/SocketAbi.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what);
    if (!ok) ++g_failures;
}

bool Option(int level, int name, int wantLevel, int wantName) {
    int l = -1, n = -1;
    return kudroid::SocketOptionToHost(level, name, &l, &n) && l == wantLevel && n == wantName;
}

}  // namespace

int main() {
    using namespace kudroid;
    std::printf("=== Linux to Darwin socket ABI ===\n");

    // The option a client uses to confirm a non-blocking connect.
    Check(Option(1, 4, 0xffff, 0x1007), "SOL_SOCKET/SO_ERROR maps to Darwin's");
    Check(Option(1, 2, 0xffff, 0x4), "SO_REUSEADDR");
    Check(Option(1, 9, 0xffff, 0x8), "SO_KEEPALIVE");
    Check(Option(1, 7, 0xffff, 0x1001) && Option(1, 8, 0xffff, 0x1002), "SO_SNDBUF and SO_RCVBUF");
    Check(Option(1, 20, 0xffff, 0x1006) && Option(1, 21, 0xffff, 0x1005), "SO_RCVTIMEO and SO_SNDTIMEO");
    Check(Option(6, 1, 6, 1), "TCP_NODELAY keeps its number");
    Check(Option(6, 4, 6, 0x10) && Option(6, 5, 6, 0x101) && Option(6, 6, 6, 0x102),
          "TCP keepalive idle, interval and count");
    Check(Option(0, 33, 0, 10) && Option(0, 34, 0, 11) && Option(0, 35, 0, 12),
          "IP multicast TTL, loop and membership");
    Check(Option(41, 26, 41, 27), "IPV6_V6ONLY");
    int l, n;
    Check(!SocketOptionToHost(1, 25, &l, &n), "SO_BINDTODEVICE has no Darwin equivalent");
    Check(!SocketOptionToHost(99, 1, &l, &n), "an unknown level is refused");

    Check(SocketErrnoToGuest(35) == 11, "EAGAIN");
    Check(SocketErrnoToGuest(36) == 115, "EINPROGRESS");
    Check(SocketErrnoToGuest(61) == 111, "ECONNREFUSED");
    Check(SocketErrnoToGuest(60) == 110, "ETIMEDOUT");
    Check(SocketErrnoToGuest(54) == 104, "ECONNRESET");
    Check(SocketErrnoToGuest(65) == 113, "EHOSTUNREACH");
    Check(SocketErrnoToGuest(102) == 95 && SocketErrnoToGuest(45) == 95, "both Darwin spellings of ENOTSUP");
    Check(SocketErrnoToGuest(4) == 4 && SocketErrnoToGuest(9) == 9 && SocketErrnoToGuest(0) == 0,
          "numbers shared by both systems pass through");

    Check(SocketMsgFlagsToHost(0x4000) == 0x80000, "MSG_NOSIGNAL");
    Check(SocketMsgFlagsToHost(0x40) == 0x80, "MSG_DONTWAIT");
    Check(SocketMsgFlagsToHost(0x100) == 0x40, "MSG_WAITALL");
    Check(SocketMsgFlagsToHost(0x2) == 0x2 && SocketMsgFlagsToHost(0x1) == 0x1, "MSG_PEEK and MSG_OOB");
    Check(SocketMsgFlagsToHost(0x8000) == 0, "MSG_MORE has no Darwin equivalent and is dropped");

    Check(StatusFlagsToHost(0x800) == 0x4, "O_NONBLOCK to Darwin");
    Check(StatusFlagsToHost(0x2 | 0x800 | 0x400) == (0x2 | 0x4 | 0x8), "access mode, O_NONBLOCK and O_APPEND");
    Check(StatusFlagsToGuest(0x4) == 0x800, "O_NONBLOCK back to Linux");
    Check(StatusFlagsToGuest(StatusFlagsToHost(0x2 | 0x800 | 0x400 | 0x2000)) == (0x2 | 0x800 | 0x400 | 0x2000),
          "status flags survive a round trip");

    Check(FcntlCommandToHost(3) == 3 && FcntlCommandToHost(4) == 4, "F_GETFL and F_SETFL keep their numbers");
    Check(FcntlCommandToHost(6) == 8 && FcntlCommandToHost(7) == 9 && FcntlCommandToHost(5) == 7,
          "the lock commands differ and are translated");
    Check(FcntlCommandToHost(1030) == 67, "F_DUPFD_CLOEXEC");
    Check(FcntlCommandToHost(1031) == -1, "F_SETPIPE_SZ has no Darwin equivalent");

    LinuxFlock guest = {1, 0, 100, 50, 7};  // F_WRLCK from offset 100, length 50
    DarwinFlock host;
    FlockToHost(guest, &host);
    Check(host.l_type == 3 && host.l_start == 100 && host.l_len == 50 && host.l_whence == 0,
          "a write lock becomes F_WRLCK with the same range");
    host.l_type = 2;  // F_UNLCK
    LinuxFlock back;
    FlockToGuest(host, &back);
    Check(back.l_type == 2 && back.l_start == 100 && back.l_len == 50, "F_UNLCK comes back as Linux's");
    Check(sizeof(LinuxFlock) == 32 && sizeof(DarwinFlock) == 24, "the structs have each platform's size");

    Check(sizeof(GuestAddrinfo) == 48, "GuestAddrinfo has Linux LP64 size 48");
    Check(offsetof(GuestAddrinfo, ai_canonname) == 24, "GuestAddrinfo ai_canonname is at offset 24 (before ai_addr)");
    Check(offsetof(GuestAddrinfo, ai_addr) == 32, "GuestAddrinfo ai_addr is at offset 32");
    Check(offsetof(GuestAddrinfo, ai_next) == 40, "GuestAddrinfo ai_next is at offset 40");

    Check(AddrInfoFlagsToHost(0x400) == 0x1000, "Linux AI_NUMERICSERV (0x400) moves to Darwin 0x1000");
    Check(AddrInfoFlagsToHost(0x8) == 0x800, "Linux AI_V4MAPPED (0x8) moves to Darwin 0x800");
    Check(AddrInfoFlagsToHost(0x10) == 0x100, "Linux AI_ALL (0x10) moves to Darwin 0x100");
    Check(AddrInfoFlagsToHost(0x20) == 0x400, "Linux AI_ADDRCONFIG (0x20) moves to Darwin 0x400");
    Check(AddrInfoFlagsToHost(0x1 | 0x2 | 0x4) == (0x1 | 0x2 | 0x4),
          "AI_PASSIVE, AI_CANONNAME, and AI_NUMERICHOST keep their values");
    Check(AddrInfoFlagsToHost(0x400 | 0x2) == (0x1000 | 0x2), "flags combine");

    {
        char name[] = "example.test";
        char alias1[] = "www.example.test";
        char* aliases[] = {alias1, nullptr};
        unsigned char a1[4] = {10, 0, 0, 1};
        unsigned char a2[4] = {10, 0, 0, 2};
        char* addrs[] = {reinterpret_cast<char*>(a1), reinterpret_cast<char*>(a2), nullptr};
        GuestHostent src = {name, aliases, 30, 4, addrs};
        alignas(8) char buf[256];
        GuestHostent dst;
        Check(HostentCopy(src, 10, &dst, buf, sizeof(buf)) == 0, "a hostent fits a 256-byte buffer");
        Check(dst.h_addrtype == 10 && dst.h_length == 4, "the guest's address family is reported");
        Check(dst.h_name != name && std::string(dst.h_name) == "example.test",
              "the name is copied into the buffer");
        Check(dst.h_aliases[0] != nullptr && std::string(dst.h_aliases[0]) == "www.example.test" &&
                  dst.h_aliases[1] == nullptr,
              "aliases are copied and null-terminated");
        Check(dst.h_addr_list[0] != nullptr && dst.h_addr_list[1] != nullptr &&
                  dst.h_addr_list[2] == nullptr &&
                  std::memcmp(dst.h_addr_list[0], a1, 4) == 0 &&
                  std::memcmp(dst.h_addr_list[1], a2, 4) == 0,
              "addresses are copied in order");
        Check(dst.h_name >= buf && dst.h_name < buf + sizeof(buf) &&
                  reinterpret_cast<char*>(dst.h_addr_list[1]) < buf + sizeof(buf),
              "everything lives inside the caller's buffer");
        char tiny[16];
        Check(HostentCopy(src, 10, &dst, tiny, sizeof(tiny)) == 34, "a small buffer reports ERANGE");
        // A misaligned buffer still gets aligned pointer arrays.
        alignas(8) char raw[300];
        Check(HostentCopy(src, 10, &dst, raw + 3, sizeof(raw) - 3) == 0 &&
                  reinterpret_cast<uintptr_t>(dst.h_aliases) % sizeof(char*) == 0,
              "a misaligned buffer is aligned for the pointer arrays");
    }

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
