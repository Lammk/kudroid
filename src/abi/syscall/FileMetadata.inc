namespace kudroid {
namespace {

// The third argument is a fixed parameter: a Linux guest passes it in a register, while a
// host varargs read would take it from the stack.
extern "C" int bionic_ioctl(int fd, unsigned long request, void* arg) {
#if defined(__APPLE__)
    // Linux FIONBIO / FIONREAD on sockets and pipes.
    if (request == 0x5421 /* FIONBIO */ && arg != nullptr) {
        const int fl = ::fcntl(fd, F_GETFL);
        if (fl < 0) return -1;
        const int next = *static_cast<int*>(arg) ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK);
        return ::fcntl(fd, F_SETFL, next);
    }
    if (request == 0x541B /* FIONREAD */ && arg != nullptr) {
        return ::ioctl(fd, FIONREAD, arg);
    }
#else
    if ((request == 0x5421 || request == 0x541B) && arg != nullptr) {
        return ::ioctl(fd, request, arg);
    }
#endif
    // Unsupported ioctl: return explicit ENOTTY instead of false 0 success.
    (void)request; (void)fd; (void)arg;
    errno = ENOTTY;
    return -1;
}

#define PR_SET_NAME 15
#define PR_GET_NAME 16
#define PR_SET_VMA  0x53564d41

#ifdef __APPLE__
// Scheduling class from the thread's role as its name states it: the frame loop and
// the render thread must not share an E-core with loaders and workers. Unnamed or
// unrecognised threads keep the default class.
static void apply_thread_qos_for_name(const char* name) {
    if (!name) return;
    qos_class_t qos = QOS_CLASS_UNSPECIFIED;
    if (std::strstr(name, "Background") || std::strstr(name, "Loading.") ||
        std::strstr(name, "GarbageCollector") || std::strstr(name, "BatchDelete") ||
        std::strstr(name, "Finalizer")) {
        qos = QOS_CLASS_UTILITY;
    } else if (std::strstr(name, "UnityMain") || std::strstr(name, "UnityGfx") ||
               std::strstr(name, "Swappy") || std::strstr(name, "RenderThread") ||
               std::strstr(name, "GLThread") || std::strstr(name, "mixer")) {
        qos = QOS_CLASS_USER_INTERACTIVE;
    } else if (std::strstr(name, "Job.Worker") || std::strstr(name, "FMOD")) {
        qos = QOS_CLASS_USER_INITIATED;
    }
    if (qos != QOS_CLASS_UNSPECIFIED) pthread_set_qos_class_self_np(qos, 0);
}
#endif

extern "C" int bionic_prctl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    if (option == PR_SET_NAME) {
        const char* name =
            arg2 ? reinterpret_cast<const char*>(arg2) : "<null>";
        const int r =
#ifdef __APPLE__
            pthread_setname_np(name);
#else
            pthread_setname_np(pthread_self(), name);
#endif
        // Fault isolation reads this (signal-safe table): engine-critical
        // threads (UnityMain, RenderThread, ...) must never be mistaken for
        // background workers when they fault.
        if (arg2) kudroid_note_thread_name(reinterpret_cast<const char*>(arg2));
#ifdef __APPLE__
        apply_thread_qos_for_name(arg2 ? name : nullptr);
#endif
        logAndroidMessage(4, "KuDroidSyscall", "prctl(PR_SET_NAME, \"" +
                          std::string(name) +
                          "\") -> " + (r == 0 ? "0" : std::to_string(r) + " errno=" + std::to_string(errno)));
        return r;
    } else if (option == PR_GET_NAME) {
        if (arg2) {
#ifdef __APPLE__
            pthread_getname_np(pthread_self(), reinterpret_cast<char*>(arg2), 16);
#else
            pthread_getname_np(pthread_self(), reinterpret_cast<char*>(arg2), 16);
#endif
        }
        return 0;
    } else if (option == PR_SET_VMA) {
        // Just fake it for PR_SET_VMA_ANON_NAME to prevent crashes
        logAndroidMessage(4, "KuDroidSyscall", "prctl(PR_SET_VMA) faked -> 0");
        return 0;
    }
    logAndroidMessage(4, "KuDroidSyscall", "prctl(option=0x" +
                      std::to_string((unsigned)option) + ") faked -> 0");
    return 0;
}

