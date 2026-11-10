// Linux MREMAP flags (asm-generic/mman.h)
#define MREMAP_MAYMOVE 1
#define MREMAP_FIXED 2

extern "C" void* bionic_mremap(void *old_address, size_t old_size, size_t new_size, int flags, void *new_address) {
#ifndef __APPLE__
    // Linux host: delegate to the real syscall.
    return ::mremap(old_address, old_size, new_size, flags, new_address);
#else
    (void)new_address;
    if (kudroid::ExecMemory::GuestMemoryOverlaps(old_address, old_size)) {
        // Prepared code cannot move to an unprepared address.
        if (new_size <= old_size && !(flags & MREMAP_FIXED)) return old_address;
        errno = ENOMEM;
        return MAP_FAILED;
    }
    // No mremap on Darwin; emulate Linux semantics:
    if (new_size == old_size) return old_address; // no-op
    if (flags & (MREMAP_MAYMOVE | MREMAP_FIXED)) {
        void* new_ptr = mmap(NULL, new_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (new_ptr != MAP_FAILED) {
            // Copy only min(old,new) to avoid overflowing the new mapping.
            std::memcpy(new_ptr, old_address, std::min(old_size, new_size));
            munmap(old_address, old_size);
            return new_ptr;
        }
        errno = ENOMEM;
        return MAP_FAILED;
    }
    // Without MAYMOVE only shrinking in place succeeds; growing fails like Linux.
    if (new_size < old_size) return old_address;
    errno = ENOMEM;
    return MAP_FAILED;
#endif
}

#define AT_HWCAP 16
#define AT_PAGESZ 6
#define AT_RANDOM 25
#define AT_HWCAP2 26

#define HWCAP_FP (1 << 0)
#define HWCAP_ASIMD (1 << 1)
#define HWCAP_EVTSTRM (1 << 2)
#define HWCAP_AES (1 << 3)
#define HWCAP_PMULL (1 << 4)
#define HWCAP_SHA1 (1 << 5)
#define HWCAP_SHA2 (1 << 6)
#define HWCAP_CRC32 (1 << 7)

static const uint8_t g_kudroid_random_aux[16] = {
    0x4b, 0x75, 0x64, 0x72, 0x6f, 0x69, 0x64, 0x53,
    0x65, 0x63, 0x75, 0x72, 0x69, 0x74, 0x79, 0x21
};

extern "C" unsigned long bionic_getauxval(unsigned long type) {
    if (type == AT_HWCAP) {
        return HWCAP_FP | HWCAP_ASIMD | HWCAP_EVTSTRM | HWCAP_AES | HWCAP_PMULL | HWCAP_SHA1 | HWCAP_SHA2 | HWCAP_CRC32;
    }
    if (type == AT_HWCAP2) {
        return 0x00000fff; // ATOMICS, FPHP, ASIMDHP, FLAGM, JSCVT, FCMA, LRCPC, DCPOP, SHA3, SM3, SM4, ASIMDDP
    }
    if (type == AT_PAGESZ) {
        // Tr kch thc trang tht ca host — trnh engine read pagesize=0.
        long pagesize = ::sysconf(_SC_PAGESIZE);
        return pagesize > 0 ? static_cast<unsigned long>(pagesize) : 4096;
    }
    if (type == AT_RANDOM) {
        return reinterpret_cast<unsigned long>(g_kudroid_random_aux);
    }
    return 0;
}

extern "C" ssize_t bionic_getrandom(void *buf, size_t buflen, unsigned int flags) {
    (void)flags;
    if (!buf || buflen == 0) return 0;
#ifdef __APPLE__
    // arc4random_buf is the preferred secure random source on Apple platforms.
    arc4random_buf(buf, buflen);
    static std::atomic<int> entropyCalls{0};
    if (entropyCalls.fetch_add(1, std::memory_order_relaxed) < 16) {
        const int savedErrno = errno;
        std::fprintf(stderr, "[KuDroidNet] getrandom count=%zu flags=0x%x -> %zu\n", buflen, flags, buflen);
        errno = savedErrno;
    }
    return static_cast<ssize_t>(buflen);
#else
    int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    ssize_t ret = ::read(fd, buf, buflen);
    ::close(fd);
    return ret;
#endif
}

// sysconf, through the GUEST's constant numbering.
//
// bionic and glibc do not agree on the values, and the guest is always bionic. The cases
// below therefore list both where they differ — and the pair that was missing is exactly
// the pair that matters here: bionic's _SC_PHYS_PAGES is 98 and _SC_AVPHYS_PAGES is 99,
// while only the glibc 85/86 were handled. A guest asking for physical memory fell
// through to Darwin's ::sysconf(98), which means something else entirely.
extern "C" long bionic_sysconf(int name) {
    switch (name) {
        case 0: // _SC_ARG_MAX
            return 131072;
        case 1: // _SC_CHILD_MAX
            return 1024;
        case 2: // _SC_CLK_TCK
            return 100;
        case 3: // _SC_NGROUPS_MAX
            return 65536;
        case 4: // _SC_OPEN_MAX
            return 32768;
        case 30: // _SC_PAGESIZE (glibc)
        case 39: // _SC_PAGESIZE (bionic)
        case 40: // _SC_PAGE_SIZE (bionic)
        {
            long pz = ::sysconf(_SC_PAGESIZE);
            return pz > 0 ? pz : 16384;
        }
        case 83: // _SC_NPROCESSORS_CONF (glibc)
        case 84: // _SC_NPROCESSORS_ONLN (glibc)
        case 96: // _SC_NPROCESSORS_CONF (bionic)
        case 97: // _SC_NPROCESSORS_ONLN (bionic)
        {
            // The same source as /proc/cpuinfo, /sys/.../present and the affinity mask.
            // It used to be std::thread::hardware_concurrency() with a fallback of 8,
            // independent of every other surface — so a guest cross-checking two of them
            // could see different machines.
            return static_cast<long>(kudroid::query_cpu_topology().total_cores);
        }
        case 85: // _SC_PHYS_PAGES (glibc)
        case 98: // _SC_PHYS_PAGES (bionic)
        {
            long pz = ::sysconf(_SC_PAGESIZE);
            if (pz <= 0) pz = 16384;
            // The real device figure, not a constant. This said 8 GB on every device.
            return static_cast<long>(kudroid::query_system_memory().total_bytes /
                                     static_cast<uint64_t>(pz));
        }
        case 86: // _SC_AVPHYS_PAGES (glibc)
        case 99: // _SC_AVPHYS_PAGES (bionic)
        {
            long pz = ::sysconf(_SC_PAGESIZE);
            if (pz <= 0) pz = 16384;
            return static_cast<long>(kudroid::query_system_memory().available_bytes /
                                     static_cast<uint64_t>(pz));
        }
        default:
            break;
    }
    return ::sysconf(name);
}

// ashmem (Android shared memory): iOS lacks it, so back it with anonymous mmap + fake fd.
static std::mutex g_ashmem_mtx;
static std::unordered_map<int, int> g_ashmem_prot;      // fd -> granted prot
static std::unordered_map<int, void*> g_ashmem_region;  // fake fd -> region
static std::unordered_map<int, size_t> g_ashmem_size;   // fake fd -> size
static std::atomic<int> g_ashmem_fake_fd{0x40000000};   // fake fds start high to avoid real fds

static void ashmem_forget(int fd) {
    std::lock_guard<std::mutex> lock(g_ashmem_mtx);
    g_ashmem_prot.erase(fd);
    auto it = g_ashmem_region.find(fd);
    if (it != g_ashmem_region.end()) {
        ::munmap(it->second, g_ashmem_size[fd]);
        g_ashmem_region.erase(it);
        g_ashmem_size.erase(fd);
    }
}

extern "C" int bionic_ashmem_create_region(const char* name, size_t size) {
    (void)name;
    if (size == 0) size = 1;
    // 1) Try POSIX shm (works on macOS).
    static std::atomic<uint32_t> counter{0};
    char shm_name[64];
    std::snprintf(shm_name, sizeof(shm_name), "/kudroid_ashmem_%d_%u", ::getpid(), counter.fetch_add(1));
    int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
        shm_unlink(shm_name);
        if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
            // fd stays valid; later mmap fails if the size overruns.
        }
        std::lock_guard<std::mutex> lock(g_ashmem_mtx);
        g_ashmem_prot[fd] = PROT_READ | PROT_WRITE;
        return fd;
    }

    // 2) Fallback (iOS): anonymous mmap as the backing region + fake fd.
    void* base = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return -1;
    std::memset(base, 0, size);
    const int fake = g_ashmem_fake_fd.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(g_ashmem_mtx);
        g_ashmem_prot[fake] = PROT_READ | PROT_WRITE;
        g_ashmem_region[fake] = base;
        g_ashmem_size[fake] = size;
    }
    logAndroidMessage(4, "KuDroidSyscall", "ashmem_create_region fallback: fake fd=" +
                      std::to_string(fake) + " size=" + std::to_string(size));
    return fake;
}

