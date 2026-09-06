#include "kudroid/abi/GuestSignals.h"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <mutex>
#include <signal.h>

#if defined(__APPLE__)
#include <sys/ucontext.h>
#include <mach/mach.h>
#include <mach/thread_status.h>
#endif

extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);

namespace kudroid {

// The layout this file exists to get right. Pinned rather than trusted: the whole bug
// was a struct that looked plausible and was wrong, and a comment would not have caught
// it. bionic LP64 (libc/include/bits/signal_types.h, __SIGACTION_BODY under
// `#if defined(__LP64__)`) declares sa_flags FIRST.
static_assert(sizeof(GuestSigaction) == 32, "guest sigaction is 32 bytes on LP64");
static_assert(offsetof(GuestSigaction, sa_flags) == 0, "sa_flags is first on LP64");
static_assert(offsetof(GuestSigaction, sa_handler_or_sigaction) == 8, "handler follows flags");
static_assert(offsetof(GuestSigaction, sa_mask) == 16, "mask follows handler");
static_assert(offsetof(GuestSigaction, sa_restorer) == 24, "restorer is last");

namespace {

// ── Signal numbers ───────────────────────────────────────────────────────────
//
// Linux and Darwin agree up to SIGFPE and diverge after it. The left column is the
// Linux/arm64 number, which is fixed by asm-generic/signal.h and safe to write as a
// literal; the right is the HOST's macro, so this table is correct on either platform
// without a second copy. Writing both sides as numbers is how a table like this rots.
//
// The divergence is not cosmetic. Guest SIGUSR1 is 10, and 10 on Darwin is SIGBUS:
// forwarding the number unchanged hands a guest's routine user-signal handler the
// signal KuDroid uses for memory faults.
//
// Two Linux signals have NO Darwin equivalent by name — SIGSTKFLT (16) and SIGPWR (30) —
// and Linux also has 33 real-time signals that Darwin does not have at all. Leaving them
// unmapped meant sigaction returned EINVAL for them, and that is not a harmless gap: a
// managed runtime suspends its threads for GC with a signal it picks from exactly that
// range. Mono/il2cpp uses SIGPWR (or SIGRTMIN on newer builds) as SIG_SUSPEND and SIGXCPU
// as SIG_RESTART, so ULTRAKILL printed "Cannot set SIG_SUSPEND handler" and ran with no
// working thread suspension.
//
// They are mapped onto host signals that exist and that nothing else here claims.
struct SignalPair {
    int linux_num;
    int host_num;
};

// Host signals borrowed for Linux numbers that have no name-wise counterpart.
//
// SIGEMT (7) and SIGINFO (29) are the only two Darwin signals no entry below uses, and
// neither is something a guest can receive by accident: SIGEMT is an emulator trap that
// arm64 never raises, and SIGINFO only arrives from a terminal keystroke that no iOS app
// has. So a handler installed for one of these is only ever reached because the guest
// itself sent the signal — which is precisely how a runtime uses SIG_SUSPEND.
//
// The alternative was to reject them, and that was already tried by omission: it produced
// a guest whose GC cannot stop the world.
#if defined(SIGEMT)
#define KUDROID_HOST_FOR_SIGSTKFLT SIGEMT
#else
#define KUDROID_HOST_FOR_SIGSTKFLT SIGSTKFLT
#endif
#if defined(SIGINFO)
#define KUDROID_HOST_FOR_SIGPWR SIGINFO
#else
#define KUDROID_HOST_FOR_SIGPWR SIGPWR
#endif

constexpr SignalPair kSignalMap[] = {
    {1, SIGHUP},   {2, SIGINT},   {3, SIGQUIT},  {4, SIGILL},   {5, SIGTRAP},
    {6, SIGABRT},  {7, SIGBUS},   {8, SIGFPE},   {9, SIGKILL},  {10, SIGUSR1},
    {11, SIGSEGV}, {12, SIGUSR2}, {13, SIGPIPE}, {14, SIGALRM}, {15, SIGTERM},
    // Linux 16 is SIGSTKFLT. No Darwin signal has that meaning, so it borrows one that
    // arm64 never raises on its own.
    {16, KUDROID_HOST_FOR_SIGSTKFLT},
    {17, SIGCHLD}, {18, SIGCONT}, {19, SIGSTOP}, {20, SIGTSTP}, {21, SIGTTIN},
    {22, SIGTTOU}, {23, SIGURG},  {24, SIGXCPU}, {25, SIGXFSZ}, {26, SIGVTALRM},
    {27, SIGPROF}, {28, SIGWINCH},
#if defined(SIGIO)
    {29, SIGIO},
#endif
    // Linux 30 is SIGPWR — Mono's default SIG_SUSPEND on Android, and the reason this
    // entry exists rather than staying a comment about an absent counterpart.
    {30, KUDROID_HOST_FOR_SIGPWR},
    {31, SIGSYS},
};

// Linux real-time signals, and the host range they map onto.
//
// Linux has SIGRTMIN=32 through SIGRTMAX=64. Darwin has none, and a guest runtime that
// asks for SIGRTMIN gets EINVAL — the newer-Mono form of the same SIG_SUSPEND failure.
//
// Mapped onto the host's own real-time range where one exists (Linux hosts, so the shim
// tests exercise a real mapping) and refused where it does not. Refusing is honest: there
// is no host signal left to borrow for 33 of them, and silently aliasing several guest
// signals onto one host signal would make a guest's suspend and restart signals the same
// number — worse than EINVAL, because the runtime would believe it had two.
constexpr int kLinuxSigRtMin = 32;
constexpr int kLinuxSigRtMax = 64;

int rt_signal_to_host(int guest_signum) {
    if (guest_signum < kLinuxSigRtMin || guest_signum > kLinuxSigRtMax) return 0;
#if defined(SIGRTMIN) && defined(SIGRTMAX)
    const int offset = guest_signum - kLinuxSigRtMin;
    const int host = SIGRTMIN + offset;
    if (host > SIGRTMAX) return 0;
    return host;
#else
    return 0;
#endif
}

int rt_signal_to_guest(int host_signum) {
#if defined(SIGRTMIN) && defined(SIGRTMAX)
    if (host_signum < SIGRTMIN || host_signum > SIGRTMAX) return 0;
    const int guest = kLinuxSigRtMin + (host_signum - SIGRTMIN);
    if (guest > kLinuxSigRtMax) return 0;
    return guest;
#else
    (void)host_signum;
    return 0;
#endif
}

// ── Flags ────────────────────────────────────────────────────────────────────
//
// Guest values are Linux's. Translated by name for the same reason as the numbers.
constexpr int kLinuxSaNoCldStop = 0x00000001;
constexpr int kLinuxSaNoCldWait = 0x00000002;
constexpr int kLinuxSaSigInfo   = 0x00000004;
constexpr int kLinuxSaOnStack   = 0x08000000;
constexpr int kLinuxSaRestart   = 0x10000000;
constexpr int kLinuxSaNoDefer   = 0x40000000;
// SA_RESETHAND is 0x80000000, which does not fit in a signed int without wrapping.
constexpr int kLinuxSaResetHand = static_cast<int>(0x80000000u);

int flags_guest_to_host(int guest_flags) {
    int out = 0;
    if (guest_flags & kLinuxSaSigInfo)   out |= SA_SIGINFO;
    if (guest_flags & kLinuxSaOnStack)   out |= SA_ONSTACK;
    if (guest_flags & kLinuxSaRestart)   out |= SA_RESTART;
    if (guest_flags & kLinuxSaNoDefer)   out |= SA_NODEFER;
    if (guest_flags & kLinuxSaResetHand) out |= SA_RESETHAND;
    if (guest_flags & kLinuxSaNoCldStop) out |= SA_NOCLDSTOP;
    if (guest_flags & kLinuxSaNoCldWait) out |= SA_NOCLDWAIT;
    return out;
}

int flags_host_to_guest(int host_flags) {
    int out = 0;
    if (host_flags & SA_SIGINFO)   out |= kLinuxSaSigInfo;
    if (host_flags & SA_ONSTACK)   out |= kLinuxSaOnStack;
    if (host_flags & SA_RESTART)   out |= kLinuxSaRestart;
    if (host_flags & SA_NODEFER)   out |= kLinuxSaNoDefer;
    if (host_flags & SA_RESETHAND) out |= kLinuxSaResetHand;
    if (host_flags & SA_NOCLDSTOP) out |= kLinuxSaNoCldStop;
    if (host_flags & SA_NOCLDWAIT) out |= kLinuxSaNoCldWait;
    return out;
}

// A guest sa_mask is a Linux bitmask: bit (n-1) means Linux signal n. Translating it
// means translating every bit, not copying the word.
void mask_guest_to_host(uint64_t guest_mask, sigset_t* out) {
    sigemptyset(out);
    for (int linux_sig = 1; linux_sig < 64; ++linux_sig) {
        if ((guest_mask & (1ull << (linux_sig - 1))) == 0) continue;
        const int host_sig = guest_signal_to_host(linux_sig);
        // Never allow masking SIGTRAP (TLS emulation) or SIGSYS (syscall emulation)
        if (host_sig != 0 && host_sig != SIGTRAP && host_sig != SIGSYS) {
            sigaddset(out, host_sig);
        }
    }
}

uint64_t mask_host_to_guest(const sigset_t* host_mask) {
    uint64_t out = 0;
    for (const SignalPair& p : kSignalMap) {
        if (sigismember(host_mask, p.host_num) == 1) {
            out |= 1ull << (p.linux_num - 1);
        }
    }
    // The real-time range, which the table above does not contain: it is a contiguous
    // mapping, not a list of pairs.
    //
    // Leaving it out silently dropped every RT signal from a mask the guest read back,
    // and that is not a cosmetic loss. The normal sequence is save-block-restore: a
    // runtime reads its mask, blocks its suspend signal, does the critical work, then
    // restores what it saved. If the readback loses the RT bits, the restore installs a
    // mask that never had them, so a signal the guest believed blocked comes through — or
    // one it believed unblocked stays masked. Mono's newer builds use SIGRTMIN for
    // SIG_SUSPEND, so this is the same signal the send path misdirects, failing one step
    // later and in the opposite direction.
#if defined(SIGRTMIN) && defined(SIGRTMAX)
    for (int host_sig = SIGRTMIN; host_sig <= SIGRTMAX; ++host_sig) {
        if (sigismember(host_mask, host_sig) != 1) continue;
        const int guest_sig = rt_signal_to_guest(host_sig);
        if (guest_sig >= 1 && guest_sig <= 64) {
            out |= 1ull << (guest_sig - 1);
        }
    }
#endif
    return out;
}

// ── Ownership ────────────────────────────────────────────────────────────────
//
// Signals KuDroid needs for the guest to work at all, as HOST numbers.
//
// SIGTRAP: guest `mrs xN, tpidr_el0` is rewritten to `BRK #(0x1000+N)` at load time,
// and the handler is what supplies the TLS pointer. Lose it and every guest TLS read
// becomes a fatal trap.
//
// SIGSYS: a raw guest `svc #0` arrives here and is emulated. Lose it and every direct
// syscall becomes fatal.
//
// SIGSEGV/SIGBUS/SIGILL/SIGABRT: KuDroid's crash reporting and the JNI_OnLoad abort
// shield. A guest may legitimately want these too — a crash reporter does — so its
// handler is recorded and dispatched to, but KuDroid's stays installed.
bool kudroid_owns(int host_signum) {
    return host_signum == SIGTRAP || host_signum == SIGSYS || host_signum == SIGSEGV ||
           host_signum == SIGBUS || host_signum == SIGILL || host_signum == SIGABRT;
}

// ── Registry ─────────────────────────────────────────────────────────────────
//
// One slot per host signal. Read from a signal handler, so: a plain array, no map, no
// allocation, and the handler pointer is atomic so a concurrent sigaction cannot be
// seen half-written.
struct GuestHandlerSlot {
    std::atomic<void*> handler{nullptr};
    std::atomic<int> guest_flags{0};
    std::atomic<uint64_t> guest_mask{0};
    std::atomic<void*> restorer{nullptr};
};

GuestHandlerSlot g_slots[NSIG];

// Guards installation only. Never taken from a signal handler — the handler reads the
// atomics above and nothing else.
std::mutex g_installMutex;

// SIG_DFL is 0 and SIG_IGN is 1, so a recorded "handler" may be one of those rather
// than a real function. Dispatch must not call them.
bool is_real_handler(void* h) {
    return h != nullptr && h != reinterpret_cast<void*>(SIG_IGN) &&
           h != reinterpret_cast<void*>(SIG_DFL);
}

// ── Guest-visible siginfo and ucontext ───────────────────────────────────────
//
// A guest handler with SA_SIGINFO reads these, and a crash reporter reads the register
// state in particular. Handing it the host's layout would give it plausible garbage —
// which for a crash reporter means a tombstone naming the wrong address, and that is
// worse than no tombstone.

// bionic arm64 siginfo_t, first three fields plus the union member that matters.
//
// The fields are NOT named si_signo/si_code/si_addr. glibc's <signal.h> defines those
// as macros (si_addr expands to _sifields._sigfault.si_addr), so a struct declaring
// them by name does not compile on a Linux host. Names here describe the layout; the
// layout is what the guest reads.
struct GuestSiginfo {
    int32_t signo;
    int32_t err;
    int32_t code;
    int32_t pad0;
    // The union in bionic is 112 bytes on LP64. Only the fault address is populated:
    // it is what a fault handler reads, and inventing values for the rest would be
    // worse than leaving them zero.
    void* addr;
    uint8_t pad1[104];
};
static_assert(sizeof(GuestSiginfo) == 128, "guest siginfo_t is 128 bytes on LP64");

// Linux arm64 stack_t: {sp, flags, size}. Darwin's is {sp, size, flags}, which is the
// second half of this file's bug — see guest_sigaltstack.
struct GuestStack {
    void* sp;
    int32_t flags;
    int32_t pad;
    uint64_t size;
};
static_assert(sizeof(GuestStack) == 24, "guest stack_t is 24 bytes on LP64");

// Linux arm64 struct sigcontext, from asm/sigcontext.h.
struct GuestSigcontext {
    uint64_t fault_address;
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
    // The kernel reserves 4096 bytes here for FP/SVE records, 16-byte aligned. Left
    // zeroed: a guest that walks it finds no records, which is a valid encoding.
    alignas(16) uint8_t reserved[4096];
};

// Linux arm64 ucontext_t.
struct GuestUcontext {
    uint64_t uc_flags;
    GuestUcontext* uc_link;
    GuestStack uc_stack;
    uint64_t uc_sigmask;
    uint8_t padding[1024 / 8 - sizeof(uint64_t)];
    GuestSigcontext uc_mcontext;
};

// Scratch for one dispatch, per thread.
//
// A GuestUcontext is over 4KB and the handler may be running on an alternate signal
// stack, where putting it on the stack risks an overflow inside the diagnostic. Static
// per-thread storage costs address space that is never faulted in unless a signal
// actually arrives on that thread.
struct DispatchScratch {
    GuestSiginfo info;
    GuestUcontext uc;
};
thread_local DispatchScratch t_scratch;

// Recursion guard. A guest handler that faults would otherwise re-enter here and
// recurse until the stack is gone — the same shape as the bug this file fixes, and the
// reason it must be impossible by construction rather than by care.
thread_local int t_depth = 0;

// The approximate stack position of the dispatch frame that raised the guard.
//
// A plain counter is not enough, because a handler is not required to return. il2cpp's
// SIGABRT handler siglongjmps out to its own recovery point, and a jump does not unwind:
// the `--t_depth` at the end of dispatch never runs. The counter then stays raised for
// the rest of the thread's life, every later signal looks like recursion, and the guest's
// handler is never called again. On device that is silent — no crash log, no breadcrumb,
// and a guest that believes its handler is installed.
//
// The stack tells the two cases apart. Genuine recursion happens with the previous
// dispatch frame still live, so it runs DEEPER: on a downward-growing stack its frame
// address is lower. A jump out unwinds past that frame, so the next dispatch runs at the
// same depth or shallower — a higher address. Comparing frame positions is therefore an
// observation about whether the earlier frame still exists, which is exactly the question.
//
// arm64 and x86-64 both grow down; there is no upward-growing platform in play here.
thread_local uintptr_t t_depth_frame = 0;

// A local's address, as a stand-in for "where this frame is".
//
// __builtin_frame_address(0) would be the direct way to ask, but at -O3 the frame pointer
// may be omitted and it is then not required to answer usefully. The address of a local
// is always in the current frame.
__attribute__((noinline)) uintptr_t current_frame_position() {
    volatile char probe = 0;
    return reinterpret_cast<uintptr_t>(&probe);
}

}  // namespace

int guest_signal_to_host(int guest_signum) {
    for (const SignalPair& p : kSignalMap) {
        if (p.linux_num == guest_signum) return p.host_num;
    }
    // Real-time signals are a contiguous range rather than a table entry: a guest asking
    // for SIGRTMIN+2 must reach the host's SIGRTMIN+2, not fall off the end.
    return rt_signal_to_host(guest_signum);
}

int host_signal_to_guest(int host_signum) {
    for (const SignalPair& p : kSignalMap) {
        if (p.host_num == host_signum) return p.linux_num;
    }
    return rt_signal_to_guest(host_signum);
}

bool guest_signal_has_handler(int host_signum) {
    if (host_signum <= 0 || host_signum >= NSIG) return false;
    return is_real_handler(g_slots[host_signum].handler.load(std::memory_order_acquire));
}

bool guest_signal_dispatch(int host_signum, void* host_siginfo, void* host_ucontext) {
    if (host_signum <= 0 || host_signum >= NSIG) return false;

    GuestHandlerSlot& slot = g_slots[host_signum];
    void* handler = slot.handler.load(std::memory_order_acquire);
    if (!is_real_handler(handler)) return false;

    // Recursion, or a handler that left without returning?
    //
    // The counter alone cannot tell: a handler that siglongjmps out skips the decrement,
    // so a raised counter means either "a dispatch frame is still live above us" (real
    // recursion, decline) or "an earlier handler jumped out and the counter was never
    // put back" (the frame is gone, proceed).
    //
    // The frame position answers it. Recursion runs deeper than the frame that raised the
    // guard — a lower address on a downward-growing stack. A jump out unwound that frame,
    // so this call runs at the same position or shallower.
    const uintptr_t frame = current_frame_position();
    if (t_depth != 0) {
        if (frame < t_depth_frame) {
            return false;  // deeper: the guest handler itself faulted
        }
        // The frame that raised the guard is gone. Reset rather than decline, or this
        // thread never dispatches to the guest again.
        t_depth = 0;
    }
    ++t_depth;
    t_depth_frame = frame;

    const int guest_signum = host_signal_to_guest(host_signum);
    const int guest_flags = slot.guest_flags.load(std::memory_order_relaxed);
    const bool wants_siginfo = (guest_flags & kLinuxSaSigInfo) != 0;

    bool state_changed = false;

    if (!wants_siginfo) {
        // Plain handler: one int, nothing to translate but the number.
        auto fn = reinterpret_cast<void (*)(int)>(handler);
        fn(guest_signum);
    } else {
        DispatchScratch& s = t_scratch;
        std::memset(&s, 0, sizeof(s));

        s.info.signo = guest_signum;
        if (host_siginfo != nullptr) {
            const siginfo_t* hi = static_cast<const siginfo_t*>(host_siginfo);
            // si_code values overlap between the platforms for the fault codes a guest
            // acts on (SEGV_MAPERR=1, SEGV_ACCERR=2, BUS_ADRALN=1, ILL_ILLOPC=1), so
            // the number carries across. si_errno does not: Darwin does not set it.
            s.info.code = hi->si_code;
            s.info.addr = hi->si_addr;
        }

        uint64_t pc_before = 0;
        (void)pc_before;
        (void)host_ucontext;
#if defined(__APPLE__) && defined(__aarch64__)
        if (host_ucontext != nullptr) {
            const ucontext_t* hu = static_cast<const ucontext_t*>(host_ucontext);
            const auto* ss = &hu->uc_mcontext->__ss;
            for (int i = 0; i < 29; ++i) s.uc.uc_mcontext.regs[i] = ss->__x[i];
            // x29/x30 are named fp/lr on Darwin but occupy regs[29]/regs[30] in the
            // Linux layout, which is what a guest unwinder walks.
            s.uc.uc_mcontext.regs[29] = arm_thread_state64_get_fp(*ss);
            s.uc.uc_mcontext.regs[30] = arm_thread_state64_get_lr(*ss);
            s.uc.uc_mcontext.sp = arm_thread_state64_get_sp(*ss);
            s.uc.uc_mcontext.pc = arm_thread_state64_get_pc(*ss);
            s.uc.uc_mcontext.pstate = ss->__cpsr;
            if (host_siginfo != nullptr) {
                s.uc.uc_mcontext.fault_address =
                    reinterpret_cast<uint64_t>(static_cast<const siginfo_t*>(host_siginfo)->si_addr);
            }
            s.uc.uc_sigmask = mask_host_to_guest(&hu->uc_sigmask);
            s.uc.uc_stack.sp = hu->uc_stack.ss_sp;
            s.uc.uc_stack.size = hu->uc_stack.ss_size;
            s.uc.uc_stack.flags = hu->uc_stack.ss_flags;
            pc_before = s.uc.uc_mcontext.pc;
        }
#endif

        auto fn = reinterpret_cast<void (*)(int, void*, void*)>(handler);
        fn(guest_signum, &s.info, &s.uc);

#if defined(__APPLE__) && defined(__aarch64__)
        // A handler that fixed the fault says so by moving pc — that is how a runtime
        // patching up a null dereference resumes. Copy the guest's edits back and
        // report that the fault is dealt with.
        //
        // A handler that left pc alone has NOT handled it, whatever it wrote elsewhere.
        // Reporting success there would resume at the faulting instruction and fault
        // again, forever: the exact loop that made ULTRAKILL spin at 100% of a core.
        if (host_ucontext != nullptr && s.uc.uc_mcontext.pc != pc_before) {
            ucontext_t* hu = static_cast<ucontext_t*>(host_ucontext);
            auto* ss = &hu->uc_mcontext->__ss;
            for (int i = 0; i < 29; ++i) ss->__x[i] = s.uc.uc_mcontext.regs[i];
            arm_thread_state64_set_fp(*ss, s.uc.uc_mcontext.regs[29]);
            arm_thread_state64_set_lr_fptr(*ss, reinterpret_cast<void*>(s.uc.uc_mcontext.regs[30]));
            arm_thread_state64_set_sp(*ss, s.uc.uc_mcontext.sp);
            arm_thread_state64_set_pc_fptr(*ss, reinterpret_cast<void*>(s.uc.uc_mcontext.pc));
            state_changed = true;
        }
#endif
    }

    --t_depth;
    t_depth_frame = 0;
    return state_changed;
}

// The host handler installed for any signal KuDroid does not own. Uniform with the
// owned path: every guest handler is reached through translation, never called with
// host-shaped arguments.
extern "C" void kudroid_guest_signal_trampoline(int host_signum, siginfo_t* info, void* uc) {
    // Diagnostic: async delivery zeroes the platform register on this OS; a
    // delivery landing inside guest x18-live code is fatal, so every delivery
    // is named (capped) to attribute clobber crashes.
    static std::mutex s_mtx;
    static int s_n{0};
    {
        std::lock_guard<std::mutex> lock(s_mtx);
        if (s_n < 12) {
            ++s_n;
            char msg[128];
            std::snprintf(msg, sizeof(msg), "guest signal delivered host=%d", host_signum);
            kudroid_android_log_message(4, "KuDroidSignal", msg);
        }
    }
    guest_signal_dispatch(host_signum, info, uc);
}

int guest_sigaction(int guest_signum, const GuestSigaction* act, GuestSigaction* oldact) {
    const int host_signum = guest_signal_to_host(guest_signum);
    if (host_signum == 0) {
        // No host signal left to carry this one. Only reachable now for a real-time signal
        // on a host with no RT range of its own — every named Linux signal is mapped.
        //
        // SAY SO. A silent EINVAL here is what "Cannot set SIG_SUSPEND handler" was: the
        // guest reported a failure, KuDroid reported nothing, and the number it had asked
        // for — the one fact needed to fix it — appeared in no log. Rate-limited to the
        // first few distinct signals so a guest retrying in a loop cannot flood the log.
        static std::mutex s_mtx;
        static int s_seen[8];
        static int s_seenN = 0;
        bool report = false;
        {
            std::lock_guard<std::mutex> lock(s_mtx);
            bool dup = false;
            for (int i = 0; i < s_seenN; ++i) {
                if (s_seen[i] == guest_signum) { dup = true; break; }
            }
            if (!dup && s_seenN < 8) {
                s_seen[s_seenN++] = guest_signum;
                report = true;
            }
        }
        if (report) {
            // Only the real-time range is reachable here now, and only on a host with no
            // RT signals of its own — every named Linux signal is mapped. Anything else is
            // simply not a signal number, so the message must not speculate about
            // SIG_SUSPEND for it: the last round was cost by a log line that pointed
            // somewhere plausible and wrong.
            const bool plausible_runtime_signal =
                guest_signum >= kLinuxSigRtMin && guest_signum <= kLinuxSigRtMax;
            char msg[256];
            std::snprintf(msg, sizeof(msg),
                          "guest sigaction(%d) REFUSED: no host signal carries Linux %d "
                          "(handler=%p flags=0x%x).%s",
                          guest_signum, guest_signum,
                          act != nullptr ? act->sa_handler_or_sigaction : nullptr,
                          act != nullptr ? static_cast<unsigned>(act->sa_flags) : 0u,
                          plausible_runtime_signal
                              ? " This is a real-time signal; a managed runtime using it"
                                " for thread suspension will report that it cannot install"
                                " SIG_SUSPEND."
                              : " This is not a valid Linux signal number.");
            kudroid_android_log_message(6, "KuDroidSignal", msg);
        }
        errno = EINVAL;
        return -1;
    }
    // SIGKILL and SIGSTOP cannot be caught. Linux rejects the attempt; so must this,
    // or the guest believes it installed something it did not.
    if (host_signum == SIGKILL || host_signum == SIGSTOP) {
        errno = EINVAL;
        return -1;
    }

    std::lock_guard<std::mutex> lock(g_installMutex);
    GuestHandlerSlot& slot = g_slots[host_signum];

    if (oldact != nullptr) {
        std::memset(oldact, 0, sizeof(*oldact));
        oldact->sa_handler_or_sigaction = slot.handler.load(std::memory_order_relaxed);
        oldact->sa_flags = slot.guest_flags.load(std::memory_order_relaxed);
        oldact->sa_mask = slot.guest_mask.load(std::memory_order_relaxed);
        oldact->sa_restorer =
            reinterpret_cast<void (*)(void)>(slot.restorer.load(std::memory_order_relaxed));
        // Nothing recorded: report the host's current disposition rather than a zeroed
        // struct, so a guest that saves and restores does not install a null handler.
        if (oldact->sa_handler_or_sigaction == nullptr) {
            struct sigaction current;
            std::memset(&current, 0, sizeof(current));
            if (::sigaction(host_signum, nullptr, &current) == 0) {
                oldact->sa_flags = flags_host_to_guest(current.sa_flags);
                oldact->sa_mask = mask_host_to_guest(&current.sa_mask);
                oldact->sa_handler_or_sigaction =
                    (current.sa_flags & SA_SIGINFO)
                        ? reinterpret_cast<void*>(current.sa_sigaction)
                        : reinterpret_cast<void*>(current.sa_handler);
                // KuDroid's own handler is not the guest's business, and handing the
                // address back invites the guest to "restore" it into a slot where it
                // would be called with host-shaped arguments.
                if (kudroid_owns(host_signum)) {
                    oldact->sa_handler_or_sigaction = reinterpret_cast<void*>(SIG_DFL);
                }
            }
        }
    }

    if (act == nullptr) return 0;  // query only

    // Record first, install second. A signal arriving between the two must find either
    // the old handler with the old flags or the new with the new — never a new handler
    // with flags that have not been published.
    slot.guest_flags.store(act->sa_flags, std::memory_order_relaxed);
    slot.guest_mask.store(act->sa_mask, std::memory_order_relaxed);
    slot.restorer.store(reinterpret_cast<void*>(act->sa_restorer), std::memory_order_relaxed);
    void* h = act->sa_handler_or_sigaction;
    slot.handler.store(h, std::memory_order_release);

    if (kudroid_owns(host_signum)) {
        if (!is_real_handler(h)) {
            static std::mutex s_dflMtx;
            static int s_dflLogged{0};
            std::lock_guard<std::mutex> lock(s_dflMtx);
            if (s_dflLogged < 6) {
                ++s_dflLogged;
                char msg[160];
                std::snprintf(msg, sizeof(msg),
                              "guest reset owned signal %d (host %d) to %s",
                              guest_signum, host_signum,
                              h == reinterpret_cast<void*>(SIG_IGN) ? "IGN" : "DFL");
                kudroid_android_log_message(4, "KuDroidSignal", msg);
            }
        } else {
            char msg[192];
            std::snprintf(msg, sizeof(msg),
                          "guest sigaction(%d) recorded for host signal %d; KuDroid keeps the "
                          "handler and will call the guest's",
                          guest_signum, host_signum);
            kudroid_android_log_message(4, "KuDroidSignal", msg);
        }
        return 0;
    }

    struct sigaction host_act;
    std::memset(&host_act, 0, sizeof(host_act));
    // Always through the trampoline, and therefore always SA_SIGINFO on the host side:
    // the trampoline needs the host siginfo and ucontext to build the guest's, and it
    // synthesises the plain-handler call itself when the guest did not ask for them.
    host_act.sa_sigaction = kudroid_guest_signal_trampoline;
    host_act.sa_flags = flags_guest_to_host(act->sa_flags) | SA_SIGINFO;
    mask_guest_to_host(act->sa_mask, &host_act.sa_mask);

    if (!is_real_handler(h)) {
        // SIG_DFL and SIG_IGN pass through as themselves: there is nothing to translate
        // and routing them through a trampoline would turn "ignore" into "call a
        // function that does nothing", which differs for signals whose default is to
        // stop the process.
        host_act.sa_handler = reinterpret_cast<void (*)(int)>(h);
        host_act.sa_flags &= ~SA_SIGINFO;
    }

    if (::sigaction(host_signum, &host_act, nullptr) != 0) {
        // The host refused it. Report which signal, for the same reason as the mapping
        // failure above: the guest prints its own message, and without this there is no
        // record of what it asked for.
        const int saved = errno;
        char msg[224];
        std::snprintf(msg, sizeof(msg),
                      "guest sigaction(%d) -> host signal %d REJECTED by the host: %s",
                      guest_signum, host_signum, std::strerror(saved));
        kudroid_android_log_message(6, "KuDroidSignal", msg);
        errno = saved;
        return -1;
    }
    return 0;
}

int guest_sigaltstack(const void* guest_ss, void* guest_oss) {
    // A guest pointer is not to be trusted. These calls arrive from a raw `svc #0` as
    // well as from libc, and a direct syscall can carry any value at all — the shim
    // tests pass 0x4e for exactly this reason. Dereferencing it would turn a guest's
    // bad argument into a KuDroid crash, so an implausible address is EFAULT, which is
    // what the guest's own error path is written for.
    //
    // This is a cheap first filter, not a full validation: it catches null and the low
    // page, which is what a malformed or uninitialised pointer looks like. The caller
    // in SyscallShim adds a real mapped-range check on Apple, where a guest SVC can
    // carry a plausible-looking address from a different address space.
    constexpr uintptr_t kMinPlausible = 4096;
    if ((guest_ss != nullptr && reinterpret_cast<uintptr_t>(guest_ss) < kMinPlausible) ||
        (guest_oss != nullptr && reinterpret_cast<uintptr_t>(guest_oss) < kMinPlausible)) {
        errno = EFAULT;
        return -1;
    }

    // Read and written through the LINUX layout. Reading a guest stack_t as the host's
    // swaps ss_flags and ss_size: a guest asking for a 64KB alternate stack passes
    // ss_flags=65536, which is neither SS_ONSTACK nor SS_DISABLE, and the shim used to
    // fake success — so the guest believed it had an alternate stack it did not have,
    // and a stack-overflow fault had nowhere to run.
    stack_t host_ss;
    stack_t host_oss;
    std::memset(&host_ss, 0, sizeof(host_ss));
    std::memset(&host_oss, 0, sizeof(host_oss));

    const GuestStack* gs = static_cast<const GuestStack*>(guest_ss);
    if (gs != nullptr) {
        host_ss.ss_sp = gs->sp;
        host_ss.ss_size = static_cast<size_t>(gs->size);
        host_ss.ss_flags = gs->flags;
#if defined(__APPLE__)
        // Darwin requires at least MINSIGSTKSZ and rejects anything smaller. A guest
        // sizing its stack for Linux may pass less; growing it is safe, because the
        // guest only ever reads back the size it asked for.
        if (!(host_ss.ss_flags & SS_DISABLE) && host_ss.ss_size < 32768) {
            host_ss.ss_size = 32768;
        }
#endif
    }

    const int rc = ::sigaltstack(gs != nullptr ? &host_ss : nullptr,
                                 guest_oss != nullptr ? &host_oss : nullptr);
    if (rc != 0) {
        // Reported, not faked. The previous shim returned 0 on failure, so a guest
        // whose alternate stack was rejected never found out.
        return -1;
    }

    if (guest_oss != nullptr) {
        GuestStack* gos = static_cast<GuestStack*>(guest_oss);
        std::memset(gos, 0, sizeof(*gos));
        gos->sp = host_oss.ss_sp;
        gos->flags = host_oss.ss_flags;
        gos->size = static_cast<uint64_t>(host_oss.ss_size);
    }
    return 0;
}

void guest_signal_reset_for_test() {
    std::lock_guard<std::mutex> lock(g_installMutex);
    for (int i = 0; i < NSIG; ++i) {
        g_slots[i].handler.store(nullptr, std::memory_order_relaxed);
        g_slots[i].guest_flags.store(0, std::memory_order_relaxed);
        g_slots[i].guest_mask.store(0, std::memory_order_relaxed);
        g_slots[i].restorer.store(nullptr, std::memory_order_relaxed);
    }
    t_depth = 0;
    t_depth_frame = 0;
}

// ── Sending a signal ─────────────────────────────────────────────────────────

namespace {

// Would this host signal kill the process if it arrived right now?
//
// The check that the whole outbound bug needed and nobody could make. A guest sending a
// signal gets 0 back whether the target had a handler or not, and if it did not, the
// default action runs — for most of these, terminate. So "the send succeeded" and "the
// process is about to die" are the same observation from the guest's side, and the log
// said neither.
//
// Ignorable-by-default signals (SIGCHLD, SIGURG, SIGWINCH, and SIGINFO where it exists)
// are excluded: arriving at an empty slot is normal for those and warning would be noise.
bool host_default_action_is_fatal(int host_signum) {
    switch (host_signum) {
        case SIGCHLD:
        case SIGURG:
        case SIGWINCH:
        case SIGCONT:
#if defined(SIGINFO)
        case SIGINFO:
#endif
            return false;
        default:
            return true;
    }
}

// Warn when a guest sends a signal that nothing will catch and whose default action
// ends the process.
//
// This is the line that was missing from the ULTRAKILL log. il2cpp installed
// SIG_SUSPEND, KuDroid recorded it, and then every suspend went to a different host
// signal with an empty slot — a fatal delivery that looked, from every log KuDroid
// wrote, like a successful send.
//
// Rate-limited per guest signal so a runtime suspending threads in a loop cannot flood
// the log, and only ever a warning: refusing the send would be worse, since a guest
// signalling a thread it does not control is legitimate.
void warn_if_delivery_is_unhandled_and_fatal(int guest_signum, int host_signum,
                                             const char* how) {
    if (guest_signal_has_handler(host_signum)) return;
    if (kudroid_owns(host_signum)) return;  // KuDroid's own handler will catch it
    if (!host_default_action_is_fatal(host_signum)) return;

    // Nothing recorded on our side and nothing of KuDroid's: is there a host handler?
    struct sigaction current;
    std::memset(&current, 0, sizeof(current));
    if (::sigaction(host_signum, nullptr, &current) == 0) {
        const bool has_host_handler =
            (current.sa_flags & SA_SIGINFO) ? current.sa_sigaction != nullptr
                                            : (current.sa_handler != SIG_DFL &&
                                               current.sa_handler != SIG_IGN);
        if (has_host_handler) return;
    }

    static std::mutex s_mtx;
    static int s_seen[16];
    static int s_seenN = 0;
    {
        std::lock_guard<std::mutex> lock(s_mtx);
        for (int i = 0; i < s_seenN; ++i) {
            if (s_seen[i] == guest_signum) return;
        }
        if (s_seenN >= 16) return;
        s_seen[s_seenN++] = guest_signum;
    }

    char msg[320];
    std::snprintf(msg, sizeof(msg),
                  "%s(%d) delivers host signal %d, which NOTHING handles and whose "
                  "default action terminates the process. If the guest installed a "
                  "handler for Linux %d it is on host %d — check that both directions "
                  "translate. A managed runtime using this as SIG_SUSPEND will die here "
                  "instead of suspending a thread.",
                  how, guest_signum, host_signum, guest_signum,
                  guest_signal_to_host(guest_signum));
    kudroid_android_log_message(6, "KuDroidSignal", msg);
}

// Shared entry check for the four senders. Returns the host number, or 0 having set
// errno — every caller then returns -1, as the syscall does.
int host_signum_for_send(int guest_signum, const char* how) {
    if (guest_signum == 0) return 0;  // the existence check; callers special-case it
    const int host_signum = guest_signal_to_host(guest_signum);
    if (host_signum == 0) {
        char msg[224];
        std::snprintf(msg, sizeof(msg),
                      "%s(%d) REFUSED: no host signal carries Linux %d, so the send "
                      "cannot be delivered to whatever installed a handler for it.",
                      how, guest_signum, guest_signum);
        kudroid_android_log_message(6, "KuDroidSignal", msg);
        errno = EINVAL;
        return 0;
    }
    warn_if_delivery_is_unhandled_and_fatal(guest_signum, host_signum, how);
    return host_signum;
}

}  // namespace

int guest_raise(int guest_signum) {
    if (guest_signum == 0) return 0;
    const int host_signum = host_signum_for_send(guest_signum, "raise");
    if (host_signum == 0) return -1;
    // pthread_kill on self rather than ::raise: raise() is defined to signal the calling
    // THREAD on both platforms, but going through pthread_kill makes that explicit and
    // matches what the other three do.
    return ::pthread_kill(::pthread_self(), host_signum) == 0 ? 0 : -1;
}

int guest_kill(int pid, int guest_signum) {
    if (guest_signum == 0) return ::kill(pid, 0);
    const int host_signum = host_signum_for_send(guest_signum, "kill");
    if (host_signum == 0) return -1;
    return ::kill(pid, host_signum);
}

int guest_pthread_kill(unsigned long thread, int guest_signum) {
    pthread_t target;
    std::memcpy(&target, &thread, sizeof(target) < sizeof(thread) ? sizeof(thread)
                                                                 : sizeof(thread));
    if (guest_signum == 0) return ::pthread_kill(target, 0) == 0 ? 0 : -1;
    const int host_signum = host_signum_for_send(guest_signum, "pthread_kill");
    if (host_signum == 0) return -1;
    // Diagnostic: pairs with the delivery tap to attribute register-clobber crashes.
    static std::mutex s_mtx;
    static int s_n{0};
    {
        std::lock_guard<std::mutex> lock(s_mtx);
        if (s_n < 12) {
            ++s_n;
            char msg[128];
            std::snprintf(msg, sizeof(msg), "guest pthread_kill guest=%d host=%d",
                          guest_signum, host_signum);
            kudroid_android_log_message(4, "KuDroidSignal", msg);
        }
    }
    const int rc = ::pthread_kill(target, host_signum);
    if (rc != 0) {
        errno = rc;
        return -1;
    }
    return 0;
}

int guest_tgkill(int tgid, int tid, int guest_signum) {
    // The tid -> pthread_t lookup belongs to the syscall shim, which owns the registry.
    // This is only the translation, so tgkill's caller resolves the thread and then
    // reaches guest_pthread_kill. Kept as a named entry point so the four senders read
    // the same way at the call sites.
    (void)tgid;
    (void)tid;
    errno = ENOSYS;
    return -1;
}

// ── Blocking a signal ────────────────────────────────────────────────────────

namespace {

// Linux's SIG_* for sigprocmask. Written as literals because the point is that they are
// NOT the host's: on Darwin the same three names are 1, 2, 3.
constexpr int kLinuxSigBlock   = 0;
constexpr int kLinuxSigUnblock = 1;
constexpr int kLinuxSigSetmask = 2;

int how_guest_to_host(int guest_how) {
    switch (guest_how) {
        case kLinuxSigBlock:   return SIG_BLOCK;
        case kLinuxSigUnblock: return SIG_UNBLOCK;
        case kLinuxSigSetmask: return SIG_SETMASK;
        default:               return -1;
    }
}

}  // namespace

int guest_sigprocmask(int guest_how, const uint64_t* guest_set, uint64_t* guest_oldset) {
    // A null set means "query only", and then `how` is not read at all — Linux allows
    // any value there. Validating it unconditionally would reject a legitimate query.
    int host_how = SIG_SETMASK;
    if (guest_set != nullptr) {
        host_how = how_guest_to_host(guest_how);
        if (host_how < 0) {
            errno = EINVAL;
            return -1;
        }
    }

    sigset_t host_set;
    sigset_t host_oldset;
    sigemptyset(&host_set);
    sigemptyset(&host_oldset);
    if (guest_set != nullptr) mask_guest_to_host(*guest_set, &host_set);

    const int rc = ::pthread_sigmask(host_how, guest_set != nullptr ? &host_set : nullptr,
                                     guest_oldset != nullptr ? &host_oldset : nullptr);
    if (rc != 0) {
        errno = rc;
        return -1;
    }
    if (guest_oldset != nullptr) *guest_oldset = mask_host_to_guest(&host_oldset);
    return 0;
}

int guest_sigsuspend(const uint64_t* guest_mask) {
    sigset_t host_mask;
    sigemptyset(&host_mask);
    if (guest_mask != nullptr) mask_guest_to_host(*guest_mask, &host_mask);
    // sigsuspend always returns -1 with EINTR on success, which is the contract on both
    // platforms; pass it through rather than inventing a 0.
    return ::sigsuspend(&host_mask);
}

int guest_sigwait(const uint64_t* guest_set, int* guest_signum_out) {
    if (guest_set == nullptr) {
        errno = EINVAL;
        return -1;
    }
    sigset_t host_set;
    sigemptyset(&host_set);
    mask_guest_to_host(*guest_set, &host_set);

    int host_signum = 0;
    const int rc = ::sigwait(&host_set, &host_signum);
    if (rc != 0) {
        errno = rc;
        return -1;
    }
    // Back to the guest's numbering, or the guest compares it against its own constants
    // and takes the wrong branch — the same error as the send, one step later.
    if (guest_signum_out != nullptr) {
        const int guest_signum = host_signal_to_guest(host_signum);
        *guest_signum_out = guest_signum != 0 ? guest_signum : host_signum;
    }
    return 0;
}

void* guest_signal(int guest_signum, void* guest_handler) {
    // signal() is sigaction() with a fixed set of flags. Routing it through
    // guest_sigaction is what keeps the registry the single source of truth: a guest
    // that installs with signal() and reads back with sigaction() must see its own
    // handler, and a KuDroid-owned signal must be recorded rather than installed.
    GuestSigaction act;
    std::memset(&act, 0, sizeof(act));
    act.sa_handler_or_sigaction = guest_handler;
    act.sa_flags = kLinuxSaRestart;  // BSD semantics, which is what bionic's signal() uses

    GuestSigaction old;
    std::memset(&old, 0, sizeof(old));
    if (guest_sigaction(guest_signum, &act, &old) != 0) {
        return reinterpret_cast<void*>(SIG_ERR);
    }
    return old.sa_handler_or_sigaction;
}


}  // namespace kudroid