struct bionic_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

extern "C" int bionic_uname(struct bionic_utsname* buf) {
    if (!buf) return -1;
    strncpy(buf->sysname, "Linux", sizeof(buf->sysname) - 1);
    strncpy(buf->nodename, "localhost", sizeof(buf->nodename) - 1);
    strncpy(buf->release, "5.10.0-kudroid", sizeof(buf->release) - 1);
    strncpy(buf->version, "#1 SMP PREEMPT 2026", sizeof(buf->version) - 1);
    strncpy(buf->machine, "aarch64", sizeof(buf->machine) - 1);
    strncpy(buf->domainname, "(none)", sizeof(buf->domainname) - 1);
    return 0;
}

static int translate_linux_open_flags(int flags) {
#if defined(__APPLE__)
    int host_flags = 0;
    int acc = flags & 3;
    if (acc == 0) host_flags |= O_RDONLY;
    else if (acc == 1) host_flags |= O_WRONLY;
    else if (acc == 2) host_flags |= O_RDWR;

    if (flags & 0x40) host_flags |= O_CREAT;
    if (flags & 0x80) host_flags |= O_EXCL;
    if (flags & 0x100) host_flags |= O_NOCTTY;
    if (flags & 0x200) host_flags |= O_TRUNC;
    if (flags & 0x400) host_flags |= O_APPEND;
    if (flags & 0x800) host_flags |= O_NONBLOCK;
#if defined(O_CLOEXEC)
    if (flags & 0x80000) host_flags |= O_CLOEXEC;
#endif
    return host_flags;
#else
    return flags;
#endif
}

static int translate_linux_dirfd(int dirfd) {
#if defined(__APPLE__)
    if (dirfd == -100) { // Linux AT_FDCWD
        return AT_FDCWD; // Darwin AT_FDCWD (-2)
    }
#endif
    return dirfd;
}

// True when an *at() call's path should go through the remapper.
//
// A relative path is resolved by the kernel against `dirfd`, so remapping it is wrong
// twice over: remap() would treat it as a stray relative path and root it under
// data/local/tmp, and the kernel would then resolve that already-absolute result
// against dirfd anyway. The guest's intent — "this name, inside the directory I already
// opened" — is preserved by leaving it alone, because the fd it names was itself
// obtained through a remapped open.
//
// AT_FDCWD is the exception: there is no directory fd, so a relative path is relative to
// the process working directory, and remap() rooting it inside the VFS is the correct
// interpretation of what the guest asked for.
static bool at_path_needs_remap(int dirfd, const char* pathname) {
    if (pathname == nullptr || *pathname == '\0') return false;
    if (pathname[0] == '/') return true;
    return translate_linux_dirfd(dirfd) == AT_FDCWD;
}

// Defined further down with the audio fd-trace budget; declared here because the
// open path is the one place FMOD's fd handles become visible.
extern "C" bool kudroid_audio_thread(void);
extern "C" int kudroid_fd_is_apk(int fd);
static std::atomic<int> g_audioFdOpsEarly{0};
static bool audio_open_trace_take() {
    if (!kudroid::log::trace_hot()) return false;
    if (g_audioFdOpsEarly.load(std::memory_order_relaxed) >= 200) return false;
    if (!kudroid_audio_thread()) return false;
    g_audioFdOpsEarly.fetch_add(1, std::memory_order_relaxed);
    return true;
}

