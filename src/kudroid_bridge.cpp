#include "kudroid/kudroid_bridge.h"
#include "kudroid/DeviceProfile.h"
#include "kudroid/elf_loader.hpp"
#include "kudroid/BionicShim.h"
#include "kudroid/VFSPathRemapper.h"
#include "kudroid/APKExtractor.h"
#include "kudroid/platform/InputShim.h"
#include "kudroid/platform/AssetShim.h"
#include "kudroid/PermissionManager.h"
#include "kudroid/KuArtRuntime.h"
#include "kudroid/platform/JavaCanvasRenderer.h"
#include "kudroid/NativeCallTelemetry.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <csignal>
#include <ctime>
#include <cctype>
#include <cerrno>
#include <setjmp.h>
#include <filesystem>
#include <set>
#include <string>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ucontext.h>
#include <dlfcn.h>
#include <unwind.h>
#include <mutex>
#include <atomic>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/task.h>
#endif

extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);
extern "C" void kudroid_persistent_breadcrumb(const char* line);

extern "C" void kudroid_ios_diagnostic_phase(const char* phase) {
#if defined(__APPLE__)
    char line[512];
    snprintf(line, sizeof(line), "ios-phase=%s pid=%d", phase ? phase : "?", getpid());
    kudroid_persistent_breadcrumb(line);
#else
    (void)phase;
#endif
}

extern "C" void kudroid_ios_diagnostic_memory(const char* phase) {
#if defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    const kern_return_t kr = task_info(mach_task_self(), TASK_VM_INFO,
                                       reinterpret_cast<task_info_t>(&info), &count);
    if (kr == KERN_SUCCESS) {
        char line[768];
        snprintf(line, sizeof(line),
                 "ios-memory phase=%s pid=%d phys_footprint=%llu resident=%llu virtual=%llu",
                 phase ? phase : "?", getpid(),
                 static_cast<unsigned long long>(info.phys_footprint),
                 static_cast<unsigned long long>(info.resident_size),
                 static_cast<unsigned long long>(info.virtual_size));
        kudroid_persistent_breadcrumb(line);
    }
#else
    (void)phase;
#endif
}

// ── Continuous logging into the app's writable directory ─────────────────────
// The host passes one directory (its Documents) through kudroid_set_log_dir().
// Normal logs are written as files; crashes are signal-based, so no C++ exception
// fires and the signal handler is what catches them — it flushes the log buffer to
// disk using only async-signal-safe calls before re-raising.
//
// Two directories, deliberately distinct:
//
//   g_docsDir - the app's Documents. Where installed apps, extracted APKs and the
//               VFS root live; the host hands this in.
//   g_logDir  - g_docsDir + "/logs". Every diagnostic file goes here.
//
// They used to be the same, so eight log files sat among put_apk_here/, android_root/,
// micro_tests/ and extracted_apk/ at the top level of Documents. Nothing about that
// was wrong, only unreadable — and it made "pull the logs" a matter of knowing which
// eight of a dozen entries were logs.
static char g_docsDir[1024] = {0};
static char g_logDir[1024] = {0};
const char* g_kudroid_log_dir_ptr = g_logDir;

// A crash buffer is useful for faults handled by KuDroid, but SIGKILL skips
// every handler.  This small append-only journal is therefore flushed after
// each breadcrumb.  It is intentionally independent of the C++ log mutex: the
// last completed write remains useful even when a thread is blocked elsewhere.
extern "C" void kudroid_persistent_breadcrumb(const char* line) {
    if (!line || !g_logDir[0]) return;
    char path[sizeof(g_logDir) + 32];
    const int n = snprintf(path, sizeof(path), "%s/native_breadcrumbs.log", g_logDir);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(path)) return;
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    struct timespec now;
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    char record[2304];
    const int record_len = snprintf(record, sizeof(record), "t_ns=%lld %s\n",
                                    static_cast<long long>(now.tv_sec) * 1000000000LL +
                                        now.tv_nsec, line);
    if (record_len <= 0) {
        (void)::close(fd);
        return;
    }
    const size_t len = static_cast<size_t>(record_len) < sizeof(record)
                           ? static_cast<size_t>(record_len) : sizeof(record) - 1;
    (void)::write(fd, record, len);
    (void)::fsync(fd);
#if defined(__APPLE__) && defined(F_FULLFSYNC)
    (void)::fcntl(fd, F_FULLFSYNC, 0);
#endif
    (void)::close(fd);
}