extern "C" int bionic_ashmem_set_name(int fd, const char* name) {
    (void)fd; (void)name;
    return 0; // debug name only — nothing to store
}

extern "C" int bionic_ashmem_set_prot_region(int fd, int prot) {
    std::lock_guard<std::mutex> lock(g_ashmem_mtx);
    g_ashmem_prot[fd] = prot;
    return 0;
}

static bool ashmem_prot_allows(int fd, int prot) {
    if (fd < 0) return true;
    std::lock_guard<std::mutex> lock(g_ashmem_mtx);
    auto it = g_ashmem_prot.find(fd);
    if (it == g_ashmem_prot.end()) return true; // not an ashmem fd
    return (prot & ~it->second) == 0;
}

// bionic_mmap helper: return the backing region for a fake ashmem fd.
extern "C" void* bionic_ashmem_mmap_fd(int fd, size_t length) {
    std::lock_guard<std::mutex> lock(g_ashmem_mtx);
    auto it = g_ashmem_region.find(fd);
    if (it == g_ashmem_region.end()) return nullptr;
    if (length > g_ashmem_size[fd]) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    return it->second;
}

// --- Linux-Specific Syscalls ---

struct FutexWaitQueue {
    std::mutex mtx;
    std::condition_variable cv;
    int waiters = 0; // guarded by g_futexGlobalMtx
};
// shared_ptr keeps the queue alive while a thread sleeps in cv.wait.
static std::unordered_map<uint32_t*, std::shared_ptr<FutexWaitQueue>> g_futexQueues;
static std::mutex g_futexGlobalMtx;

