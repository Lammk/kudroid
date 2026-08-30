#include "kudroid/NativeCallTelemetry.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <functional>

#include "kudroid/Log.h"
#include "kudroid/platform/MemoryInfo.h"

extern "C" void kudroid_persistent_breadcrumb(const char* line);

namespace kudroid {
namespace {

struct ActiveCall {
    std::mutex mutex;
    char class_name[256] = {};
    char method[256] = {};
    char signature[512] = {};
    char stage[64] = "enter";
    int vm_lock_depth = 0;
    std::atomic<unsigned> active_count{0};
    std::atomic<bool> started{false};
    std::atomic<uint64_t> next_call_id{1};
    uint64_t native_call_id = 0;
    uint64_t native_thread_id = 0;
    std::atomic<unsigned> java_active_count{0};
    uint64_t java_thread_id = 0;
    size_t java_depth = 0;
    uint64_t java_start_ns = 0;
    char java_class[256] = {};
    char java_method[256] = {};
    char java_signature[512] = {};
    std::atomic<uint64_t> java_last_progress_ns{0};
};

ActiveCall g_call;
thread_local uint64_t t_call_id = 0;

uint64_t thread_id() {
    return static_cast<uint64_t>(std::hash<std::thread::id>{}(
        std::this_thread::get_id()));
}

uint64_t now_ns() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

void watchdog_main() {
    while (g_call.started.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const bool native_active = g_call.active_count.load(std::memory_order_acquire) != 0;
        const bool java_active = g_call.java_active_count.load(std::memory_order_acquire) != 0;

        char cls[sizeof(g_call.class_name)];
        char method[sizeof(g_call.method)];
        char sig[sizeof(g_call.signature)];
        char stage[sizeof(g_call.stage)];
        int depth;
        uint64_t native_id;
        uint64_t native_tid;
        uint64_t java_tid;
        uint64_t java_start;
        size_t java_depth;
        char java_cls[sizeof(g_call.java_class)];
        char java_method[sizeof(g_call.java_method)];
        char java_sig[sizeof(g_call.java_signature)];
        {
            std::lock_guard<std::mutex> lock(g_call.mutex);
            std::memcpy(cls, g_call.class_name, sizeof(cls));
            std::memcpy(method, g_call.method, sizeof(method));
            std::memcpy(sig, g_call.signature, sizeof(sig));
            std::memcpy(stage, g_call.stage, sizeof(stage));
            depth = g_call.vm_lock_depth;
            native_id = g_call.native_call_id;
            native_tid = g_call.native_thread_id;
            java_tid = g_call.java_thread_id;
            java_start = g_call.java_start_ns;
            java_depth = g_call.java_depth;
            std::memcpy(java_cls, g_call.java_class, sizeof(java_cls));
            std::memcpy(java_method, g_call.java_method, sizeof(java_method));
            std::memcpy(java_sig, g_call.java_signature, sizeof(java_sig));
        }
        const SystemMemory memory = query_system_memory();
        char line[2048];
        const uint64_t java_elapsed = java_start != 0 ? now_ns() - java_start : 0;
        // Short Java calls stay RAM-only; durable watchdog records begin once
        // execution has exceeded the diagnostic threshold.
        if (!native_active && (!java_active || java_elapsed < 250000000ull)) continue;
        std::snprintf(line, sizeof(line),
                      "watchdog native_active=%d native_call_id=%llu native_thread_id=%llu stage=%s class=%s method=%s sig=%s vm_depth=%d java_active=%d java_thread_id=%llu java_depth=%zu java_elapsed_ms=%llu java_class=%s java_method=%s java_sig=%s footprint=%llu process_headroom=%llu available=%llu low_memory=%d",
                      native_active ? 1 : 0,
                      static_cast<unsigned long long>(native_id),
                      static_cast<unsigned long long>(native_tid), stage, cls, method, sig, depth,
                      java_active ? 1 : 0, static_cast<unsigned long long>(java_tid), java_depth,
                      static_cast<unsigned long long>(java_elapsed / 1000000), java_cls,
                      java_method, java_sig,
                      static_cast<unsigned long long>(memory.process_resident_bytes),
                      static_cast<unsigned long long>(memory.process_available_bytes),
                      static_cast<unsigned long long>(memory.available_bytes),
                      memory.low_memory ? 1 : 0);
        kudroid_persistent_breadcrumb(line);
    }
}

void copy_text(char* dst, size_t capacity, const char* src) {
    if (capacity == 0) return;
    std::snprintf(dst, capacity, "%s", src != nullptr ? src : "?");
}

}  // namespace

void native_run_begin() {
    static std::atomic<uint64_t> next_run{1};
    const uint64_t run = next_run.fetch_add(1, std::memory_order_relaxed);
    char line[160];
    std::snprintf(line, sizeof(line), "run-start run_id=%llu thread_id=%llu",
                  static_cast<unsigned long long>(run),
                  static_cast<unsigned long long>(thread_id()));
    kudroid_persistent_breadcrumb(line);
}

void native_phase(const char* phase) {
    char line[256];
    std::snprintf(line, sizeof(line), "phase=%s thread_id=%llu",
                  phase != nullptr ? phase : "?",
                  static_cast<unsigned long long>(thread_id()));
    kudroid_persistent_breadcrumb(line);
}

void native_call_enter(const char* class_name, const char* method,
                       const char* signature, int vm_lock_depth) {
    {
        std::lock_guard<std::mutex> lock(g_call.mutex);
        copy_text(g_call.class_name, sizeof(g_call.class_name), class_name);
        copy_text(g_call.method, sizeof(g_call.method), method);
        copy_text(g_call.signature, sizeof(g_call.signature), signature);
        copy_text(g_call.stage, sizeof(g_call.stage), "enter");
        g_call.vm_lock_depth = vm_lock_depth;
        g_call.native_call_id = g_call.next_call_id.load(std::memory_order_relaxed);
        g_call.native_thread_id = thread_id();
    }
    g_call.active_count.fetch_add(1, std::memory_order_acq_rel);
    t_call_id = g_call.next_call_id.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_call.mutex);
        g_call.native_call_id = t_call_id;
    }
    bool expected = false;
    if (g_call.started.compare_exchange_strong(expected, true,
                                               std::memory_order_acq_rel)) {
        std::thread(watchdog_main).detach();
    }
}

