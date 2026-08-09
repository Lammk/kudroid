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
#include <atomic>
#include <unistd.h>
#include <string>
#include <array>
#include <dlfcn.h>

namespace kudroid {
namespace {

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


#define KUDROID_SYNC_MAGIC 0x4B5544524F4944ULL

struct InlineGuestSync {
    std::atomic<uint64_t> magic;
    void* host_ptr;
};

static std::atomic_flag gSyncInitLock = ATOMIC_FLAG_INIT;

static inline void* get_or_init_sync(void* guest_ptr, int type) {
    if (!guest_ptr) return nullptr;
    auto* sync = reinterpret_cast<InlineGuestSync*>(guest_ptr);
    
    if (sync->magic.load(std::memory_order_acquire) == KUDROID_SYNC_MAGIC) {
        return sync->host_ptr;
    }

    while (gSyncInitLock.test_and_set(std::memory_order_acquire)) {
        sched_yield();
    }
    
    if (sync->magic.load(std::memory_order_acquire) == KUDROID_SYNC_MAGIC) {
        gSyncInitLock.clear(std::memory_order_release);
        return sync->host_ptr;
    }

    void* host_obj = nullptr;
    if (type == 1) {
        auto* hostMutex = static_cast<pthread_mutex_t*>(std::malloc(sizeof(pthread_mutex_t)));
        ::pthread_mutex_init(hostMutex, nullptr);
        host_obj = hostMutex;
    } else if (type == 2) {
        auto* hostCond = static_cast<pthread_cond_t*>(std::malloc(sizeof(pthread_cond_t)));
        ::pthread_cond_init(hostCond, nullptr);
        host_obj = hostCond;
    } else if (type == 3) {
        auto* hostRwlock = static_cast<pthread_rwlock_t*>(std::malloc(sizeof(pthread_rwlock_t)));
        ::pthread_rwlock_init(hostRwlock, nullptr);
        host_obj = hostRwlock;
    }

    sync->host_ptr = host_obj;
    sync->magic.store(KUDROID_SYNC_MAGIC, std::memory_order_release);
    gSyncInitLock.clear(std::memory_order_release);
    return host_obj;
}

static inline void destroy_sync(void* guest_ptr, int type) {
    if (!guest_ptr) return;
    auto* sync = reinterpret_cast<InlineGuestSync*>(guest_ptr);
    if (sync->magic.load(std::memory_order_acquire) == KUDROID_SYNC_MAGIC) {
        sync->magic.store(0, std::memory_order_release);
        void* host_obj = sync->host_ptr;
        if (host_obj) {
            if (type == 1) ::pthread_mutex_destroy(static_cast<pthread_mutex_t*>(host_obj));
            else if (type == 2) ::pthread_cond_destroy(static_cast<pthread_cond_t*>(host_obj));
            else if (type == 3) ::pthread_rwlock_destroy(static_cast<pthread_rwlock_t*>(host_obj));
            std::free(host_obj);
        }
    }
}

extern "C" int bionic_pthread_mutex_init(void* guestMutex, const void* attr) {
    (void)attr;
    trace("pthread_mutex_init()");
    auto* hostMutex = static_cast<pthread_mutex_t*>(std::malloc(sizeof(pthread_mutex_t)));
    if (!hostMutex) return -1;
    const int result = ::pthread_mutex_init(hostMutex, nullptr);
    if (result != 0) { std::free(hostMutex); return result; }
    auto* sync = reinterpret_cast<InlineGuestSync*>(guestMutex);
    sync->host_ptr = hostMutex;
    sync->magic.store(KUDROID_SYNC_MAGIC, std::memory_order_release);
    trace("pthread_mutex_init() -> 0");
    return 0;
}


extern "C" int bionic_pthread_cond_init(void* cond, const void* attr) {
    (void)attr;
    auto* hostCond = static_cast<pthread_cond_t*>(std::malloc(sizeof(pthread_cond_t)));
    if (!hostCond) return -1;
    const int result = ::pthread_cond_init(hostCond, nullptr);
    if (result != 0) { std::free(hostCond); return result; }
    auto* sync = reinterpret_cast<InlineGuestSync*>(cond);
    sync->host_ptr = hostCond;
    sync->magic.store(KUDROID_SYNC_MAGIC, std::memory_order_release);
    return 0;
}

extern "C" int bionic_pthread_rwlock_init(void* rwlock, const void* attr) {
    (void)attr;
    auto* hostRwlock = static_cast<pthread_rwlock_t*>(std::malloc(sizeof(pthread_rwlock_t)));
    if (!hostRwlock) return -1;
    const int result = ::pthread_rwlock_init(hostRwlock, nullptr);
    if (result != 0) { std::free(hostRwlock); return result; }
    auto* sync = reinterpret_cast<InlineGuestSync*>(rwlock);
    sync->host_ptr = hostRwlock;
    sync->magic.store(KUDROID_SYNC_MAGIC, std::memory_order_release);
    return 0;
}



extern "C" int bionic_pthread_mutex_lock(void* guestMutex) {
    trace("pthread_mutex_lock()");
    pthread_mutex_t* hostMutex = static_cast<pthread_mutex_t*>(get_or_init_sync(guestMutex, 1));
    const int result = hostMutex ? ::pthread_mutex_lock(hostMutex) : -1;
    trace(result == 0 ? "pthread_mutex_lock() -> 0" : "pthread_mutex_lock() -> error");
    return result;
}

extern "C" int bionic_pthread_mutex_unlock(void* guestMutex) {
    trace("pthread_mutex_unlock()");
    pthread_mutex_t* hostMutex = static_cast<pthread_mutex_t*>(get_or_init_sync(guestMutex, 1));
    const int result = hostMutex ? ::pthread_mutex_unlock(hostMutex) : -1;
    trace(result == 0 ? "pthread_mutex_unlock() -> 0" : "pthread_mutex_unlock() -> error");
    return result;
}

extern "C" int bionic_pthread_mutex_destroy(void* guestMutex) {
    trace("pthread_mutex_destroy()");
    destroy_sync(guestMutex, 1);
    trace("pthread_mutex_destroy() -> 0");
    return 0;
}

// --- Cond and Rwlock Wrappers ---
extern "C" int bionic_pthread_cond_destroy(void* cond) {
    destroy_sync(cond, 2);
    return 0;
}
extern "C" int bionic_pthread_cond_wait(void* cond, void* mutex) {
    pthread_cond_t* hostCond = static_cast<pthread_cond_t*>(get_or_init_sync(cond, 2));
    pthread_mutex_t* hostMutex = static_cast<pthread_mutex_t*>(get_or_init_sync(mutex, 1));
    if (!hostCond || !hostMutex) return -1;
    return ::pthread_cond_wait(hostCond, hostMutex);
}
extern "C" int bionic_pthread_cond_timedwait(void* cond, void* mutex, const struct timespec* abstime) {
    pthread_cond_t* hostCond = static_cast<pthread_cond_t*>(get_or_init_sync(cond, 2));
    pthread_mutex_t* hostMutex = static_cast<pthread_mutex_t*>(get_or_init_sync(mutex, 1));
    if (!hostCond || !hostMutex) return -1;
    return ::pthread_cond_timedwait(hostCond, hostMutex, abstime);
}
extern "C" int bionic_pthread_cond_signal(void* cond) {
    pthread_cond_t* hostCond = static_cast<pthread_cond_t*>(get_or_init_sync(cond, 2));
    return hostCond ? ::pthread_cond_signal(hostCond) : -1;
}
extern "C" int bionic_pthread_cond_broadcast(void* cond) {
    pthread_cond_t* hostCond = static_cast<pthread_cond_t*>(get_or_init_sync(cond, 2));
    return hostCond ? ::pthread_cond_broadcast(hostCond) : -1;
}

extern "C" int bionic_pthread_rwlock_destroy(void* rwlock) {
    destroy_sync(rwlock, 3);
    return 0;
}
extern "C" int bionic_pthread_rwlock_rdlock(void* rwlock) {
    pthread_rwlock_t* hostRwlock = static_cast<pthread_rwlock_t*>(get_or_init_sync(rwlock, 3));
    return hostRwlock ? ::pthread_rwlock_rdlock(hostRwlock) : -1;
}
extern "C" int bionic_pthread_rwlock_wrlock(void* rwlock) {
    pthread_rwlock_t* hostRwlock = static_cast<pthread_rwlock_t*>(get_or_init_sync(rwlock, 3));
    return hostRwlock ? ::pthread_rwlock_wrlock(hostRwlock) : -1;
}
extern "C" int bionic_pthread_rwlock_unlock(void* rwlock) {
    pthread_rwlock_t* hostRwlock = static_cast<pthread_rwlock_t*>(get_or_init_sync(rwlock, 3));
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
extern "C" int vfs_mkdir(const char* path, mode_t mode);
extern "C" int vfs_rmdir(const char* path);
extern "C" DIR* vfs_opendir(const char* name);
extern "C" struct dirent* vfs_readdir(DIR* dirp);
extern "C" int vfs_closedir(DIR* dirp);
extern "C" ssize_t vfs_readlink(const char* path, char* buf, size_t bufsiz);
extern "C" char* vfs_realpath(const char* path, char* resolved_path);
extern "C" int vfs_fstat(int fd, void* info);
extern "C" int vfs_fstat64(int fd, void* info);

// Dummy fallback functions
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


extern "C" int bionic_pthread_mutex_trylock(void* guestMutex) {
    pthread_mutex_t* hostMutex = static_cast<pthread_mutex_t*>(get_or_init_sync(guestMutex, 1));
    return hostMutex ? ::pthread_mutex_trylock(hostMutex) : -1;
}

extern "C" int bionic_pthread_key_create(void* guestKey, void (*destructor)(void*)) {
    pthread_key_t hostKey;
    int res = ::pthread_key_create(&hostKey, destructor);
    if (res == 0) {
        *static_cast<int*>(guestKey) = static_cast<int>(hostKey);
    }
    return res;
}
extern "C" void* bionic_pthread_getspecific(int guestKey) {
    return ::pthread_getspecific(static_cast<pthread_key_t>(guestKey));
}
extern "C" int bionic_pthread_setspecific(int guestKey, const void* value) {
    return ::pthread_setspecific(static_cast<pthread_key_t>(guestKey), value);
}
extern "C" int bionic_pthread_key_delete(int guestKey) {
    return ::pthread_key_delete(static_cast<pthread_key_t>(guestKey));
}

extern "C" int bionic_pthread_once(int* guest_once, void (*init_routine)(void)) {
    static pthread_mutex_t once_lock = PTHREAD_MUTEX_INITIALIZER;
    ::pthread_mutex_lock(&once_lock);
    if (*guest_once == 0) {
        *guest_once = 1;
        init_routine();
    }
    ::pthread_mutex_unlock(&once_lock);
    return 0;
}


#undef sa_handler
#undef sa_sigaction

struct android_sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, void*, void*);
    };
    uint64_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

#include <signal.h>

extern "C" int bionic_sigaction(int signum, const struct android_sigaction* act, struct android_sigaction* oldact) {
    (void)act;
    (void)oldact;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "sigaction(signum=%d)", signum);
    trace(buf);
    
    // For now, just ignore it to prevent it from crashing the host handler,
    // but returning 0 makes IL2CPP think it succeeded.
    // If it's SIGSEGV (11), IL2CPP will crash on intentional null deref.
    return 0;
}

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
    {"pthread_mutexattr_settype", reinterpret_cast<void*>(&bionic_pthread_mutexattr_settype)},
    {"pthread_mutex_trylock", reinterpret_cast<void*>(&bionic_pthread_mutex_trylock)},
    {"pthread_key_create", reinterpret_cast<void*>(&bionic_pthread_key_create)},
    {"pthread_getspecific", reinterpret_cast<void*>(&bionic_pthread_getspecific)},
    {"pthread_setspecific", reinterpret_cast<void*>(&bionic_pthread_setspecific)},
    {"pthread_key_delete", reinterpret_cast<void*>(&bionic_pthread_key_delete)},
    {"pthread_once", reinterpret_cast<void*>(&bionic_pthread_once)},
    {"sigaction", reinterpret_cast<void*>(&bionic_sigaction)},
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
    {"fstat", reinterpret_cast<void*>(&vfs_fstat)},
    {"fstat64", reinterpret_cast<void*>(&vfs_fstat64)},
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
