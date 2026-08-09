#include "kudroid/BionicShim.h"
#include "kudroid/VFSPathRemapper.h"

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
#include <array>
#include <dlfcn.h>

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

[[maybe_unused]] std::string formatGuestLog(const char* format,
                                            const uint64_t* arguments,
                                            const uint64_t* stackArguments) {
    std::string output;
    if (!format) return output;
    std::size_t argumentIndex = 0;
    auto nextArgument = [&]() -> uint64_t {
        return argumentIndex < 5 ? arguments[argumentIndex++]
                                 : stackArguments[argumentIndex++ - 5];
    };
    for (const char* cursor = format; *cursor; ++cursor) {
        if (*cursor != '%') {
            output += *cursor;
            continue;
        }
        ++cursor;
        if (*cursor == '%') {
            output += '%';
            continue;
        }
        while (*cursor == 'l' || *cursor == 'z') ++cursor;
        const uint64_t value = nextArgument();
        switch (*cursor) {
            case 'd':
            case 'i': {
                const auto signedValue = static_cast<int64_t>(value);
                if (signedValue < 0) {
                    output += '-';
                    appendUnsigned(output, static_cast<uint64_t>(-signedValue), 10);
                } else {
                    appendUnsigned(output, static_cast<uint64_t>(signedValue), 10);
                }
                break;
            }
            case 'u': appendUnsigned(output, value, 10); break;
            case 'x': appendUnsigned(output, value, 16); break;
            case 'p':
                output += "0x";
                appendUnsigned(output, value, 16);
                break;
            case 's': {
                const char* stringValue = reinterpret_cast<const char*>(value);
                output += stringValue ? stringValue : "<null>";
                break;
            }
            default:
                output += "<unsupported:%";
                output += *cursor ? *cursor : '?';
                output += '>';
                break;
        }
    }
    return output;
}

int logAndroidMessage(int priority, const char* tag, const std::string& message) {
    char traceMessage[256];
    std::snprintf(traceMessage, sizeof(traceMessage),
                  "__android_log_print(priority=%d, tag=%s)", priority,
                  tag ? tag : "<null>");
    trace(traceMessage);
    gShimTrace += "[BionicShim] android log message: ";
    gShimTrace += message;
    gShimTrace += '\n';
    std::fprintf(stdout, "[AndroidLog][%s]: %s\n", tag ? tag : "unknown",
                 message.c_str());
    return 0;
}

#if defined(__aarch64__)
extern "C" int kudroid_android_log_print_trampoline();

