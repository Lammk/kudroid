// test_shims.cpp — host-side regression tests for the guest syscall shims in
// src/abi/SyscallShim.cpp. Every function here is `extern "C"` and compiles
// on the Linux host (the Apple-only #ifdef branches are verified by the
// build-macos CI job instead).
//
// Covered regressions:
//   1. bionic_pthread_once: nested once on a DIFFERENT control word must not
//      deadlock (old global-mutex impl held one lock across init_routine).
//   2. bionic_pthread_once: init runs exactly once across many threads.
//   3. bionic_futex: WAIT/WAKE, EAGAIN on value mismatch, ETIMEDOUT on past
//      timeout, CMP_REQUEUE precondition, and repeated wait/wake cycles that
//      exercise the queue erase path (leak fix).
//   4. bionic_mremap: shrink in place without MREMAP_MAYMOVE succeeds (old
//      Apple branch always returned ENOMEM without the flag).
//   5. bionic_sigaction: SA_* flags round-trip through oldact (previously only
//      SA_SIGINFO was translated on Apple; host passes values straight through).
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <future>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sched.h>
#include <semaphore.h>
#include <sys/uio.h>
#include <unistd.h>

// ─── Shims under test (extern "C", defined in SyscallShim.cpp) ──────────────
extern "C" int bionic_pthread_once(int* guest_once, void (*init_routine)(void));
extern "C" int bionic_futex(uint32_t* uaddr, int futex_op, uint32_t val,
                            const struct timespec* timeout, uint32_t* uaddr2,
                            uint32_t val3);
extern "C" void* bionic_mremap(void* old_address, size_t old_size,
                               size_t new_size, int flags, void* new_address);
extern "C" int bionic_sigaction(int signum, const struct android_sigaction* act,
                                struct android_sigaction* oldact);
extern "C" long bionic_syscall(long number, ...);
extern "C" int bionic_sigaltstack(const stack_t* ss, stack_t* oss);
extern "C" int bionic_sched_getaffinity(pid_t pid, size_t cpusetsize, void* mask);
extern "C" int bionic_sem_init(sem_t* sem, int pshared, unsigned int value);
extern "C" int bionic_sem_destroy(sem_t* sem);
extern "C" int bionic_sem_wait(sem_t* sem);
extern "C" int bionic_sem_trywait(sem_t* sem);
extern "C" int bionic_sem_post(sem_t* sem);
extern "C" int bionic_sem_getvalue(sem_t* sem, int* out);
extern "C" int bionic_sem_timedwait(sem_t* sem, const struct timespec* abs_timeout);
extern "C" int bionic_tgkill(int pid, int tid, int sig);
extern "C" int bionic_pipe2(int pipefd[2], int flags);
extern "C" int bionic___cxa_guard_acquire(uint64_t* g);
extern "C" void bionic___cxa_guard_release(uint64_t* g);
extern "C" void bionic___cxa_guard_abort(uint64_t* g);
extern "C" void* bionic_ALooper_prepare(int opts);
extern "C" void* bionic_ALooper_forThread(void);
extern "C" void bionic_ALooper_acquire(void* looper);
extern "C" void bionic_ALooper_release(void* looper);
extern "C" void* bionic_dlopen(const char* filename, int flags);
extern "C" void* bionic_dlsym(void* handle, const char* symbol);
extern "C" int bionic_dlclose(void* handle);
extern "C" int bionic_pthread_mutex_init(void* guestMutex, const void* attr);
extern "C" int bionic_pthread_mutex_lock(void* guestMutex);
extern "C" int bionic_pthread_mutex_unlock(void* guestMutex);
extern "C" int bionic_pthread_mutex_trylock(void* guestMutex);
extern "C" int bionic_pthread_mutex_destroy(void* guestMutex);
extern "C" int bionic_pthread_mutexattr_init(void* attr);
extern "C" int bionic_pthread_mutexattr_settype(void* attr, int type);
extern "C" int bionic_pthread_mutexattr_gettype(void* attr, int* type);
extern "C" int bionic_pthread_mutexattr_destroy(void* attr);

// Guest library hooks, installed by kudroid_run_apk in production.
extern "C" void* (*kudroid_guest_library_open)(const char* filename);
extern "C" void* (*kudroid_guest_library_symbol)(void* handle, const char* symbol);
extern "C" int (*kudroid_guest_library_owns)(void* handle);

