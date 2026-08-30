#include "kudroid/NativeCallTelemetry.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

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
};

ActiveCall g_call;

void watchdog_main() {
    while (g_call.started.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if (g_call.active_count.load(std::memory_order_acquire) == 0) continue;

        char cls[sizeof(g_call.class_name)];
        char method[sizeof(g_call.method)];
        char sig[sizeof(g_call.signature)];
        char stage[sizeof(g_call.stage)];
        int depth;
        {
            std::lock_guard<std::mutex> lock(g_call.mutex);
            std::memcpy(cls, g_call.class_name, sizeof(cls));
            std::memcpy(method, g_call.method, sizeof(method));
            std::memcpy(sig, g_call.signature, sizeof(sig));
            std::memcpy(stage, g_call.stage, sizeof(stage));
            depth = g_call.vm_lock_depth;
        }
        const SystemMemory memory = query_system_memory();
        char line[2048];
        std::snprintf(line, sizeof(line),
                      "native-watchdog stage=%s class=%s method=%s sig=%s vm_depth=%d footprint=%llu process_headroom=%llu available=%llu low_memory=%d",
                      stage, cls, method, sig, depth,
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

void native_call_enter(const char* class_name, const char* method,
                       const char* signature, int vm_lock_depth) {
    {
        std::lock_guard<std::mutex> lock(g_call.mutex);
        copy_text(g_call.class_name, sizeof(g_call.class_name), class_name);
        copy_text(g_call.method, sizeof(g_call.method), method);
        copy_text(g_call.signature, sizeof(g_call.signature), signature);
        copy_text(g_call.stage, sizeof(g_call.stage), "enter");
        g_call.vm_lock_depth = vm_lock_depth;
    }
    g_call.active_count.fetch_add(1, std::memory_order_acq_rel);
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
                      "native-stage stage=%s class=%s method=%s sig=%s vm_depth=%d",
                      stage, g_call.class_name, g_call.method, g_call.signature,
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
}

}  // namespace kudroid
