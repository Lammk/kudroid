// Address family translation between Linux guest and Darwin host ABI.
// Linux AF_INET6 is 10, Darwin AF_INET6 is 30.
namespace {
inline int guest_to_host_af(int af) {
#if defined(__APPLE__)
    if (af == 10 /* Linux AF_INET6 */) {
        return AF_INET6;
    }
#endif
    return af;
}
}  // namespace

// Writing to a closed peer raises SIGPIPE on Darwin unless the socket opts out, and a
// guest that expects EPIPE (or passes MSG_NOSIGNAL) must not be able to kill the host.
static inline void net_no_sigpipe(int fd) {
#if defined(__APPLE__)
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#else
    (void)fd;
#endif
}

extern "C" int bionic_socket(int domain, int type, int protocol) {
    const int host_domain = guest_to_host_af(domain);
    // Linux folds SOCK_NONBLOCK (0x800) and SOCK_CLOEXEC (0x80000) into the type; Darwin
    // has no such flags and would reject the call.
    const bool nonblock = (type & 0x800) != 0;
    const bool cloexec = (type & 0x80000) != 0;
    const int fd = ::socket(host_domain, type & 0xF, protocol);
    int err = errno;
    if (fd >= 0) {
        net_fd_set(fd, true);
        net_no_sigpipe(fd);
        if (nonblock) {
            const int fl = ::fcntl(fd, F_GETFL);
            if (fl >= 0) ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        }
        if (cloexec) ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    std::fprintf(stderr, "[KuDroidNet] socket(domain=%d type=%d) -> %d\n",
                 domain, type, fd);
    errno = err;
    return net_result(fd);
}

extern "C" int bionic_bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
#if defined(__APPLE__)
    struct sockaddr_storage ss;
    socklen_t sslen = sizeof(ss);
    if (linux_to_darwin_sockaddr(addr, addrlen, &ss, &sslen)) {
        if (ss.ss_family == AF_UNIX) {
            const char* sp = reinterpret_cast<struct sockaddr_un*>(&ss)->sun_path;
            struct stat st;
            if (::stat(sp, &st) == 0 && S_ISSOCK(st.st_mode)) ::unlink(sp);
        }
        return net_result(::bind(sockfd, reinterpret_cast<const struct sockaddr*>(&ss), sslen));
    }
    errno = EAFNOSUPPORT;
    return net_result(-1);
#else
    return net_result(::bind(sockfd, addr, addrlen));
#endif
}

extern "C" int bionic_getsockname(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
#if defined(__APPLE__)
    struct sockaddr_storage ss;
    socklen_t sslen = sizeof(ss);
    int rc = ::getsockname(sockfd, reinterpret_cast<struct sockaddr*>(&ss), &sslen);
    if (rc == 0 && addr && addrlen) {
        darwin_to_linux_sockaddr(reinterpret_cast<struct sockaddr*>(&ss), sslen, addr, addrlen);
    }
    return net_result(rc);
#else
    return ::getsockname(sockfd, addr, addrlen);
#endif
}

extern "C" int bionic_getpeername(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
#if defined(__APPLE__)
    struct sockaddr_storage ss;
    socklen_t sslen = sizeof(ss);
    int rc = ::getpeername(sockfd, reinterpret_cast<struct sockaddr*>(&ss), &sslen);
    if (rc == 0 && addr && addrlen) {
        darwin_to_linux_sockaddr(reinterpret_cast<struct sockaddr*>(&ss), sslen, addr, addrlen);
    }
    return net_result(rc);
#else
    return ::getpeername(sockfd, addr, addrlen);
#endif
}