// Mirror of the guest android_sigaction layout from SyscallShim.cpp.
struct android_sigaction {
    union {
        void (*android_sa_handler)(int);
        void (*android_sa_sigaction)(int, void*, void*);
    };
    uint64_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

// ─── Host-link stubs ─────────────────────────────────────────────────────────
// The JNI_CreateJavaVM/classpathJar stub is no longer needed: KuART defines them itself
// in KuArtRuntime.cpp (Avian has been removed).

// Linux futex command constants (match SyscallShim.cpp).
#define FUTEX_WAIT           0
#define FUTEX_WAKE           1
#define FUTEX_CMP_REQUEUE    4
#define FUTEX_WAIT_BITSET    9
#define FUTEX_PRIVATE_FLAG   128
#define FUTEX_CLOCK_REALTIME 256
#define MREMAP_MAYMOVE 1

// Guest-visible sa_flags are ALWAYS Linux values (asm-generic/signal.h) — the
// shim translates them to/from Darwin on Apple. Use the Linux constants here
// instead of the host's SA_* macros so the test is correct on both hosts.
#define LINUX_SA_RESTART   0x10000000
#define LINUX_SA_NODEFER   0x40000000
#define LINUX_SA_RESETHAND 0x80000000

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        ++g_checks;                                                            \
        if (cond) {                                                            \
            std::printf("  OK: %s\n", msg);                                    \
        } else {                                                               \
            std::printf("  FAIL: %s (line %d)\n", msg, __LINE__);              \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

// ─── 1. bionic_pthread_once ─────────────────────────────────────────────────

static std::atomic<int> g_outer_runs{0};
static std::atomic<int> g_inner_runs{0};
static int g_once_inner = 0; // guest control word B
static int g_once_outer = 0; // guest control word A

static void inner_routine(void) {
    ++g_inner_runs;
}

static void outer_routine(void) {
    ++g_outer_runs;
    // Nested once on a DIFFERENT control word. The old implementation held a
    // single global mutex across init_routine(), so this re-locked itself and
    // deadlocked. Must complete now.
    int r = bionic_pthread_once(&g_once_inner, inner_routine);
    if (r != 0) g_outer_runs.store(-1000); // poison marker
}

static void test_pthread_once_nested() {
    std::printf("[pthread_once] nested once on different control word\n");
    // Run in a separate thread and bound it with a watchdog so a regression
    // fails the test instead of hanging the process forever.
    auto fut = std::async(std::launch::async, [] {
        return bionic_pthread_once(&g_once_outer, outer_routine);
    });
    auto status = fut.wait_for(std::chrono::seconds(5));
    if (status != std::future_status::ready) {
        std::printf("  FAIL: nested once DEADLOCKED (timeout after 5s)\n");
        ++g_failures;
        ++g_checks;
        return; // process exit will reap the stuck thread
    }
    CHECK(fut.get() == 0, "outer once returns 0");
    CHECK(g_outer_runs.load() == 1, "outer init ran exactly once");
    CHECK(g_inner_runs.load() == 1, "inner init ran exactly once");
    CHECK(bionic_pthread_once(&g_once_outer, outer_routine) == 0,
          "fast path after completion returns 0");
}

static void test_pthread_once_concurrent() {
    std::printf("[pthread_once] init runs exactly once across 8 threads\n");
    static int once_cw = 0;
    static std::atomic<int> runs{0};
    auto worker = [] {
        for (int i = 0; i < 100; ++i) {
            bionic_pthread_once(&once_cw, [] { ++runs; });
        }
    };
    std::thread threads[8];
    for (auto& t : threads) t = std::thread(worker);
    for (auto& t : threads) t.join();
    CHECK(runs.load() == 1, "init_routine invoked exactly once total");
    CHECK(bionic_pthread_once(&once_cw, [] { ++runs; }) == 0,
          "later callers take fast path");
}

// ─── 2. bionic_futex ─────────────────────────────────────────────────────────

static std::atomic<bool> g_waiter_ready{false};
static uint32_t g_futex_word = 0;
static std::atomic<int> g_wake_result{0};

static void futex_waiter() {
    // Signal that we are about to park, then wait on word == 0.
    g_waiter_ready.store(true, std::memory_order_release);
    int r = bionic_futex(&g_futex_word, FUTEX_WAIT, 0, nullptr, nullptr, 0);
    g_wake_result.store(r);
}

static void test_futex_wait_wake() {
    std::printf("[futex] WAIT/WAKE\n");
    g_futex_word = 0;
    g_waiter_ready.store(false);
    g_wake_result.store(-123);
    std::thread t(futex_waiter);
    // Wait for the waiter to park.
    while (!g_waiter_ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // WAKE with the wrong value present would still wake the queue; use the
    // normal sequence: mutate word, then wake.
    g_futex_word = 1;
    int woken = bionic_futex(&g_futex_word, FUTEX_WAKE, 1, nullptr, nullptr, 0);
    t.join();
    CHECK(woken == 1, "WAKE returns 1");
    CHECK(g_wake_result.load() == 0, "WAIT returns 0 after being woken");
}

static void test_futex_eagain() {
    std::printf("[futex] WAIT with mismatched value -> EAGAIN\n");
    uint32_t word = 42;
    errno = 0;
    int r = bionic_futex(&word, FUTEX_WAIT, 41, nullptr, nullptr, 0);
    CHECK(r == -1 && errno == EAGAIN, "returns -1/EAGAIN without parking");
}

static void test_futex_etimedout() {
    std::printf("[futex] WAIT with a tiny relative timeout -> ETIMEDOUT\n");
    uint32_t word = 0;
    // FUTEX_WAIT's timeout is RELATIVE, so this asks for 20ms and must time out
    // after roughly that long.
    struct timespec relative = {0, 20 * 1000 * 1000};
    errno = 0;
    const auto t0 = std::chrono::steady_clock::now();
    int r = bionic_futex(&word, FUTEX_WAIT, 0, &relative, nullptr, 0);
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();
    CHECK(r == -1 && errno == ETIMEDOUT, "returns -1/ETIMEDOUT");
    CHECK(waited >= 15, "after actually waiting for the requested interval");
}

// ─── futex timeout semantics: relative vs absolute ───────────────────────────
//
// FUTEX_WAIT takes a RELATIVE timeout; FUTEX_WAIT_BITSET takes an ABSOLUTE
// deadline. The shim treated both as absolute, subtracting CLOCK_MONOTONIC from
// whatever the guest passed.
//
// On a device that has been up a while that is catastrophic rather than merely
// wrong. Unity's asset threads call FUTEX_WAIT (op=128) with a few tens of
// milliseconds, so tv_sec is 0; on a phone six days into its uptime the
// subtraction produced 0 - 528730 and every wait returned ETIMEDOUT without
// waiting, so the guest spun in place. ULTRAKILL's AssetGarbageCollectorHelper
// never got past the prctl that named it, and the log ended there.
//
// The host clock is nowhere near zero either, which is what makes these tests able
// to tell the two interpretations apart at all: a relative 30ms read as an absolute
// deadline is always in the past.

static void test_futex_wait_timeout_is_relative() {
    std::printf("[futex] FUTEX_WAIT treats its timeout as relative, not absolute\n");

    uint32_t word = 0;
    // A wake arrives well within the timeout. Read as an absolute deadline this
    // would expire instantly and report ETIMEDOUT before the waker ever ran.
    struct timespec relative = {5, 0};  // 5 seconds, relative

    std::atomic<int> result{-999};
    std::atomic<int> saved_errno{0};
    std::thread waiter([&] {
        errno = 0;
        result = bionic_futex(&word, FUTEX_WAIT, 0, &relative, nullptr, 0);
        saved_errno = errno;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    CHECK(result.load() == -999,
          "the waiter is still parked 80ms in — an absolute reading would already have "
          "returned ETIMEDOUT, which is what made Unity spin");

    word = 1;
    bionic_futex(&word, FUTEX_WAKE, 1, nullptr, nullptr, 0);
    waiter.join();
    CHECK(result.load() == 0, "the wake was received and the wait succeeded");
    CHECK(saved_errno.load() != ETIMEDOUT, "and it was not reported as a timeout");
}

static void test_futex_wait_relative_zero_does_not_hang() {
    std::printf("[futex] a zero relative timeout expires at once\n");
    uint32_t word = 0;
    struct timespec zero = {0, 0};
    errno = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const int r = bionic_futex(&word, FUTEX_WAIT, 0, &zero, nullptr, 0);
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();
    CHECK(r == -1 && errno == ETIMEDOUT, "a zero timeout is ETIMEDOUT");
    CHECK(waited < 500, "and returns promptly rather than sleeping");
}

static void test_futex_wait_rejects_malformed_relative_timeout() {
    std::printf("[futex] a malformed relative timeout is EINVAL\n");
    uint32_t word = 0;
    struct timespec negative = {-1, 0};
    errno = 0;
    CHECK(bionic_futex(&word, FUTEX_WAIT, 0, &negative, nullptr, 0) == -1 &&
              errno == EINVAL,
          "a negative timeout is rejected, not clamped into a long sleep");

    struct timespec overflow_ns = {0, 1000000000};
    errno = 0;
    CHECK(bionic_futex(&word, FUTEX_WAIT, 0, &overflow_ns, nullptr, 0) == -1 &&
              errno == EINVAL,
          "tv_nsec >= 1e9 is rejected");
}

static void test_futex_wait_bitset_timeout_is_absolute() {
    std::printf("[futex] FUTEX_WAIT_BITSET treats its timeout as an absolute deadline\n");

    uint32_t word = 0;

    // A deadline in the past must expire immediately. Read as relative, {0,0} would
    // also expire at once — so use a deadline that is clearly past but non-zero, and
    // pair it with the future-deadline check below, which the two readings cannot
    // both satisfy.
    struct timespec past;
    ::clock_gettime(CLOCK_MONOTONIC, &past);
    past.tv_sec -= 10;
    errno = 0;
    CHECK(bionic_futex(&word, FUTEX_WAIT_BITSET, 0, &past, nullptr, ~0u) == -1 &&
              errno == ETIMEDOUT,
          "a deadline ten seconds in the past is ETIMEDOUT");

    // A deadline 60ms in the future must wait about that long. Read as relative this
    // would be the host's whole uptime — an effectively infinite sleep.
    struct timespec soon;
    ::clock_gettime(CLOCK_MONOTONIC, &soon);
    soon.tv_nsec += 60 * 1000 * 1000;
    if (soon.tv_nsec >= 1000000000) { soon.tv_sec += 1; soon.tv_nsec -= 1000000000; }
    errno = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const int r = bionic_futex(&word, FUTEX_WAIT_BITSET, 0, &soon, nullptr, ~0u);
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();
    CHECK(r == -1 && errno == ETIMEDOUT, "a near-future deadline times out");
    CHECK(waited >= 40 && waited < 3000,
          "after waiting for the deadline rather than for the host's uptime");
}

static void test_futex_wait_bitset_absolute_wake_still_works() {
    std::printf("[futex] an absolute deadline still yields to a wake\n");

    uint32_t word = 0;
    struct timespec deadline;
    ::clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += 30;  // far enough out that only a wake can end this

    std::atomic<int> result{-999};
    std::thread waiter([&] {
        result = bionic_futex(&word, FUTEX_WAIT_BITSET, 0, &deadline, nullptr, ~0u);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK(result.load() == -999, "still waiting on a 30-second deadline");

    word = 1;
    bionic_futex(&word, FUTEX_WAKE, 1, nullptr, nullptr, 0);
    waiter.join();
    CHECK(result.load() == 0, "the wake ended the wait early");
}

static void test_futex_cmp_requeue_precond() {
    std::printf("[futex] CMP_REQUEUE with mismatched val3 -> EAGAIN\n");
    uint32_t word = 7;
    errno = 0;
    int r = bionic_futex(&word, FUTEX_CMP_REQUEUE, 1, nullptr, nullptr, 8);
    CHECK(r == -1 && errno == EAGAIN, "returns -1/EAGAIN when *uaddr != val3");
}

static void test_futex_wait_wake_cycles() {
    std::printf("[futex] 500 WAIT/WAKE cycles (queue erase path)\n");
    for (int i = 0; i < 500; ++i) {
        uint32_t word = 0;
        std::atomic<bool> ready{false};
        std::thread t([&] {
            ready.store(true, std::memory_order_release);
            bionic_futex(&word, FUTEX_WAIT, 0, nullptr, nullptr, 0);
        });
        while (!ready.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        word = 1;
        bionic_futex(&word, FUTEX_WAKE, 1, nullptr, nullptr, 0);
        t.join();
    }
    CHECK(true, "no crash/leak in repeated wait/wake");
}

// ─── 3. bionic_mremap ────────────────────────────────────────────────────────

static void test_mremap_shrink_in_place() {
    std::printf("[mremap] shrink without MREMAP_MAYMOVE\n");
    size_t page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    void* p = ::mmap(nullptr, page * 3, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(p != MAP_FAILED, "mmap 3 pages");
    if (p == MAP_FAILED) return;
    std::memset(p, 0xAB, page * 3);
    errno = 0;
    void* shrunk = bionic_mremap(p, page * 3, page, 0, nullptr);
    if (shrunk == MAP_FAILED) {
        std::printf("  INFO: shrink failed errno=%d (unexpected on Linux)\n",
                    errno);
        ++g_failures;
        ++g_checks;
    } else {
        CHECK(shrunk == p, "shrink keeps the same address");
        CHECK(static_cast<unsigned char*>(shrunk)[page - 1] == 0xAB,
              "content preserved in remaining region");
    }
    ::munmap(p, page * 3);
}

static void test_mremap_grow_with_maymove() {
    std::printf("[mremap] grow with MREMAP_MAYMOVE preserves content\n");
    size_t page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    void* p = ::mmap(nullptr, page, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return;
    std::memset(p, 0xCD, page);
    errno = 0;
    void* grown = bionic_mremap(p, page, page * 4, MREMAP_MAYMOVE, nullptr);
    if (grown == MAP_FAILED) {
        std::printf("  INFO: grow failed errno=%d\n", errno);
        ++g_failures;
        ++g_checks;
    } else {
        CHECK(static_cast<unsigned char*>(grown)[page - 1] == 0xCD,
              "first page content preserved after grow");
        ::munmap(grown, page * 4);
    }
}

// ─── 4. bionic_sigaction ─────────────────────────────────────────────────────

static void test_sigaction_flag_roundtrip() {
    std::printf("[sigaction] SA_* flags round-trip through oldact\n");
    struct android_sigaction act;
    std::memset(&act, 0, sizeof(act));
    act.android_sa_handler = SIG_IGN;
    // Guest-visible flags are Linux values regardless of host: on Linux they
    // pass straight through, on Apple the shim translates them.
    act.sa_flags = LINUX_SA_RESTART | LINUX_SA_NODEFER | LINUX_SA_RESETHAND;
    if (::bionic_sigaction(SIGUSR1, &act, nullptr) != 0) {
        std::printf("  FAIL: bionic_sigaction set failed errno=%d\n", errno);
        ++g_failures;
        ++g_checks;
        return;
    }
    struct android_sigaction old;
    std::memset(&old, 0, sizeof(old));
    CHECK(::bionic_sigaction(SIGUSR1, nullptr, &old) == 0,
          "query succeeds");
    CHECK((old.sa_flags & LINUX_SA_RESTART) != 0, "SA_RESTART preserved");
    CHECK((old.sa_flags & LINUX_SA_NODEFER) != 0, "SA_NODEFER preserved");
    CHECK((old.sa_flags & LINUX_SA_RESETHAND) != 0, "SA_RESETHAND preserved");

    // Restore default so we don't leave the process in a weird state.
    struct android_sigaction dfl;
    std::memset(&dfl, 0, sizeof(dfl));
    dfl.android_sa_handler = SIG_DFL;
    dfl.sa_flags = 0;
    ::bionic_sigaction(SIGUSR1, &dfl, nullptr);
}

// ─── syscall() mappings: bionic_syscall must understand fixed ARM64 NUMBERS, not
// must be the arch host number (x86_64 Linux: gettid=186/futex=202/pvm=310). ───────
// Guest always uses Linux ARM64 syscall number.
#define GUEST_SYS_gettid 178
#define GUEST_SYS_futex 98
#define GUEST_SYS_process_vm_readv 270
#define GUEST_SYS_sigaltstack 132
#define GUEST_SYS_sched_getaffinity 123
#define FUTEX_WAIT_PRIVATE 128
#define FUTEX_WAKE_PRIVATE 129

static void test_syscall_mappings() {
    std::printf("[syscall] raw Linux-arm64 numbers routed correctly\n");

    // gettid(178): unique per thread (guard static of libc++abi depends).
    const long mainTid = bionic_syscall(GUEST_SYS_gettid);
    CHECK(mainTid > 0, "gettid(178) > 0");
    long otherTid = 0;
    std::thread t([&] { otherTid = bionic_syscall(GUEST_SYS_gettid); });
    t.join();
    CHECK(otherTid != mainTid, "gettid(178) unique per thread");

    // sigaltstack(132): this is also the first direct Linux ARM64 SVC observed
    // from a guest thread on iOS. Querying the current stack exercises the
    // mapping without changing the host signal stack.
    stack_t current_stack{};
    errno = 0;
    CHECK(bionic_syscall(GUEST_SYS_sigaltstack, nullptr, &current_stack) == 0,
          "sigaltstack(132) query is routed by the Linux ARM64 number");
    errno = 0;
    CHECK(bionic_sigaltstack(reinterpret_cast<const stack_t*>(0x4e), nullptr) == -1 &&
              errno == EFAULT,
          "sigaltstack rejects an invalid guest pointer with EFAULT");

    // tgkill: registry tid -> pthread_t (write when gettid) must be found.
    CHECK(bionic_tgkill(::getpid(), static_cast<int>(mainTid), 0) == 0,
          "tgkill(pid, self tid, 0) == 0");

    // process_vm_readv(270) self-read: fbjni probe reads 8 bytes then compares magic.
    uint64_t magic = 0x1122334455667788ULL;
    uint64_t local = 0;
    struct iovec local_iov = { &local, sizeof(local) };
    struct iovec remote_iov = { &magic, sizeof(magic) };
    const long nread = bionic_syscall(GUEST_SYS_process_vm_readv,
                                      static_cast<int>(::getpid()),
                                      &local_iov, 1UL, &remote_iov, 1UL, 0UL);
    CHECK(nread == 8 && local == magic, "process_vm_readv(270) self: 8 bytes");
    errno = 0;
    CHECK(bionic_syscall(GUEST_SYS_process_vm_readv,
                         static_cast<int>(::getpid()) + 12345,
                         &local_iov, 1UL, &remote_iov, 1UL, 0UL) == -1,
          "process_vm_readv(270) foreign pid -> error");

    // futex via raw syscall(98) (libc++ __libcpp_atomic_wait uses this route).
    static uint32_t futex_word = 0;
    std::atomic<bool> waiter_ready{false};
    std::atomic<bool> waiter_done{false};
    std::thread waiter([&] {
        waiter_ready = true;
        bionic_syscall(GUEST_SYS_futex, &futex_word, FUTEX_WAIT_PRIVATE, 0U,
                       (void*)nullptr, (void*)nullptr, 0U);
        waiter_done = true;
    });
    while (!waiter_ready) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    futex_word = 1;
    const long woken = bionic_syscall(GUEST_SYS_futex, &futex_word,
                                      FUTEX_WAKE_PRIVATE, 1U,
                                      (void*)nullptr, (void*)nullptr, 0U);
    waiter.join();
    CHECK(woken == 1, "futex(98) wake returns 1");
    CHECK(waiter_done.load(), "futex(98) wait returned after wake");

    // pipe2 (macOS not available; emulation using pipe + fcntl).
    int fds[2] = {-1, -1};
    CHECK(bionic_pipe2(fds, 0x80000 /*O_CLOEXEC*/) == 0, "pipe2 O_CLOEXEC ok");
    if (fds[0] >= 0) {
        CHECK((::fcntl(fds[0], F_GETFD) & FD_CLOEXEC) != 0, "pipe2 set CLOEXEC");
        ::close(fds[0]); ::close(fds[1]);
    }
}

// ─── sched_getaffinity: raw syscall vs wrapper ───────────────────────────────
//
// The two have OPPOSITE success conventions, and conflating them silently produces
// an empty CPU set rather than an error.
//
// The raw syscall returns the number of bytes written. bionic's wrapper uses that
// count to zero the remainder of the caller's set:
//
//     int rc = __sched_getaffinity(pid, size, set);
//     if (rc == -1) return -1;
//     if ((size_t)rc < size) memset((char*)set + rc, 0, size - rc);
//     return 0;
//
// so a raw syscall that returns 0 makes bionic clear the WHOLE mask. Unity then
// reported "Cores = 0" and "0 big (mask: 0x0), 0 little (mask: 0x0)" and sized its
// job system from an empty set.

static void test_sched_getaffinity_raw_returns_byte_count() {
    std::printf("[sched_getaffinity] raw syscall returns bytes written\n");

    unsigned long mask = 0;
    const long rc = bionic_syscall(GUEST_SYS_sched_getaffinity, 0, sizeof(mask), &mask);
    CHECK(rc == static_cast<long>(sizeof(mask)),
          "raw syscall(123) returns sizeof(mask), not 0");
    CHECK(mask != 0, "the mask is not empty");
    CHECK(__builtin_popcountl(mask) == 8, "eight CPUs are reported online");

    // A guest asking for fewer bytes than a word must get exactly that many, or
    // bionic's memset of the "remainder" would run off the end of its buffer.
    unsigned char small[2] = {0xAA, 0xAA};
    const long small_rc = bionic_syscall(GUEST_SYS_sched_getaffinity, 0, sizeof(small), &small);
    CHECK(small_rc == static_cast<long>(sizeof(small)),
          "a 2-byte set reports 2 bytes written");
    CHECK(small[0] == 0xFF, "the low byte carries CPUs 0-7");

    // Reproduce what bionic does with the return value: whatever it clears, the
    // result must still describe a non-empty set.
    unsigned long emulated[4] = {0, 0, 0, 0};
    const long n = bionic_syscall(GUEST_SYS_sched_getaffinity, 0, sizeof(emulated), &emulated);
    CHECK(n > 0, "a larger set still succeeds");
    if (n > 0 && static_cast<size_t>(n) < sizeof(emulated)) {
        std::memset(reinterpret_cast<char*>(emulated) + n, 0, sizeof(emulated) - n);
    }
    CHECK(emulated[0] != 0,
          "after bionic zeroes the remainder the set is still non-empty — the bug that "
          "made Unity see zero cores");

    errno = 0;
    CHECK(bionic_syscall(GUEST_SYS_sched_getaffinity, 0, sizeof(mask), nullptr) == -1,
          "a null mask pointer is an error, not a silent success");
}

// cpu_set_t, CPU_COUNT and CPU_ISSET are glibc extensions and do not exist on
// Apple, so the set is inspected as raw bytes instead. That is also closer to
// what the guest sees: bionic's cpu_set_t is a flat little-endian bitmap, so CPU
// n lives in bit (n % 8) of byte (n / 8) regardless of the host's own struct.
namespace {

constexpr size_t kGuestCpuSetBytes = 128;  // bionic's CPU_SETSIZE / 8

bool guest_cpu_isset(const unsigned char* set, unsigned cpu) {
    return (set[cpu / 8] >> (cpu % 8)) & 1u;
}

unsigned guest_cpu_count(const unsigned char* set, size_t bytes) {
    unsigned n = 0;
    for (size_t i = 0; i < bytes; ++i) {
        n += static_cast<unsigned>(__builtin_popcount(set[i]));
    }
    return n;
}

}  // namespace

static void test_sched_getaffinity_wrapper_returns_zero() {
    std::printf("[sched_getaffinity] wrapper returns 0 on success\n");

    unsigned char set[kGuestCpuSetBytes];
    std::memset(set, 0, sizeof(set));
    CHECK(bionic_sched_getaffinity(0, sizeof(set), set) == 0,
          "the wrapper reports success as 0");
    CHECK(guest_cpu_count(set, sizeof(set)) == 8, "the wrapper fills in eight CPUs");
    CHECK(guest_cpu_isset(set, 0) && guest_cpu_isset(set, 7), "CPUs 0 and 7 are both set");
    CHECK(!guest_cpu_isset(set, 8), "CPU 8 is not claimed");

    errno = 0;
    CHECK(bionic_sched_getaffinity(0, 0, set) == -1 && errno == EINVAL,
          "a zero-sized set is rejected with EINVAL");
    errno = 0;
    CHECK(bionic_sched_getaffinity(0, sizeof(set), nullptr) == -1 && errno == EINVAL,
          "a null set is rejected with EINVAL");
}

// ─── POSIX unnamed semaphores ────────────────────────────────────────────────
//
// Darwin declares sem_init/sem_wait/sem_post/sem_destroy but does not implement
// them: each returns -1/ENOSYS. Only sem_open works. Unshimmed, a guest call fell
// through to libSystem and failed, so a "wait" did not wait and a "post" woke
// nobody.
//
// libunity.so imports exactly those four. Unity spawns a helper thread, names it,
// then blocks on a semaphore for the spawner's handshake. With sem_wait failing
// immediately the handshake could never complete, and the log stopped dead on
// prctl(PR_SET_NAME, "AssetGarbageCollectorHelper") with no mention of a semaphore.
//
// These run on the Linux host, where the host's own sem_* DO work — which is the
// point: the shim must be exercised, not the host, so every call here goes through
// bionic_sem_*.

static void test_sem_counting_semantics() {
    std::printf("[semaphore] init/post/wait counting\n");

    sem_t sem;
    std::memset(&sem, 0, sizeof(sem));
    CHECK(bionic_sem_init(&sem, 0, 0) == 0, "sem_init to 0 succeeds");

    int value = -1;
    CHECK(bionic_sem_getvalue(&sem, &value) == 0 && value == 0, "starts at 0");

    // A wait on a zero semaphore must block, so it cannot be called here. trywait
    // is the observable equivalent.
    errno = 0;
    CHECK(bionic_sem_trywait(&sem) == -1 && errno == EAGAIN,
          "trywait on an empty semaphore reports EAGAIN rather than succeeding");

    CHECK(bionic_sem_post(&sem) == 0, "post succeeds");
    CHECK(bionic_sem_getvalue(&sem, &value) == 0 && value == 1, "post raised the count");
    CHECK(bionic_sem_wait(&sem) == 0, "wait takes the posted token without blocking");
    CHECK(bionic_sem_getvalue(&sem, &value) == 0 && value == 0, "and consumed it");

    // Counting, not binary: three posts must permit three waits.
    for (int i = 0; i < 3; ++i) bionic_sem_post(&sem);
    CHECK(bionic_sem_getvalue(&sem, &value) == 0 && value == 3, "three posts count to 3");
    CHECK(bionic_sem_wait(&sem) == 0 && bionic_sem_wait(&sem) == 0 &&
              bionic_sem_wait(&sem) == 0,
          "three waits all succeed");
    errno = 0;
    CHECK(bionic_sem_trywait(&sem) == -1 && errno == EAGAIN, "the fourth finds it empty");

    CHECK(bionic_sem_init(&sem, 0, 5) == 0, "re-init at the same address");
    CHECK(bionic_sem_getvalue(&sem, &value) == 0 && value == 5,
          "replaces the old state rather than adding to it");

    bionic_sem_destroy(&sem);
}

// The handshake Unity actually performs, and the reason the log stopped.
static void test_sem_blocks_until_posted() {
    std::printf("[semaphore] wait blocks until another thread posts\n");

    sem_t sem;
    std::memset(&sem, 0, sizeof(sem));
    bionic_sem_init(&sem, 0, 0);

    std::atomic<bool> waiter_returned{false};
    std::atomic<bool> waiter_started{false};

    std::thread waiter([&] {
        waiter_started = true;
        if (bionic_sem_wait(&sem) == 0) waiter_returned = true;
    });

    while (!waiter_started.load()) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(!waiter_returned.load(),
          "the waiter is still parked — a sem_wait that returns immediately is the bug "
          "that let Unity's helper thread race past its handshake");

    bionic_sem_post(&sem);
    waiter.join();
    CHECK(waiter_returned.load(), "posting released it");

    bionic_sem_destroy(&sem);
}

// Two threads, both waiting: one post must release exactly one of them.
static void test_sem_post_releases_one_waiter() {
    std::printf("[semaphore] one post releases exactly one waiter\n");

    sem_t sem;
    std::memset(&sem, 0, sizeof(sem));
    bionic_sem_init(&sem, 0, 0);

    std::atomic<int> released{0};
    std::atomic<int> started{0};
    std::thread a([&] { ++started; if (bionic_sem_wait(&sem) == 0) ++released; });
    std::thread b([&] { ++started; if (bionic_sem_wait(&sem) == 0) ++released; });

    while (started.load() < 2) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    bionic_sem_post(&sem);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(released.load() == 1, "exactly one waiter woke");

    bionic_sem_post(&sem);
    a.join();
    b.join();
    CHECK(released.load() == 2, "the second post released the other");

    bionic_sem_destroy(&sem);
}

static void test_sem_timedwait_deadline() {
    std::printf("[semaphore] timedwait honours its absolute deadline\n");

    sem_t sem;
    std::memset(&sem, 0, sizeof(sem));
    bionic_sem_init(&sem, 0, 0);

    // An expired deadline on an empty semaphore is ETIMEDOUT, not a spin. The old
    // implementation called the host ::sem_trywait on the guest's sem_t, which on
    // Darwin fails ENOSYS on the first iteration and then busy-loops to the deadline.
    struct timespec past;
    ::clock_gettime(CLOCK_REALTIME, &past);
    past.tv_sec -= 1;
    errno = 0;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(bionic_sem_timedwait(&sem, &past) == -1 && errno == ETIMEDOUT,
          "an already-expired deadline reports ETIMEDOUT");
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0).count();
    CHECK(elapsed < 100, "and returns at once rather than spinning to it");

    // An available token must be taken even when the deadline has passed.
    bionic_sem_post(&sem);
    CHECK(bionic_sem_timedwait(&sem, &past) == 0,
          "an expired deadline still takes a token that is already available");

    // A live deadline blocks, then times out.
    struct timespec soon;
    ::clock_gettime(CLOCK_REALTIME, &soon);
    soon.tv_nsec += 120 * 1000 * 1000;
    if (soon.tv_nsec >= 1000000000) { soon.tv_sec += 1; soon.tv_nsec -= 1000000000; }
    errno = 0;
    const auto t1 = std::chrono::steady_clock::now();
    CHECK(bionic_sem_timedwait(&sem, &soon) == -1 && errno == ETIMEDOUT,
          "a future deadline on an empty semaphore times out");
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t1).count();
    CHECK(waited >= 100, "after actually waiting for it");

    bionic_sem_destroy(&sem);
}

// Destroying a semaphore with a thread parked on it must not leave that thread
// blocked forever: during shutdown that turns an exit into a hang.
static void test_sem_destroy_releases_waiters() {
    std::printf("[semaphore] destroy releases a parked waiter\n");

    sem_t sem;
    std::memset(&sem, 0, sizeof(sem));
    bionic_sem_init(&sem, 0, 0);

    std::atomic<bool> returned{false};
    std::atomic<bool> started{false};
    std::thread waiter([&] {
        started = true;
        bionic_sem_wait(&sem);
        returned = true;
    });

    while (!started.load()) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(!returned.load(), "the waiter is parked");

    bionic_sem_destroy(&sem);
    waiter.join();
    CHECK(returned.load(), "destroy woke it instead of leaving it blocked at shutdown");
}

static void test_sem_rejects_null() {
    std::printf("[semaphore] null arguments are rejected\n");
    errno = 0;
    CHECK(bionic_sem_init(nullptr, 0, 0) == -1 && errno == EINVAL, "sem_init(null)");
    errno = 0;
    CHECK(bionic_sem_wait(nullptr) == -1 && errno == EINVAL, "sem_wait(null)");
    errno = 0;
    CHECK(bionic_sem_post(nullptr) == -1 && errno == EINVAL, "sem_post(null)");
    errno = 0;
    CHECK(bionic_sem_destroy(nullptr) == -1 && errno == EINVAL, "sem_destroy(null)");
    sem_t sem;
    std::memset(&sem, 0, sizeof(sem));
    bionic_sem_init(&sem, 0, 0);
    errno = 0;
    CHECK(bionic_sem_getvalue(&sem, nullptr) == -1 && errno == EINVAL,
          "sem_getvalue with a null out pointer");
    bionic_sem_destroy(&sem);
}

// ─── __cxa_guard shim (recursion-tolerant) ──────────────────────────────────

static void test_guard_acquire_release() {
    uint64_t guard = 0;
    CHECK(bionic___cxa_guard_acquire(&guard) == 1, "guard: first acquire claims (returns 1)");
    const uint8_t* g = reinterpret_cast<const uint8_t*>(&guard);
    CHECK((g[0] & 0x1) == 0, "guard: done bit clear while in-progress");
    CHECK((g[1] & 0x2) != 0, "guard: in-progress bit set after claim");
    CHECK(*reinterpret_cast<const uint32_t*>(g + 4) != 0, "guard: tid stored");
    bionic___cxa_guard_release(&guard);
    CHECK((g[0] & 0x1) != 0, "guard: done bit set after release");
    CHECK((g[1] & 0x2) == 0, "guard: in-progress cleared after release");
    CHECK(bionic___cxa_guard_acquire(&guard) == 0, "guard: acquire after done returns 0");
}

static void test_guard_same_tid_recursion_tolerated() {
    // Replay Discord crash: same thread re-enter guard is in-progress.
    // Shim must NOT abort — clear + return 1 (re-init).
    uint64_t guard = 0;
    CHECK(bionic___cxa_guard_acquire(&guard) == 1, "guard-rec: outer claim");
    const uint8_t* g = reinterpret_cast<const uint8_t*>(&guard);
    CHECK((g[1] & 0x2) != 0, "guard-rec: in-progress before recursion");

    // Critical re-enter — previously abort("recursive initialization").
    const int inner = bionic___cxa_guard_acquire(&guard);
    CHECK(inner == 1, "guard-rec: same-tid re-enter returns 1 (no abort)");
    CHECK((g[1] & 0x2) == 0, "guard-rec: in-progress cleared before re-claim");
    CHECK(*reinterpret_cast<const uint32_t*>(g + 4) == 0, "guard-rec: tid cleared");

    // Inner claim again (re-init) then release → done.
    CHECK(bionic___cxa_guard_acquire(&guard) == 1, "guard-rec: inner re-claim");
    bionic___cxa_guard_release(&guard);
    CHECK((g[0] & 0x1) != 0, "guard-rec: done after inner release");

    // If inner init is FAIL (guard_abort), then the state is clean and someone can retry.
    uint64_t g2 = 0;
    CHECK(bionic___cxa_guard_acquire(&g2) == 1, "guard-rec: claim g2");
    CHECK(bionic___cxa_guard_acquire(&g2) == 1, "guard-rec: recursion on g2 tolerated");
    bionic___cxa_guard_abort(&g2);
    CHECK((g2 & 0xff) == 0, "guard-rec: abort clears in-progress+tid, done stays 0");
    CHECK(bionic___cxa_guard_acquire(&g2) == 1, "guard-rec: retry after abort claims again");
    bionic___cxa_guard_release(&g2);
}

static void test_guard_recursion_loop_cut() {
    // Init recursively forever (missing class on classpath) → after 8 right shims
    // returns 0 (pretend done) instead of infinite re-init (hang). Guard uses static to
    // Stable address (no stack-slot reuse from previous test).
    static uint64_t s_guard = 0;
    uint64_t& guard = s_guard;
    int first = -1, ninth = -1;
    for (int i = 1; i <= 12; ++i) {
        const int r = bionic___cxa_guard_acquire(&guard); // claim (in-progress)
        if (first < 0) first = r;
        const int r2 = bionic___cxa_guard_acquire(&guard); // same-tid recursion
        if (i == 9) ninth = r2;
        bionic___cxa_guard_abort(&guard); // init fail → clean guard for next round
        if (r2 == 0) break; // loop cut
    }
    CHECK(first == 1, "guard-cut: first claim");
    CHECK(ninth == 0, "guard-cut: recursion #9 returns 0 (loop cut, no hang)");
}

static void test_guard_cross_thread_wait() {
    // Thread A claim keeps guard; Thread B waits (spins) until A releases → B feels done.
    uint64_t guard = 0;
    std::atomic<bool> b_ready{false}, b_done{false};
    CHECK(bionic___cxa_guard_acquire(&guard) == 1, "guard-wait: A claims");
    std::thread t([&] {
        b_ready = true;
        const int r = bionic___cxa_guard_acquire(&guard);
        b_done = (r == 0); // B only continues when done
    });
    while (!b_ready.load()) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(!b_done.load(), "guard-wait: B blocked while A holds");
    bionic___cxa_guard_release(&guard);
    t.join();
    CHECK(b_done.load(), "guard-wait: B unblocked after release (returns 0 = done)");
}

// ─── 6. guest library dlopen/dlsym/dlclose ──────────────────────────────────
//
// A guest .so LibraryManager already mapped gets its own handle rather than the
// shared DUMMY_HANDLE. The reason is resolution scope: dlsym on DUMMY_HANDLE
// searches every loaded guest library and returns the first match, so a caller that
// deliberately opened one library gets another's function whenever both export the
// same name. AGDK's GameActivity is that caller — it dlopens the .so named by
// android.app.lib_name and reads its entry points out of that handle.
//
// The hooks are installed by kudroid_run_apk in production; here they are installed
// directly, because what needs proving is the shim's routing and — above all — that
// bionic_dlclose does NOT hand a guest handle to ::dlclose. That pointer belongs to
// LibraryManager, and the host loader would dereference it as its own.

static int g_guest_open_calls = 0;
static int g_guest_symbol_calls = 0;
static const char* g_guest_last_open = nullptr;

// Stands in for an ElfLoader. Only its address is used.
static int g_fake_guest_library = 0;
#define FAKE_GUEST_HANDLE (static_cast<void*>(&g_fake_guest_library))
static int g_fake_guest_symbol = 0;
#define FAKE_GUEST_SYMBOL (static_cast<void*>(&g_fake_guest_symbol))

static void* fake_guest_open(const char* filename) {
    ++g_guest_open_calls;
    g_guest_last_open = filename;
    if (filename != nullptr && std::strstr(filename, "libguestprobe.so") != nullptr) {
        return FAKE_GUEST_HANDLE;
    }
    return nullptr;
}

static void* fake_guest_symbol(void* handle, const char* symbol) {
    ++g_guest_symbol_calls;
    if (handle != FAKE_GUEST_HANDLE) return nullptr;
    if (symbol != nullptr && std::strcmp(symbol, "guest_only_entry") == 0) {
        return FAKE_GUEST_SYMBOL;
    }
    return nullptr;
}

static int fake_guest_owns(void* handle) {
    return handle == FAKE_GUEST_HANDLE ? 1 : 0;
}

// ALooper_forThread must never return null.
//
// Android's main thread always has a looper — ActivityThread prepares one before any
// activity exists — so native code treats a null as fatal rather than as something to
// prepare around. AGDK's initializeNativeCode did exactly that: it aborted with
// "Unable to retrieve native ALooper", which surfaced as an UnsatisfiedLinkError during
// onCreate and left the app on a black screen with no crash to read.
//
// KuART's main loop is Java and never called ALooper_prepare, so forThread() had nothing
// to return. Creating on demand is what fixes it, and this pins the property rather than
// the implementation: whoever asks first gets a looper.
static void test_alooper_never_null() {
    void* first = bionic_ALooper_forThread();
    CHECK(first != nullptr,
          "ALooper_forThread returns a looper without a prior prepare");

    // The same looper every time: guest code stores the pointer and compares it later,
    // and a second instance would silently split fd registrations between two loopers.
    CHECK(bionic_ALooper_forThread() == first,
          "ALooper_forThread is stable across calls");
    CHECK(bionic_ALooper_prepare(0) == first,
          "ALooper_prepare returns the same main looper");

    // An unbalanced release must not destroy the main looper. Guest code releases what it
    // did not acquire, and on Android the main looper simply outlives it; freeing here
    // would leave a dangling pointer for any thread already inside pollOnce, faulting far
    // from the release that caused it.
    bionic_ALooper_release(first);
    bionic_ALooper_release(first);
    bionic_ALooper_release(first);
    CHECK(bionic_ALooper_forThread() == first,
          "the main looper survives more releases than acquires");

    bionic_ALooper_acquire(first);
    bionic_ALooper_release(first);
    CHECK(bionic_ALooper_forThread() == first,
          "a balanced acquire/release leaves it alive");
}

static void test_guest_library_handles() {
    std::printf("[dlfcn] guest .so gets its own handle\n");

    kudroid_guest_library_open = &fake_guest_open;
    kudroid_guest_library_symbol = &fake_guest_symbol;
    kudroid_guest_library_owns = &fake_guest_owns;

    // A guest library resolves to its own handle, not the shared dummy one.
    void* h = bionic_dlopen("/data/app/com.example/lib/arm64-v8a/libguestprobe.so", 0);
    CHECK(h == FAKE_GUEST_HANDLE, "dlopen of a guest .so returns its own handle");
    CHECK(g_guest_open_calls == 1, "the guest open hook was consulted");

    // dlsym on that handle reaches the guest library.
    CHECK(bionic_dlsym(h, "guest_only_entry") == FAKE_GUEST_SYMBOL,
          "dlsym on a guest handle resolves within that library");

    // A symbol the guest library does not export must not silently come back from
    // somewhere else via that handle. "malloc" exists in the host process, so this
    // is the case that catches a fallthrough: the guest hook returns null and the
    // host path answers, which is correct for dlsym semantics but must be reached
    // through the fallback and not attributed to the guest handle.
    g_guest_symbol_calls = 0;
    (void)bionic_dlsym(h, "malloc");
    CHECK(g_guest_symbol_calls == 1,
          "an unexported symbol asks the guest library first, then falls back");

    // A library that is NOT a loaded guest .so still gets the dummy handle, so the
    // existing behaviour for libc.so/libm.so is unchanged.
    void* dummy = bionic_dlopen("libnotaguestlibrary_kudroid.so", 0);
    CHECK(dummy != nullptr && dummy != FAKE_GUEST_HANDLE,
          "a non-guest library still resolves to the dummy handle");

    // The one that would crash: ::dlclose on a pointer the host loader never issued.
    CHECK(bionic_dlclose(FAKE_GUEST_HANDLE) == 0,
          "dlclose of a guest handle succeeds without reaching the host loader");

    kudroid_guest_library_open = nullptr;
    kudroid_guest_library_symbol = nullptr;
    kudroid_guest_library_owns = nullptr;

    // With no hook installed the shim must behave exactly as before.
    void* before = bionic_dlopen("libguestprobe.so", 0);
    CHECK(before != FAKE_GUEST_HANDLE,
          "without the hook a guest name falls back to the dummy handle");
}

// ─── dl_iterate_phdr ─────────────────────────────────────────────────────────
//
// This used to return 0 unconditionally, described as a "safe stub". It was not safe: a
// guest's statically linked libc++abi — which every NDK app ships — locates its exception
// tables by walking dl_iterate_phdr for PT_GNU_EH_FRAME. Reporting no modules means
// _Unwind_RaiseException finds no FDE for the throwing frame and calls std::terminate, so
// every C++ `throw` inside a guest library aborts the process.
//
// On device that appeared as four frames of libc++_shared.so above abort() with no
// exception message, immediately after GameActivity_register — which reads as the game
// crashing rather than a shim returning the wrong answer.

extern "C" void kudroid_register_guest_module(void* base, size_t size, const char* path);
extern "C" void kudroid_register_guest_phdrs(void* base, const void* phdrs,
                                             unsigned short phnum);
extern "C" int bionic_dl_iterate_phdr(int (*callback)(void* info, size_t size, void* data),
                                      void* data);

// The prefix of bionic's dl_phdr_info that any unwinder reads.
struct TestDlPhdrInfo {
    uintptr_t      dlpi_addr;
    const char*    dlpi_name;
    const void*    dlpi_phdr;
    unsigned short dlpi_phnum;
};

namespace {

struct PhdrVisit {
    int calls = 0;
    bool sawModule = false;
    uintptr_t addr = 0;
    const void* phdr = nullptr;
    unsigned short phnum = 0;
    std::string name;
    size_t reportedSize = 0;
    // Stop the walk when this module is seen, to check the contract's early exit.
    const char* stopAt = nullptr;
};

int visitPhdrs(void* info, size_t size, void* data) {
    auto* visit = static_cast<PhdrVisit*>(data);
    auto* phdrInfo = static_cast<const TestDlPhdrInfo*>(info);
    ++visit->calls;
    visit->reportedSize = size;
    if (phdrInfo->dlpi_name != nullptr &&
        std::strstr(phdrInfo->dlpi_name, "test_phdr_module.so") != nullptr) {
        visit->sawModule = true;
        visit->addr = phdrInfo->dlpi_addr;
        visit->phdr = phdrInfo->dlpi_phdr;
        visit->phnum = phdrInfo->dlpi_phnum;
        visit->name = phdrInfo->dlpi_name;
    }
    if (visit->stopAt != nullptr && phdrInfo->dlpi_name != nullptr &&
        std::strstr(phdrInfo->dlpi_name, visit->stopAt) != nullptr) {
        return 7;  // any non-zero: the unwinder returns non-zero once it finds its module
    }
    return 0;
}

} // namespace

void test_dl_iterate_phdr_reports_guest_modules() {
    // Stand-in for a mapped image. Only the addresses are compared, so the contents do not
    // matter — what is under test is whether the registration is reported at all.
    alignas(8) static unsigned char fakeImage[256] = {};
    void* base = fakeImage;
    const void* phdrs = fakeImage + 64;

    kudroid_register_guest_module(base, sizeof(fakeImage), "/fake/lib/test_phdr_module.so");
    kudroid_register_guest_phdrs(base, phdrs, 5);

    PhdrVisit visit;
    const int result = bionic_dl_iterate_phdr(&visitPhdrs, &visit);

    CHECK(result == 0, "a walk with no early exit returns 0");
    CHECK(visit.calls >= 1, "the callback runs — the stub never called it at all");
    CHECK(visit.sawModule, "a registered guest module is reported");
    CHECK(visit.addr == reinterpret_cast<uintptr_t>(base),
          "dlpi_addr is the module's load bias");
    CHECK(visit.phdr == phdrs, "dlpi_phdr is the registered header table");
    CHECK(visit.phnum == 5, "dlpi_phnum is the registered count");
    CHECK(visit.name == "/fake/lib/test_phdr_module.so", "dlpi_name is the module path");
    // The size must match the struct bionic passes, or a guest reading the later fields
    // walks off the end of ours.
    CHECK(visit.reportedSize >= sizeof(TestDlPhdrInfo),
          "the reported size covers the fields an unwinder reads");

    // Non-zero from the callback stops the walk and becomes the return value. An unwinder
    // relies on this the moment it finds the module holding the PC.
    PhdrVisit stopping;
    stopping.stopAt = "test_phdr_module.so";
    const int stopped = bionic_dl_iterate_phdr(&visitPhdrs, &stopping);
    CHECK(stopped == 7, "a non-zero callback result stops the walk and is returned");
}

void test_dl_iterate_phdr_skips_modules_without_headers() {
    // A module registered for crash symbolication but whose headers were never recorded
    // must not be reported with a null table — an unwinder would dereference it.
    alignas(8) static unsigned char headerless[64] = {};
    kudroid_register_guest_module(headerless, sizeof(headerless),
                                 "/fake/lib/test_headerless.so");

    struct Seen {
        bool headerless = false;
        bool nullTable = false;
    } seen;

    auto visitor = [](void* info, size_t, void* data) -> int {
        auto* phdrInfo = static_cast<const TestDlPhdrInfo*>(info);
        auto* s = static_cast<Seen*>(data);
        if (phdrInfo->dlpi_phdr == nullptr || phdrInfo->dlpi_phnum == 0) {
            s->nullTable = true;
        }
        if (phdrInfo->dlpi_name != nullptr &&
            std::strstr(phdrInfo->dlpi_name, "test_headerless.so") != nullptr) {
            s->headerless = true;
        }
        return 0;
    };

    bionic_dl_iterate_phdr(visitor, &seen);
    CHECK(!seen.headerless, "a module with no registered headers is skipped");
    CHECK(!seen.nullTable, "no module is reported with a null header table");
}

// ─── guest mutex kinds ───────────────────────────────────────────────────────
// A guest pthread_mutex_t built by PTHREAD_RECURSIVE_MUTEX_INITIALIZER never
// reaches pthread_mutex_init, so the shim first sees it at pthread_mutex_lock and
// must recover its kind from the control word. Bionic stores the kind in bits
// 14-15 of the first 32-bit word.
//
// Getting this wrong is not a subtle degradation: the guest's second re-entrant
// lock blocks forever on a non-recursive host mutex, which presents as a native
// method that entered and never returned, with no output of its own.

// Guest pthread_mutex_t is 4 bytes on bionic arm64, but allocate the full 40-byte
// bionic pthread_mutex_t footprint so nothing the shim reads lands out of bounds.
struct GuestMutex {
    uint32_t control;
    unsigned char reserved[36];
};

static constexpr uint32_t kBionicRecursiveInit = 1u << 14;
static constexpr uint32_t kBionicErrorcheckInit = 2u << 14;

// Bound every lock that is expected to succeed: a regression here hangs rather
// than fails, and a hung test tells you far less than a failing one.
template <typename Fn>
static bool completes_within(Fn&& fn, int timeout_ms) {
    auto fut = std::async(std::launch::async, std::forward<Fn>(fn));
    return fut.wait_for(std::chrono::milliseconds(timeout_ms)) ==
           std::future_status::ready;
}

static void test_static_recursive_mutex_relocks() {
    std::printf("[mutex] static recursive initializer survives lazy creation\n");
    // Deliberately NOT passed to bionic_pthread_mutex_init — that is the whole
    // point. The shim has to infer the kind on first lock.
    static GuestMutex guest = {kBionicRecursiveInit, {}};

    // Lock AND unlock on the same thread: a recursive mutex is owned by the thread
    // that locked it, so unlocking from elsewhere is EPERM and would be testing
    // the wrong thing.
    int inner_unlock = -1;
    int outer_unlock = -1;
    const bool done = completes_within([&] {
        if (bionic_pthread_mutex_lock(&guest) != 0) return false;
        if (bionic_pthread_mutex_lock(&guest) != 0) return false;
        inner_unlock = bionic_pthread_mutex_unlock(&guest);
        outer_unlock = bionic_pthread_mutex_unlock(&guest);
        return true;
    }, 2000);
    CHECK(done, "a recursive static mutex re-locks instead of deadlocking");
    CHECK(inner_unlock == 0, "inner unlock succeeds");
    CHECK(outer_unlock == 0, "outer unlock succeeds");
    bionic_pthread_mutex_destroy(&guest);
}

static void test_static_normal_mutex_reports_deadlock() {
    std::printf("[mutex] static normal initializer reports self-deadlock\n");
    static GuestMutex guest = {0, {}};

    // A NORMAL mutex re-locked by its owner is undefined behaviour in POSIX and a
    // permanent hang in practice. Returning EDEADLK runs the guest's error path
    // instead of losing the thread, and keeps the failure visible in the log.
    int second = 0;
    const bool done = completes_within([&second] {
        if (bionic_pthread_mutex_lock(&guest) != 0) return false;
        second = bionic_pthread_mutex_lock(&guest);
        bionic_pthread_mutex_unlock(&guest);
        return true;
    }, 2000);
    CHECK(done, "re-locking a normal mutex returns instead of blocking forever");
    CHECK(second == EDEADLK, "the second lock reports EDEADLK");
    bionic_pthread_mutex_destroy(&guest);
}

static void test_mutexattr_init_clears_stale_kind() {
    std::printf("[mutex] mutexattr_init clears a stale kind\n");
    // A guest declares pthread_mutexattr_t on the stack; init must not leave
    // whatever was in that frame to be read as the mutex kind.
    uint32_t attr = 0xFFFFFFFFu;
    CHECK(bionic_pthread_mutexattr_init(&attr) == 0, "mutexattr_init succeeds");
    int kind = -1;
    CHECK(bionic_pthread_mutexattr_gettype(&attr, &kind) == 0, "gettype succeeds");
    CHECK(kind == 0, "the kind is NORMAL, not garbage from the caller's stack");

    static GuestMutex guest = {0, {}};
    CHECK(bionic_pthread_mutex_init(&guest, &attr) == 0, "mutex_init with the cleared attr succeeds");
    int second = 0;
    const bool done = completes_within([&second] {
        if (bionic_pthread_mutex_lock(&guest) != 0) return false;
        second = bionic_pthread_mutex_lock(&guest);
        bionic_pthread_mutex_unlock(&guest);
        return true;
    }, 2000);
    CHECK(done && second == EDEADLK, "a mutex built from the cleared attr is not recursive");
    bionic_pthread_mutex_destroy(&guest);
    bionic_pthread_mutexattr_destroy(&attr);
}

static void test_mutexattr_settype_recursive_is_honoured() {
    std::printf("[mutex] mutexattr_settype(RECURSIVE) reaches the host mutex\n");
    uint32_t attr = 0;
    bionic_pthread_mutexattr_init(&attr);
    CHECK(bionic_pthread_mutexattr_settype(&attr, 1 /* RECURSIVE */) == 0,
          "settype(RECURSIVE) succeeds");
    int kind = -1;
    bionic_pthread_mutexattr_gettype(&attr, &kind);
    CHECK(kind == 1, "gettype reads back RECURSIVE");

    static GuestMutex guest = {0, {}};
    CHECK(bionic_pthread_mutex_init(&guest, &attr) == 0, "mutex_init(RECURSIVE) succeeds");
    const bool done = completes_within([] {
        if (bionic_pthread_mutex_lock(&guest) != 0) return false;
        if (bionic_pthread_mutex_lock(&guest) != 0) return false;
        bionic_pthread_mutex_unlock(&guest);
        bionic_pthread_mutex_unlock(&guest);
        return true;
    }, 2000);
    CHECK(done, "an explicitly recursive mutex re-locks");
    bionic_pthread_mutex_destroy(&guest);
    bionic_pthread_mutexattr_destroy(&attr);
}

static void test_mutex_reinit_replaces_mapping() {
    std::printf("[mutex] re-init replaces the host mutex rather than leaking it\n");
    static GuestMutex guest = {0, {}};

    // First init: NORMAL. Lock and unlock so the mapping is definitely live.
    uint32_t normal = 0;
    bionic_pthread_mutexattr_init(&normal);
    CHECK(bionic_pthread_mutex_init(&guest, &normal) == 0, "first init succeeds");
    CHECK(bionic_pthread_mutex_lock(&guest) == 0, "lock after first init succeeds");
    CHECK(bionic_pthread_mutex_unlock(&guest) == 0, "unlock after first init succeeds");

    // Re-init the SAME guest address as RECURSIVE. The new kind must take effect;
    // if the old mapping were kept, the re-lock below would deadlock.
    uint32_t recursive = 0;
    bionic_pthread_mutexattr_init(&recursive);
    bionic_pthread_mutexattr_settype(&recursive, 1);
    CHECK(bionic_pthread_mutex_init(&guest, &recursive) == 0, "re-init succeeds");
    const bool done = completes_within([] {
        if (bionic_pthread_mutex_lock(&guest) != 0) return false;
        if (bionic_pthread_mutex_lock(&guest) != 0) return false;
        bionic_pthread_mutex_unlock(&guest);
        bionic_pthread_mutex_unlock(&guest);
        return true;
    }, 2000);
    CHECK(done, "the re-initialised kind is the one in effect");
    bionic_pthread_mutex_destroy(&guest);
    bionic_pthread_mutexattr_destroy(&normal);
    bionic_pthread_mutexattr_destroy(&recursive);
}

static void test_mutex_shared_between_threads() {
    std::printf("[mutex] a guest mutex still excludes a second thread\n");
    // The trylock fast path must not turn a real lock into a no-op: two threads
    // incrementing under the same guest mutex must not lose an update.
    static GuestMutex guest = {0, {}};
    uint32_t attr = 0;
    bionic_pthread_mutexattr_init(&attr);
    bionic_pthread_mutex_init(&guest, &attr);

    constexpr int kThreads = 4;
    constexpr int kIterations = 2000;
    long counter = 0;  // deliberately not atomic — the mutex is what protects it
    std::atomic<bool> lock_error{false};

    auto worker = [&] {
        for (int i = 0; i < kIterations; ++i) {
            if (bionic_pthread_mutex_lock(&guest) != 0) { lock_error = true; return; }
            ++counter;
            if (bionic_pthread_mutex_unlock(&guest) != 0) { lock_error = true; return; }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    CHECK(!lock_error.load(), "every lock and unlock reported success");
    CHECK(counter == static_cast<long>(kThreads) * kIterations,
          "no increment was lost — the mutex provides real exclusion");
    bionic_pthread_mutex_destroy(&guest);
    bionic_pthread_mutexattr_destroy(&attr);
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("=== SyscallShim host tests ===\n");
    test_pthread_once_nested();
    test_pthread_once_concurrent();
    test_futex_wait_wake();
    test_futex_eagain();
    test_futex_etimedout();
    test_futex_wait_timeout_is_relative();
    test_futex_wait_relative_zero_does_not_hang();
    test_futex_wait_rejects_malformed_relative_timeout();
    test_futex_wait_bitset_timeout_is_absolute();
    test_futex_wait_bitset_absolute_wake_still_works();
    test_futex_cmp_requeue_precond();
    test_futex_wait_wake_cycles();
    test_syscall_mappings();
    test_sched_getaffinity_raw_returns_byte_count();
    test_sched_getaffinity_wrapper_returns_zero();
    test_sem_counting_semantics();
    test_sem_blocks_until_posted();
    test_sem_post_releases_one_waiter();
    test_sem_timedwait_deadline();
    test_sem_destroy_releases_waiters();
    test_sem_rejects_null();
    test_mremap_shrink_in_place();
    test_mremap_grow_with_maymove();
    test_sigaction_flag_roundtrip();
    test_guard_acquire_release();
    test_guard_same_tid_recursion_tolerated();
    test_guard_cross_thread_wait();
    test_guard_recursion_loop_cut();
    test_alooper_never_null();
    test_guest_library_handles();
    test_dl_iterate_phdr_reports_guest_modules();
    test_dl_iterate_phdr_skips_modules_without_headers();
    test_static_recursive_mutex_relocks();
    test_static_normal_mutex_reports_deadlock();
    test_mutexattr_init_clears_stale_kind();
    test_mutexattr_settype_recursive_is_honoured();
    test_mutex_reinit_replaces_mapping();
    test_mutex_shared_between_threads();
    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
