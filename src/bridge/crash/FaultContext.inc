// Recovery budget for worker faults: a chunk-processing job over corrupt
// data faults per element (observed: 4-5 faults/iteration across 50-200 elements),
// so chunk jobs need a four-digit budget to complete without dying midway. Past it
// the thread is not progressing and the fault is fatal. Each skip is microseconds;
// the cost of headroom is minimal while too small a cap turns a survivable batch
// into an engine shutdown.
static constexpr int kMaxWorkerRecoveries = 1024;
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
// Null-base loop is never a reservation walk: below 0x10000 there is nothing
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
// The pc-rotating variant of the same storm (observed: `ldrb w6,[x10]` over a
// null struct, fault_addr cycling 0x0/0x1/0x2/0x3/0x34/0x80, four pcs chasing
// each other in a tight loop, 129 skips in microseconds until the 128 cap).
// Same-pc run tracking cannot see it — the pc never repeats often enough —
// and the count budget only delays the same fatal by (cap / burst) rounds.
// This counter counts the skips themselves, pc-blind, within the same 1ms
// tightness window: that many nullish faults back-to-back is a dead loop
// regardless of shape. An inter-frame burst never reaches 1% of it, and every
// member of the burst being nullish is what distinguishes it from a genuine
// forward walk (which touches real, advancing addresses).
static constexpr int kMaxNullBurst = 256;
// Nullish address: below the first page, or just below zero (a null base
// with a negative structure offset — observed: ldp [x18=0,#-32] faulting at
// -32). Nothing is mapped at either extreme, so inventing a result there is
// exactly as safe as on the null page.
static bool fault_addr_is_nullish(unsigned long long addr) {
    return addr < 0x10000u || addr >= (0ull - 0x10000ull);
}

// Unmapped-away-from-everything: false for a nullish address (the load-zero
// path owns those), false when any mapping answers (then the store-drop path
// must NOT apply — a committed-but-protected page is a live object this
// handler cannot classify; the containment requirement also defeats the
// mach_vm_region gap quirk that would answer with the first region for an
// address below all mappings), false inside the thread's own stack bounds
// and one guard page beyond them. Between heap, stack and the null page the
// rest of the 64-bit space is reservation nobody committed: dropping one
// write there poisons no readable state.
//
// QueryRegionProt and query_thread_stack_bounds are the probes the fatal path
// already uses for diagnostics, so a fresh worker fault pays only what the
// crash report would have paid anyway.
static bool unmapped_addr_is_store_safe(unsigned long long addr) {
    if (fault_addr_is_nullish(addr)) return false;
    char cur[4] = "???", max[4] = "???";
    uint64_t rbase = 0, rsize = 0;
    if (kudroid::QueryRegionProt(reinterpret_cast<const void*>(addr), cur, max,
                                 &rbase, &rsize) &&
        addr >= rbase && addr < rbase + rsize) {
        return false;  // a real region answers: not our shape
    }
    const kudroid::StackBounds st = kudroid::query_thread_stack_bounds();
    if (st.valid) {
        constexpr uintptr_t kStackGuard = 0x10000;
        if (addr >= st.low - kStackGuard && addr < st.high + kStackGuard) return false;
    }
    return true;
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
    // Nullish faults back-to-back regardless of pc. See kMaxNullBurst.
    std::atomic<int> nullBurst{0};
};
static WorkerBudget g_workerBudgets[8];

void crashResetWorkerBudgets(void) {
    for (auto& slot : g_workerBudgets) {
        slot.tid.store(0, std::memory_order_relaxed);
        slot.count.store(0, std::memory_order_relaxed);
        slot.lastNs.store(0, std::memory_order_relaxed);
        slot.nullRun.store(0, std::memory_order_relaxed);
        slot.nullBurst.store(0, std::memory_order_relaxed);
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
static void signal_safe_log(const char* tag, const char* msg) {
    char out[512];
    const int n = std::snprintf(out, sizeof(out), "[%s] %s\n", tag ? tag : "KuDroidTrap", msg ? msg : "");
    if (n > 0) {
        (void)::write(STDERR_FILENO, out, static_cast<size_t>(n));
        kudroid_persistent_breadcrumb(out);
    }
}

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
                signal_safe_log("KuDroidSignal", msg);
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
    // Store-drop shaping: a frame-spaced unmapped store is a new bad object,
    // not progress on an old one, so give it a fresh budget. A tight same-pc
    // sequence is a live write loop whose dropped stores are corrupting state
    // every frame — kMaxStoreRun drops, then peek refuses and the fatal path
    // surfaces the loop. Runs before the charge below so a spent budget still
    // reaches this reset.
    if (!fault_addr_is_nullish(addr) &&
        !(last != 0 && nowNs - last <= kMaxNullGapNs && pc != 0 &&
          pc == s->lastPc.load(std::memory_order_relaxed))) {
        s->count.store(0, std::memory_order_relaxed);
        s->walk.store(0, std::memory_order_relaxed);
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
            signal_safe_log("KuDroidSignal", msg);
        }
    } else {
        s->nullRun.store(fault_addr_is_nullish(addr) ? 1 : 0, std::memory_order_relaxed);
    }
    // pc-blind null-burst tracking (see kMaxNullBurst): within the same 1ms
    // tightness window, any nullish fault extends the burst; anything else —
    // time gap, a mapped fault address, a reset — starts it over.
    if (fault_addr_is_nullish(addr) && last != 0 && nowNs - last <= kMaxNullGapNs) {
        const int burst = s->nullBurst.fetch_add(1, std::memory_order_relaxed) + 1;
        if (burst == kMaxNullBurst) {
            char msg[96];
            std::snprintf(msg, sizeof(msg),
                          "null-burst: %d null skips in a row retired this worker",
                          kMaxNullBurst);
            signal_safe_log("KuDroidSignal", msg);
        }
    } else {
        s->nullBurst.store(0, std::memory_order_relaxed);
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
    // A live pc-rotating null storm (a tight run of nullish faults within the
    // 1ms window) is refused outright — the count budget must not be what
    // stops it, because that turns the crash into a slow-motion rerun at
    // 8x the skips. A burst that ages past the window re-enters the normal
    // budget path: isolated recurring null faults are a bad object, and the
    // count budget is exactly the tool for that.
    if (fault_addr_is_nullish(addr) &&
        s->nullBurst.load(std::memory_order_relaxed) >= kMaxNullBurst) {
        const long long nowNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        const long long prevNs = s->lastNs.load(std::memory_order_relaxed);
        if (prevNs != 0 && nowNs - prevNs <= kMaxNullGapNs) return false;
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


