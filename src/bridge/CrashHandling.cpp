// CrashHandling.cpp — the process-wide signal handler, the fault-skip
// machinery and the JNI_OnLoad shield. Split out of kudroid_bridge.cpp.
#include "BridgeState.h"
#include "BridgeShared.h"
#include "CrashInternal.h"
#include "CrashHandling.h"
#include "kudroid/BionicShim.h"
#include "kudroid/FaultSkip.h"
#include "kudroid/debug/FrameWalk.h"
#include "kudroid/KuArtRuntime.h"
#include "kudroid/elf_loader.hpp"
#include "kudroid/NativeCallTelemetry.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <setjmp.h>
#include <sys/ucontext.h>
#include <unistd.h>
#include <unwind.h>
#include <mutex>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_status.h>
#include <exception>
extern "C" {
extern void* objc_msgSend(void* self, void* op, ...);
extern void* sel_registerName(const char* name);
extern void NSSetUncaughtExceptionHandler(void (*handler)(void* exception));
}
#else
#include <sys/syscall.h>
#endif

// Cross-unit externs the handler reaches into.
extern "C" bool bionic_handle_guest_syscall_trap(void* context);
extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);
#include "kudroid/abi/GuestSignals.h"  // guest_signal_dispatch / has_handler

// ── JNI_OnLoad abort() Shield ────────────────────────────────────────────────
// Secondary libraries (conscrypt, HttpClient, maesdk, etc.) may call abort()
// directly in JNI_OnLoad when optional methods or fields are missing. abort()
// triggers SIGABRT which bypasses C++ try/catch and kills the process. This guard
// catches aborts during JNI_OnLoad and skips the failed module so app startup continues.
// Only active strictly around JNI_OnLoad invocations.
static thread_local sigjmp_buf g_jniGuardJmp;
static thread_local volatile sig_atomic_t g_jniGuardActive = 0;
static thread_local int g_jniGuardSignal = 0;

// Machine state captured when the guard swallows a signal.
//
// The guard used to siglongjmp straight out of the signal handler, which is before
// crashHandler writes kudroid_crash.log — so a library that segfaulted inside
// JNI_OnLoad produced one WARNING line and nothing else: no pc, no fault address,
// no stack. There was no way to tell what it touched.
//
// The handler cannot format or write safely, so it only copies scalars here (all
// async-signal-safe stores) and the caller reports them after the jump returns.
struct JniGuardFault {
    int signal = 0;
    int si_code = 0;
    const void* fault_addr = nullptr;
    uint64_t pc = 0;
    uint64_t lr = 0;
    uint64_t sp = 0;
    uint64_t fp = 0;
    uint64_t x[9] = {0};
    // Stack words from sp, copied in the handler because by the time the caller
    // runs the frame is gone.
    uint64_t stack[32] = {0};
    bool have_regs = false;
};
static thread_local JniGuardFault g_jniGuardFault;

// Dedicated alternate stack for signal handlers. Stored at file scope because after
// siglongjmp out of a handler, the kernel still treats the alt stack as active;
// it must be re-armed for subsequent crash handling.
static char g_altSignalStack[64 * 1024];
static bool g_altStackArmed = false;

static void armAltSignalStack(void) {
    stack_t ss;
    memset(&ss, 0, sizeof(ss));
    ss.ss_sp = g_altSignalStack;
    ss.ss_size = sizeof(g_altSignalStack);
    ss.ss_flags = 0;
    g_altStackArmed = (sigaltstack(&ss, nullptr) == 0);
}

// Invoke JNI_OnLoad under signal protection. sigsetjmp is placed in a dedicated function
// to prevent longjmp from clobbering caller local variables (-Wclobbered).
// Returns: 0 on normal return (*outVersion valid), -1 on C++ exception, >0 on trapped signal.
//
// A guarded library often calls back into Java before it faults (RegisterNatives,
// GetMethodID, an actual Java call), so the interpreter may have live frames when
// the jump happens. siglongjmp does not unwind, so those frames' scope guards never
// run and the interpreter is left describing frames that no longer exist — the next
// exception on this thread then crashes rendering its own stack trace. Snapshot the
// interpreter's per-thread bookkeeping before the call and put it back after any
// abnormal return.
__attribute__((noinline))
int kudroid_call_jni_onload_guarded(jint (*fn)(JavaVM*, void*),
                                          JavaVM* vm, jint* outVersion) {
    // volatile: read after siglongjmp, so it must not live only in a register the
    // jump restores to its pre-call value.
    static thread_local volatile size_t kuartState[KUART_THREAD_STATE_WORDS] = {0};
    kuart_save_thread_state(const_cast<size_t*>(kuartState));

    if (sigsetjmp(g_jniGuardJmp, 1) != 0) {
        // Return from signal handler — re-arm alternate stack since siglongjmp
        // does not unwind the alt stack context normally.
        if (g_altStackArmed) armAltSignalStack();
        kuart_restore_thread_state(const_cast<const size_t*>(kuartState));
        return g_jniGuardSignal > 0 ? g_jniGuardSignal : 1;
    }
    g_jniGuardActive = 1;
    g_jniGuardFault = JniGuardFault{};
    int rc = 0;
    try {
        *outVersion = fn(vm, nullptr);
    } catch (...) {
        rc = -1;
    }
    g_jniGuardActive = 0;
    // A C++ exception thrown through the guest library unwinds C++ frames but the
    // interpreter's guards are only on Execute() frames it owns; a library that
    // throws across a JNI boundary can still skip them.
    if (rc != 0) kuart_restore_thread_state(const_cast<const size_t*>(kuartState));
    return rc;
}

// Build stamp — forward declaration used in appendTestHeader.
extern "C" const char* kudroid_build_stamp(void);

// Recovery budget for worker faults: a chunk-processing job over corrupt
// data faults per element (observed: 4 faults/iteration), so a few dozen bad
// elements need a triple-digit budget. Past it the thread is not progressing
// and the fault is fatal. Each skip is microseconds; the cost of headroom is
// a few hundred breadcrumb lines worst case, while too small a cap (16 fired
// in 25ms) turns a survivable batch into a shutdown.
static constexpr int kMaxWorkerRecoveries = 128;
// A different shape of storm is a bounded indexing loop: Unity's serialized
// pointer-relocation pass walks a relocation table and stores one pointer per
// entry into an output array, e.g.
//   str x17, [x18, x16, lsl #3]   // x16 = 0..0xfe, x18 = output array
// When that array field is null the store faults on every iteration and the
// address advances by 8 each time until the loop terminates. That loop is
// self-terminating (fixed trip count), so the general 128 budget kills a run
// that was about to finish. Forward walks at one pc get a separate, larger
// ceiling; anything that repeats or regresses an address still charges the
// normal budget and bails, so a genuine infinite loop cannot hide here.
static constexpr int kMaxWalkRecoveries = 8192;
// A null-base loop is never a reservation walk: below 0x10000 there is nothing
// to commit, so every skip only pushes the corruption further (observed: a
// null memset dropped store-by-store, then a SIGBUS through a garbage register
// far from the loop). Same-pc null faults arriving back-to-back get a tiny
// run, not the walk ceiling — the report must name the loop, not its
// aftermath. Back-to-back is load-bearing: the same pc faulting once per
// frame is a recurring bad object, not a loop, and must not accumulate.
// Loop iterations are microseconds apart (each skip is microseconds) while
// frames are milliseconds apart, so a 1ms gap restarts the run.
static constexpr int kMaxNullRun = 4;
static constexpr long long kMaxNullGapNs = 1000000LL;
// Nullish address: below the first page, or just below zero (a null base
// with a negative structure offset — observed: ldp [x18=0,#-32] faulting at
// -32). Nothing is mapped at either extreme, so inventing a result there is
// exactly as safe as on the null page.
static bool fault_addr_is_nullish(unsigned long long addr) {
    return addr < 0x10000u || addr >= (0ull - 0x10000ull);
}
// Largest per-step advance still treated as one contiguous walk. A stride that
// jumps further is not a bounded table walk and must not inherit this budget.
static constexpr unsigned long long kWalkStepMax = 1ULL << 20;
// Per-worker recovery budgets: the old single global counter let one hot
// worker eat the budget for all, and its 30s reset raced (one thread's stale
// read zeroed another's accumulated count → unbounded skips). Slots are keyed
// by tid, atomics-only (signal context: no locks, no alloc). More distinct
// faulting workers than slots fails CLOSED (no slot = no skip).
struct WorkerBudget {
    std::atomic<unsigned long long> tid{0};
    std::atomic<int> count{0};
    // Steady-clock ns of this thread's last skipped fault. A 30s+ gap means
    // the thread ran fine in between — isolated faults, not a storm — so the
    // budget resets. A genuine storm faults microseconds apart and never
    // sees a reset.
    std::atomic<long long> lastNs{0};
    // Last skipped fault (pc, address) and how many consecutive same-pc
    // forward steps it has taken. See kMaxWalkRecoveries.
    std::atomic<unsigned long long> lastPc{0};
    std::atomic<unsigned long long> lastAddr{0};
    std::atomic<int> walk{0};
    // Consecutive same-pc faults in the null page. See kMaxNullRun.
    std::atomic<int> nullRun{0};
};
static WorkerBudget g_workerBudgets[8];

void crashResetWorkerBudgets(void) {
    for (auto& slot : g_workerBudgets) {
        slot.tid.store(0, std::memory_order_relaxed);
        slot.count.store(0, std::memory_order_relaxed);
        slot.lastNs.store(0, std::memory_order_relaxed);
        slot.nullRun.store(0, std::memory_order_relaxed);
    }
}

// Process-wide per-pc skip ledger. Per-thread budgets are blind to a storm
// spread across sibling workers: ~16 Job.Workers walked the SAME load in
// lockstep, each staying far under its own cap while the fleet burned ~3000
// skips into one uncommitted reservation and ended in SIGBUS at the cliff.
// One load skipped more than a few hundred times process-wide is not
// recovering anything — every skip poisons a destination register and steps
// the walk deeper. Fixed table, atomics only (signal context: no locks, no
// alloc). Untracked (table full of other pcs) fails open: per-thread budgets
// still bound those.
static constexpr int kMaxSkipsPerPc = 1024;
struct PcSkipSlot {
    std::atomic<unsigned long long> pc{0};
    std::atomic<int> count{0};
};
static PcSkipSlot g_pcSkips[32];
static bool pc_skip_peek(unsigned long long pc) {
    if (pc == 0) return true;
    for (const auto& s : g_pcSkips) {
        if (s.pc.load(std::memory_order_relaxed) == pc) {
            return s.count.load(std::memory_order_relaxed) < kMaxSkipsPerPc;
        }
    }
    return true;
}
static void pc_skip_note(unsigned long long pc) {
    if (pc == 0) return;
    for (auto& s : g_pcSkips) {
        if (s.pc.load(std::memory_order_relaxed) == pc) {
            const int before = s.count.fetch_add(1, std::memory_order_relaxed);
            if (before + 1 == kMaxSkipsPerPc) {
                char msg[96];
                std::snprintf(msg, sizeof(msg),
                              "skip-storm: pc 0x%llx retired after %d skips",
                              static_cast<unsigned long long>(pc), kMaxSkipsPerPc);
                kudroid_android_log_message(4, "KuDroidSignal", msg);
            }
            return;
        }
    }
    for (auto& s : g_pcSkips) {
        unsigned long long z = 0;
        if (s.pc.compare_exchange_strong(z, pc, std::memory_order_relaxed)) {
            s.count.store(1, std::memory_order_relaxed);
            return;
        }
    }
}

// True when this fault continues a bounded same-pc forward walk.
static bool worker_fault_is_walk(const WorkerBudget* s, unsigned long long pc,
                                 unsigned long long addr) {
    if (s == nullptr || pc == 0) return false;
    const unsigned long long lp = s->lastPc.load(std::memory_order_relaxed);
    const unsigned long long la = s->lastAddr.load(std::memory_order_relaxed);
    return lp == pc && addr > la && (addr - la) <= kWalkStepMax;
}

static WorkerBudget* worker_budget_for(unsigned long long tid) {
    if (tid == 0) return nullptr;
    for (auto& s : g_workerBudgets) {
        if (s.tid.load(std::memory_order_relaxed) == tid) return &s;
    }
    for (auto& s : g_workerBudgets) {
        unsigned long long z = 0;
        if (s.tid.compare_exchange_strong(z, tid, std::memory_order_relaxed)) return &s;
        if (z == tid) return &s;
    }
    return nullptr;
}