extern "C" int kudroid_android_log_print_from_registers(const uint64_t* registers) {
    const int priority = static_cast<int>(registers[0]);
    const char* tag = reinterpret_cast<const char*>(registers[1]);
    const char* format = reinterpret_cast<const char*>(registers[2]);
    const auto* stackArguments = reinterpret_cast<const uint64_t*>(registers[8]);
    return logAndroidMessage(priority, tag,
                             formatGuestLog(format, registers + 3, stackArguments));
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

extern "C" void bionic_cxa_finalize(void*) {
    trace("__cxa_finalize() -> no-op");
}

extern "C" void bionic_runtime_noop() {
    trace("weak compiler runtime hook -> no-op");
}

extern "C" int bionic_pthread_mutex_init(void* guestMutex,
                                           const void* attr) {
    (void)attr;
    trace("pthread_mutex_init()");
    auto* hostMutex = static_cast<pthread_mutex_t*>(std::malloc(sizeof(pthread_mutex_t)));
    if (!hostMutex) return -1;
    // Ignore Android's attr to avoid memory layout mismatch crash on iOS
    const int result = ::pthread_mutex_init(hostMutex, nullptr);
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

static std::unordered_map<void*, pthread_cond_t*> gGuestConds;
static std::unordered_map<void*, pthread_rwlock_t*> gGuestRwlocks;

extern "C" int bionic_pthread_cond_init(void* cond, const void* attr) {
    (void)attr;
    auto* hostCond = static_cast<pthread_cond_t*>(std::malloc(sizeof(pthread_cond_t)));
    if (!hostCond) return -1;
    const int result = ::pthread_cond_init(hostCond, nullptr);
    if (result != 0) { std::free(hostCond); return result; }
    ::pthread_mutex_lock(&gMutexRegistryLock);
    gGuestConds[cond] = hostCond;
    ::pthread_mutex_unlock(&gMutexRegistryLock);
    return 0;
}

extern "C" int bionic_pthread_rwlock_init(void* rwlock, const void* attr) {
    (void)attr;
    auto* hostRwlock = static_cast<pthread_rwlock_t*>(std::malloc(sizeof(pthread_rwlock_t)));
    if (!hostRwlock) return -1;
    const int result = ::pthread_rwlock_init(hostRwlock, nullptr);
    if (result != 0) { std::free(hostRwlock); return result; }
    ::pthread_mutex_lock(&gMutexRegistryLock);
    gGuestRwlocks[rwlock] = hostRwlock;
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

// --- Cond and Rwlock Wrappers ---
extern "C" int bionic_pthread_cond_destroy(void* cond) {
    ::pthread_mutex_lock(&gMutexRegistryLock);
    auto it = gGuestConds.find(cond);
    if (it == gGuestConds.end()) { ::pthread_mutex_unlock(&gMutexRegistryLock); return -1; }
    pthread_cond_t* hostCond = it->second;
    gGuestConds.erase(it);
    ::pthread_mutex_unlock(&gMutexRegistryLock);
    int res = ::pthread_cond_destroy(hostCond);
    std::free(hostCond);
    return res;
}
extern "C" int bionic_pthread_cond_wait(void* cond, void* mutex) {
    ::pthread_mutex_lock(&gMutexRegistryLock);
    auto itC = gGuestConds.find(cond);
    auto itM = gGuestMutexes.find(mutex);
    pthread_cond_t* hostCond = itC != gGuestConds.end() ? itC->second : nullptr;
    pthread_mutex_t* hostMutex = itM != gGuestMutexes.end() ? itM->second : nullptr;
    ::pthread_mutex_unlock(&gMutexRegistryLock);
    if (!hostCond || !hostMutex) return -1;
    return ::pthread_cond_wait(hostCond, hostMutex);
}
extern "C" int bionic_pthread_cond_timedwait(void* cond, void* mutex, const struct timespec* abstime) {
    ::pthread_mutex_lock(&gMutexRegistryLock);
    auto itC = gGuestConds.find(cond);
    auto itM = gGuestMutexes.find(mutex);
    pthread_cond_t* hostCond = itC != gGuestConds.end() ? itC->second : nullptr;
    pthread_mutex_t* hostMutex = itM != gGuestMutexes.end() ? itM->second : nullptr;
    ::pthread_mutex_unlock(&gMutexRegistryLock);
    if (!hostCond || !hostMutex) return -1;
    return ::pthread_cond_timedwait(hostCond, hostMutex, abstime);
}
extern "C" int bionic_pthread_cond_signal(void* cond) {
    ::pthread_mutex_lock(&gMutexRegistryLock);
    auto it = gGuestConds.find(cond);
    pthread_cond_t* hostCond = it != gGuestConds.end() ? it->second : nullptr;
    ::pthread_mutex_unlock(&gMutexRegistryLock);
    return hostCond ? ::pthread_cond_signal(hostCond) : -1;
}
extern "C" int bionic_pthread_cond_broadcast(void* cond) {
    ::pthread_mutex_lock(&gMutexRegistryLock);
    auto it = gGuestConds.find(cond);
    pthread_cond_t* hostCond = it != gGuestConds.end() ? it->second : nullptr;
    ::pthread_mutex_unlock(&gMutexRegistryLock);
    return hostCond ? ::pthread_cond_broadcast(hostCond) : -1;
}

extern "C" int bionic_pthread_rwlock_destroy(void* rwlock) {
    ::pthread_mutex_lock(&gMutexRegistryLock);
    auto it = gGuestRwlocks.find(rwlock);
    if (it == gGuestRwlocks.end()) { ::pthread_mutex_unlock(&gMutexRegistryLock); return -1; }
    pthread_rwlock_t* hostRwlock = it->second;
    gGuestRwlocks.erase(it);
    ::pthread_mutex_unlock(&gMutexRegistryLock);
    int res = ::pthread_rwlock_destroy(hostRwlock);
    std::free(hostRwlock);
    return res;
}
extern "C" int bionic_pthread_rwlock_rdlock(void* rwlock) {
    ::pthread_mutex_lock(&gMutexRegistryLock);
    auto it = gGuestRwlocks.find(rwlock);
    pthread_rwlock_t* hostRwlock = it != gGuestRwlocks.end() ? it->second : nullptr;
    ::pthread_mutex_unlock(&gMutexRegistryLock);
    return hostRwlock ? ::pthread_rwlock_rdlock(hostRwlock) : -1;
}
extern "C" int bionic_pthread_rwlock_wrlock(void* rwlock) {
    ::pthread_mutex_lock(&gMutexRegistryLock);
    auto it = gGuestRwlocks.find(rwlock);
    pthread_rwlock_t* hostRwlock = it != gGuestRwlocks.end() ? it->second : nullptr;
    ::pthread_mutex_unlock(&gMutexRegistryLock);
    return hostRwlock ? ::pthread_rwlock_wrlock(hostRwlock) : -1;
}
extern "C" int bionic_pthread_rwlock_unlock(void* rwlock) {
    ::pthread_mutex_lock(&gMutexRegistryLock);
    auto it = gGuestRwlocks.find(rwlock);
    pthread_rwlock_t* hostRwlock = it != gGuestRwlocks.end() ? it->second : nullptr;
    ::pthread_mutex_unlock(&gMutexRegistryLock);
    return hostRwlock ? ::pthread_rwlock_unlock(hostRwlock) : -1;
}

extern "C" void bionic_stack_chk_fail() {
    std::fprintf(stderr, "Bionic shim: stack check failed\n");
    std::abort();
}

extern "C" void* bionic_dlopen(const char* filename, int flag) {
    (void)filename;
    (void)flag;
    trace("dlopen() dummy fallback");
    return nullptr;
}

extern "C" void* bionic_dlsym(void* handle, const char* symbol) {
    (void)handle;
    (void)symbol;
    trace("dlsym() dummy fallback");
    return nullptr;
}

extern "C" int bionic_dlclose(void* handle) {
    (void)handle;
    trace("dlclose() dummy fallback");
    return -1;
}

extern "C" char* bionic_dlerror(void) {
    trace("dlerror() dummy fallback");
    return const_cast<char*>("dlerror not implemented");
}

struct SymbolEntry {
    const char* name;
    void* address;
};

// --- Pthread Overrides ---
extern "C" int bionic_pthread_attr_init(void* attr) { (void)attr; return 0; }
extern "C" int bionic_pthread_attr_destroy(void* attr) { (void)attr; return 0; }
extern "C" int bionic_pthread_attr_setstacksize(void* attr, size_t stacksize) { (void)attr; (void)stacksize; return 0; }
extern "C" int bionic_pthread_attr_getstack(void* attr, void** stackaddr, size_t* stacksize) { (void)attr; (void)stackaddr; (void)stacksize; return 0; }
extern "C" int bionic_pthread_attr_setdetachstate(void* attr, int state) { (void)attr; (void)state; return 0; }
extern "C" int bionic_pthread_condattr_init(void* attr) { (void)attr; return 0; }
extern "C" int bionic_pthread_condattr_destroy(void* attr) { (void)attr; return 0; }
extern "C" int bionic_pthread_mutexattr_init(void* attr) { (void)attr; return 0; }
extern "C" int bionic_pthread_mutexattr_destroy(void* attr) { (void)attr; return 0; }
extern "C" int bionic_pthread_mutexattr_settype(void* attr, int type) { (void)attr; (void)type; return 0; }

extern "C" int bionic_pthread_create(pthread_t* thread, void* attr, void* (*start_routine)(void*), void* arg) {
    (void)attr;
    // Ignore Android's pthread_attr_t and pass nullptr to iOS's pthread_create
    return ::pthread_create(thread, nullptr, start_routine, arg);
}

extern "C" int* __error(void);

const SymbolEntry kSymbols[] = {
    {"pthread_create", reinterpret_cast<void*>(&bionic_pthread_create)},
    {"pthread_attr_init", reinterpret_cast<void*>(&bionic_pthread_attr_init)},
    {"pthread_attr_destroy", reinterpret_cast<void*>(&bionic_pthread_attr_destroy)},
    {"pthread_attr_setstacksize", reinterpret_cast<void*>(&bionic_pthread_attr_setstacksize)},
    {"pthread_attr_getstack", reinterpret_cast<void*>(&bionic_pthread_attr_getstack)},
    {"pthread_attr_setdetachstate", reinterpret_cast<void*>(&bionic_pthread_attr_setdetachstate)},
    {"pthread_condattr_init", reinterpret_cast<void*>(&bionic_pthread_condattr_init)},
    {"pthread_condattr_destroy", reinterpret_cast<void*>(&bionic_pthread_condattr_destroy)},
    {"pthread_mutexattr_init", reinterpret_cast<void*>(&bionic_pthread_mutexattr_init)},
    {"pthread_mutexattr_destroy", reinterpret_cast<void*>(&bionic_pthread_mutexattr_destroy)},
    {"pthread_cond_init", reinterpret_cast<void*>(&bionic_pthread_cond_init)},
    {"pthread_cond_destroy", reinterpret_cast<void*>(&bionic_pthread_cond_destroy)},
    {"pthread_cond_wait", reinterpret_cast<void*>(&bionic_pthread_cond_wait)},
    {"pthread_cond_timedwait", reinterpret_cast<void*>(&bionic_pthread_cond_timedwait)},
    {"pthread_cond_signal", reinterpret_cast<void*>(&bionic_pthread_cond_signal)},
    {"pthread_cond_broadcast", reinterpret_cast<void*>(&bionic_pthread_cond_broadcast)},
    {"pthread_rwlock_init", reinterpret_cast<void*>(&bionic_pthread_rwlock_init)},
    {"pthread_rwlock_destroy", reinterpret_cast<void*>(&bionic_pthread_rwlock_destroy)},
    {"pthread_rwlock_rdlock", reinterpret_cast<void*>(&bionic_pthread_rwlock_rdlock)},
    {"pthread_rwlock_wrlock", reinterpret_cast<void*>(&bionic_pthread_rwlock_wrlock)},
    {"pthread_rwlock_unlock", reinterpret_cast<void*>(&bionic_pthread_rwlock_unlock)},
    
    {"__errno", reinterpret_cast<void*>(&__error)},
    {"snprintf", reinterpret_cast<void*>(&std::snprintf)},
    {"memcpy", reinterpret_cast<void*>(&std::memcpy)},
    {"dlopen", reinterpret_cast<void*>(&bionic_dlopen)},
    {"dlsym", reinterpret_cast<void*>(&bionic_dlsym)},
    {"dlclose", reinterpret_cast<void*>(&bionic_dlclose)},
    {"dlerror", reinterpret_cast<void*>(&bionic_dlerror)},
    {"malloc", reinterpret_cast<void*>(&bionic_malloc)},
    {"calloc", reinterpret_cast<void*>(&std::calloc)},
    {"realloc", reinterpret_cast<void*>(&std::realloc)},
    {"free", reinterpret_cast<void*>(&bionic_free)},
    {"mmap", reinterpret_cast<void*>(&::mmap)},
    {"munmap", reinterpret_cast<void*>(&::munmap)},
    {"open", reinterpret_cast<void*>(&vfs_open)},
    {"open64", reinterpret_cast<void*>(&vfs_open64)},
    {"fopen", reinterpret_cast<void*>(&vfs_fopen)},
    {"fopen64", reinterpret_cast<void*>(&vfs_fopen64)},
    {"freopen", reinterpret_cast<void*>(&vfs_freopen)},
    {"access", reinterpret_cast<void*>(&vfs_access)},
    {"stat", reinterpret_cast<void*>(&vfs_stat)},
    {"stat64", reinterpret_cast<void*>(&vfs_stat64)},
    {"lstat", reinterpret_cast<void*>(&vfs_lstat)},
    {"lstat64", reinterpret_cast<void*>(&vfs_lstat64)},
    {"chmod", reinterpret_cast<void*>(&vfs_chmod)},
    {"chown", reinterpret_cast<void*>(&vfs_chown)},
    {"unlink", reinterpret_cast<void*>(&vfs_unlink)},
    {"remove", reinterpret_cast<void*>(&vfs_remove)},
    {"rename", reinterpret_cast<void*>(&vfs_rename)},
    {"mkdir", reinterpret_cast<void*>(&vfs_mkdir)},
    {"rmdir", reinterpret_cast<void*>(&vfs_rmdir)},
    {"opendir", reinterpret_cast<void*>(&vfs_opendir)},
    {"readdir", reinterpret_cast<void*>(&vfs_readdir)},
    {"closedir", reinterpret_cast<void*>(&vfs_closedir)},
    {"readlink", reinterpret_cast<void*>(&vfs_readlink)},
    {"realpath", reinterpret_cast<void*>(&vfs_realpath)},
    {"pthread_create", reinterpret_cast<void*>(&::pthread_create)},
    {"pthread_join", reinterpret_cast<void*>(&::pthread_join)},
    {"pthread_mutex_init", reinterpret_cast<void*>(&bionic_pthread_mutex_init)},
    {"pthread_mutex_lock", reinterpret_cast<void*>(&bionic_pthread_mutex_lock)},
    {"pthread_mutex_unlock", reinterpret_cast<void*>(&bionic_pthread_mutex_unlock)},
    {"pthread_mutex_destroy", reinterpret_cast<void*>(&bionic_pthread_mutex_destroy)},
    {"__stack_chk_guard", reinterpret_cast<void*>(&gStackCheckGuard)},
    {"__stack_chk_fail", reinterpret_cast<void*>(&bionic_stack_chk_fail)},
#if defined(__aarch64__)
    {"__android_log_print", reinterpret_cast<void*>(&kudroid_android_log_print_trampoline)},
#else
    {"__android_log_print", reinterpret_cast<void*>(&bionic_android_log_print)},
#endif
    {"__cxa_finalize", reinterpret_cast<void*>(&bionic_cxa_finalize)},
    {"_ITM_registerTMCloneTable", reinterpret_cast<void*>(&bionic_runtime_noop)},
    {"_ITM_deregisterTMCloneTable", reinterpret_cast<void*>(&bionic_runtime_noop)},
    {"__gmon_start__", reinterpret_cast<void*>(&bionic_runtime_noop)},
};

} // namespace

extern "C" uint64_t kudroid_universal_dummy() {
    trace("WARNING: kudroid_universal_dummy called!");
    return 0;
}

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

        // Try host dlsym
        void* host_ptr = ::dlsym(RTLD_DEFAULT, name);
        if (host_ptr) {
            char traceMessage[256];
            std::snprintf(traceMessage, sizeof(traceMessage),
                          "bound %s -> %p (host)", name, host_ptr);
            trace(traceMessage);
            return host_ptr;
        }

        std::fprintf(stderr, "Missing Bionic symbol: %s\n", name);
        char traceMessage[256];
        std::snprintf(traceMessage, sizeof(traceMessage),
                  "missing %s -> universal dummy", name);
        trace(traceMessage);
        return reinterpret_cast<void*>(&kudroid_universal_dummy);
    } else {
        std::fprintf(stderr, "Missing Bionic symbol: <null>\n");
        return nullptr;
    }
}

void bionic_shim_reset_trace() {
    gShimTrace.clear();
}

const char* bionic_shim_trace() {
    return gShimTrace.c_str();
}

} // namespace kudroid