extern "C" int bionic_connect(int sockfd, const struct sockaddr* addr,
                              socklen_t addrlen) {
    char dst[64] = {'?'};
    if (addr != nullptr && addrlen >= 2) {
        const unsigned char* b = reinterpret_cast<const unsigned char*>(addr);
        const unsigned fam = static_cast<unsigned>(b[0]) | (static_cast<unsigned>(b[1]) << 8);
        if (fam == 2 && addrlen >= 8) {
            const unsigned port = (static_cast<unsigned>(b[2]) << 8) | static_cast<unsigned>(b[3]);
            std::snprintf(dst, sizeof(dst), "%u.%u.%u.%u:%u", b[4], b[5], b[6], b[7], port);
        } else if (fam == 10 && addrlen >= 24) {
            char ipbuf[INET6_ADDRSTRLEN] = {'?'};
            ::inet_ntop(AF_INET6, b + 8, ipbuf, sizeof(ipbuf));
            const unsigned port = (static_cast<unsigned>(b[2]) << 8) | static_cast<unsigned>(b[3]);
            std::snprintf(dst, sizeof(dst), "[%s]:%u", ipbuf, port);
        } else {
            std::snprintf(dst, sizeof(dst), "family=%u", fam);
        }
    }
#if defined(__APPLE__)
    struct sockaddr_storage ss;
    socklen_t sslen = sizeof(ss);
    if (!linux_to_darwin_sockaddr(addr, addrlen, &ss, &sslen)) {
        errno = EAFNOSUPPORT;
        return net_result(-1);
    }
    const int ret = ::connect(sockfd, reinterpret_cast<const struct sockaddr*>(&ss), sslen);
#else
    const int ret = ::connect(sockfd, addr, addrlen);
#endif
    std::fprintf(stderr, "[KuDroidNet] connect(%s) -> %d (%s)\n", dst, ret,
                 ret == 0 ? "OK" : std::strerror(errno));
    return net_result(ret);
}

// ── socket calls the host libc would otherwise receive with Linux numbers ────────────

extern "C" int bionic_setsockopt(int fd, int level, int name, const void* value, socklen_t len) {
#if defined(__APPLE__)
    int hostLevel = 0, hostName = 0;
    if (!kudroid::SocketOptionToHost(level, name, &hostLevel, &hostName)) {
        // Unknown or Linux-specific socket option: accept and ignore (return 0)
        // libcurl, okhttp, and mono when they set SO_BINDTODEVICE, SO_MARK, etc.
        return 0;
    }
    return net_result(::setsockopt(fd, hostLevel, hostName, value, len));
#else
    return ::setsockopt(fd, level, name, value, len);
#endif
}

extern "C" int bionic_getsockopt(int fd, int level, int name, void* value, socklen_t* len) {
#if defined(__APPLE__)
    int hostLevel = 0, hostName = 0;
    if (!kudroid::SocketOptionToHost(level, name, &hostLevel, &hostName)) {
        errno = ENOPROTOOPT;
        return net_result(-1);
    }
    const int rc = ::getsockopt(fd, hostLevel, hostName, value, len);
    // SO_ERROR reports a pending errno, which the guest compares against Linux's numbers.
    if (rc == 0 && level == 1 && name == 4 && value != nullptr && len != nullptr &&
        *len >= sizeof(int)) {
        int pending = 0;
        std::memcpy(&pending, value, sizeof(pending));
        if (net_trace_take()) {
            std::fprintf(stderr, "[KuDroidNet] getsockopt(fd=%d, SO_ERROR) -> %d\n", fd, pending);
        }
        pending = kudroid::SocketErrnoToGuest(pending);
        std::memcpy(value, &pending, sizeof(pending));
    }
    return net_result(rc);
#else
    return ::getsockopt(fd, level, name, value, len);
#endif
}

