// libcore.io.KuNet: the socket and DNS calls behind java.net.
//
// Java passes Linux numbers (AF_INET6 is 10, SOL_SOCKET is 1, ...) and expects Linux errno
// values back, so every call goes through the same guest-facing shims a native library
// reaches, which translate for the host. Results are non-negative on success and
// -errno on failure. A blocking call drops the VM lock so other Java threads keep running.
#include "kudroid/kuart/LibCore.h"
#include "kudroid/abi/SocketAbi.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/kuart/DexValue.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/VmLock.h"

extern "C" {
int bionic_socket(int domain, int type, int protocol);
int bionic_bind(int fd, const struct sockaddr* addr, socklen_t len);
int bionic_connect(int fd, const struct sockaddr* addr, socklen_t len);
int bionic_listen(int fd, int backlog);
int bionic_accept4(int fd, struct sockaddr* addr, socklen_t* len, int flags);
int bionic_getsockname(int fd, struct sockaddr* addr, socklen_t* len);
int bionic_getpeername(int fd, struct sockaddr* addr, socklen_t* len);
int bionic_setsockopt(int fd, int level, int name, const void* value, socklen_t len);
int bionic_getsockopt(int fd, int level, int name, void* value, socklen_t* len);
int bionic_fcntl(int fd, int cmd, long arg);
int bionic_shutdown(int fd, int how);
int bionic_close(int fd);
ssize_t bionic_recv(int fd, void* buf, size_t len, int flags);
ssize_t bionic_send(int fd, const void* buf, size_t len, int flags);
ssize_t bionic_sendto(int fd, const void* buf, size_t len, int flags, const struct sockaddr* addr,
                      socklen_t addrlen);
ssize_t bionic_recvfrom(int fd, void* buf, size_t len, int flags, struct sockaddr* addr,
                        socklen_t* addrlen);
int bionic_ioctl(int fd, unsigned long request, void* arg);
int bionic_getaddrinfo(const char* node, const char* service, const kudroid::GuestAddrinfo* hints,
                       kudroid::GuestAddrinfo** res);
void bionic_freeaddrinfo(kudroid::GuestAddrinfo* res);
int bionic_getnameinfo(const struct sockaddr* sa, socklen_t salen, char* host, socklen_t hostlen,
                       char* serv, socklen_t servlen, int flags);
}

