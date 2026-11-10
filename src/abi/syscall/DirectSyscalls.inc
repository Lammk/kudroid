
// Audio-side fd tracing. FMOD does its bank IO on its own thread, and by then the
// preload-era caps on the fd traces below are long spent, so the audio path's own
// reads/seeks were invisible exactly when a createSound fails. Audio IO therefore
// gets its own budget: per-thread identity plus a bounded op count, so one clip
// load keeps a complete trace without flooding the log.
extern "C" bool kudroid_audio_thread(void);
static std::atomic<int> g_audioFdOps{0};
constexpr int kAudioFdOpBudget = 1500;
static bool audio_fd_trace_take() {
    if (!kudroid::log::trace_hot()) return false;
    if (g_audioFdOps.load(std::memory_order_relaxed) >= kAudioFdOpBudget) return false;
    if (!kudroid_audio_thread()) return false;
    g_audioFdOps.fetch_add(1, std::memory_order_relaxed);
    return true;
}

extern "C" long bionic_syscall(long number, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    uintptr_t entryX19 = 0;
#if defined(__aarch64__)
    // x19 at entry is the guard pointer when this syscall came from
    // __cxa_guard_acquire (see log_guard_acquire_diag). Reading a register is free;
    // the expensive part is the frame walk, which only happens for an in-progress
    // guard and only for the first few.
    asm volatile("mov %0, x19" : "=r"(entryX19));
#endif

    switch (number) {
        // Epoll & Descriptors
        case 20: // epoll_create1
            return bionic_epoll_create1(static_cast<int>(a1));

        case 21: // epoll_ctl
            return bionic_epoll_ctl(static_cast<int>(a1), static_cast<int>(a2), static_cast<int>(a3), reinterpret_cast<void*>(a4));

        case 22: // epoll_pwait / epoll_wait
            return bionic_epoll_wait(static_cast<int>(a1), reinterpret_cast<void*>(a2), static_cast<int>(a3), static_cast<int>(a4));

        case 23: // dup
            return ::dup(static_cast<int>(a1));

        case 24: // dup3
            return bionic_dup3(static_cast<int>(a1), static_cast<int>(a2), static_cast<int>(a3));

        case 34: { // mkdirat
            const char* path = reinterpret_cast<const char*>(a2);
            if (!path) return -1;
            const int dirfd = static_cast<int>(a1);
            const std::string remapped =
                at_path_needs_remap(dirfd, path)
                    ? kudroid::VFSPathRemapper::getInstance().remap(path)
                    : std::string(path);
            const int host_dirfd = translate_linux_dirfd(dirfd);
            return ::mkdirat(host_dirfd, remapped.c_str(), static_cast<mode_t>(a3));
        }
        case 35: { // unlinkat
            const char* path = reinterpret_cast<const char*>(a2);
            if (!path) return -1;
            const int dirfd = static_cast<int>(a1);
            const std::string remapped =
                at_path_needs_remap(dirfd, path)
                    ? kudroid::VFSPathRemapper::getInstance().remap(path)
                    : std::string(path);
            const int host_dirfd = translate_linux_dirfd(dirfd);
            return ::unlinkat(host_dirfd, remapped.c_str(), static_cast<int>(a3));
        }
        case 46: // ftruncate
            return ::ftruncate(static_cast<int>(a1), static_cast<off_t>(a2));

        case 56: // openat
            return bionic_openat(static_cast<int>(a1), reinterpret_cast<const char*>(a2), static_cast<int>(a3), static_cast<mode_t>(a4));

        case 57: // close
            return bionic_close(static_cast<int>(a1));

        case 59: // pipe2
            return bionic_pipe2(reinterpret_cast<int*>(a1), static_cast<int>(a2));

        case 62: { // lseek
            const int lfd = static_cast<int>(a1);
            const off_t res = ::lseek(lfd, static_cast<off_t>(a2), static_cast<int>(a3));
            // The zip-reader pairs lseek(offset)+read() on base.apk: without the
            // seek half of the pair the read trace has no offsets to geolocate.
            if (res >= 0 && kudroid_fd_is_apk(lfd)) {
                static std::atomic<int> s_apkSeeks{0};
                if (s_apkSeeks.load(std::memory_order_relaxed) < 200) {
                    s_apkSeeks.fetch_add(1, std::memory_order_relaxed);
                    std::fprintf(stderr,
                                 "[KuDroidApk] lseek fd=%d to=%lld whence=%d ret=%lld\n",
                                 lfd, static_cast<long long>(a2), static_cast<int>(a3),
                                 static_cast<long long>(res));
                }
            }
            if (audio_fd_trace_take()) {
                const std::string p = fd_path(lfd);
                std::fprintf(stderr,
                             "%s[KuDroidFd] AUDIO lseek fd=%d apk=%d path=%s to=%lld "
                             "whence=%d ret=%lld\n",
                             kudroid_trace_stamp(), lfd, kudroid_fd_is_apk(lfd) ? 1 : 0,
                             p.empty() ? "?" : p.c_str(), static_cast<long long>(a2),
                             static_cast<int>(a3), static_cast<long long>(res));
            }
            return res;
        }

        case 63: // read
            return bionic_read(static_cast<int>(a1), reinterpret_cast<void*>(a2),
                               static_cast<size_t>(a3));

        case 64: // write
            return ::write(static_cast<int>(a1), reinterpret_cast<const void*>(a2), static_cast<size_t>(a3));

        case 67: // pread64
            return bionic_pread64(static_cast<int>(a1), reinterpret_cast<void*>(a2),
                                  static_cast<size_t>(a3), static_cast<off_t>(a4));

        case 68: // pwrite64
            return ::pwrite(static_cast<int>(a1), reinterpret_cast<const void*>(a2), static_cast<size_t>(a3), static_cast<off_t>(a4));

        case 79: // newfstatat
            return bionic_newfstatat(static_cast<int>(a1), reinterpret_cast<const char*>(a2), reinterpret_cast<struct bionic_stat64*>(a3), static_cast<int>(a4));

        case 48: { // faccessat: File.Exists probes reach here via raw SVC
            const char* path = reinterpret_cast<const char*>(a2);
            const int dirfd = static_cast<int>(a1);
            const std::string remapped =
                at_path_needs_remap(dirfd, path)
                    ? kudroid::VFSPathRemapper::getInstance().remap(path)
                    : std::string(path ? path : "");
            const int host_dirfd = translate_linux_dirfd(dirfd);
            return ::faccessat(host_dirfd, remapped.c_str(),
                               static_cast<int>(a3), static_cast<int>(a4));
        }

        case 291: // statx: same remap as the symbol path, not raw host
            return bionic_statx(static_cast<int>(a1), reinterpret_cast<const char*>(a2),
                                static_cast<int>(a3), static_cast<unsigned>(a4),
                                reinterpret_cast<void*>(a5));


        case 98: // futex
            return emulate_futex_direct(a1, a2, a3, a4, a5, a6);

        case 101: // nanosleep
            return ::nanosleep(reinterpret_cast<const struct timespec*>(a1), reinterpret_cast<struct timespec*>(a2));

        case 113: // clock_gettime
            return bionic_clock_gettime(static_cast<int>(a1), reinterpret_cast<struct timespec*>(a2));

        case 114: // clock_getres
            return bionic_clock_getres(static_cast<int>(a1), reinterpret_cast<struct timespec*>(a2));

        case 124: // sched_yield
            return ::sched_yield();

        case 132: // sigaltstack (Linux arm64 syscall number)
            return bionic_sigaltstack(reinterpret_cast<const void*>(a1),
                                      reinterpret_cast<void*>(a2));

        // rt_sigaction (Linux arm64 syscall 134). Missing before, so a guest that used
        // syscall() rather than the libc wrapper got ENOSYS and silently ran without
        // the handler it thought it had installed.
        case 134:
            return bionic_rt_sigaction(static_cast<int>(a1), reinterpret_cast<const void*>(a2),
                                       reinterpret_cast<void*>(a3), static_cast<size_t>(a4));

        // The outbound signal syscalls, Linux arm64 numbering.
        //
        // A managed runtime does not always go through the libc wrapper: Mono's thread
        // suspension issues the syscall directly on some builds, and until now every one
        // of these fell through to the ENOSYS default. That is not a silent no-op — the
        // guest reads -1/ENOSYS from a suspend it believes it sent, and waits for a
        // thread that was never signalled.
        case 129: // kill(pid, sig)
            return kudroid::guest_kill(static_cast<int>(a1), static_cast<int>(a2));

        case 130: // tkill(tid, sig)
            return bionic_tkill(static_cast<int>(a1), static_cast<int>(a2));

        case 131: // tgkill(tgid, tid, sig)
            return bionic_tgkill(static_cast<int>(a1), static_cast<int>(a2),
                                 static_cast<int>(a3));

        // rt_sigprocmask(how, set, oldset, sigsetsize). Both `how` and the mask need
        // translating; see bionic_sigprocmask.
        case 135:
            if (static_cast<size_t>(a4) != sizeof(uint64_t)) { errno = EINVAL; return -1; }
            return kudroid::guest_sigprocmask(static_cast<int>(a1),
                                              reinterpret_cast<const uint64_t*>(a2),
                                              reinterpret_cast<uint64_t*>(a3));

        case 133: // rt_sigsuspend(mask, sigsetsize)
            if (static_cast<size_t>(a2) != sizeof(uint64_t)) { errno = EINVAL; return -1; }
            return kudroid::guest_sigsuspend(reinterpret_cast<const uint64_t*>(a1));

        // rt_sigpending(set, sigsetsize): which signals are pending, in the GUEST's
        // numbering. Reported raw, the guest tests bits against its own constants and
        // reads the wrong ones — the same error as the send, one step later.
        case 136: {
            if (static_cast<size_t>(a2) != sizeof(uint64_t)) { errno = EINVAL; return -1; }
            sigset_t host_pending;
            sigemptyset(&host_pending);
            if (::sigpending(&host_pending) != 0) return -1;
            if (a1 != 0) {
                uint64_t guest_pending = 0;
                for (int host_sig = 1; host_sig < NSIG; ++host_sig) {
                    if (sigismember(&host_pending, host_sig) != 1) continue;
                    const int guest_sig = kudroid::host_signal_to_guest(host_sig);
                    if (guest_sig >= 1 && guest_sig < 65) {
                        guest_pending |= (1ull << (guest_sig - 1));
                    }
                }
                *reinterpret_cast<uint64_t*>(a1) = guest_pending;
            }
            return 0;
        }

        case 160: // uname
            return bionic_uname(reinterpret_cast<struct bionic_utsname*>(a1));

        case 167: // prctl
            return bionic_prctl(static_cast<int>(a1), a2, a3, a4, a5);

        case 169: // gettimeofday
            return bionic_gettimeofday(reinterpret_cast<void*>(a1), reinterpret_cast<void*>(a2));

        case 172: // getpid
            return static_cast<long>(::getpid());

        case 174: // getuid
            return static_cast<long>(::getuid());

        case 175: // geteuid
            return static_cast<long>(::geteuid());

        case 176: // getgid
            return static_cast<long>(::getgid());

        case 177: // getegid
            return static_cast<long>(::getegid());

        case 178: { // gettid
#ifdef __APPLE__
            uint64_t tid = 0;
            pthread_threadid_np(NULL, &tid);
            const long result = static_cast<long>(tid);
#else
            const long result = static_cast<long>(::syscall(SYS_gettid));
#endif
            tid_registry_record(result);
            log_guard_acquire_diag(result, entryX19);
            return result;
        }
        case 198: // socket
            return bionic_socket(static_cast<int>(a1), static_cast<int>(a2), static_cast<int>(a3));

        case 200: // bind
            return bionic_bind(static_cast<int>(a1), reinterpret_cast<const struct sockaddr*>(a2), static_cast<socklen_t>(a3));

        case 203: // connect
            return bionic_connect(static_cast<int>(a1), reinterpret_cast<const struct sockaddr*>(a2), static_cast<socklen_t>(a3));

        case 204: // getsockname
            return bionic_getsockname(static_cast<int>(a1), reinterpret_cast<struct sockaddr*>(a2), reinterpret_cast<socklen_t*>(a3));

        case 205: // getpeername
            return bionic_getpeername(static_cast<int>(a1), reinterpret_cast<struct sockaddr*>(a2), reinterpret_cast<socklen_t*>(a3));

        case 199: // socketpair
            return bionic_socketpair(static_cast<int>(a1), static_cast<int>(a2), static_cast<int>(a3), reinterpret_cast<int*>(a4));

        case 201: // listen
            return bionic_listen(static_cast<int>(a1), static_cast<int>(a2));

        case 202: // accept
            return bionic_accept4(static_cast<int>(a1), reinterpret_cast<struct sockaddr*>(a2), reinterpret_cast<socklen_t*>(a3), 0);

        case 208: // setsockopt
            return bionic_setsockopt(static_cast<int>(a1), static_cast<int>(a2), static_cast<int>(a3), reinterpret_cast<const void*>(a4), static_cast<socklen_t>(a5));

        case 209: // getsockopt
            return bionic_getsockopt(static_cast<int>(a1), static_cast<int>(a2), static_cast<int>(a3), reinterpret_cast<void*>(a4), reinterpret_cast<socklen_t*>(a5));

        case 210: // shutdown
            return bionic_shutdown(static_cast<int>(a1), static_cast<int>(a2));

        case 206: // sendto
            return bionic_sendto(static_cast<int>(a1), reinterpret_cast<const void*>(a2), static_cast<size_t>(a3), static_cast<int>(a4), reinterpret_cast<const struct sockaddr*>(a5), static_cast<socklen_t>(a6));

        case 207: // recvfrom
            return bionic_recvfrom(static_cast<int>(a1), reinterpret_cast<void*>(a2), static_cast<size_t>(a3), static_cast<int>(a4), reinterpret_cast<struct sockaddr*>(a5), reinterpret_cast<socklen_t*>(a6));

        case 72: // pselect6 (the sixth argument is a {sigset, size} pair the shim ignores)
            return bionic_pselect(static_cast<int>(a1), reinterpret_cast<void*>(a2), reinterpret_cast<void*>(a3), reinterpret_cast<void*>(a4), reinterpret_cast<const struct timespec*>(a5), nullptr);

        case 73: // ppoll
            return bionic_ppoll(reinterpret_cast<struct pollfd*>(a1), static_cast<unsigned long>(a2), reinterpret_cast<const struct timespec*>(a3), nullptr);

        case 211: // sendmsg
            return bionic_sendmsg(static_cast<int>(a1), reinterpret_cast<const GuestMsghdr*>(a2), static_cast<int>(a3));

        case 212: // recvmsg
            return bionic_recvmsg(static_cast<int>(a1), reinterpret_cast<GuestMsghdr*>(a2), static_cast<int>(a3));

        case 243: // recvmmsg
            return bionic_recvmmsg(static_cast<int>(a1), reinterpret_cast<GuestMmsghdr*>(a2), static_cast<unsigned int>(a3), static_cast<int>(a4), reinterpret_cast<void*>(a5));

        case 269: // sendmmsg
            return bionic_sendmmsg(static_cast<int>(a1), reinterpret_cast<GuestMmsghdr*>(a2), static_cast<unsigned int>(a3), static_cast<int>(a4));

        case 122: // sched_setaffinity (Linux arm64 syscall)
            // iOS does not support binding threads to CPU cores; return 0 (success)
            return 0;

        case 123: // sched_getaffinity (Linux arm64 syscall)
            // a1=pid, a2=cpusetsize, a3=mask pointer.
            //
            // Returns the NUMBER OF BYTES WRITTEN, which is what the kernel does and
            // what bionic's wrapper relies on:
            //
            //     int rc = __sched_getaffinity(pid, size, set);
            //     if (rc == -1) return -1;
            //     if ((size_t)rc < size) memset((char*)set + rc, 0, size - rc);
            //     return 0;
            //
            // Returning 0 here therefore made bionic memset the ENTIRE mask to zero,
            // handing the guest an affinity set with no CPUs in it. Unity read that
            // back as "Cores = 0" and "0 big (mask: 0x0), 0 little (mask: 0x0)", then
            // sized its job system from it.
            if (a3 != 0 && a2 > 0) {
                const size_t requested = static_cast<size_t>(a2);
                const size_t written = requested < sizeof(unsigned long)
                                           ? requested
                                           : sizeof(unsigned long);
                // Zero only the part being reported: bionic clears the remainder
                // itself, and a mask larger than one word must not be pre-cleared
                // past `written` or the return value would be a lie.
                memset(reinterpret_cast<void*>(a3), 0, written);
                // Same topology as the wrapper, sysconf and /proc/cpuinfo. This was
                // 0xFF regardless of the device.
                const unsigned long online =
                    static_cast<unsigned long>(kudroid::cpu_online_mask());
                memcpy(reinterpret_cast<void*>(a3), &online, written);
                return static_cast<long>(written);
            }
            errno = EFAULT;
            return -1;

        case 215: // munmap
            return bionic_munmap(reinterpret_cast<void*>(a1), static_cast<size_t>(a2));

        case 222: // mmap
            return (long)bionic_mmap(reinterpret_cast<void*>(a1), static_cast<size_t>(a2), static_cast<int>(a3), static_cast<int>(a4), static_cast<int>(a5), static_cast<off_t>(a6));

        case 226: // mprotect
            return bionic_mprotect(reinterpret_cast<void*>(a1), static_cast<size_t>(a2), static_cast<int>(a3));

        case 233: // madvise
            return bionic_madvise(reinterpret_cast<void*>(a1), static_cast<size_t>(a2), static_cast<int>(a3));

        case 242: // accept4
            return bionic_accept4(static_cast<int>(a1), reinterpret_cast<struct sockaddr*>(a2), reinterpret_cast<socklen_t*>(a3), static_cast<int>(a4));

        case 278: // getrandom
            return bionic_getrandom(reinterpret_cast<void*>(a1), static_cast<size_t>(a2), static_cast<unsigned int>(a3));

        case KUDROID_SYS_process_vm_readv: {
            const pid_t pid = static_cast<pid_t>(a1);
            const struct iovec* local_iov = reinterpret_cast<const struct iovec*>(a2);
            const unsigned long liovcnt = static_cast<unsigned long>(a3);
            const struct iovec* remote_iov = reinterpret_cast<const struct iovec*>(a4);
            const unsigned long riovcnt = static_cast<unsigned long>(a5);
            const unsigned long flags = static_cast<unsigned long>(a6);
            return bionic_process_vm_readv(pid, local_iov, liovcnt, remote_iov, riovcnt, flags);
        }
        default:
            break;
    }

    // Unmapped — log once per number, then ENOSYS (never run the wrong host call).
    static long s_unknownSeen[64];
    static int s_unknownCount = 0;
    bool known = false;
    for (int i = 0; i < s_unknownCount; ++i) {
        if (s_unknownSeen[i] == number) { known = true; break; }
    }
    if (!known && s_unknownCount < 64) {
        s_unknownSeen[s_unknownCount++] = number;
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "bionic_syscall: unhandled Linux syscall %ld -> ENOSYS", number);
        logAndroidMessage(4, "KuDroidSyscall", msg);
    }
    errno = ENOSYS;
    return -1;
}

