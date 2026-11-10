extern "C" int bionic_clock_gettime(int clock_id, struct timespec *tp) {
    if (!tp) {
        errno = EFAULT;
        return -1;
    }
    return ::clock_gettime(kudroid::host_clock_for_guest(clock_id), tp);
}

extern "C" int bionic_clock_gettime64(int clock_id, struct timespec *tp) {
    return bionic_clock_gettime(clock_id, tp);
}

// clock_getres with the guest's clock ids. Without it the call went to the host with a
// Linux id the host rejects, and runtime code that probes CLOCK_MONOTONIC support
// through it concluded there was none and timed itself from the wall clock instead.
extern "C" int bionic_clock_getres(int clock_id, struct timespec* res) {
    struct timespec host{};
    if (::clock_getres(kudroid::host_clock_for_guest(clock_id), &host) != 0) return -1;
    if (res) *res = host;
    return 0;
}

// bionic's struct timeval is {long tv_sec; long tv_usec;}. The host's microseconds field
// is 32 bits, so a host gettimeofday into the guest's buffer left the upper half of
// tv_usec as whatever the guest's stack held. Callers that read it as 64 bits then saw
// "now" shifted by a stack-dependent multiple of 4295 seconds, which turned a 5-second
// timer into a multi-day wait.
extern "C" int bionic_gettimeofday(void* tv, void* tz) {
    if (tv) {
        struct timeval host{};
        if (::gettimeofday(&host, nullptr) != 0) return -1;
        const int64_t out[2] = {static_cast<int64_t>(host.tv_sec),
                                static_cast<int64_t>(host.tv_usec)};
        std::memcpy(tv, out, sizeof(out));
    }
    if (tz) {
        const int32_t zone[2] = {0, 0};  // struct timezone is obsolete; report UTC, no DST
        std::memcpy(tz, zone, sizeof(zone));
    }
    return 0;
}

extern "C" int bionic___isfinite(double d) { return std::isfinite(d) ? 1 : 0; }
extern "C" int bionic___isfinitef(float f) { return std::isfinite(f) ? 1 : 0; }
extern "C" int bionic___isfinitel(long double ld) { return std::isfinite(ld) ? 1 : 0; }

extern "C" int bionic_timer_create(int clockid, void* sevp, void** timerid) {
    (void)clockid; (void)sevp;
    if (!timerid) { errno = EINVAL; return -1; }
    static uintptr_t s_timerId = 0x1000;
    *timerid = reinterpret_cast<void*>(++s_timerId);
    return 0;
}

extern "C" int bionic_timer_settime(void* timerid, int flags, const void* new_value, void* old_value) {
    (void)timerid; (void)flags; (void)new_value; (void)old_value;
    return 0;
}

extern "C" int bionic_timer_gettime(void* timerid, void* curr_value) {
    (void)timerid;
    if (curr_value) std::memset(curr_value, 0, sizeof(struct timespec) * 2);
    return 0;
}

extern "C" int bionic_timer_delete(void* timerid) {
    (void)timerid;
    return 0;
}

#ifdef __APPLE__
static size_t prot_prefix_len(uintptr_t addr, size_t len, vm_prot_t required);
#endif

// The guest's sigaltstack, through the guest's own stack_t layout.
//
// Linux's is {sp, flags, size}; Darwin's is {sp, size, flags}. Taking a `stack_t*`
// here and dereferencing it read those two fields swapped: a guest asking for a 64KB
// alternate stack passed ss_flags=65536, which is neither SS_ONSTACK nor SS_DISABLE,
// the host rejected it — and the old shim logged the failure then returned 0 anyway, so
// the guest went on believing it had an alternate stack it did not have. A stack
// overflow then had nowhere to run its handler, which is exactly the case an alternate
// stack exists for.
extern "C" int bionic_sigaltstack(const void* ss, void* oss) {
#ifdef __APPLE__
    // A direct guest SVC can carry a pointer that is plausible but not accessible in
    // this address space. GuestSignals rejects null and the low page; the Mach-based
    // protection check lives here.
    //
    // Each side is checked for what it is actually used for: `ss` is read from and `oss`
    // is written to. Checking both for readability would approve an `oss` in a read-only
    // page and fault on the store instead. Both must be accessible ENTIRELY — unlike a
    // bulk copy there is no meaningful partial stack_t, so a short prefix is EFAULT.
    if ((ss != nullptr && prot_prefix_len(reinterpret_cast<uintptr_t>(ss), 24,
                                          VM_PROT_READ) != 24) ||
        (oss != nullptr && prot_prefix_len(reinterpret_cast<uintptr_t>(oss), 24,
                                           VM_PROT_WRITE) != 24)) {
        errno = EFAULT;
        return -1;
    }
#endif
    return kudroid::guest_sigaltstack(ss, oss);
}

