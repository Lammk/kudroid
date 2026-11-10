// getifaddrs with Linux layout: Darwin also reports AF_LINK/AF_SYSTEM
// entries whose sockaddr Unity's enumerator feeds to inet_ntop, producing
// "sa_addr inet_ntop() failed ... ENOLINK" noise (and wrong results for any
// consumer). Keep AF_INET/AF_INET6 only, translated to Linux sockaddr layout
// (family u16 LE, no sa_len). Field order of ifaddrs matches on both (arm64).
struct GuestIfaddrs {
    GuestIfaddrs* g_next;
    char* g_name;
    unsigned g_flags;
    unsigned char* g_addr;
    unsigned char* g_netmask;
    unsigned char* g_broadaddr;
    void* g_data;
};

namespace {
unsigned char* translate_sockaddr(const unsigned char* d) {
    if (d == nullptr) return nullptr;
#if defined(__APPLE__)
    const unsigned fam = d[1];  // Darwin: len @0, family @1
    if (fam == 2) {             // AF_INET both
        unsigned char* l = static_cast<unsigned char*>(std::malloc(16));
        if (!l) return nullptr;
        l[0] = 2;
        l[1] = 0;
        std::memcpy(l + 2, d + 2, 6);  // port + addr
        std::memset(l + 8, 0, 8);
        return l;
    }
    if (fam == 30) {  // AF_INET6 Darwin -> 10 Linux
        unsigned char* l = static_cast<unsigned char*>(std::malloc(28));
        if (!l) return nullptr;
        l[0] = 10;
        l[1] = 0;
        std::memcpy(l + 2, d + 2, 26);  // port + flow + addr + scope
        return l;
    }
    return nullptr;  // AF_LINK and the rest: not guest-visible
#else
    const unsigned fam = static_cast<unsigned>(d[0]) | (static_cast<unsigned>(d[1]) << 8);
    if (fam == 2) {  // AF_INET
        unsigned char* l = static_cast<unsigned char*>(std::malloc(16));
        if (!l) return nullptr;
        std::memcpy(l, d, 16);
        return l;
    }
    if (fam == 10) {  // AF_INET6
        unsigned char* l = static_cast<unsigned char*>(std::malloc(28));
        if (!l) return nullptr;
        std::memcpy(l, d, 28);
        return l;
    }
    return nullptr;  // Filter out AF_PACKET (17) and other non-inet families
#endif
}
void free_guest_ifaddrs(GuestIfaddrs* head) {
    while (head != nullptr) {
        GuestIfaddrs* next = head->g_next;
        std::free(head->g_name);
        std::free(head->g_addr);
        std::free(head->g_netmask);
        std::free(head->g_broadaddr);
        std::free(head);
        head = next;
    }
}
}  // namespace

extern "C" int bionic_getifaddrs(GuestIfaddrs** out) {
    if (!out) return -1;
    *out = nullptr;
    struct ifaddrs* host = nullptr;
    if (::getifaddrs(&host) != 0) return -1;
    GuestIfaddrs* head = nullptr;
    GuestIfaddrs** tail = &head;
    unsigned kept = 0;
    for (struct ifaddrs* h = host; h != nullptr; h = h->ifa_next) {
        unsigned char* addr =
            translate_sockaddr(reinterpret_cast<unsigned char*>(h->ifa_addr));
        if (h->ifa_addr != nullptr && addr == nullptr) continue;  // skipped family
        GuestIfaddrs* g =
            static_cast<GuestIfaddrs*>(std::calloc(1, sizeof(GuestIfaddrs)));
        if (!g) {
            std::free(addr);
            continue;
        }
        g->g_name = h->ifa_name ? ::strdup(h->ifa_name) : nullptr;
        g->g_flags = h->ifa_flags;
        g->g_addr = addr;
        g->g_netmask = translate_sockaddr(
            reinterpret_cast<unsigned char*>(h->ifa_netmask));
        g->g_broadaddr = translate_sockaddr(
            reinterpret_cast<unsigned char*>(h->ifa_broadaddr));
        *tail = g;
        tail = &g->g_next;
        ++kept;
    }
    ::freeifaddrs(host);
    *out = head;
    std::fprintf(stderr, "[KuDroidNet] getifaddrs -> %u entries\n", kept);
    return 0;
}

extern "C" void bionic_freeifaddrs(GuestIfaddrs* head) {
    free_guest_ifaddrs(head);
}