namespace kudroid {
namespace kuart {

namespace {

// Linux numbers, as Java and the shims use them.
constexpr int kEintr = 4;
constexpr int kEinval = 22;
constexpr int kEagain = 11;
constexpr int kEinprogress = 115;
constexpr int kEtimedout = 110;
constexpr int kSolSocket = 1;
constexpr int kSoError = 4;
constexpr int kSoLinger = 13;
constexpr int kONonblock = 0x800;
constexpr int kMsgNosignal = 0x4000;
constexpr unsigned long kFionread = 0x541B;
constexpr int kFGetfl = 3;
constexpr int kFSetfl = 4;

// Cap one transfer so a huge request does not need a huge temporary.
constexpr int kMaxChunk = 256 * 1024;

const char* Utf8(const DexValue& v) {
    if (v.l == nullptr) return "";
    auto* str = reinterpret_cast<DexString*>(v.l);
    return str->utf8 ? str->utf8 : "";
}

DexArray* Bytes(const DexValue& v) { return reinterpret_cast<DexArray*>(v.l); }

bool RangeOk(const DexArray* a, int off, int len) {
    return a != nullptr && off >= 0 && len >= 0 && off <= a->length && len <= a->length - off;
}

// sockaddr in Linux layout: family (2 bytes LE), port (BE), then the address.
size_t MakeSockaddr(const uint8_t* addr, int addrLen, int port, uint8_t out[28]) {
    std::memset(out, 0, 28);
    if (addrLen == 16) {
        out[0] = 10;
        out[2] = static_cast<uint8_t>((port >> 8) & 0xff);
        out[3] = static_cast<uint8_t>(port & 0xff);
        std::memcpy(out + 8, addr, 16);
        return 28;
    }
    out[0] = 2;
    out[2] = static_cast<uint8_t>((port >> 8) & 0xff);
    out[3] = static_cast<uint8_t>(port & 0xff);
    if (addrLen == 4) std::memcpy(out + 4, addr, 4);
    return 16;
}

// peer[0] = address length (4 or 16), peer[1..16] = address, peer[17..18] = port.
constexpr int kPeerBytes = 19;

void FillPeer(const uint8_t* sa, socklen_t len, DexArray* out) {
    if (out == nullptr || out->length < kPeerBytes || len < 2) return;
    uint8_t* d = out->Data();
    std::memset(d, 0, kPeerBytes);
    const unsigned family = sa[0];
    if (family == 2 && len >= 8) {
        d[0] = 4;
        std::memcpy(d + 1, sa + 4, 4);
    } else if (family == 10 && len >= 24) {
        d[0] = 16;
        std::memcpy(d + 1, sa + 8, 16);
    } else {
        return;
    }
    d[17] = sa[2];
    d[18] = sa[3];
}

// poll() on one fd. Returns 1 ready, 0 timeout, -errno.
int WaitFd(int fd, short events, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        int remaining = -1;
        if (timeoutMs >= 0) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  deadline - std::chrono::steady_clock::now())
                                  .count();
            remaining = left > 0 ? static_cast<int>(left) : 0;
        }
        struct pollfd p;
        p.fd = fd;
        p.events = events;
        p.revents = 0;
        const int rc = ::poll(&p, 1, remaining);
        if (rc > 0) return 1;
        if (rc == 0) return 0;
        if (errno != EINTR) return -errno;
    }
}

int DoConnect(int fd, const uint8_t* addr, int addrLen, int port, int timeoutMs) {
    uint8_t sa[28];
    const size_t len = MakeSockaddr(addr, addrLen, port, sa);
    if (timeoutMs <= 0) {
        for (;;) {
            if (bionic_connect(fd, reinterpret_cast<struct sockaddr*>(sa),
                               static_cast<socklen_t>(len)) == 0) {
                return 0;
            }
            if (errno != kEintr) return -errno;
        }
    }
    // Connect without blocking, then wait for the socket to become writable.
    const long flags = bionic_fcntl(fd, kFGetfl, 0);
    if (flags >= 0) bionic_fcntl(fd, kFSetfl, flags | kONonblock);
    int result = 0;
    if (bionic_connect(fd, reinterpret_cast<struct sockaddr*>(sa), static_cast<socklen_t>(len)) !=
        0) {
        if (errno != kEinprogress) {
            result = -errno;
        } else {
            const int ready = WaitFd(fd, POLLOUT, timeoutMs);
            if (ready == 0) {
                result = -kEtimedout;
            } else if (ready < 0) {
                result = ready;
            } else {
                int err = 0;
                socklen_t errLen = sizeof(err);
                if (bionic_getsockopt(fd, kSolSocket, kSoError, &err, &errLen) != 0) {
                    result = -errno;
                } else {
                    result = -err;
                }
            }
        }
    }
    if (flags >= 0) bionic_fcntl(fd, kFSetfl, flags);
    return result;
}

int DoRead(int fd, uint8_t* buf, int len, int timeoutMs) {
    if (timeoutMs > 0) {
        const int ready = WaitFd(fd, POLLIN, timeoutMs);
        if (ready == 0) return -kEtimedout;
        if (ready < 0) return ready;
    }
    for (;;) {
        const ssize_t n = bionic_recv(fd, buf, static_cast<size_t>(len), 0);
        if (n >= 0) return static_cast<int>(n);
        if (errno == kEintr) continue;
        if (errno == kEagain) {
            const int ready = WaitFd(fd, POLLIN, timeoutMs > 0 ? timeoutMs : -1);
            if (ready == 0) return -kEtimedout;
            if (ready < 0) return ready;
            continue;
        }
        return -errno;
    }
}