extern "C" int bionic_openat(int dirfd, const char* pathname, int flags, mode_t mode) {
    if (!pathname) return -1;
    const int host_dirfd = translate_linux_dirfd(dirfd);
    const bool remapPath = at_path_needs_remap(dirfd, pathname);
    const std::string remapped =
        remapPath ? kudroid::VFSPathRemapper::getInstance().remap(pathname)
                  : std::string(pathname);
    const int host_flags = translate_linux_open_flags(flags);
    if (host_flags & O_CREAT) {
        std::error_code ec;
        // Only meaningful for an absolute result; for a path relative to dirfd the
        // parent is the directory the fd already refers to, which exists by definition.
        if (remapPath) {
            std::filesystem::create_directories(std::filesystem::path(remapped).parent_path(), ec);
        }
    }
    const int fd = ::openat(host_dirfd, remapped.c_str(), host_flags, mode);
    // FMOD's fd path probes the size of the file it is about to stream: the
    // bytes-per-clip math is st_size minus how much the loader already read
    // through its own handles. A probe handle on base.apk (the guest sizes
    // clips through st_size even though every clip is an entry INSIDE it) is
    // where the size goes wrong, so name the fd, the path and the number the
    // guest will see on the audio thread. Nothing else logs that open.
    if (audio_open_trace_take()) {
        struct stat ost;
        const bool haveStat = fd >= 0 && ::fstat(fd, &ost) == 0;
        std::fprintf(stderr,
                     "%s[KuDroidFd] AUDIO open fd=%d apk=%d flags=0x%x path=%s size=%lld\n",
                     kudroid_trace_stamp(), fd, fd >= 0 && kudroid_fd_is_apk(fd) ? 1 : 0,
                     flags, remapped.c_str(),
                     haveStat ? static_cast<long long>(ost.st_size) : -1LL);
    }
    // Open hits on game data: the last visibility gap (misses already log).
    // A bundle opened but never read, or never opened at all, decides the stall.
    // Match the original path too: il2cpp/C# FileStream comes through openat,
    // and remap mangles jar: URLs past recognition. Same list as vfs taps.
    const std::string orig = pathname ? pathname : "";
    auto hit = [&](const char* s) {
        return orig.find(s) != std::string::npos ||
               remapped.find(s) != std::string::npos;
    };
    if (hit("/assets/") || hit(".apk") || hit(".bank") || hit(".fsb") ||
        hit(".mp3") || hit(".ogg") || hit(".wav") || hit(".mp4") ||
        hit(".webm") || hit("jar:") || hit(".json") || hit("catalog") ||
        hit("/files/") || hit("/sdcard/") || hit(".bundle") ||
        hit(".resource") || hit("sharedassets") || hit("data.unity3d") ||
        hit("assets/bin/") || hit("/level")) {
        // Failures are the signal: a bundle open that fails is an empty scene,
        // silent audio and a trackless video with no other trace. Log every
        // failure; cap only the success noise.
        if (fd < 0) {
            static std::atomic<int> s_failLogged{0};
            if (s_failLogged.load() < 60) {
                ++s_failLogged;
                std::fprintf(stderr, "[KuDroidIO] open FAIL %s -> %d\n", orig.c_str(), fd);
            }
        } else {
            static std::atomic<int> s_logged{0};
            if (s_logged.load() < 80) {
                ++s_logged;
                // Guest path: stable across installs (host path embeds a UUID).
                std::fprintf(stderr, "[KuDroidIO] open %s -> %d\n", orig.c_str(), fd);
            }
        }
    }
    return fd;
}

struct bionic_stat64 {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint64_t __pad1;
    int64_t  st_size;
    int32_t  st_blksize;
    int32_t  __pad2;
    int64_t  st_blocks;
    int64_t  __st_atime_sec;
    uint64_t __st_atime_nsec;
    int64_t  __st_mtime_sec;
    uint64_t __st_mtime_nsec;
    int64_t  __st_ctime_sec;
    uint64_t __st_ctime_nsec;
    uint32_t __unused4;
    uint32_t __unused5;
};

static int guest_stat_at(int dirfd, const char* pathname, struct stat* output, int flags) {
    constexpr int noFollow = 0x100;
    constexpr int noAutomount = 0x800;
    constexpr int emptyPath = 0x1000;
    if (!pathname || !output) { errno = EFAULT; return -1; }
    if (flags & ~(noFollow | noAutomount | emptyPath)) { errno = EINVAL; return -1; }
    const int hostDirfd = translate_linux_dirfd(dirfd);
    if (*pathname == '\0') {
        if (!(flags & emptyPath)) { errno = ENOENT; return -1; }
        return hostDirfd == AT_FDCWD ? ::stat(".", output) : ::fstat(hostDirfd, output);
    }
    const std::string mapped = at_path_needs_remap(dirfd, pathname)
        ? kudroid::VFSPathRemapper::getInstance().remap(pathname) : std::string(pathname);
    const int hostFlags = (flags & noFollow) ? AT_SYMLINK_NOFOLLOW : 0;
    const int result = ::fstatat(hostDirfd, mapped.c_str(), output, hostFlags);
    if (result != 0) {
        const int savedErrno = errno;
        static std::atomic<unsigned> samples{0};
        if (samples.fetch_add(1, std::memory_order_relaxed) < 24) {
            logAndroidMessage(5, "KuDroidVFS", "directory-stat failed fd=" + std::to_string(dirfd)
                + " path=" + pathname + " flags=" + std::to_string(flags) + " errno=" + std::to_string(savedErrno));
        }
        errno = savedErrno;
    }
    return result;
}

