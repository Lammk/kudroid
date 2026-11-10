namespace kudroid {
namespace {

constexpr uintptr_t kStackGuardCookie = 0x1337BEEFCAFECAFEULL;
uintptr_t gStackCheckGuard = kStackGuardCookie;

// Shared trace buffer across shim modules.
void trace(const char* message) { trace_shim(message); }

extern "C" int bionic_dummy() {
    trace("dummy fallback invoked -> 0");
    return 0;
}

extern "C" void* bionic_malloc(size_t size) {
    return std::malloc(size);
}

extern "C" void bionic_free(void* ptr) {
    std::free(ptr);
}

void appendUnsigned(std::string& output, uint64_t value, unsigned base) {
    char buffer[32];
    const char* digits = "0123456789abcdef";
    char* cursor = buffer + sizeof(buffer);
    do {
        *--cursor = digits[value % base];
        value /= base;
    } while (value != 0);
    output.append(cursor, buffer + sizeof(buffer));
}

static std::mutex g_logAndroidMutex;
extern "C" void kudroid_persistent_breadcrumb(const char* line);

// Console sink: never block on a full pipe whose reader has stalled. stdout is the launcher's pipe or pty when the app is started with a console attached.
// If nothing drains it, a write blocks forever, and the thread blocked here holds the log
// mutex: a GL thread that logs a call then stalls the whole frame loop. The descriptor is
// kept non-blocking (checked per call, since it can be replaced), so a full console drops
// the line instead.
static int g_hostConsoleFd = -1;
static std::once_flag g_hostConsoleOnce;

static void set_console_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags >= 0 && (flags & O_NONBLOCK) == 0) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

static void init_host_console() {
    // Keep host diagnostics outside any guest stdout capture pipe.
    std::call_once(g_hostConsoleOnce, [] {
        g_hostConsoleFd = ::fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 3);
    });
}

extern "C" void kudroid_console_init_nonblocking(void) {
    init_host_console();
    set_console_nonblocking(STDOUT_FILENO);
}

static void write_console_fd(int fd, const char* data, size_t len) {
    if (fd < 0) return;
    constexpr size_t kChunk = 480;
    set_console_nonblocking(fd);
    while (len > 0) {
        const size_t n = len < kChunk ? len : kChunk;
        const ssize_t w = ::write(fd, data, n);
        if (w <= 0) return;  // EAGAIN (console full), EPIPE or error: drop the rest
        data += w;
        len -= static_cast<size_t>(w);
    }
}

static void console_write_nonblocking(const char* data, size_t len) {
    kudroid_console_init_nonblocking();
    write_console_fd(STDOUT_FILENO, data, len);
}

// Bounded wait for the log lock. A sink that wedges while holding it must not
// take the watchdog and every guest thread down with it: after the bound the
// line is dropped, and while the lock looks wedged later callers only try once.
static std::atomic<uint64_t> g_logWedgedUntilNs{0};
// Who holds the log mutex and what it is writing, for the wedge report. Written
// only by the holder; read racily by a waiter that gave up, which is enough for a
// diagnostic.
static char g_logHolderText[160];
static std::atomic<uint64_t> g_logHolderTid{0};
static uint64_t log_now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
static bool lock_log_bounded(std::mutex& mtx) {
    if (mtx.try_lock()) return true;
    const uint64_t start = log_now_ns();
    if (start < g_logWedgedUntilNs.load(std::memory_order_relaxed)) return false;
    constexpr uint64_t kBoundNs = 200ull * 1000000ull;
    while (log_now_ns() - start < kBoundNs) {
        ::usleep(200);
        if (mtx.try_lock()) return true;
    }
    g_logWedgedUntilNs.store(log_now_ns() + 1000ull * 1000000ull, std::memory_order_relaxed);
    char report[256];
    std::snprintf(report, sizeof(report), "log-lock-wedged: holder tid=%llu line=%.150s",
                  static_cast<unsigned long long>(g_logHolderTid.load(std::memory_order_relaxed)),
                  g_logHolderText);
    kudroid_persistent_breadcrumb(report);
    return false;
}

#if defined(__APPLE__)
extern "C" __attribute__((weak)) void kudroid_remote_log_broadcast(int level, const char* tag, const char* message) {
    (void)level; (void)tag; (void)message;
}
#endif

int logAndroidMessage(int priority, const char* tag, const std::string& message);
static void emit_android_log_line(int priority, const char* tag, const std::string& message);