int DoWrite(int fd, const uint8_t* buf, int len) {
    int done = 0;
    while (done < len) {
        const ssize_t n =
            bionic_send(fd, buf + done, static_cast<size_t>(len - done), kMsgNosignal);
        if (n > 0) {
            done += static_cast<int>(n);
            continue;
        }
        if (n < 0 && errno == kEintr) continue;
        if (n < 0 && errno == kEagain) {
            const int ready = WaitFd(fd, POLLOUT, -1);
            if (ready < 0) return ready;
            continue;
        }
        return n < 0 ? -errno : -kEinval;
    }
    return done;
}

int DoSockName(int fd, DexArray* out, bool peer) {
    uint8_t sa[28];
    socklen_t len = sizeof(sa);
    const int rc = peer ? bionic_getpeername(fd, reinterpret_cast<struct sockaddr*>(sa), &len)
                        : bionic_getsockname(fd, reinterpret_cast<struct sockaddr*>(sa), &len);
    if (rc != 0) return -errno;
    FillPeer(sa, len, out);
    return 0;
}

// Resolves to up to out->length / 17 entries of {length, 16 address bytes}.
int DoResolve(const std::string& host, DexArray* out) {
    kudroid::GuestAddrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = 0;  // AF_UNSPEC
    hints.ai_socktype = 1;  // SOCK_STREAM: one entry per address
    kudroid::GuestAddrinfo* res = nullptr;
    const int rc = bionic_getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (rc != 0 || res == nullptr) {
        if (res != nullptr) bionic_freeaddrinfo(res);
        return -1;
    }
    const int capacity = out->length / 17;
    int count = 0;
    std::vector<std::vector<uint8_t>> seen;
    for (auto* ai = res; ai != nullptr && count < capacity; ai = ai->ai_next) {
        if (ai->ai_addr == nullptr) continue;
        const uint8_t* sa = reinterpret_cast<const uint8_t*>(ai->ai_addr);
        std::vector<uint8_t> entry(17, 0);
        if (sa[0] == 2 && ai->ai_addrlen >= 8) {
            entry[0] = 4;
            std::memcpy(&entry[1], sa + 4, 4);
        } else if (sa[0] == 10 && ai->ai_addrlen >= 24) {
            entry[0] = 16;
            std::memcpy(&entry[1], sa + 8, 16);
        } else {
            continue;
        }
        if (std::find(seen.begin(), seen.end(), entry) != seen.end()) continue;
        seen.push_back(entry);
        std::memcpy(out->Data() + static_cast<size_t>(count) * 17, entry.data(), 17);
        ++count;
    }
    bionic_freeaddrinfo(res);
    return count > 0 ? count : -1;
}

}  // namespace