extern "C" int bionic_newfstatat(int dirfd, const char* pathname, struct bionic_stat64* statbuf, int flags) {
    if (!statbuf) { errno = EFAULT; return -1; }
    struct stat host_st;
    memset(&host_st, 0, sizeof(host_st));
    const int res = guest_stat_at(dirfd, pathname, &host_st, flags);
    if (res != 0) return res;

    memset(statbuf, 0, sizeof(*statbuf));
    statbuf->st_dev = static_cast<uint64_t>(host_st.st_dev);
    statbuf->st_ino = static_cast<uint64_t>(host_st.st_ino);
    statbuf->st_mode = static_cast<uint32_t>(host_st.st_mode);
    statbuf->st_nlink = static_cast<uint32_t>(host_st.st_nlink);
    statbuf->st_uid = static_cast<uint32_t>(host_st.st_uid);
    statbuf->st_gid = static_cast<uint32_t>(host_st.st_gid);
    statbuf->st_rdev = static_cast<uint64_t>(host_st.st_rdev);
    statbuf->st_size = static_cast<int64_t>(host_st.st_size);
    statbuf->st_blksize = static_cast<int32_t>(host_st.st_blksize);
    statbuf->st_blocks = static_cast<int64_t>(host_st.st_blocks);
#if defined(__APPLE__)
    statbuf->__st_atime_sec = host_st.st_atimespec.tv_sec;
    statbuf->__st_atime_nsec = host_st.st_atimespec.tv_nsec;
    statbuf->__st_mtime_sec = host_st.st_mtimespec.tv_sec;
    statbuf->__st_mtime_nsec = host_st.st_mtimespec.tv_nsec;
    statbuf->__st_ctime_sec = host_st.st_ctimespec.tv_sec;
    statbuf->__st_ctime_nsec = host_st.st_ctimespec.tv_nsec;
#else
    statbuf->__st_atime_sec = host_st.st_atim.tv_sec;
    statbuf->__st_atime_nsec = host_st.st_atim.tv_nsec;
    statbuf->__st_mtime_sec = host_st.st_mtim.tv_sec;
    statbuf->__st_mtime_nsec = host_st.st_mtim.tv_nsec;
    statbuf->__st_ctime_sec = host_st.st_ctim.tv_sec;
    statbuf->__st_ctime_nsec = host_st.st_ctim.tv_nsec;
#endif
    return 0;
}

// bionic's struct statfs for arm64. Darwin's is a different shape and a different size,
// so forwarding the guest's buffer to ::statfs would have it read f_bsize out of what is
// actually f_type, and f_blocks out of padding.
//
// Apps ask this before writing anything substantial — Minecraft checks free space before
// downloading a world, installers check before unpacking — and a garbage answer reads as
// "disk full" or, worse, as a huge amount of free space followed by a failed write.
struct bionic_statfs64 {
    uint64_t f_type;
    uint64_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    struct { int32_t val[2]; } f_fsid;
    uint64_t f_namelen;
    uint64_t f_frsize;
    uint64_t f_flags;
    uint64_t f_spare[4];
};

static int fill_bionic_statfs(const struct statvfs& src, struct bionic_statfs64* dst) {
    memset(dst, 0, sizeof(*dst));
    // EXT4_SUPER_MAGIC. The value matters: apps branch on f_type to decide whether a
    // path is on external storage, and an unrecognised filesystem is treated as
    // read-only by some of them.
    dst->f_type = 0xEF53u;
    dst->f_bsize = src.f_bsize;
    dst->f_frsize = src.f_frsize != 0 ? src.f_frsize : src.f_bsize;
    dst->f_blocks = src.f_blocks;
    dst->f_bfree = src.f_bfree;
    dst->f_bavail = src.f_bavail;
    dst->f_files = src.f_files;
    dst->f_ffree = src.f_ffree;
    dst->f_namelen = src.f_namemax;
    dst->f_flags = src.f_flag;
    return 0;
}