// Drop the waiter count; erase the entry when nobody remains.
// Must be called without g_futexGlobalMtx held (lock order: global before queue-mtx).
static void futex_leave(uint32_t* uaddr, const std::shared_ptr<FutexWaitQueue>& q) {
    std::unique_lock<std::mutex> lock(g_futexGlobalMtx);
    if (--q->waiters <= 0) {
        auto it = g_futexQueues.find(uaddr);
        if (it != g_futexQueues.end() && it->second == q) {
            g_futexQueues.erase(it);
        }
    }
}

// Futex command constants (Linux).
#define FUTEX_WAIT           0
#define FUTEX_WAKE           1
#define FUTEX_REQUEUE        3
#define FUTEX_CMP_REQUEUE    4
#define FUTEX_WAIT_BITSET    9
#define FUTEX_WAKE_BITSET    10
#define FUTEX_PRIVATE_FLAG   128
#define FUTEX_CLOCK_REALTIME 256
#define FUTEX_BITSET_MATCH_ANY 0xffffffff

// Bounded trace of futex waits. Threads that implement timers are always traced (their
// wait is the whole story when a timed callback never fires); every other thread is
// traced only for long waits, from a small shared budget so the log cannot flood.
static std::atomic<int> g_futexTraceBudget{40};
static std::atomic<int> g_futexTimerBudget{400};
static std::atomic<uint32_t*> g_timerFutexAddr{nullptr};

// 0 = ordinary thread, 1 = thread-pool worker, 2 = the timer scheduler. Cached once the
// thread has a name.
static int futex_thread_class() {
    static thread_local int cls = -1;
    if (cls >= 0) return cls;
    char name[32] = {0};
    pthread_getname_np(pthread_self(), name, sizeof(name));
    if (name[0] == '\0') return 0;
    cls = std::strcmp(name, "Timer-Scheduler") == 0 ? 2 : std::strstr(name, "Threadpool") ? 1 : 0;
    return cls;
}

