// BridgeState.cpp — shared state for the kudroid_bridge unit family.
//
// Split out of the original kudroid_bridge.cpp: this holds the directories
// (Documents / logs), the persistent breadcrumb journal, the gentle-crash
// flags and buffers, the thread-registry bookkeeping, and the shared log
// utilities every bridge unit writes its diagnostics through.

#include "BridgeState.h"
#include "BridgeShared.h"
#include "CrashHandling.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <string>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/task.h>
#else
#include <sys/syscall.h>
#endif

extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);

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
char g_docsDir[1024] = {0};
char g_logDir[1024] = {0};
const char* g_kudroid_log_dir_ptr = g_logDir;

extern "C" void kudroid_persistent_breadcrumb(const char* line) {
    if (!line || !g_logDir[0]) return;
    static std::atomic<int> s_fd{-2};
    int fd = s_fd.load(std::memory_order_relaxed);
    if (fd == -2) {
        char path[sizeof(g_logDir) + 32];
        const int n = snprintf(path, sizeof(path), "%s/native_breadcrumbs.log", g_logDir);
        if (n > 0 && static_cast<std::size_t>(n) < sizeof(path)) {
            int opened = ::open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
            int expected = -2;
            if (s_fd.compare_exchange_strong(expected, opened, std::memory_order_acq_rel)) {
                fd = opened;
            } else {
                if (opened >= 0) ::close(opened);
                fd = s_fd.load(std::memory_order_acquire);
            }
        } else {
            s_fd.store(-1, std::memory_order_relaxed);
            fd = -1;
        }
    }
    // No reporting here: this function is reached from the fatal signal
    // handler, and anything it called that takes a lock or touches stdio could
    // deadlock inside the very report meant to explain the fault. Whether the
    // journal is writable is checked once in kudroid_set_log_dir instead, which
    // runs in normal context.
    struct timespec now;
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    char record[2304];
    const int record_len = snprintf(record, sizeof(record), "t_ns=%lld %s\n",
                                    static_cast<long long>(now.tv_sec) * 1000000000LL +
                                        now.tv_nsec, line);
    if (record_len <= 0) return;
    const size_t len = static_cast<size_t>(record_len) < sizeof(record)
                           ? static_cast<size_t>(record_len) : sizeof(record) - 1;
    if (fd >= 0) {
        (void)::write(fd, record, len);
        return;
    }
    char path[sizeof(g_logDir) + 32];
    const int n = snprintf(path, sizeof(path), "%s/native_breadcrumbs.log", g_logDir);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(path)) return;
    const int fallback_fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fallback_fd < 0) return;
    (void)::write(fallback_fd, record, len);
    (void)::close(fallback_fd);
}
// Previously 16KB was too small: ELF loading and verbose lines filled the
// buffer, pushing crucial pre-crash context (e.g. EGL initialization) out.
// Expanded to 256KB; static allocation ensures signal-handler safety without heap usage.
char g_crashBuf[262144];
volatile sig_atomic_t g_crashLen = 0;
std::mutex g_crashBufMtx;
char g_abortMessage[1024] = {0};
// si_code of the signal being reported, stored by the handler for tests and for
// readers that want signalled-vs-faulted without parsing the whole line.
volatile int g_crash_signal_si_code = 0;

// Gentle crash flags live in BridgeShared.cpp (g_hasCrashed / g_lastCrashTail);
// the crash handler writes them and AppLifecycle reads them for the shell.


// ── Thread registry ──────────────────────────────────────────────────────────
// Numeric tids, plain atomic loads in the signal handler, stores only from
// normal context. Writers: thread sampler, GraphicsShim, guest UI dispatch.
// Readers: crash handler, watchdog, stall reports.
static std::atomic<unsigned long long> g_guestUiThread{0};
static std::atomic<unsigned long long> g_renderThreads[8] = {};

struct ThreadNameSlot {
    std::atomic<unsigned long long> tid{0};
    // Seqlock: odd while the writer (under mutex, normal context) updates the
    // name; the handler snapshots without locking and retries/discards on a
    // torn read instead of classifying on half-written bytes.
    std::atomic<unsigned> gen{0};
    char name[32] = {0};
};