// Records this thread's skip at nowNs; returns false when its budget is spent.
// Only the timestamp-CAS winner zeroes (stale-gap reset), so a concurrent
// increment is never lost into a fresh zero.
static bool worker_budget_note(WorkerBudget* s, long long nowNs,
                               unsigned long long pc, unsigned long long addr) {
    const long long last = s->lastNs.load(std::memory_order_relaxed);
    if (last != 0 && nowNs - last > 30000000000LL) {
        long long expect = last;
        if (s->lastNs.compare_exchange_strong(expect, nowNs, std::memory_order_relaxed)) {
            s->count.store(0, std::memory_order_relaxed);
            s->walk.store(0, std::memory_order_relaxed);
            s->lastPc.store(0, std::memory_order_relaxed);
            s->lastAddr.store(0, std::memory_order_relaxed);
        }
    } else {
        s->lastNs.store(nowNs, std::memory_order_relaxed);
    }
    // A same-pc forward step is a bounded table walk, not a stuck storm: charge
    // the dedicated walk budget (and count the walk length) instead of the
    // general one. Any other shape charges count, which is what eventually
    // parks a genuinely broken worker.
    if (worker_fault_is_walk(s, pc, addr)) {
        s->walk.fetch_add(1, std::memory_order_relaxed);
    } else {
        s->walk.store(1, std::memory_order_relaxed);
        if (s->count.load(std::memory_order_relaxed) >= kMaxWorkerRecoveries) return false;
        s->count.fetch_add(1, std::memory_order_relaxed);
    }
    // Null-page run tracking (see kMaxNullRun): same-pc faults below 0x10000
    // arriving back-to-back are a null-base loop. `last` is the previous
    // fault's timestamp, read before this one overwrote it. Read lastPc
    // before overwriting it too.
    const bool tight = last != 0 && nowNs - last <= kMaxNullGapNs && pc != 0 &&
                       pc == s->lastPc.load(std::memory_order_relaxed) &&
                       fault_addr_is_nullish(addr);
    if (tight) {
        const int run = s->nullRun.fetch_add(1, std::memory_order_relaxed) + 1;
        if (run == kMaxNullRun) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "null-run: pc 0x%llx retired after %d null faults",
                          pc, kMaxNullRun);
            kudroid_android_log_message(4, "KuDroidSignal", msg);
        }
    } else {
        s->nullRun.store(fault_addr_is_nullish(addr) ? 1 : 0, std::memory_order_relaxed);
    }
    s->lastPc.store(pc, std::memory_order_relaxed);
    s->lastAddr.store(addr, std::memory_order_relaxed);
    return true;
}

// Peek without recording: the skip itself mutates guest context, so the budget
// must gate the attempt, not follow it. Single-threaded per slot in practice
// (one thread's handler cannot run concurrently with itself), so peek→try→note
// cannot interleave against itself; cross-tid slot sharing only follows OS tid
// reuse after a thread died.
static bool worker_budget_peek(WorkerBudget* s, unsigned long long pc,
                               unsigned long long addr) {
    if (s == nullptr) return false;
    // Null-base loop breaker (see kMaxNullRun): once the run is spent the
    // skip is refused, so the fatal path reports the loop instead of masking
    // it into downstream garbage. Time-gated like the counter: a spent run
    // from an earlier burst must not refuse a fresh fault (lastNs is the
    // previous fault's timestamp; clock_gettime is signal-safe).
    if (pc != 0 && pc == s->lastPc.load(std::memory_order_relaxed) &&
        fault_addr_is_nullish(addr) &&
        s->nullRun.load(std::memory_order_relaxed) >= kMaxNullRun) {
        const long long nowNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        const long long prevNs = s->lastNs.load(std::memory_order_relaxed);
        if (prevNs != 0 && nowNs - prevNs <= kMaxNullGapNs) return false;
    }
    if (worker_fault_is_walk(s, pc, addr)) {
        return s->walk.load(std::memory_order_relaxed) < kMaxWalkRecoveries;
    }
    // A retired walk re-enters here with count at its cap: fail closed.
    return s->count.load(std::memory_order_relaxed) < kMaxWorkerRecoveries;
}


// ── Host Abort Interception ──────────────────────────────────────────────────
// SIGABRT typically results from an uncaught exception or failed assertion.
// These handlers record the abort message before termination:
// 1. Uncaught ObjC exceptions (NSException from ANGLE/Metal/UIKit)
// 2. Uncaught C++ exceptions (std::terminate / exception::what())
#if defined(__APPLE__)
#include <exception>
extern "C" {
extern void* objc_msgSend(void* self, void* op, ...);
extern void* sel_registerName(const char* name);
extern void NSSetUncaughtExceptionHandler(void (*handler)(void* exception));
}

static void kudroid_uncaught_objc_handler(void* exception) {
    // exception is NSException* — [exception reason] -> NSString* -> UTF8String.
    if (!exception) return;
    void* reason = objc_msgSend(exception, sel_registerName("reason"));
    if (reason) {
        const char* utf8 = static_cast<const char*>(
            objc_msgSend(reason, sel_registerName("UTF8String")));
        if (utf8) kudroid_store_abort_message(utf8);
    }
}

static void kudroid_terminate_handler() {
    if (std::current_exception()) {
        try {
            std::rethrow_exception(std::current_exception());
        } catch (const std::exception& e) {
            if (e.what() && e.what()[0]) kudroid_store_abort_message(e.what());
        } catch (...) {
            kudroid_store_abort_message("uncaught non-std C++ exception");
        }
    }
    std::abort();
}
#endif

// snprintf returns the would-be written length (which exceeds buffer size on truncation).
// Clamping to actual buffer bounds prevents stack overrun and garbage output in crash logs.
static void crashWriteLine(int fd, const char* buf, int len, size_t bufSize) {
    if (len <= 0 || !buf) return;
    size_t n = (size_t)len;
    if (n >= bufSize) n = bufSize - 1;
    (void)!write(fd, buf, n);
}

// Unwind via _Unwind_Backtrace (no heap allocations) + dladdr (best effort).
struct UnwindContext {
    int fd;
    int count;
};

static _Unwind_Reason_Code unwindCallback(struct _Unwind_Context* ctx, void* arg) {
    UnwindContext* u = static_cast<UnwindContext*>(arg);
    if (u->count >= 20) return _URC_END_OF_STACK;
    const uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc != 0) {
        char line[512];
        Dl_info info;
        int m;
        if (dladdr(reinterpret_cast<void*>(pc), &info) != 0 && info.dli_fname) {
            if (info.dli_sname) {
                const long offset = (long)(pc - (uintptr_t)info.dli_saddr);
                m = snprintf(line, sizeof(line), "  #%02d pc 0x%llx  %s+0x%lx (%s)\n",
                             u->count, (unsigned long long)pc, info.dli_sname,
                             offset, info.dli_fname);
            } else {
                const long offset = (long)(pc - (uintptr_t)info.dli_fbase);
                m = snprintf(line, sizeof(line),
                             "  #%02d pc 0x%llx  +0x%lx (%s)\n",
                             u->count, (unsigned long long)pc, offset,
                             info.dli_fname);
            }
        } else {
            char guest[512];
            if (kudroid::kudroid_lookup_guest_module(
                    reinterpret_cast<void*>(pc), guest, sizeof(guest))) {
                m = snprintf(line, sizeof(line), "  #%02d pc 0x%llx  %s\n",
                             u->count, (unsigned long long)pc, guest);
            } else {
                m = snprintf(line, sizeof(line), "  #%02d pc 0x%llx\n",
                             u->count, (unsigned long long)pc);
            }
        }
        crashWriteLine(u->fd, line, m, sizeof(line));
    }
    u->count++;
    return _URC_NO_REASON;
}

static void writeBacktrace(int fd) {
    UnwindContext ctx = {fd, 0};
    _Unwind_Backtrace(unwindCallback, &ctx);
}


// Build stamp: distinguishes whether the currently running IPA is the latest version.
// Compare this stamp in kudroid_crash.log against CI build stamps to verify
// the installed build version.

#if defined(__aarch64__) || defined(__arm64__)
// Symbolicate an address to 'function+offset (module)' or 'module+offset' —
// providing exact crash location context instead of raw unmapped addresses.
// Prioritize guest ELF loader symbol tables over host dladdr.
// Falls back to host dladdr and raw module base offsets.
static void symbolicateAddr(uintptr_t pc, char* out, size_t outSize) {
    if (kudroid::kudroid_lookup_guest_module(reinterpret_cast<void*>(pc), out, outSize)) {
        return;
    }
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(pc), &info) != 0 && info.dli_fname) {
        if (info.dli_sname) {
            const long offset = (long)(pc - (uintptr_t)info.dli_saddr);
            snprintf(out, outSize, "%s+0x%lx (%s)", info.dli_sname, offset,
                     info.dli_fname);
        } else {
            const long offset = (long)(pc - (uintptr_t)info.dli_fbase);
            snprintf(out, outSize, "0x%llx+0x%lx (%s)",
                     (unsigned long long)pc, offset, info.dli_fname);
        }
    } else {
        snprintf(out, outSize, "0x%llx (no symbol)", (unsigned long long)pc);
    }
}
#endif

// Render the state captured when the JNI_OnLoad guard swallowed a signal.
//
// The guard jumps out of the signal handler before crashHandler's reporting code
// runs, so a library that faulted inside JNI_OnLoad produced a single WARNING line
// and nothing usable. This produces the same information the crash log carries —
// signal, fault address, pc/lr symbolicated, registers, raw stack — for a fault the
// process survived.
std::string describeJniGuardFault(const std::string& library, int guardRc) {
    const JniGuardFault& f = g_jniGuardFault;
    std::string out;
    char line[1024];

    snprintf(line, sizeof(line),
             "[kudroid_core] === JNI_OnLoad fault in %s ===\n", library.c_str());
    out += line;
    snprintf(line, sizeof(line), "build: %s\n", kudroid_build_stamp());
    out += line;

    // guardRc > 0 is the signal number; -1 means a C++ exception escaped, which
    // never reaches the handler and therefore has no register state.
    if (guardRc < 0) {
        out += "cause: C++ exception escaped JNI_OnLoad (no signal, no registers)\n";
        return out;
    }

    const int sig = f.signal != 0 ? f.signal : guardRc;
    const char* signame = "?";
    switch (sig) {
        case SIGSEGV: signame = "SIGSEGV"; break;
        case SIGBUS:  signame = "SIGBUS";  break;
        case SIGABRT: signame = "SIGABRT"; break;
        case SIGILL:  signame = "SIGILL";  break;
        case SIGTRAP: signame = "SIGTRAP"; break;
        default: break;
    }
    snprintf(line, sizeof(line), "signal = %d (%s)\nsi_code = %d\nfault_addr = %p\n",
             sig, signame, f.si_code, f.fault_addr);
    out += line;

    // A near-null fault address is the signature of dereferencing a JNI handle that
    // came back null — usually a FindClass/GetMethodID that failed and whose return
    // value the library never checked.
    const uintptr_t addr = reinterpret_cast<uintptr_t>(f.fault_addr);
    if (sig == SIGSEGV && addr < 0x10000) {
        snprintf(line, sizeof(line),
                 "note: fault address is near null (offset 0x%llx) — typically a null "
                 "JNI handle dereferenced without checking the return value\n",
                 (unsigned long long)addr);
        out += line;
    }

    if (!f.have_regs) {
        out += "registers: unavailable on this platform\n";
        return out;
    }

    snprintf(line, sizeof(line), "pc = 0x%llx\nlr = 0x%llx\nsp = 0x%llx\nfp = 0x%llx\n",
             (unsigned long long)f.pc, (unsigned long long)f.lr,
             (unsigned long long)f.sp, (unsigned long long)f.fp);
    out += line;

#if defined(__aarch64__) || defined(__arm64__)
    {
        char symPc[512];
        char symLr[512];
        symbolicateAddr(static_cast<uintptr_t>(f.pc), symPc, sizeof(symPc));
        symbolicateAddr(static_cast<uintptr_t>(f.lr), symLr, sizeof(symLr));
        snprintf(line, sizeof(line), "pc_sym: %s\nlr_sym: %s\n", symPc, symLr);
        out += line;
    }
#endif

    for (int i = 0; i < 9; ++i) {
        snprintf(line, sizeof(line), "x%d = 0x%llx\n", i, (unsigned long long)f.x[i]);
        out += line;
    }

    out += "--- stack from sp ---\n";
    for (int i = 0; i < 32; i += 4) {
        snprintf(line, sizeof(line), "sp%+04d: %016llx  %016llx  %016llx  %016llx\n",
                 i * 8, (unsigned long long)f.stack[i],
                 (unsigned long long)f.stack[i + 1],
                 (unsigned long long)f.stack[i + 2],
                 (unsigned long long)f.stack[i + 3]);
        out += line;
    }

#if defined(__aarch64__) || defined(__arm64__)
    // Walk the frame chain so the caller inside the library is named, not just the
    // faulting instruction.
    //
    // Through FrameWalker, with this thread's real stack bounds. The previous version
    // accepted any fp in [0x1000, 0x7fffffffffff] and followed `fp = *fp` — the same
    // shape that crashed the guard diagnostic on the guest's main thread with
    // fault_addr=0x100000000050, an address that passes that test and is nowhere near a
    // stack. A diagnostic must not be able to do that.
    //
    // The bounds are the CALLING thread's, which is correct here: this runs after the
    // guard longjmp'd back, on the thread that took the fault.
    out += "--- fp chain ---\n";
    {
        kudroid::FrameWalker walker(static_cast<uintptr_t>(f.fp),
                                    kudroid::query_thread_stack_bounds());
        if (!walker.valid()) {
            out += "  (no readable frame chain: fp is outside this thread's stack)\n";
        }
        for (int depth = 0; depth < 24 && walker.valid(); ++depth) {
            uintptr_t savedLr = 0;
            if (!walker.return_address(&savedLr)) break;
            char sym[512];
            symbolicateAddr(savedLr, sym, sizeof(sym));
            snprintf(line, sizeof(line), "  #%02d lr=0x%llx  %s\n", depth,
                     (unsigned long long)savedLr, sym);
            out += line;
            if (!walker.next()) break;
        }
    }
#endif

    return out;
}