static bool futex_trace_enter(uint32_t* uaddr, uint32_t val, const struct timespec* timeout,
                              const char* kind) {
    const int cls = futex_thread_class();
    const long long ms = timeout ? static_cast<long long>(timeout->tv_sec) * 1000 +
                                       timeout->tv_nsec / 1000000
                                 : -1;
    if (cls == 0) {
        if (ms < 20) return false;
        if (g_futexTraceBudget.fetch_sub(1, std::memory_order_relaxed) <= 0) return false;
    } else if (g_futexTimerBudget.fetch_sub(1, std::memory_order_relaxed) <= 0) {
        return false;
    }
    if (cls == 2) g_timerFutexAddr.store(uaddr, std::memory_order_relaxed);
    char name[32] = {0};
    pthread_getname_np(pthread_self(), name, sizeof(name));
    char msg[192];
    snprintf(msg, sizeof(msg), "futex-%s enter name=%s uaddr=%p val=%u timeout_ms=%lld", kind,
             name, static_cast<void*>(uaddr), val, ms);
    kudroid_persistent_breadcrumb(msg);
    return true;
}
static void futex_trace_exit(uint32_t* uaddr, const char* how,
                             std::chrono::steady_clock::time_point t0) {
    const long long el = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0).count();
    char name[32] = {0};
    pthread_getname_np(pthread_self(), name, sizeof(name));
    char msg[160];
    snprintf(msg, sizeof(msg), "futex-wait exit name=%s uaddr=%p %s elapsed_ms=%lld", name,
             static_cast<void*>(uaddr), how, el);
    kudroid_persistent_breadcrumb(msg);
}
static void futex_trace_wake(uint32_t* uaddr, uint32_t count, int woken) {
    if (uaddr != g_timerFutexAddr.load(std::memory_order_relaxed)) return;
    static std::atomic<int> budget{200};
    if (budget.fetch_sub(1, std::memory_order_relaxed) <= 0) return;
    char name[32] = {0};
    pthread_getname_np(pthread_self(), name, sizeof(name));
    char msg[160];
    snprintf(msg, sizeof(msg), "futex-wake on timer addr=%p by=%s count=%u queue=%s",
             static_cast<void*>(uaddr), name, count, woken ? "present" : "absent");
    kudroid_persistent_breadcrumb(msg);
}

