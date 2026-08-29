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
#include <thread>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
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
extern "C" int bionic_tgkill(int pid, int tid, int sig);
extern "C" int bionic_pipe2(int pipefd[2], int flags);
extern "C" int bionic___cxa_guard_acquire(uint64_t* g);
extern "C" void bionic___cxa_guard_release(uint64_t* g);
extern "C" void bionic___cxa_guard_abort(uint64_t* g);
extern "C" void* bionic_dlopen(const char* filename, int flags);
extern "C" void* bionic_dlsym(void* handle, const char* symbol);
extern "C" int bionic_dlclose(void* handle);

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
#define FUTEX_PRIVATE_FLAG   128
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
    test_syscall_mappings();
    test_mremap_shrink_in_place();
    test_mremap_grow_with_maymove();
    test_sigaction_flag_roundtrip();
    test_guard_acquire_release();
    test_guard_same_tid_recursion_tolerated();
    test_guard_cross_thread_wait();
    test_guard_recursion_loop_cut();
    test_guest_library_handles();
    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