// Handlers that were installed before KuDroid's, per signal.
//
// A plain array indexed by signal number, not a map: this is read from a signal
// handler, where allocating or taking a lock is not allowed. NSIG covers every signal
// the platform defines.
static struct sigaction g_previousHandlers[NSIG];

// Hand a fault to whoever had the signal before us.
//
// Only reached for faults KuDroid does not own. Restoring the previous disposition and
// re-raising is what lets a chained handler see the ORIGINAL machine state: calling its
// function pointer directly would work for an SA_SIGINFO handler but not for a Mach
// exception port, and re-raising covers both.
static bool chainToPreviousHandler(int sig) {
    if (sig <= 0 || sig >= NSIG) return false;
    const struct sigaction& previous = g_previousHandlers[sig];
    const bool hasHandler =
        (previous.sa_flags & SA_SIGINFO) ? previous.sa_sigaction != nullptr
                                         : (previous.sa_handler != nullptr &&
                                            previous.sa_handler != SIG_DFL &&
                                            previous.sa_handler != SIG_IGN);
    if (!hasHandler) return false;
    sigaction(sig, &previous, nullptr);
    raise(sig);
    return true;
}


// ── AArch64 fault-instruction skip & worker resume ─────────────────────────
// A faulted worker parked forever deadlocks the engine: the main thread waits
// on the worker's job fence (JobHandle.Complete in libsystem cvwait) and the
// picture freezes with input dead. For a faulting LOAD/STORE whose semantics
// survive without the memory traffic, skipping the instruction lets the
// worker finish its job and wake the waiter:
//
//   load (LDR/LDP/LDRB/...): destination register(s) := 0, pc += 4.
//   store (STR/STP/...):     the write is dropped, pc += 4.
//
// Soundness rules (all checked before touching state):
// - SIGSEGV/SIGBUS only. Anything else parks.
// - pc must sit in a registered guest module: host text is never rewritten.
// - Only plain transfers: unsigned/unscaled immediate, register-offset,
//   literal loads, signed-offset pairs, PRFM, whole-register SIMD structure
//   transfers with no offset (LD1/ST1 x1-4, LD2/ST2, LD3/ST3, LD4/ST4, LD1R),
//   and SIMD pairs with no writeback (STP/LDP s/d/q). Pre/post-index
//   (writeback), exclusives, LSE atomics, SIMD lane-indexed forms, and
//   everything else park —
//   faking a base update or a synchronisation primitive corrupts silently,
//   while a skipped plain transfer only loses one value.
// - The computed effective address must equal si_addr: a decode that does
//   not explain the fault is a mis-decode, and those park.
// - Encoding shapes verified against aarch64-linux-gnu-as output, not the
//   ARM ARM from memory: b86d784c ldr w12,[x2,x13,lsl#2] allows,
//   a9c10440/a8c10440 ldp pre/post-index refuse, b8200041 ldadd refuses via
//   the bits[11:10]==10 rule (every LSE form carries bit21==1 with
//   bits[11:10] in {0,1,3}).
// - All async-signal-safe: scalar reads/writes, no locks, no allocation.
namespace kudroid {

bool fault_skip_branches_through(uint32_t nextWord, unsigned reg) {
    // 0xD6/0xD7 group with bits[24:21] == 0 is exactly the branch-to-register
    // family: BR, BLR, RET, ERET, DRPS and their authenticated variants. One
    // mask covers all of them, so a new pointer-auth form cannot slip past.
    if ((nextWord & 0xFE000000u) != 0xD6000000u) return false;
    // ERET/DRPS encode XZR in Rn; a fabricated destination is never 31 (the
    // decoder refuses rt >= 29), so they can never match a real skip.
    return ((nextWord >> 5) & 31u) == reg;
}

FaultSkipPlan fault_decode_skip(uint32_t w, uint64_t pc, uint64_t baseVal,
                                uint64_t rmVal) {
    FaultSkipPlan p;
    const unsigned top = w >> 24;
    const unsigned rt = (w >> 0) & 31;
    const unsigned rm = (w >> 16) & 31;

    if (top == 0x58 || top == 0xD8) {
        // LDR literal (32/64-bit): pc-relative, no writeback.
        const int32_t imm19 =
            static_cast<int32_t>((w >> 5) & 0x7FFFFu) << 13 >> 13;
        p.effAddr = pc + static_cast<int64_t>(imm19) * 4;
        p.isLoad = true;
        p.rt = rt;
        p.skippable = true;
        return p;
    }
    if ((top & 0x3B) == 0x38 || (top & 0x3B) == 0x39) {
        // Structural exclusion, not assembler luck: every atomic/exclusive/LSE
        // form (LDAR/STLR/LDAXP/STLXP/LDADD/SWP/CAS/CASP/LDAPR) carries
        // bits[29:24] in {001000, 011100}, i.e. bit27==0, while this branch
        // requires bits29,28,27==1. The check below makes the invariant
        // explicit so a future mask edit cannot silently admit an RMW into
        // the load-zero/store-drop path (faking an atomic is silent
        // corruption, worse than the crash).
        const unsigned major = (w >> 24) & 0x3F;
        if (major == 0x08 || major == 0x1C) return p;  // exclusive/LSE, LDAPR
        // Single transfer. SIMD/FP lanes (bit26) share the encoding with
        // integer lanes: Rt names a vector register (zeroed in __ns), Rn
        // stays an integer base, and the addressing modes compute
        // identically. Only the data-size rule differs (opc bit23 set =
        // 128-bit Q register, else 8<<size). SIMD lane-indexed forms stay
        // refused — lanes, not whole registers, fault there and zeroing them
        // is not semantics-preserving. Whole-register structure and pair
        // forms are decoded in their own branches below.
        const bool simd = (w & (1u << 26)) != 0;
        // PRFM: refused, not skipped. It is a hint with no destination, so a
        // skip would be harmless — but a faulting PRFM means its address is
        // bad, and silently advancing hides a real problem the very next
        // instruction will trip on anyway with a full report.
        if (((w >> 23) & 0x1FF) == 0x1F3) {
            return p;
        }
        // LSE atomics never reach the bit21==0 path (assembler-verified:
        // every LSE single-transfer form carries bit21==1), so no opc guard
        // is needed here — and none could be both correct and complete.
        // (LSE is integer-only; the bits[11:10]==10 rule below excludes it
        // structurally for both lanes.)
        p.isLoad = ((w >> 22) & 1) != 0;
        p.isVector = simd;
        const unsigned opc = (w >> 22) & 3;
        const unsigned sizeLog = (w >> 30) & 3;
        // Integer: one scale. SIMD: Q (opc bit 2) is 16 bytes, otherwise
        // the lane width 8<<size. Assembler-verified across B/H/S/D/Q.
        const unsigned scale = simd ? ((opc & 2) ? 4 : sizeLog) : sizeLog;
        if ((w & (1u << 21)) != 0) {
            // Register offset: bits[11:10] must be 10. This one test excludes
            // every LSE atomic and exclusive (verified: all carry bit21==1
            // with bits[11:10] in {0,1,3}, never 2).
            if (((w >> 10) & 3) != 2) return p;
            if (rm > 30) return p;  // 31 is not a valid index register
            const unsigned option = (w >> 13) & 7;
            const unsigned amount = (w >> 12) & 1;
            uint64_t idx = 0;
            switch (option) {
                case 0x3: idx = rmVal; break;  // LSL
                case 0x2:                       // UXTW
                case 0x1: idx = rmVal & 0xFFFFFFFFu; break;
                case 0x6: idx = rmVal; break;  // UXTX
                case 0x7: idx = rmVal; break;  // SXTX
                case 0x5:                       // SXTW
                    idx = static_cast<uint64_t>(
                        static_cast<int64_t>(static_cast<int32_t>(rmVal & 0xFFFFFFFFu)));
                    break;
                default: return p;  // 0x0/0x4: not a plain index
            }
            const unsigned shift = amount ? scale : 0;
            p.effAddr = baseVal + (idx << shift);
        } else if (((w >> 24) & 1) != 0) {
            // Unsigned immediate: no writeback.
            const uint64_t imm12 = (w >> 10) & 0xFFFu;
            p.effAddr = baseVal + (imm12 << scale);
        } else {
            // 0x_8 group (bit24==0; the entry mask already excludes the
            // post/pre-index bit patterns, which live elsewhere): the low two
            // bits select the form. 00 = LDUR/STUR and 10 = LDTR/STTR are both
            // unscaled with no writeback (assembler-verified against capstone
            // across B/H/W/X). 01/11 are post/pre-index with a base writeback
            // that cannot be faked: refuse.
            const unsigned mode = (w >> 10) & 3;
            if (mode == 1 || mode == 3) return p;
            int32_t simm9 =
                static_cast<int32_t>(((w >> 12) & 0x1FFu) << 23) >> 23;
            p.effAddr = baseVal + static_cast<int64_t>(simm9);
        }
        p.rt = rt;
        p.skippable = true;
        return p;
    }
    if ((top & 0x3F) == 0x28 || (top & 0x3F) == 0x29 ||
        (top & 0x3F) == 0x2C || (top & 0x3F) == 0x2D) {
        // Integer pair, or SIMD&FP pair with bit26 set (STP/LDP s/d/q): two
        // explicit destinations, signed offset, no writeback. The vector
        // shape moves whole registers, so it zeroes like the integer pair.
        // Assembler-verified: ad7f5e55 ldp q21,q23,[x18,#-32] (live crash),
        // 2d408c02 ldp s2,s3,[x0,#4], 6d7f0c02 ldp d2,d3,[x0,#-16],
        // ad011404 stp q4,q5,[x0,#32], ad7e1fe6 ldp q6,q7,[sp,#-64].
        // Scale is opc+2 (S=4B, D=8B, Q=16B); opc==3 is unallocated.
        // Indexing lives in bits[24:23] verified against the assembler as
        // (w>>23)&3: 2 = signed offset, 3 = pre-index, 1 = post-index,
        // 0 = unallocated (ad808400 pre-index and acc10c02 post-index refuse).
        if (((w >> 23) & 3) != 2) return p;  // writeback/unallocated
        const bool simdPair = (w & (1u << 26)) != 0;
        unsigned scale;
        if (simdPair) {
            const unsigned opc = (w >> 30) & 3;
            if (opc == 3) return p;
            scale = opc + 2;
        } else {
            scale = ((w >> 31) & 1) ? 3 : 2;  // 64- vs 32-bit lanes
        }
        p.isPair = true;
        p.isLoad = ((w >> 22) & 1) != 0;
        p.rt = rt;
        p.rt2 = (w >> 10) & 31;
        p.isVector = simdPair;
        p.rtCount = 1;
        int32_t simm7 = static_cast<int32_t>(((w >> 15) & 0x7Fu) << 25) >> 25;
        p.effAddr = baseVal + (static_cast<int64_t>(simm7) << scale);
        p.skippable = true;
        return p;
    }
    if (top == 0x0C || top == 0x4C) {
        // AdvSIMD multiple structures, no offset (LD1/ST1 x1-4, LD2/ST2,
        // LD3/ST3, LD4/ST4): whole vector registers move, so a faulting one
        // zeroes (loads) or drops (stores) exactly like a scalar pair.
        // Assembler-verified: 0c008a5e st2 {v30.2s,v31.2s},[x18] (live
        // crash), 4c002800 st1 x4, 4c40a800 ld1 x2, 4c400800 ld4, 0c000800 st4.
        // Writeback forms carry the post-index register in bits[20:16]
        // (4cdf7800 ld1,[x0],#16 has Rm=31 there; 0c818800 st2,[x0],x1 has
        // Rm=1): a faked base update corrupts silently, so any nonzero there
        // refuses, same rule as scalar post/pre-index.
        if (((w >> 16) & 0x1F) != 0) return p;
        const unsigned opc = (w >> 12) & 0xF;
        unsigned n = 0;
        switch (opc) {
            case 0x7: n = 1; break;
            case 0xA: case 0x8: n = 2; break;
            case 0x6: case 0x4: n = 3; break;
            case 0x2: case 0x0: n = 4; break;
            default: return p;  // unallocated in the no-offset form
        }
        if (rt + n > 32) return p;  // register list must not wrap
        p.isLoad = ((w >> 22) & 1) != 0;
        p.isVector = true;
        p.rt = rt;
        p.rtCount = n;
        p.effAddr = baseVal;
        p.skippable = true;
        return p;
    }
    if (top == 0x0D || top == 0x4D) {
        // AdvSIMD single structure (LD1R plus one-lane LD1/ST1). Only LD1R
        // moves a whole register (replicated); lane forms touch part of one,
        // and zeroing the rest is not semantics-preserving, so they refuse.
        // LD1R-vs-lane verified against all 60 lane forms (every size, index,
        // LD+ST): none carries opcode 0xC, so it selects LD1R exactly.
        // Stores here are always lane forms (no ST1R exists): refuse.
        // Assembler-verified: 0d40c905 ld1r {v5.2s},[x8] allows; 4d408000
        // ld1 {v0.s}[2],[x0] and 4d004862 st1 {v2.h}[5],[x3] refuse.
        if (((w >> 22) & 1) == 0) return p;
        if (((w >> 12) & 0xF) != 0xC) return p;
        p.isLoad = true;
        p.isVector = true;
        p.rt = rt;
        p.rtCount = 1;
        p.effAddr = baseVal;
        p.skippable = true;
        return p;
    }
    return p;
}

}  // namespace kudroid