extern "C" int bionic_futex(uint32_t *uaddr, int futex_op, uint32_t val, const struct timespec *timeout, uint32_t *uaddr2, uint32_t val3) {
    int cmd = futex_op & 127; // remove private flag
    (void)uaddr2;

    if (cmd == FUTEX_WAIT || cmd == FUTEX_WAIT_BITSET) {
        // FUTEX_WAIT_BITSET with a bitset of 0 is invalid.
        if (cmd == FUTEX_WAIT_BITSET && val3 == 0) { errno = EINVAL; return -1; }

        std::shared_ptr<FutexWaitQueue> q;
        {
            std::unique_lock<std::mutex> lock(g_futexGlobalMtx);
            auto& slot = g_futexQueues[uaddr];
            if (!slot) slot = std::make_shared<FutexWaitQueue>();
            q = slot;
            q->waiters++;
        }

        std::unique_lock<std::mutex> qLock(q->mtx);

        // Linux: nu *uaddr != val khi vo wait → EAGAIN ngay.
        if (*uaddr != val) {
            qLock.unlock(); // th queue-mtx before khi ly global (th t kha)
            futex_leave(uaddr, q);
            errno = guest_errno(EAGAIN);
            return -1;
        }

        if (timeout) {
            // FUTEX_WAIT takes a RELATIVE timeout. FUTEX_WAIT_BITSET takes an
            // ABSOLUTE deadline. This treated BOTH as absolute, and the difference is
            // not subtle on a device that has been up for a while.
            //
            // Unity's asset threads pass FUTEX_WAIT (op=128 = FUTEX_WAIT |
            // FUTEX_PRIVATE_FLAG) with a small relative timeout — tens of
            // milliseconds, so tv_sec is 0. Subtracting CLOCK_MONOTONIC from that
            // gave remSec = 0 - 528730 on a phone six days into its uptime, so every
            // wait returned ETIMEDOUT before waiting at all and the guest spun
            // instead of progressing. When tv_sec was large enough to survive the
            // subtraction the other branch ran and the 24h cap parked the thread for
            // a day. Either way AssetGarbageCollectorHelper never came back, and the
            // log ended on the prctl that named it.
            const bool absolute = (cmd == FUTEX_WAIT_BITSET);

            // The clock only matters for an absolute deadline. FUTEX_CLOCK_REALTIME
            // selects CLOCK_REALTIME; without it the deadline is CLOCK_MONOTONIC.
            const bool realtime = (futex_op & FUTEX_CLOCK_REALTIME) != 0;
            const clockid_t deadline_clock = kudroid::host_clock_for_guest(realtime ? 0 : 1);

            const BlockingWaitScope tracked(WaitKind::kFutexTimed, uaddr,
                                            guest_return_address(6));

            if (!absolute) {
                // Relative: wait exactly as long as asked. A negative or absurd value
                // is rejected the way the kernel does rather than being clamped into a
                // long sleep.
                if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
                    timeout->tv_nsec >= 1000000000) {
                    qLock.unlock();
                    futex_leave(uaddr, q);
                    errno = EINVAL;
                    return -1;
                }
                // Tell the registry how long this wait asked for. An idle worker that
                // requested a long timeout and is inside it is not stalled, and
                // reporting it as such is worse than saying nothing: the captured
                // ULTRAKILL log's only stall line was exactly that, an
                // AssetGarbageCollectorHelper doing what it asked to do, while the
                // wedged main thread went unmentioned.
                blocking_wait_note_budget(
                    static_cast<uint64_t>(timeout->tv_sec) * 1000ull +
                    static_cast<uint64_t>(timeout->tv_nsec) / 1000000ull);
                const auto duration = std::chrono::seconds(timeout->tv_sec) +
                                      std::chrono::nanoseconds(timeout->tv_nsec);
                const bool traced = futex_trace_enter(uaddr, val, timeout, "timed");
                const auto t0 = std::chrono::steady_clock::now();
                if (q->cv.wait_for(qLock, duration) == std::cv_status::timeout) {
                    qLock.unlock();
                    futex_leave(uaddr, q);
                    if (traced) futex_trace_exit(uaddr, "timeout", t0);
                    errno = guest_errno(ETIMEDOUT);
                    return -1;
                }
                if (traced) futex_trace_exit(uaddr, "woken", t0);
            } else {
                // Absolute: re-derive the remaining time on every pass. Waiting on a
                // capped slice and looping keeps a far-future deadline from either
                // overflowing the duration or being silently shortened to the cap —
                // the previous code turned a distant deadline into a 24-hour sleep and
                // then reported a timeout that had not happened.
                for (bool first = true;; first = false) {
                    struct timespec now;
                    ::clock_gettime(deadline_clock, &now);
                    int64_t remSec = int64_t(timeout->tv_sec) - int64_t(now.tv_sec);
                    int64_t remNs = int64_t(timeout->tv_nsec) - int64_t(now.tv_nsec);
                    if (remNs < 0) { remSec -= 1; remNs += 1000000000; }
                    if (remSec < 0) {
                        qLock.unlock();
                        futex_leave(uaddr, q);
                        errno = guest_errno(ETIMEDOUT);
                        return -1;
                    }
                    // The budget is the time remaining ON THE FIRST PASS, which is the
                    // total the caller asked for. Refreshing it every pass would be
                    // wrong: the remaining time shrinks as elapsed time grows, so the
                    // two would cross at half the requested wait and the report would
                    // fire while the wait was still legitimate.
                    if (first) {
                        blocking_wait_note_budget(static_cast<uint64_t>(remSec) * 1000ull +
                                                  static_cast<uint64_t>(remNs) / 1000000ull);
                    }
                    int64_t sliceSec = remSec;
                    int64_t sliceNs = remNs;
                    const bool partial = sliceSec > 86400;
                    if (partial) { sliceSec = 86400; sliceNs = 0; }
                    const auto duration = std::chrono::seconds(sliceSec) +
                                          std::chrono::nanoseconds(sliceNs);
                    if (q->cv.wait_for(qLock, duration) != std::cv_status::timeout) {
                        break;  // woken (or spurious — the guest re-checks *uaddr)
                    }
                    if (!partial) {
                        qLock.unlock();
                        futex_leave(uaddr, q);
                        errno = guest_errno(ETIMEDOUT);
                        return -1;
                    }
                    // A slice expired but the deadline has not: keep waiting.
                }
            }
        } else {
            // No timeout: this returns only when someone wakes it. If the wake never
            // comes the thread is parked for the life of the process, which is what
            // the registry exists to make visible.
            const BlockingWaitScope tracked(WaitKind::kFutex, uaddr,
                                            guest_return_address(6));
            const bool traced = futex_trace_enter(uaddr, val, nullptr, "untimed");
            const auto t0 = std::chrono::steady_clock::now();
            q->cv.wait(qLock);
            if (traced) futex_trace_exit(uaddr, "woken", t0);
        }
        qLock.unlock();
        futex_leave(uaddr, q);
        return 0;
    } else if (cmd == FUTEX_WAKE || cmd == FUTEX_WAKE_BITSET) {
        // Copy the queue under the global lock: a leaving waiter may erase the
        // entry the moment it is released, so the iterator must not be touched
        // after unlock (use-after-erase crashed notify_one on garbage).
        std::shared_ptr<FutexWaitQueue> q;
        {
            std::unique_lock<std::mutex> lock(g_futexGlobalMtx);
            auto it = g_futexQueues.find(uaddr);
            if (it != g_futexQueues.end()) q = it->second;
        }
        futex_trace_wake(uaddr, val, q ? 1 : 0);
        if (q) {
            std::lock_guard<std::mutex> qLock(q->mtx);
            if (val == 1) q->cv.notify_one();
            else q->cv.notify_all();
            return val;
        }
        return 0;
    } else if (cmd == FUTEX_REQUEUE || cmd == FUTEX_CMP_REQUEUE) {
        // FUTEX_CMP_REQUEUE requires *uaddr == val3 before requeueing.
        if (cmd == FUTEX_CMP_REQUEUE && *uaddr != val3) { errno = guest_errno(EAGAIN); return -1; }

        std::shared_ptr<FutexWaitQueue> q;
        {
            std::unique_lock<std::mutex> lock(g_futexGlobalMtx);
            auto it = g_futexQueues.find(uaddr);
            if (it != g_futexQueues.end()) q = it->second;
        }
        if (q) {
            std::lock_guard<std::mutex> qLock(q->mtx);
            if (val == 1) q->cv.notify_one();
            else q->cv.notify_all();
            return val;
        }
        return 0;
    }
    errno = ENOSYS;
    return -1;
}