extern "C" int bionic_statfs(const char* path, struct bionic_statfs64* buf) {
    if (!path || !buf) {
        errno = EFAULT;
        return -1;
    }
    const std::string remapped = kudroid::VFSPathRemapper::getInstance().remap(path);
    struct statvfs host {};
    if (::statvfs(remapped.c_str(), &host) != 0) return -1;
    return fill_bionic_statfs(host, buf);
}

extern "C" int bionic_fstatfs(int fd, struct bionic_statfs64* buf) {
    if (!buf) {
        errno = EFAULT;
        return -1;
    }
    struct statvfs host {};
    if (::fstatvfs(fd, &host) != 0) return -1;
    return fill_bionic_statfs(host, buf);
}

// Check in bionic_mmap: ashmem fd may only map with granted prot.
static bool ashmem_prot_allows(int fd, int prot);
extern "C" ssize_t bionic_read(int fd, void* buf, size_t count);
extern "C" ssize_t bionic_write(int fd, const void* buf, size_t count);
extern "C" int bionic_socket(int domain, int type, int protocol);
extern "C" int bionic_bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen);
extern "C" int bionic_connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen);
extern "C" int bionic_getsockname(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
extern "C" int bionic_getpeername(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
extern "C" int bionic_socketpair(int domain, int type, int protocol, int sv[2]);
extern "C" int bionic_accept4(int fd, struct sockaddr* addr, socklen_t* addrlen, int flags);
extern "C" int bionic_listen(int fd, int backlog);
extern "C" int bionic_shutdown(int fd, int how);
extern "C" int bionic_setsockopt(int fd, int level, int name, const void* value, socklen_t len);
extern "C" int bionic_getsockopt(int fd, int level, int name, void* value, socklen_t* len);
extern "C" ssize_t bionic_sendto(int fd, const void* buf, size_t len, int flags,
                                 const struct sockaddr* addr, socklen_t addrlen);
extern "C" ssize_t bionic_recvfrom(int fd, void* buf, size_t len, int flags,
                                   struct sockaddr* addr, socklen_t* addrlen);
extern "C" int bionic_ppoll(struct pollfd* fds, unsigned long nfds, const struct timespec* tmo,
                            const void* sigmask);
extern "C" int bionic_pselect(int nfds, void* readfds, void* writefds, void* exceptfds,
                              const struct timespec* tmo, const void* sigmask);
extern "C" int bionic_dup3(int oldfd, int newfd, int flags);
struct GuestMsghdr;
struct GuestMmsghdr;
extern "C" ssize_t bionic_sendmsg(int fd, const GuestMsghdr* msg, int flags);
extern "C" ssize_t bionic_recvmsg(int fd, GuestMsghdr* msg, int flags);
extern "C" int bionic_sendmmsg(int fd, GuestMmsghdr* msgvec, unsigned int vlen, int flags);
extern "C" int bionic_recvmmsg(int fd, GuestMmsghdr* msgvec, unsigned int vlen, int flags, void* timeout);
extern "C" int getaddrinfo(const char* node, const char* service,
                           const struct addrinfo* hints, struct addrinfo** res);
extern "C" const char* gai_strerror(int errcode);
extern "C" int bionic_statx(int dirfd, const char* pathname, int flags,
                            unsigned mask, void* statxbuf);
extern "C" ssize_t bionic_pread64(int fd, void* buf, size_t count, off_t offset);
// Fake ashmem fd (iOS fallback): return granted region or nullptr when not a fake fd.
extern "C" void* bionic_ashmem_mmap_fd(int fd, size_t length);
// Defined next to the fd→path map further down; declared here because bionic_mmap
// (earlier in this file) uses it for the mmap-on-APK diagnostics the pread tap
// already provides. extern "C" so the name cannot be captured by the anonymous
// namespaces the definition lives inside.
extern "C" int kudroid_fd_is_apk(int fd);