#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
namespace {

bool fault_skip_load_store(ucontext_t* uc, uintptr_t faultAddr, uint64_t* newPcOut,
                           bool* isLoadOut) {
    if (uc == nullptr || newPcOut == nullptr) return false;
    const uint64_t pc = uc->uc_mcontext->__ss.__pc;
    // Guest text only (see above). The lookup try-locks and gives up rather
    // than blocking, so it is safe here.
    char mod[256] = {0};
    if (!kudroid::kudroid_lookup_guest_module(reinterpret_cast<void*>(pc), mod,
                                              sizeof(mod))) {
        return false;
    }
    const uint32_t w = *reinterpret_cast<const uint32_t*>(pc);
    const unsigned rn = (w >> 5) & 31;

    auto regVal = [&](unsigned r) -> uint64_t {
        if (r == 31) return 0;
        if (r == 30) return arm_thread_state64_get_lr(uc->uc_mcontext->__ss);
        if (r == 29) return arm_thread_state64_get_fp(uc->uc_mcontext->__ss);
        if (r > 28) return 0;
        return uc->uc_mcontext->__ss.__x[r];
    };
    const uint64_t baseVal =
        (rn == 31) ? arm_thread_state64_get_sp(uc->uc_mcontext->__ss) : regVal(rn);
    const uint64_t rmVal = regVal((w >> 16) & 31);

    const kudroid::FaultSkipPlan p = kudroid::fault_decode_skip(w, pc, baseVal, rmVal);
    if (!p.skippable) return false;
    // The decode must explain the fault. Anything else is a mis-decode.
    // Null-page imprecision: a faulting multi-register SIMD access in the
    // first page may report si_addr as the page rather than the faulting byte
    // (observed: st2 [x18=0x1f8] reported as 0x0). Inside the first page the
    // exact byte carries no safety signal — nothing is mapped there — so
    // same-page equality suffices; everywhere else it stays exact.
    if (p.effAddr != faultAddr &&
        (p.effAddr >= 0x1000u || faultAddr >= 0x1000u)) return false;
    // The fabricated result must not become a control-flow target. When the
    // word right after the faulting load branches through the register this
    // skip would zero, resuming there calls address 0 instead: the thread hops
    // to pc=0 and the report names that, not the load whose base was wrong
    // (observed live on a null-base load followed by `blr x8`). Refuse the
    // skip so the fault is reported where it happened. pc+4 is fetchable
    // whenever it stays inside pc's page, and pc is executing right now.
    if (p.isLoad) {
        if ((pc & 0xFFFu) + 4 < 0x1000u) {
            const uint32_t next = *reinterpret_cast<const uint32_t*>(pc + 4);
            unsigned via = 32;  // >= 32: no register this skip fabricates
            if (kudroid::fault_skip_branches_through(next, p.rt)) {
                via = p.rt;
            } else if (p.isPair && kudroid::fault_skip_branches_through(next, p.rt2)) {
                via = p.rt2;
            } else if (p.isVector) {
                for (unsigned i = 1; i < p.rtCount; ++i) {
                    if (kudroid::fault_skip_branches_through(next, p.rt + i)) {
                        via = p.rt + i;
                        break;
                    }
                }
            }
            if (via < 32) {
                char nb[192];
                const int cn = std::snprintf(
                    nb, sizeof(nb),
                    "null-probe skip refused: next insn branches through r%u, the "
                    "register the skip would fabricate",
                    via);
                if (cn > 0) kudroid_persistent_breadcrumb(nb);
                return false;
            }
        }
    }
    // Reached only for nullish faults, so the fabricated result below lands
    // in a register whose only sane value a null read could have produced anyway. Storm control stays in the per-thread budget.
    if (p.isLoad) {
        if (p.isVector) {
            // SIMD/FP lane: all 32 vector registers exist (no XZR), so no
            // destination gate is needed. Zero, like the scalar case below: a
            // null-page read is the only fault that reaches here, so the value
            // the guest would have seen is zero bytes. A poison pattern is
            // worse, not better -- 0xCD bytes reinterpreted as four pointers
            // are four wild addresses. Structure loads zero every consecutive
            // destination (rtCount); a pair's second dest is explicit, not
            // rt+1 (observed: ldp q21,q23). The decoder already refused a
            // wrapping structure list; 5-bit pair fields cannot wrap.
            if (p.rt > 31 || p.rtCount < 1 || p.rtCount > 4 ||
                p.rt + p.rtCount > 32) return false;
            if (p.isPair && p.rt2 > 31) return false;
            for (unsigned i = 0; i < p.rtCount; ++i) {
                std::memset(&uc->uc_mcontext->__ns.__v[p.rt + i], 0,
                            sizeof(uc->uc_mcontext->__ns.__v[0]));
            }
            if (p.isPair) {
                std::memset(&uc->uc_mcontext->__ns.__v[p.rt2], 0,
                            sizeof(uc->uc_mcontext->__ns.__v[0]));
            }
        } else {
            // 29/30 never take a transfer result in valid code, 31 is XZR
            // (no effect): refuse rather than reason about them.
            if (p.rt >= 29 || (p.isPair && p.rt2 >= 29)) return false;
            // Zero, not a poison value: the register result feeds straight
            // into later pointer arithmetic, and a poison like 0xDEAD turned
            // "base + offset" into an unmapped address a few instructions
            // later (observed live: x12=0xdead then SIGBUS on x14 load in a
            // Unity Job.Worker). Zero is what the hardware faults read as on
            // a null page, which is the semantics a faulting load leaves
            // behind — an engine null-check then treats it as the missing
            // data it already handles everywhere else.
            uc->uc_mcontext->__ss.__x[p.rt] = 0;
            if (p.isPair) uc->uc_mcontext->__ss.__x[p.rt2] = 0;
        }
    }
    *newPcOut = pc + 4;
    if (isLoadOut != nullptr) *isLoadOut = p.isLoad;
    return true;
}

}  // namespace

static bool kudroid_try_skip_fault(int /*sig*/, siginfo_t* info, void* ucontext) {
    if (info == nullptr || ucontext == nullptr) return false;
    ucontext_t* uc = reinterpret_cast<ucontext_t*>(ucontext);
    uint64_t newPc = 0;
    bool isLoad = false;
    const uintptr_t faultAddr = reinterpret_cast<uintptr_t>(info->si_addr);
    // A fault may only be silently recovered when it looks like a null-page
    // probe: the guest dereferences a pointer whose null check it got wrong.
    // That is the one case where inventing a result cannot corrupt live state,
    // because nothing the guest owns lives below the first page — or just
    // below zero, for a null base with a negative structure offset. Anywhere
    // else the fault is a real bad access, and fabricating a load result feeds
    // garbage into the structures the guest is walking -- observed live as a
    // hash-table loop that stored a poisoned SIMD lane (0xCD) into a live
    // array, kept walking with the poisoned index and finally wrote 33GB off
    // the heap. Refuse those: a crash on the real address, with the region and
    // permissions logged, is diagnosable; thousands of silent corruptions are
    // not.
    if (!fault_addr_is_nullish(faultAddr)) return false;
    if (!fault_skip_load_store(uc, faultAddr, &newPc, &isLoad)) return false;
    arm_thread_state64_set_pc_fptr(uc->uc_mcontext->__ss,
                                   reinterpret_cast<void*>(newPc));
    char mark[256];
    const int n = snprintf(mark, sizeof(mark),
                           "fault-skipped fault_addr=0x%llx resumed_at_pc=0x%llx",
                           (unsigned long long)faultAddr,
                           (unsigned long long)newPc);
    if (n > 0) kudroid_persistent_breadcrumb(mark);
    // Recovered faults otherwise live only in native_breadcrumbs.log, so a storm that
    // eventually exhausts the per-thread budget looks like a single fatal fault in the
    // primary crash log (observed: 128 silent skips, then one fatal load in a Unity Job
    // worker). Mirror the first few and every 16th to kudroid_crash.log so the run-up
    // is visible where it is actually read. Async-signal-safe: open/write/close only.
    {
        static std::atomic<unsigned> s_skipLog{0};
        const unsigned seen = s_skipLog.fetch_add(1, std::memory_order_relaxed);
        if (seen < 8 || (seen % 16) == 0) {
            if (g_logDir[0]) {
                // Which mapping the fault came from decides the fix: a region
                // that exists with insufficient cur protection is a commit we
                // failed to make, while no region at all (or one whose max
                // protection cannot grant the access) is a wild address.
                char reg[160];
                {
                    char cur[4] = "???", max[4] = "???";
                    uint64_t rbase = 0, rsize = 0;
                    // A lookup that returns a region the address is not inside
                    // is not the fault's region: for an address below the first
                    // mapping the kernel answers with the first region in the
                    // space (observed: fault_addr=0x28 reported as a region at
                    // 0x100850000). Reporting that as "the region" reads as a
                    // committed-but-unprotected page and sends the next
                    // investigation the wrong way, so require containment.
                    if (kudroid::QueryRegionProt(
                            reinterpret_cast<const void*>(faultAddr), cur, max,
                            &rbase, &rsize) &&
                        faultAddr >= rbase && faultAddr < rbase + rsize) {
                        std::snprintf(reg, sizeof(reg),
                                      " region=0x%llx+0x%llx cur=%s max=%s",
                                      (unsigned long long)rbase,
                                      (unsigned long long)rsize, cur, max);
                    } else {
                        std::snprintf(reg, sizeof(reg), " region=<none>");
                    }
                }
                char rec[480];
                const int m = std::snprintf(rec, sizeof(rec),
                                            "[fault-skip #%u] %s kind=%s%s\n",
                                            seen + 1, mark,
                                            isLoad ? "load" : "store", reg);
                if (m > 0) {
                    char path[1200];
                    size_t dl = std::strlen(g_logDir);
                    if (dl >= sizeof(path) - 32) dl = sizeof(path) - 32;
                    std::memcpy(path, g_logDir, dl);
                    const char* suffix = "/kudroid_crash.log";
                    std::memcpy(path + dl, suffix, std::strlen(suffix) + 1);
                    const int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
                    if (fd >= 0) {
                        (void)!::write(fd, rec, static_cast<size_t>(m));
                        ::close(fd);
                    }
                }
            }
        }
    }
    return true;
}
#else
// Non-Apple or non-arm64 hosts never run guest AArch64 text in-process:
// every worker fault parks.
static bool kudroid_try_skip_fault(int, siginfo_t*, void*) { return false; }
#endif

// SIGUSR2 interrupter for the fatal-path park below. Empty on purpose: its job
// is turning the default-terminate disposition into EINTR, so a main thread
// blocked in the engine wait returns to its loop, where the shell poll sees
// g_hasCrashed and shows the modal. Safe in a handler by doing nothing.
static void kudroid_wake_handler(int) {}