// Not variadic here: the guest passes the argument in a register, and the host's variadic
// fcntl would read it from the stack.
extern "C" int bionic_fcntl(int fd, int cmd, long arg) {
#if defined(__APPLE__)
    const int hostCmd = kudroid::FcntlCommandToHost(cmd);
    if (hostCmd < 0) {
        errno = EINVAL;
        return -1;
    }
    switch (cmd) {
        case 3: {  // F_GETFL
            const int flags = ::fcntl(fd, F_GETFL);
            return flags < 0 ? net_result(flags) : kudroid::StatusFlagsToGuest(flags);
        }
        case 4:  // F_SETFL
            return net_result(::fcntl(fd, F_SETFL, kudroid::StatusFlagsToHost(static_cast<int>(arg))));
        case 5:    // F_GETLK
        case 6:    // F_SETLK
        case 7: {  // F_SETLKW
            auto* guest = reinterpret_cast<kudroid::LinuxFlock*>(arg);
            if (guest == nullptr) {
                errno = EFAULT;
                return -1;
            }
            kudroid::DarwinFlock translated;
            kudroid::FlockToHost(*guest, &translated);
            struct flock lock;
            std::memset(&lock, 0, sizeof(lock));
            lock.l_start = translated.l_start;
            lock.l_len = translated.l_len;
            lock.l_pid = translated.l_pid;
            lock.l_type = translated.l_type;
            lock.l_whence = translated.l_whence;
            const int rc = ::fcntl(fd, hostCmd, &lock);
            if (rc == 0 && cmd == 5) {
                translated.l_start = lock.l_start;
                translated.l_len = lock.l_len;
                translated.l_pid = lock.l_pid;
                translated.l_type = lock.l_type;
                translated.l_whence = lock.l_whence;
                kudroid::FlockToGuest(translated, guest);
            }
            return net_result(rc);
        }
        default:
            return net_result(::fcntl(fd, hostCmd, static_cast<int>(arg)));
    }
#else
    return ::fcntl(fd, cmd, arg);
#endif
}

extern "C" ssize_t bionic_send(int fd, const void* buf, size_t len, int flags) {
#if defined(__APPLE__)
    const ssize_t rc = ::send(fd, buf, len, kudroid::SocketMsgFlagsToHost(flags));
    if (net_trace_take()) {
        std::fprintf(stderr, "[KuDroidNet] send fd=%d len=%zu -> %zd errno=%d\n", fd, len, rc,
                     rc < 0 ? errno : 0);
    }
    return net_result(rc);
#else
    return ::send(fd, buf, len, flags);
#endif
}

extern "C" ssize_t bionic_recv(int fd, void* buf, size_t len, int flags) {
#if defined(__APPLE__)
    const ssize_t rc = ::recv(fd, buf, len, kudroid::SocketMsgFlagsToHost(flags));
    if (net_trace_take()) {
        std::fprintf(stderr, "[KuDroidNet] recv fd=%d len=%zu -> %zd errno=%d\n", fd, len, rc,
                     rc < 0 ? errno : 0);
    }
    return net_result(rc);
#else
    return ::recv(fd, buf, len, flags);
#endif
}

extern "C" ssize_t bionic_sendto(int fd, const void* buf, size_t len, int flags,
                                 const struct sockaddr* addr, socklen_t addrlen) {
#if defined(__APPLE__)
    const int hostFlags = kudroid::SocketMsgFlagsToHost(flags);
    if (addr != nullptr) {
        struct sockaddr_storage ss;
        socklen_t sslen = sizeof(ss);
        if (!linux_to_darwin_sockaddr(addr, addrlen, &ss, &sslen)) {
            errno = EAFNOSUPPORT;
            return net_result(-1);
        }
        return net_result(::sendto(fd, buf, len, hostFlags,
                                   reinterpret_cast<const struct sockaddr*>(&ss), sslen));
    }
    return net_result(::sendto(fd, buf, len, hostFlags, nullptr, 0));
#else
    return ::sendto(fd, buf, len, flags, addr, addrlen);
#endif
}

extern "C" ssize_t bionic_recvfrom(int fd, void* buf, size_t len, int flags,
                                   struct sockaddr* addr, socklen_t* addrlen) {
#if defined(__APPLE__)
    const int hostFlags = kudroid::SocketMsgFlagsToHost(flags);
    if (addr != nullptr && addrlen != nullptr) {
        struct sockaddr_storage ss;
        socklen_t sslen = sizeof(ss);
        const ssize_t rc = ::recvfrom(fd, buf, len, hostFlags,
                                      reinterpret_cast<struct sockaddr*>(&ss), &sslen);
        if (rc >= 0) {
            darwin_to_linux_sockaddr(reinterpret_cast<struct sockaddr*>(&ss), sslen, addr, addrlen);
        }
        return net_result(rc);
    }
    return net_result(::recvfrom(fd, buf, len, hostFlags, nullptr, nullptr));
#else
    return ::recvfrom(fd, buf, len, flags, addr, addrlen);
#endif
}