bool Invoke_libcore_io_KuNet(Interpreter* interp, const char* name, const DexValue* args,
                             size_t num_args, DexValue* result) {
    const auto Is = [&](const char* n) { return std::strcmp(name, n) == 0; };

    if (Is("socket") && num_args >= 2) {
        int fd;
        {
            VmLockRelease unlocked;
            fd = bionic_socket(args[0].i, args[1].i, 0);
        }
        *result = DexValue::Int(fd >= 0 ? fd : -errno);
        return true;
    }
    if (Is("connect") && num_args >= 4) {
        DexArray* addr = Bytes(args[1]);
        if (addr == nullptr || (addr->length != 4 && addr->length != 16)) {
            *result = DexValue::Int(-kEinval);
            return true;
        }
        uint8_t copy[16];
        std::memcpy(copy, addr->Data(), static_cast<size_t>(addr->length));
        const int alen = addr->length;
        int rc;
        {
            VmLockRelease unlocked;
            rc = DoConnect(args[0].i, copy, alen, args[2].i, args[3].i);
        }
        *result = DexValue::Int(rc);
        return true;
    }
    if (Is("bind") && num_args >= 3) {
        DexArray* addr = Bytes(args[1]);
        uint8_t copy[16] = {0};
        int alen = 4;
        if (addr != nullptr && (addr->length == 4 || addr->length == 16)) {
            alen = addr->length;
            std::memcpy(copy, addr->Data(), static_cast<size_t>(alen));
        } else if (addr != nullptr) {
            *result = DexValue::Int(-kEinval);
            return true;
        }
        uint8_t sa[28];
        const size_t len = MakeSockaddr(copy, alen, args[2].i, sa);
        const int rc = bionic_bind(args[0].i, reinterpret_cast<struct sockaddr*>(sa),
                                   static_cast<socklen_t>(len));
        *result = DexValue::Int(rc == 0 ? 0 : -errno);
        return true;
    }
    if (Is("listen") && num_args >= 2) {
        const int rc = bionic_listen(args[0].i, args[1].i);
        *result = DexValue::Int(rc == 0 ? 0 : -errno);
        return true;
    }
    if (Is("accept") && num_args >= 3) {
        const int fd = args[0].i;
        const int timeoutMs = args[1].i;
        DexArray* peer = Bytes(args[2]);
        uint8_t sa[28];
        std::memset(sa, 0, sizeof(sa));
        socklen_t len = sizeof(sa);
        int accepted = -1;
        int err = 0;
        {
            VmLockRelease unlocked;
            int ready = 1;
            if (timeoutMs > 0) ready = WaitFd(fd, POLLIN, timeoutMs);
            if (ready == 0) {
                err = kEtimedout;
            } else if (ready < 0) {
                err = -ready;
            } else {
                for (;;) {
                    accepted = bionic_accept4(fd, reinterpret_cast<struct sockaddr*>(sa), &len, 0);
                    if (accepted >= 0 || errno != kEintr) break;
                }
                if (accepted < 0) err = errno;
            }
        }
        if (accepted >= 0) FillPeer(sa, len, peer);
        *result = DexValue::Int(accepted >= 0 ? accepted : -err);
        return true;
    }
    if (Is("read") && num_args >= 5) {
        DexArray* buf = Bytes(args[1]);
        const int off = args[2].i;
        const int len = args[3].i;
        if (!RangeOk(buf, off, len)) {
            *result = DexValue::Int(-kEinval);
            return true;
        }
        if (len == 0) {
            *result = DexValue::Int(0);
            return true;
        }
        std::vector<uint8_t> tmp(static_cast<size_t>(std::min(len, kMaxChunk)));
        int n;
        {
            VmLockRelease unlocked;
            n = DoRead(args[0].i, tmp.data(), static_cast<int>(tmp.size()), args[4].i);
        }
        if (n > 0) std::memcpy(buf->Data() + off, tmp.data(), static_cast<size_t>(n));
        *result = DexValue::Int(n);
        return true;
    }
    if (Is("write") && num_args >= 4) {
        DexArray* buf = Bytes(args[1]);
        const int off = args[2].i;
        const int len = args[3].i;
        if (!RangeOk(buf, off, len)) {
            *result = DexValue::Int(-kEinval);
            return true;
        }
        std::vector<uint8_t> tmp(buf->Data() + off, buf->Data() + off + len);
        int n;
        {
            VmLockRelease unlocked;
            n = DoWrite(args[0].i, tmp.data(), len);
        }
        *result = DexValue::Int(n);
        return true;
    }
    if (Is("close") && num_args >= 1) {
        const int fd = args[0].i;
        {
            VmLockRelease unlocked;
            // A thread blocked on this socket wakes on shutdown; close alone would not.
            bionic_shutdown(fd, 2);
            bionic_close(fd);
        }
        *result = DexValue::Int(0);
        return true;
    }
    if (Is("shutdown") && num_args >= 2) {
        const int rc = bionic_shutdown(args[0].i, args[1].i);
        *result = DexValue::Int(rc == 0 ? 0 : -errno);
        return true;
    }
    if (Is("sockname") && num_args >= 3) {
        *result = DexValue::Int(DoSockName(args[0].i, Bytes(args[1]), args[2].i != 0));
        return true;
    }
    if (Is("setOption") && num_args >= 4) {
        const int fd = args[0].i;
        const int level = args[1].i;
        const int opt = args[2].i;
        const int value = args[3].i;
        int rc;
        if (level == kSolSocket && opt == kSoLinger) {
            int linger[2] = {value >= 0 ? 1 : 0, value >= 0 ? value : 0};
            rc = bionic_setsockopt(fd, level, opt, linger, sizeof(linger));
        } else {
            rc = bionic_setsockopt(fd, level, opt, &value, sizeof(value));
        }
        *result = DexValue::Int(rc == 0 ? 0 : -errno);
        return true;
    }
    if (Is("getOption") && num_args >= 3) {
        const int fd = args[0].i;
        const int level = args[1].i;
        const int opt = args[2].i;
        int value[2] = {0, 0};
        socklen_t len = sizeof(value);
        if (bionic_getsockopt(fd, level, opt, value, &len) != 0) {
            *result = DexValue::Int(INT32_MIN);
        } else if (level == kSolSocket && opt == kSoLinger) {
            *result = DexValue::Int(value[0] != 0 ? value[1] : -1);
        } else {
            *result = DexValue::Int(value[0]);
        }
        return true;
    }
    if (Is("pollRead") && num_args >= 2) {
        // 1 when the socket is readable (data, end of stream or error), 0 on timeout.
        int ready;
        {
            VmLockRelease unlocked;
            ready = WaitFd(args[0].i, POLLIN, args[1].i);
        }
        *result = DexValue::Int(ready);
        return true;
    }
    if (Is("available") && num_args >= 1) {
        int pending = 0;
        const int rc = bionic_ioctl(args[0].i, kFionread, &pending);
        *result = DexValue::Int(rc == 0 ? pending : 0);
        return true;
    }
    if (Is("sendTo") && num_args >= 6) {
        DexArray* buf = Bytes(args[1]);
        const int off = args[2].i;
        const int len = args[3].i;
        DexArray* addr = Bytes(args[4]);
        if (!RangeOk(buf, off, len)) {
            *result = DexValue::Int(-kEinval);
            return true;
        }
        std::vector<uint8_t> tmp(buf->Data() + off, buf->Data() + off + len);
        uint8_t sa[28];
        size_t salen = 0;
        if (addr != nullptr && (addr->length == 4 || addr->length == 16)) {
            salen = MakeSockaddr(addr->Data(), addr->length, args[5].i, sa);
        }
        ssize_t n;
        {
            VmLockRelease unlocked;
            n = salen > 0 ? bionic_sendto(args[0].i, tmp.data(), tmp.size(), kMsgNosignal,
                                          reinterpret_cast<struct sockaddr*>(sa),
                                          static_cast<socklen_t>(salen))
                          : bionic_send(args[0].i, tmp.data(), tmp.size(), kMsgNosignal);
        }
        *result = DexValue::Int(n >= 0 ? static_cast<int>(n) : -errno);
        return true;
    }
    if (Is("recvFrom") && num_args >= 6) {
        DexArray* buf = Bytes(args[1]);
        const int off = args[2].i;
        const int len = args[3].i;
        const int timeoutMs = args[4].i;
        DexArray* peer = Bytes(args[5]);
        if (!RangeOk(buf, off, len)) {
            *result = DexValue::Int(-kEinval);
            return true;
        }
        std::vector<uint8_t> tmp(static_cast<size_t>(std::min(std::max(len, 1), kMaxChunk)));
        uint8_t sa[28];
        std::memset(sa, 0, sizeof(sa));
        socklen_t salen = sizeof(sa);
        ssize_t n = -1;
        int err = 0;
        {
            VmLockRelease unlocked;
            int ready = 1;
            if (timeoutMs > 0) ready = WaitFd(args[0].i, POLLIN, timeoutMs);
            if (ready == 0) {
                err = kEtimedout;
            } else if (ready < 0) {
                err = -ready;
            } else {
                for (;;) {
                    n = bionic_recvfrom(args[0].i, tmp.data(), tmp.size(), 0,
                                        reinterpret_cast<struct sockaddr*>(sa), &salen);
                    if (n >= 0 || errno != kEintr) break;
                }
                if (n < 0) err = errno;
            }
        }
        if (n >= 0) {
            const int copy = std::min(static_cast<int>(n), len);
            if (copy > 0) std::memcpy(buf->Data() + off, tmp.data(), static_cast<size_t>(copy));
            FillPeer(sa, salen, peer);
            *result = DexValue::Int(copy);
        } else {
            *result = DexValue::Int(-err);
        }
        return true;
    }
    if (Is("resolve") && num_args >= 2) {
        DexArray* out = Bytes(args[1]);
        if (out == nullptr || out->length < 17) {
            *result = DexValue::Int(-1);
            return true;
        }
        const std::string host = Utf8(args[0]);
        int rc;
        {
            VmLockRelease unlocked;
            rc = DoResolve(host, out);
        }
        *result = DexValue::Int(rc);
        return true;
    }
    if (Is("reverse") && num_args >= 2) {
        // addr (4 or 16 bytes) -> host name bytes in out; 0 when there is no name.
        DexArray* addr = Bytes(args[0]);
        DexArray* out = Bytes(args[1]);
        if (addr == nullptr || out == nullptr || (addr->length != 4 && addr->length != 16)) {
            *result = DexValue::Int(0);
            return true;
        }
        uint8_t sa[28];
        const size_t salen = MakeSockaddr(addr->Data(), addr->length, 0, sa);
        char host[1025];
        host[0] = '\0';
        int rc;
        {
            VmLockRelease unlocked;
            rc = bionic_getnameinfo(reinterpret_cast<struct sockaddr*>(sa),
                                    static_cast<socklen_t>(salen), host, sizeof(host), nullptr, 0,
                                    0);
        }
        const size_t n = rc == 0 ? std::strlen(host) : 0;
        if (n == 0 || static_cast<int>(n) > out->length) {
            *result = DexValue::Int(0);
            return true;
        }
        std::memcpy(out->Data(), host, n);
        *result = DexValue::Int(static_cast<int>(n));
        return true;
    }
    if (Is("hostName") && num_args >= 1) {
        DexArray* out = Bytes(args[0]);
        char host[256];
        host[0] = '\0';
        if (out == nullptr || ::gethostname(host, sizeof(host)) != 0) {
            *result = DexValue::Int(0);
            return true;
        }
        host[sizeof(host) - 1] = '\0';
        const size_t n = std::min(std::strlen(host), static_cast<size_t>(out->length));
        std::memcpy(out->Data(), host, n);
        *result = DexValue::Int(static_cast<int>(n));
        return true;
    }
    if ((Is("interfaceNames") && num_args == 0) ||
        (Is("interfaceAddresses") && num_args == 1)) {
        struct ifaddrs* head = nullptr;
        if (::getifaddrs(&head) != 0) {
            *result = DexValue::Ref(nullptr);
            return true;
        }
        std::unique_ptr<struct ifaddrs, decltype(&::freeifaddrs)> owned(head, ::freeifaddrs);
        std::vector<std::string> names;
        std::vector<std::vector<uint8_t>> rows;
        const bool want_names = Is("interfaceNames");
        const std::string requested = want_names ? "" : Utf8(args[0]);
        for (auto* entry = head; entry; entry = entry->ifa_next) {
            if (!entry->ifa_name) continue;
            if (want_names) {
                if (std::find(names.begin(), names.end(), entry->ifa_name) == names.end())
                    names.emplace_back(entry->ifa_name);
                continue;
            }
            if (requested != entry->ifa_name || !entry->ifa_addr || !entry->ifa_netmask) continue;
            const int family = entry->ifa_addr->sa_family;
            if ((family != AF_INET && family != AF_INET6) || entry->ifa_netmask->sa_family != family) continue;
            const size_t length = family == AF_INET ? 4 : 16;
            auto address_bytes = [family](const struct sockaddr* address) -> const uint8_t* {
                if (family == AF_INET)
                    return reinterpret_cast<const uint8_t*>(&reinterpret_cast<const sockaddr_in*>(address)->sin_addr);
                return reinterpret_cast<const uint8_t*>(&reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr);
            };
            const uint8_t* mask = address_bytes(entry->ifa_netmask);
            unsigned prefix = 0;
            bool zero_seen = false, valid = true;
            for (size_t i = 0; i < length; ++i) {
                for (int bit = 7; bit >= 0; --bit) {
                    if (mask[i] & (1u << bit)) { ++prefix; if (zero_seen) valid = false; }
                    else zero_seen = true;
                }
            }
            if (!valid) continue;
            std::vector<uint8_t> row(7 + length + 4, 0);
            row[0] = static_cast<uint8_t>(length);
            row[1] = static_cast<uint8_t>(prefix);
            if (family == AF_INET6) {
                const uint32_t scope = reinterpret_cast<const sockaddr_in6*>(entry->ifa_addr)->sin6_scope_id;
                for (unsigned i = 0; i < 4; ++i) row[3 + i] = static_cast<uint8_t>(scope >> (24 - 8 * i));
            }
            std::memcpy(row.data() + 7, address_bytes(entry->ifa_addr), length);
            if (family == AF_INET && (entry->ifa_flags & IFF_BROADCAST) && entry->ifa_broadaddr &&
                entry->ifa_broadaddr->sa_family == AF_INET) {
                row[2] = 1;
                std::memcpy(row.data() + 7 + length, address_bytes(entry->ifa_broadaddr), 4);
            }
            rows.push_back(std::move(row));
        }
        auto* linker = interp->linker();
        auto* array_class = linker->FindClass(want_names ? "[Ljava/lang/String;" : "[[B");
        auto* byte_class = want_names ? nullptr : linker->FindClass("[B");
        DexArray* array = array_class ? linker->AllocArray(array_class, static_cast<int32_t>(
            want_names ? names.size() : rows.size())) : nullptr;
        if (array) {
            auto** values = reinterpret_cast<DexObject**>(array->Data());
            for (int32_t i = 0; i < array->length; ++i) {
                if (want_names) values[i] = linker->InternString(names[i]);
                else {
                    DexArray* row = byte_class ? linker->AllocArray(byte_class, static_cast<int32_t>(rows[i].size())) : nullptr;
                    if (!row) { *result = DexValue::Ref(nullptr); return true; }
                    std::memcpy(row->Data(), rows[i].data(), rows[i].size());
                    values[i] = row;
                }
            }
        }
        *result = DexValue::Ref(array);
        return true;
    }
    if (Is("joinGroup") && num_args >= 3) {
        // (fd, group bytes (4), join): ip_mreq with the wildcard interface.
        DexArray* group = Bytes(args[1]);
        if (group == nullptr || group->length != 4) {
            *result = DexValue::Int(-kEinval);
            return true;
        }
        uint8_t mreq[8] = {0};
        std::memcpy(mreq, group->Data(), 4);
        const int opt = args[2].i != 0 ? 35 : 36;  // IP_ADD_MEMBERSHIP / IP_DROP_MEMBERSHIP
        const int rc = bionic_setsockopt(args[0].i, 0, opt, mreq, sizeof(mreq));
        *result = DexValue::Int(rc == 0 ? 0 : -errno);
        return true;
    }
    return false;
}

}  // namespace kuart
}  // namespace kudroid