// Previously 16KB was too small: ELF loading and verbose lines filled the
// buffer, pushing crucial pre-crash context (e.g. EGL initialization) out.
// Expanded to 256KB; static allocation ensures signal-handler safety without heap usage.
static char g_crashBuf[262144];
static volatile sig_atomic_t g_crashLen = 0;
static std::mutex g_crashBufMtx;
static char g_abortMessage[1024] = {0};

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
static int kudroid_call_jni_onload_guarded(jint (*fn)(JavaVM*, void*),
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

static int kudroid_jit_available(void);
// Build stamp — forward declaration used in appendTestHeader.
extern "C" const char* kudroid_build_stamp(void);

// Gentle crash variables:
static std::atomic<bool> g_hasCrashed{false};
static char g_lastCrashTail[16384] = {0};
static pthread_t g_mainThread = 0;

static void extractLastLines(const char* src, size_t srcLen, char* dst, size_t dstCap, int maxLines = 30) {
    if (!src || srcLen == 0 || dstCap == 0) {
        if (dstCap > 0) dst[0] = '\0';
        return;
    }
    int lineCount = 0;
    const char* p = src + srcLen - 1;
    while (p >= src && (*p == '\n' || *p == '\r')) p--;
    const char* end = p + 1;
    while (p >= src) {
        if (*p == '\n') {
            lineCount++;
            if (lineCount >= maxLines) {
                p++;
                break;
            }
        }
        p--;
    }
    if (p < src) p = src;
    size_t len = static_cast<size_t>(end - p);
    if (len >= dstCap) len = dstCap - 1;
    memcpy(dst, p, len);
    dst[len] = '\0';
}

// Guest Android logs (bionic_android_log_print) mirrored here to provide
// immediate pre-crash diagnostic context in a ring buffer.
extern "C" void kudroid_append_crash_log(const char* text, size_t len) {
    if (!text || len == 0) return;
    if (len > 8192) len = 8192; // single line length limit
    std::lock_guard<std::mutex> lock(g_crashBufMtx);
    size_t cur = static_cast<size_t>(g_crashLen);
    const size_t cap = sizeof(g_crashBuf) - 1;
    if (len > cap - cur) {
        const size_t drop = len - (cap - cur);
        if (drop < cur) {
            memmove(g_crashBuf, g_crashBuf + drop, cur - drop);
            cur -= drop;
        } else {
            cur = 0;
        }
    }
    memcpy(g_crashBuf + cur, text, len);
    cur += len;
    g_crashBuf[cur] = '\0';
    g_crashLen = static_cast<sig_atomic_t>(cur);
}

// android_set_abort_message: capture guest abort message for crash reporting.
extern "C" void kudroid_store_abort_message(const char* msg) {
    if (!msg) return;
    strncpy(g_abortMessage, msg, sizeof(g_abortMessage) - 1);
    g_abortMessage[sizeof(g_abortMessage) - 1] = '\0';
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

static void mirrorCrash(const std::string& log) {
    // Synchronized with kudroid_append_crash_log mutex to prevent data races
    // with active guest logging threads.
    std::lock_guard<std::mutex> lock(g_crashBufMtx);
    size_t n = log.size();
    if (n >= sizeof(g_crashBuf)) n = sizeof(g_crashBuf) - 1;
    memcpy(g_crashBuf, log.data(), n);
    g_crashBuf[n] = '\0';
    g_crashLen = (sig_atomic_t)n;
}

static void writeLogFile(const char* name, const std::string& content) {
    if (!g_logDir[0]) return;
    std::string path = std::string(g_logDir) + "/" + name;
    FILE* f = fopen(path.c_str(), "w");
    if (f) {
        fwrite(content.data(), 1, content.size(), f);
        fclose(f);
    }
}

// Log a line that already carries a "[kudroid_core] " prefix for stderr.
//
// logAndroidMessage prepends the TAG, so passing a line that spells the same origin
// out again produced "[KuDroidCore] [kudroid_core] ..." in kudroid_android_logs.txt
// and the KDB stream. The prefix is worth keeping on stderr, which has no tag column,
// so it is stripped here rather than removed from the call sites.
//
// One tag for the whole file: it used to be "KuDroidCore" from the loader and
// "kudroid_core" from the JNI_OnLoad path, so filtering the log by origin missed half
// the lines.
static void logCoreLine(int priority, const std::string& line) {
    static constexpr char kPrefix[] = "[kudroid_core] ";
    static constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
    const char* text = line.c_str();
    if (line.compare(0, kPrefixLen, kPrefix) == 0) text += kPrefixLen;
    kudroid_android_log_message(priority, "kudroid_core", text);
}

// Safely capture crash buffer snapshot under mutex lock to provide
// comprehensive diagnostic logs for tests.
extern "C" const char* kudroid_crash_log_snapshot(void) {
    std::lock_guard<std::mutex> lock(g_crashBufMtx);
    const size_t n = static_cast<size_t>(g_crashLen);
    char* out = static_cast<char*>(std::malloc(n + 1));
    if (!out) return nullptr;
    std::memcpy(out, g_crashBuf, n);
    out[n] = '\0';
    return out;
}

// Append crash buffer snapshot to test execution logs.
static void appendCrashSnapshot(std::string& log, const char* sectionName) {
    const char* snap = kudroid_crash_log_snapshot();
    if (!snap) return;
    if (*snap) {
        log += std::string("\n--- ") + sectionName + " ---\n";
        log += snap;
    }
    std::free(const_cast<char*>(snap));
}

static void appendTestHeader(std::string& log, const char* test, const char* path) {
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

    // Build stamp (version + commit hash) prepended to test logs for IPA build identification.
    log += "[kudroid_core] Build: " + std::string(kudroid_build_stamp()) + "\n";
    log += "[kudroid_core] ===== " + std::string(test) + " =====\n";
    log += "[kudroid_core] Timestamp: " + oss.str() + "\n";
    if (path && std::strcmp(path, "N/A") != 0) {
        log += "[kudroid_core] Path: " + std::string(path) + "\n";
    }
#if defined(__APPLE__)
    log += "[kudroid_core] JIT: " +
           std::string(kudroid_jit_available() ? "Enabled" : "Disabled") + "\n";
#endif
}

// Build stamp: distinguishes whether the currently running IPA is the latest version.
// Compare this stamp in kudroid_crash.log against CI build stamps to verify
// the installed build version.
extern "C" const char* kudroid_build_stamp(void) {
    static const char kStamp[] =
        "kudroid_core v0.8.0 " __DATE__ " " __TIME__ " "
#ifdef KUDROID_GIT_HASH
        KUDROID_GIT_HASH
#else
        "(no-git-hash)"
#endif
        ;
    return kStamp;
}

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
static std::string describeJniGuardFault(const std::string& library, int guardRc) {
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
    // faulting instruction. Range-checked at every step: a bad fp must not turn a
    // survivable fault into a real crash.
    out += "--- fp chain ---\n";
    {
        uint64_t fp = f.fp;
        for (int depth = 0; depth < 24; ++depth) {
            if (!(fp > 0x1000 && fp < 0x7fffffffffffULL)) break;
            const uint64_t* p = reinterpret_cast<const uint64_t*>(fp);
            const uint64_t savedFp = p[0];
            const uint64_t savedLr = p[1];
            if (savedLr == 0) break;
            char sym[512];
            symbolicateAddr(static_cast<uintptr_t>(savedLr), sym, sizeof(sym));
            snprintf(line, sizeof(line), "  #%02d lr=0x%llx  %s\n", depth,
                     (unsigned long long)savedLr, sym);
            out += line;
            if (!(savedFp > fp && savedFp < 0x7fffffffffffULL)) break;
            fp = savedFp;
        }
    }
#endif

    return out;
}

// Append to kudroid_crash.log rather than overwrite: several libraries can fault in
// one launch, and crashHandler truncates the file for a real crash. Losing the
// earlier faults would hide the first one, which is usually the informative one.
static void appendCrashLogFile(const std::string& content) {
    if (!g_logDir[0]) return;
    const std::string path = std::string(g_logDir) + "/kudroid_crash.log";
    FILE* fp = fopen(path.c_str(), "a");
    if (fp == nullptr) return;
    fwrite(content.data(), 1, content.size(), fp);
    fclose(fp);
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
static void chainToPreviousHandler(int sig) {
    if (sig <= 0 || sig >= NSIG) return;
    const struct sigaction& previous = g_previousHandlers[sig];
    const bool hasHandler =
        (previous.sa_flags & SA_SIGINFO) ? previous.sa_sigaction != nullptr
                                         : (previous.sa_handler != nullptr &&
                                            previous.sa_handler != SIG_DFL &&
                                            previous.sa_handler != SIG_IGN);
    if (!hasHandler) return;
    sigaction(sig, &previous, nullptr);
    raise(sig);
}

static void crashHandler(int sig, siginfo_t* info, void* ucontext) {
    if (sig == SIGTRAP) {
        if (kudroid::bionic_handle_tpidr_trap(ucontext)) {
            return; // handled successfully, resuming execution!
        }
        // A SIGTRAP that is not one of KuDroid's TLS breakpoints belongs to whoever
        // installed a handler before us — a debugger bridge, a crash reporter, or
        // LiveContainer's own dyld interception, all of which use breakpoints for
        // their own purposes. Reporting it as a KuDroid crash would both lose their
        // event and produce a misleading log.
        chainToPreviousHandler(sig);
    }

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
            // Only read the stack when sp looks like a mapped address: a
            // double fault inside the handler would take down the process for
            // real, which is exactly what the guard exists to avoid.
            if (f.sp > 0x1000 && f.sp < 0x7fffffffffffULL) {
                const uint64_t* stack = reinterpret_cast<const uint64_t*>(f.sp);
                for (int i = 0; i < 32; ++i) f.stack[i] = stack[i];
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

    // Flush stdout/stderr streams to ensure buffered diagnostic messages
    // are not lost before termination.
    fflush(stdout);
    fflush(stderr);
    
    if (g_logDir[0]) {
        // construct '<dir>/kudroid_crash.log' path without heap allocation.
        char path[1200];
        size_t dl = strlen(g_logDir);
        if (dl >= sizeof(path) - 32) dl = sizeof(path) - 32;
        memcpy(path, g_logDir, dl);
        const char* suffix = "/kudroid_crash.log";
        memcpy(path + dl, suffix, strlen(suffix) + 1);

        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
            }

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
                uint64_t x0 = uc->uc_mcontext->__ss.__x[0];
                uint64_t x1 = uc->uc_mcontext->__ss.__x[1];
                uint64_t x2 = uc->uc_mcontext->__ss.__x[2];
                uint64_t x3 = uc->uc_mcontext->__ss.__x[3];
                uint64_t x4 = uc->uc_mcontext->__ss.__x[4];
                uint64_t x5 = uc->uc_mcontext->__ss.__x[5];
                uint64_t x6 = uc->uc_mcontext->__ss.__x[6];
                uint64_t x7 = uc->uc_mcontext->__ss.__x[7];
                uint64_t x8 = uc->uc_mcontext->__ss.__x[8];
                m = snprintf(sigline, sizeof(sigline),
                    "pc = 0x%llx\nlr = 0x%llx\nsp = 0x%llx\nfp = 0x%llx\ncpsr = 0x%llx\n"
                    "x0 = 0x%llx\nx1 = 0x%llx\nx2 = 0x%llx\nx3 = 0x%llx\n"
                    "x4 = 0x%llx\nx5 = 0x%llx\nx6 = 0x%llx\nx7 = 0x%llx\nx8 = 0x%llx\n",
                    (unsigned long long)pc, (unsigned long long)lr, (unsigned long long)sp,
                    (unsigned long long)fp, (unsigned long long)cpsr,
                    (unsigned long long)x0, (unsigned long long)x1, (unsigned long long)x2,
                    (unsigned long long)x3, (unsigned long long)x4, (unsigned long long)x5,
                    (unsigned long long)x6, (unsigned long long)x7, (unsigned long long)x8);
                crashWriteLine(fd, sigline, m, sizeof(sigline));

                // Symbolicate PC/LR for meaningful source/function attribution.
                char symPc[512], symLr[512];
                symbolicateAddr((uintptr_t)pc, symPc, sizeof(symPc));
                symbolicateAddr((uintptr_t)lr, symLr, sizeof(symLr));
                m = snprintf(sigline, sizeof(sigline), "pc_sym: %s\nlr_sym: %s\n", symPc, symLr);
                crashWriteLine(fd, sigline, m, sizeof(sigline));

                // Raw stack dump from faulting SP to recover caller frames and register state.
                (void)!write(fd, "\n--- stack from sp ---\n", 22);
                {
                    const uint64_t* stack = reinterpret_cast<const uint64_t*>(sp);
                    for (int i = 0; i < 128; i += 4) {
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
                // Validate frame pointers within bounded ranges to prevent secondary faults.
                // slot56 (guard) — ch dump raw, decode offline.
                (void)!write(fd, "\n--- fp chain ---\n", 17);
                {
                    uint64_t f = fp;
                    for (int i = 0; i < 32; ++i) {
                        if (!(f > 0x1000 && f < 0x7fffffffffffULL)) break;
                        const uint64_t* p = reinterpret_cast<const uint64_t*>(f);
                        const uint64_t savedFp = p[0];
                        const uint64_t savedLr = p[1];
                        const uint64_t slot56  = p[7]; // [fp+56]
                        char lrMod[256] = {0};
                        const bool inGuest =
                            kudroid::kudroid_lookup_guest_module(
                                reinterpret_cast<void*>(savedLr), lrMod, sizeof(lrMod));
                        // Nu no/not thuc guest ELF th y l code HOST — gm c
                        // Avian (link tnh vo KuDroidShell). before y ch in raw
                        // "lr=0x104e62934?" nn mi abort ca Avian u v danh v
                        // phi on. symbolicateAddr c sn symbol table (function static
                        // nh crashHandler vn ra tn) → in lun tn function y 
                        // l do abort hin ra ngay trong crash log, no/not cn atos.
                        char lrSym[512];
                        if (!inGuest) {
                            symbolicateAddr((uintptr_t)savedLr, lrSym, sizeof(lrSym));
                        }
                        const bool guardLike =
                            slot56 > 0x100000000ULL && slot56 < 0x7fffffffffffULL;
                        int n2 = snprintf(sigline, sizeof(sigline),
                            "fp%02d: f=0x%llx lr=0x%llx %s slot56=0x%llx%s\n",
                            i, (unsigned long long)f, (unsigned long long)savedLr,
                            inGuest ? lrMod : lrSym, (unsigned long long)slot56,
                            guardLike ? " <-- guard?" : "");
                        crashWriteLine(fd, sigline, n2, sizeof(sigline));
                        // Chain hp l: frame k cao hn, delta ≤ 64KB.
                        if (!(savedFp > f && savedFp - f < 0x10000)) break;
                        f = savedFp;
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

            // Dump ui stderr (l do abort/fatal ca KuART — thng in ra y
            // nhng b mt v iOS no/not hin th stderr). open/lseek/read/write
            // u async-signal-safe nn call c trong signal handler.
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

    // --- GENTLE CRASH SYSTEM ---
    // nh du trng thi crash v trch xut ti a 30 dng log cui cng
    // UI Swift hin th cnh bo "Whoops, the app crashed" m no/not ng app.
    g_hasCrashed.store(true);
    
    // Thu thp 30 dng log cui cng t g_crashBuf
    extractLastLines(g_crashBuf, (size_t)g_crashLen, g_lastCrashTail, sizeof(g_lastCrashTail), 30);
    
    // Nu g_lastCrashTail qu ngn, ghp thm thng tin signal v PC
    if (strlen(g_lastCrashTail) < 30) {
        char fallbackSummary[512];
        snprintf(fallbackSummary, sizeof(fallbackSummary),
                 "[Crash Signal: %d] Fault at %p (Thread 0x%llx)",
                 sig, info ? info->si_addr : nullptr, (unsigned long long)(uintptr_t)pthread_self());
        strncat(g_lastCrashTail, fallbackSummary, sizeof(g_lastCrashTail) - strlen(g_lastCrashTail) - 1);
    }

    // Nu crash xy ra trn background thread (render thread, game thread, worker thread):
    // no/not call function lock mutex trong signal handler trnh deadlock/freeze!
#if defined(__APPLE__)
    const bool isBackground = !pthread_main_np();
#else
    const bool isBackground = g_mainThread != 0 && !pthread_equal(pthread_self(), g_mainThread);
#endif
    if (isBackground) {
        // thread background b crash: gii phng v tm stop thread ny Swift timer pht hin crash
        // v t ng hin th Gentle Crash modal m no/not lm freeze/treo launcher.
        pause();
    } else {
        // Nu crash ngay trn main thread, write nhn v kt thc an ton
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

static void installCrashHandlers(void) {
    static bool installed = false;
    if (installed) return;
    installed = true;
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crashHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    // run handler trn stack ring: khi crash l stack overflow (hoc stack ca
    // JVM thread qu nh), handler run trn stack hng s double-fault v
    // no/not write c crash log — ng triu chng "app cht im lng". Cp stack
    // tnh (no/not heap, an ton trong signal context) ri bt SA_ONSTACK.
    {
        // SIGSTKSZ no/not cn l hng bin dch trn glibc mi → dng kch thc
        // c nh (64KB, tha cho handler ny) mng static hp l mi libc.
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
    const int kSignals[] = {SIGILL, SIGBUS, SIGSEGV, SIGTRAP, SIGABRT};
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

    // Android/bionic mc nh IGNORE SIGPIPE (write vo pipe/socket ng tr
    // EPIPE thay v git process). Game .so tin vo hnh vi ny — nu host gi
    // SIGPIPE mc nh, ch cn game write log vo mt pipe ng l app cht
    // ngay m no/not c crash log.
    ::signal(SIGPIPE, SIG_IGN);

#if defined(__APPLE__)
    // Bt l do abort before khi n xy ra: ObjC exception cha bt + C++ terminate.
    NSSetUncaughtExceptionHandler(&kudroid_uncaught_objc_handler);
    std::set_terminate(&kudroid_terminate_handler);
#endif
}

extern "C" void kudroid_set_log_dir(const char* dir) {
    if (!dir) return;
    g_mainThread = pthread_self();

    // The host passes Documents; logs go into a subdirectory of it. Keeping the two
    // apart is the whole point — Documents also holds put_apk_here/, android_root/,
    // extracted_apk/ and micro_tests/, and eight log files scattered among them made
    // the directory hard to read and "which files are the logs" a thing to remember.
    strncpy(g_docsDir, dir, sizeof(g_docsDir) - 1);
    g_docsDir[sizeof(g_docsDir) - 1] = '\0';

    snprintf(g_logDir, sizeof(g_logDir), "%s/logs", g_docsDir);

    // Must exist before anything opens a file in it. The crash handler in particular
    // cannot create directories — it is restricted to async-signal-safe calls — so a
    // missing logs/ would silently lose every crash report.
    std::error_code mkdirError;
    std::filesystem::create_directories(g_logDir, mkdirError);
    if (mkdirError) {
        // Fall back to Documents rather than losing logs entirely. A cluttered
        // directory beats no diagnostics.
        fprintf(stderr, "[kudroid_core] cannot create %s (%s); logging to Documents\n",
                g_logDir, mkdirError.message().c_str());
        strncpy(g_logDir, g_docsDir, sizeof(g_logDir) - 1);
        g_logDir[sizeof(g_logDir) - 1] = '\0';
    }

    installCrashHandlers();

    static bool s_logDirInitialized = false;
    if (!s_logDirInitialized) {
        s_logDirInitialized = true;
        // Truncate the log exactly once per KuDroid launch. Pressing X to return to
        // the launcher and starting another app must keep what came before, or the
        // log of the run that just failed is gone before it can be read.
        char aPath[1200];
        snprintf(aPath, sizeof(aPath), "%s/kudroid_android_logs.txt", g_logDir);
        FILE* afp = fopen(aPath, "w");
        if (afp) {
            // The FIRST line of the main log is always the build stamp, so which
            // commit the running IPA was built from is visible without opening a
            // separate version file.
            fprintf(afp, "[kudroid_core] Build: %s\n", kudroid_build_stamp());
            fclose(afp);
        }

        // Old runs wrote these straight into Documents. Move them aside so the top
        // level ends up clean even for someone upgrading, rather than leaving a
        // confusing mix of stale files next to the new logs/ directory.
        {
            static const char* kMoved[] = {
                "kudroid_android_logs.txt", "stderr.log", "kudroid_crash.log",
                "classes.log", "kudroid_version.txt", "kudroid_uninstall_debug.txt",
            };
            for (const char* name : kMoved) {
                const std::filesystem::path from =
                    std::filesystem::path(g_docsDir) / name;
                std::error_code ec;
                if (!std::filesystem::exists(from, ec)) continue;
                const std::filesystem::path to =
                    std::filesystem::path(g_logDir) / (std::string("old_") + name);
                std::filesystem::rename(from, to, ec);
                if (ec) std::filesystem::remove(from, ec);
            }
        }

#if defined(__APPLE__)
        // Redirect stderr (fd 2) into a file. O_TRUNC once per launch, for the same
        // reason as above.
        char errPath[1200];
        snprintf(errPath, sizeof(errPath), "%s/stderr.log", g_logDir);
        int errFd = open(errPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (errFd >= 0) {
            dup2(errFd, STDERR_FILENO);
            setvbuf(stderr, nullptr, _IONBF, 0);
            close(errFd);
            // AFC (copying files over USB/Finder) can only read a file that is
            // readable by other, and umask may have masked 0644 down to 0600.
            ::chmod(errPath, 0644);
            // First line of stderr.log is the build stamp too: every log file
            // identifies the commit of the IPA that produced it.
            fprintf(stderr, "[kudroid_core] Build: %s\n", kudroid_build_stamp());
            fprintf(stderr, "[kudroid_core] log directory: %s\n", g_logDir);
        }
#endif
    }

    // Write the build stamp to its own file so the running version can be checked
    // without reading a log, which answers "is the iPhone still on the old build?".
    const char* stamp = kudroid_build_stamp();
    writeLogFile("kudroid_version.txt", std::string(stamp) + "\n");

    // KuART's classes.log belongs with the other diagnostics, not in Documents.
    std::string classesLogPath = (std::filesystem::path(g_logDir) / "classes.log").string();
    kuart_set_missing_class_log_path(classesLogPath.c_str());
}

extern "C" void kudroid_set_documents_dir(const char* dir) {
    if (dir && dir[0] != '\0') {
        kudroid::VFSPathRemapper::getInstance().setDocumentsDirectory(dir);
        kudroid::PermissionManager::getInstance().init(dir);

        if (g_docsDir[0] == '\0') {
            strncpy(g_docsDir, dir, sizeof(g_docsDir) - 1);
            g_docsDir[sizeof(g_docsDir) - 1] = '\0';
        }

        // classes.log is a diagnostic, so it follows the log directory rather than
        // Documents. This used to point it back at Documents and undo whatever
        // kudroid_set_log_dir had chosen — the two functions disagreed and the last
        // one called won.
        if (g_logDir[0] != '\0') {
            const std::string classesLogPath =
                (std::filesystem::path(g_logDir) / "classes.log").string();
            kuart_set_missing_class_log_path(classesLogPath.c_str());
        }
    }
}

// pointer lp metal ton cc c th truy cp bng bionicshim
void* g_metalLayer = nullptr;
int g_metalLayerWidth = 1080;
int g_metalLayerHeight = 1920;
float g_metalLayerDensity = 3.0f;

// i qua pipeline log chun (stdout + kudroid_android_logs.txt + crash buffer)
// — nh ngha trong SyscallShim.cpp, dng chung vi GraphicsShim.
extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);

// GraphicsShim.cpp — ANGLE first-touch phi trn main thread (xem comment 
// kudroid_set_metal_layer).
extern "C" void kudroid_gpu_warmup_egl(void);
extern "C" void* bionic_ANativeWindow_fromSurface(void* env, void* surface);

extern "C" void kudroid_set_metal_layer(void* layer, int width, int height, float density) {
    g_metalLayer = layer;
    g_metalLayerWidth = width;
    g_metalLayerHeight = height;
    g_metalLayerDensity = density > 0.0f ? density : g_metalLayerDensity;
    // Log kch thc nhn t Swift — before y no/not log g nn width/height
    // sai (0/m) ch l ra ANativeWindow_lock (hoc no/not bao gi nu game
    // no/not dng lock).
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "kudroid_set_metal_layer(layer=%p, size=%dx%d, density=%.2f)",
                  layer, width, height, density);
    kudroid_android_log_message(2, "KuDroidGPU", buf);

    // Resize the software canvas to the real surface. The Java-side Canvas asks for
    // these numbers via native_getSurfaceWidth/Height, so layout is computed for the
    // actual screen instead of the old hardcoded 1080x1920.
    if (width > 0 && height > 0) {
        kudroid::JavaCanvasRenderer::getInstance().init(width, height);
    }

    // render thread t khi create EGL display sch s t u
    // kudroid_gpu_warmup_egl();
}

extern "C" const char* kudroid_vfs_self_test_log(void) {
    const std::string log = kudroid::run_vfs_self_test();
    writeLogFile("kudroid_vfs_selftest.txt", log);
    char* result = static_cast<char*>(std::malloc(log.size() + 1));
    if (result) std::memcpy(result, log.c_str(), log.size() + 1);
    return result;
}

extern "C" const char* kudroid_vfs_extended_test_log(void) {
    const std::string log = kudroid::run_vfs_extended_test();
    writeLogFile("kudroid_vfs_extended_test.txt", log);
    char* result = static_cast<char*>(std::malloc(log.size() + 1));
    if (result) std::memcpy(result, log.c_str(), log.size() + 1);
    return result;
}

extern "C" const char* kudroid_install_apk(const char* apkPath) {
    std::string log = "[kudroid_core] ===== APK Install =====\n";
    if (!apkPath || !*apkPath) {
        log += "[kudroid_apk] ERROR: APK path is empty\n";
    } else {
        const std::filesystem::path source(apkPath);
        std::string pkgId = kudroid::APKExtractor::get_package_name(source.string());
        if (pkgId.empty()) {
            pkgId = source.stem().string();
            auto uIdx = pkgId.find('_');
            if (uIdx != std::string::npos) {
                pkgId = pkgId.substr(0, uIdx);
            }
        }
        for (char& character : pkgId) {
            if (!(std::isalnum(static_cast<unsigned char>(character)) || character == '_' || character == '-' || character == '.')) {
                character = '_';
            }
        }
        auto& remapper = kudroid::VFSPathRemapper::getInstance();
        const std::filesystem::path appDir = std::filesystem::path(remapper.androidRoot()) /
                                             "data/app" / pkgId;
        log += "[kudroid_apk] APK: " + source.string() + "\n";
        log += "[kudroid_apk] Target Android Package: " + pkgId + "\n";
        log += "[kudroid_apk] Target extraction directory: " + appDir.string() + "\n";
        bool extractedOk = false;
        if (kudroid::APKExtractor::is_bundle_container(source.string())) {
            log += "[kudroid_apk] Split-APK bundle detected (.xapk/.apks/.apkm), merging splits...\n";
            extractedOk = kudroid::APKExtractor::extract_bundle(source.string(), appDir.string());
        } else {
            extractedOk = kudroid::APKExtractor::extract_apk(source.string(), appDir.string());
        }
        if (extractedOk) {
            // pkgId pha trn on t tn file APK khi get_package_name() no/not read
            // c manifest (bundle container: manifest tht nm trong base APK bn
            // trong, no/not top level). after khi gii nn, app_info.json cha
            // package tht do parseAxml ca base manifest → dng n lm ngun duy
            // nht v i tn th mc, data/app v dalvik-cache lun theo
            // package ID ch no/not phi tn file APK.
            std::string effectiveAppName = pkgId;
            std::filesystem::path effectiveAppDir = appDir;
            {
                std::ifstream infoFile(appDir / "app_info.json");
                std::string line;
                while (std::getline(infoFile, line)) {
                    const auto pos = line.find("\"package\": \"");
                    if (pos == std::string::npos) continue;
                    const auto start = pos + 12;
                    const auto end = line.find('"', start);
                    if (end == std::string::npos) break;
                    const std::string realPkg = line.substr(start, end - start);
                    if (realPkg.empty() || realPkg == pkgId) break;
                    const std::filesystem::path target =
                        std::filesystem::path(remapper.androidRoot()) / "data/app" / realPkg;
                    std::error_code renameEc;
                    std::filesystem::remove_all(target, renameEc);
                    std::filesystem::rename(appDir, target, renameEc);
                    if (!renameEc) {
                        effectiveAppName = realPkg;
                        effectiveAppDir = target;
                        log += "[kudroid_apk] Normalized install dir to package ID: " + realPkg + "\n";
                    }
                    break;
                }
            }

            log += "[kudroid_apk] APK extracted successfully to " + effectiveAppName + "\n";
        } else {
            log += "[kudroid_apk] INSTALL FAILED: " +
                   kudroid::APKExtractor::lastError() + "\n";
        }
    }
    writeLogFile("kudroid_apk_install.txt", log);
    char* result = static_cast<char*>(std::malloc(log.size() + 1));
    if (result) std::memcpy(result, log.c_str(), log.size() + 1);
    return result;
}

#if defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#include <libkern/OSCacheControl.h>
#include <TargetConditionals.h>

// csops() l mt api ring t nhng n nh; c s dng read trng thi ch k m ca qu trnh.
// cs_debugged c t khi ng dn jit (k m ng) hot ng
// di livecontainer / trnh g error, iu ny cho php cc trang prot_exec run.
extern "C" int csops(pid_t pid, unsigned int ops, void* useraddr, size_t usersize);
#ifndef CS_OPS_STATUS
#define CS_OPS_STATUS 0
#endif
#ifndef CS_DEBUGGED
#define CS_DEBUGGED 0x10000000
#endif
#endif

// return 1 nu jit (memory c th execute) c v kh dng, ngc li return 0.
extern "C" int kudroid_is_jit_enabled(void) {
#if defined(__APPLE__)
    // Ask the kernel: map a page and try to make it executable.
    //
    // This is not one signal among several — it is the question itself. Every route that
    // grants the permission (a TrollStore install, an attached debugger, the allow-jit
    // entitlement, LiveContainer's JIT mode) ends in the same place: mprotect(PROT_EXEC)
    // succeeds. And if it fails, the process cannot execute memory it wrote, whatever any
    // other indicator says.
    //
    // So a negative result is returned as-is. An earlier version consulted the proxies
    // below after a negative probe, on the theory that a false negative would wrongly
    // refuse to launch. That was backwards: mprotect cannot report a false negative for
    // the operation it just performed, while the proxies report false POSITIVES readily —
    // /Applications/TrollStore.app exists whenever TrollStore is installed on the device,
    // which says nothing about how THIS app was signed. The result was an app sideloaded
    // without JIT reporting "JIT: Enabled" on a device that happens to have TrollStore.
    //
    // Cached: code-signing status is fixed at exec, and this is asked repeatedly.
    static const int probed = [] {
        const size_t len = static_cast<size_t>(getpagesize());
        void* page = ::mmap(nullptr, len, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANON, -1, 0);
        if (page == MAP_FAILED) return -1;  // out of memory: no answer, not a "no"
        const int rc = ::mprotect(page, len, PROT_READ | PROT_EXEC);
        ::munmap(page, len);
        return rc == 0 ? 1 : 0;
    }();
    if (probed >= 0) return probed;

    // Only reached when the probe could not run at all, which means the process could not
    // allocate one page. The proxies are guesses, used here because a guess beats nothing.

    // CS_DEBUGGED: a debugger is attached (AltStore, SideStore, Sideloadly, Xcode,
    // StikDebug, Jitterbug) — the state that permits RW -> RX.
    unsigned int flags = 0;
    if (csops(getpid(), CS_OPS_STATUS, &flags, sizeof(flags)) == 0) {
        if (flags & CS_DEBUGGED) {
            return 1;
        }
    }

    // TrollStore installs are signed with unrestricted entitlements. Checked through paths
    // that are safe to stat; anything privileged here would risk SIGKILL. Note this only
    // establishes that TrollStore exists on the device, so it is deliberately last and
    // only consulted when the kernel could not be asked.
    if (access("/Applications/TrollStore.app", F_OK) == 0 ||
        access("/var/mobile/Library/TrollStore", F_OK) == 0 ||
        getenv("TROLLSTORE_ENABLED") != nullptr) {
        return 1;
    }
    return 0; // Hon ton no/not c JIT!
#else
    return 1;
#endif
}

static int kudroid_jit_available(void) {
    return kudroid_is_jit_enabled();
}

extern "C" const char* kudroid_jit_status(void) {
    // Names the route, not just the verdict. "JIT: Disabled" alone left the user with
    // nothing to act on, and — while the TrollStore proxy could still produce a false
    // positive — no way to tell a real grant from a bad guess.
    const char* text = "JIT: Disabled";
#if defined(__APPLE__)
    if (kudroid_is_jit_enabled()) {
        unsigned int flags = 0;
        const bool debugged = csops(getpid(), CS_OPS_STATUS, &flags, sizeof(flags)) == 0 &&
                              (flags & CS_DEBUGGED) != 0;
        text = debugged ? "JIT: Enabled (debugger)" : "JIT: Enabled";
    }
#else
    if (kudroid_is_jit_enabled()) text = "JIT: Enabled";
#endif
    const size_t length = strlen(text) + 1;
    char* result = (char*)malloc(length);
    if (result) memcpy(result, text, length);
    return result;
}

static std::atomic<int> g_requestedOrientation{-1}; // -1=Unspecified, 0=Landscape, 1=Portrait

#if defined(__APPLE__)
extern "C" __attribute__((weak)) void kudroid_notify_orientation_change(int orientation) { (void)orientation; }
#else
extern "C" void kudroid_notify_orientation_change(int orientation) { (void)orientation; }
#endif

extern "C" void kudroid_set_requested_orientation(int orientation) {
    g_requestedOrientation.store(orientation);
    kudroid_notify_orientation_change(orientation);
    fprintf(stderr, "[KuDroidCore] Screen orientation requested: %d (%s)\n",
            orientation,
            (orientation == 0 || orientation == 6 || orientation == 8) ? "Landscape" :
            (orientation == 1 || orientation == 7 || orientation == 9) ? "Portrait" : "Sensor/Auto");
}

extern "C" int kudroid_get_requested_orientation(void) {
    return g_requestedOrientation.load();
}

extern "C" JNIEXPORT void JNICALL Java_android_app_Activity_setRequestedOrientation_1native(JNIEnv* env, jclass clazz, jint orientation) {
    (void)env; (void)clazz;
    kudroid_set_requested_orientation(orientation);
}

extern "C" void kudroid_blit_canvas_to_layer(void* layer, const void* bits, int width, int height);

static std::atomic<bool> s_isApkRunning{false};

extern "C" void kudroid_unbind_metal_layer(void) {
    g_metalLayer = nullptr;
    g_metalLayerWidth = 0;
    g_metalLayerHeight = 0;
    s_isApkRunning.store(false);
    kudroid_android_log_message(2, "KuDroidGPU", "kudroid_unbind_metal_layer: GPU surface unbound cleanly.");
}

static uint32_t* s_softwareFrameBuffer = nullptr;
static size_t s_softwareFrameBufferSize = 0;
static int s_softwareWidth = 1080;
static int s_softwareHeight = 1920;
static std::mutex s_canvasMutex;

static void ensure_software_framebuffer() {
    int w = g_metalLayerWidth > 0 ? g_metalLayerWidth : 1080;
    int h = g_metalLayerHeight > 0 ? g_metalLayerHeight : 1920;
    s_softwareWidth = w;
    s_softwareHeight = h;
    size_t needed = (size_t)w * h * sizeof(uint32_t);
    if (!s_softwareFrameBuffer || needed > s_softwareFrameBufferSize) {
        void* nb = std::realloc(s_softwareFrameBuffer, needed);
        if (nb) {
            s_softwareFrameBuffer = static_cast<uint32_t*>(nb);
            s_softwareFrameBufferSize = needed;
            std::memset(s_softwareFrameBuffer, 0, needed);
        }
    }
}

// Chuyn ARGB (Android format) sang RGBA / BGRA (iOS Metal format)
static inline uint32_t argb_to_rgba(uint32_t argb) {
    uint32_t a = (argb >> 24) & 0xFF;
    uint32_t r = (argb >> 16) & 0xFF;
    uint32_t g = (argb >> 8) & 0xFF;
    uint32_t b = (argb) & 0xFF;
    return (r) | (g << 8) | (b << 16) | (a << 24);
}

extern "C" JNIEXPORT void JNICALL Java_android_graphics_Canvas_native_1drawColor(JNIEnv* env, jclass clazz, jint color) {
    (void)env; (void)clazz;
    std::lock_guard<std::mutex> lock(s_canvasMutex);
    ensure_software_framebuffer();
    if (!s_softwareFrameBuffer) return;
    uint32_t rgba = argb_to_rgba(static_cast<uint32_t>(color));
    size_t total = (size_t)s_softwareWidth * s_softwareHeight;
    for (size_t i = 0; i < total; ++i) {
        s_softwareFrameBuffer[i] = rgba;
    }
}

extern "C" JNIEXPORT void JNICALL Java_android_graphics_Canvas_native_1drawRect(JNIEnv* env, jclass clazz, jfloat left, jfloat top, jfloat right, jfloat bottom, jint color) {
    (void)env; (void)clazz;
    std::lock_guard<std::mutex> lock(s_canvasMutex);
    ensure_software_framebuffer();
    if (!s_softwareFrameBuffer) return;

    int x0 = std::max(0, std::min(s_softwareWidth - 1, static_cast<int>(left)));
    int y0 = std::max(0, std::min(s_softwareHeight - 1, static_cast<int>(top)));
    int x1 = std::max(0, std::min(s_softwareWidth - 1, static_cast<int>(right)));
    int y1 = std::max(0, std::min(s_softwareHeight - 1, static_cast<int>(bottom)));
    uint32_t rgba = argb_to_rgba(static_cast<uint32_t>(color));

    for (int y = y0; y <= y1; ++y) {
        uint32_t* row = s_softwareFrameBuffer + y * s_softwareWidth;
        for (int x = x0; x <= x1; ++x) {
            row[x] = rgba;
        }
    }
}

extern "C" JNIEXPORT void JNICALL Java_android_graphics_Canvas_native_1drawText(JNIEnv* env, jclass clazz, jstring text, jfloat x, jfloat y, jint color, jfloat textSize) {
    (void)env; (void)clazz; (void)text; (void)textSize;
    Java_android_graphics_Canvas_native_1drawRect(env, clazz, x, y - 16.0f, x + 250.0f, y + 4.0f, color);
}

extern "C" JNIEXPORT void JNICALL Java_android_graphics_Canvas_native_1drawBitmap(JNIEnv* env, jclass clazz, jintArray pixels, jint width, jint height, jfloat x, jfloat y) {
    (void)env; (void)clazz;
    if (!pixels || width <= 0 || height <= 0) return;
    std::lock_guard<std::mutex> lock(s_canvasMutex);
    ensure_software_framebuffer();
    if (!s_softwareFrameBuffer) return;

    jint* src = env->GetIntArrayElements(pixels, nullptr);
    if (!src) return;

    int dstX = static_cast<int>(x);
    int dstY = static_cast<int>(y);

    for (int r = 0; r < height; ++r) {
        int py = dstY + r;
        if (py < 0 || py >= s_softwareHeight) continue;
        for (int c = 0; c < width; ++c) {
            int px = dstX + c;
            if (px < 0 || px >= s_softwareWidth) continue;
            uint32_t pixel = static_cast<uint32_t>(src[r * width + c]);
            s_softwareFrameBuffer[py * s_softwareWidth + px] = argb_to_rgba(pixel);
        }
    }

    env->ReleaseIntArrayElements(pixels, src, JNI_ABORT);
}

extern "C" JNIEXPORT void JNICALL Java_android_graphics_Canvas_native_1flush(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    std::lock_guard<std::mutex> lock(s_canvasMutex);
    if (g_metalLayer && s_softwareFrameBuffer && s_softwareWidth > 0 && s_softwareHeight > 0) {
        kudroid_blit_canvas_to_layer(g_metalLayer, s_softwareFrameBuffer, s_softwareWidth, s_softwareHeight);
    }
}

#if defined(__APPLE__)
extern "C" __attribute__((weak)) void kudroid_trigger_haptic(int intensity) { (void)intensity; }
#else
extern "C" void kudroid_trigger_haptic(int intensity) { (void)intensity; }
#endif

extern "C" void kudroid_vibrate(int intensity) {
    kudroid_trigger_haptic(intensity);
    fprintf(stderr, "[KuDroidCore] kudroid_vibrate(intensity=%d: %s)\n",
            intensity,
            intensity == 1 ? "Light" : (intensity == 2 ? "Medium" : "Heavy"));
}

extern "C" JNIEXPORT void JNICALL Java_android_os_Vibrator_kudroid_1vibrate_1native(JNIEnv* env, jclass clazz, jint intensity) {
    (void)env; (void)clazz;
    kudroid_vibrate(intensity);
}

static std::atomic<int> s_keepScreenOn{1}; // Mc nh khi run app l 1 (No Sleep)

extern "C" void kudroid_set_keep_screen_on(int keepOn) {
    s_keepScreenOn.store(keepOn != 0 ? 1 : 0);
    fprintf(stderr, "[KuDroidCore] kudroid_set_keep_screen_on(%d)\n", keepOn != 0 ? 1 : 0);
}

extern "C" int kudroid_get_keep_screen_on(void) {
    return s_keepScreenOn.load();
}

extern "C" JNIEXPORT void JNICALL Java_android_view_Window_setKeepScreenOnNative(JNIEnv* env, jclass clazz, jboolean keepOn) {
    (void)env; (void)clazz;
    kudroid_set_keep_screen_on(keepOn ? 1 : 0);
}

extern "C" JNIEXPORT void JNICALL Java_android_view_View_setKeepScreenOnNative(JNIEnv* env, jclass clazz, jboolean keepOn) {
    (void)env; (void)clazz;
    kudroid_set_keep_screen_on(keepOn ? 1 : 0);
}

extern "C" JNIEXPORT void JNICALL Java_android_os_PowerManager_00024WakeLock_setKeepScreenOnNative(JNIEnv* env, jclass clazz, jboolean keepOn) {
    (void)env; (void)clazz;
    kudroid_set_keep_screen_on(keepOn ? 1 : 0);
}

// ── soft keyboard ────────────────────────────────────────────────────────────
//
// KuDroid has no keyboard of its own; iOS does. The guest's
// InputMethodManager.showSoftInput reaches kudroid_show_soft_input, which forwards
// to a callback the Swift side registers — kudroid_core is a static library and
// cannot touch UIKit, so the view that becomes first responder has to call back in.
//
// Text goes the other way, from the host keyboard into the guest's focused
// InputConnection, through kuart_dispatch_text_input.

static kudroid_soft_input_show_cb s_softInputShow = nullptr;
static kudroid_soft_input_hide_cb s_softInputHide = nullptr;
static std::atomic<int> s_softInputVisible{0};

extern "C" void kudroid_set_soft_input_callbacks(kudroid_soft_input_show_cb show,
                                                kudroid_soft_input_hide_cb hide) {
    s_softInputShow = show;
    s_softInputHide = hide;
    fprintf(stderr, "[KuDroidCore] soft input callbacks %s\n",
            (show != nullptr || hide != nullptr) ? "registered" : "cleared");
}

extern "C" int kudroid_show_soft_input(int flags) {
    fprintf(stderr, "[KuDroidCore] kudroid_show_soft_input(flags=0x%x) host=%s\n",
            flags, s_softInputShow != nullptr ? "yes" : "none");
    if (s_softInputShow == nullptr) return 0;
    s_softInputShow(flags);
    return 1;
}

extern "C" int kudroid_hide_soft_input(void) {
    fprintf(stderr, "[KuDroidCore] kudroid_hide_soft_input() host=%s\n",
            s_softInputHide != nullptr ? "yes" : "none");
    if (s_softInputHide == nullptr) return 0;
    s_softInputHide();
    return 1;
}

extern "C" int kudroid_is_soft_input_visible(void) {
    return s_softInputVisible.load();
}

extern "C" void kudroid_set_soft_input_visible(int visible) {
    const int v = visible != 0 ? 1 : 0;
    if (s_softInputVisible.exchange(v) != v) {
        fprintf(stderr, "[KuDroidCore] soft input now %s\n", v ? "visible" : "hidden");
    }
}

extern "C" void kudroid_dispatch_text_input(const char* utf8) {
    if (utf8 == nullptr || utf8[0] == '\0') return;
    kuart_dispatch_text_input(utf8);
}

extern "C" void kudroid_dispatch_delete_backward(void) {
    kuart_dispatch_delete_backward();
}

#include "kudroid/KuArtRuntime.h"

// --- nh ngha nativeactivity ---

struct ANativeActivityCallbacks {
    void* onStart;
    void* onResume;
    void* onSaveInstanceState;
    void* onPause;
    void* onStop;
    void* onDestroy;
    void* onWindowFocusChanged;
    void* onNativeWindowCreated;
    void* onNativeWindowResized;
    void* onNativeWindowRedrawNeeded;
    void* onNativeWindowDestroyed;
    void* onInputQueueCreated;
    void* onInputQueueDestroyed;
    void* onContentRectChanged;
    void* onConfigurationChanged;
    void* onLowMemory;
};

struct ANativeActivity {
    ANativeActivityCallbacks* callbacks;
    JavaVM* vm;
    JNIEnv* env;
    jclass clazz;
    const char* internalDataPath;
    const char* externalDataPath;
    int32_t sdkVersion;
    void* instance;
    void* assetManager;
    const char* obbPath;
};


// T kim tra KuART: np framework.dex nhng ri th JNI c hai chiu. Tham s
// gi li cho tng thch ABI vi v Swift (before l ng dn rt.jar ca Avian).
extern "C" char* kudroid_test_jvm(const char* unused_path) {
    (void)unused_path;
    std::string log;
    appendTestHeader(log, "KuART Integration Test", "N/A");
    installCrashHandlers();

    log += "[kudroid_core] Phase: init KuART\n";
    kudroid::bionic_shim_reset_trace();

    static std::string* g_kuart_test_log = &log;
    g_kuart_test_log = &log;
    kuart_set_log_callback([](const char* msg) {
        if (g_kuart_test_log) {
            *g_kuart_test_log += "[KuART] ";
            *g_kuart_test_log += msg;
            *g_kuart_test_log += "\n";
        }
    });

    // app_dir rng = ch np framework.dex nhng.
    if (!kuart_init("")) {
        log += "[kudroid_core] ERROR: kuart_init failed: " + std::string(kuart_last_error()) + "\n";
        kuart_set_log_callback(nullptr);
        g_kuart_test_log = nullptr;
        return strdup(log.c_str());
    }
    log += "[kudroid_core] KuART DEX loaded: " + std::to_string(kuart_num_dex_files()) + "\n";

    JavaVM* vm = kuart_get_javavm();
    if (!vm) {
        log += "[kudroid_core] ERROR: JavaVM is null!\n";
        kuart_set_log_callback(nullptr);
        g_kuart_test_log = nullptr;
        return strdup(log.c_str());
    }

    JNIEnv* env = nullptr;
    kuart_get_env(vm, reinterpret_cast<void**>(&env), 0);

    if (env) {
        log += "[kudroid_core] Phase: testing JNI FindClass\n";
        // ActivityThread l class m ng khi ng tht s dng — n load c
        // ngha l framework nhng hp l v class linker run ng.
        for (const char* name : {"android/app/ActivityThread", "android/app/Activity",
                                 "android/os/Looper", "android/util/Log"}) {
            jclass c = env->FindClass(name);
            log += std::string("[kudroid_core] ") + (c ? "SUCCESS" : "FAILED ") +
                   ": FindClass(" + name + ")\n";
            if (env->ExceptionCheck()) env->ExceptionClear();
        }

        jclass at = env->FindClass("android/app/ActivityThread");
        if (at) {
            jmethodID main = env->GetStaticMethodID(at, "main", "([Ljava/lang/String;)V");
            log += std::string("[kudroid_core] ") + (main ? "SUCCESS" : "FAILED ") +
                   ": GetStaticMethodID(ActivityThread.main)\n";
        }

        jstring testStr = env->NewStringUTF("Hello KuART");
        if (testStr) {
            const char* utf = env->GetStringUTFChars(testStr, nullptr);
            log += "[kudroid_core] SUCCESS: Created JNI string: ";
            log += utf ? utf : "null";
            log += "\n";
            env->ReleaseStringUTFChars(testStr, utf);
        }
        log += "[kudroid_core] classes resolved: " +
               std::to_string(kuart_num_loaded_classes()) + "\n";
    } else {
        log += "[kudroid_core] ERROR: Failed to get JNIEnv!\n";
    }

    log += "[kudroid_core] Phase: shutdown KuART\n";
    kuart_shutdown();
    kuart_set_log_callback(nullptr);
    g_kuart_test_log = nullptr;

    log += "[kudroid_core] KuART test completed.\n";
    return strdup(log.c_str());
}

// Guest .so mappings PHI sng bng tui process — ng ng ngha dlopen trn
// Android (libs li trong process cho ti khi app cht). Bng chng log my
// tht: mi crash triangle t u u l SIGABRT trong libtriangle_gles.so+
// 0x4xxx NGAY after khi run_apk in dng cui "ANativeActivity_onCreate not found"
// — render thread guest run SONG SONG vi run_apk (spawn t JNI_OnLoad), cn
// manager before y l bin LOCAL nn khi run_apk return, destructor ElfLoader
// munmap ton b guest .so trong lc render thread vn ang execute bn trong
// → execute vng nh unmap → abort. Warm-up ANGLE cc vng before ch lm
// render thread ti xa hn before khi munmap (race) nn crash di dn — no/not
// phi fix gc.
static kudroid::LibraryManager& globalLibraryManager() {
    static kudroid::LibraryManager instance;
    return instance;
}

// The directory kudroid_run_apk scanned for guest .so files.
//
// Kept because findLibrary() has to answer for a library that exists on disk but was
// never loaded — a lib listed in android.app.lib_name that failed to map still has a
// real path, and reporting null for it turns a load failure into a much vaguer
// "library not found" at the caller.
static std::mutex g_nativeLibDirMtx;
static std::string g_nativeLibDir;

extern "C" void kudroid_set_native_lib_dir(const char* dir) {
    if (dir == nullptr) return;
    std::lock_guard<std::mutex> lock(g_nativeLibDirMtx);
    g_nativeLibDir = dir;
}

extern "C" int kudroid_find_native_library(const char* name, char* out,
                                           unsigned long out_size) {
    if (name == nullptr || *name == '\0' || out == nullptr || out_size == 0) return 0;

    // Accept every form a caller might hold. android.app.lib_name gives the bare name
    // ("minecraftpe"), System.loadLibrary the same, while code that already has a file
    // name or a full path passes that instead. Normalising here means the Java side
    // does not have to guess which one it has.
    std::string base = std::filesystem::path(name).filename().string();
    if (base.empty()) return 0;
    std::string filename = base;
    if (filename.size() < 3 || filename.compare(filename.size() - 3, 3, ".so") != 0) {
        filename = "lib" + filename + ".so";
    }

    const auto emit = [&](const std::string& path) -> int {
        if (path.empty() || path.size() + 1 > out_size) return 0;
        std::memcpy(out, path.c_str(), path.size() + 1);
        return 1;
    };

    // A loaded library is the authoritative answer: its key is the canonical path the
    // ELF was actually mapped from, so it cannot disagree with reality.
    {
        kudroid::LibraryManager& manager = globalLibraryManager();
        for (const auto& pair : manager.libraries()) {
            if (std::filesystem::path(pair.first).filename().string() == filename) {
                return emit(pair.first);
            }
        }
    }

    // Not loaded: fall back to the scanned directory, but only if the file is there.
    // Returning a constructed path that does not exist would give the caller something
    // to fail on later rather than a clear miss now.
    std::string dir;
    {
        std::lock_guard<std::mutex> lock(g_nativeLibDirMtx);
        dir = g_nativeLibDir;
    }
    if (!dir.empty()) {
        std::error_code ec;
        const std::filesystem::path candidate = std::filesystem::path(dir) / filename;
        if (std::filesystem::exists(candidate, ec)) return emit(candidate.string());
    }
    return 0;
}

// Hook tra symbol guest — nh ngha trong SyscallShim.cpp, bionic_dlsym dng
// khi handle l DUMMY_HANDLE (dlopen("libc.so") v.v.).
extern "C" {
extern void* (*kudroid_guest_symbol_lookup)(const char* name);
extern void* (*kudroid_guest_library_open)(const char* filename);
extern void* (*kudroid_guest_library_symbol)(void* handle, const char* symbol);
extern int (*kudroid_guest_library_owns)(void* handle);
}

// dlopen/dlsym/dlclose for guest .so files LibraryManager already mapped.
//
// A guest dlopen used to return DUMMY_HANDLE, and dlsym on that scans every loaded
// library and takes the first match — so a caller that named one library could get
// another's function whenever both export the same symbol. GameActivity is the caller
// that makes this concrete: it dlopens the .so from android.app.lib_name and reads its
// entry points out of that handle alone.
//
// The handle IS the ElfLoader pointer, kept in a registry so a pointer arriving from
// guest code can be validated before it is dereferenced — the same reason the JNI
// layer validates receivers rather than trusting them.
namespace {

std::mutex& guestHandleMutex() {
    static std::mutex m;
    return m;
}

std::set<void*>& guestHandles() {
    static std::set<void*> handles;
    return handles;
}

void* guestLibraryOpen(const char* filename) {
    if (filename == nullptr || *filename == '\0') return nullptr;

    // Match on the file name so every form works: an absolute Android path
    // (/data/app/<pkg>/lib/arm64-v8a/libfoo.so), a bare "libfoo.so", or the real
    // container path that findLibrary() hands out.
    const std::string wanted = std::filesystem::path(filename).filename().string();
    if (wanted.empty()) return nullptr;

    kudroid::LibraryManager& manager = globalLibraryManager();
    for (const auto& pair : manager.libraries()) {
        if (std::filesystem::path(pair.first).filename().string() != wanted) continue;
        void* handle = pair.second.get();
        std::lock_guard<std::mutex> lock(guestHandleMutex());
        guestHandles().insert(handle);
        return handle;
    }
    return nullptr;
}

void* guestLibrarySymbol(void* handle, const char* symbol) {
    if (handle == nullptr || symbol == nullptr || *symbol == '\0') return nullptr;
    {
        std::lock_guard<std::mutex> lock(guestHandleMutex());
        if (guestHandles().count(handle) == 0) return nullptr;
    }
    return static_cast<kudroid::ElfLoader*>(handle)->getSymbolAddress(symbol);
}

int guestLibraryOwns(void* handle) {
    if (handle == nullptr) return 0;
    std::lock_guard<std::mutex> lock(guestHandleMutex());
    return guestHandles().count(handle) != 0 ? 1 : 0;
}

}  // namespace

extern "C" const char* kudroid_run_apk(const char* appName) {
    if (s_isApkRunning.exchange(true)) {
        kudroid_android_log_message(3, "kudroid_core", "kudroid_run_apk: APK is already running in background, ignoring duplicate launch request.");
        return strdup("[kudroid_core] APK is already running.\n");
    }

    // Reset/truncate logs so persistent breadcrumbs represent one APK run.
    if (g_logDir[0] != '\0') {
        char aPath[1200];
        snprintf(aPath, sizeof(aPath), "%s/kudroid_android_logs.txt", g_logDir);
        FILE* afp = fopen(aPath, "w");
        if (afp) {
            // Build stamp dng u tin ca log phin run APK mi.
            fprintf(afp, "[kudroid_core] Build: %s\n", kudroid_build_stamp());
            fclose(afp);
        }

        snprintf(aPath, sizeof(aPath), "%s/native_breadcrumbs.log", g_logDir);
        FILE* bfp = fopen(aPath, "w");
        if (bfp) fclose(bfp);

        snprintf(aPath, sizeof(aPath), "%s/kudroid_crash.log", g_logDir);
        FILE* cfp = fopen(aPath, "w");
        if (cfp) fclose(cfp);

#if defined(__APPLE__)
        char errPath[1200];
        snprintf(errPath, sizeof(errPath), "%s/stderr.log", g_logDir);
        int errFd = open(errPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (errFd >= 0) {
            dup2(errFd, STDERR_FILENO);
            setvbuf(stderr, nullptr, _IONBF, 0);
            close(errFd);
            // AFC (kdb dump / Finder) ch read c file c quyn read-other.
            ::chmod(errPath, 0644);
            fprintf(stderr, "[kudroid_core] Build: %s\n", kudroid_build_stamp());
        }
#endif
    }

    kudroid::native_run_begin();
    kudroid::native_phase("apk-run-enter");

    std::string log;
    appendTestHeader(log, "Run APK Native Libraries", appName);
    kudroid::bionic_shim_reset_trace();

    // JIT permission is a precondition for launching, not a mode to degrade into.
    //
    // Guest .so files are mapped RW and then mprotect'd to add PROT_EXEC. iOS grants
    // PROT_EXEC on anonymous memory only to a debugged process or one holding the JIT
    // entitlement, and it will never grant it on an unsigned file mapping — which every
    // guest .so is. So without the permission no guest native code can run, and there
    // is no workaround at this layer: it is a code-signing rule, not missing code.
    //
    // KuDroid targets Android apps with NDK libraries, where that means the app cannot
    // start at all. Refusing here with instructions is more useful than loading the
    // Java side and failing later at the first native call, and it avoids mapping
    // hundreds of megabytes that are certain to be discarded.
    //
    // Everything downstream of this gate still keeps its interpreter path: most Dex
    // methods are interpreted even with JIT available (JitCompiler covers a small
    // opcode subset), java.lang natives run from LibCore inside this signed binary,
    // and JitCache stays null-safe. That is how the runtime works, not a fallback.
    if (kudroid_is_jit_enabled() == 0) {
        log +=
            "[kudroid_core] ERROR: JIT is not enabled, cannot launch.\n"
            "[kudroid_core]        Android apps ship native libraries (.so) that must be\n"
            "[kudroid_core]        mapped executable. iOS refuses that without JIT\n"
            "[kudroid_core]        permission, so the app cannot start.\n"
            "[kudroid_core]        Enable JIT, then launch again:\n"
            "[kudroid_core]          - LiveContainer: turn on JIT for this app\n"
            "[kudroid_core]          - Sideloaded: attach StikDebug / SideStore\n"
            "[kudroid_core]          - TrollStore: install from TrollStore\n";
        std::fputs(log.c_str(), stderr);
        logCoreLine(6, "[kudroid_core] launch refused: JIT not enabled");
        mirrorCrash(log);
        // Released here: the launch never began, so a retry after enabling JIT must not
        // be rejected as a duplicate by the guard at the top of this function.
        s_isApkRunning.store(false);
        return strdup(log.c_str());
    }

    log += "[kudroid_core] Phase: init LibraryManager\n";

    if (!appName || !*appName) {
        log += "[kudroid_core] ERROR: null or empty app name\n";
    } else {
        auto& remapper = kudroid::VFSPathRemapper::getInstance();
        std::string resolvedAppName = appName;
        std::filesystem::path appDir = std::filesystem::path(remapper.androidRoot()) / "data/app" / resolvedAppName;

        // ── Chun ha tn app = package ID tht ───────────────────────────
        // Th mc install thng mang tn FILE APK ti v (vd
        // "Minecraft_PE_26.30_BANDISHARE") trong khi Android tht nh danh
        // app bng package ID ("com.mojang.minecraftpe"). read manifest NGAY
        // T U i tn th mc + di chuyn dalvik-cache sang tn chun:
        // mi ng dn runtime (dalvik-cache, data/data, sdcard/Android/data)
        // khp Android tht v nht qun d ti li APK vi tn file khc.
        {
            const auto mfPath = appDir / "AndroidManifest.xml";
            std::string pkgId;
            if (std::filesystem::exists(mfPath)) {
                std::ifstream mf(mfPath, std::ios::binary);
                if (mf) {
                    std::vector<std::uint8_t> axml(
                        (std::istreambuf_iterator<char>(mf)),
                        std::istreambuf_iterator<char>());
                    kudroid::ManifestInfo mi =
                        kudroid::APKExtractor::parse_manifest(axml.data(), axml.size());
                    if (mi.packageName.empty() && axml.size() > 4 && axml[0] == '<') {
                        mi = kudroid::APKExtractor::parse_manifest_text(
                            reinterpret_cast<const char*>(axml.data()), axml.size());
                    }
                    pkgId = mi.packageName;
                }
            }

            if (!pkgId.empty() && pkgId != resolvedAppName) {
                const auto cleanAppDir =
                    std::filesystem::path(remapper.androidRoot()) / "data/app" / pkgId;
                std::error_code renEc;
                bool movedApp = false;
                if (!std::filesystem::exists(cleanAppDir)) {
                    std::filesystem::rename(appDir, cleanAppDir, renEc);
                    movedApp = !renEc;
                }
                // Dalvik-cache c ca ng dex2jar phi theo tn mi bc
                // dn dp after ny tm thy v remove c.
                const auto oldCache = std::filesystem::path(remapper.androidRoot()) /
                                      "data/dalvik-cache" / resolvedAppName;
                const auto newCache = std::filesystem::path(remapper.androidRoot()) /
                                      "data/dalvik-cache" / pkgId;
                if (std::filesystem::exists(oldCache) && !std::filesystem::exists(newCache)) {
                    std::error_code cacheEc;
                    std::filesystem::rename(oldCache, newCache, cacheEc);
                }
                if (movedApp) {
                    resolvedAppName = pkgId;
                    appDir = cleanAppDir;
                } else if (std::filesystem::exists(cleanAppDir)) {
                    // c th mc chun t before (ci ) — gi nguyn b cc c.
                }
            }
        }

        // Fallback c: nu manifest no/not read c, th app_info.json do
        // extractor write lc install (cha package ID chun).
        if (std::filesystem::exists(appDir)) {
            std::filesystem::path infoPath = appDir / "app_info.json";
            if (std::filesystem::exists(infoPath)) {
                std::ifstream f(infoPath);
                std::string line;
                while (std::getline(f, line)) {
                    auto pos = line.find("\"package\": \"");
                    if (pos != std::string::npos) {
                        auto start = pos + 12;
                        auto end = line.find("\"", start);
                        if (end != std::string::npos) {
                            std::string pkg = line.substr(start, end - start);
                            if (!pkg.empty() && pkg != resolvedAppName) {
                                std::filesystem::path cleanTarget = std::filesystem::path(remapper.androidRoot()) / "data/app" / pkg;
                                std::error_code ec;
                                std::filesystem::rename(appDir, cleanTarget, ec);
                                if (!ec) {
                                    resolvedAppName = pkg;
                                    appDir = cleanTarget;
                                }
                            }
                        }
                    }
                }
            }
        } else {
            auto uIdx = resolvedAppName.find('_');
            if (uIdx != std::string::npos) {
                std::string base = resolvedAppName.substr(0, uIdx);
                std::filesystem::path baseDir = std::filesystem::path(remapper.androidRoot()) / "data/app" / base;
                if (std::filesystem::exists(baseDir)) {
                    resolvedAppName = base;
                    appDir = baseDir;
                }
            }
        }

        const std::filesystem::path libDir = appDir / ("lib/" KUDROID_DEVICE_ABI);
        const std::filesystem::path assetsDir = appDir / "assets";
        kudroid_set_assets_dir(assetsDir.string().c_str());
        kudroid_set_native_lib_dir(libDir.string().c_str());
        
        auto appendAndEcho = [&](const std::string& line) {
            log += line + "\n";
            std::fprintf(stderr, "%s\n", line.c_str());
            logCoreLine(2, line);
            mirrorCrash(log);
        };
        
        appendAndEcho("[kudroid_core] Scanning library directory: " + libDir.string());

        if (!std::filesystem::exists(libDir)) {
            appendAndEcho("[kudroid_core] ERROR: Library directory does not exist: " + libDir.string());
        } else {
            kudroid::LibraryManager& manager = globalLibraryManager();

            // Ci hook tra symbol guest cho bionic_dlsym(DUMMY_HANDLE, ...):
            // dlopen("libc.so") tr handle gi nhng dlsym(handle, "function") trn
            // Android tht vn resolve c — no/not ci hook th init code ca
            // guest nhn nullptr ri call → SIGSEGV pc=0x0 (crash libmaesdk).
            kudroid_guest_symbol_lookup = [](const char* name) -> void* {
                return globalLibraryManager().resolveGlobalSymbol(name);
            };

            // Per-library dlopen/dlsym for the guest's own .so files. Installed here
            // rather than at load time because it must be live before any guest code
            // runs, and this is the point where the libraries exist.
            kudroid_guest_library_open = &guestLibraryOpen;
            kudroid_guest_library_symbol = &guestLibrarySymbol;
            kudroid_guest_library_owns = &guestLibraryOwns;

            // KuART phi sn sng before khi dlopen bt k .so no: static
            // initializer ca libminecraftpe.so call JNI_GetCreatedJavaVMs, nu
            // cha c VM th n t dng state sai ri no/not sa li c.
            kuart_set_log_callback([](const char* msg) {
                kudroid_android_log_message(4, "KuART", msg);
                std::fprintf(stderr, "[KuART] %s\n", msg);
            });
            kuart_set_symbol_lookup([](const char* symbol) -> void* {
                return globalLibraryManager().resolveGlobalSymbol(symbol);
            });

            if (kuart_init(appDir.string().c_str())) {
                appendAndEcho("[kudroid_core] KuART ready: " +
                              std::to_string(kuart_num_dex_files()) + " DEX loaded");
            } else {
                appendAndEcho("[kudroid_core] ERROR: KuART init failed: " +
                              std::string(kuart_last_error()));
            }

            std::vector<std::string> soFiles;
            for (const auto& entry : std::filesystem::directory_iterator(libDir)) {
                if (entry.path().extension() == ".so") {
                    soFiles.push_back(entry.path().string());
                }
            }

            if (soFiles.empty()) {
                appendAndEcho("[kudroid_core] WARNING: No .so files found in " + libDir.string());
            }
            // Not an `else`: everything below — the manifest parse, KuART launch and
            // ActivityThread.main — has to run even when there are no native libraries
            // to load. It used to sit in the else branch, so an app with no usable .so
            // (an APK built for another ABI, say) silently never started its Java side
            // either. The load loop below is a no-op on an empty list.
            {
                for (const auto& soPath : soFiles) {
                    appendAndEcho("[kudroid_core] Attempting to load: " + soPath);
                    if (!manager.loadRecursive(soPath.c_str())) {
                        appendAndEcho("[kudroid_core] LOAD FAILED for " + soPath + ": " + manager.lastError());
                    } else {
                        appendAndEcho("[kudroid_core] LOAD SUCCESS for " + soPath);
                    }
                }
                
                appendAndEcho("[kudroid_core] Total loaded libraries: " + std::to_string(manager.libraries().size()));
                for (const auto& pair : manager.libraries()) {
                    char mapLine[256];
                    snprintf(mapLine, sizeof(mapLine), "  %s -> %p", pair.first.c_str(), pair.second->baseAddress());
                    appendAndEcho(mapLine);
                }

                JavaVM* jvm = kuart_get_javavm();
                if (!jvm) {
                    appendAndEcho("[kudroid_core] ERROR: KuART failed to initialize.");
                } else {
                    char jvmLine[128];
                    snprintf(jvmLine, sizeof(jvmLine), "[kudroid_core] KuART ready (JavaVM=%p).", (void*)jvm);
                    appendAndEcho(jvmLine);

                    auto native_activity_create = reinterpret_cast<void (*)(ANativeActivity*, void*, size_t)>(
                        manager.resolveAppSymbol("ANativeActivity_onCreate")
                    );

                    kuart_set_load_library_callback([](const char* libname) -> int {
                        static std::set<std::string> s_loadedJniOnLoads;
                if (!libname || !*libname) return 0;
                std::string name = libname;
                std::string filename = name;
                if (filename.find(".so") == std::string::npos) {
                    filename = "lib" + filename + ".so";
                }
                kudroid::LibraryManager& manager = globalLibraryManager();
                void* sym = manager.resolveSymbolInLib(filename, "JNI_OnLoad");
                if (!sym) sym = manager.resolveSymbolInLib(name, "JNI_OnLoad");
                if (sym) {
                    if (s_loadedJniOnLoads.insert(filename).second) {
                        JavaVM* jvm = kuart_get_javavm();
                        if (jvm) {
                            bionic_init_main_thread_tls();
                            char msg[256];
                            snprintf(msg, sizeof(msg), "[kudroid_core] Invoking JNI_OnLoad in %s (System.loadLibrary)", filename.c_str());
                            logCoreLine(4, msg);
                            std::fprintf(stderr, "%s\n", msg);

                            auto jni_onload = reinterpret_cast<jint (*)(JavaVM*, void*)>(sym);
                            jint version = 0;
                            const int guardRc = kudroid_call_jni_onload_guarded(jni_onload, jvm, &version);
                            if (guardRc == 0) {
                                snprintf(msg, sizeof(msg), "[kudroid_core] JNI_OnLoad(%s) returned version: %d", filename.c_str(), version);
                                logCoreLine(4, msg);
                                std::fprintf(stderr, "%s\n", msg);
                            } else if (guardRc < 0) {
                                snprintf(msg, sizeof(msg), "[kudroid_core] WARNING: Native exception in JNI_OnLoad for %s", filename.c_str());
                                logCoreLine(5, msg);
                                std::fprintf(stderr, "%s\n", msg);
                                const std::string report =
                                    describeJniGuardFault(filename, guardRc);
                                std::fputs(report.c_str(), stderr);
                                appendCrashLogFile(report);
                            } else {
                                snprintf(msg, sizeof(msg), "[kudroid_core] WARNING: JNI_OnLoad in %s raised fatal signal %d", filename.c_str(), guardRc);
                                logCoreLine(5, msg);
                                std::fprintf(stderr, "%s\n", msg);
                                // The guard leaves the crash-log path unreached, so
                                // report the captured state here. Without it a
                                // swallowed fault was one WARNING line with no pc,
                                // no fault address and no stack — nothing to work
                                // from. Goes to kudroid_crash.log as well, where
                                // every other fault in this process is recorded.
                                const std::string report =
                                    describeJniGuardFault(filename, guardRc);
                                std::fputs(report.c_str(), stderr);
                                appendCrashLogFile(report);
                            }

                            // JNI requires native code to check and clear pending
                            // exceptions before returning to Java. Libraries that
                            // ignore the rule leave one in flight, and the
                            // interpreter then attributes it to the Java call that
                            // triggered the load: an exception raised inside a
                            // JNI_OnLoad callback came back out of
                            // System.loadLibrary("PlayFabMultiplayer"), which
                            // Minecraft only guards with
                            // catch(UnsatisfiedLinkError), so MainActivity.<clinit>
                            // died on an unrelated ArrayIndexOutOfBoundsException.
                            //
                            // Done on every exit path, not just the clean one: a
                            // library that aborted mid-callback is even more likely
                            // to have left one behind. The description carries the
                            // Java stack trace so the discarded exception stays
                            // diagnosable instead of vanishing.
                            const char* leaked = nullptr;
                            if (kuart_take_pending_exception(&leaked)) {
                                snprintf(msg, sizeof(msg),
                                         "[kudroid_core] JNI_OnLoad(%s) left a pending Java exception; cleared",
                                         filename.c_str());
                                logCoreLine(5, msg);
                                // The description carries a multi-line stack trace,
                                // so it goes straight to stderr rather than through
                                // the fixed-size log buffer that would truncate it.
                                std::fprintf(stderr, "%s:\n%s\n", msg,
                                             leaked != nullptr ? leaked : "?");
                            }
                        }
                    }
                }
                return 1;
            });

                    if (native_activity_create) {
                        appendAndEcho("[kudroid_core] Native Game Activity detected.");

                        appendAndEcho("[kudroid_core] Found ANativeActivity_onCreate, invoking...");

                        static std::string s_internalDataPath;
                        static std::string s_externalDataPath;
                        static std::string s_obbPath;
                        s_internalDataPath = "/data/data/" + resolvedAppName;
                        s_externalDataPath = "/sdcard/Android/data/" + resolvedAppName;
                        s_obbPath = "/sdcard/Android/obb/" + resolvedAppName;

                        std::error_code vfsEc;
                        std::filesystem::create_directories(std::filesystem::path(remapper.androidRoot()) / "data/data" / resolvedAppName, vfsEc);
                        std::filesystem::create_directories(std::filesystem::path(remapper.androidRoot()) / "sdcard/Android/data" / resolvedAppName, vfsEc);
                        std::filesystem::create_directories(std::filesystem::path(remapper.androidRoot()) / "sdcard/Android/obb" / resolvedAppName, vfsEc);

                        static ANativeActivityCallbacks mock_callbacks = {};
                        static ANativeActivity mock_activity = {
                            &mock_callbacks,
                            jvm,
                            nullptr, // env
                            nullptr, // clazz
                            s_internalDataPath.c_str(),
                            s_externalDataPath.c_str(),
                            KUDROID_SDK_INT, // must match Build.VERSION.SDK_INT
                            nullptr, // instance
                            nullptr, // assetManager
                            s_obbPath.c_str()
                        };
                        kuart_get_env(jvm, reinterpret_cast<void**>(&mock_activity.env), 0);

                        if (mock_activity.env) {
                            JNIEnv* env = mock_activity.env;
                            if (mock_activity.assetManager) {
                                env->DeleteGlobalRef(static_cast<jobject>(mock_activity.assetManager));
                                mock_activity.assetManager = nullptr;
                            }
                            jclass assetCls = env->FindClass("android/content/res/AssetManager");
                            if (assetCls && !env->ExceptionCheck()) {
                                jmethodID ctor = env->GetMethodID(assetCls, "<init>", "()V");
                                jobject am = ctor ? env->NewObject(assetCls, ctor) : nullptr;
                                if (am) {
                                    mock_activity.assetManager = env->NewGlobalRef(am);
                                    env->DeleteLocalRef(am);
                                    appendAndEcho("[kudroid_core] AssetManager jobject created.");
                                }
                                if (env->ExceptionCheck()) env->ExceptionClear();
                                env->DeleteLocalRef(assetCls);
                            } else {
                                if (env->ExceptionCheck()) env->ExceptionClear();
                            }
                        }

                        native_activity_create(&mock_activity, nullptr, 0);
                        appendAndEcho("[kudroid_core] ANativeActivity_onCreate completed.");

                        auto call1 = [&](const char* name, void* fn) {
                            if (!fn) return;
                            appendAndEcho(std::string("[kudroid_core] Calling ") + name);
                            reinterpret_cast<void (*)(ANativeActivity*)>(fn)(&mock_activity);
                        };
                        auto call2 = [&](const char* name, void* fn, void* arg2) {
                            if (!fn) return;
                            appendAndEcho(std::string("[kudroid_core] Calling ") + name);
                            reinterpret_cast<void (*)(ANativeActivity*, void*)>(fn)(&mock_activity, arg2);
                        };
                        call1("onStart", mock_callbacks.onStart);
                        call1("onResume", mock_callbacks.onResume);
                        call2("onWindowFocusChanged", mock_callbacks.onWindowFocusChanged, reinterpret_cast<void*>(1));
                        call2("onNativeWindowCreated", mock_callbacks.onNativeWindowCreated, bionic_ANativeWindow_fromSurface(nullptr, nullptr));
                        call2("onInputQueueCreated", mock_callbacks.onInputQueueCreated, kudroid_get_input_queue());
                        appendAndEcho("[kudroid_core] Lifecycle callbacks invoked successfully!");
                    } else {
                        appendAndEcho("[kudroid_core] Java APK Application detected (No ANativeActivity_onCreate).");
                        
                        std::string targetActivity = "";
                        std::string pkgName = "";
                        // Everything the manifest declares. Populated below and used
                        // as the fallback list, so KuDroid only ever tries classes the
                        // app actually declares.
                        std::vector<std::string> manifestActivities;
                        std::string manifestAppClass;
                        // android:appComponentFactory. Android instantiates it before
                        // any component, so its <clinit> is the first guest code to
                        // run; skipping it leaves whatever the app initialises there
                        // empty. Generic: whatever the manifest names, nothing assumed.
                        std::string manifestComponentFactory;
                        // <meta-data> per activity plus the <application> block. Kept
                        // whole rather than reduced to what one app needs: a component
                        // reads its own manifest entry while constructing itself, so
                        // whatever key it wants has to already be there.
                        kudroid::ManifestInfo manifestInfo;

                        // U TIN 1: parse AndroidManifest.xml GII NN trong
                        // appDir — ngun main xc duy nht. Log c cho thy
                        // app_info.json c th thiu/stale (Target Activity b
                        // on "Minecraft.MainActivity" trong khi package tht
                        // l com.mojang.minecraftpe) → ClassNotFoundException.
                        const auto manifestPath = appDir / "AndroidManifest.xml";
                        if (std::filesystem::exists(manifestPath)) {
                            std::ifstream mf(manifestPath, std::ios::binary);
                            if (mf) {
                                std::vector<std::uint8_t> axml(
                                    (std::istreambuf_iterator<char>(mf)),
                                    std::istreambuf_iterator<char>());
                                appendAndEcho("[kudroid_core] Parsing AndroidManifest.xml (" +
                                              std::to_string(axml.size()) + " bytes)...");
                                kudroid::ManifestInfo mi =
                                    kudroid::APKExtractor::parse_manifest(axml.data(), axml.size());
                                // APK repack (apktool/BANDISHARE...) thng cha
                                // manifest dng TEXT — AXML parser tr rng vi
                                // chng. Th parse text before khi b cuc.
                                if (mi.mainActivity.empty() && axml.size() > 4 &&
                                    axml[0] == '<') {
                                    appendAndEcho("[kudroid_core] Manifest is TEXT XML, trying text parser...");
                                    mi = kudroid::APKExtractor::parse_manifest_text(
                                        reinterpret_cast<const char*>(axml.data()), axml.size());
                                }
                                appendAndEcho("[kudroid_core] Manifest parse: package='" + mi.packageName +
                                              "' mainActivity='" + mi.mainActivity + "'" +
                                              " activities=" + std::to_string(mi.activities.size()) +
                                              (mi.appClass.empty() ? "" : " application='" + mi.appClass + "'") +
                                              (mi.appComponentFactory.empty()
                                                   ? ""
                                                   : " appComponentFactory='" + mi.appComponentFactory + "'"));
                                if (!mi.mainActivity.empty()) targetActivity = mi.mainActivity;
                                if (!mi.packageName.empty()) pkgName = mi.packageName;
                                manifestAppClass = mi.appClass;
                                manifestComponentFactory = mi.appComponentFactory;
                                // The manifest is the authoritative list of what this
                                // app can launch: launcher activities first, then the
                                // rest. Feeding these to ActivityThread replaces the
                                // old habit of inventing names like "<pkg>.Main",
                                // which could never exist unless the app happened to
                                // use that exact name.
                                manifestActivities = mi.launchOrder();
                                for (const auto& a : mi.activities) {
                                    appendAndEcho(std::string("[kudroid_core]   manifest activity: ") + a.name +
                                                  (a.isLauncher ? "  [LAUNCHER]" : "") +
                                                  (a.isAlias ? "  [alias]" : "") +
                                                  (a.metaData.empty()
                                                       ? ""
                                                       : "  [" + std::to_string(a.metaData.size()) +
                                                             " meta-data]"));
                                }
                                manifestInfo = mi;
                            } else {
                                appendAndEcho("[kudroid_core] WARNING: Cannot open AndroidManifest.xml");
                            }
                        } else {
                            appendAndEcho("[kudroid_core] WARNING: AndroidManifest.xml not found in " + appDir.string());
                        }

                        // U TIN 2: app_info.json do extractor write lc install.
                        if (targetActivity.empty()) {
                            std::filesystem::path infoPath = appDir / "app_info.json";
                            if (std::filesystem::exists(infoPath)) {
                                std::ifstream f(infoPath);
                                std::string line;
                                while (std::getline(f, line)) {
                                    auto posAct = line.find("\"main_activity\": \"");
                                    if (posAct != std::string::npos) {
                                        auto start = posAct + 18;
                                        auto end = line.find("\"", start);
                                        if (end != std::string::npos) {
                                            std::string act = line.substr(start, end - start);
                                            if (!act.empty()) targetActivity = act;
                                        }
                                    }
                                    auto posPkg = line.find("\"package\": \"");
                                    if (posPkg != std::string::npos) {
                                        auto start = posPkg + 12;
                                        auto end = line.find("\"", start);
                                        if (end != std::string::npos) {
                                            pkgName = line.substr(start, end - start);
                                        }
                                    }
                                }
                            }
                        }

                        // U TIN 3: qut class trong DEX ca app tm Activity.
                        // KuART read thng classes*.dex nn no/not cn classes.jar.
                        // Danh sch Activity tht verify c — dng lm fallback
                        // cho ActivityThread, khai bo scope ny dng c c
                        // khi khi qut no/not run.
                        std::vector<std::string> verifiedActivities;
                        if (targetActivity.empty()) {
                            std::vector<std::string> classes;
                            {
                                constexpr size_t kMaxClasses = 20000;
                                std::vector<char*> raw(kMaxClasses, nullptr);
                                const size_t n = kuart_list_app_classes(raw.data(), kMaxClasses);
                                classes.reserve(n);
                                for (size_t i = 0; i < n; ++i) classes.emplace_back(raw[i]);
                                kuart_free_class_list(raw.data(), n);
                            }
                            {
                                appendAndEcho("[kudroid_core] Scanning app DEX for Activity classes...");
                                appendAndEcho("[kudroid_core] Found " + std::to_string(classes.size()) +
                                              " non-system classes in app DEX");
                                // Chm im chn launcher:
                                // -1000 Activity ca SDK bn th ba (push/analytics
                                // v.v.) — no/not phi entry point, launch n
                                // ch cho screen/display xm (bi hc Braze).
                                // +100 tn cha "Activity"
                                // +50 package khp pkgName (t manifest/app_info)
                                // +depth bonus: package cng ngn (gc app) cng
                                // Heuristics to pick launcher:
                                // -1000 Activity of 3rd party SDK (push/analytics, etc.) — should not be entry point,
                                // launching them just shows blank screen (lesson learned from Braze).
                                // +100 name contains "Activity"
                                // +50 package matches pkgName (from manifest/app_info)
                                // +depth bonus: shorter package (app root) is more likely the main entry point.
                                //
                                // Prefixes, not substrings, and only packages that
                                // genuinely belong to an embedded SDK.
                                //
                                // A substring test rejects any class whose name merely
                                // CONTAINS one of these, wherever it appears: an app at
                                // com.google.* lost 1000 points for its own launcher,
                                // and so did every app with "adjust" or "amplitude"
                                // anywhere in a class name. Two entries were not SDKs at
                                // all — "zarchiver" is an app (ru.zdevs.zarchiver), and
                                // "unity3d" covers com.unity3d.player.UnityPlayerActivity
                                // which IS the launcher of essentially every Unity game.
                                static const char* kSdkActivityPrefixes[] = {
                                    "com/braze/", "com/appboy/", "com/facebook/",
                                    "com/firebase/", "com/google/firebase/",
                                    "com/google/android/gms/", "com/google/ads/",
                                    "com/google/android/play/", "com/appsflyer/",
                                    "com/adjust/sdk/", "com/amplitude/", "com/mixpanel/",
                                    "com/crashlytics/", "io/sentry/", "com/playfab/",
                                    "com/microsoft/appcenter/", "com/android/billingclient/",
                                    "com/onesignal/", "com/urbanairship/",
                                };
                                // Calculate score ONCE instead of inside loop for every
                                // class (large JARs can have 10k+ classes).
                                std::string pkgPrefix;
                                if (!pkgName.empty()) {
                                    // pkgName dng a.b.c → prefix "a/b/c/".
                                    pkgPrefix = pkgName;
                                    for (char& c : pkgPrefix) if (c == '.') c = '/';
                                    pkgPrefix += '/';
                                }
                                // A class inside the app's OWN package is never an
                                // embedded SDK, whatever it is called. This is what
                                // keeps an app published under com.google.* — or one
                                // that vendors an SDK into its own namespace — from
                                // blacklisting its own launcher.
                                auto isSdkOwned = [&pkgPrefix](const std::string& cls) {
                                    if (!pkgPrefix.empty() &&
                                        cls.compare(0, pkgPrefix.size(), pkgPrefix) == 0) {
                                        return false;
                                    }
                                    for (const char* prefix : kSdkActivityPrefixes) {
                                        if (cls.rfind(prefix, 0) == 0) return true;
                                    }
                                    return false;
                                };
                                std::string best;
                                int bestScore = -1;
                                std::string bestAny; // fallback nu no/not c *Activity no
                                int bestAnyScore = -1;
                                for (const auto& cls : classes) {
                                    const bool isActivity = cls.find("Activity") != std::string::npos;
                                    const size_t depth = static_cast<size_t>(
                                        std::count(cls.begin(), cls.end(), '/'));
                                    int score = (isActivity ? 100 : 0) + static_cast<int>(10 - depth);
                                    // Launcher activity hu nh lun tn "...MainActivity".
                                    if (cls.size() >= 13 &&
                                        cls.compare(cls.size() - 13, 13, "/MainActivity") == 0) score += 80;
                                    if (!pkgPrefix.empty() &&
                                        cls.compare(0, pkgPrefix.size(), pkgPrefix) == 0) score += 50;
                                    if (isSdkOwned(cls)) score -= 1000;

                                    if (isActivity && score > bestScore) {
                                        bestScore = score;
                                        best = cls;
                                    }
                                    if (score > bestAnyScore) {
                                        bestAnyScore = score;
                                        bestAny = cls;
                                    }
                                }
                                if (!best.empty() && bestScore > 0) {
                                    for (char& c : best) if (c == '/') c = '.';
                                    targetActivity = best;
                                    appendAndEcho("[kudroid_core] Resolved from app DEX: " + best);
                                } else if (!bestAny.empty() && bestAnyScore > 0) {
                                    // no/not c *Activity "sch" — dng class im cao nht.
                                    for (char& c : bestAny) if (c == '/') c = '.';
                                    targetActivity = bestAny;
                                    appendAndEcho("[kudroid_core] No clean *Activity class; using highest-scored app class: " + bestAny);
                                } else if (!classes.empty()) {
                                    // Mi class u b tr im SDK — log vi ci debug.
                                    for (size_t i = 0; i < classes.size() && i < 5; ++i) {
                                        appendAndEcho("[kudroid_core]   candidate(all-SDK?): " + classes[i]);
                                    }
                                    std::string first = classes.front();
                                    for (char& c : first) if (c == '/') c = '.';
                                    targetActivity = first;
                                    appendAndEcho("[kudroid_core] Using first app class as last resort: " + first);
                                }

                                // U TIN 3.5 — VERIFY: tn class no/not ni ln
                                // g khi app b ProGuard obfuscate (a.a.a v.v.).
                                // Kim tra candidate THT S extends
                                // android.app.Activity through KuART class inheritance.
                                //
                                // Score-based Activity resolution: avoid blindly picking the first candidate,
                                // as SDK sub-activities (analytics, push) extend Activity but do not render UI.
                                // Use kSdkActivityHints and scoring to select the main UI Activity.

                                if (!targetActivity.empty() &&
                                    kuart_class_extends_activity(targetActivity.c_str()) != 1) {
                                    appendAndEcho("[kudroid_core] Candidate '" + targetActivity +
                                                  "' does NOT extend Activity (obfuscated?). Verifying all classes...");
                                    targetActivity.clear();
                                    int verifiedBestScore = -1;
                                    std::string verifiedBest;
                                    int checked = 0;
                                    // Collect real candidate Activities as fallbacks for ActivityThread if the primary
                                    // candidate fails. Two-pass scan: Pass 0 checks names containing 'Activity',
                                    // Pass 1 inspects remaining classes (bounded at 2000 items for large DEX).
                                    for (int pass = 0; pass < 2 && verifiedBestScore <= 0; ++pass) {
                                        int checkedThisPass = 0;
                                        for (const auto& cls : classes) {
                                            const bool hasName = cls.find("Activity") != std::string::npos;
                                            if (pass == 0 && !hasName) continue;
                                            if (pass == 1 && hasName) continue;
                                            if (++checkedThisPass > (pass == 0 ? 8000 : 2000)) break;
                                            std::string dotted = cls;
                                            for (char& c : dotted) if (c == '/') c = '.';
                                            ++checked;
                                            if (kuart_class_extends_activity(dotted.c_str()) != 1) continue;
                                            // Real Activity candidate — calculate relevance score.
                                            const size_t depth = static_cast<size_t>(
                                                std::count(cls.begin(), cls.end(), '/'));
                                            int score = 100 + static_cast<int>(10 - depth);
                                            if (cls.size() >= 13 &&
                                                cls.compare(cls.size() - 13, 13, "/MainActivity") == 0) score += 80;
                                            if (!pkgPrefix.empty() &&
                                                cls.compare(0, pkgPrefix.size(), pkgPrefix) == 0) score += 50;
                                            if (isSdkOwned(cls)) score -= 1000;
                                            appendAndEcho("[kudroid_core]   Activity candidate: " + dotted +
                                                          " (score=" + std::to_string(score) + ")");
                                            verifiedActivities.push_back(dotted);
                                            if (score > verifiedBestScore) {
                                                verifiedBestScore = score;
                                                verifiedBest = dotted;
                                            }
                                            if (verifiedBestScore >= 185) break; // pkg match + MainActivity — high confidence match
                                        }
                                    }
                                    appendAndEcho("[kudroid_core] Activity verify done: " +
                                                  std::to_string(checked) + " classes checked");
                                    if (!verifiedBest.empty() && verifiedBestScore > 0) {
                                        targetActivity = verifiedBest;
                                        appendAndEcho("[kudroid_core] Verified best Activity: " + verifiedBest);
                                    } else if (!verifiedBest.empty()) {
                                        // All candidates belong to SDKs — select best-scoring candidate with a warning.
                                        targetActivity = verifiedBest;
                                        appendAndEcho("[kudroid_core] WARNING: only SDK Activities found; using least-bad: " + verifiedBest);
                                    } else {
                                        appendAndEcho("[kudroid_core] WARNING: no class in app DEX extends android.app.Activity");
                                    }
                                } else if (!targetActivity.empty()) {
                                    appendAndEcho("[kudroid_core] Verified: '" + targetActivity + "' extends Activity ✓");
                                }
                            }
                        }

                        // PRIORITY 4: heuristic guessing from package name only when prior sources fail.
                        std::string guessBase; // shared base name for fallback list
                        // Prefer any manifest-declared activity over a guess: the
                        // manifest is what Android itself reads.
                        if (targetActivity.empty() && !manifestActivities.empty()) {
                            targetActivity = manifestActivities.front();
                            appendAndEcho("[kudroid_core] Using first manifest-declared activity: " +
                                          targetActivity);
                        }
                        if (targetActivity.empty()) {
                            if (!pkgName.empty()) {
                                targetActivity = pkgName + ".MainActivity";
                            } else {
                                std::string base = appName;
                                auto uIdx = base.find('_');
                                if (uIdx != std::string::npos) {
                                    base = base.substr(0, uIdx);
                                }
                                targetActivity = base + ".MainActivity";
                            }
                            appendAndEcho("[kudroid_core] WARNING: guessed Activity '" + targetActivity +
                                          "' (manifest/dex-scan/JNI lookup failed)");
                        }

                        // Fallback list for ActivityThread, in descending order of
                        // authority:
                        //   1. activities the MANIFEST declares (launcher first),
                        //   2. classes verified to extend android.app.Activity.
                        //
                        // Names are no longer invented from the package. Guessed
                        // candidates like "<pkg>.Main" only ever produced
                        // ClassNotFoundException — an app either declares an activity
                        // in its manifest or it cannot be launched, which is exactly
                        // how Android decides. The only guess left is the last-resort
                        // targetActivity above, used when there is no manifest at all.
                        std::vector<std::string> fallbackStorage;
                        auto addFallback = [&](const std::string& s) {
                            if (s.empty() || s == targetActivity) return;
                            for (const auto& f : fallbackStorage)
                                if (f == s) return;
                            if (fallbackStorage.size() < 12) fallbackStorage.push_back(s);
                        };
                        for (const auto& a : manifestActivities) addFallback(a);
                        for (const auto& v : verifiedActivities) addFallback(v);
                        std::vector<const char*> fallbackPtrs;
                        for (const auto& f : fallbackStorage) fallbackPtrs.push_back(f.c_str());

                        appendAndEcho("[kudroid_core] Target Activity: " + targetActivity);
                        if (!fallbackPtrs.empty()) {
                            appendAndEcho("[kudroid_core] Fallback candidates: " +
                                          std::to_string(fallbackPtrs.size()));
                        }
                        if (!manifestComponentFactory.empty()) {
                            appendAndEcho("[kudroid_core] appComponentFactory: " +
                                          manifestComponentFactory);
                        }
                        if (!manifestAppClass.empty()) {
                            appendAndEcho("[kudroid_core] Application class: " + manifestAppClass);
                        }
                        appendAndEcho("[kudroid_core] Launching Android ActivityThread runtime (KuART)...");
                        kudroid::native_phase("activity-thread-before-main");

                        // Register the manifest BEFORE launching. A component reads its
                        // own entry while constructing itself — AGDK's GameActivity asks
                        // getActivityInfo(...).metaData for "android.app.lib_name" inside
                        // onCreate to find the .so holding its renderer — so registering
                        // after launch would be too late and the activity would come up
                        // with no native library and an empty surface.
                        {
                            std::vector<const char*> actPtrs;
                            for (const auto& a : manifestActivities) actPtrs.push_back(a.c_str());
                            kuart_register_package(pkgName.c_str(),
                                                   actPtrs.empty() ? nullptr : actPtrs.data(),
                                                   static_cast<int>(actPtrs.size()));

                            // Registers keys/values as parallel arrays; the storage has
                            // to outlive the call, hence the vectors of c_str().
                            const auto registerMeta =
                                [&](const std::string& component,
                                    const std::vector<kudroid::MetaDataEntry>& meta) {
                                    if (meta.empty()) return;
                                    std::vector<const char*> keys, values;
                                    keys.reserve(meta.size());
                                    values.reserve(meta.size());
                                    for (const auto& m : meta) {
                                        keys.push_back(m.name.c_str());
                                        values.push_back(m.value.c_str());
                                    }
                                    kuart_register_component_meta_data(
                                        component.c_str(), keys.data(), values.data(),
                                        static_cast<int>(keys.size()));
                                    appendAndEcho("[kudroid_core] meta-data registered for " +
                                                  (component.empty() ? std::string("<application>")
                                                                     : component) +
                                                  ": " + std::to_string(meta.size()) + " entries");
                                    for (const auto& m : meta) {
                                        appendAndEcho("[kudroid_core]     " + m.name + " = " +
                                                      m.value);
                                    }
                                };

                            registerMeta(std::string(), manifestInfo.applicationMetaData);
                            for (const auto& a : manifestInfo.activities) {
                                registerMeta(a.name, a.metaData);
                            }
                        }

                        if (!kuart_launch_app(pkgName.c_str(),
                                              manifestComponentFactory.c_str(),
                                              manifestAppClass.c_str(),
                                              targetActivity.c_str(),
                                              fallbackPtrs.empty() ? nullptr : fallbackPtrs.data(),
                                              static_cast<int>(fallbackPtrs.size()))) {
                            appendAndEcho("[kudroid_core] ERROR: ActivityThread.main failed: " +
                                          std::string(kuart_last_error()));
                        }
                        kudroid::native_phase("activity-thread-after-main");
                    }
                }
            }
        }
    }

    const char* trace = kudroid::bionic_shim_trace();
    if (trace && *trace) {
        log += "[kudroid_core] Bionic/global binding trace:\n";
        log += trace;
    }

    writeLogFile("kudroid_run_apk.txt", log);
    s_isApkRunning.store(false);
    char* result = static_cast<char*>(malloc(log.size() + 1));
    if (result) memcpy(result, log.c_str(), log.size() + 1);
    return result;
}

extern "C" int kudroid_self_test(void) {
    fprintf(stderr, "[kudroid_core] Self-test starting...\n");
    fprintf(stderr, "[kudroid_core] Creating ElfLoader with dummy path '/nonexistent'...\n");

    try {
        kudroid::ElfLoader loader("/nonexistent");

        fprintf(stderr, "[kudroid_core] ElfLoader constructed OK.\n");
        fprintf(stderr, "[kudroid_core] isLoaded() = %s\n", loader.isLoaded() ? "true" : "false");
        fprintf(stderr, "[kudroid_core] entryPoint() = 0x%llx\n", (unsigned long long)loader.entryPoint());
        fprintf(stderr, "[kudroid_core] segments count = %zu\n", loader.segments().size());

        fprintf(stderr, "[kudroid_core] Calling parse()...\n");
        bool ok = loader.parse();
        fprintf(stderr, "[kudroid_core] parse() returned %s\n", ok ? "true" : "false");

        fprintf(stderr, "[kudroid_core] Self-test PASSED.\n");
        (void)loader;
        return 0;
    } catch (...) {
        fprintf(stderr, "[kudroid_core] Self-test FAILED: exception thrown!\n");
        return -1;
    }
}

extern "C" const char* kudroid_self_test_log(void) {
    std::string log;
    appendTestHeader(log, "Self-Test", "/nonexistent");
    log += "[kudroid_core] Phase: construct ElfLoader\n";
    log += "[kudroid_core] Creating ElfLoader with dummy path '/nonexistent'...\n";

    try {
        kudroid::ElfLoader loader("/nonexistent");

        log += "[kudroid_core] ElfLoader constructed OK.\n";

        char buf[256];
        snprintf(buf, sizeof(buf), "[kudroid_core] isLoaded() = %s\n", loader.isLoaded() ? "true" : "false");
        log += buf;

        snprintf(buf, sizeof(buf), "[kudroid_core] entryPoint() = 0x%llx\n", (unsigned long long)loader.entryPoint());
        log += buf;

        snprintf(buf, sizeof(buf), "[kudroid_core] segments count = %zu\n", loader.segments().size());
        log += buf;

        log += "[kudroid_core] Phase: parse()\n";
        bool ok = loader.parse();

        snprintf(buf, sizeof(buf), "[kudroid_core] parse() returned %s\n", ok ? "true" : "false");
        log += buf;

        log += "[kudroid_core] Self-test PASSED.\n";
        (void)loader;

        writeLogFile("kudroid_selftest.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) {
            memcpy(result, log.c_str(), log.size() + 1);
        }
        return result;
    } catch (...) {
        log += "[kudroid_core] Self-test FAILED: exception thrown!\n";

        writeLogFile("kudroid_selftest.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) {
            memcpy(result, log.c_str(), log.size() + 1);
        }
        return result;
    }
}

extern "C" const char* kudroid_load_elf(const char* path) {
    std::string log;
    appendTestHeader(log, "ELF Load", path);
    if (!path) {
        log += "[kudroid_core] ERROR: null path\n";
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    }

    char buf[512];
    snprintf(buf, sizeof(buf), "[kudroid_core] Loading ELF: %s\n", path);
    log += buf;

    try {
        kudroid::ElfLoader loader(path);

        log += "[kudroid_core] Phase: parse ELF headers\n";
        bool ok = loader.parse();

        if (!ok) {
            snprintf(buf, sizeof(buf), "[kudroid_core] PARSE FAILED: %s\n", loader.lastError());
            log += buf;
        } else {
            log += "[kudroid_core] ELF parsed successfully.\n";

            snprintf(buf, sizeof(buf), "[kudroid_core]   Entry point: 0x%llx\n", (unsigned long long)loader.entryPoint());
            log += buf;

            snprintf(buf, sizeof(buf), "[kudroid_core]   PT_LOAD segments: %zu\n", loader.segments().size());
            log += buf;

            int segNum = 0;
            for (const auto& seg : loader.segments()) {
                snprintf(buf, sizeof(buf),
                    "[kudroid_core]   [%d] vaddr=0x%llx offset=0x%llx filesz=%llu memsz=%llu flags=0x%x\n",
                    segNum++,
                    (unsigned long long)seg.vaddr,
                    (unsigned long long)seg.offset,
                    (unsigned long long)seg.filesz,
                    (unsigned long long)seg.memsz,
                    seg.flags);
                log += buf;
            }

            // attempt mapping (currently stubbed)
            log += "[kudroid_core] Phase: map PT_LOAD segments\n";
            if (loader.map()) {
                log += "[kudroid_core] Map OK.\n";
            } else {
                snprintf(buf, sizeof(buf), "[kudroid_core] Map failed: %s\n", loader.lastError());
                log += buf;
            }

            // attempt relocation (currently stubbed)
            log += "[kudroid_core] Phase: resolve relocations/imports\n";
            if (loader.relocate()) {
                log += "[kudroid_core] Relocate OK.\n";
            } else {
                snprintf(buf, sizeof(buf), "[kudroid_core] Relocate failed: %s\n", loader.lastError());
                log += buf;
            }
        }

        log += "[kudroid_core] Load complete.\n";

        writeLogFile("kudroid_load.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    } catch (...) {
        log += "[kudroid_core] EXCEPTION during load!\n";
        writeLogFile("kudroid_load.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    }
}

extern "C" const char* kudroid_execution_test(const char* path) {
    std::string log;
    appendTestHeader(log, "ELF Execution", path);
    if (!path) {
        log += "[kudroid_core] ERROR: null path\n";
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    }

    char buf[512];
    snprintf(buf, sizeof(buf), "[kudroid_core] Execution test for: %s\n", path);
    log += buf;

    // Refuse native execution when JIT is disabled: executing PROT_EXEC pages
    // without dynamic code signing terminates the process abruptly.
    if (!kudroid_jit_available()) {
        log += "[kudroid_core] ABORT: JIT is Disabled — cannot execute native code.\n";
        log += "[kudroid_core] Enable JIT in LiveContainer and retry.\n";
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    }

    try {
        kudroid::ElfLoader loader(path);

        if (!loader.parse()) {
            snprintf(buf, sizeof(buf), "[kudroid_core] PARSE FAILED: %s\n", loader.lastError());
            log += buf;
            writeLogFile("kudroid_exec.txt", log);
            char* result = (char*)malloc(log.size() + 1);
            if (result) memcpy(result, log.c_str(), log.size() + 1);
            return result;
        }

        log += "[kudroid_core] Phase: parse -> OK\n";

        if (!loader.map()) {
            snprintf(buf, sizeof(buf), "[kudroid_core] MAP FAILED: %s\n", loader.lastError());
            log += buf;
            writeLogFile("kudroid_exec.txt", log);
            char* result = (char*)malloc(log.size() + 1);
            if (result) memcpy(result, log.c_str(), log.size() + 1);
            return result;
        }

        log += "[kudroid_core] Phase: map -> OK\n";

        if (!loader.relocate()) {
            snprintf(buf, sizeof(buf), "[kudroid_core] RELOCATE FAILED: %s\n", loader.lastError());
            log += buf;
            writeLogFile("kudroid_exec.txt", log);
            char* result = (char*)malloc(log.size() + 1);
            if (result) memcpy(result, log.c_str(), log.size() + 1);
            return result;
        }

        log += "[kudroid_core] Phase: relocate/import binding -> OK\n";
        log += "[kudroid_core] Phase: invoke exported kudroid_add(40, 20)\n";

        // Snapshot logs for crash handler prior to executing JIT code.
        mirrorCrash(log);

        std::string execResult = loader.testExecution();
        log += execResult;
        log += "\n";

        writeLogFile("kudroid_exec.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    } catch (...) {
        log += "[kudroid_core] EXCEPTION during execution test!\n";
        writeLogFile("kudroid_exec.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    }
}

extern "C" const char* kudroid_bionic_execution_test(const char* path) {
    std::string log;
    appendTestHeader(log, "Bionic Shim Execution", path);
    kudroid::bionic_shim_reset_trace();
    if (!path) {
        log += "[kudroid_core] ERROR: null path\n";
    } else if (!kudroid_jit_available()) {
        log += "[kudroid_core] ABORT: JIT is Disabled\n";
    } else {
        kudroid::ElfLoader loader(path);
        if (!loader.parse()) {
            log += "[kudroid_core] PARSE FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.map()) {
            log += "[kudroid_core] MAP FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.relocate()) {
            log += "[kudroid_core] RELOCATE FAILED: " + std::string(loader.lastError()) + "\n";
        } else {
            log += "[kudroid_core] ELF mapped and Bionic imports bound.\n";
            void* address = loader.getSymbolAddress("kudroid_bionic_test");
            if (!address) {
                log += "[kudroid_core] SYMBOL FAILED: kudroid_bionic_test not found\n";
            } else {
                log += "[kudroid_core] Running kudroid_bionic_test()...\n";
                const char* shimTrace = kudroid::bionic_shim_trace();
                if (shimTrace && *shimTrace) {
                    log += "[kudroid_core] Bionic trace before call:\n";
                    log += shimTrace;
                }
                mirrorCrash(log);
                using TestFunction = int (*)();
                const int result = reinterpret_cast<TestFunction>(address)();
                log += "[kudroid_core] BIONIC TEST RESULT: " +
                       std::to_string(result) + (result == 0 ? " (SUCCESS)\n" : " (FAILED)\n");
            }
        }
    }

    const char* shimTrace = kudroid::bionic_shim_trace();
    if (shimTrace && *shimTrace) {
        log += "[kudroid_core] Bionic trace:\n";
        log += shimTrace;
    }

    writeLogFile("kudroid_bionic_test.txt", log);
    char* result = static_cast<char*>(malloc(log.size() + 1));
    if (result) memcpy(result, log.c_str(), log.size() + 1);
    return result;
}

extern "C" const char* kudroid_multi_elf_test(const char* consumerPath,
                                               const char* providerPath) {
    std::string log;
    appendTestHeader(log, "Multi-ELF Dependency Resolution", consumerPath);
    kudroid::bionic_shim_reset_trace();
    log += "[kudroid_core] Phase: create LibraryManager\n";

    if (!consumerPath || !providerPath) {
        log += "[kudroid_core] ERROR: null ELF path\n";
    } else {
        kudroid::LibraryManager manager;
        log += "[kudroid_core] Phase: parse DT_NEEDED\n";
        const auto dependencies = kudroid::parse_elf_dependencies(consumerPath);
        log += "[kudroid_core] DT_NEEDED count: " +
               std::to_string(dependencies.size()) + "\n";
        for (const auto& dependency : dependencies) {
            log += "[kudroid_core]   dependency: " + dependency + "\n";
        }

        log += "[kudroid_core] Phase: load primary ELF recursively\n";
        if (!manager.loadRecursive(consumerPath)) {
            log += "[kudroid_core] PRIMARY LOAD FAILED: " +
                   manager.lastError() + "\n";
        } else {
            log += "[kudroid_core] Primary ELF load OK\n";
        }

        log += "[kudroid_core] Phase: load sibling ELF recursively\n";
        if (!manager.loadRecursive(providerPath)) {
            log += "[kudroid_core] SIBLING LOAD FAILED: " +
                   manager.lastError() + "\n";
        } else {
            log += "[kudroid_core] Sibling ELF load OK\n";
        }

        const std::size_t loadedCount = manager.libraries().size();
        log += "[kudroid_core] Loaded library count: " +
               std::to_string(loadedCount) + "\n";
        for (const auto& library : manager.libraries()) {
            log += "[kudroid_core]   loaded: " + library.first + "\n";
        }

        log += "[kudroid_core] Phase: duplicate-load prevention\n";
        const bool duplicateLoad = manager.loadRecursive(consumerPath);
        log += duplicateLoad && manager.libraries().size() == loadedCount
            ? "[kudroid_core] Duplicate load skipped successfully\n"
            : "[kudroid_core] Duplicate load check failed\n";

        log += "[kudroid_core] Phase: global symbol resolution\n";
        void* symbol = manager.resolveGlobalSymbol("kudroid_dependency_value");
        log += symbol
            ? "[kudroid_core] Global symbol kudroid_dependency_value resolved from provider\n"
            : "[kudroid_core] Global symbol kudroid_dependency_value NOT resolved\n";

        log += "[kudroid_core] Phase: execute consumer -> provider call\n";
        void* consumerSymbol = manager.resolveGlobalSymbol("kudroid_multi_elf_test");
        if (!consumerSymbol) {
            log += "[kudroid_core] Consumer symbol kudroid_multi_elf_test NOT resolved\n";
        } else if (!kudroid_jit_available()) {
            log += "[kudroid_core] Consumer execution skipped: JIT Disabled\n";
        } else {
            const char* trace = kudroid::bionic_shim_trace();
            if (trace && *trace) {
                log += "[kudroid_core] Import trace before execution:\n";
                log += trace;
            }
            mirrorCrash(log);
            using MultiElfFunction = int (*)();
            const int result = reinterpret_cast<MultiElfFunction>(consumerSymbol)();
            log += "[kudroid_core] Consumer returned: " + std::to_string(result) + "\n";
            log += result == 42
                ? "[kudroid_core] MULTI-ELF TEST RESULT: SUCCESS (35 + 7 = 42)\n"
                : "[kudroid_core] MULTI-ELF TEST RESULT: FAILED (expected 42)\n";
        }
    }

    const char* trace = kudroid::bionic_shim_trace();
    if (trace && *trace) {
        log += "[kudroid_core] Bionic/global binding trace:\n";
        log += trace;
    }

    writeLogFile("kudroid_multi_elf_test.txt", log);
    char* result = static_cast<char*>(malloc(log.size() + 1));
    if (result) memcpy(result, log.c_str(), log.size() + 1);
    return result;
}

extern "C" void kudroid_gpu_cleanup_on_test_exit(void);

extern "C" const char* kudroid_run_so_test(const char* soPath, const char* entrypoint) {
    std::string log;
    appendTestHeader(log, "Dynamic Remote .so Test Execution", soPath);
    kudroid::bionic_shim_reset_trace();
    kudroid_gpu_cleanup_on_test_exit();

    if (!soPath) {
        log += "❌ ERROR: null SO path provided\n";
        return strdup(log.c_str());
    }

    log += "[kudroid_runner] Loading library: " + std::string(soPath) + "\n";
    kudroid::LibraryManager manager;

    if (!manager.loadRecursive(soPath)) {
        log += "❌ LOAD FAILED: " + manager.lastError() + "\n";
        const char* trace = kudroid::bionic_shim_trace();
        if (trace && *trace) {
            log += "\n[kudroid_runner] Binding trace during failure:\n";
            log += trace;
        }
        return strdup(log.c_str());
    }
    log += "✔ Library loaded successfully with " + std::to_string(manager.libraries().size()) + " dependencies resolved\n";

    // Locate entry point
    std::vector<std::string> candidateNames;
    if (entrypoint && strlen(entrypoint) > 0) {
        candidateNames.push_back(entrypoint);
    }
    candidateNames.push_back("kudroid_test_main");
    candidateNames.push_back("kudroid_main");
    candidateNames.push_back("test_main");
    candidateNames.push_back("main");

    void* symbol = nullptr;
    std::string foundName;
    for (const auto& name : candidateNames) {
        symbol = manager.resolveGlobalSymbol(name.c_str());
        if (symbol) {
            foundName = name;
            break;
        }
    }

    if (!symbol) {
        log += "❌ ERROR: No recognized entrypoint symbol found! Candidates tried:\n";
        for (const auto& name : candidateNames) log += "  - " + name + "\n";
        return strdup(log.c_str());
    }
    log += "✔ Entrypoint resolved: '" + foundName + "' at " + std::to_string(reinterpret_cast<uintptr_t>(symbol)) + "\n";

    log += "🚀 Executing test function in sandbox...\n\n";
    mirrorCrash(log);

    // Support 64-bit function signatures returning const char* or uintptr_t
    typedef const char* (*StringTestFn)();
    auto strFn = reinterpret_cast<StringTestFn>(symbol);
    const char* outputStr = strFn();

    if (outputStr && reinterpret_cast<uintptr_t>(outputStr) > 0x10000) {
        log += "=== OUTPUT FROM TEST .SO ===\n";
        log += std::string(outputStr) + "\n";
    } else {
        const int retCode = static_cast<int>(reinterpret_cast<intptr_t>(outputStr));
        log += "=== TEST STATUS RETURNED ===\n";
        log += "Exit Code: " + std::to_string(retCode) + (retCode == 0 ? " (SUCCESS ✔)\n" : " (FAILED ❌)\n");
    }

    const char* trace = kudroid::bionic_shim_trace();
    if (trace && *trace) {
        log += "\n[kudroid_runner] Bionic runtime shim trace:\n";
        log += trace;
    }

    kudroid_gpu_cleanup_on_test_exit();
    log += "\n🎉 Test execution finished.\n";
    return strdup(log.c_str());
}

extern "C" int kudroid_clear_app_cache(const char* package_name) {
    if (!package_name) return 0;
    
    // construct app cache directory path using VFS path remapper
    const std::string androidRoot = kudroid::VFSPathRemapper::getInstance().androidRoot();
    std::filesystem::path cachePath = std::filesystem::path(androidRoot) / "data/data" / package_name / "cache";
    std::filesystem::path codeCachePath = std::filesystem::path(androidRoot) / "data/data" / package_name / "code_cache";
    
    std::error_code ec;
    int success = 1;
    if (std::filesystem::exists(cachePath, ec)) {
        if (std::filesystem::remove_all(cachePath, ec) == static_cast<std::uintmax_t>(-1)) {
            success = 0;
        }
    }
    if (std::filesystem::exists(codeCachePath, ec)) {
        if (std::filesystem::remove_all(codeCachePath, ec) == static_cast<std::uintmax_t>(-1)) {
            success = 0;
        }
    }
    return success;
}

extern "C" int kudroid_delete_app(const char* package_name);

// Uninstall app with step-by-step progress callback (UTF-8 phase description, percent 0-100).
// Runs synchronously on caller thread — Swift UI dispatches to main thread.
// Provides real-time UI progress feedback during large asset/file tree deletion.
typedef void (*kudroid_delete_progress_cb)(const char* phase, int percent, void* userdata);

namespace {
// ── [DEBUG UNINSTALL] Step-by-Step Uninstall Trace ───────────────────────────
// Tracks full progress of file removals to <Documents>/logs/kudroid_uninstall_debug.txt
// for diagnosis during large app deletions.
std::string g_uninstallDbg;

void uninstallDbg(const std::string& msg) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    struct tm tmv;
    localtime_r(&now.tv_sec, &tmv);
    char ts[40];
    std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%03ld",
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec, now.tv_nsec / 1000000);
    g_uninstallDbg += std::string("[") + ts + "] " + msg + "\n";
    if (g_logDir[0]) {
        const std::string path =
            std::string(g_logDir) + "/kudroid_uninstall_debug.txt";
        if (FILE* f = fopen(path.c_str(), "w")) {
            fwrite(g_uninstallDbg.data(), 1, g_uninstallDbg.size(), f);
            fclose(f);
            ::chmod(path.c_str(), 0644); // readable via AFC/USB file sharing like stderr.log
        }
    }
}

// Summarize file/directory status (existence, type, permissions) for early diagnostics.
std::string describePath(const std::filesystem::path& p) {
    std::error_code ec;
    const std::string raw = p.string();
    std::string s = raw;
    if (!std::filesystem::exists(p, ec)) {
        s += ec ? " [exists-err: " + ec.message() + "]" : " [MISSING]";
    } else {
        s += std::filesystem::is_directory(p, ec) ? " [dir]" : " [file]";
    }
    if (::access(raw.c_str(), W_OK) != 0)
        s += std::string(" [NOT-WRITABLE errno=") + std::strerror(errno) + "]";
    else
        s += " [writable]";
    return s;
}

// Fast top-level child directory pruning via atomic remove_all operations.
// Avoids deep recursive filesystem traversal overhead on large asset trees.
void removeTreeWithProgress(const std::filesystem::path& p,
                            const char* phase,
                            kudroid_delete_progress_cb cb, void* ud,
                            double basePct, double spanPct,
std::uint64_t /*unused parameter*/) {
    const auto nowMs = [] {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<std::int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
    };
    std::error_code ec;
    uninstallDbg("phase='" + std::string(phase) + "' path=" + describePath(p));
    if (!std::filesystem::exists(p, ec)) return;

    // Single file entry (rare).
    if (!std::filesystem::is_directory(p, ec)) {
        const std::int64_t t0 = nowMs();
        std::filesystem::remove(p, ec);
        uninstallDbg("  single-file removed in " + std::to_string(nowMs() - t0) +
                     "ms ec=" + (ec ? ec.message() : std::string("none")));
        return;
    }

    // List top-level directory children non-recursively.
    std::vector<std::filesystem::path> children;
    for (std::filesystem::directory_iterator cIt(p, ec), cEnd;
         cIt != cEnd && !ec; cIt.increment(ec)) {
        children.push_back(cIt->path());
    }
    if (ec) uninstallDbg("  listing FAILED: " + ec.message());

    const std::int64_t t0 = nowMs();
    int idx = 0;
    bool hadErrors = false;
    for (const auto& child : children) {
        const std::int64_t childStart = nowMs();
        std::error_code rmEc;
        const auto n = std::filesystem::remove_all(child, rmEc);
        const std::int64_t ms = nowMs() - childStart;
        if (rmEc) hadErrors = true;
        uninstallDbg("  [" + std::to_string(++idx) + "/" +
                     std::to_string(children.size()) + "] remove_all " +
                     child.filename().string() + " -> " + std::to_string(n) +
                     " entries in " + std::to_string(ms) + "ms" +
                     (rmEc ? " ec=" + rmEc.message() : "") +
                     (ms >= 2000 ? " \u26a0\ufe0f SLOW" : ""));
        if (cb && spanPct > 0 && !children.empty()) {
            const int pct = static_cast<int>(
                basePct + spanPct * static_cast<double>(idx) /
                              static_cast<double>(children.size()));
            cb(phase, pct, ud);
        }
    }
    // Remove root directory (now empty).
    std::error_code raEc;
    std::filesystem::remove_all(p, raEc);
    uninstallDbg("  phase done in " + std::to_string(nowMs() - t0) +
                 "ms children=" + std::to_string(children.size()) +
                 " residue=" +
                 (std::filesystem::exists(p, raEc) ? "YES(still-there!)" : "no") +
                 (hadErrors ? " HAD_ERRORS" : ""));
}
} // namespace

extern "C" int kudroid_delete_app_progress(const char* package_name,
                                           kudroid_delete_progress_cb cb,
                                           void* userdata) {
    g_uninstallDbg.clear();
    uninstallDbg(std::string("=== UNINSTALL START pkg=") +
                 (package_name ? package_name : "(null)") +
                 " cb=" + (cb ? "yes" : "NO") + " ===");
    if (!package_name) {
        uninstallDbg("ABORT: package_name null");
        return 0;
    }

    const std::string androidRoot = kudroid::VFSPathRemapper::getInstance().androidRoot();
    uninstallDbg("androidRoot=" + androidRoot);
    const std::filesystem::path appCodePath =
        std::filesystem::path(androidRoot) / "data/app" / package_name;
    const std::filesystem::path appDataPath =
        std::filesystem::path(androidRoot) / "data/data" / package_name;
    const std::filesystem::path dalvikRoot =
        std::filesystem::path(androidRoot) / "data/dalvik-cache";
    uninstallDbg("code:   " + describePath(appCodePath));
    uninstallDbg("data:   " + describePath(appDataPath));
    uninstallDbg("dalvik: " + describePath(dalvikRoot));

    // Avoid recursive tree byte scanning; progress is tracked across top-level folders.
    std::error_code ec;
    // Prune any legacy dalvik-cache directory artifacts.
    std::vector<std::filesystem::path> dalvikMatches;
    if (std::filesystem::is_directory(dalvikRoot, ec)) {
        for (const auto& e : std::filesystem::directory_iterator(dalvikRoot, ec)) {
            const std::string name = e.path().filename().string();
            if (name == package_name || name.rfind(std::string(package_name) + "_", 0) == 0)
                dalvikMatches.push_back(e.path());
        }
    }
    uninstallDbg("dalvikMatches=" + std::to_string(dalvikMatches.size()));

    int success = 1;
    // Chia %: code 0-45, data 45-70, dalvik 70-100.
    removeTreeWithProgress(appCodePath, "Removing app files",
                           cb, userdata, 0.0, 45.0, 0);
    removeTreeWithProgress(appDataPath, "Removing app data",
                           cb, userdata, 45.0, 25.0, 0);

    if (cb) cb("Removing compiled cache", 70, userdata);
    for (const auto& m : dalvikMatches) {
        removeTreeWithProgress(m, "Removing compiled cache",
                               cb, userdata, 70.0, 30.0, 0);
    }
    if (cb) cb("Done", 100, userdata);
    uninstallDbg("=== UNINSTALL DONE success=" + std::to_string(success) + " ===");
    return success;
}

extern "C" int kudroid_delete_app(const char* package_name) {
    return kudroid_delete_app_progress(package_name, nullptr, nullptr);
}

// Detailed debug log from the most recent uninstall operation.
// Returned string is malloced; caller must free().
extern "C" const char* kudroid_uninstall_debug_log(void) {
    char* out = static_cast<char*>(std::malloc(g_uninstallDbg.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, g_uninstallDbg.c_str(), g_uninstallDbg.size() + 1);
    return out;
}

extern "C" const char* kudroid_get_app_info(const char* package_name) {
    if (!package_name) return strdup("{}");
    
    const std::string androidRoot = kudroid::VFSPathRemapper::getInstance().androidRoot();
    std::filesystem::path appDir = std::filesystem::path(androidRoot) / "data/data" / package_name;
    
    uintmax_t totalSize = 0;
    std::error_code ec;
    
    if (std::filesystem::exists(appDir, ec)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(appDir, ec)) {
            if (!std::filesystem::is_directory(entry.status(ec))) {
                totalSize += std::filesystem::file_size(entry, ec);
            }
        }
    }
    
    char buf[512];
    std::snprintf(buf, sizeof(buf), 
        "{\n"
        "  \"package_name\": \"%s\",\n"
        "  \"data_size_bytes\": %llu,\n"
        "  \"installed\": %s\n"
        "}", 
        package_name, 
        static_cast<unsigned long long>(totalSize),
        std::filesystem::exists(appDir, ec) ? "true" : "false");
        
    return strdup(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
extern "C" const char* kudroid_syscall_so_test(const char* path) {
    std::string log;
    appendTestHeader(log, "Syscall Traps (ARM64 .so) Test", path);
    if (!path) {
        log += "[kudroid_syscall] ERROR: null path\n";
    } else if (!kudroid_jit_available()) {
        log += "[kudroid_syscall] ABORT: JIT is Disabled — cannot execute ARM64 ELF\n";
    } else {
        kudroid::ElfLoader loader(path);
        if (!loader.parse()) {
            log += "[kudroid_syscall] PARSE FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.map()) {
            log += "[kudroid_syscall] MAP FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.relocate()) {
            log += "[kudroid_syscall] RELOCATE FAILED: " + std::string(loader.lastError()) + "\n";
        } else {
            log += "[kudroid_syscall] ELF mapped and Bionic imports bound.\n";
            mirrorCrash(log);
            void* address = loader.getSymbolAddress("kudroid_syscall_test");
            if (!address) {
                log += "[kudroid_syscall] SYMBOL FAILED: kudroid_syscall_test not found\n";
            } else {
                log += "[kudroid_syscall] Running kudroid_syscall_test()...\n";
                int (*test_func)() = reinterpret_cast<int (*)()>(address);
                
                // clear BionicShim trace buffer
                kudroid::bionic_shim_reset_trace();
                mirrorCrash(log);
                
                int result = test_func();
                log += "[kudroid_syscall] SYSCALL TEST RESULT: " +
                       (result == 0 ? std::string("0 (SUCCESS)") : std::to_string(result) + " (FAILED)") + "\n";
            }
        }
        log += "[kudroid_syscall] Bionic shim trace:\n";
        log += "[BionicShim] ";
        log += kudroid::bionic_shim_trace();
        log += "\n";
    }
    appendCrashSnapshot(log, "log up to test end");
    writeLogFile("kudroid_syscall_test.txt", log);
    return strdup(log.c_str());
}

extern "C" const char* kudroid_jni_massive_so_test(const char* path) {
    std::string log;
    appendTestHeader(log, "Massive JNI 200+ Functions Test", path);
    if (!path) {
        log += "[kudroid_jni] ERROR: null path\n";
    } else if (!kudroid_jit_available()) {
        log += "[kudroid_jni] ABORT: JIT is Disabled — cannot execute ARM64 ELF\n";
    } else {
        kudroid::ElfLoader loader(path);
        if (!loader.parse()) {
            log += "[kudroid_jni] PARSE FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.map()) {
            log += "[kudroid_jni] MAP FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.relocate()) {
            log += "[kudroid_jni] RELOCATE FAILED: " + std::string(loader.lastError()) + "\n";
        } else {
            loader.registerEhFrame();
            loader.executeInit();
            log += "[kudroid_jni] ELF mapped, relocated, and initialized.\n";
            mirrorCrash(log);
            
            // execute JNI_OnLoad if exported
            void* jniOnLoadAddr = loader.getSymbolAddress("JNI_OnLoad");
            if (jniOnLoadAddr) {
                log += "[kudroid_jni] Found JNI_OnLoad, executing...\n";
                mirrorCrash(log);
                // retrieve JavaVM from bridge
                using JNI_OnLoad_t = jint (*)(JavaVM*, void*);
                JNI_OnLoad_t jniOnLoad = reinterpret_cast<JNI_OnLoad_t>(jniOnLoadAddr);
                jint version = jniOnLoad(kuart_get_javavm(), nullptr);
                log += "[kudroid_jni] JNI_OnLoad returned version: 0x" + std::to_string(version) + "\n";
                // Same rule as the System.loadLibrary path: a library that left a
                // Java exception in flight must not have it attributed to whatever
                // runs next on this thread.
                const char* leaked = nullptr;
                if (kuart_take_pending_exception(&leaked)) {
                    log += "[kudroid_jni] JNI_OnLoad left a pending Java exception; cleared:\n";
                    log += leaked != nullptr ? leaked : "?";
                    log += "\n";
                }
                mirrorCrash(log);
            }
            
            void* address = loader.getSymbolAddress("kudroid_jni_massive_test");
            if (!address) {
                log += "[kudroid_jni] SYMBOL FAILED: kudroid_jni_massive_test not found\n";
            } else {
                log += "[kudroid_jni] Running kudroid_jni_massive_test()...\n";
                mirrorCrash(log);
                int (*test_func)(void*) = reinterpret_cast<int (*)(void*)>(address);
                
                // configure callbacks
                static std::string* g_jni_test_log = &log;
                g_jni_test_log = &log;
                kuart_set_log_callback([](const char* msg) {
                    if (g_jni_test_log) {
                        *g_jni_test_log += "[KuART] ";
                        *g_jni_test_log += msg;
                        *g_jni_test_log += "\n";
                    }
                });

                kuart_init(""); // embedded framework is sufficient for this test
                mirrorCrash(log);
                void* vm = kuart_get_javavm();
                
                int result = test_func(vm);
                log += "[kudroid_jni] TEST RESULT: " +
                       (result == 0 ? std::string("0 (SUCCESS)") : std::to_string(result) + " (FAILED)") + "\n";
                mirrorCrash(log);
                log += "[kudroid_jni] TEST RESULT: " +
                       (result == 0 ? std::string("0 (SUCCESS)") : std::to_string(result) + " (FAILED)") + "\n";
            }
        }
    }
    writeLogFile("kudroid_jni_massive_test.txt", log);
    return strdup(log.c_str());
}

// GPU .so execution test — loads ARM64 ELF test module via ELF loader,
// intercepting dlopen/dlsym calls to GPU libraries via BionicShim to map
// directly to iOS native graphics (MoltenVK / ANGLE).
// ─────────────────────────────────────────────────────────────────────────────

extern "C" const char* kudroid_gpu_vulkan_so_test(const char* path) {
    std::string log;
    appendTestHeader(log, "GPU Vulkan .so Intercept Test", path);
    kudroid::bionic_shim_reset_trace();
    if (!path) {
        log += "[kudroid_gpu] ERROR: null path\n";
    } else if (!kudroid_jit_available()) {
        log += "[kudroid_gpu] ABORT: JIT is Disabled — cannot execute ARM64 ELF\n";
    } else {
        kudroid::ElfLoader loader(path);
        if (!loader.parse()) {
            log += "[kudroid_gpu] PARSE FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.map()) {
            log += "[kudroid_gpu] MAP FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.relocate()) {
            log += "[kudroid_gpu] RELOCATE FAILED: " + std::string(loader.lastError()) + "\n";
        } else {
            log += "[kudroid_gpu] ELF mapped and Bionic imports bound.\n";
            mirrorCrash(log);
            void* address = loader.getSymbolAddress("kudroid_gpu_vulkan_test");
            if (!address) {
                log += "[kudroid_gpu] SYMBOL FAILED: kudroid_gpu_vulkan_test not found\n";
            } else {
                log += "[kudroid_gpu] Running kudroid_gpu_vulkan_test()...\n";
                mirrorCrash(log);
                using VkTestFn = int (*)(uint32_t*);
                uint32_t ext_count = 0;
                const int result = reinterpret_cast<VkTestFn>(address)(&ext_count);
                log += "[kudroid_gpu] VULKAN TEST RESULT: " +
                       std::to_string(result) + (result == 0 ? " (SUCCESS)" : " (FAILED)") + "\n";
                log += "[kudroid_gpu] Vulkan extensions found: " + std::to_string(ext_count) + "\n";
            }
        }
    }

    appendCrashSnapshot(log, "log up to test end");

    const char* shimTrace = kudroid::bionic_shim_trace();
    if (shimTrace && *shimTrace) {
        log += "[kudroid_gpu] Bionic shim trace:\n";
        log += shimTrace;
    }

    writeLogFile("kudroid_gpu_vulkan_test.txt", log);
    return strdup(log.c_str());
}

extern "C" const char* kudroid_gpu_opengl_so_test(const char* path) {
    std::string log;
    appendTestHeader(log, "GPU OpenGL+EGL .so Intercept Test", path);
    kudroid::bionic_shim_reset_trace();
    if (!path) {
        log += "[kudroid_gpu] ERROR: null path\n";
    } else if (!kudroid_jit_available()) {
        log += "[kudroid_gpu] ABORT: JIT is Disabled — cannot execute ARM64 ELF\n";
    } else {
        kudroid::ElfLoader loader(path);
        if (!loader.parse()) {
            log += "[kudroid_gpu] PARSE FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.map()) {
            log += "[kudroid_gpu] MAP FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.relocate()) {
            log += "[kudroid_gpu] RELOCATE FAILED: " + std::string(loader.lastError()) + "\n";
        } else {
            log += "[kudroid_gpu] ELF mapped and Bionic imports bound.\n";
            mirrorCrash(log);
            void* address = loader.getSymbolAddress("kudroid_gpu_opengl_test");
            if (!address) {
                log += "[kudroid_gpu] SYMBOL FAILED: kudroid_gpu_opengl_test not found\n";
            } else {
                log += "[kudroid_gpu] Running kudroid_gpu_opengl_test()...\n";
                mirrorCrash(log);
                using GlTestFn = int (*)();
                const int result = reinterpret_cast<GlTestFn>(address)();
                log += "[kudroid_gpu] OPENGL+EGL TEST RESULT: " +
                       std::to_string(result) + (result == 0 ? " (SUCCESS)" : " (FAILED)") + "\n";
            }
        }
    }

    appendCrashSnapshot(log, "log up to test end");

    const char* shimTrace = kudroid::bionic_shim_trace();
    if (shimTrace && *shimTrace) {
        log += "[kudroid_gpu] Bionic shim trace:\n";
        log += shimTrace;
    }

    writeLogFile("kudroid_gpu_opengl_test.txt", log);
    return strdup(log.c_str());
}

#include "kudroid/APKExtractor.h"

extern "C" const char* kudroid_load_apk(const char* apkPath) {
    // Use local buffer + strdup: avoids non-thread-safe static buffers.
    std::string log;
    appendTestHeader(log, "APK Loader execution", apkPath);
    
    if (!apkPath) {
        log += "[kudroid_apk] ERROR: APK path is null\n";
        return strdup(log.c_str());
    }
    
    std::string apkStr(apkPath);
    // Documents, not the log directory: an extracted APK is app data. These read
    // g_logDir back when the two were the same variable, which after the split would
    // have buried a whole extracted APK tree inside logs/.
    const char* docsRoot = g_docsDir[0] != '\0' ? g_docsDir : g_logDir;
    std::string targetDir = std::string(docsRoot) + "/extracted_apk";

    log += "[kudroid_apk] Initializing VFS...\n";
    kudroid::VFSPathRemapper::getInstance().setDocumentsDirectory(docsRoot);
    
    log += "[kudroid_apk] Extracting APK to: " + targetDir + "\n";
    bool extractedOk = false;
    if (kudroid::APKExtractor::is_bundle_container(apkStr)) {
        log += "[kudroid_apk] Split-APK bundle detected (.xapk/.apks/.apkm), merging splits...\n";
        extractedOk = kudroid::APKExtractor::extract_bundle(apkStr, targetDir);
    } else {
        extractedOk = kudroid::APKExtractor::extract_apk(apkStr, targetDir);
    }
    if (!extractedOk) {
        log += "[kudroid_apk] ERROR: Extraction failed - " + kudroid::APKExtractor::lastError() + "\n";
        return strdup(log.c_str());
    }
    log += "[kudroid_apk] Extracted successfully.\n";

    // Notify AAssetManager shim about extracted APK assets directory.
    kudroid_set_assets_dir((targetDir + "/assets").c_str());
    
    log += "[kudroid_apk] Scanning for native libraries (.so)...\n";
    // Process-lifetime (see globalLibraryManager) — JNI_OnLoad spawns guest render threads;
    // library mappings must persist after this function returns.
    kudroid::LibraryManager& libManager = globalLibraryManager();
    std::string libDir = targetDir + "/lib/" KUDROID_DEVICE_ABI;

    // ── KuART ──────────────────────────────────────────────────────────────
    // Load embedded framework.dex + APK classes*.dex.
    // No DEX-to-JAR conversion: KuART directly executes DEX bytecode.
    kuart_set_symbol_lookup([](const char* symbol) -> void* {
        return globalLibraryManager().resolveGlobalSymbol(symbol);
    });
    const bool kuartOk = kuart_init(targetDir.c_str()) != 0;
    if (kuartOk) {
        log += "[kudroid_apk] KuART ready: " + std::to_string(kuart_num_dex_files()) +
               " DEX loaded\n";
    } else {
        log += "[kudroid_apk] WARNING: KuART init failed (" + std::string(kuart_last_error()) + ")\n";
    }

    if (std::filesystem::exists(libDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(libDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".so") {
                log += "[kudroid_apk] Loading library: " + entry.path().filename().string() + "\n";
                if (!libManager.loadRecursive(entry.path().string())) {
                    log += "[kudroid_apk] WARNING: Failed to load " + entry.path().filename().string() + "\n";
                } else {
                    log += "[kudroid_apk] Loaded successfully.\n";
                }
            }
        }
    } else {
        log += "[kudroid_apk] No native libraries found in " + libDir + "\n";
    }
    
    log += "[kudroid_apk] APK Load Complete.\n";
    return strdup(log.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// KuART DEX loading verification
//
// before y function ny dch DEX → JAR cho Avian. KuART np DEX trc tip nn gi n
// ch parse v bo co ni dung — gi tn c v Swift no/not phi i.
// Returns malloc'ed log string; caller must free().
// ─────────────────────────────────────────────────────────────────────────────
extern "C" const char* kudroid_translate_dex(const char* dexPath) {
    if (!dexPath || !*dexPath) {
        return strdup("[kudroid_dex] ERROR: null DEX path\n");
    }

    std::string log;
    log += "[kudroid_dex] Loading DEX with KuART: " + std::string(dexPath) + "\n";

    // Load DEX directory — KuART indexes all classes*.dex within the directory.
    const std::string dir = std::filesystem::path(dexPath).parent_path().string();
    if (!kuart_init(dir.c_str())) {
        log += "[kudroid_dex] LOAD FAILED: " + std::string(kuart_last_error()) + "\n";
        writeLogFile("kudroid_dex_translate.txt", log);
        return strdup(log.c_str());
    }

    log += "[kudroid_dex] DEX files loaded: " + std::to_string(kuart_num_dex_files()) + "\n";

    constexpr size_t kMaxList = 20000;
    std::vector<char*> classes(kMaxList, nullptr);
    const size_t n = kuart_list_app_classes(classes.data(), kMaxList);
    log += "[kudroid_dex] App classes found: " + std::to_string(n) + "\n";
    for (size_t i = 0; i < n && i < 20; ++i) {
        log += "[kudroid_dex]   " + std::string(classes[i]) + "\n";
    }
    if (n > 20) log += "[kudroid_dex]   ... (" + std::to_string(n - 20) + " more)\n";
    kuart_free_class_list(classes.data(), n);

    log += "[kudroid_dex] Classes resolved: " + std::to_string(kuart_num_loaded_classes()) + "\n";
    writeLogFile("kudroid_dex_translate.txt", log);
    return strdup(log.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Gentle Crash API
// ─────────────────────────────────────────────────────────────────────────────
extern "C" int kudroid_has_crashed(void) {
    return g_hasCrashed.load() ? 1 : 0;
}

extern "C" void kudroid_clear_crash_state(void) {
    g_hasCrashed.store(false);
    g_lastCrashTail[0] = '\0';
}

extern "C" const char* kudroid_get_last_crash_tail(void) {
    if (g_lastCrashTail[0] == '\0') {
        return strdup("No recent crash detected.");
    }
    return strdup(g_lastCrashTail);
}

// ─────────────────────────────────────────────────────────────────────────────
// Permission Dialog Prompt Bridge
// ─────────────────────────────────────────────────────────────────────────────
static kudroid_permission_prompt_cb g_permission_prompt_cb = nullptr;

extern "C" void kudroid_set_permission_prompt_callback(kudroid_permission_prompt_cb cb) {
    g_permission_prompt_cb = cb;
}

extern "C" void kudroid_prompt_permission_request(const char* packageName, const char* permissionsCsv, int requestCode, void* activityHandle) {
    if (g_permission_prompt_cb != nullptr) {
        g_permission_prompt_cb(packageName, permissionsCsv, requestCode, activityHandle);
    } else {
        kudroid_submit_permission_response(activityHandle, requestCode, permissionsCsv, 1);
    }
}

extern "C" void kudroid_submit_permission_response(void* activityHandle, int requestCode, const char* permissionsCsv, int granted) {
    if (activityHandle == nullptr || permissionsCsv == nullptr) return;
    std::string csv = permissionsCsv;
    std::vector<std::string> perms;
    std::string token;
    std::istringstream tokenStream(csv);
    while (std::getline(tokenStream, token, ',')) {
        if (!token.empty()) perms.push_back(token);
    }
    if (perms.empty()) return;

    // Log permission grant result
    std::fprintf(stderr, "[PermissionManager] Permission response for request %d (%zu perms): %s\n",
                 requestCode, perms.size(), granted ? "GRANTED" : "DENIED");
}