#ifndef __APPLE__
#include <sys/syscall.h>
#endif

// bionic: long syscall(long number, ...)
// Guest libc/libc++ call syscall() directly with Linux numbers; map them to host
// behaviour and return ENOSYS for unknown numbers (safer than running the wrong call).
#define KUDROID_SYS_gettid 178
#define KUDROID_SYS_getpid 39
#define KUDROID_SYS_futex 98
#define KUDROID_SYS_process_vm_readv 270

// Registry tid -> pthread_t: guest tgkill uses tids from our gettid().
static std::mutex g_tidRegistryMtx;
static std::unordered_map<long, pthread_t> g_tidRegistry;

static void tid_registry_record(long tid) {
    if (tid <= 0) return;
    pthread_t self = pthread_self();
    std::lock_guard<std::mutex> lock(g_tidRegistryMtx);
    g_tidRegistry[tid] = self;
}

extern "C" pid_t bionic_gettid() {
#ifdef __APPLE__
    uint64_t tid = 0;
    pthread_threadid_np(NULL, &tid);
    const pid_t result = static_cast<pid_t>(tid);
    tid_registry_record(static_cast<long>(result));
    return result;
#else
    const pid_t result = static_cast<pid_t>(::syscall(SYS_gettid));
    tid_registry_record(static_cast<long>(result));
    return result;
#endif
}

#ifdef __APPLE__
// How many bytes from `addr` carry every protection bit in `required`.
//
// Returns a LENGTH, not a yes/no, because the kernel this stands in for answers with a
// length: a read that starts in a committed page and runs into a PROT_NONE reservation
// returns the readable prefix and no error. Verified against the real syscall on Linux —
// a 16-byte read straddling the boundary returns 6, not EFAULT. Answering yes/no would
// either approve the faulting half or reject a request the guest is entitled to have
// partly served.
//
// Protection, not mere mappedness, is the question. Getting that wrong is what turned a
// probe meant to PREVENT a segfault into the cause of one: a PROT_NONE reservation IS a
// mapped region — vm_region_64 reports it with a base, a size and protection 0 — so a
// range check answered "safe to read" for memory that faults on its first byte.
//
// That is not an edge case on iOS, it is the common case. Unity reserves address space in
// half-gigabyte PROT_NONE chunks before committing any of it; the ULTRAKILL log has
// eleven `mmap(len=536854528, prot=0x0, flags=0x22)` calls in a row, and 0x5500000000 —
// the address that faulted — has exactly that shape. libunity's own crash reporter probed
// it, this said yes, and the memcpy took the fault inside _platform_memmove with lr in
// bionic_syscall.
static size_t prot_prefix_len(uintptr_t addr, size_t len, vm_prot_t required) {
    if (len == 0) return 0;
    if (addr > static_cast<uintptr_t>(-1) - len) return 0;
    const uintptr_t end = addr + len;
    uintptr_t reached = addr;
    vm_address_t region_addr = static_cast<vm_address_t>(addr);
    while (reached < end) {
        vm_size_t region_size = 0;
        // The real info struct, not an integer buffer. The protection bits are the whole
        // point of this call now, and VM_REGION_BASIC_INFO_COUNT_64 is DEFINED as
        // sizeof(vm_region_basic_info_data_64_t)/sizeof(int) — so the struct and the
        // count cannot disagree, which is what the integer buffer was guarding against.
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object_name = MACH_PORT_NULL;
        const kern_return_t kr = vm_region_64(mach_task_self(), &region_addr, &region_size,
                                              VM_REGION_BASIC_INFO_64,
                                              reinterpret_cast<vm_region_info_t>(&info),
                                              &count, &object_name);
        if (kr != KERN_SUCCESS) break;
        const uintptr_t rstart = static_cast<uintptr_t>(region_addr);
        const uintptr_t rend = rstart + region_size;
        if (region_size == 0 || rend <= rstart) break; // overflow
        // vm_region_64 returns the region containing the address OR the next one after
        // it. A gap before the point reached means the rest is unmapped.
        if (reached < rstart) break;
        if ((info.protection & required) != required) break;
        reached = rend;
        region_addr = rend;
    }
    if (reached <= addr) return 0;
    const size_t covered = static_cast<size_t>(reached - addr);
    return covered < len ? covered : len;
}
#endif