extern "C" ssize_t bionic_pread64(int fd, void* buf, size_t count, off_t offset) {
    const auto t0 = std::chrono::steady_clock::now();
    ssize_t ret;
    do {
        ret = ::pread(fd, buf, count, offset);
    } while (ret < 0 && errno == EINTR);
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0)
                             .count();
    // Diagnostic: short reads on asset/bank streaming corrupt async loads.
    if ((ret >= 0 && static_cast<size_t>(ret) < count) || ret < 0) {
        static std::atomic<int> s_logged{0};
        if (s_logged.load() < 30) {
            ++s_logged;
            std::fprintf(stderr,
                         "[KuDroidIO] pread64 fd=%d count=%zu offset=%lld ret=%zd%s\n",
                         fd, count, static_cast<long long>(offset), ret,
                         ret < 0 ? " ERR" : " SHORT");
        }
    }
    // Diagnostic: reads on base.apk prove catalog/asset bytes actually flow.
    if (ret > 0) {
        if (fd_is_apk(fd)) {
            static std::atomic<int> s_apk{0};
            if (s_apk.load() < 30) {
                ++s_apk;
                std::fprintf(stderr,
                             "[KuDroidApk] pread fd=%d offset=%lld count=%zu ret=%zd took=%lldms\n",
                             fd, static_cast<long long>(offset), count, ret, ms);
            }
            // Per-MB totals, mirroring [KuDroidApkF]: the fread tracker goes blind once
            // a loader switches to random access, which is exactly when the
            // Addressables catalog question gets asked. Silence past ~3MB means the
            // catalog fetch never reached the APK fd.
            static std::atomic<uint64_t> s_apkTotal{0};
            const uint64_t before =
                s_apkTotal.fetch_add(static_cast<uint64_t>(ret), std::memory_order_relaxed);
            if ((before / (1024 * 1024)) != ((before + static_cast<uint64_t>(ret)) / (1024 * 1024))) {
                std::fprintf(stderr, "[KuDroidApk] pread total=%lluMB\n",
                             static_cast<unsigned long long>(
                                 (before + static_cast<uint64_t>(ret)) / (1024 * 1024)));
            }
        }
        io_volume_add(std::string(fd_path(fd)), static_cast<uint64_t>(ret), "read");
    }
    // Audio thread: traced on any fd and regardless of result, because a clip load
    // that fails is exactly the case where the failing offset matters.
    if (audio_fd_trace_take()) {
        const std::string p = fd_path(fd);
        std::fprintf(stderr,
                     "%s[KuDroidFd] AUDIO pread fd=%d apk=%d path=%s offset=%lld count=%zu "
                     "ret=%zd took=%lldms\n",
                     kudroid_trace_stamp(), fd, kudroid_fd_is_apk(fd) ? 1 : 0,
                     p.empty() ? "?" : p.c_str(), static_cast<long long>(offset), count,
                     ret, ms);
    }
    // Diagnostic: slow asset reads starve async loaders (mixer hits pending voices).
    if (ms >= 5 && count >= 65536) {
        static std::atomic<int> s_slow{0};
        if (s_slow.load() < 20) {
            ++s_slow;
            std::fprintf(stderr,
                         "[KuDroidIO] slow pread64 fd=%d count=%zu ret=%zd took=%lldms\n",
                         fd, count, ret, ms);
        }
    }
    // Diagnostic: bulk flow (bundles, scenes). A stuck load shows no big reads.
    if (count >= 1048576 && ret > 0) {
        static std::atomic<int> s_big{0};
        if (s_big.load() < 15) {
            ++s_big;
            std::fprintf(stderr, "[KuDroidIO] big pread64 fd=%d count=%zu ret=%zd\n", fd,
                         count, ret);
        }
    }
    return ret;
}
extern "C" ssize_t bionic_pwrite64(int fd, const void* buf, size_t count, off_t offset) {
    return ::pwrite(fd, buf, count, offset);
}
extern "C" int bionic_ftruncate64(int fd, off_t length) {
    return ::ftruncate(fd, length);
}

// pipe2: macOS lacks it — pipe() + fcntl. Linux O_NONBLOCK=0x800, O_CLOEXEC=0x80000.
extern "C" int bionic_pipe2(int pipefd[2], int flags) {
#ifdef __linux__
    return ::pipe2(pipefd, flags);
#else
    if (::pipe(pipefd) != 0) return -1;
    if (flags & 0x800) { // O_NONBLOCK
        for (int i = 0; i < 2; ++i) {
            const int fl = ::fcntl(pipefd[i], F_GETFL);
            if (fl < 0 || ::fcntl(pipefd[i], F_SETFL, fl | O_NONBLOCK) != 0) {
                const int saved = errno;
                ::close(pipefd[0]); ::close(pipefd[1]);
                errno = saved;
                return -1;
            }
        }
    }
    if (flags & 0x80000) { // O_CLOEXEC
        for (int i = 0; i < 2; ++i) {
            const int fl = ::fcntl(pipefd[i], F_GETFD);
            if (fl < 0 || ::fcntl(pipefd[i], F_SETFD, fl | FD_CLOEXEC) != 0) {
                const int saved = errno;
                ::close(pipefd[0]); ::close(pipefd[1]);
                errno = saved;
                return -1;
            }
        }
    }
    return 0;
#endif
}