// A guest library may issue `svc #0` directly instead of calling the imported
// libc syscall wrapper. Darwin delivers SIGSYS for that Linux syscall ABI; on
// Apple, emulate the request from the signal context and resume after SVC.
// This keeps the boundary generic for all Linux/Android arm64 guests.
extern "C" bool bionic_handle_guest_syscall_trap(void* context) {
#if defined(__APPLE__) && defined(__aarch64__)
    if (context == nullptr) return false;
    ucontext_t* uc = static_cast<ucontext_t*>(context);
    uint32_t* pc = reinterpret_cast<uint32_t*>(uc->uc_mcontext->__ss.__pc);
    uintptr_t svc_pc = reinterpret_cast<uintptr_t>(pc);
    uint32_t inst = *pc;
    if ((inst & 0xFFE0001F) != 0xD4000001) {
        if (svc_pc < 4) return false;
        pc--;
        inst = *pc;
        if ((inst & 0xFFE0001F) != 0xD4000001) return false;
        svc_pc -= 4;
    }
    if (uc->uc_mcontext->__ss.__x[8] > 1000) return false;
    const long result = bionic_syscall(
        static_cast<long>(uc->uc_mcontext->__ss.__x[8]),
        uc->uc_mcontext->__ss.__x[0], uc->uc_mcontext->__ss.__x[1],
        uc->uc_mcontext->__ss.__x[2], uc->uc_mcontext->__ss.__x[3],
        uc->uc_mcontext->__ss.__x[4], uc->uc_mcontext->__ss.__x[5]);
    uc->uc_mcontext->__ss.__x[0] = static_cast<uint64_t>(result);
    uc->uc_mcontext->__ss.__pc = svc_pc + 4;
    return true;
#else
    (void)context;
    return false;
#endif
}