// Guest libs share the host address space, so only pid==self is meaningful —
// used by fbjni/Hermes to probe readability before reading.
extern "C" long bionic_process_vm_readv(pid_t pid, const struct iovec* local_iov, unsigned long liovcnt,
                                         const struct iovec* remote_iov, unsigned long riovcnt,
                                         unsigned long flags) {
#ifdef __linux__
    // Host Linux has a real syscall — handle every pid safely.
    return static_cast<long>(::syscall(SYS_process_vm_readv, pid, local_iov, liovcnt,
                                       remote_iov, riovcnt, flags));
#else
    (void)riovcnt;
    if (flags != 0) { errno = EINVAL; return -1; }
    if (pid != static_cast<pid_t>(::getpid())) {
        errno = pid < 1 ? ESRCH : EPERM;
        return -1;
    }
    if (liovcnt == 0) return 0;
    if (liovcnt > 1024 || riovcnt > 1024) { errno = EINVAL; return -1; }
    long total = 0;
    for (unsigned long i = 0; i < liovcnt && i < riovcnt; ++i) {
        const uint8_t* src = static_cast<const uint8_t*>(remote_iov[i].iov_base);
        uint8_t* dst = static_cast<uint8_t*>(local_iov[i].iov_base);
        const size_t n = std::min(remote_iov[i].iov_len, local_iov[i].iov_len);
        if (n == 0) continue;
        if (!src || !dst) {
            if (total == 0) { errno = EFAULT; return -1; }
            return total;
        }
        // The guest is asking "can I read here?" precisely because it does NOT know, so
        // the answer has to be established before the memcpy rather than discovered by
        // faulting during it.
        //
        // Protection, not mappedness — the distinction this used to miss, and it cost a
        // crash: a PROT_NONE reservation is mapped, Unity makes hundreds of gigabytes of
        // them at startup, and approving one turned libunity's own crash-reporter probe
        // into a fault inside _platform_memmove.
        //
        // The DESTINATION is checked too, which it was not before and which is just as
        // fatal: a local_iov pointing into a read-only or reserved page faults on the
        // write side of the same memcpy, at an address that looks like the guest's own
        // buffer with nothing to suggest this shim chose to write there.
        //
        // A partly-accessible range copies its accessible PREFIX, matching the kernel
        // rather than a convenient simplification. Checked against the real syscall on
        // Linux: a 16-byte read spanning a committed page and the PROT_NONE mapping after
        // it returns 6 and no error. Refusing the whole request would make a guest whose
        // probe legitimately runs off the end of a region conclude the memory is
        // unreadable when it is not.
        size_t copy = prot_prefix_len(reinterpret_cast<uintptr_t>(src), n, VM_PROT_READ);
        const size_t writable = prot_prefix_len(reinterpret_cast<uintptr_t>(dst), copy,
                                                VM_PROT_WRITE);
        if (writable < copy) copy = writable;
        if (copy == 0) {
            if (total == 0) { errno = EFAULT; return -1; }
            return total;
        }
        std::memcpy(dst, src, copy);
        total += static_cast<long>(copy);
        // A short copy means the range ran out of accessible memory, so there is nothing
        // further to read in this iovec and no point starting the next.
        if (copy < n) return total;
    }
    return total;
#endif
}

// futex via syscall(): libc++ calls syscall(98) for atomic wait/notify; route into bionic_futex.
extern "C" int bionic_futex(uint32_t* uaddr, int futex_op, uint32_t val,
                             const struct timespec* timeout, uint32_t* uaddr2, uint32_t val3);