// The OS thread id, the same number the watchdog, the stall reports and the
// thread sampler print. Async-signal-safe on both platforms, which is why the
// crash handler can call it.
unsigned long long currentThreadIdForCrash(void) {
#if defined(__APPLE__)
    uint64_t tid = 0;
    pthread_threadid_np(nullptr, &tid);
    return static_cast<unsigned long long>(tid);
#else
    return static_cast<unsigned long long>(::syscall(SYS_gettid));
#endif
}

static ThreadNameSlot g_threadNames[32];
static std::mutex g_threadNameMutex;  // normal context only, never the handler


extern "C" void kudroid_note_thread_name(const char* name) {
    if (name == nullptr || *name == '\0') return;
    const unsigned long long tid = currentThreadIdForCrash();
    std::lock_guard<std::mutex> lock(g_threadNameMutex);
    for (auto& slot : g_threadNames) {
        if (slot.tid.load(std::memory_order_relaxed) == tid) {
            slot.gen.fetch_add(1, std::memory_order_relaxed);
            std::snprintf(slot.name, sizeof(slot.name), "%s", name);
            slot.gen.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    for (auto& slot : g_threadNames) {
        unsigned long long empty = 0;
        if (slot.tid.compare_exchange_strong(empty, tid,
                                             std::memory_order_relaxed)) {
            slot.gen.fetch_add(1, std::memory_order_relaxed);
            std::snprintf(slot.name, sizeof(slot.name), "%s", name);
            slot.gen.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
}

// Signal-handler side: exact/prefix match on the recorded name. Reads only,
// seqlock-guarded against torn writes (a write in flight reads as unknown,
// i.e. worker, never as a half-written critical). Deliberately NOT a "Main"
// substring: that also matched DomainMain/Remain-style names. Observed Unity
// critical names: UnityMain, RenderThread, UnityGfxDeviceW.
// The name is copied to a stack buffer with NUL padding first, so the
// lookahead below can never overread even a full 31-char name.
static bool thread_name_marks_critical(unsigned long long tid) {
    if (tid == 0) return false;
    for (auto& slot : g_threadNames) {
        if (slot.tid.load(std::memory_order_relaxed) != tid) continue;
        const unsigned g0 = slot.gen.load(std::memory_order_acquire);
        char n[40] = {0};
        for (size_t i = 0; i < 31; ++i) n[i] = slot.name[i];
        const unsigned g1 = slot.gen.load(std::memory_order_acquire);
        if ((g0 & 1) != 0 || g0 != g1) return false;  // torn: unknown, not critical
        // Exact engine-main names.
        if (std::strcmp(n, "UnityMain") == 0 || std::strcmp(n, "MainThread") == 0) return true;
        // Render-thread family by prefix (RenderThread and vendor variants).
        if (std::strncmp(n, "Render", 6) == 0) return true;
        // Gfx device worker by distinctive infix (UnityGfxDeviceW, ...).
        if (std::strstr(n, "GfxDevice") != nullptr) return true;
        return false;
    }
    return false;
}

extern "C" void kudroid_note_guest_ui_thread(void) {
    g_guestUiThread.store(currentThreadIdForCrash(), std::memory_order_relaxed);
}

extern "C" unsigned long long kudroid_guest_ui_thread_id(void) {
    return g_guestUiThread.load(std::memory_order_relaxed);
}

extern "C" void kudroid_fault_state_reset_for_app(void) {
    g_guestUiThread.store(0, std::memory_order_relaxed);
    for (auto& slot : g_renderThreads) slot.store(0, std::memory_order_relaxed);
    for (auto& slot : g_threadNames) {
        slot.tid.store(0, std::memory_order_relaxed);
        slot.gen.store(0, std::memory_order_relaxed);
    }
    crashResetWorkerBudgets();
}

extern "C" void kudroid_note_render_thread(void) {
    const unsigned long long tid = currentThreadIdForCrash();
    for (auto& slot : g_renderThreads) {
        if (slot.load(std::memory_order_relaxed) == tid) return;
    }
    for (auto& slot : g_renderThreads) {
        unsigned long long empty = 0;
        if (slot.compare_exchange_strong(empty, tid, std::memory_order_relaxed)) return;
    }
}

// Watchdog hook: completion time of the last successful eglSwapBuffers. The
// guest cannot lie about this one — no swaps for seconds means the frame loop
// is dead no matter what any other timestamp says.
static std::atomic<uint64_t> g_lastSwapNs{0};

extern "C" void kudroid_note_frame_presented(void) {
    g_lastSwapNs.store(
        static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count()),
        std::memory_order_relaxed);
}

extern "C" uint64_t kudroid_last_frame_presented_ns(void) {
    return g_lastSwapNs.load(std::memory_order_relaxed);
}

extern "C" void kudroid_frame_presented_reset(void) {
    g_lastSwapNs.store(0, std::memory_order_relaxed);
}

// True when a fault on `tid` must stop the app: host main, guest UI, any

void extractLastLines(const char* src, size_t srcLen, char* dst, size_t dstCap, int maxLines) {
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

extern "C" const char* kudroid_crash_log_snapshot(void) {
    std::lock_guard<std::mutex> lock(g_crashBufMtx);
    const size_t n = static_cast<size_t>(g_crashLen);
    char* out = static_cast<char*>(std::malloc(n + 1));
    if (!out) return nullptr;
    std::memcpy(out, g_crashBuf, n);
    out[n] = '\0';
    return out;
}

void mirrorCrash(const std::string& log) {
    // Synchronized with kudroid_append_crash_log mutex to prevent data races
    // with active guest logging threads.
    std::lock_guard<std::mutex> lock(g_crashBufMtx);
    size_t n = log.size();
    if (n >= sizeof(g_crashBuf)) n = sizeof(g_crashBuf) - 1;
    memcpy(g_crashBuf, log.data(), n);
    g_crashBuf[n] = '\0';
    g_crashLen = (sig_atomic_t)n;
}

void writeLogFile(const char* name, const std::string& content) {
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
void logCoreLine(int priority, const std::string& line) {
    static constexpr char kPrefix[] = "[kudroid_core] ";
    static constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
    const char* text = line.c_str();
    if (line.compare(0, kPrefixLen, kPrefix) == 0) text += kPrefixLen;
    kudroid_android_log_message(priority, "kudroid_core", text);
}

// Append crash buffer snapshot to test execution logs.
void appendCrashSnapshot(std::string& log, const char* sectionName) {
    const char* snap = kudroid_crash_log_snapshot();
    if (!snap) return;
    if (*snap) {
        log += std::string("\n--- ") + sectionName + " ---\n";
        log += snap;
    }
    std::free(const_cast<char*>(snap));
}

void appendTestHeader(std::string& log, const char* test, const char* path) {
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
        "kudroid_core vT1.0.0 " __DATE__ " " __TIME__ " "
#ifdef KUDROID_GIT_HASH
        KUDROID_GIT_HASH
#else
        "(no-git-hash)"
#endif
        ;
    return kStamp;
}

// providing exact crash location context instead of raw unmapped addresses.

void appendCrashLogFile(const std::string& content) {
    if (!g_logDir[0]) return;
    const std::string path = std::string(g_logDir) + "/kudroid_crash.log";
    FILE* fp = fopen(path.c_str(), "a");
    if (fp == nullptr) return;
    fwrite(content.data(), 1, content.size(), fp);
    fclose(fp);
}

// ── Library residency ────────────────────────────────────────────────────────
// Guest .so mappings live for the process lifetime (unmapping under a detached
// thread aborts), so a run started after an install would reuse stale code.
// The state itself lives in BridgeShared.cpp (s_libsResident/s_libsGeneration):
// the run path reads it directly.
void bridgeMarkLibsResident(uint64_t generation) {
    extern std::atomic<bool> s_libsResident;      // BridgeShared.h
    extern std::atomic<uint64_t> s_libsGeneration;  // BridgeShared.h
    s_libsResident.store(true);
    s_libsGeneration.store(generation);
}

bool bridgeLibsResident(uint64_t* generationOut) {
    extern std::atomic<bool> s_libsResident;        // BridgeShared.h
    extern std::atomic<uint64_t> s_libsGeneration;  // BridgeShared.h
    if (!s_libsResident.load()) return false;
    if (generationOut) *generationOut = s_libsGeneration.load();
    return true;
}

// ── Fault fatality (from CrashHandling) ─────────────────────────────────
bool kudroid_fault_is_fatal(unsigned long long tid, bool isHostMain) {
    if (isHostMain) return true;
    if (tid != 0 && tid == g_guestUiThread.load(std::memory_order_relaxed)) return true;
    for (auto& slot : g_renderThreads) {
        if (tid != 0 && tid == slot.load(std::memory_order_relaxed)) return true;
    }
    if (thread_name_marks_critical(tid)) return true;
    return false;
}
