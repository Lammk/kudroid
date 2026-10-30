#include "kudroid/abi/SocketAbi.h"

#include <string.h>

namespace kudroid {

namespace {

// Linux numbers.
constexpr int kLinuxSolSocket = 1;
constexpr int kLinuxIpprotoIp = 0;
constexpr int kLinuxIpprotoTcp = 6;
constexpr int kLinuxIpprotoIpv6 = 41;

// Darwin numbers.
constexpr int kDarwinSolSocket = 0xffff;

}  // namespace

int SocketErrnoToGuest(int e) {
    switch (e) {
        case 35: return 11;    // EAGAIN
        case 36: return 115;   // EINPROGRESS
        case 37: return 114;   // EALREADY
        case 38: return 88;    // ENOTSOCK
        case 39: return 89;    // EDESTADDRREQ
        case 40: return 90;    // EMSGSIZE
        case 41: return 91;    // EPROTOTYPE
        case 42: return 92;    // ENOPROTOOPT
        case 43: return 93;    // EPROTONOSUPPORT
        case 44: return 94;    // ESOCKTNOSUPPORT
        case 45: return 95;    // ENOTSUP
        case 46: return 96;    // EPFNOSUPPORT
        case 47: return 97;    // EAFNOSUPPORT
        case 48: return 98;    // EADDRINUSE
        case 49: return 99;    // EADDRNOTAVAIL
        case 50: return 100;   // ENETDOWN
        case 51: return 101;   // ENETUNREACH
        case 52: return 102;   // ENETRESET
        case 53: return 103;   // ECONNABORTED
        case 54: return 104;   // ECONNRESET
        case 55: return 105;   // ENOBUFS
        case 56: return 106;   // EISCONN
        case 57: return 107;   // ENOTCONN
        case 58: return 108;   // ESHUTDOWN
        case 60: return 110;   // ETIMEDOUT
        case 61: return 111;   // ECONNREFUSED
        case 64: return 112;   // EHOSTDOWN
        case 65: return 113;   // EHOSTUNREACH
        case 102: return 95;   // EOPNOTSUPP
        default: return e;
    }
}

bool SocketOptionToHost(int level, int name, int* hostLevel, int* hostName) {
    if (hostLevel == nullptr || hostName == nullptr) return false;
    switch (level) {
        case kLinuxSolSocket:
            *hostLevel = kDarwinSolSocket;
            switch (name) {
                case 1: *hostName = 0x1; return true;       // SO_DEBUG
                case 2: *hostName = 0x4; return true;       // SO_REUSEADDR
                case 3: *hostName = 0x1008; return true;    // SO_TYPE
                case 4: *hostName = 0x1007; return true;    // SO_ERROR
                case 5: *hostName = 0x10; return true;      // SO_DONTROUTE
                case 6: *hostName = 0x20; return true;      // SO_BROADCAST
                case 7: *hostName = 0x1001; return true;    // SO_SNDBUF
                case 8: *hostName = 0x1002; return true;    // SO_RCVBUF
                case 9: *hostName = 0x8; return true;       // SO_KEEPALIVE
                case 10: *hostName = 0x100; return true;    // SO_OOBINLINE
                case 13: *hostName = 0x80; return true;     // SO_LINGER
                case 15: *hostName = 0x200; return true;    // SO_REUSEPORT
                case 18: *hostName = 0x1004; return true;   // SO_RCVLOWAT
                case 19: *hostName = 0x1003; return true;   // SO_SNDLOWAT
                case 20: *hostName = 0x1006; return true;   // SO_RCVTIMEO
                case 21: *hostName = 0x1005; return true;   // SO_SNDTIMEO
                case 30: *hostName = 0x2; return true;      // SO_ACCEPTCONN
                default: return false;
            }
        case kLinuxIpprotoTcp:
            *hostLevel = kLinuxIpprotoTcp;
            switch (name) {
                case 1: *hostName = 0x1; return true;       // TCP_NODELAY
                case 2: *hostName = 0x2; return true;       // TCP_MAXSEG
                case 3: *hostName = 0x4; return true;       // TCP_CORK -> TCP_NOPUSH
                case 4: *hostName = 0x10; return true;      // TCP_KEEPIDLE -> TCP_KEEPALIVE
                case 5: *hostName = 0x101; return true;     // TCP_KEEPINTVL
                case 6: *hostName = 0x102; return true;     // TCP_KEEPCNT
                default: return false;
            }
        case kLinuxIpprotoIp:
            *hostLevel = kLinuxIpprotoIp;
            switch (name) {
                case 1: *hostName = 3; return true;         // IP_TOS
                case 2: *hostName = 4; return true;         // IP_TTL
                case 3: *hostName = 2; return true;         // IP_HDRINCL
                case 4: *hostName = 1; return true;         // IP_OPTIONS
                case 8: *hostName = 26; return true;        // IP_PKTINFO
                case 12: *hostName = 24; return true;       // IP_RECVTTL
                case 13: *hostName = 27; return true;       // IP_RECVTOS
                case 32: *hostName = 9; return true;        // IP_MULTICAST_IF
                case 33: *hostName = 10; return true;       // IP_MULTICAST_TTL
                case 34: *hostName = 11; return true;       // IP_MULTICAST_LOOP
                case 35: *hostName = 12; return true;       // IP_ADD_MEMBERSHIP
                case 36: *hostName = 13; return true;       // IP_DROP_MEMBERSHIP
                default: return false;
            }
        case kLinuxIpprotoIpv6:
            *hostLevel = kLinuxIpprotoIpv6;
            switch (name) {
                case 16: *hostName = 4; return true;        // IPV6_UNICAST_HOPS
                case 17: *hostName = 9; return true;        // IPV6_MULTICAST_IF
                case 18: *hostName = 10; return true;       // IPV6_MULTICAST_HOPS
                case 19: *hostName = 11; return true;       // IPV6_MULTICAST_LOOP
                case 20: *hostName = 12; return true;       // IPV6_JOIN_GROUP
                case 21: *hostName = 13; return true;       // IPV6_LEAVE_GROUP
                case 26: *hostName = 27; return true;       // IPV6_V6ONLY
                case 49: *hostName = 61; return true;       // IPV6_RECVPKTINFO
                case 50: *hostName = 46; return true;       // IPV6_PKTINFO
                case 66: *hostName = 35; return true;       // IPV6_RECVTCLASS
                case 67: *hostName = 36; return true;       // IPV6_TCLASS
                default: return false;
            }
        default:
            return false;
    }
}

int SocketMsgFlagsToHost(int f) {
    int host = 0;
    if (f & 0x1) host |= 0x1;         // MSG_OOB
    if (f & 0x2) host |= 0x2;         // MSG_PEEK
    if (f & 0x4) host |= 0x4;         // MSG_DONTROUTE
    if (f & 0x20) host |= 0x10;       // MSG_TRUNC
    if (f & 0x40) host |= 0x80;       // MSG_DONTWAIT
    if (f & 0x80) host |= 0x8;        // MSG_EOR
    if (f & 0x100) host |= 0x40;      // MSG_WAITALL
    if (f & 0x4000) host |= 0x80000;  // MSG_NOSIGNAL
    return host;
}

int StatusFlagsToHost(int f) {
    int host = f & 3;                 // O_ACCMODE
    if (f & 0x800) host |= 0x4;       // O_NONBLOCK
    if (f & 0x400) host |= 0x8;       // O_APPEND
    if (f & 0x2000) host |= 0x40;     // O_ASYNC
    return host;
}

int StatusFlagsToGuest(int f) {
    int guest = f & 3;
    if (f & 0x4) guest |= 0x800;
    if (f & 0x8) guest |= 0x400;
    if (f & 0x40) guest |= 0x2000;
    return guest;
}

int FcntlCommandToHost(int c) {
    switch (c) {
        case 0: return 0;        // F_DUPFD
        case 1: return 1;        // F_GETFD
        case 2: return 2;        // F_SETFD
        case 3: return 3;        // F_GETFL
        case 4: return 4;        // F_SETFL
        case 5: return 7;        // F_GETLK
        case 6: return 8;        // F_SETLK
        case 7: return 9;        // F_SETLKW
        case 8: return 6;        // F_SETOWN
        case 9: return 5;        // F_GETOWN
        case 1030: return 67;    // F_DUPFD_CLOEXEC
        default: return -1;
    }
}

namespace {

int16_t LockTypeToHost(int16_t t) {
    switch (t) {
        case 0: return 1;  // F_RDLCK
        case 1: return 3;  // F_WRLCK
        default: return 2; // F_UNLCK
    }
}

int16_t LockTypeToGuest(int16_t t) {
    switch (t) {
        case 1: return 0;
        case 3: return 1;
        default: return 2;
    }
}

}  // namespace

void FlockToHost(const LinuxFlock& g, DarwinFlock* h) {
    h->l_start = g.l_start;
    h->l_len = g.l_len;
    h->l_pid = g.l_pid;
    h->l_type = LockTypeToHost(g.l_type);
    h->l_whence = g.l_whence;
}

void FlockToGuest(const DarwinFlock& h, LinuxFlock* g) {
    g->l_type = LockTypeToGuest(h.l_type);
    g->l_whence = h.l_whence;
    g->l_start = h.l_start;
    g->l_len = h.l_len;
    g->l_pid = h.l_pid;
}

static_assert(offsetof(GuestAddrinfo, ai_canonname) == 24, "GuestAddrinfo ai_canonname offset");
static_assert(offsetof(GuestAddrinfo, ai_addr) == 32, "GuestAddrinfo ai_addr offset");
static_assert(offsetof(GuestAddrinfo, ai_next) == 40, "GuestAddrinfo ai_next offset");
static_assert(sizeof(GuestAddrinfo) == 48, "GuestAddrinfo size");

int AddrInfoFlagsToHost(int f) {
    if (f & ~0xf0f) return -1;
    int host = f & ~0x8;
    if (f & 0x8) host |= 0x1000;  // AI_NUMERICSERV
    return host;
}

int AddrInfoFlagsToLinux(int f) {
    if (f & ~0xd0f) return -1;
    int host = f & 0x7;
    if (f & 0x8) host |= 0x400;  // AI_NUMERICSERV
    if (f & 0x100) host |= 0x10; // AI_ALL
    if (f & 0x400) host |= 0x20; // AI_ADDRCONFIG
    if (f & 0x800) host |= 0x8;  // AI_V4MAPPED
    return host;
}

int NameInfoFlagsToLinux(int f) {
    if (f & ~0x1f) return -1;
    int host = f & 0x10;       // NI_DGRAM
    if (f & 0x1) host |= 0x4;  // NI_NOFQDN
    if (f & 0x2) host |= 0x1;  // NI_NUMERICHOST
    if (f & 0x4) host |= 0x8;  // NI_NAMEREQD
    if (f & 0x8) host |= 0x2;  // NI_NUMERICSERV
    return host;
}

int AddrInfoErrorToGuest(int e) {
    switch (e) {
        case 0: return 0;
        case -1: return 3;
        case -2: return 8;
        case -3: return 2;
        case -4: return 4;
        case -5: return 7;
        case -6: return 5;
        case -7: return 10;
        case -8: return 9;
        case -9: return 1;
        case -10: return 6;
        case -11: return 11;
        case -12: return 14;
        default: return 4;
    }
}

int AddrInfoErrorToLinux(int e) {
    switch (e) {
        case 0: return 0;
        case 1: return -9;
        case 2: return -3;
        case 3: return -1;
        case 4: return -4;
        case 5: return -6;
        case 6: return -10;
        case 7: return -5;
        case 8: return -2;
        case 9: return -8;
        case 10: return -7;
        case 11: return -11;
        case 14: return -12;
        default: return e;
    }
}

int HostentCopy(const GuestHostent& src, int guestAddrType, GuestHostent* dst, char* buf,
                size_t buflen) {
    if (dst == nullptr || buf == nullptr) return 34;
    size_t aliasCount = 0;
    size_t addrCount = 0;
    size_t stringBytes = (src.h_name != nullptr ? strlen(src.h_name) + 1 : 0);
    for (; src.h_aliases != nullptr && src.h_aliases[aliasCount] != nullptr; ++aliasCount) {
        stringBytes += strlen(src.h_aliases[aliasCount]) + 1;
    }
    while (src.h_addr_list != nullptr && src.h_addr_list[addrCount] != nullptr) ++addrCount;
    const size_t addrLen = src.h_length > 0 ? static_cast<size_t>(src.h_length) : 0;

    // Pointer arrays first, so they land on the 8-byte boundary they need.
    const size_t misalign = reinterpret_cast<uintptr_t>(buf) % sizeof(char*);
    const size_t pad = misalign == 0 ? 0 : sizeof(char*) - misalign;
    const size_t arrayBytes = (aliasCount + 1 + addrCount + 1) * sizeof(char*);
    const size_t need = pad + arrayBytes + stringBytes + addrCount * addrLen;
    if (need > buflen) return 34;

    char* cursor = buf + pad;
    char** aliases = reinterpret_cast<char**>(cursor);
    char** addrs = aliases + aliasCount + 1;
    cursor += arrayBytes;

    char* name = nullptr;
    if (src.h_name != nullptr) {
        const size_t n = strlen(src.h_name) + 1;
        memcpy(cursor, src.h_name, n);
        name = cursor;
        cursor += n;
    }
    for (size_t i = 0; i < aliasCount; ++i) {
        const size_t n = strlen(src.h_aliases[i]) + 1;
        memcpy(cursor, src.h_aliases[i], n);
        aliases[i] = cursor;
        cursor += n;
    }
    aliases[aliasCount] = nullptr;
    for (size_t i = 0; i < addrCount; ++i) {
        memcpy(cursor, src.h_addr_list[i], addrLen);
        addrs[i] = cursor;
        cursor += addrLen;
    }
    addrs[addrCount] = nullptr;

    dst->h_name = name;
    dst->h_aliases = aliases;
    dst->h_addrtype = guestAddrType;
    dst->h_length = src.h_length;
    dst->h_addr_list = addrs;
    return 0;
}

}  // namespace kudroid