// Declared at namespace scope — `extern "C"` is invalid inside a function.
#if defined(__aarch64__)
extern "C" bool kudroid_lookup_guest_module(void* addr, char* out, std::size_t outSize);
#endif

// DIAG gate: guard/TLS diagnostics run only when investigating (KUDROID_GUARD_DIAG=1).
// Off by default — per-call frame walks cost real runtime; futex logging stays unconditional but capped.
[[maybe_unused]] static bool guard_diag_enabled() {
    static const int enabled = []() {
        const char* v = std::getenv("KUDROID_GUARD_DIAG");
        return v && v[0] == '1';
    }();
    return enabled != 0;
}

static long emulate_futex_direct(uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4, uintptr_t arg5, uintptr_t arg6) {
    uint32_t* uaddr = reinterpret_cast<uint32_t*>(arg1);
    const int futex_op = static_cast<int>(arg2);
    const uint32_t val = static_cast<uint32_t>(arg3);
    const struct timespec* timeout = reinterpret_cast<const struct timespec*>(arg4);
    uint32_t* uaddr2 = reinterpret_cast<uint32_t*>(arg5);
    const uint32_t val3 = static_cast<uint32_t>(arg6);
#if defined(__aarch64__)
    // Futex waits are logged unconditionally but capped at 16 distinct addresses.
    {
        const int cmd = futex_op & 127;
        if (cmd == 0 /* FUTEX_WAIT */ || cmd == 9 /* FUTEX_WAIT_BITSET */) {
            static std::mutex s_seenMtx;
            static void* s_seen[16];
            static int s_seenN = 0;
            bool dup = false;
            {
                // Guarded: this used to race, and two threads entering an unseen
                // futex at once could both write s_seen[s_seenN] and lose one.
                std::lock_guard<std::mutex> lock(s_seenMtx);
                for (int i = 0; i < s_seenN; ++i) {
                    if (s_seen[i] == uaddr) { dup = true; break; }
                }
                if (!dup && s_seenN < 16) s_seen[s_seenN++] = uaddr;
                else dup = true;  // table full: stop logging rather than grow
            }
            if (!dup) {
                char mod[256] = {0};
                kudroid_lookup_guest_module(uaddr, mod, sizeof(mod));
                char msg[352];
                // tid, because without it this line cannot be joined to anything. The
                // captured ULTRAKILL log had two futex_wait lines and one stall report,
                // and no way to tell whether the reported thread was one of the two:
                // the stall said tid=2844404, these said nothing at all.
                snprintf(msg, sizeof(msg),
                         "futex_wait uaddr=0x%llx [%s] val=%u op=%d tid=%llu",
                         (unsigned long long)(uintptr_t)uaddr, mod, val, futex_op,
                         current_thread_id());
                logAndroidMessage(4, "KuDroidSyscall", msg);
            }
        }
    }
#endif
    return static_cast<long>(bionic_futex(uaddr, futex_op, val, timeout, uaddr2, val3));
}

