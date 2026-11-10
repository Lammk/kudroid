// CrashHandling.cpp — the process-wide signal handler, the fault-skip
// machinery and the JNI_OnLoad shield. Split out of kudroid_bridge.cpp.
#include "BridgeState.h"
#include "BridgeShared.h"
#include "CrashInternal.h"
#include "CrashHandling.h"
#include "TrapDiagnostics.h"
#include "kudroid/BionicShim.h"
#include "kudroid/FaultSkip.h"
#include "kudroid/ElfAliasStore.h"
#include "kudroid/debug/FrameWalk.h"
#include "kudroid/KuArtRuntime.h"
#include "kudroid/elf_loader.hpp"
#include "kudroid/NativeCallTelemetry.h"
#include "kudroid/FmodVorbisFallback.h"

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
#include <libkern/OSCacheControl.h>
#include "kudroid/MachVmDecls.h"
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
#include "kudroid/kuart/MethodResolutionTrace.h"

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

#include "crash/FaultContext.inc"
#include "crash/FaultRecovery.inc"
#include "crash/SignalReport.inc"
#if defined(__APPLE__) && defined(__aarch64__)
namespace {
extern "C" void platformProbeHandler(int) {
    // Empty on purpose: the kernel's signal frame is what the interrupted code
    // sees on return, so whatever this handler does to x18 is not the question.
}
}  // namespace

void kudroid_probe_platform_register(void) {
    static bool done = false;
    if (done) return;
    done = true;

    const int sig = SIGUSR2;
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = platformProbeHandler;
    sigemptyset(&sa.sa_mask);
    struct sigaction old;
    if (sigaction(sig, &sa, &old) != 0) return;

    const long pid = static_cast<long>(::getpid());
    const uint64_t sentinel = 0x5150C0DE12345678ULL;
    uint64_t afterSyscall = 0;
    uint64_t afterDelivery = 0;
    // Both measurements are inline syscalls (Darwin arm64: getpid=20, kill=37)
    // rather than C calls, so the only thing between writing x18 and reading it
    // back is the kernel path being measured.
    __asm__ __volatile__(
        "mov x18, %1\n\t"
        "mov x16, #20\n\t"
        "svc #0x80\n\t"
        "mov %0, x18\n\t"
        : "=r"(afterSyscall)
        : "r"(sentinel)
        : "x0", "x16", "memory", "cc");
    __asm__ __volatile__(
        "mov x18, %1\n\t"
        "mov w0, %w2\n\t"
        "mov w1, %w3\n\t"
        "mov x16, #37\n\t"
        "svc #0x80\n\t"
        "mov %0, x18\n\t"
        : "=r"(afterDelivery)
        : "r"(sentinel), "r"(pid), "r"(static_cast<long>(sig))
        : "x0", "x1", "x16", "memory", "cc");
    (void)sigaction(sig, &old, nullptr);

    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  "x18: syscall=0x%llx delivery=0x%llx sentinel=0x%llx -> %s",
                  static_cast<unsigned long long>(afterSyscall),
                  static_cast<unsigned long long>(afterDelivery),
                  static_cast<unsigned long long>(sentinel),
                  afterSyscall != sentinel ? "syscall path"
                                           : (afterDelivery == sentinel ? "preserved"
                                                                        : "delivery zeroes it"));
    kudroid_android_log_message(4, "KuDroidProbe", msg);
    char line[224];
    const int n = std::snprintf(line, sizeof(line), "[KuDroidProbe] %s\n", msg);
    if (n > 0) (void)::write(STDERR_FILENO, line, static_cast<size_t>(n));
}
#else
void kudroid_probe_platform_register(void) {}
#endif

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
        signal_safe_log("KuDroidTrap", line);
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