// clock_nanosleep: macOS has only relative nanosleep; use it for both clocks.
extern "C" int bionic_clock_nanosleep(int clock_id, int flags, const struct timespec* req,
                                       struct timespec* rem) {
    if (!req) { errno = EFAULT; return -1; }
    struct timespec r = *req;
    if (flags & 1) { // TIMER_ABSTIME
        struct timespec now;
        // Absolute deadlines run on the caller's clock, not always REALTIME.
        if (bionic_clock_gettime(clock_id, &now) != 0) return errno;
        r.tv_sec -= now.tv_sec;
        r.tv_nsec -= now.tv_nsec;
        if (r.tv_nsec < 0) { r.tv_sec -= 1; r.tv_nsec += 1000000000; }
        if (r.tv_sec < 0) { r.tv_sec = 0; r.tv_nsec = 0; }
    }
#ifdef __linux__
    return ::clock_nanosleep(static_cast<clockid_t>(clock_id), 0, &r, rem);
#else
    return ::nanosleep(&r, rem);
#endif
}

// usleep: forward to the host; previously missing so resolution was unreliable.
extern "C" int bionic_usleep(unsigned int usecs) {
    return ::usleep(usecs);
}

// tgkill(pid, tid, sig): look up the tid->pthread_t recorded at gettid.
// `sig` is a LINUX number and must be translated before it reaches the host, exactly as
// sigaction translates it on the way in. This used to forward it raw, and the asymmetry
// is what broke il2cpp's thread suspension: the guest installs a handler for Linux
// SIGPWR (30), which lands on host SIGINFO (29), and then sends 30 — Darwin's SIGUSR1,
// an empty slot whose default action terminates the process. The install and the send
// both reported success.
extern "C" int bionic_tgkill(int pid, int tid, int sig) {
    if (pid != static_cast<int>(::getpid())) { errno = ESRCH; return -1; }
    pthread_t target;
    {
        std::lock_guard<std::mutex> lock(g_tidRegistryMtx);
        auto it = g_tidRegistry.find(static_cast<long>(tid));
        if (it == g_tidRegistry.end()) { errno = ESRCH; return -1; }
        target = it->second;
    }
    if (sig == 0) return 0; // kim tra thread tn ti
    unsigned long thread_bits = 0;
    std::memcpy(&thread_bits, &target,
                sizeof(thread_bits) < sizeof(target) ? sizeof(thread_bits) : sizeof(target));
    return kudroid::guest_pthread_kill(thread_bits, sig);
}

// The rest of the outbound signal surface. All four were previously unshimmed, so the
// guest's Linux number went straight to the host — see the note on bionic_tgkill above.
extern "C" int bionic_raise(int sig) {
    return kudroid::guest_raise(sig);
}

extern "C" int bionic_kill(pid_t pid, int sig) {
    return kudroid::guest_kill(static_cast<int>(pid), sig);
}

// libc contract, unlike the syscall senders: 0 or the error NUMBER, never -1. A
// collector suspending threads tells "thread already gone" (ESRCH) from a real failure
// by that number, and aborts on anything else.
extern "C" int bionic_pthread_kill(pthread_t thread, int sig) {
    unsigned long thread_bits = 0;
    std::memcpy(&thread_bits, &thread,
                sizeof(thread_bits) < sizeof(thread) ? sizeof(thread_bits) : sizeof(thread));
    errno = 0;
    if (kudroid::guest_pthread_kill(thread_bits, sig) == 0) return 0;
    return errno != 0 ? errno : EINVAL;
}

extern "C" int bionic_tkill(int tid, int sig) {
    return bionic_tgkill(static_cast<int>(::getpid()), tid, sig);
}

// abort(), so the guest's own abort message and thread reach the log before the process
// goes down.
//
// Not a translation — SIGABRT is 6 on both platforms — but the one place where a guest
// says "I am giving up" with a reason it has already stored, and routing it through
// guest_raise means the fatal-signal breadcrumb is written by KuDroid's handler with
// that reason attached. Falling through to the host's abort() reached the same handler
// but named nothing about the guest.
extern "C" void bionic_abort(void) {
    // Unblock SIGABRT first: abort() must not be silently swallowed by a guest that
    // blocked it, which is what the C standard requires of a conforming abort().
    sigset_t only_abort;
    sigemptyset(&only_abort);
    sigaddset(&only_abort, SIGABRT);
    ::pthread_sigmask(SIG_UNBLOCK, &only_abort, nullptr);

    kudroid::guest_raise(6);  // Linux SIGABRT

    // A handler that returned, or one that was ignored: abort() may not return, so fall
    // back to the host's, which restores SIG_DFL and re-raises.
    ::abort();
}