// Diagnostic for __cxa_guard_acquire recursion: logs in-progress guards with module + caller.
#if defined(__aarch64__)
extern "C" bool kudroid_lookup_guest_module(void* addr, char* out, std::size_t outSize);
__attribute__((noinline))
static void log_guard_acquire_diag(long tid, uintptr_t guard) {
    uintptr_t x29v = 0;
    asm volatile("mov %0, x29" : "=r"(x29v));
    // Walk from this frame; keep the deepest [fp+56] == guard match.
    uintptr_t gaCaller = 0;
    kudroid::FrameWalker walker(x29v, kudroid::cached_thread_stack_bounds());
    for (int i = 0; i < 6 && walker.valid(); ++i) {
        uintptr_t candGuard = 0;
        if (walker.slot(56, &candGuard) && candGuard == guard) {
            uintptr_t caller = 0;
            if (walker.return_address(&caller)) gaCaller = caller;
        }
        if (!walker.next()) break;
    }
    char guardMod[256] = {0}, callerMod[256] = {0};
    kudroid_lookup_guest_module(reinterpret_cast<void*>(guard), guardMod, sizeof(guardMod));
    kudroid_lookup_guest_module(reinterpret_cast<void*>(gaCaller), callerMod, sizeof(callerMod));
    unsigned b0 = 0, b1 = 0, storedTid = 0, inProgress = 0, sameTid = 0;
    // The guard lives in a guest module's .bss; confirm via the module registry.
    if (guardMod[0] != '\0') {
        const volatile uint8_t* g = reinterpret_cast<const volatile uint8_t*>(guard);
        b0 = g[0];
        b1 = g[1];
        storedTid = *reinterpret_cast<const volatile uint32_t*>(guard + 4);
        inProgress = (b1 & 0x2) != 0;
        sameTid = inProgress && (static_cast<long>(storedTid) == tid);
    }
    if (!inProgress) return; // new claim — not the case under investigation
    // Bounded to the first 16 reports; unconditional logging would sit on a hot path.
    static std::atomic<int> s_emitted{0};
    if (s_emitted.fetch_add(1, std::memory_order_relaxed) >= 16) return;
    char msg[640];
    snprintf(msg, sizeof(msg),
             "guard_diag %s tid=%ld guard=0x%llx [%s] b0=%u b1=%u stored_tid=%u "
             "call_site=0x%llx caller=0x%llx [%s]",
             sameTid ? "SAME_TID_RECURSION" : "waiting",
             tid, (unsigned long long)guard, guardMod, b0, b1, storedTid,
             (unsigned long long)(uintptr_t)__builtin_return_address(1),
             (unsigned long long)gaCaller, callerMod);
    logAndroidMessage(4, "KuDroidSyscall", msg);
}
#else
static void log_guard_acquire_diag(long tid, uintptr_t guard) { (void)tid; (void)guard; }
#endif

// __cxa_guard_acquire / release / abort — handle same-thread recursion.
// Same-thread re-entry clears in-progress and succeeds instead of aborting.
static std::mutex g_guardMtx;

// Cap same-tid recursions; after N pretend done so a failing init cannot hang forever.
static constexpr int kGuardMaxRecursions = 8;
static std::unordered_map<uintptr_t, int> g_guardRecursions;

static int guard_recursion_count(uintptr_t g) {
    int& n = g_guardRecursions[g];
    return ++n;
}

extern "C" int bionic___cxa_guard_acquire(uint64_t* g) {
    if (!g) return 1;
    // Fast path: done needs no lock.
    if (reinterpret_cast<const volatile uint8_t*>(g)[0] & 0x1) return 0;
    // Take tid before locking to keep lock order consistent.
    const long tid = static_cast<long>(bionic_gettid());
    // Declared outside the loop so one spin is one registry entry, however many
    // times it goes round, and so it clears on every exit path.
    std::optional<BlockingWaitScope> spinTracked;
    std::unique_lock<std::mutex> lock(g_guardMtx);
    for (;;) {
        volatile uint8_t* b0 = reinterpret_cast<volatile uint8_t*>(g);
        if (b0[0] & 0x1) return 0; // already initialised
        if (b0[1] & 0x2) {          // initialisation in progress
            const uint32_t stored = *reinterpret_cast<volatile uint32_t*>(b0 + 4);
            if (stored == static_cast<uint32_t>(tid)) {
                // Same thread re-entered → clear and retry instead of aborting.
                const int rec = guard_recursion_count(reinterpret_cast<uintptr_t>(g));
                if (rec > kGuardMaxRecursions) {
                    // Loop-cut: guard keeps failing; pretend done to escape.
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                             "guard_recursion_loop_cut guard=0x%llx tid=%ld "
                             "rec=%d -> return 0 (pretend done)",
                             (unsigned long long)(uintptr_t)g, tid, rec);
                    logAndroidMessage(4, "KuDroidSyscall", msg);
                    return 0;
                }
                b0[1] &= static_cast<uint8_t>(~0x6);
                *reinterpret_cast<volatile uint32_t*>(b0 + 4) = 0;
                lock.unlock();
                char msg[224];
                snprintf(msg, sizeof(msg),
                         "guard_recursion_tolerated guard=0x%llx tid=%ld -> re-init",
                         (unsigned long long)(uintptr_t)g, tid);
                logAndroidMessage(4, "KuDroidSyscall", msg);
                return 1;
            }
            // Another thread is initialising → mark waiting and spin.
            if (!spinTracked) {
                spinTracked.emplace(WaitKind::kGuardSpin, g, guest_return_address(6));
            }
            // The guard word carries the tid of whoever is running the initialiser.
            // Recording it turns "spinning on guard X" into "spinning on X, held by
            // tid Y", which pairs with Y's own stalled line and names the whole cycle
            // instead of half of it.
            blocking_wait_note_owner(*reinterpret_cast<volatile uint32_t*>(b0 + 4));
            blocking_wait_note_iteration();
            b0[1] |= 0x4;
            lock.unlock();
            ::sched_yield();
            lock.lock();
            continue;
        }
        // Claim: mark in-progress and record our tid.
        b0[1] |= 0x2;
        *reinterpret_cast<volatile uint32_t*>(b0 + 4) = static_cast<uint32_t>(tid);
        return 1;
    }
}