extern "C" ssize_t bionic___sendto_chk(int fd, const void* buf, size_t len, size_t buflen,
                                       int flags, const struct sockaddr* addr,
                                       socklen_t addrlen) {
    (void)buflen;
    return bionic_sendto(fd, buf, len, flags, addr, addrlen);
}

extern "C" ssize_t bionic___recvfrom_chk(int fd, void* buf, size_t len, size_t buflen,
                                         int flags, struct sockaddr* addr,
                                         socklen_t* addrlen) {
    (void)buflen;
    return bionic_recvfrom(fd, buf, len, flags, addr, addrlen);
}

extern "C" int bionic_accept4(int fd, struct sockaddr* addr, socklen_t* addrlen, int flags) {
#if defined(__APPLE__)
    int accepted;
    if (addr != nullptr && addrlen != nullptr) {
        struct sockaddr_storage ss;
        socklen_t sslen = sizeof(ss);
        accepted = ::accept(fd, reinterpret_cast<struct sockaddr*>(&ss), &sslen);
        if (accepted >= 0) {
            darwin_to_linux_sockaddr(reinterpret_cast<struct sockaddr*>(&ss), sslen, addr, addrlen);
        }
    } else {
        accepted = ::accept(fd, nullptr, nullptr);
    }
    if (accepted >= 0) {
        net_fd_set(accepted, true);
        net_no_sigpipe(accepted);
        if (flags & 0x800) {
            const int fl = ::fcntl(accepted, F_GETFL);
            if (fl >= 0) ::fcntl(accepted, F_SETFL, fl | O_NONBLOCK);
        }
        if (flags & 0x80000) ::fcntl(accepted, F_SETFD, FD_CLOEXEC);
    }
    return net_result(accepted);
#else
    return ::accept4(fd, addr, addrlen, flags);
#endif
}

extern "C" int bionic_accept(int fd, struct sockaddr* addr, socklen_t* addrlen) {
    return bionic_accept4(fd, addr, addrlen, 0);
}

extern "C" int bionic_listen(int fd, int backlog) {
    return net_result(::listen(fd, backlog));
}

extern "C" int bionic_shutdown(int fd, int how) {
    return net_result(::shutdown(fd, how));
}

extern "C" int bionic_socketpair(int domain, int type, int protocol, int sv[2]) {
#if defined(__APPLE__)
    // SOCK_NONBLOCK / SOCK_CLOEXEC ride in the type on Linux and do not exist here.
    const int rc = ::socketpair(guest_to_host_af(domain), type & 0xF, protocol, sv);
    const int err = errno;
    if (rc == 0) {
        for (int i = 0; i < 2; ++i) {
            net_no_sigpipe(sv[i]);
            if (type & 0x800) {
                const int fl = ::fcntl(sv[i], F_GETFL);
                if (fl >= 0) ::fcntl(sv[i], F_SETFL, fl | O_NONBLOCK);
            }
            if (type & 0x80000) ::fcntl(sv[i], F_SETFD, FD_CLOEXEC);
        }
    }
    errno = err;
    return net_result(rc);
#else
    return ::socketpair(domain, type, protocol, sv);
#endif
}

