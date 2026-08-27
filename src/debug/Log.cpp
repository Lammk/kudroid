#include "kudroid/Log.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>

// Standard pipeline (stdout + kudroid_android_logs.txt + crash buffer) — defined
// meaning in SyscallShim.cpp, which is the "C version" for kudroid_jni/GraphicsShim.
extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);

namespace kudroid {
namespace log {

namespace {
std::mutex g_mtx;
bool g_verbose = true;        // debugging real machine — enabled by default
bool g_verbose_initialized = false;

bool env_verbose() {
    const char* v = std::getenv("KUDROID_LOG_VERBOSE");
    if (!v || !*v) return true;
    return v[0] != '0' && v[0] != 'n' && v[0] != 'N';
}
} // namespace

void set_verbose(bool enabled) {
    std::lock_guard<std::mutex> lock(g_mtx);
    g_verbose = enabled;
    g_verbose_initialized = true;
}

bool verbose_enabled() {
    std::lock_guard<std::mutex> lock(g_mtx);
    if (!g_verbose_initialized) {
        g_verbose = env_verbose();
        g_verbose_initialized = true;
    }
    return g_verbose;
}

void write(Level level, const char* tag, const char* fmt, ...) {
    if (!fmt || !*fmt) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    kudroid_android_log_message(static_cast<int>(level), tag ? tag : "", buf);
}

} // namespace log
} // namespace kudroid