// Dynamic Loading (dlfcn): serialize ANGLE/MoltenVK loads; concurrent Metal init aborts.
extern "C" void* bionic_dlopen(const char* filename, int flags) {
    static std::mutex g_gpuFrameworkMtx;
    (void)flags;
    logAndroidMessage(4, "KuDroidSyscall", std::string("bionic_dlopen: requested ") + (filename ? filename : "NULL"));

    if (!filename) {
        return RTLD_DEFAULT;
    }

    // A guest .so LibraryManager mapped gets its own handle so dlsym searches that library alone.
    // Checked first: host dlopen cannot parse ELF binaries and crashes dyld on Darwin.
    if (kudroid_guest_library_open != nullptr) {
        if (void* guest = kudroid_guest_library_open(filename)) {
            logAndroidMessage(4, "KuDroidSyscall",
                              std::string("bionic_dlopen: resolved to guest library handle for ") + filename);
            return guest;
        }
    }

    // Map Android GPU library requests directly to embedded frameworks.
    if (strstr(filename, "libEGL.so") || strstr(filename, "libGLESv2.so") || 
        strstr(filename, "libGLESv1_CM.so") || strstr(filename, "libGLESv3.so")) {
        const char* fw = strstr(filename, "libEGL") ? "libEGL.framework/libEGL"
                                                    : "libGLESv2.framework/libGLESv2";

        std::lock_guard<std::mutex> gpuLock(g_gpuFrameworkMtx);
        void* handle = kudroid::dlopen_bundled_framework(fw, RTLD_NOW | RTLD_GLOBAL);
        if (handle) {
            logAndroidMessage(4, "KuDroidGPU", std::string("Successfully loaded ") + fw);
            return handle;
        }
    }
    
    if (strstr(filename, "libvulkan.so")) {
        std::lock_guard<std::mutex> gpuLock(g_gpuFrameworkMtx);
        void* handle = kudroid::dlopen_bundled_framework("MoltenVK.framework/MoltenVK",
                                                         RTLD_NOW | RTLD_GLOBAL);
        if (handle) {
            logAndroidMessage(4, "KuDroidGPU", "Successfully loaded MoltenVK.framework");
            return handle;
        }
    }

    // Host dlopen: only for native host binaries (e.g. frameworks/dylibs).
    // Never pass ELF .so files to host dyld.
    const bool is_elf_so = (strstr(filename, ".so") != nullptr);
    if (!is_elf_so) {
        void* real = ::dlopen(filename, flags ? flags : RTLD_NOW);
        if (real) {
            logAndroidMessage(4, "KuDroidSyscall", std::string("bionic_dlopen: resolved to real host handle for ") + filename);
            return real;
        }
    }

#define DUMMY_HANDLE ((void*)0x4B5544524F494421ULL) // "KUDROID!" as a handle

    // Emulate the Android linker: pretend the requested library resolved.
    logAndroidMessage(4, "KuDroidSyscall", std::string("bionic_dlopen: fallback returning DUMMY_HANDLE for ") + filename);
    return DUMMY_HANDLE;
}