// Name resolution in reverse. The caller's sockaddr is Linux-shaped.
extern "C" int bionic_getnameinfo(const struct sockaddr* sa, socklen_t salen, char* host,
                                  socklen_t hostlen, char* serv, socklen_t servlen, int flags) {
    if (sa == nullptr) return 5;
#if defined(__APPLE__)
    struct sockaddr_storage ss;
    socklen_t sslen = sizeof(ss);
    if (!linux_to_darwin_sockaddr(sa, salen, &ss, &sslen)) return 5;
    const int rc = ::getnameinfo(reinterpret_cast<const struct sockaddr*>(&ss), sslen, host,
                                  hostlen, serv, servlen, flags);
    if (rc == EAI_SYSTEM) errno = guest_errno(errno);
    return rc;
#else
    const int hostFlags = kudroid::NameInfoFlagsToLinux(flags);
    if (hostFlags < 0) return 3;
    return kudroid::AddrInfoErrorToGuest(::getnameinfo(sa, salen, host, hostlen, serv, servlen,
                                                       hostFlags));
#endif
}

// hostent keeps its layout across the two libcs; only the AF_INET6 number differs.
static struct hostent* hostent_to_guest(struct hostent* h) {
#if defined(__APPLE__)
    if (h != nullptr && h->h_addrtype == AF_INET6) h->h_addrtype = 10;
#endif
    return h;
}

extern "C" struct hostent* bionic_gethostbyname(const char* name) {
    return hostent_to_guest(::gethostbyname(name));
}

extern "C" struct hostent* bionic_gethostbyname2(const char* name, int af) {
    return hostent_to_guest(::gethostbyname2(name, guest_to_host_af(af)));
}

extern "C" struct hostent* bionic_gethostbyaddr(const void* addr, socklen_t len, int af) {
    return hostent_to_guest(::gethostbyaddr(addr, len, guest_to_host_af(af)));
}

// Darwin has no gethostbyname_r, so it is built on getaddrinfo and copied into the
// caller's buffer.
extern "C" int bionic_gethostbyname_r(const char* name, struct hostent* ret, char* buf,
                                      size_t buflen, struct hostent** result, int* h_errnop) {
    if (result != nullptr) *result = nullptr;
    if (name == nullptr || ret == nullptr || buf == nullptr) {
        if (h_errnop != nullptr) *h_errnop = 1;  // HOST_NOT_FOUND
        return EINVAL;
    }
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;
    struct addrinfo* res = nullptr;
    if (::getaddrinfo(name, nullptr, &hints, &res) != 0 || res == nullptr) {
        if (res != nullptr) ::freeaddrinfo(res);
        if (h_errnop != nullptr) *h_errnop = 1;  // HOST_NOT_FOUND
        return 0;
    }
    std::vector<unsigned char> addrBytes;
    for (struct addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
        if (ai->ai_family != AF_INET || ai->ai_addr == nullptr) continue;
        const unsigned char* sa = reinterpret_cast<const unsigned char*>(ai->ai_addr);
        addrBytes.insert(addrBytes.end(), sa + 4, sa + 8);  // sin_addr
    }
    std::vector<char*> addrList;
    for (size_t i = 0; i + 4 <= addrBytes.size(); i += 4) {
        addrList.push_back(reinterpret_cast<char*>(&addrBytes[i]));
    }
    addrList.push_back(nullptr);
    char* aliases[1] = {nullptr};
    std::string canonical = res->ai_canonname != nullptr ? res->ai_canonname : name;
    kudroid::GuestHostent src;
    src.h_name = &canonical[0];
    src.h_aliases = aliases;
    src.h_addrtype = 2;
    src.h_length = 4;
    src.h_addr_list = addrList.data();
    ::freeaddrinfo(res);
    kudroid::GuestHostent out;
    const int rc = kudroid::HostentCopy(src, 2, &out, buf, buflen);
    if (rc != 0) {
        if (h_errnop != nullptr) *h_errnop = 0;
        return rc;
    }
    std::memcpy(ret, &out, sizeof(out));
    if (result != nullptr) *result = ret;
    return 0;
}