extern "C" void bionic___cxa_guard_release(uint64_t* g) {
    if (!g) return;
    std::lock_guard<std::mutex> lock(g_guardMtx);
    volatile uint8_t* b0 = reinterpret_cast<volatile uint8_t*>(g);
    b0[0] |= 0x1;                                    // done
    b0[1] &= static_cast<uint8_t>(~0x6);             // clear in-progress + waiting
    // Spinners re-check under the lock — no signal needed.
}

extern "C" void bionic___cxa_guard_abort(uint64_t* g) {
    if (!g) return;
    std::lock_guard<std::mutex> lock(g_guardMtx);
    volatile uint8_t* b0 = reinterpret_cast<volatile uint8_t*>(g);
    b0[1] &= static_cast<uint8_t>(~0x6);
    *reinterpret_cast<volatile uint32_t*>(b0 + 4) = 0;
}

extern "C" int bionic_epoll_create1(int flags);
extern "C" int bionic_epoll_ctl(int epfd, int op, int fd, void *event_ptr);
extern "C" int bionic_epoll_wait(int epfd, void *events_ptr, int maxevents, int timeout);
extern "C" int bionic_pipe2(int pipefd[2], int flags);
extern "C" int bionic_clock_gettime(int clock_id, struct timespec *tp);
extern "C" void* bionic_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
extern "C" int bionic_mprotect(void *addr, size_t len, int prot);
extern "C" int bionic_close(int fd);
extern "C" ssize_t bionic_getrandom(void *buf, size_t buflen, unsigned int flags);
// Defined further down, next to the rest of the guest signal glue.
extern "C" int bionic_rt_sigaction(int signum, const void* act, void* oldact,
                                   size_t sigsetsize);