// GraphicsShim MUST intercept Vulkan/EGL surface symbols BEFORE the real
// MoltenVK/ANGLE handle is consulted, otherwise the game gets the real
// vkGetInstanceProcAddr and never sees vkCreateAndroidSurfaceKHR. Every
// egl*/ANativeWindow* entry also goes through the shim: the wrappers forward
// to ANGLE internally but add NULL-guards and detailed logging (e.g.
// TriangleGLES calls eglInitialize(display, 0, 0) with NULL out-params which
// some ANGLE builds dereference). Anything not in the shim table falls
// through to the real handle below.
static bool isShimPriority(const char* s) {
    if (!s) return false;
    if (strncmp(s, "egl", 3) == 0) return true;
    if (strncmp(s, "gl", 2) == 0) return true;
    if (strncmp(s, "ANativeWindow", 13) == 0) return true;
    return strcmp(s, "vkGetInstanceProcAddr") == 0 ||
           strcmp(s, "vkEnumerateInstanceExtensionProperties") == 0 ||
           strcmp(s, "vkCreateAndroidSurfaceKHR") == 0;
}

static thread_local std::string s_guestDlError;
static thread_local bool s_guestDlErrorPending = false;

extern "C" void* bionic_dlsym(void* handle, const char* symbol) {
    if (!symbol) return nullptr;

    if (isShimPriority(symbol)) {
        size_t count = 0;
        const SymbolEntry* symbols = get_graphics_symbols(&count);
        for (size_t i = 0; i < count; ++i) {
            if (strcmp(symbols[i].name, symbol) == 0) {
                logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via GraphicsShim (priority)");
                return symbols[i].address;
            }
        }
    }

    // Direct routing for graphics API prefixes
    if (symbol[0] == 'g' && symbol[1] == 'l') {
        if (void* f = kudroid::get_gl_func(symbol)) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via GraphicsShim (gl)");
            return f;
        }
    }
    if (symbol[0] == 'e' && symbol[1] == 'g' && symbol[2] == 'l') {
        if (void* f = kudroid::get_egl_func(symbol)) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via GraphicsShim (egl)");
            return f;
        }
    }
    if (symbol[0] == 'v' && symbol[1] == 'k') {
        if (void* f = kudroid::get_vk_func(symbol)) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via GraphicsShim (vk)");
            return f;
        }
    }

    // A guest library handle before ::dlsym, which would dereference a pointer it
    // does not own. The lookup validates the handle itself, so an unrelated one
    // simply returns null here.
    bool guest_handle = false;
    if (handle && handle != RTLD_DEFAULT && handle != DUMMY_HANDLE) {
        if (kudroid_guest_library_owns != nullptr && kudroid_guest_library_owns(handle)) {
            guest_handle = true;
            if (kudroid_guest_library_symbol != nullptr) {
                if (void* guest = kudroid_guest_library_symbol(handle, symbol)) {
                    logAndroidMessage(2, "KuDroidSyscall",
                                      std::string("bionic_dlsym: [") + symbol + "] found in guest library handle");
                    return guest;
                }
            }
        }
    }

    // For a real host handle, prefer its own symbols first. A guest handle is
    // excluded: it is a LibraryManager pointer, and ::dlsym would dereference it as
    // one of its own — a SIGSEGV rather than a miss.
    if (handle && handle != RTLD_DEFAULT && handle != DUMMY_HANDLE && !guest_handle) {
        if (void* real = ::dlsym(handle, symbol)) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] found in real handle");
            return real;
        }
    }

    // Route through the shim symbol tables (syscall/graphics/input/audio).
    size_t count = 0;
    const SymbolEntry* symbols = get_syscall_symbols(&count);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(symbols[i].name, symbol) == 0) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via SyscallShim");
            return symbols[i].address;
        }
    }
    symbols = get_graphics_symbols(&count);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(symbols[i].name, symbol) == 0) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via GraphicsShim");
            return symbols[i].address;
        }
    }
    symbols = get_input_symbols(&count);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(symbols[i].name, symbol) == 0) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via InputShim");
            return symbols[i].address;
        }
    }
    symbols = get_audio_symbols(&count);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(symbols[i].name, symbol) == 0) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via AudioShim");
            return symbols[i].address;
        }
    }
    symbols = get_media_symbols(&count);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(symbols[i].name, symbol) == 0) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via MediaShim");
            return symbols[i].address;
        }
    }

    // If it was a dummy handle, do NOT search RTLD_DEFAULT globally.
    // Fall back to guest libs; Android libs like libc.so have no host counterpart.
    if (handle == DUMMY_HANDLE) {
        if (kudroid_guest_symbol_lookup) {
            if (void* guest = kudroid_guest_symbol_lookup(symbol)) {
                if (!is_universal_dummy(guest)) {
                    logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via guest LibraryManager");
                    return guest;
                }
            }
        }
        s_guestDlError = std::string("dlsym: undefined symbol: ") + symbol;
        s_guestDlErrorPending = true;
        logAndroidMessage(5, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] NOT FOUND (in dummy handle)");
        return nullptr;
    }

    // Fall back to the host process image (ANGLE, MoltenVK, libc, ...).
    void* fallback = ::dlsym(RTLD_DEFAULT, symbol);
    if (fallback) {
        logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via RTLD_DEFAULT fallback");
    } else {
        logAndroidMessage(5, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] NOT FOUND");
    }
    return fallback;
}