// ppoll/pselect take a timespec and a signal mask; the mask is ignored, and a nanosecond
// timeout is rounded up to whole milliseconds so a short wait never becomes a spin.
extern "C" int bionic_ppoll(struct pollfd* fds, unsigned long nfds, const struct timespec* tmo,
                            const void* sigmask) {
    (void)sigmask;
    int timeoutMs = -1;
    if (tmo != nullptr) {
        const long long ms = static_cast<long long>(tmo->tv_sec) * 1000 +
                             (static_cast<long long>(tmo->tv_nsec) + 999999) / 1000000;
        timeoutMs = ms > 0x7fffffff ? 0x7fffffff : static_cast<int>(ms);
    }
    return bionic_poll(fds, nfds, timeoutMs);
}

extern "C" int bionic_pselect(int nfds, void* readfds, void* writefds, void* exceptfds,
                              const struct timespec* tmo, const void* sigmask) {
    (void)sigmask;
    const int rc = ::pselect(nfds, static_cast<fd_set*>(readfds), static_cast<fd_set*>(writefds),
                             static_cast<fd_set*>(exceptfds), tmo, nullptr);
    return net_result(rc);
}

extern "C" ssize_t bionic_readv(int fd, const struct iovec* iov, int iovcnt) {
    const ssize_t rc = ::readv(fd, iov, iovcnt);
    net_trace_vector_io("readv", fd, static_cast<size_t>(std::max(0, iovcnt)), rc);
    return net_result(rc);
}

extern "C" ssize_t bionic_writev(int fd, const struct iovec* iov, int iovcnt) {
    const ssize_t rc = ::writev(fd, iov, iovcnt);
    net_trace_vector_io("writev", fd, static_cast<size_t>(std::max(0, iovcnt)), rc);
    return net_result(rc);
}

// Unlike dup2, dup3 fails when old equals new and honors O_CLOEXEC; Darwin has no dup3.
extern "C" int bionic_dup3(int oldfd, int newfd, int flags) {
    if (oldfd == newfd) {
        errno = EINVAL;
        return -1;
    }
    const int rc = ::dup2(oldfd, newfd);
    if (rc >= 0 && (flags & 0x80000) != 0) {  // O_CLOEXEC
        const int fl = ::fcntl(newfd, F_GETFD);
        if (fl >= 0) ::fcntl(newfd, F_SETFD, fl | FD_CLOEXEC);
    }
    return rc;
}

struct GuestMsghdr {
    void* msg_name;
    uint32_t msg_namelen;
    uint32_t __pad;
    struct iovec* msg_iov;
    size_t msg_iovlen;
    void* msg_control;
    size_t msg_controllen;
    int msg_flags;
};

struct GuestMmsghdr {
    GuestMsghdr msg_hdr;
    unsigned int msg_len;
};

namespace {
inline int guest_msg_flags_to_host(int flags) {
#if defined(__APPLE__)
    return kudroid::SocketMsgFlagsToHost(flags);
#else
    return flags;
#endif
}
}  // namespace

extern "C" ssize_t bionic_sendmsg(int fd, const GuestMsghdr* g, int flags) {
    if (!g) {
        errno = EFAULT;
        return -1;
    }
    struct msghdr h;
    std::memset(&h, 0, sizeof(h));
#if defined(__APPLE__)
    struct sockaddr_storage ss;
    socklen_t sslen = sizeof(ss);
    if (g->msg_name && g->msg_namelen > 0) {
        if (linux_to_darwin_sockaddr(reinterpret_cast<const struct sockaddr*>(g->msg_name),
                                     g->msg_namelen, &ss, &sslen)) {
            h.msg_name = &ss;
            h.msg_namelen = sslen;
        } else {
            errno = EAFNOSUPPORT;
            return -1;
        }
    }
#else
    h.msg_name = g->msg_name;
    h.msg_namelen = g->msg_namelen;
#endif
    h.msg_iov = g->msg_iov;
    h.msg_iovlen = static_cast<decltype(h.msg_iovlen)>(g->msg_iovlen);
    const ssize_t rc = ::sendmsg(fd, &h, guest_msg_flags_to_host(flags));
    net_trace_vector_io("sendmsg", fd, g->msg_iovlen, rc);
    return net_result(rc);
}