// The outbound signal senders, reached from bionic_syscall below as well as from the
// symbol table. Declared here because the syscall switch comes first in this file.
extern "C" int bionic_tgkill(int pid, int tid, int sig);
extern "C" int bionic_tkill(int tid, int sig);
#if defined(__APPLE__)
static bool linux_to_darwin_sockaddr(const struct sockaddr* src, socklen_t srclen,
                                     struct sockaddr_storage* dst, socklen_t* dstlen) {
    if (!src || srclen < 2 || !dst || !dstlen) return false;
    const unsigned char* b = reinterpret_cast<const unsigned char*>(src);
    const unsigned fam = static_cast<unsigned>(b[0]) | (static_cast<unsigned>(b[1]) << 8);
    unsigned char* out = reinterpret_cast<unsigned char*>(dst);
    std::memset(dst, 0, sizeof(*dst));
    if (fam == 2 && srclen >= 8) {
        out[0] = 16;
        out[1] = AF_INET;
        std::memcpy(out + 2, b + 2, std::min<size_t>(srclen - 2, 14));
        *dstlen = 16;
        return true;
    } else if (fam == 10 && srclen >= 24) {
        out[0] = 28;
        out[1] = AF_INET6;
        std::memcpy(out + 2, b + 2, std::min<size_t>(srclen - 2, 26));
        *dstlen = 28;
        return true;
    } else if (fam == 1) {
        struct sockaddr_un* s = reinterpret_cast<struct sockaddr_un*>(dst);
        size_t n = srclen > 2 ? srclen - 2 : 0;
        const char* gp = reinterpret_cast<const char*>(b) + 2;
        char path[1024];
        bool abstract = (n > 0 && gp[0] == 0);
        if (abstract) {
            size_t m = (n - 1 < sizeof(path) - 1) ? n - 1 : sizeof(path) - 1;
            std::memcpy(path, gp + 1, m);
            path[m] = '\0';
        } else {
            size_t m = strnlen(gp, n);
            if (m > sizeof(path) - 1) m = sizeof(path) - 1;
            std::memcpy(path, gp, m);
            path[m] = '\0';
        }
        if (abstract || std::strlen(path) > sizeof(s->sun_path) - 1) {
            uint64_t h = 1469598103934665603ull;
            for (const char* q = path; *q; ++q) {
                h = (h ^ static_cast<uint8_t>(*q)) * 1099511628211ull;
            }
            const char* tmp = std::getenv("TMPDIR");
            if (!tmp || !*tmp || std::strlen(tmp) > sizeof(s->sun_path) - 22) tmp = "/tmp";
            char dir[300];
            std::snprintf(dir, sizeof(dir), "%s%ss", tmp, tmp[std::strlen(tmp) - 1] == '/' ? "" : "/");
            mkdir(dir, 0700);
            std::snprintf(path, sizeof(path), "%s/%c%016llx", dir, abstract ? 'a' : 'p',
                          static_cast<unsigned long long>(h));
        }
        n = std::strlen(path);
        if (n > sizeof(s->sun_path) - 1) n = sizeof(s->sun_path) - 1;
        std::memcpy(s->sun_path, path, n);
        s->sun_path[n] = '\0';
        s->sun_family = AF_UNIX;
        s->sun_len = static_cast<uint8_t>(2 + n + 1);
        *dstlen = s->sun_len;
        return true;
    }
    return false;
}

static void darwin_to_linux_sockaddr(const struct sockaddr* src, socklen_t srclen,
                                     struct sockaddr* dst, socklen_t* dstlen) {
    if (!src || !dst || !dstlen || *dstlen < 2) return;
    const unsigned char* s = reinterpret_cast<const unsigned char*>(src);
    unsigned char* d = reinterpret_cast<unsigned char*>(dst);
    const unsigned fam = s[1];
    if (fam == AF_INET) {
        socklen_t copy_len = std::min<socklen_t>(*dstlen, 16);
        std::memset(dst, 0, *dstlen);
        d[0] = 2;
        d[1] = 0;
        if (copy_len > 2) {
            std::memcpy(d + 2, s + 2, copy_len - 2);
        }
        *dstlen = 16;
    } else if (fam == AF_INET6) {
        socklen_t copy_len = std::min<socklen_t>(*dstlen, 28);
        std::memset(dst, 0, *dstlen);
        d[0] = 10;
        d[1] = 0;
        if (copy_len > 2) {
            std::memcpy(d + 2, s + 2, copy_len - 2);
        }
        *dstlen = 28;
    } else if (fam == AF_UNIX) {
        const struct sockaddr_un* un = reinterpret_cast<const struct sockaddr_un*>(src);
        size_t n = strnlen(un->sun_path, sizeof(un->sun_path));
        std::memset(dst, 0, *dstlen);
        d[0] = 1;
        d[1] = 0;
        socklen_t need = static_cast<socklen_t>(2 + n + 1);
        if (*dstlen > 2) {
            size_t copy = std::min<size_t>(n, *dstlen - 2);
            std::memcpy(d + 2, un->sun_path, copy);
        }
        *dstlen = need;
    } else {
        socklen_t copy_len = std::min<socklen_t>(*dstlen, srclen);
        std::memset(dst, 0, *dstlen);
        d[0] = static_cast<unsigned char>(fam);
        d[1] = 0;
        if (copy_len > 2) {
            std::memcpy(d + 2, s + 2, copy_len - 2);
        }
        *dstlen = srclen;
    }
}
#endif

namespace {
// Defined with the fd-path map further down; declared here because the lseek
// case sits above that definition.
std::string fd_path(int fd);
}  // namespace