// Captures what a guest that aborts WITHOUT a message means: libc++'s
// __libcpp_verbose_abort (hardening checks) and the C standard assert().
// A crash breadcrumb with no message cannot be told apart from an abort the
// guest never explained; these are the entry points that know the reason.
static void store_libc_abort_reason(const char* fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    kudroid_store_abort_message(msg);
}

// libc++ hardening trap: void __libcpp_verbose_abort(const char* format, ...)
extern "C" void bionic___libcpp_verbose_abort(const char* format, ...) {
    if (format && *format) {
        va_list ap;
        va_start(ap, format);
        char msg[512];
        vsnprintf(msg, sizeof(msg), format, ap);
        va_end(ap);
        kudroid_store_abort_message(msg);
    }
    bionic_abort();
}

// C assert(): void __assert_fail(const char*, const char*, unsigned int, const char*)
extern "C" void bionic___assert_fail(const char* expr, const char* file, unsigned int line,
                                      const char* function) {
    store_libc_abort_reason("assertion failed: %s (%s:%u in %s)",
                            expr ? expr : "?", file ? file : "?", line,
                            function ? function : "?");
    bionic_abort();
}

// macOS/BSD variant: void __assert_rtn(const char*, const char*, int, const char*)
extern "C" void bionic___assert_rtn(const char* function, const char* file, int line,
                                     const char* expr) {
    store_libc_abort_reason("assertion failed: %s (%s:%d in %s)",
                            expr ? expr : "?", file ? file : "?", line,
                            function ? function : "?");
    bionic_abort();
}

// sigprocmask / pthread_sigmask, where BOTH the mask and `how` need translating: Linux
// numbers SIG_BLOCK/UNBLOCK/SETMASK 0/1/2 and Darwin numbers them 1/2/3, so forwarding
// the guest's value selects a different operation rather than failing.
extern "C" int bionic_sigprocmask(int how, const void* set, void* oldset) {
    return kudroid::guest_sigprocmask(how, static_cast<const uint64_t*>(set),
                                      static_cast<uint64_t*>(oldset));
}

extern "C" int bionic_pthread_sigmask(int how, const void* set, void* oldset) {
    return kudroid::guest_sigprocmask(how, static_cast<const uint64_t*>(set),
                                      static_cast<uint64_t*>(oldset));
}

extern "C" int bionic_sigsuspend(const void* mask) {
    return kudroid::guest_sigsuspend(static_cast<const uint64_t*>(mask));
}

extern "C" int bionic_sigwait(const void* set, int* sig) {
    return kudroid::guest_sigwait(static_cast<const uint64_t*>(set), sig);
}

// signal(): sigaction with BSD flags, recorded through the same registry so a guest that
// installs with one and reads back with the other sees its own handler.
extern "C" void* bionic_signal(int sig, void* handler) {
    return kudroid::guest_signal(sig, handler);
}

// bionic's sigset_t operations, on the GUEST's 64-bit mask.
//
// These take a pointer to the guest's sigset_t — a bare unsigned long on LP64 bionic —
// and the bit for Linux signal n is (n-1). The host's macros operate on the host's
// sigset_t, which on Darwin is also 32 bits wide but numbers its bits by HOST signal, so
// letting these bind to the host's implementations mixes the two numbering schemes in
// one word. A guest that builds a mask with sigaddset and hands it to sigprocmask would
// then have it translated a second time.
extern "C" int bionic_sigemptyset(void* set) {
    if (!set) { errno = EINVAL; return -1; }
    *static_cast<uint64_t*>(set) = 0;
    return 0;
}

extern "C" int bionic_sigfillset(void* set) {
    if (!set) { errno = EINVAL; return -1; }
    *static_cast<uint64_t*>(set) = ~0ull;
    return 0;
}

extern "C" int bionic_sigaddset(void* set, int sig) {
    if (!set || sig < 1 || sig >= 65) { errno = EINVAL; return -1; }
    *static_cast<uint64_t*>(set) |= (1ull << (sig - 1));
    return 0;
}

extern "C" int bionic_sigdelset(void* set, int sig) {
    if (!set || sig < 1 || sig >= 65) { errno = EINVAL; return -1; }
    *static_cast<uint64_t*>(set) &= ~(1ull << (sig - 1));
    return 0;
}

extern "C" int bionic_sigismember(const void* set, int sig) {
    if (!set || sig < 1 || sig >= 65) { errno = EINVAL; return -1; }
    return (*static_cast<const uint64_t*>(set) & (1ull << (sig - 1))) != 0 ? 1 : 0;
}

extern "C" int bionic_sched_getscheduler(pid_t pid) {
    (void)pid;
    return 0; // SCHED_OTHER
}