extern "C" ssize_t bionic_recvmsg(int fd, GuestMsghdr* g, int flags) {
    if (!g) {
        errno = EFAULT;
        return -1;
    }
    struct msghdr h;
    std::memset(&h, 0, sizeof(h));
#if defined(__APPLE__)
    struct sockaddr_storage ss;
    if (g->msg_name && g->msg_namelen > 0) {
        h.msg_name = &ss;
        h.msg_namelen = sizeof(ss);
    }
#else
    h.msg_name = g->msg_name;
    h.msg_namelen = g->msg_namelen;
#endif
    h.msg_iov = g->msg_iov;
    h.msg_iovlen = static_cast<decltype(h.msg_iovlen)>(g->msg_iovlen);

    const ssize_t rc = ::recvmsg(fd, &h, guest_msg_flags_to_host(flags));
    net_trace_vector_io("recvmsg", fd, g->msg_iovlen, rc);
    if (rc >= 0) {
#if defined(__APPLE__)
        if (g->msg_name && g->msg_namelen > 0) {
            darwin_to_linux_sockaddr(reinterpret_cast<struct sockaddr*>(&ss), h.msg_namelen,
                                     reinterpret_cast<struct sockaddr*>(g->msg_name), &g->msg_namelen);
        }
#else
        g->msg_namelen = h.msg_namelen;
#endif
        g->msg_controllen = 0;
        g->msg_flags = h.msg_flags;
    }
    return net_result(rc);
}

extern "C" int bionic_sendmmsg(int fd, GuestMmsghdr* v, unsigned int vlen, int flags) {
    if (!v && vlen > 0) {
        errno = EFAULT;
        return -1;
    }
    unsigned int count = 0;
    for (; count < vlen; ++count) {
        const ssize_t r = bionic_sendmsg(fd, &v[count].msg_hdr, flags);
        if (r < 0) {
            return count > 0 ? static_cast<int>(count) : -1;
        }
        v[count].msg_len = static_cast<unsigned int>(r);
    }
    return static_cast<int>(count);
}

extern "C" int bionic_recvmmsg(int fd, GuestMmsghdr* v, unsigned int vlen, int flags, void* timeout) {
    (void)timeout;
    if (!v && vlen > 0) {
        errno = EFAULT;
        return -1;
    }
    unsigned int count = 0;
    for (; count < vlen; ++count) {
        const int cur_flags = (count > 0) ? (flags | 0x40) : flags;
        const ssize_t r = bionic_recvmsg(fd, &v[count].msg_hdr, cur_flags);
        if (r < 0) {
            return count > 0 ? static_cast<int>(count) : -1;
        }
        v[count].msg_len = static_cast<unsigned int>(r);
    }
    return static_cast<int>(count);
}

extern "C" const char* bionic_inet_ntop(int af, const void* src, char* dst,
                                        socklen_t size) {
    const int host_af = guest_to_host_af(af);
    return ::inet_ntop(host_af, src, dst, size);
}

extern "C" int bionic_inet_pton(int af, const char* src, void* dst) {
    const int host_af = guest_to_host_af(af);
    return ::inet_pton(host_af, src, dst);
}

