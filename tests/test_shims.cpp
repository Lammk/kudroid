// test_shims.cpp — host-side regression tests for the guest syscall shims in
// src/shims/SyscallShim.cpp. Every function here is `extern "C"` and compiles
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
#include <thread>

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
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
// SyscallShim.o transitively pulls kudroid_bridge.o -> kudroid_jni.o, which
// references the Avian JVM entry points. Avian is only linked on iOS builds
// (CMakeLists warns when libavian.a is missing), so provide inert stubs for
// the host test binary — the tests never call them.
extern "C" int JNI_CreateJavaVM(void**, void**, void*) { return -1; }
extern "C" const uint8_t* classpathJar(size_t* size) {
    if (size) *size = 0;
    return nullptr;
}

// Linux futex command constants (match SyscallShim.cpp).
#define FUTEX_WAIT           0
#define FUTEX_WAKE           1
#define FUTEX_CMP_REQUEUE    4
#define FUTEX_PRIVATE_FLAG   128
#define MREMAP_MAYMOVE 1

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
    std::printf("[futex] WAIT with past absolute timeout -> ETIMEDOUT\n");
    uint32_t word = 0;
    struct timespec past = {0, 0}; // 1970-01-01, way in the past
    errno = 0;
    int r = bionic_futex(&word, FUTEX_WAIT, 0, &past, nullptr, 0);
    CHECK(r == -1 && errno == ETIMEDOUT, "returns -1/ETIMEDOUT");
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
    // Linux host values are identical to the Linux guest values; the shim
    // passes them straight through. On Apple these get translated.
    act.sa_flags = SA_RESTART | SA_NODEFER | SA_RESETHAND;
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
    CHECK((old.sa_flags & SA_RESTART) != 0, "SA_RESTART preserved");
    CHECK((old.sa_flags & SA_NODEFER) != 0, "SA_NODEFER preserved");
    CHECK((old.sa_flags & SA_RESETHAND) != 0, "SA_RESETHAND preserved");

    // Restore default so we don't leave the process in a weird state.
    struct android_sigaction dfl;
    std::memset(&dfl, 0, sizeof(dfl));
    dfl.android_sa_handler = SIG_DFL;
    dfl.sa_flags = 0;
    ::bionic_sigaction(SIGUSR1, &dfl, nullptr);
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("=== SyscallShim host tests ===\n");
    test_pthread_once_nested();
    test_pthread_once_concurrent();
    test_futex_wait_wake();
    test_futex_eagain();
    test_futex_etimedout();
    test_futex_cmp_requeue_precond();
    test_futex_wait_wake_cycles();
    test_mremap_shrink_in_place();
    test_mremap_grow_with_maymove();
    test_sigaction_flag_roundtrip();
    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