// Repeat-collapse: bursts of byte-identical lines (per-draw GPU chatter,
// per-clip asset errors) each pay open/write/close on the log file and say
// nothing new. The first occurrence emits, consecutive repeats fold into a
// count, and the next different line flushes one summary. Keyed on
// tag+message; bounded table, linear scan — the distinct-line working set is
// a few dozen tags.
struct LogRepeatSlot {
    uint64_t hash = 0;
    int priority = 0;
    int count = 0;   // 0 = idle, 1 = emitted once, N = N-1 repeats suppressed
    std::string key; // tag + '\n' + message
};
constexpr size_t kMaxLogRepeatSlots = 48;
static std::vector<LogRepeatSlot> g_logRepeatSlots;

static uint64_t log_line_hash(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

// Logging is diagnostics only and must never wait on a thread that is parked.
// Drop the line when (a) this thread is already inside the logger (a fault handler
// that logs while the logger runs would self-deadlock on the non-recursive mutex) or
// (b) the world may be stopped: a thread parked by the guest runtime's suspend
// handler keeps the log mutex it held, and the collector, running meanwhile, would
// wait on it forever.
static thread_local bool t_inLogMessage = false;

int logAndroidMessage(int priority, const char* tag, const std::string& message) {
    if (message.find("BoxCollider does not support negative scale") != std::string::npos ||
        message.find("The effective box size has been forced positive") != std::string::npos) {
        return 0;
    }
    if (t_inLogMessage || kudroid::guest_async_handler_active()) return 0;
    if (!lock_log_bounded(g_logAndroidMutex)) return 0;
    std::lock_guard<std::mutex> lock(g_logAndroidMutex, std::adopt_lock);
    struct ReentryFlag {
        ReentryFlag() { t_inLogMessage = true; }
        ~ReentryFlag() { t_inLogMessage = false; }
    } reentryFlag;
    {
        uint64_t tid = 0;
#if defined(__APPLE__)
        ::pthread_threadid_np(nullptr, &tid);
#endif
        g_logHolderTid.store(tid, std::memory_order_relaxed);
        std::snprintf(g_logHolderText, sizeof(g_logHolderText), "[%s] %s", tag ? tag : "",
                      message.c_str());
    }

    // kudroid_clear_all_logs() removes and recreates kudroid_android_logs.txt
    // (a fresh inode) before every guest run, and stderr.log is dup2'd over.
    // The cached file fd below kept pointing at the deleted inode — its O_APPEND
    // writes landed in an unlinked file and kudroid_android_logs.txt stayed
    // frozen at the one process-start line clear_all_logs wrote itself. The
    // reopen is forced by kudroid_android_log_force_reopen, called from
    // kudroid_clear_all_logs after it recreates the files.
    const char* tg = tag ? tag : "";
    std::string key;
    key.reserve(std::strlen(tg) + message.size() + 1);
    key += tg;
    key += '\n';
    key += message;
    const uint64_t h = log_line_hash(key);

    LogRepeatSlot* slot = nullptr;
    for (auto& s : g_logRepeatSlots) {
        if (s.hash == h && s.key == key) { slot = &s; break; }
    }
    if (slot != nullptr && slot->count > 0) {
        ++slot->count;
        return 0; // identical consecutive repeat: folded into the summary
    }
    // New content (or an old key reappearing): close every open burst with
    // one summary line before emitting.
    for (auto& s : g_logRepeatSlots) {
        if (s.count > 1) {
            const size_t sep = s.key.find('\n');
            const std::string summary =
                s.key.substr(sep + 1) + "   [x" + std::to_string(s.count) +
                " repeats collapsed]";
            emit_android_log_line(s.priority, s.key.substr(0, sep).c_str(), summary);
        }
        s.count = 0;
    }
    if (slot == nullptr) {
        for (auto& s : g_logRepeatSlots) {
            if (s.count == 0) { slot = &s; break; }
        }
        if (slot == nullptr) {
            if (g_logRepeatSlots.size() < kMaxLogRepeatSlots) {
                g_logRepeatSlots.emplace_back();
                slot = &g_logRepeatSlots.back();
            } else {
                slot = &g_logRepeatSlots.front(); // evict oldest
            }
        }
        slot->key = key;
        slot->hash = h;
    }
    slot->priority = priority;
    slot->count = 1;
    emit_android_log_line(priority, tag, message);
    return 0;
}

// The kudroid_android_logs.txt sink's cached descriptor lives at file scope so
// kudroid_android_log_force_reopen can drop it: kudroid_clear_all_logs()
// removes and recreates the file (fresh inode) before every guest run, and an
// O_APPEND write to the cached fd keeps landing in the unlinked old inode.
static int g_androidLogFd = -1;
static char g_androidLogFdDir[1024] = {0};
static void android_log_close_cached_fd() {
    if (g_androidLogFd >= 0) {
        ::close(g_androidLogFd);
        g_androidLogFd = -1;
    }
    g_androidLogFdDir[0] = '\0';
}

extern "C" void kudroid_android_log_force_reopen(void) {
    init_host_console();
    std::lock_guard<std::mutex> lock(g_logAndroidMutex);
    android_log_close_cached_fd();
    g_logRepeatSlots.clear();
}

// Emit one formatted line to every sink. Caller holds g_logAndroidMutex.
static void emit_android_log_line(int priority, const char* tag, const std::string& message) {
    char traceMessage[256];
    snprintf(traceMessage, sizeof(traceMessage),
                  "__android_log_print(priority=%d, tag=%s)", priority,
                  tag ? tag : "<null>");
    trace(traceMessage);
    {
        std::string full = "android log message: ";
        full += message;
        trace_shim(full.c_str());
    }

    char time_buf[32] = {0};
    struct timespec ts;
    if (::clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        struct tm tm_buf;
        localtime_r(&ts.tv_sec, &tm_buf);
        int ms = static_cast<int>(ts.tv_nsec / 1000000);
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d.%03d",
                 tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, ms);
    }
    
    // Dump to standard output (Xcode/syslog)
    {
        char console_line[512];
        int cn = time_buf[0]
            ? snprintf(console_line, sizeof(console_line), "[%s] [AndroidLog][%s]: %s\n",
                       time_buf, tag ? tag : "unknown", message.c_str())
            : snprintf(console_line, sizeof(console_line), "[AndroidLog][%s]: %s\n",
                       tag ? tag : "unknown", message.c_str());
        if (cn > 0) {
            if (static_cast<size_t>(cn) > sizeof(console_line) - 1) {
                cn = sizeof(console_line) - 1;
                console_line[cn - 1] = '\n';
            }
            init_host_console();
            write_console_fd(g_hostConsoleFd, console_line, static_cast<size_t>(cn));
        }
    }
    
    // Broadcast via KDB WebSocket if connected
#if defined(__APPLE__)
    if (kudroid_remote_log_broadcast) {
        kudroid_remote_log_broadcast(priority, tag ? tag : "KuDroid", message.c_str());
    }
#endif

    // Also append to a file in Documents for the user to easily read.
    //
    // The descriptor is cached across calls. This runs for every guest log line
    // (Unity's per-clip audio errors, GPU chatter, per-syscall traces) while
    // holding g_logAndroidMutex, so an open/close pair per line serialized the
    // log file behind every thread in the process and turned a burst of errors
    // into a stall. Reopen only when the log directory changes or a write fails.
    if (::g_kudroid_log_dir_ptr && ::g_kudroid_log_dir_ptr[0] != '\0') {
        if (g_androidLogFd < 0 ||
            std::strcmp(g_androidLogFdDir, ::g_kudroid_log_dir_ptr) != 0) {
            if (g_androidLogFd >= 0) {
                ::close(g_androidLogFd);
                g_androidLogFd = -1;
            }
            char log_path[1024];
            snprintf(log_path, sizeof(log_path), "%s/kudroid_android_logs.txt",
                     ::g_kudroid_log_dir_ptr);
            g_androidLogFd = ::open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
            snprintf(g_androidLogFdDir, sizeof(g_androidLogFdDir), "%s",
                     ::g_kudroid_log_dir_ptr);
        }
        if (g_androidLogFd >= 0) {
            char line_buf[4096];
            int n = 0;
            if (time_buf[0]) {
                n = snprintf(line_buf, sizeof(line_buf), "[%s] [%s] %s\n", time_buf, tag ? tag : "unknown", message.c_str());
            } else {
                n = snprintf(line_buf, sizeof(line_buf), "[%s] %s\n", tag ? tag : "unknown", message.c_str());
            }
            if (n > 0) {
                if (static_cast<size_t>(n) > sizeof(line_buf) - 1) n = sizeof(line_buf) - 1;
                if (::write(g_androidLogFd, line_buf, static_cast<size_t>(n)) < 0) {
                    // A removed directory or full disk must not wedge logging:
                    // drop the cached fd and let the next line try to reopen.
                    android_log_close_cached_fd();
                }
            }
        }
    }

    // Mirror into the crash buffer so kudroid_crash.log shows the game's own
    // Provide diagnostic log buffer up to crash event.
    {
        std::string full;
        if (time_buf[0]) { full += '['; full += time_buf; full += "] "; }
        if (tag) { full += '['; full += tag; full += "] "; }
        full += message;
        full += '\n';
        kudroid_append_crash_log(full.data(), full.size());
    }
}

