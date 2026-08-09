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

extern "C" void* bionic_dlopen(const char* filename, int flag) {
    trace("dlopen() dummy fallback");
    return nullptr;
}

extern "C" void* bionic_dlsym(void* handle, const char* symbol) {
    trace("dlsym() dummy fallback");
    return nullptr;
}

extern "C" int bionic_dlclose(void* handle) {
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

const SymbolEntry kSymbols[] = {
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

#include <dlfcn.h>

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