void native_call_stage(const char* stage) {
    if (g_call.active_count.load(std::memory_order_acquire) == 0) return;
    char line[2048];
    {
        std::lock_guard<std::mutex> lock(g_call.mutex);
        copy_text(g_call.stage, sizeof(g_call.stage), stage);
        std::snprintf(line, sizeof(line),
                      "native-stage call_id=%llu thread_id=%llu stage=%s class=%s method=%s sig=%s vm_depth=%d",
                      static_cast<unsigned long long>(t_call_id),
                      static_cast<unsigned long long>(thread_id()), stage,
                      g_call.class_name, g_call.method, g_call.signature,
                      g_call.vm_lock_depth);
    }
    kudroid_persistent_breadcrumb(line);
}

void native_call_exit() {
    unsigned count = g_call.active_count.load(std::memory_order_acquire);
    while (count != 0 && !g_call.active_count.compare_exchange_weak(
                              count, count - 1, std::memory_order_acq_rel,
                              std::memory_order_acquire)) {
    }
    t_call_id = 0;
}

bool java_call_should_trace(const char* method, size_t depth) {
    if (depth <= 2) return true;
    if (method == nullptr) return false;
    return std::strstr(method, "onCreate") != nullptr ||
           std::strstr(method, "onStart") != nullptr ||
           std::strstr(method, "onResume") != nullptr ||
           std::strcmp(method, "main") == 0 ||
           std::strstr(method, "launch") != nullptr;
}

void java_call_enter(const char* class_name, const char* method,
                     const char* signature, size_t depth) {
    {
        std::lock_guard<std::mutex> lock(g_call.mutex);
        copy_text(g_call.java_class, sizeof(g_call.java_class), class_name);
        copy_text(g_call.java_method, sizeof(g_call.java_method), method);
        copy_text(g_call.java_signature, sizeof(g_call.java_signature), signature);
        g_call.java_thread_id = thread_id();
        g_call.java_depth = depth;
    g_call.java_start_ns = now_ns();
        g_call.java_last_progress_ns.store(g_call.java_start_ns, std::memory_order_release);
    }
    g_call.java_active_count.fetch_add(1, std::memory_order_acq_rel);
    bool expected = false;
    if (g_call.started.compare_exchange_strong(expected, true,
                                                std::memory_order_acq_rel)) {
        std::thread(watchdog_main).detach();
    }
    char line[1200];
    std::snprintf(line, sizeof(line),
                  "java-enter thread_id=%llu depth=%zu class=%s method=%s sig=%s",
                  static_cast<unsigned long long>(thread_id()), depth,
                  class_name != nullptr ? class_name : "?",
                  method != nullptr ? method : "?",
                  signature != nullptr ? signature : "?");
    // Enter/exit are intentionally not persisted: doing so for every tiny
    // framework helper turns tracing itself into a launch-time workload.
    (void)line;
}

void java_call_exit() {
    if (g_call.java_active_count.load(std::memory_order_acquire) == 0) return;
    const uint64_t elapsed = now_ns() - g_call.java_start_ns;
    char line[1200];
    {
        std::lock_guard<std::mutex> lock(g_call.mutex);
        std::snprintf(line, sizeof(line),
                      "java-exit thread_id=%llu depth=%zu duration_ms=%llu class=%s method=%s sig=%s",
                      static_cast<unsigned long long>(g_call.java_thread_id), g_call.java_depth,
                      static_cast<unsigned long long>(elapsed / 1000000), g_call.java_class,
                      g_call.java_method, g_call.java_signature);
    }
    if (elapsed >= 250000000ull) kudroid_persistent_breadcrumb(line);
    g_call.java_active_count.fetch_sub(1, std::memory_order_acq_rel);
}

}  // namespace kudroid
