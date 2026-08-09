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

namespace kudroid {
namespace {

pthread_mutex_t gMutexRegistryLock = PTHREAD_MUTEX_INITIALIZER;
std::unordered_map<void*, pthread_mutex_t*> gGuestMutexes;
uintptr_t gStackCheckGuard = 0x4b7544726f696421ULL;

extern "C" int bionic_dummy() {
    return 0;
}

extern "C" int bionic_android_log_print(int priority, const char* tag,
                                          const char* format, ...) {
    (void)priority;
    std::fprintf(stdout, "[AndroidLog][%s]: ", tag ? tag : " unknown");

    va_list args;
    va_start(args, format);
    std::vfprintf(stdout, format ? format : "", args);
    va_end(args);

    std::fputc('\n', stdout);
    return 0;
}

extern "C" int bionic_pthread_mutex_init(void* guestMutex,
                                           const pthread_mutexattr_t* attr) {
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
    pthread_mutex_t* hostMutex = findGuestMutex(guestMutex);
    return hostMutex ? ::pthread_mutex_lock(hostMutex) : -1;
}

extern "C" int bionic_pthread_mutex_unlock(void* guestMutex) {
    pthread_mutex_t* hostMutex = findGuestMutex(guestMutex);
    return hostMutex ? ::pthread_mutex_unlock(hostMutex) : -1;
}

extern "C" int bionic_pthread_mutex_destroy(void* guestMutex) {
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
    {"malloc", reinterpret_cast<void*>(&std::malloc)},
    {"calloc", reinterpret_cast<void*>(&std::calloc)},
    {"realloc", reinterpret_cast<void*>(&std::realloc)},
    {"free", reinterpret_cast<void*>(&std::free)},
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
                return symbol.address;
            }
        }

        std::fprintf(stderr, "Missing Bionic symbol: %s\n", name);
    } else {
        std::fprintf(stderr, "Missing Bionic symbol: <null>\n");
    }

    return reinterpret_cast<void*>(&bionic_dummy);
}

} // namespace kudroid