#if defined(__aarch64__)
extern "C" int kudroid_android_log_print_trampoline();
// snprintf/sprintf handlers live in GuestVarargs.cpp, next to the formatter, so the
// freestanding arm64 test can link them without dragging in this file.
extern "C" int kudroid_snprintf_trampoline();
extern "C" int kudroid_sprintf_trampoline();
#if defined(__APPLE__) && defined(__aarch64__)
extern "C" int kudroid_swprintf_trampoline();
extern "C" int bionic_vswprintf(wchar_t* out, size_t size, const wchar_t* format, va_list ap) {
    GuestVarargs registers{};
    unsigned gp = 0, fp = 0;
    if (!GuestVarargsFromVaList(ap, &registers, &gp, &fp)) {
        errno = EINVAL;
        return -1;
    }
    return FormatGuestWideVarargs(out, size, format, &registers, gp, fp);
}
#endif
extern "C" int kudroid_snprintf_chk_trampoline();
extern "C" int kudroid_sprintf_chk_trampoline();
extern "C" int kudroid_fprintf_trampoline();
extern "C" int kudroid_printf_trampoline();
extern "C" int kudroid_fprintf_chk_trampoline();
extern "C" int kudroid_printf_chk_trampoline();
extern "C" void kudroid_syslog_trampoline();
extern "C" int kudroid_asprintf_trampoline();
extern "C" int kudroid_asprintf_chk_trampoline();
extern "C" int kudroid_dprintf_trampoline();
extern "C" int kudroid_dprintf_chk_trampoline();
extern "C" int kudroid_log_buf_print_trampoline();
extern "C" void kudroid_syslog_chk_trampoline();