extern "C" void* bionic_android_dlopen_ext(const char* filename, int flags, const void* extinfo) {
    (void)extinfo;
    return bionic_dlopen(filename, flags);
}

extern "C" void* bionic_loader_dlopen(const char* filename, int flags, const void* caller_addr) {
    (void)caller_addr;
    return bionic_dlopen(filename, flags);
}

extern "C" void* bionic_loader_android_dlopen_ext(const char* filename, int flags, const void* extinfo, const void* caller_addr) {
    (void)extinfo;
    (void)caller_addr;
    return bionic_dlopen(filename, flags);
}

extern "C" int bionic_dlclose(void* handle) {
    // DUMMY_HANDLE is a fake handle for Android libs missing on the host.
    if (!handle || handle == RTLD_DEFAULT || handle == DUMMY_HANDLE) return 0;
    // A guest library handle is ours, not the host loader's. Guest mappings live for
    // the life of the process on purpose (unmapping one while another thread executes
    // inside it is the crash that made LibraryManager a process-wide singleton), so
    // report success without touching the mapping.
    if (kudroid_guest_library_owns != nullptr && kudroid_guest_library_owns(handle)) {
        return 0;
    }
    return ::dlclose(handle);
}

extern "C" char* bionic_dlerror() {
    if (s_guestDlErrorPending) {
        s_guestDlErrorPending = false;
        ::dlerror();
        return s_guestDlError.data();
    }
    return ::dlerror();
}