static void crashHandler(int sig, siginfo_t* info, void* ucontext) {
    if (sig == SIGSYS && bionic_handle_guest_syscall_trap(ucontext)) {
        return;
    }
    // Diagnostic: which fatal signal actually arrives. Per-signal caps: TRAP
    // fires constantly (TLS emulation), the rest are rare.
    if (sig == SIGTRAP || sig == SIGILL || sig == SIGBUS || sig == SIGSEGV ||
        sig == SIGABRT) {
        static std::atomic<int> s_trapSeen{0};
        static std::atomic<int> s_fatalSeen{0};
        bool log = false;
        if (sig == SIGTRAP) {
            if (s_trapSeen.load() < 3) {
                ++s_trapSeen;
                log = true;
            }
        } else if (s_fatalSeen.load() < 8) {
            ++s_fatalSeen;
            log = true;
        }
        if (log) {
            char line[96];
            std::snprintf(line, sizeof(line), "fatal signal %d", sig);
            kudroid_android_log_message(4, "KuDroidTrap", line);
        }
    }
    if (sig == SIGTRAP) {
        if (kudroid::bionic_handle_tpidr_trap(ucontext)) {
            return; // handled successfully, resuming execution!
        }
        if (kudroid::bionic_handle_jit26_trap(ucontext)) {
            static std::atomic<int> s_jit26Seen{0};
            if (s_jit26Seen.load() < 5) {
                ++s_jit26Seen;
                kudroid_android_log_message(4, "KuDroidTrap",
                                            "JIT26 prepare/detach trap with no script; "
                                            "stepped over (unprepared)");
            }
            return;
        }
        static std::atomic<int> s_trapUnhandled{0};
        if (s_trapUnhandled.load() < 8) {
            ++s_trapUnhandled;
            kudroid_android_log_message(4, "KuDroidTrap", "SIGTRAP not emulated");
        }
        // A SIGTRAP that is not one of KuDroid's TLS breakpoints belongs to whoever
        // installed a handler before us — a debugger bridge, a crash reporter, or
        // LiveContainer's own dyld interception, all of which use breakpoints for
        // their own purposes. Reporting it as a KuDroid crash would both lose their
        // event and produce a misleading log.
        if (chainToPreviousHandler(sig)) {
            return;
        }
    }
    // The same TLS breakpoints can arrive as SIGILL (observed at teardown while
    // startup traps arrive as SIGTRAP). Pattern-gated like above: a genuine
    // illegal instruction never matches our BRK range and falls through to the
    // crash reporter untouched.
    if (sig == SIGILL) {
        if (kudroid::bionic_handle_tpidr_trap(ucontext)) {
            static std::atomic<int> s_illEmulated{0};
            if (s_illEmulated.load() < 5) {
                ++s_illEmulated;
                kudroid_android_log_message(4, "KuDroidTrap", "SIGILL emulated as TPIDR");
            }
            return;
        }
    }

    // Record the fault BEFORE anyone gets a chance to swallow it.
    //
    // Everything below this point can return without writing a thing: the guest's own
    // handler may report the fault true and resume, and the JNI_OnLoad shield jumps out
    // entirely. Both are correct behaviours and both used to leave no KuDroid-side trace
    // at all.
    //
    // That cost a full round. One run took a SIGSEGV at 0x64696f72646e6140 that only
    // exists in the log as libunity's own tombstone text, arriving through
    // __android_log_print with no thread, no KuDroid context and no way to place it in
    // the timeline — while KuDroid's crash log described a different, later fault and
    // presented it as the crash. One line here, in the same file as the watchdog and the
    // thread samples, is what makes the order of events readable.
    //
    // It carries pc/lr and the abort message for the same reason, learned the same way.
    // A guest handler that does not return reaches none of the reporting below: the
    // ULTRAKILL run that took sig=6 with guest_handler=1 wrote no crash log at all,
    // because il2cpp's SIGABRT handler siglongjmps out to its own recovery point. The
    // fault address for an abort() is just the pc of abort inside libsystem and says
    // nothing; pc, lr and the module they fall in are what identify the caller, and
    // android_set_abort_message is the guest's own statement of what went wrong. All of
    // it was already in memory and none of it was written anywhere.
    //
    // Everything here is async-signal-safe: reads of scalars, snprintf into a local,
    // kudroid_lookup_guest_module (a try_lock that gives up rather than blocking) and
    // one O_APPEND write.
    {
        char mark[1536];
        char tname[64] = "?";
#if defined(__APPLE__)
        pthread_getname_np(pthread_self(), tname, sizeof(tname));
#else
        (void)pthread_getname_np(pthread_self(), tname, sizeof(tname));
#endif
        // Signalled (raise/pthread_kill/sigqueue) vs faulted (hardware): si_code is the
        // only field that tells them apart, and which side decided to abort is exactly
        // what a SIGABRT with pc inside libsystem leaves ambiguous.
        int si_code_snap = 0;
        if (info != nullptr) si_code_snap = info->si_code;
        g_crash_signal_si_code = si_code_snap;

        // pc/lr, and the guest module they land in.
        //
        // Named pc_mod/lr_mod rather than symbolicated: symbolicateAddr falls through to
        // dladdr, which takes the dynamic linker's lock and is not safe here. The guest
        // module lookup is, and a module+offset is what actually needs decoding offline
        // anyway — a raw address alone cannot even be attributed to a library.
        uint64_t pc = 0, lr = 0;
        bool have_pc = false;
        char pc_mod[256] = {0};
        char lr_mod[256] = {0};
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (ucontext != nullptr) {
            const ucontext_t* uc = static_cast<const ucontext_t*>(ucontext);
            pc = uc->uc_mcontext->__ss.__pc;
            lr = uc->uc_mcontext->__ss.__lr;
            have_pc = true;
        }
#elif defined(__aarch64__) && defined(__linux__)
        if (ucontext != nullptr) {
            const ucontext_t* uc = static_cast<const ucontext_t*>(ucontext);
            pc = uc->uc_mcontext.pc;
            lr = uc->uc_mcontext.regs[30];
            have_pc = true;
        }
#endif
        if (have_pc) {
            if (!kudroid::kudroid_lookup_guest_module(reinterpret_cast<void*>(pc),
                                                     pc_mod, sizeof(pc_mod))) {
                snprintf(pc_mod, sizeof(pc_mod), "0x%llx (host or unknown)",
                         (unsigned long long)pc);
            }
            if (!kudroid::kudroid_lookup_guest_module(reinterpret_cast<void*>(lr),
                                                     lr_mod, sizeof(lr_mod))) {
                snprintf(lr_mod, sizeof(lr_mod), "0x%llx (host or unknown)",
                         (unsigned long long)lr);
            }
        }

        // The first GUEST frame above the fault, which for an abort() is the only thing
        // that identifies the caller.
        //
        // pc and lr are not enough and cannot be. The captured ULTRAKILL run reported
        // `pc=0x1da7b81dc (host or unknown) lr=0x213e07c1c (host or unknown)`: pc was
        // exactly the fault address, both were inside libsystem, and no frame belonged to
        // libil2cpp — because abort() and _sigtramp sit between the handler and whoever
        // decided to abort. Adding pc/lr answered the question by showing it was the
        // wrong question.
        //
        // Walking finds the frame that matters. Every step is bounded by this thread's
        // REAL stack — queried through pthread_get_stackaddr_np/get_stacksize_np rather
        // than a plausibility test — because a diagnostic that faults takes down the
        // process it was supposed to explain. That is not hypothetical: the run after this
        // one was added died inside the guard diagnostic, which still used a plausibility
        // test, at fault_addr=0x100000000050. FrameWalker is the shared implementation
        // both now use.
        char guest_frame[256] = {0};
        int guest_frame_depth = -1;
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (ucontext != nullptr) {
            const ucontext_t* uc = static_cast<const ucontext_t*>(ucontext);
            kudroid::FrameWalker walker(static_cast<uintptr_t>(uc->uc_mcontext->__ss.__fp),
                                        kudroid::query_thread_stack_bounds());
            for (int depth = 0; depth < 24 && walker.valid(); ++depth) {
                uintptr_t ret = 0;
                if (!walker.return_address(&ret)) break;
                if (kudroid::kudroid_lookup_guest_module(reinterpret_cast<void*>(ret),
                                                        guest_frame, sizeof(guest_frame))) {
                    guest_frame_depth = walker.depth();
                    break;
                }
                if (!walker.next()) break;
            }
        }
#endif

        // Field names are uniform with the rest of this file on purpose. `sig=` already
        // means a Java type signature in native-enter/native-exit — 76 lines of one run
        // used it that way against this line's one — so grepping `sig=` for signals
        // returns almost entirely the wrong records. `signo=` is unambiguous, and
        // `thread_id=` is the numeric id every other breadcrumb prints, which is what
        // makes this line joinable to the watchdog, the stall reports and the thread
        // samples instead of only readable next to them.
        int n = snprintf(mark, sizeof(mark),
                         "fatal-signal signo=%d si_code=%d fault_addr=%p thread=%s "
                         "thread_id=%llu guest_handler=%d jni_guard=%d",
                         sig,
                         si_code_snap,
                         info != nullptr ? info->si_addr : nullptr,
                         tname[0] != '\0' ? tname : "?",
                         currentThreadIdForCrash(),
                         kudroid::guest_signal_has_handler(sig) ? 1 : 0,
                         g_jniGuardActive ? 1 : 0);
        if (n > 0 && static_cast<size_t>(n) < sizeof(mark) && have_pc) {
            n += snprintf(mark + n, sizeof(mark) - static_cast<size_t>(n),
                          " pc=%s lr=%s", pc_mod, lr_mod);
        }
        // The guest frame, when the walk found one. `frame_depth` says how many host
        // frames sat between the signal and it, which is what distinguishes "the guest
        // faulted" (depth 0-1) from "the guest called into libsystem and that aborted"
        // (deeper) — the case pc/lr cannot describe at all.
        if (n > 0 && static_cast<size_t>(n) < sizeof(mark) && guest_frame_depth >= 0) {
            n += snprintf(mark + n, sizeof(mark) - static_cast<size_t>(n),
                          " guest_frame=%s frame_depth=%d", guest_frame, guest_frame_depth);
        } else if (n > 0 && static_cast<size_t>(n) < sizeof(mark) && have_pc) {
            n += snprintf(mark + n, sizeof(mark) - static_cast<size_t>(n),
                          " guest_frame=none-in-24-frames");
        }
        // The guest's own words, last, because it is the only variable-length part and
        // the fields above must not be pushed out by a long message.
        if (n > 0 && static_cast<size_t>(n) < sizeof(mark) && g_abortMessage[0] != '\0') {
            snprintf(mark + n, sizeof(mark) - static_cast<size_t>(n),
                     " abort_message=\"%.512s\"", g_abortMessage);
        }
        kudroid_persistent_breadcrumb(mark);
    }

    // Stop the watchdog timing this thread's work, HERE — at the one point every path
    // passes through.
    //
    // This used to sit further down, past the guest-handler dispatch and past the
    // JNI_OnLoad shield, and that was the same mistake twice over: those are exits, not
    // the entry. A guest handler that does not return — il2cpp's SIGABRT handler
    // siglongjmps out — skipped it entirely, so the last run reported
    // `native_call_id=14 native_elapsed_ms=9429` for nine seconds after UnityMain had
    // taken the signal, timing a call whose thread was gone. The `fatal-signal` line
    // above was written from the right place and was the only reason that was visible.
    //
    // The cost of being early is a false positive: a fault the guest genuinely fixes and
    // resumes from also stops the watchdog. That is the right trade — a stopped watchdog
    // says "stopped, reason=fatal-signal" and can be reasoned about, while a watchdog
    // reporting a dead thread's call as a live hang sends the reader somewhere else
    // entirely. Two relaxed stores, safe in a signal handler.
    kudroid::native_note_fatal_signal(sig,
                                      static_cast<unsigned long long>(currentThreadIdForCrash()));

    // For worker threads (e.g. Unity Job.Worker), try instruction load/store skip FIRST.
    // Unity's crash handler writes a tombstone without fixing memory or moving PC, which
    // aborts the thread or crashes the process. Skipping the faulting instruction lets
    // worker jobs safely finish without bringing down the game.
    {
#if defined(__APPLE__)
        const bool isHostMain = pthread_main_np() != 0;
#else
        const bool isHostMain =
            g_mainThread != 0 && pthread_equal(pthread_self(), g_mainThread);
#endif
        const unsigned long long tid = currentThreadIdForCrash();
        WorkerBudget* budget =
            (!kudroid_fault_is_fatal(tid, isHostMain) && (sig == SIGSEGV || sig == SIGBUS))
                ? worker_budget_for(tid)
                : nullptr;
        unsigned long long faultPc = 0;
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (ucontext != nullptr) {
            faultPc = static_cast<ucontext_t*>(ucontext)->uc_mcontext->__ss.__pc;
        }
#endif
        const unsigned long long faultAddr =
            info != nullptr ? reinterpret_cast<unsigned long long>(info->si_addr) : 0;
        // Emergency stop for a walked-off-the-cliff walk: past 512 steps of
        // the same load over uncommitted memory the walk is not recovering
        // anything — every skip poisons one destination and walks further into
        // the reservation. Retire the skip path here (budget reads spent, so
        // the second block parks the thread like any other exhausted worker)
        // instead of feeding the storm thousands more skips and a SIGBUS at
        // the cliff edge. Cap < kMaxWalkRecoveries keeps ordinary bounded
        // walks (which terminate) unaffected.
        static constexpr int kMaxSaneWalk = 512;
        if (budget != nullptr &&
            budget->walk.load(std::memory_order_relaxed) >= kMaxSaneWalk) {
            budget = nullptr;
        }
        // Distributed-storm gate: per-thread budgets cannot see a lockstep
        // fleet walking one load. Retire the skip path for this pc process-
        // wide once its global budget is spent, so the fault falls through to
        // the fatal path and the shell sees the crash instead of a soft-locked
        // engine that is silently corrupting itself.
        if (budget != nullptr && !pc_skip_peek(faultPc)) {
            budget = nullptr;
        }
        if (worker_budget_peek(budget, faultPc, faultAddr) &&
            kudroid_try_skip_fault(sig, info, ucontext)) {
            const long long nowNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
            worker_budget_note(budget, nowNs, faultPc, faultAddr);
            pc_skip_note(faultPc);
            return;
        }
    }

    // A SIGSEGV at pc=0 (or any unmapped low pc) after a null-probe skip means
    // the fabricated zero fed an indirect call: `blr xN` jumped to 0 and lr
    // points right after the call. Resuming is impossible (there is no
    // instruction to return to) and parking the thread without a report hides
    // the chain, so write the connection down and let the fatal path run —
    // the modal tells the user the skip caused the crash, not just "signal 11".
    // (Observed live, skip-tutorial: skip #1 fabricated x8=0; the very next
    // instruction was blr x8; crash pc=0x0, lr = skip_pc + 4, x8 = 0.)
    {
        unsigned long long crashPc = 0;
        unsigned long long crashLr = 0;
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (ucontext != nullptr) {
            crashPc = static_cast<ucontext_t*>(ucontext)->uc_mcontext->__ss.__pc;
            crashLr = static_cast<unsigned long long>(
                static_cast<ucontext_t*>(ucontext)->uc_mcontext->__ss.__lr);
        }
#endif
        if ((sig == SIGSEGV || sig == SIGBUS) && crashPc < 0x10000) {
            char mark[192];
            const int m = std::snprintf(
                mark, sizeof(mark),
                "null-call crash: pc=0x%llx lr=0x%llx — indirect call through a "
                "pointer a null-probe skip fabricated",
                crashPc, crashLr);
            if (m > 0) kudroid_persistent_breadcrumb(mark);
            // Ensure the report below is treated as fatal regardless of role.
            g_hasCrashed.store(true);
            g_lastCrashTail[0] = '\0';
        }
    }

    // The guest's own handler, for the signals KuDroid must keep installed.
    //
    // Gate on the faulting pc being in a registered guest module. A fault in host
    // text (the shell itself, KuART) is a KuDroid bug and must go down the fatal
    // path below: handing it to the guest reporter makes the guest write a
    // tombstone for OUR bug and resume into the same fault — the screen freezes
    // with the crash logged but no modal, exactly the invisible-crash report.
    // (Observed: SIGSEGV in kuart::DexClass::IsSubClassOf while the guest had a
    // handler installed; the host fault never reached kudroid_crash handling.)
    {
        void* faultPcHost = nullptr;
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (ucontext != nullptr) {
            faultPcHost = reinterpret_cast<void*>(
                static_cast<ucontext_t*>(ucontext)->uc_mcontext->__ss.__pc);
        }
#endif
        char modBuf[128];
        const bool pcInGuest = faultPcHost != nullptr &&
                               kudroid::kudroid_lookup_guest_module(faultPcHost, modBuf,
                                                                    sizeof(modBuf));
        if (!pcInGuest) {
            kudroid_persistent_breadcrumb(
                "host-pc fault: guest handler skipped, taking fatal path");
            goto host_fatal_path;
        }
    }
    if (kudroid::guest_signal_dispatch(sig, info, ucontext)) {
        // The guest's own crash reporter (Unity's tombstone) took the signal and
        // resumed, so the fatal path below never runs and kudroid_crash.log stays
        // empty — yet the app may be dying inside (frozen screen, dead input).
        // Leave a one-line record so a guest-handled fault is not invisible.
        if (sig == SIGSEGV || sig == SIGABRT || sig == SIGBUS) {
            static std::atomic<int> s_guestHandled{0};
            if (s_guestHandled.fetch_add(1, std::memory_order_relaxed) < 8) {
                char note[512];
                const uintptr_t fa =
                    info != nullptr ? reinterpret_cast<uintptr_t>(info->si_addr) : 0;
                int n = std::snprintf(note, sizeof(note),
                                      "guest-handler resolved fatal sig=%d fault=0x%llx\n",
                                      sig, static_cast<unsigned long long>(fa));
                if (n > 0) {
                    kudroid_persistent_breadcrumb(note);
                    // Also append to kudroid_crash.log itself.
                    char path[1200];
                    size_t dl = std::strlen(g_logDir);
                    if (dl >= sizeof(path) - 32) dl = sizeof(path) - 32;
                    std::memcpy(path, g_logDir, dl);
                    std::memcpy(path + dl, "/kudroid_crash.log", 19);
                    const int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
                    if (fd >= 0) {
                        (void)!::write(fd, note, static_cast<size_t>(n));
                        ::close(fd);
                    }
                }
            }
        }
        kudroid_persistent_breadcrumb("fatal-signal resolved-by-guest-handler");
        // The guest handled it, but the faulting thread rarely continues usefully
        // (it resumed into the same fault last time this path ran). The shell's
        // 0.25s poll reads g_hasCrashed to show the modal and run teardown, so
        // a guest-handled fatal must not stay invisible.
        if (sig == SIGSEGV || sig == SIGABRT || sig == SIGBUS) {
            g_hasCrashed.store(true);
            g_lastCrashTail[0] = '\0';
            extractLastLines(g_crashBuf, (size_t)g_crashLen, g_lastCrashTail,
                             sizeof(g_lastCrashTail), 30);
        }
        return;
    }

host_fatal_path:;

    // Inside a guarded JNI_OnLoad invocation: if this library aborts/segfaults,
    // skip it rather than killing the entire process. Only async-signal-safe calls used here;
    // logging deferred to caller upon siglongjmp return.
    //
    // Capture the machine state first. This path never reaches the crash-log code
    // below, so without this a swallowed fault left no pc, no fault address and no
    // stack — only a WARNING line saying a signal happened. Copying scalars is
    // async-signal-safe; formatting and writing are left to the caller.
    if (g_jniGuardActive && (sig == SIGABRT || sig == SIGSEGV || sig == SIGBUS ||
                             sig == SIGILL  || sig == SIGTRAP)) {
        JniGuardFault& f = g_jniGuardFault;
        f.signal = sig;
        if (info != nullptr) {
            f.si_code = info->si_code;
            f.fault_addr = info->si_addr;
        }
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (ucontext != nullptr) {
            ucontext_t* uc = static_cast<ucontext_t*>(ucontext);
            f.pc = uc->uc_mcontext->__ss.__pc;
            f.lr = uc->uc_mcontext->__ss.__lr;
            f.sp = uc->uc_mcontext->__ss.__sp;
            f.fp = uc->uc_mcontext->__ss.__fp;
            for (int i = 0; i < 9; ++i) f.x[i] = uc->uc_mcontext->__ss.__x[i];
            // Copy the stack words only when sp is genuinely inside this thread's stack.
            //
            // This used to test `sp > 0x1000 && sp < 0x7fffffffffff`, which excludes null
            // and nothing else — the same plausibility test that let the guard diagnostic
            // dereference 0x100000000018 and turned a diagnostic into the crash. Here the
            // consequence would be worse still: a double fault inside the signal handler,
            // which is precisely what the JNI_OnLoad guard exists to avoid.
            {
                const kudroid::StackBounds bounds = kudroid::query_thread_stack_bounds();
                const uintptr_t sp = static_cast<uintptr_t>(f.sp);
                const size_t want = sizeof(f.stack);
                if (bounds.valid && (sp & 0x7) == 0 && sp >= bounds.low &&
                    sp + want <= bounds.high) {
                    const uint64_t* stack = reinterpret_cast<const uint64_t*>(sp);
                    for (int i = 0; i < 32; ++i) f.stack[i] = stack[i];
                }
            }
            f.have_regs = true;
        }
#else
        (void)ucontext;
#endif
        g_jniGuardActive = 0;
        g_jniGuardSignal = sig;
        siglongjmp(g_jniGuardJmp, 1);
    }

    // Fast path for recoverable worker faults, BEFORE the crash dump below.
    //
    // A recovered fault killed nothing, but the dump path writes ~256KB per
    // fault (full report + log buffer + stderr tail): 16 recovered faults
    // produced a 4.7MB crash log in 25ms and saturated I/O for no diagnostic
    // gain. A skipped fault gets one breadcrumb and resumes; only faults
    // that actually park or crash pay for the full report.
    //
    // Placement is deliberate: after guest dispatch (Unity gets first
    // refusal) and after the JNI-OnLoad guard (which longjmps out when
    // active), but before fflush and the kudroid_crash.log dump. The
    // watchdog stop above already ran and stays — a skipped fault still
    // interrupted the call it is measured against.
    {
#if defined(__APPLE__)
        const bool isHostMain = pthread_main_np() != 0;
#else
        const bool isHostMain =
            g_mainThread != 0 && pthread_equal(pthread_self(), g_mainThread);
#endif
        const unsigned long long tid = currentThreadIdForCrash();
        WorkerBudget* budget =
            (!kudroid_fault_is_fatal(tid, isHostMain) && (sig == SIGSEGV || sig == SIGBUS))
                ? worker_budget_for(tid)
                : nullptr;
        unsigned long long faultPc = 0;
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (ucontext != nullptr) {
            faultPc = static_cast<ucontext_t*>(ucontext)->uc_mcontext->__ss.__pc;
        }
#endif
        const unsigned long long faultAddr =
            info != nullptr ? reinterpret_cast<unsigned long long>(info->si_addr) : 0;
        if (worker_budget_peek(budget, faultPc, faultAddr) &&
            kudroid_try_skip_fault(sig, info, ucontext)) {
            // Per-thread progress check lives in worker_budget_note: a 30s+
            // gap since this thread's previous skip resets its budget, so a
            // long session does not die on an isolated fault #129. A storm
            // keeps its count and stays fatal.
            const long long nowNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
            worker_budget_note(budget, nowNs, faultPc, faultAddr);
            return;
        }
    }

    if (g_logDir[0]) {
        // construct '<dir>/kudroid_crash.log' path without heap allocation.
        char path[1200];
        size_t dl = strlen(g_logDir);
        if (dl >= sizeof(path) - 32) dl = sizeof(path) - 32;
        memcpy(path, g_logDir, dl);
        const char* suffix = "/kudroid_crash.log";
        memcpy(path + dl, suffix, strlen(suffix) + 1);

        int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            const char* hdr = "[kudroid_core] CRASH — fatal signal caught\n";
            (void)!write(fd, hdr, strlen(hdr));
            const char* stamp = kudroid_build_stamp();
            char stampLine[512];
            int sm = snprintf(stampLine, sizeof(stampLine), "build: %s\n", stamp);
            crashWriteLine(fd, stampLine, sm, sizeof(stampLine));
            char sigline[2048];
            int m = snprintf(sigline, sizeof(sigline), "signal = %d\n", sig);
            crashWriteLine(fd, sigline, m, sizeof(sigline));

            // Record faulting thread name for diagnostic context.
            {
                char tname[64] = "?";
#if defined(__APPLE__)
                pthread_getname_np(pthread_self(), tname, sizeof(tname));
#else
                (void)pthread_getname_np(pthread_self(), tname, sizeof(tname));
#endif
                m = snprintf(sigline, sizeof(sigline), "thread = %s (0x%llx)\n",
                             tname[0] ? tname : "?",
                             (unsigned long long)(uintptr_t)pthread_self());
                crashWriteLine(fd, sigline, m, sizeof(sigline));
            }

            // log faulting memory address
            if (info) {
                m = snprintf(sigline, sizeof(sigline),
                    "fault_addr = %p\nsi_code = %d\n",
                    info->si_addr, info->si_code);
                crashWriteLine(fd, sigline, m, sizeof(sigline));
                {
                    // Region and permissions of the faulting address: separates
                    // "mapped but not committed with this access" from "no such
                    // mapping" without needing a live debugger.
                    char cur[4] = "???", max[4] = "???";
                    uint64_t rbase = 0, rsize = 0;
                    if (kudroid::QueryRegionProt(info->si_addr, cur, max,
                                                 &rbase, &rsize)) {
                        m = snprintf(sigline, sizeof(sigline),
                            "fault_region = 0x%llx+0x%llx cur=%s max=%s\n",
                            (unsigned long long)rbase,
                            (unsigned long long)rsize, cur, max);
                    } else {
                        m = snprintf(sigline, sizeof(sigline),
                                     "fault_region = <none>\n");
                    }
                    crashWriteLine(fd, sigline, m, sizeof(sigline));
                }
            }

#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
            if (ucontext && info) {
                ucontext_t* uc = reinterpret_cast<ucontext_t*>(ucontext);
                const uint64_t pc = uc->uc_mcontext->__ss.__pc;
                char mod[256] = {0};
                if (!kudroid::kudroid_lookup_guest_module(reinterpret_cast<void*>(pc), mod, sizeof(mod))) {
                    m = snprintf(sigline, sizeof(sigline), "fault_skip_diag: pc not in guest module\n");
                } else if ((pc & 3) != 0) {
                    m = snprintf(sigline, sizeof(sigline), "fault_skip_diag: unaligned pc\n");
                } else {
                    const uint32_t w = *reinterpret_cast<const uint32_t*>(pc);
                    auto regVal = [&](unsigned r) -> uint64_t {
                        if (r == 31) return 0;
                        if (r == 30) return arm_thread_state64_get_lr(uc->uc_mcontext->__ss);
                        if (r == 29) return arm_thread_state64_get_fp(uc->uc_mcontext->__ss);
                        if (r > 28) return 0;
                        return uc->uc_mcontext->__ss.__x[r];
                    };
                    const unsigned rn = (w >> 5) & 31;
                    const uint64_t baseVal = (rn == 31) ? arm_thread_state64_get_sp(uc->uc_mcontext->__ss) : regVal(rn);
                    const uint64_t rmVal = regVal((w >> 16) & 31);
                    const kudroid::FaultSkipPlan p = kudroid::fault_decode_skip(w, pc, baseVal, rmVal);
                    if (!p.skippable) {
                        m = snprintf(sigline, sizeof(sigline), "fault_skip_diag: instruction 0x%08x not skippable\n", w);
                    } else if (p.effAddr != reinterpret_cast<uintptr_t>(info->si_addr)) {
                        m = snprintf(sigline, sizeof(sigline), "fault_skip_diag: effAddr 0x%llx != si_addr %p\n",
                                     (unsigned long long)p.effAddr, info->si_addr);
                    } else {
                        m = snprintf(sigline, sizeof(sigline), "fault_skip_diag: skippable plan ok (fatal thread or budget cap)\n");
                    }
                }
                crashWriteLine(fd, sigline, m, sizeof(sigline));
            }
#endif

            // in thanh ghi pc (arm64)
#if defined(__aarch64__) || defined(__arm64__)
            if (ucontext) {
#if defined(__APPLE__)
                ucontext_t* uc = (ucontext_t*)ucontext;
                uint64_t pc = uc->uc_mcontext->__ss.__pc;
                uint64_t lr = uc->uc_mcontext->__ss.__lr;
                uint64_t sp = uc->uc_mcontext->__ss.__sp;
                uint64_t fp = uc->uc_mcontext->__ss.__fp;
                uint64_t cpsr = uc->uc_mcontext->__ss.__cpsr;
                m = snprintf(sigline, sizeof(sigline),
                    "pc = 0x%llx\nlr = 0x%llx\nsp = 0x%llx\nfp = 0x%llx\ncpsr = 0x%llx\n",
                    (unsigned long long)pc, (unsigned long long)lr, (unsigned long long)sp,
                    (unsigned long long)fp, (unsigned long long)cpsr);
                crashWriteLine(fd, sigline, m, sizeof(sigline));

                // All 29 general-purpose registers, not the first nine.
                //
                // x0-x8 alone cannot identify the base register of a faulting access, and
                // that is the one thing a BUS_ADRALN or a bad load needs: the last round
                // reported fault_addr=0x16be80148 with the pointer it came from sitting in
                // x19 and x23, neither of which was printed. The tombstone libunity writes
                // to logcat has all of them, so the two logs described the same fault at
                // different levels of detail and the fuller one was the guest's.
                //
                // Four per line to keep the log readable and the snprintf well inside its
                // buffer.
                for (int r = 0; r < 29; r += 4) {
                    char regbuf[256];
                    int rn = 0;
                    if (r + 3 < 29) {
                        rn = snprintf(regbuf, sizeof(regbuf),
                                      "x%-2d = 0x%016llx  x%-2d = 0x%016llx  "
                                      "x%-2d = 0x%016llx  x%-2d = 0x%016llx\n",
                                      r,     (unsigned long long)uc->uc_mcontext->__ss.__x[r],
                                      r + 1, (unsigned long long)uc->uc_mcontext->__ss.__x[r + 1],
                                      r + 2, (unsigned long long)uc->uc_mcontext->__ss.__x[r + 2],
                                      r + 3, (unsigned long long)uc->uc_mcontext->__ss.__x[r + 3]);
                    } else {
                        rn = snprintf(regbuf, sizeof(regbuf), "x%-2d = 0x%016llx\n",
                                      r, (unsigned long long)uc->uc_mcontext->__ss.__x[r]);
                    }
                    crashWriteLine(fd, regbuf, rn, sizeof(regbuf));
                }

                // The faulting instruction itself.
                //
                // For SIGBUS/BUS_ADRALN the instruction is the whole answer and the
                // registers are only supporting evidence: the encoding names the base
                // register, the offset and the access width, which is what says whether the
                // pointer was wrong or the access was wider than the pointer's alignment.
                // Without it a misaligned 16-byte LDP and a misaligned 8-byte LDR look
                // identical in the log, and they have different causes.
                //
                // Reading at pc is safe — it was just executed, so the page is mapped and
                // readable. The neighbours are only read when they share pc's page, which
                // keeps a diagnostic from becoming a second fault at a page boundary.
                {
                    const uint64_t page = pc & ~static_cast<uint64_t>(0xFFF);
                    const uint32_t* at_pc = reinterpret_cast<const uint32_t*>(pc);
                    m = snprintf(sigline, sizeof(sigline),
                                 "\n--- instruction at pc ---\ninst = 0x%08x\n",
                                 (pc & 3) == 0 ? *at_pc : 0u);
                    crashWriteLine(fd, sigline, m, sizeof(sigline));
                    for (int d = -2; d <= 2; ++d) {
                        const uint64_t a = pc + static_cast<uint64_t>(d * 4);
                        if ((a & ~static_cast<uint64_t>(0xFFF)) != page) continue;
                        if ((a & 3) != 0) continue;
                        int n = snprintf(sigline, sizeof(sigline), "  %s0x%llx: 0x%08x\n",
                                         d == 0 ? "-> " : "   ",
                                         (unsigned long long)a,
                                         *reinterpret_cast<const uint32_t*>(a));
                        crashWriteLine(fd, sigline, n, sizeof(sigline));
                    }
                }

                // Where the fault address sits relative to this thread's stack.
                //
                // Last round's fault_addr was 20808 bytes ABOVE the value of
                // pthread_self() and the log could not say so, which left "stack overflow"
                // and "wrong pointer" equally consistent with the evidence. Printing the
                // bounds and the verdict answers it in the crash log instead of by hand.
                {
                    char* const stack_top =
                        static_cast<char*>(pthread_get_stackaddr_np(pthread_self()));
                    const size_t stack_size = pthread_get_stacksize_np(pthread_self());
                    char* const stack_low = stack_top - stack_size;
                    const char* fa = info != nullptr
                                         ? static_cast<const char*>(info->si_addr)
                                         : nullptr;
                    const char* verdict = "no fault address";
                    if (fa != nullptr) {
                        if (fa >= stack_low && fa < stack_top) verdict = "inside this thread's stack";
                        else if (fa >= stack_top)              verdict = "ABOVE the top of this thread's stack";
                        else if (fa >= stack_low - 0x10000)    verdict = "just BELOW the stack (guard page / overflow)";
                        else                                    verdict = "not on this thread's stack";
                    }
                    m = snprintf(sigline, sizeof(sigline),
                                 "\n--- stack bounds ---\nstack = [0x%llx, 0x%llx) size=%llu\n"
                                 "fault_addr is %s\n",
                                 (unsigned long long)(uintptr_t)stack_low,
                                 (unsigned long long)(uintptr_t)stack_top,
                                 (unsigned long long)stack_size, verdict);
                    crashWriteLine(fd, sigline, m, sizeof(sigline));
                }

                // Symbolicate PC/LR for meaningful source/function attribution.
                char symPc[512], symLr[512];
                symbolicateAddr((uintptr_t)pc, symPc, sizeof(symPc));
                symbolicateAddr((uintptr_t)lr, symLr, sizeof(symLr));
                m = snprintf(sigline, sizeof(sigline), "pc_sym: %s\nlr_sym: %s\n", symPc, symLr);
                crashWriteLine(fd, sigline, m, sizeof(sigline));

                // Raw stack dump from faulting SP to recover caller frames and register state.
                //
                // Bounded by the thread's real stack, and clamped rather than skipped: a
                // fault near the top of the stack still has useful words below sp, and
                // refusing the whole dump would lose them. This read had NO check at all —
                // 1024 bytes from whatever sp happened to be — which in a crash handler
                // means a double fault that destroys the report for the original crash.
                (void)!write(fd, "\n--- stack from sp ---\n", 22);
                {
                    const kudroid::StackBounds bounds = kudroid::query_thread_stack_bounds();
                    const uintptr_t spv = static_cast<uintptr_t>(sp);
                    int words = 0;
                    if (bounds.valid && (spv & 0x7) == 0 && spv >= bounds.low &&
                        spv < bounds.high) {
                        const uintptr_t available = bounds.high - spv;
                        words = static_cast<int>(available / sizeof(uint64_t));
                        if (words > 128) words = 128;
                        words &= ~3;  // whole rows of four, which is how it is printed
                    }
                    if (words == 0) {
                        int n0 = snprintf(sigline, sizeof(sigline),
                                          "sp=0x%llx is outside this thread's stack "
                                          "[0x%llx, 0x%llx) — not dumped\n",
                                          (unsigned long long)spv,
                                          (unsigned long long)bounds.low,
                                          (unsigned long long)bounds.high);
                        crashWriteLine(fd, sigline, n0, sizeof(sigline));
                    }
                    const uint64_t* stack = reinterpret_cast<const uint64_t*>(spv);
                    for (int i = 0; i < words; i += 4) {
                        int n = snprintf(sigline, sizeof(sigline),
                            "sp%+04d: %016llx  %016llx  %016llx  %016llx\n",
                            i * 8,
                            (unsigned long long)stack[i],
                            (unsigned long long)stack[i + 1],
                            (unsigned long long)stack[i + 2],
                            (unsigned long long)stack[i + 3]);
                        crashWriteLine(fd, sigline, n, sizeof(sigline));
                    }
                }

                // Walk frame chain from faulting FP (ucontext) to locate __cxa_guard_acquire frame:
                // [fp+8] = saved LR (caller), [fp+56] = x19 = guard pointer.
                // slot56 (guard) — ch dump raw, decode offline.
                //
                // Through FrameWalker, bounded by this thread's real stack. The previous
                // version read p[0], p[1] and p[7] straight off any fp that passed
                // `> 0x1000 && < 0x7fffffffffff`, which is the same unchecked read that
                // crashed the guard diagnostic — and here it would fault INSIDE the crash
                // handler, losing the report for the original fault entirely.
                //
                // Bounds come from the faulting thread, which is this one: the handler
                // runs on the thread that took the signal.
                (void)!write(fd, "\n--- fp chain ---\n", 17);
                {
                    const kudroid::StackBounds bounds = kudroid::query_thread_stack_bounds();
                    kudroid::FrameWalker walker(static_cast<uintptr_t>(fp), bounds);
                    if (!walker.valid()) {
                        int n0 = snprintf(sigline, sizeof(sigline),
                                          "fp=0x%llx is outside this thread's stack "
                                          "[0x%llx, 0x%llx) — no chain to walk\n",
                                          (unsigned long long)fp,
                                          (unsigned long long)bounds.low,
                                          (unsigned long long)bounds.high);
                        crashWriteLine(fd, sigline, n0, sizeof(sigline));
                    }
                    for (int i = 0; i < 32 && walker.valid(); ++i) {
                        uintptr_t savedLr = 0;
                        const bool haveLr = walker.return_address(&savedLr);
                        uintptr_t slot56 = 0;
                        const bool haveSlot = walker.slot(56, &slot56);

                        char lrMod[256] = {0};
                        const bool inGuest =
                            haveLr && kudroid::kudroid_lookup_guest_module(
                                          reinterpret_cast<void*>(savedLr), lrMod, sizeof(lrMod));
                        // When not in a guest ELF it is host code; symbolicate it so aborts name the function.
                        char lrSym[512] = {0};
                        if (haveLr && !inGuest) {
                            symbolicateAddr(savedLr, lrSym, sizeof(lrSym));
                        }
                        const bool guardLike =
                            haveSlot && slot56 > 0x100000000ULL && slot56 < 0x7fffffffffffULL;
                        int n2 = snprintf(sigline, sizeof(sigline),
                            "fp%02d: f=0x%llx lr=0x%llx %s slot56=0x%llx%s\n",
                            i, (unsigned long long)walker.fp(),
                            (unsigned long long)savedLr,
                            haveLr ? (inGuest ? lrMod : lrSym) : "(no return address)",
                            (unsigned long long)slot56,
                            guardLike ? " <-- guard?" : (haveSlot ? "" : " (slot56 out of bounds)"));
                        crashWriteLine(fd, sigline, n2, sizeof(sigline));
                        if (!haveLr) break;
                        if (!walker.next()) break;
                    }
                }
#elif defined(__linux__)
                ucontext_t* uc = (ucontext_t*)ucontext;
                uint64_t pc = uc->uc_mcontext.pc;
                uint64_t lr = uc->uc_mcontext.regs[30];
                m = snprintf(sigline, sizeof(sigline),
                    "pc = 0x%llx\nlr = 0x%llx\n",
                    (unsigned long long)pc, (unsigned long long)lr);
                crashWriteLine(fd, sigline, m, sizeof(sigline));

                char symPc[512], symLr[512];
                symbolicateAddr((uintptr_t)pc, symPc, sizeof(symPc));
                symbolicateAddr((uintptr_t)lr, symLr, sizeof(symLr));
                m = snprintf(sigline, sizeof(sigline), "pc_sym: %s\nlr_sym: %s\n", symPc, symLr);
                crashWriteLine(fd, sigline, m, sizeof(sigline));
#endif
            }
#endif

            (void)!write(fd, "\n--- backtrace ---\n", 19);
            writeBacktrace(fd);

            if (g_abortMessage[0]) {
                (void)!write(fd, "\n--- abort message ---\n", 23);
                (void)!write(fd, g_abortMessage, strlen(g_abortMessage));
                (void)!write(fd, "\n", 1);
            }

            (void)!write(fd, "\n--- log up to crash ---\n", 25);
            (void)!write(fd, g_crashBuf, (size_t)g_crashLen);

            // Also dump the stderr tail (KuART abort reasons go there); all calls are async-signal-safe.
            {
                char errPath[1200];
                size_t dl = strlen(g_logDir);
                if (dl < sizeof(errPath) - 32) {
                    memcpy(errPath, g_logDir, dl);
                    memcpy(errPath + dl, "/stderr.log", 12);
                    int errFd = open(errPath, O_RDONLY);
                    if (errFd >= 0) {
                        const char* stderrHdr = "\n--- stderr tail (kuart abort reason) ---\n";
                        (void)!write(fd, stderrHdr, strlen(stderrHdr));
                        off_t errSize = lseek(errFd, 0, SEEK_END);
                        const off_t maxTail = 4096;
                        if (errSize > maxTail) {
                            lseek(errFd, -maxTail, SEEK_END);
                        } else {
                            lseek(errFd, 0, SEEK_SET);
                        }
                        char errBuf[512];
                        ssize_t errN;
                        while ((errN = read(errFd, errBuf, sizeof(errBuf))) > 0) {
                            (void)!write(fd, errBuf, (size_t)errN);
                        }
                        (void)!write(fd, "\n", 1);
                        close(errFd);
                    }
                }
            }

            const char* traceStr = kudroid::bionic_shim_trace();
            if (traceStr && *traceStr) {
                (void)!write(fd, "\n--- bionic shim trace ---\n", 27);
                (void)!write(fd, traceStr, strlen(traceStr));
            }

            close(fd);
        }
    }

    // Mark crashed state and keep the last 30 log lines for the Swift warning.
    //
    // Reaching here means the fault was NOT recovered by the fast path above
    // (fatal thread, over budget, unskippable instruction, or a non-memory
    // signal): the full report above was worth writing. Fatal threads stop
    // the app; anything else parks with a warning breadcrumb.
    {
#if defined(__APPLE__)
        const bool isHostMain = pthread_main_np() != 0;
#else
        const bool isHostMain =
            g_mainThread != 0 && pthread_equal(pthread_self(), g_mainThread);
#endif
        const unsigned long long tid = currentThreadIdForCrash();
        // Fatal threads never consume a budget slot; workers look theirs up
        // (claiming if new). No slot left fails closed: spent reads as max.
        const bool roleFatal = kudroid_fault_is_fatal(tid, isHostMain);
        WorkerBudget* budget = roleFatal ? nullptr : worker_budget_for(tid);
        const int spent =
            budget != nullptr ? budget->count.load(std::memory_order_relaxed) : kMaxWorkerRecoveries;
        // Walk storm = fatal. The walk budget lets one bounded table walk live
        // thousands of skips long, but a walk that far exceeds any plausible
        // relocation table has a broken source pointer (observed live: 4000+
        // skips at one pc over an uncommitted 0x392000000 region, faulting
        // every 0x80 bytes) — report it as a crash so the shell shows the
        // modal instead of leaving a dead worker inside a running app.
        const int walked = budget != nullptr ? budget->walk.load(std::memory_order_relaxed) : 0;
        // A crash whose pc sits in the first page is an indirect call through a
        // null pointer — with lr pointing right after a `blr`, that call was
        // fed by a fabricated zero from the null-probe skip above. The thread
        // cannot resume (there is nothing to return to), so a parked-worker
        // budget is meaningless: report the crash so the modal appears and the
        // teardown runs instead of freezing the game behind a dead worker.
        unsigned long long fatalPc = 0;
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (ucontext != nullptr) {
            fatalPc = static_cast<ucontext_t*>(ucontext)->uc_mcontext->__ss.__pc;
        }
#endif
        const bool nullCall =
            (sig == SIGSEGV || sig == SIGBUS) && fatalPc != 0 && fatalPc < 0x10000;
        const bool fatal = roleFatal || spent >= kMaxWorkerRecoveries ||
                           walked >= kMaxWalkRecoveries || nullCall;
        if (fatal) {
            g_hasCrashed.store(true);
            g_lastCrashTail[0] = '\0';  // filled below from g_crashBuf
        } else {
            // The parked worker below never runs again: the engine deadlocks the
            // first time it synchronises on this thread's fence (observed: frames
            // keep swapping for two minutes, then the screen freezes with three
            // more threads dead and no modal). A parked thread is a dead thread —
            // the session is over, so report it as a crash instead of timing it.
            g_hasCrashed.store(true);
            g_lastCrashTail[0] = '\0';
            if (budget != nullptr) budget->count.fetch_add(1, std::memory_order_relaxed);
            char mark[256];
            const int n = snprintf(
                mark, sizeof(mark),
                "worker-fault-isolated signo=%d thread_id=%llu "
                "faults_so_far=%d (parked; session reported as crashed)",
                sig, tid, spent + 1);
            if (n > 0) kudroid_persistent_breadcrumb(mark);
        }
    }

    // Collect the last 30 log lines.
    extractLastLines(g_crashBuf, (size_t)g_crashLen, g_lastCrashTail, sizeof(g_lastCrashTail), 30);

    // If the tail is too short, append signal and PC.
    if (strlen(g_lastCrashTail) < 30) {
        char fallbackSummary[512];
        snprintf(fallbackSummary, sizeof(fallbackSummary),
                 "[Crash Signal: %d] Fault at %p (Thread 0x%llx)",
                 sig, info ? info->si_addr : nullptr, (unsigned long long)(uintptr_t)pthread_self());
        strncat(g_lastCrashTail, fallbackSummary, sizeof(g_lastCrashTail) - strlen(g_lastCrashTail) - 1);
    }

    // On a background thread: never lock a mutex in the signal handler.
