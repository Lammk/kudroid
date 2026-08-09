#include "kudroid/BionicShim.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unordered_map>
#include <unistd.h>
#include <string>

namespace kudroid {
namespace {

pthread_mutex_t gMutexRegistryLock = PTHREAD_MUTEX_INITIALIZER;
std::unordered_map<void*, pthread_mutex_t*> gGuestMutexes;
uintptr_t gStackCheckGuard = 0x4b7544726f696421ULL;
thread_local std::string gShimTrace;

void trace(const char* message) {
    gShimTrace += "[BionicShim] ";
    gShimTrace += message;
    gShimTrace += '\n';
}

extern "C" int bionic_dummy() {
    trace("dummy fallback invoked -> 0");
    return 0;
}

extern "C" void* bionic_malloc(size_t size) {
    void* result = std::malloc(size);
    char message[128];
    std::snprintf(message, sizeof(message), "malloc(size=%zu) -> %p", size, result);
    trace(message);
    return result;
}

extern "C" void bionic_free(void* pointer) {
    char message[128];
    std::snprintf(message, sizeof(message), "free(pointer=%p)", pointer);
    trace(message);
    std::free(pointer);
}

extern "C" int bionic_android_log_print(int priority, const char* tag,
                                          const char* format, ...) {
    char traceMessage[256];
    std::snprintf(traceMessage, sizeof(traceMessage),
                  "__android_log_print(priority=%d, tag=%s)", priority,
                  tag ? tag : "<null>");
    trace(traceMessage);
    std::fprintf(stdout, "[AndroidLog][%s]: ", tag ? tag : " unknown");

    va_list args;
    va_start(args, format);
    va_list traceArgs;
    va_copy(traceArgs, args);
    char formattedMessage[512];
    std::vsnprintf(formattedMessage, sizeof(formattedMessage),
                   format ? format : "", traceArgs);
    va_end(traceArgs);
    gShimTrace += "[BionicShim] android log message: ";
    gShimTrace += formattedMessage;
    gShimTrace += '\n';
    std::vfprintf(stdout, format ? format : "", args);
    va_end(args);

    std::fputc('\n', stdout);
    return 0;
}

extern "C" int bionic_pthread_mutex_init(void* guestMutex,
                                           const pthread_mutexattr_t* attr) {
    trace("pthread_mutex_init()");
    auto* hostMutex = static_cast<pthread_mutex_t*>(std::malloc(sizeof(pthread_mutex_t)));
    if (!hostMutex) return -1;
    const int result = ::pthread_mutex_init(hostMutex, attr);
    if (result != 0) {
        std::free(hostMutex);
        return result;
    }
    ::pthread_mutex_lock(&gMutexRegistryLock);
    gGuestMutexes[guestMutex] = hostMutex;
    ::pthread_mutex_unlock(&gMutexRegistryLock);
    trace("pthread_mutex_init() -> 0");
    return 0;
}

pthread_mutex_t* findGuestMutex(void* guestMutex) {
    ::pthread_mutex_lock(&gMutexRegistryLock);
    auto it = gGuestMutexes.find(guestMutex);
    pthread_mutex_t* hostMutex = it == gGuestMutexes.end() ? nullptr : it->second;
    ::pthread_mutex_unlock(&gMutexRegistryLock);
    return hostMutex;
}

extern "C" int bionic_pthread_mutex_lock(void* guestMutex) {
    trace("pthread_mutex_lock()");
    pthread_mutex_t* hostMutex = findGuestMutex(guestMutex);
    const int result = hostMutex ? ::pthread_mutex_lock(hostMutex) : -1;
    trace(result == 0 ? "pthread_mutex_lock() -> 0" : "pthread_mutex_lock() -> error");
    return result;
}

extern "C" int bionic_pthread_mutex_unlock(void* guestMutex) {
    trace("pthread_mutex_unlock()");
    pthread_mutex_t* hostMutex = findGuestMutex(guestMutex);
    const int result = hostMutex ? ::pthread_mutex_unlock(hostMutex) : -1;
    trace(result == 0 ? "pthread_mutex_unlock() -> 0" : "pthread_mutex_unlock() -> error");
    return result;
}

extern "C" int bionic_pthread_mutex_destroy(void* guestMutex) {
    trace("pthread_mutex_destroy()");
    ::pthread_mutex_lock(&gMutexRegistryLock);
    auto it = gGuestMutexes.find(guestMutex);
    if (it == gGuestMutexes.end()) {
        ::pthread_mutex_unlock(&gMutexRegistryLock);
        return -1;
    }
    pthread_mutex_t* hostMutex = it->second;
    gGuestMutexes.erase(it);
    ::pthread_mutex_unlock(&gMutexRegistryLock);
    const int result = ::pthread_mutex_destroy(hostMutex);
    std::free(hostMutex);
    trace(result == 0 ? "pthread_mutex_destroy() -> 0" : "pthread_mutex_destroy() -> error");
    return result;
}

extern "C" void bionic_stack_chk_fail() {
    std::fprintf(stderr, "Bionic shim: stack check failed\n");
    std::abort();
}

struct SymbolEntry {
    const char* name;
    void* address;
};

const SymbolEntry kSymbols[] = {
    {"malloc", reinterpret_cast<void*>(&bionic_malloc)},
    {"calloc", reinterpret_cast<void*>(&std::calloc)},
    {"realloc", reinterpret_cast<void*>(&std::realloc)},
    {"free", reinterpret_cast<void*>(&bionic_free)},
    {"mmap", reinterpret_cast<void*>(&::mmap)},
    {"munmap", reinterpret_cast<void*>(&::munmap)},
    {"pthread_create", reinterpret_cast<void*>(&::pthread_create)},
    {"pthread_join", reinterpret_cast<void*>(&::pthread_join)},
    {"pthread_mutex_init", reinterpret_cast<void*>(&bionic_pthread_mutex_init)},
    {"pthread_mutex_lock", reinterpret_cast<void*>(&bionic_pthread_mutex_lock)},
    {"pthread_mutex_unlock", reinterpret_cast<void*>(&bionic_pthread_mutex_unlock)},
    {"pthread_mutex_destroy", reinterpret_cast<void*>(&bionic_pthread_mutex_destroy)},
    {"__stack_chk_guard", reinterpret_cast<void*>(&gStackCheckGuard)},
    {"__stack_chk_fail", reinterpret_cast<void*>(&bionic_stack_chk_fail)},
    {"__android_log_print", reinterpret_cast<void*>(&bionic_android_log_print)},
};

} // namespace

void* resolve_bionic_symbol(const char* name) {
    if (name) {
        for (const auto& symbol : kSymbols) {
            if (std::strcmp(name, symbol.name) == 0) {
                char traceMessage[256];
                std::snprintf(traceMessage, sizeof(traceMessage),
                              "bound %s -> %p", name, symbol.address);
                trace(traceMessage);
                return symbol.address;
            }
        }

        std::fprintf(stderr, "Missing Bionic symbol: %s\n", name);
        char traceMessage[256];
        std::snprintf(traceMessage, sizeof(traceMessage),
                  "missing %s -> dummy fallback", name);
        trace(traceMessage);
    } else {
        std::fprintf(stderr, "Missing Bionic symbol: <null>\n");
    }

    return reinterpret_cast<void*>(&bionic_dummy);
}

void bionic_shim_reset_trace() {
    gShimTrace.clear();
}

const char* bionic_shim_trace() {
    return gShimTrace.c_str();
}

} // namespace kudroid