// Stream sink for the fprintf/printf handlers in GuestVarargs.cpp. A null stream is stdout.
extern "C" void kudroid_guest_stdio_write(void* stream, const char* data, size_t len) {
    FILE* out = stream != nullptr ? static_cast<FILE*>(stream) : stdout;
    if (!len) return;
    if (out == stdout) { console_write_nonblocking(data, len); return; }
    std::fwrite(data, 1, len, out);
}

extern "C" int kudroid_android_log_print_from_registers(const uint64_t* frame) {
    const auto* registers = reinterpret_cast<const GuestVarargs*>(frame);
    const int priority = static_cast<int>(registers->gp[0]);
    const char* tag = reinterpret_cast<const char*>(registers->gp[1]);
    const char* format = reinterpret_cast<const char*>(registers->gp[2]);
    // Varargs begin at the fourth integer register: priority, tag and format took three.
    char message[1024];
    FormatGuestVarargs(message, sizeof(message), format, registers, /*firstGpIndex=*/3);
    return logAndroidMessage(priority, tag, message);
}
#else
extern "C" int bionic_android_log_print(int priority, const char* tag,
                                          const char* format, ...) {

    va_list args;
    va_start(args, format);
    char formattedMessage[512];
    std::vsnprintf(formattedMessage, sizeof(formattedMessage),
                   format ? format : "", args);
    va_end(args);
    return logAndroidMessage(priority, tag, formattedMessage);
}
#endif

// Do not trace high-frequency no-ops (__cxa_finalize, atexit) to avoid flooding.
// avoid tracing frequent unload events to keep logs concise.
extern "C" void bionic_cxa_finalize(void*) {
}

extern "C" int bionic_cxa_atexit(void (*)(void*), void*, void*) {
    return 0;
}

extern "C" int bionic___cxa_thread_atexit_impl(void (*)(void*), void*, void*) {
    return 0;
}

extern "C" void bionic_runtime_noop() {
}