#if defined(__APPLE__)
    const bool isBackground = !pthread_main_np();
#else
    const bool isBackground = g_mainThread != 0 && !pthread_equal(pthread_self(), g_mainThread);
#endif
    if (isBackground) {
        // A guest thread that faulted is parked here permanently so the launcher UI
        // stays alive and the Swift timer can show the crash modal.
        //
        // The loop is not cosmetic. pause() returns -1/EINTR as soon as ANY handler on
        // this thread returns, and falling out of it returns from crashHandler, which
        // resumes the faulting instruction and faults again — an endless fault → handler
        // → pause → fault cycle on a thread that is already dead. Each pass appends
        // another crash block, which is why one run produced two unrelated-looking
        // tombstones for the same thread and no way to tell which fault came first.
        //
        // Blocking every signal first means the next fault on this thread cannot even
        // re-enter the handler: the thread stops here, once, for good.
        sigset_t block_all;
        sigfillset(&block_all);
        pthread_sigmask(SIG_BLOCK, &block_all, nullptr);
        // Parking the worker alone leaves the engine running on a dead thread:
        // the frames log keeps scrolling while the screen is frozen and no modal
        // ever appears, because the shell's poll checks g_hasCrashed but the
        // main thread never observes the crash itself. Everything that reaches
        // this park has already written its report and set g_hasCrashed (both
        // the fatal branch and the worker-isolated branch above), so wake the
        // main thread: its wait fails with EINTR and the loop reaches the modal
        // path with the report on disk.
        // Only inside a live guest session (kudroid_set_log_dir also records a
        // main thread in hosts that merely exercise this reporter, e.g. the
        // breadcrumb test, and those depend on the park-and-hold behaviour).
        // Only into a disposition we own or the default: the guest sigaction
        // shim installs its trampoline over ours without chaining, and waking
        // into a foreign handler would run guest/debugger code on the main
        // thread instead of interrupting it. Default still terminates, which
        // beats a freeze with the report already on disk.
        if (g_hasCrashed.load(std::memory_order_relaxed) && s_isApkRunning.load(std::memory_order_relaxed) &&
            g_mainThread != 0 && !pthread_equal(pthread_self(), g_mainThread)) {
            struct sigaction cur;
            std::memset(&cur, 0, sizeof(cur));
            const bool known = ::sigaction(SIGUSR2, nullptr, &cur) == 0;
            const bool ours = known && (cur.sa_flags & SA_SIGINFO) == 0 &&
                              cur.sa_handler == kudroid_wake_handler;
            const bool dfl = known && cur.sa_handler == SIG_DFL;
            if (ours || dfl) {
                pthread_kill(g_mainThread, SIGUSR2);
            }
        }
        for (;;) {
            pause();
            // pause() also returns for a signal that is merely unblocked-and-ignored,
            // so sleeping keeps this from becoming a spin if that ever happens.
            struct timespec forever = {3600, 0};
            nanosleep(&forever, nullptr);
        }
    } else {
        // Crash on the main thread itself: restore default and re-raise so the
        // process dies at once instead of parking the thread the shell runs on.
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

void installCrashHandlers(void) {
    static bool installed = false;
    if (installed) return;
    installed = true;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crashHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    // Run the handler on a separate static stack (signal-safe, survives stack overflow).
    {
        // Fixed 64KB static stack (SIGSTKSZ is no longer constant on new glibc).
        armAltSignalStack();
        if (g_altStackArmed) {
            sa.sa_flags |= SA_ONSTACK;
        }
    }

    // Keep whatever was installed before us, per signal.
    //
    // SIGTRAP is load-bearing, not merely diagnostic: guest `mrs xN, tpidr_el0` is
    // rewritten to `BRK #(0x1000+N)` at load time and the handler supplies the TLS
    // pointer. If a host (LiveContainer, a debugger bridge, a crash reporter) already
    // has a handler and we replace it without recording it, its own traps are silently
    // dropped; if it replaces ours, every guest TLS read becomes a fatal trap.
    //
    // Chaining is what makes both survive: bionic_handle_tpidr_trap claims only the
    // BRK immediates KuDroid itself planted, and anything it does not recognise goes to
    // the previous handler rather than being reported as a KuDroid crash.
    const int kSignals[] = {SIGILL, SIGBUS, SIGSEGV, SIGTRAP, SIGABRT, SIGSYS};
    for (int sig : kSignals) {
        struct sigaction previous;
        memset(&previous, 0, sizeof(previous));
        if (sigaction(sig, &sa, &previous) != 0) continue;
        const bool hadHandler =
            (previous.sa_flags & SA_SIGINFO) ? previous.sa_sigaction != nullptr
                                             : (previous.sa_handler != SIG_DFL &&
                                                previous.sa_handler != SIG_IGN);
        if (hadHandler) {
            g_previousHandlers[sig] = previous;
            fprintf(stderr,
                    "[kudroid_core] signal %d already had a handler; chaining to it for"
                    " faults KuDroid does not own\n",
                    sig);
        }
    }

    // Android/bionic ignores SIGPIPE by default (writes fail with EPIPE instead of killing).
    ::signal(SIGPIPE, SIG_IGN);

    // The fatal-path park wakes the main thread with SIGUSR2 (see above). The
    // interrupter installed below turns default-terminate into EINTR so the
    // engine wait returns and the shell poll reaches the modal; without it the
    // process dies before the modal can show. Plain sa_handler, no SA_RESTART:
    // restarting the wait would defeat the wake. A guest SIGUSR2 handler
    // displaces this one (the shim does not chain), and the park checks the
    // disposition before sending, so it never fires into the guest trampoline.

    // SIGUSR2 interrupter for the fatal-path park (defined above crashHandler).
    {
        struct sigaction wake;
        std::memset(&wake, 0, sizeof(wake));
        wake.sa_handler = kudroid_wake_handler;
        sigemptyset(&wake.sa_mask);
        ::sigaction(SIGUSR2, &wake, nullptr);
    }

#if defined(__APPLE__)
    // Catch abort reasons as they happen: uncaught ObjC exceptions + C++ terminate.
    NSSetUncaughtExceptionHandler(&kudroid_uncaught_objc_handler);
    std::set_terminate(&kudroid_terminate_handler);
#endif

    // g_mainThread is deliberately NOT recorded here: kudroid_set_log_dir
    // installs these handlers in test hosts too, and a recorded main thread
    // makes the fatal park wake the test process itself. The app bridge records
    // it through crashNoteMainThread() instead.
}

// Diagnostic: whose handler owns the fatal signals right now? Call at X and at
// nativeDone entry to bracket a disposition change across teardown.
extern "C" void kudroid_log_signal_disposition(const char* tag) {
    static const int kSigs[] = {SIGTRAP, SIGILL, SIGBUS, SIGSEGV, SIGABRT};
    for (int sig : kSigs) {
        struct sigaction cur;
        std::memset(&cur, 0, sizeof(cur));
        if (::sigaction(sig, nullptr, &cur) != 0) continue;
        const void* h = (cur.sa_flags & SA_SIGINFO)
                            ? reinterpret_cast<const void*>(cur.sa_sigaction)
                            : reinterpret_cast<const void*>(cur.sa_handler);
        const bool ours = (cur.sa_flags & SA_SIGINFO) &&
                          cur.sa_sigaction == crashHandler;
        char line[128];
        const bool is_dfl = (cur.sa_flags & SA_SIGINFO) ? (cur.sa_sigaction == nullptr)
                                                        : (cur.sa_handler == SIG_DFL);
        std::snprintf(line, sizeof(line), "%s sig=%d handler=%p %s", tag != nullptr ? tag : "?",
                      sig, h, ours ? "OURS" : (is_dfl ? "DFL" : "FOREIGN"));
        kudroid_android_log_message(4, "KuDroidTrap", line);
    }
}

// Thin predicates so the seam in CrashHandling.h stays narrow.
#if defined(__APPLE__)
int crashIsMainThread(void) { return pthread_main_np() != 0 ? 1 : 0; }
int crashIsBackgroundThread(void) { return pthread_main_np() == 0 ? 1 : 0; }
#else
int crashIsMainThread(void) {
    return g_mainThread != 0 && pthread_equal(pthread_self(), g_mainThread) ? 1 : 0;
}
int crashIsBackgroundThread(void) { return crashIsMainThread() ? 0 : 1; }
#endif
void crashNoteMainThread(void) { g_mainThread = pthread_self(); }