extern "C" int bionic_getaddrinfo(const char* node, const char* service,
                                  const kudroid::GuestAddrinfo* hints,
                                  kudroid::GuestAddrinfo** res) {
    if (res == nullptr) return 8;
    *res = nullptr;

    struct addrinfo host_hints{};
    if (hints != nullptr) {
#if defined(__APPLE__)
        host_hints.ai_flags = kudroid::AddrInfoFlagsToHost(hints->ai_flags);
#else
        host_hints.ai_flags = kudroid::AddrInfoFlagsToLinux(hints->ai_flags);
#endif
        if (host_hints.ai_flags < 0) return 3;
        host_hints.ai_family = guest_to_host_af(hints->ai_family);
        host_hints.ai_socktype = hints->ai_socktype & 0xF;
        host_hints.ai_protocol = hints->ai_protocol;
    }

    struct addrinfo* dres = nullptr;
    const int ret = ::getaddrinfo(node, service, &host_hints, &dres);
    const int saved_errno = errno;
    std::fprintf(stderr, "[KuDroidNet] getaddrinfo(%s,%s) -> %d (%s)\n",
                 node ? node : "<null>", service ? service : "<null>", ret,
                 ret == 0 ? "OK" : gai_strerror(ret));
#if defined(__APPLE__)
    const int guest_result = ret;
#else
    const int guest_result = kudroid::AddrInfoErrorToGuest(ret);
#endif
    if (guest_result != 0) {
        if (guest_result == 11) errno = guest_errno(saved_errno);
        return guest_result;
    }

    kudroid::GuestAddrinfo* head = nullptr;
    kudroid::GuestAddrinfo** tail = &head;
    for (struct addrinfo* a = dres; a != nullptr; a = a->ai_next) {
        if (a->ai_family != AF_INET && a->ai_family != AF_INET6 && a->ai_family != AF_UNIX) continue;
        const size_t canon_len = a->ai_canonname ? std::strlen(a->ai_canonname) + 1 : 0;
        uint8_t* blk = reinterpret_cast<uint8_t*>(std::calloc(1, sizeof(kudroid::GuestAddrinfo) + 128 + canon_len));
        if (!blk) {
            while (head != nullptr) {
                auto* next = head->ai_next;
                std::free(head);
                head = next;
            }
            ::freeaddrinfo(dres);
            return 6;
        }
        auto* g = reinterpret_cast<kudroid::GuestAddrinfo*>(blk);
        g->ai_flags = hints != nullptr ? hints->ai_flags : 0;
        g->ai_family = (a->ai_family == AF_INET6 ? 10 : (a->ai_family == AF_INET ? 2 : a->ai_family));
        g->ai_socktype = a->ai_socktype;
        g->ai_protocol = a->ai_protocol;

        auto* sa = reinterpret_cast<struct sockaddr*>(blk + sizeof(kudroid::GuestAddrinfo));
#if defined(__APPLE__)
        socklen_t sa_cap = 128;
        darwin_to_linux_sockaddr(a->ai_addr, a->ai_addrlen, sa, &sa_cap);
        g->ai_addrlen = sa_cap;
#else
        std::memcpy(sa, a->ai_addr, a->ai_addrlen);
        g->ai_addrlen = a->ai_addrlen;
#endif
        g->ai_addr = sa;

        if (canon_len > 0) {
            char* canon = reinterpret_cast<char*>(blk + sizeof(kudroid::GuestAddrinfo) + 128);
            std::memcpy(canon, a->ai_canonname, canon_len);
            g->ai_canonname = canon;
        }

        *tail = g;
        tail = &g->ai_next;
    }
    ::freeaddrinfo(dres);
    if (!head) return 8;
    *res = head;
    return 0;
}

extern "C" int bionic_android_getaddrinfofornet(const char* node, const char* service,
                                               const kudroid::GuestAddrinfo* hints,
                                               unsigned netid, unsigned mark,
                                               kudroid::GuestAddrinfo** res) {
    (void)netid;
    (void)mark;
    return bionic_getaddrinfo(node, service, hints, res);
}

extern "C" void bionic_freeaddrinfo(kudroid::GuestAddrinfo* res) {
    auto* g = res;
    while (g != nullptr) {
        auto* next = g->ai_next;
        std::free(g);
        g = next;
    }
}

extern "C" const char* bionic_gai_strerror(int errcode) {
#if !defined(__APPLE__)
    errcode = kudroid::AddrInfoErrorToLinux(errcode);
#endif
    return ::gai_strerror(errcode);
}

extern "C" unsigned int bionic_if_nametoindex(const char* ifname) {
    return ::if_nametoindex(ifname);
}

extern "C" uint16_t bionic_htons(uint16_t v) { return __builtin_bswap16(v); }
extern "C" uint32_t bionic_htonl(uint32_t v) { return __builtin_bswap32(v); }

