#include "kudroid/BionicShim.h"
#include "kudroid/VFSPathRemapper.h"

#include <cmath>
#include <ctime>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <unwind.h>
#include <cxxabi.h>
#include <chrono>
#include <sys/socket.h>
#include <condition_variable>

extern "C" void __gxx_personality_v0();

// For Bionic pthread emulation
#include <cstdarg>
#ifdef __APPLE__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/event.h>
#include <dispatch/dispatch.h>
#else
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#endif

#ifndef EPOLLIN
#define EPOLLIN 0x001
#define EPOLLPRI 0x002
#define EPOLLOUT 0x004
#define EPOLLERR 0x008
#define EPOLLHUP 0x010
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3
#endif

struct android_epoll_event {
    uint32_t events;
    uint64_t data;
} __attribute__((packed));
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <unordered_map>
#include <map>
#include <mutex>
#include <shared_mutex>
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
    snprintf(traceMessage, sizeof(traceMessage),
                  "__android_log_print(priority=%d, tag=%s)", priority,
                  tag ? tag : "<null>");
    trace(traceMessage);
    gShimTrace += "[BionicShim] android log message: ";
    gShimTrace += message;
    gShimTrace += '\n';
    fprintf(stdout, "[AndroidLog][%s]: %s\n", tag ? tag : "unknown",
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


static std::unordered_map<void*, void*> gSyncRegistry;
static std::shared_mutex gSyncRegistryLock;

static inline void* get_or_init_sync(void* guest_ptr, int type) {
    if (!guest_ptr) return nullptr;
    
    {
        std::shared_lock<std::shared_mutex> lock(gSyncRegistryLock);
        auto it = gSyncRegistry.find(guest_ptr);
        if (it != gSyncRegistry.end()) {
            return it->second;
        }
    }
    
    std::unique_lock<std::shared_mutex> lock(gSyncRegistryLock);
    // Double check
    auto it = gSyncRegistry.find(guest_ptr);
    if (it != gSyncRegistry.end()) {
        return it->second;
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

    gSyncRegistry[guest_ptr] = host_obj;
    return host_obj;
}

static inline void destroy_sync(void* guest_ptr, int type) {
    if (!guest_ptr) return;
    
    std::unique_lock<std::shared_mutex> lock(gSyncRegistryLock);
    auto it = gSyncRegistry.find(guest_ptr);
    if (it != gSyncRegistry.end()) {
        void* host_obj = it->second;
        if (host_obj) {
            if (type == 1) ::pthread_mutex_destroy(static_cast<pthread_mutex_t*>(host_obj));
            else if (type == 2) ::pthread_cond_destroy(static_cast<pthread_cond_t*>(host_obj));
            else if (type == 3) ::pthread_rwlock_destroy(static_cast<pthread_rwlock_t*>(host_obj));
            free(host_obj);
        }
        gSyncRegistry.erase(it);
    }
}

extern "C" int bionic_pthread_mutex_init(void* guestMutex, const void* attr) {
    (void)attr;
    trace("pthread_mutex_init()");
    auto* hostMutex = static_cast<pthread_mutex_t*>(std::malloc(sizeof(pthread_mutex_t)));
    if (!hostMutex) return -1;
    const int result = ::pthread_mutex_init(hostMutex, nullptr);
    if (result != 0) { free(hostMutex); return result; }
    
    std::unique_lock<std::shared_mutex> lock(gSyncRegistryLock);
    gSyncRegistry[guestMutex] = hostMutex;
    trace("pthread_mutex_init() -> 0");
    return 0;
}

extern "C" int bionic_pthread_cond_init(void* cond, const void* attr) {
    (void)attr;
    auto* hostCond = static_cast<pthread_cond_t*>(std::malloc(sizeof(pthread_cond_t)));
    if (!hostCond) return -1;
    const int result = ::pthread_cond_init(hostCond, nullptr);
    if (result != 0) { free(hostCond); return result; }
    
    std::unique_lock<std::shared_mutex> lock(gSyncRegistryLock);
    gSyncRegistry[cond] = hostCond;
    return 0;
}

extern "C" int bionic_pthread_rwlock_init(void* rwlock, const void* attr) {
    (void)attr;
    auto* hostRwlock = static_cast<pthread_rwlock_t*>(std::malloc(sizeof(pthread_rwlock_t)));
    if (!hostRwlock) return -1;
    const int result = ::pthread_rwlock_init(hostRwlock, nullptr);
    if (result != 0) { free(hostRwlock); return result; }
    
    std::unique_lock<std::shared_mutex> lock(gSyncRegistryLock);
    gSyncRegistry[rwlock] = hostRwlock;
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
    fprintf(stderr, "Bionic shim: stack check failed\n");
    std::abort();
}

extern "C" void* bionic_dlopen(const char* filename, int flag) {
    (void)flag;
    if (filename) {
        if (strstr(filename, "libvulkan.so")) {
            trace("dlopen: Intercepted libvulkan.so! Returning VULKAN handle.");
            return (void*)0x8888;
        }
        if (strstr(filename, "libGLESv2.so") || strstr(filename, "libEGL.so") || strstr(filename, "libOpenSLES.so")) {
            trace("dlopen: Intercepted Graphics/Audio! Returning NATIVE handle.");
            return (void*)0x9999;
        }
        char msg[256];
        snprintf(msg, sizeof(msg), "dlopen() dummy fallback for: %s", filename);
        trace(msg);
    } else {
        trace("dlopen() dummy fallback for NULL");
    }
    return nullptr;
}

extern "C" void* bionic_dlsym(void* handle, const char* symbol) {
    if (handle == (void*)0x8888 || handle == (void*)0x9999) {
        if (symbol) {
            void* host_ptr = ::dlsym(RTLD_DEFAULT, symbol);
            if (host_ptr) {
                char msg[256];
                snprintf(msg, sizeof(msg), "dlsym: Resolved %s natively", symbol);
                trace(msg);
                return host_ptr;
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg), "dlsym: Native symbol %s not found!", symbol);
                trace(msg);
            }
        }
        return nullptr;
    }
    
    if (symbol) {
        char msg[256];
        snprintf(msg, sizeof(msg), "dlsym() dummy fallback for: %s", symbol);
        trace(msg);
    } else {
        trace("dlsym() dummy fallback for NULL");
    }
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
extern "C" int vfs_fstat(int fd, void* info);
extern "C" int vfs_fstat64(int fd, void* info);

// Dummy fallback functions
extern "C" int bionic_pthread_attr_init(void* attr) { (void)attr; return 0; }
extern "C" int bionic_pthread_attr_destroy(void* attr) { (void)attr; return 0; }
extern "C" int bionic_pthread_attr_setstacksize(void* attr, size_t stacksize) { (void)attr; (void)stacksize; return 0; }
extern "C" int bionic_pthread_attr_getstack(void* attr, void** stackaddr, size_t* stacksize) { (void)attr; (void)stackaddr; (void)stacksize; return 0; }
extern "C" int bionic_pthread_attr_setdetachstate(void* attr, int state) { (void)attr; (void)state; return 0; }
extern "C" int bionic_pthread_getattr_np(pthread_t thread, void* attr) { (void)thread; (void)attr; return 0; }

// Global metal layer pointer provided by kudroid_bridge.cpp
} // namespace
} // namespace kudroid
extern void* g_metalLayer;
namespace kudroid {
namespace {

extern "C" void* bionic_ANativeWindow_fromSurface(void* env, void* surface) {
    (void)env; (void)surface;
    // ANGLE on iOS uses CAMetalLayer/UIView as the native window!
    return g_metalLayer;
}
extern "C" int bionic_ANativeWindow_getWidth(void* window) { (void)window; return 1080; }
extern "C" int bionic_ANativeWindow_getHeight(void* window) { (void)window; return 1920; }
extern "C" int bionic_ANativeWindow_setBuffersGeometry(void* window, int width, int height, int format) {
    (void)window; (void)width; (void)height; (void)format; return 0;
}
extern "C" void bionic_ANativeWindow_release(void* window) { (void)window; }
extern "C" void bionic_ANativeWindow_acquire(void* window) { (void)window; }

extern "C" void* bionic_ASensorManager_getInstance() {
    static int dummyManager = 1;
    return &dummyManager;
}
extern "C" void* bionic_ASensorManager_createEventQueue(void* manager, void* looper, int ident, void* callback, void* data) {
    (void)manager; (void)looper; (void)ident; (void)callback; (void)data;
    static int dummyQueue = 1;
    return &dummyQueue;
}
extern "C" int bionic_ASensorManager_destroyEventQueue(void* manager, void* queue) {
    (void)manager; (void)queue; return 0;
}
extern "C" int bionic_ASensorManager_getSensorList(void* manager, void** list) {
    (void)manager; (void)list; return 0; // 0 sensors
}
extern "C" void* bionic_ASensorManager_getDefaultSensor(void* manager, int type) {
    (void)manager; (void)type;
    static int dummySensor = 1;
    return &dummySensor;
}
extern "C" int bionic_ASensorEventQueue_enableSensor(void* queue, void* sensor) {
    (void)queue; (void)sensor;
    return 0;
}
extern "C" int bionic_ASensorEventQueue_disableSensor(void* queue, void* sensor) {
    (void)queue; (void)sensor; return 0;
}
extern "C" int bionic_ASensorEventQueue_setEventRate(void* queue, void* sensor, int32_t usec) {
    (void)queue; (void)sensor; (void)usec;
    return 0;
}
extern "C" int bionic_ASensorEventQueue_hasEvents(void* queue) {
    (void)queue; return 0;
}
extern "C" ssize_t bionic_ASensorEventQueue_getEvents(void* queue, void* events, size_t count) {
    (void)queue; (void)events; (void)count; return 0;
}

extern "C" int bionic_ioctl(int fd, unsigned long request, ...) {
    (void)fd; (void)request;
    return 0; // Success for all dummy fds
}

#define PR_SET_NAME 15
#define PR_SET_VMA  0x53564d41

extern "C" int bionic_prctl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    if (option == PR_SET_NAME) {
#ifdef __APPLE__
        return pthread_setname_np(reinterpret_cast<const char*>(arg2));
#else
        return pthread_setname_np(pthread_self(), reinterpret_cast<const char*>(arg2));
#endif
    } else if (option == PR_SET_VMA) {
        // Just fake it for PR_SET_VMA_ANON_NAME to prevent crashes
        return 0;
    }
    return 0;
}

// Memory mapping wrappers to strip Linux specific flags
extern "C" void* bionic_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
#ifdef __APPLE__
    // Strip MAP_POPULATE (0x8000) and other Linux flags not present in Darwin
    int darwin_flags = flags & ~(0x8000 | 0x4000); 
    return ::mmap(addr, length, prot, darwin_flags, fd, offset);
#else
    return ::mmap(addr, length, prot, flags, fd, offset);
#endif
}

extern "C" void* bionic_mmap64(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    return bionic_mmap(addr, length, prot, flags, fd, offset);
}

extern "C" int bionic_mprotect(void *addr, size_t len, int prot) {
    return ::mprotect(addr, len, prot);
}

extern "C" int bionic_madvise(void *addr, size_t length, int advice) {
#ifdef __APPLE__
    // MADV_DONTNEED in Linux is 4, in Darwin it is 4 as well, but others might differ.
    // For safety in emulation, we return 0 for unsupported advice.
    if (advice > 10) return 0;
    return ::madvise(addr, length, advice);
#else
    return ::madvise(addr, length, advice);
#endif
}

extern "C" int bionic_sigaltstack(const stack_t *ss, stack_t *oss) {
    return ::sigaltstack(ss, oss);
}

#ifndef __APPLE__
#include <sys/syscall.h>
#endif

extern "C" pid_t bionic_gettid() {
#ifdef __APPLE__
    uint64_t tid;
    pthread_threadid_np(NULL, &tid);
    return static_cast<pid_t>(tid);
#else
    return ::syscall(SYS_gettid);
#endif
}

extern "C" void* bionic_mremap(void *old_address, size_t old_size, size_t new_size, int flags, void *new_address) {
    (void)new_address;
    if (flags & 1) { // MREMAP_MAYMOVE
        void* new_ptr = mmap(NULL, new_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (new_ptr != MAP_FAILED) {
            std::memcpy(new_ptr, old_address, old_size);
            munmap(old_address, old_size);
            return new_ptr;
        }
    }
    return MAP_FAILED;
}

#define AT_HWCAP 16
#define HWCAP_NEON (1 << 12)
#define HWCAP_AES (1 << 3)
#define HWCAP_PMULL (1 << 4)
#define HWCAP_SHA1 (1 << 5)
#define HWCAP_SHA2 (1 << 6)
#define HWCAP_CRC32 (1 << 7)

extern "C" unsigned long bionic_getauxval(unsigned long type) {
    if (type == AT_HWCAP) {
        return HWCAP_NEON | HWCAP_AES | HWCAP_PMULL | HWCAP_SHA1 | HWCAP_SHA2 | HWCAP_CRC32;
    }
    return 0;
}

extern "C" ssize_t bionic_getrandom(void *buf, size_t buflen, unsigned int flags) {
    (void)flags;
    int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    ssize_t ret = ::read(fd, buf, buflen);
    ::close(fd);
    return ret;
}

extern "C" int bionic_ashmem_create_region(const char *name, size_t size) {
    (void)name;
    char shm_name[64];
    std::snprintf(shm_name, sizeof(shm_name), "/kudroid_ashmem_%d_%d", ::getpid(), rand());
    int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
        shm_unlink(shm_name);
        if (::ftruncate(fd, size) < 0) {
            // Ignored, just for suppressing warning
        }
        return fd;
    }
    return -1;
}

extern "C" int bionic_ashmem_set_prot_region(int fd, int prot) {
    (void)fd; (void)prot;
    return 0; // Always allow
}

extern "C" const char* bionic_ASensor_getName(void* sensor) {
    (void)sensor; return "DummySensor";
}
extern "C" const char* bionic_ASensor_getVendor(void* sensor) {
    (void)sensor; return "Kudroid";
}
extern "C" int bionic_ASensor_getType(void* sensor) {
    (void)sensor; return 1; // ASENSOR_TYPE_ACCELEROMETER
}
extern "C" float bionic_ASensor_getResolution(void* sensor) {
    (void)sensor; return 1.0f;
}
extern "C" int bionic_ASensor_getMinDelay(void* sensor) {
    (void)sensor; return 10000;
}

// --- Linux-Specific Syscalls ---

struct FutexWaitQueue {
    std::mutex mtx;
    std::condition_variable cv;
};
static std::unordered_map<uint32_t*, FutexWaitQueue> g_futexQueues;
static std::mutex g_futexGlobalMtx;

extern "C" int bionic_futex(uint32_t *uaddr, int futex_op, uint32_t val, const struct timespec *timeout, uint32_t *uaddr2, uint32_t val3) {
    (void)uaddr; (void)futex_op; (void)val; (void)timeout; (void)uaddr2; (void)val3;
    int cmd = futex_op & 127; // remove private flag
    if (cmd == 0) { // FUTEX_WAIT
        std::unique_lock<std::mutex> lock(g_futexGlobalMtx);
        FutexWaitQueue& q = g_futexQueues[uaddr];
        std::unique_lock<std::mutex> qLock(q.mtx);
        lock.unlock();

        if (*uaddr != val) return -1;

        if (timeout) {
            auto duration = std::chrono::seconds(timeout->tv_sec) + std::chrono::nanoseconds(timeout->tv_nsec);
            if (q.cv.wait_for(qLock, duration) == std::cv_status::timeout) return -1;
        } else {
            q.cv.wait(qLock);
        }
        return 0;
    } else if (cmd == 1) { // FUTEX_WAKE
        std::unique_lock<std::mutex> lock(g_futexGlobalMtx);
        auto it = g_futexQueues.find(uaddr);
        if (it != g_futexQueues.end()) {
            std::unique_lock<std::mutex> qLock(it->second.mtx);
            lock.unlock();
            if (val == 1) it->second.cv.notify_one();
            else it->second.cv.notify_all();
            return val;
        }
        return 0;
    }
    return -1;
}

#ifdef __APPLE__

static int create_loopback_udp() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(fd); return -1;
    }
    socklen_t len = sizeof(addr);
    getsockname(fd, (struct sockaddr*)&addr, &len);
    connect(fd, (struct sockaddr*)&addr, len);
    
    // Set non-blocking just in case
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

// For timerfd, we keep a map of fd to GCD timer
static std::map<int, dispatch_source_t> g_timerfds;
static std::mutex g_timerfds_mtx;
#endif

extern "C" int bionic_eventfd(unsigned int initval, int flags) {
#ifdef __APPLE__
    (void)flags;
    int fd = create_loopback_udp();
    if (fd >= 0 && initval > 0) {
        uint64_t val = initval;
        write(fd, &val, sizeof(val));
    }
    return fd;
#else
    return ::eventfd(initval, flags);
#endif
}

extern "C" int bionic_timerfd_create(int clockid, int flags) {
#ifdef __APPLE__
    (void)clockid; (void)flags;
    return create_loopback_udp();
#else
    return ::timerfd_create(clockid, flags);
#endif
}

struct bionic_itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
};

extern "C" int bionic_timerfd_settime(int fd, int flags, const struct bionic_itimerspec *new_value, struct bionic_itimerspec *old_value) {
#ifdef __APPLE__
    (void)flags; (void)old_value;
    if (fd < 0 || !new_value) return -1;
    
    std::lock_guard<std::mutex> lock(g_timerfds_mtx);
    if (g_timerfds.count(fd)) {
        dispatch_source_cancel(g_timerfds[fd]);
        g_timerfds.erase(fd);
    }
    
    if (new_value->it_value.tv_sec == 0 && new_value->it_value.tv_nsec == 0) {
        return 0; // disarm
    }
    
    dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0));
    uint64_t start = new_value->it_value.tv_sec * 1000000000ull + new_value->it_value.tv_nsec;
    uint64_t interval = new_value->it_interval.tv_sec * 1000000000ull + new_value->it_interval.tv_nsec;
    if (interval == 0) interval = DISPATCH_TIME_FOREVER;
    
    dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, start), interval, 1000000); // 1ms leeway
    dispatch_source_set_event_handler(timer, ^{
        uint64_t exp = 1;
        write(fd, &exp, sizeof(exp));
    });
    dispatch_resume(timer);
    g_timerfds[fd] = timer;
    
    return 0;
#else
    return ::timerfd_settime(fd, flags, reinterpret_cast<const struct itimerspec*>(new_value), reinterpret_cast<struct itimerspec*>(old_value));
#endif
}

extern "C" int bionic_epoll_create(int size) {
#ifdef __APPLE__
    (void)size;
    return kqueue();
#else
    return ::epoll_create(size);
#endif
}
extern "C" int bionic_epoll_create1(int flags) { 
#ifdef __APPLE__
    (void)flags; return kqueue(); 
#else
    return ::epoll_create1(flags);
#endif
}

extern "C" int bionic_epoll_ctl(int epfd, int op, int fd, void *event_ptr) {
#ifdef __APPLE__
    if (epfd < 0 || fd < 0) return -1;
    struct android_epoll_event* event = static_cast<struct android_epoll_event*>(event_ptr);
    
    if (op == EPOLL_CTL_DEL) {
        struct kevent changes[2];
        EV_SET(&changes[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        EV_SET(&changes[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        // Execute individually to safely ignore ENOENT (filter not found)
        kevent(epfd, &changes[0], 1, NULL, 0, NULL);
        kevent(epfd, &changes[1], 1, NULL, 0, NULL);
        return 0;
    } 
    
    uint16_t base_flags = EV_ADD;
    uint32_t ep_events = event->events;
    void* udata = reinterpret_cast<void*>(event->data);
    
    // Android defines: EPOLLET = (1U << 31), EPOLLONESHOT = (1U << 30)
    if (ep_events & (1U << 31)) base_flags |= EV_CLEAR;
    if (ep_events & (1U << 30)) base_flags |= EV_ONESHOT;

    if (op == EPOLL_CTL_MOD) {
        // Blindly delete both first
        struct kevent del_changes[2];
        EV_SET(&del_changes[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        EV_SET(&del_changes[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        kevent(epfd, &del_changes[0], 1, NULL, 0, NULL);
        kevent(epfd, &del_changes[1], 1, NULL, 0, NULL);
    }
    
    struct kevent changes[2];
    int num_changes = 0;
    
    if (ep_events & EPOLLIN) {
        EV_SET(&changes[num_changes++], fd, EVFILT_READ, base_flags, 0, 0, udata);
    }
    if (ep_events & EPOLLOUT) {
        EV_SET(&changes[num_changes++], fd, EVFILT_WRITE, base_flags, 0, 0, udata);
    }
    
    if (num_changes > 0) {
        if (kevent(epfd, changes, num_changes, NULL, 0, NULL) == -1) return -1;
    }
    return 0;
#else
    return ::epoll_ctl(epfd, op, fd, reinterpret_cast<struct epoll_event*>(event_ptr));
#endif
}

extern "C" int bionic_epoll_wait(int epfd, void *events_ptr, int maxevents, int timeout) {
#ifdef __APPLE__
    if (epfd < 0 || maxevents <= 0) return -1;
    struct android_epoll_event* events = static_cast<struct android_epoll_event*>(events_ptr);
    
    // We fetch maxevents * 2 because read and write events for the same FD are separated in kqueue
    struct kevent* evlist = (struct kevent*)std::malloc(sizeof(struct kevent) * maxevents * 2);
    if (!evlist) return -1;
    
    struct timespec ts;
    struct timespec* ts_ptr = NULL;
    if (timeout >= 0) {
        ts.tv_sec = timeout / 1000;
        ts.tv_nsec = (timeout % 1000) * 1000000;
        ts_ptr = &ts;
    }
    
    int n = kevent(epfd, NULL, 0, evlist, maxevents * 2, ts_ptr);
    int unique_events = 0;
    
    if (n > 0) {
        std::unordered_map<uint64_t, uint32_t> coalesced;
        for (int i = 0; i < n; i++) {
            uint64_t udata = reinterpret_cast<uint64_t>(evlist[i].udata);
            uint32_t flags = 0;
            if (evlist[i].filter == EVFILT_READ) flags |= EPOLLIN;
            else if (evlist[i].filter == EVFILT_WRITE) flags |= EPOLLOUT;
            if (evlist[i].flags & EV_ERROR) flags |= EPOLLERR;
            if (evlist[i].flags & EV_EOF) flags |= EPOLLHUP;
            coalesced[udata] |= flags;
        }
        
        for (auto const& [udata, flags] : coalesced) {
            if (unique_events >= maxevents) break;
            events[unique_events].events = flags;
            events[unique_events].data = udata;
            unique_events++;
        }
    }
    std::free(evlist);
    return n >= 0 ? unique_events : -1;
#else
    return ::epoll_wait(epfd, reinterpret_cast<struct epoll_event*>(events_ptr), maxevents, timeout);
#endif
}

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

struct android_sigaction {
    union {
        void (*android_sa_handler)(int);
        void (*android_sa_sigaction)(int, void*, void*);
    };
    uint64_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

#include <signal.h>

extern "C" int bionic_sigaction(int signum, const struct android_sigaction* act, struct android_sigaction* oldact) {
#ifdef KUDROID_DEBUG
    char buf[128];
    std::snprintf(buf, sizeof(buf), "sigaction(signum=%d)", signum);
    trace(buf);
#endif
    
    struct sigaction host_act;
    struct sigaction host_oldact;
    
    if (act) {
        std::memset(&host_act, 0, sizeof(host_act));
        if (act->sa_flags & 0x00000004) { // Android SA_SIGINFO
            host_act.sa_sigaction = reinterpret_cast<void (*)(int, siginfo_t*, void*)>(act->android_sa_sigaction);
            host_act.sa_flags |= SA_SIGINFO; // iOS SA_SIGINFO
        } else {
            host_act.sa_handler = act->android_sa_handler;
        }
    }
    
    int ret = ::sigaction(signum, act ? &host_act : nullptr, oldact ? &host_oldact : nullptr);
    
    if (oldact && ret == 0) {
        std::memset(oldact, 0, sizeof(struct android_sigaction));
        if (host_oldact.sa_flags & SA_SIGINFO) {
            oldact->sa_flags |= 0x00000004;
            oldact->android_sa_sigaction = reinterpret_cast<void (*)(int, void*, void*)>(host_oldact.sa_sigaction);
        } else {
            oldact->android_sa_handler = host_oldact.sa_handler;
        }
    }
    
    return ret;
}

static pthread_key_t tls_key;
static pthread_once_t tls_key_once = PTHREAD_ONCE_INIT;

static void tls_destructor(void* tls_base) {
    if (tls_base) {
        std::free(tls_base);
    }
}

static void init_tls_key() {
    ::pthread_key_create(&tls_key, tls_destructor);
}

struct BionicThreadArgs {
    void* (*start_routine)(void*);
    void* arg;
};

static void* bionic_thread_wrapper(void* rawArgs);

extern "C" void bionic_init_main_thread_tls(void) {
    ::pthread_once(&tls_key_once, init_tls_key);
    void* tls_base = std::aligned_alloc(16, 65536); 
    std::memset(tls_base, 0, 65536);
    
    char* tls_ptr = (char*)tls_base + 32768;
    
    // Set a dummy stack guard cookie at Slot 5 (offset 40)
    uint64_t* stack_guard_ptr = reinterpret_cast<uint64_t*>(tls_ptr + 40);
    *stack_guard_ptr = 0x1337BEEFCAFECAFE;
    
    ::pthread_setspecific(tls_key, tls_base);

#if defined(__aarch64__)
    // Read BEFORE
    uint64_t old_tpidr = 0;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(old_tpidr));
    
    // Write new value
    __asm__ volatile("msr tpidr_el0, %0" : : "r"(tls_ptr));
    
    // Read AFTER to verify
    uint64_t new_tpidr = 0;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(new_tpidr));
    
    // Check the stack guard is readable at offset 40
    uint64_t guard_check = *reinterpret_cast<uint64_t*>(new_tpidr + 40);
    
    fprintf(stderr, "[TLS_DIAG] tls_base=%p tls_ptr=%p\n", tls_base, tls_ptr);
    fprintf(stderr, "[TLS_DIAG] tpidr_el0 BEFORE=0x%llx AFTER=0x%llx\n", 
            (unsigned long long)old_tpidr, (unsigned long long)new_tpidr);
    fprintf(stderr, "[TLS_DIAG] stack_guard@offset40=0x%llx (expect 0x1337BEEFCAFECAFE)\n",
            (unsigned long long)guard_check);
    fprintf(stderr, "[TLS_DIAG] write %s\n", 
            (new_tpidr == (uint64_t)tls_ptr) ? "SUCCESS" : "FAILED");
#endif
}

} // namespace
} // namespace kudroid

namespace kudroid {
bool bionic_handle_tpidr_trap(void* ucontext) {
#if defined(__APPLE__) && defined(__aarch64__)
    if (!ucontext) return false;
    ucontext_t* uc = static_cast<ucontext_t*>(ucontext);
    uint32_t* pc = reinterpret_cast<uint32_t*>(uc->uc_mcontext->__ss.__pc);
    uint32_t inst = *pc;
    
    // Check if it's our BRK #(0x1000 + N) instruction
    // BRK encoding: 1101 0100 001i iiii iiii iiii iii0 0000 -> 0xD4200000 | (imm16 << 5)
    if ((inst & 0xFFE0001F) == 0xD4200000) {
        uint32_t imm16 = (inst >> 5) & 0xFFFF;
        if (imm16 >= 0x1000 && imm16 < 0x1020) { // 0x1000 to 0x101F
            uint32_t reg = imm16 - 0x1000;
            void* tls_base = ::pthread_getspecific(tls_key);
            char* tls_ptr = tls_base ? (static_cast<char*>(tls_base) + 32768) : nullptr;
            
            // Write the TLS pointer into the faulting thread's register state
            uc->uc_mcontext->__ss.__x[reg] = reinterpret_cast<uint64_t>(tls_ptr);
            
            // Advance PC to skip the BRK instruction
            uc->uc_mcontext->__ss.__pc += 4;
            return true;
        }
    }
#endif
    return false;
}
} // namespace kudroid

namespace kudroid {
namespace {

static void* bionic_thread_wrapper(void* rawArgs) {
    BionicThreadArgs* args = static_cast<BionicThreadArgs*>(rawArgs);
    void* (*start_routine)(void*) = args->start_routine;
    void* arg = args->arg;
    delete args;

    ::pthread_once(&tls_key_once, init_tls_key);

    // Allocate 64KB for Android TLS block and set tpidr_el0
    // Darwin uses tpidrro_el0, so tpidr_el0 is free for us!
    void* tls_base = std::aligned_alloc(16, 65536); 
    std::memset(tls_base, 0, 65536);
    ::pthread_setspecific(tls_key, tls_base);

    // Set a dummy stack guard cookie at Slot 5 (offset 40)
    uint64_t* stack_guard_ptr = reinterpret_cast<uint64_t*>(reinterpret_cast<char*>(tls_base) + 32768 + 40);
    *stack_guard_ptr = 0x1337BEEFCAFECAFE;

#if defined(__aarch64__)
    __asm__ volatile("msr tpidr_el0, %0" : : "r"((char*)tls_base + 32768));
#endif

    void* result = start_routine(arg);

    // No need to free(tls_base) here, the destructor will handle it automatically
    // when the thread terminates, even if it terminates via pthread_exit().
    return result;
}

extern "C" int bionic_pthread_create(pthread_t* thread, void* attr, void* (*start_routine)(void*), void* arg) {
    (void)attr;
    BionicThreadArgs* args = new BionicThreadArgs{start_routine, arg};
    return ::pthread_create(thread, nullptr, bionic_thread_wrapper, args);
}

extern "C" int* __error(void);

// ============================================================================
// ALooper — Android event loop (minimal shim for Unity)
// ============================================================================
struct ALooper_shim {
    int dummy;
};

static ALooper_shim g_mainLooper = {0};

extern "C" void* bionic_ALooper_prepare(int opts) {
    (void)opts;
    return &g_mainLooper;
}

extern "C" void* bionic_ALooper_forThread() {
    return &g_mainLooper;
}

extern "C" int bionic_ALooper_pollAll(int timeoutMillis, int* outFd, int* outEvents, void** outData) {
    if (outFd) *outFd = 0;
    if (outEvents) *outEvents = 0;
    if (outData) *outData = nullptr;
    
    if (timeoutMillis > 0) {
        usleep(static_cast<unsigned>(timeoutMillis) * 1000);
    }
    return -3; // ALOOPER_POLL_TIMEOUT
}

extern "C" int bionic_ALooper_pollOnce(int timeoutMillis, int* outFd, int* outEvents, void** outData) {
    return bionic_ALooper_pollAll(timeoutMillis, outFd, outEvents, outData);
}

extern "C" int bionic_ALooper_addFd(void* looper, int fd, int ident, int events, void* callback, void* data) {
    (void)looper; (void)fd; (void)ident; (void)events; (void)callback; (void)data;
    return 1; // success
}

extern "C" int bionic_ALooper_removeFd(void* looper, int fd) {
    (void)looper; (void)fd;
    return 1; // success
}

extern "C" void bionic_ALooper_wake(void* looper) {
    (void)looper;
}

extern "C" void bionic_ALooper_acquire(void* looper) {
    (void)looper;
}

extern "C" void bionic_ALooper_release(void* looper) {
    (void)looper;
}

// ============================================================================
// memalign — aligned memory allocation (CRITICAL for Unity)
// ============================================================================
extern "C" void* bionic_memalign(size_t alignment, size_t size) {
    void* ptr = nullptr;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    if (::posix_memalign(&ptr, alignment, size) != 0) return nullptr;
    return ptr;
}

// ============================================================================
// __system_property_get — Android property system stub
// ============================================================================
extern "C" int bionic_system_property_get(const char* name, char* value) {
    if (!name || !value) return 0;
    // Return empty string for all properties, length 0
    value[0] = '\0';

    // Provide some useful defaults
    if (std::strcmp(name, "ro.build.version.sdk") == 0) {
        std::strcpy(value, "29");
        return 2;
    }
    if (std::strcmp(name, "ro.build.version.release") == 0) {
        std::strcpy(value, "10");
        return 2;
    }
    if (std::strcmp(name, "ro.product.cpu.abi") == 0) {
        std::strcpy(value, "arm64-v8a");
        return 9;
    }
    if (std::strcmp(name, "ro.debuggable") == 0) {
        std::strcpy(value, "0");
        return 1;
    }
    if (std::strcmp(name, "persist.sys.timezone") == 0) {
        std::strcpy(value, "UTC");
        return 3;
    }
    return 0;
}

extern "C" int bionic_system_property_find(const char* name) {
    (void)name;
    return 0; // not found
}

extern "C" void bionic_system_property_read_callback(
    void* pi, void (*callback)(void*, const char*, const char*, unsigned), void* cookie) {
    (void)pi; (void)callback; (void)cookie;
}

// ============================================================================
// dl_iterate_phdr — ELF program header iteration stub
// ============================================================================
extern "C" int bionic_dl_iterate_phdr(
    int (*callback)(void* info, size_t size, void* data), void* data) {
    (void)callback; (void)data;
    // Return 0 = no ELF headers to iterate (safe stub)
    return 0;
}

// ============================================================================
// lseek64 — 64-bit file seek (on Darwin/iOS lseek is already 64-bit)
// ============================================================================
extern "C" off_t bionic_lseek64(int fd, off_t offset, int whence) {
    return ::lseek(fd, offset, whence);
}

// ============================================================================
// __android_log_vprint — variadic log printing
// ============================================================================
extern "C" int bionic_android_log_vprint(int prio, const char* tag, const char* fmt, va_list ap) {
    (void)prio;
    std::fprintf(stderr, "[%s] ", tag ? tag : "unknown");
    std::vfprintf(stderr, fmt, ap);
    std::fprintf(stderr, "\n");
    return 0;
}

extern "C" int bionic_android_log_write(int prio, const char* tag, const char* text) {
    (void)prio;
    std::fprintf(stderr, "[%s] %s\n", tag ? tag : "unknown", text ? text : "");
    return 0;
}

extern "C" int bionic_android_log_buf_write(int bufId, int prio, const char* tag, const char* msg) {
    (void)bufId;
    return bionic_android_log_write(prio, tag, msg);
}

// ============================================================================
// __ctype_get_mb_cur_max — max bytes per multibyte character
// ============================================================================
extern "C" size_t bionic_ctype_get_mb_cur_max() {
    return 4; // UTF-8
}

// ============================================================================
// _ctype_ — character classification table (Bionic-compatible)
// ============================================================================
static const unsigned short g_bionic_ctype_[257] = {
    0, // EOF slot
    // 0x00-0x08: control chars
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    // 0x09: tab (control + blank + space)
    0x20 | 0x01 | 0x40,
    // 0x0A-0x0D: control + space (newline, vtab, formfeed, carriage return)
    0x20 | 0x01, 0x20 | 0x01, 0x20 | 0x01, 0x20 | 0x01,
    // 0x0E-0x1F: control
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    // 0x20: space
    0x01 | 0x40,
    // 0x21-0x2F: punctuation
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    // 0x30-0x39: digits
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    // 0x3A-0x40: punctuation
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    // 0x41-0x46: uppercase hex
    0x02 | 0x80, 0x02 | 0x80, 0x02 | 0x80, 0x02 | 0x80, 0x02 | 0x80, 0x02 | 0x80,
    // 0x47-0x5A: uppercase
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    // 0x5B-0x60: punctuation
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    // 0x61-0x66: lowercase hex
    0x08 | 0x80, 0x08 | 0x80, 0x08 | 0x80, 0x08 | 0x80, 0x08 | 0x80, 0x08 | 0x80,
    // 0x67-0x7A: lowercase
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    // 0x7B-0x7E: punctuation
    0x10, 0x10, 0x10, 0x10,
    // 0x7F: DEL (control)
    0x20,
    // 0x80-0xFF: high bytes (0)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// Global pointer as expected by Android binaries for the data symbol
extern "C" const unsigned short* _ctype_ = &g_bionic_ctype_[1];

// ============================================================================
// pthread_condattr_setclock — set clock for condition variable
// ============================================================================
extern "C" int bionic_pthread_condattr_setclock(void* attr, int clock_id) {
    (void)attr; (void)clock_id;
    return 0; // success, silently ignore (Darwin uses CLOCK_REALTIME always)
}

// ============================================================================
// Google internal stubs (used by Bionic internally, safe to no-op)
// ============================================================================
extern "C" void bionic_google_potentially_blocking_region_begin() {}
extern "C" void bionic_google_potentially_blocking_region_end() {}

// Dummy for __register_atfork
extern "C" int bionic_register_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void), void* dso_handle) {
    (void)dso_handle;
#ifdef __APPLE__
    return pthread_atfork(prepare, parent, child);
#else
    return 0; // iOS does not really support fork(), so just return success
#endif
}

const SymbolEntry kSymbols[] = {
    {"__register_atfork", reinterpret_cast<void*>(&bionic_register_atfork)},
    {"pthread_create", reinterpret_cast<void*>(&bionic_pthread_create)},
    {"pthread_attr_init", reinterpret_cast<void*>(&bionic_pthread_attr_init)},
    {"pthread_attr_destroy", reinterpret_cast<void*>(&bionic_pthread_attr_destroy)},
    {"pthread_attr_setstacksize", reinterpret_cast<void*>(&bionic_pthread_attr_setstacksize)},
    {"pthread_attr_getstack", reinterpret_cast<void*>(&bionic_pthread_attr_getstack)},
    {"pthread_attr_setdetachstate", reinterpret_cast<void*>(&bionic_pthread_attr_setdetachstate)},
    {"pthread_getattr_np", reinterpret_cast<void*>(&bionic_pthread_getattr_np)},
    {"ANativeWindow_fromSurface", reinterpret_cast<void*>(&bionic_ANativeWindow_fromSurface)},
    {"ANativeWindow_getWidth", reinterpret_cast<void*>(&bionic_ANativeWindow_getWidth)},
    {"ANativeWindow_getHeight", reinterpret_cast<void*>(&bionic_ANativeWindow_getHeight)},
    {"ANativeWindow_setBuffersGeometry", reinterpret_cast<void*>(&bionic_ANativeWindow_setBuffersGeometry)},
    {"ANativeWindow_release", reinterpret_cast<void*>(&bionic_ANativeWindow_release)},
    {"ANativeWindow_acquire", reinterpret_cast<void*>(&bionic_ANativeWindow_acquire)},
    {"ASensorManager_getInstance", reinterpret_cast<void*>(&bionic_ASensorManager_getInstance)},
    {"ASensorManager_createEventQueue", reinterpret_cast<void*>(&bionic_ASensorManager_createEventQueue)},
    {"ASensorManager_destroyEventQueue", reinterpret_cast<void*>(&bionic_ASensorManager_destroyEventQueue)},
    {"ASensorManager_getSensorList", reinterpret_cast<void*>(&bionic_ASensorManager_getSensorList)},
    {"ASensorManager_getDefaultSensor", reinterpret_cast<void*>(&bionic_ASensorManager_getDefaultSensor)},
    {"ASensorEventQueue_enableSensor", reinterpret_cast<void*>(&bionic_ASensorEventQueue_enableSensor)},
    {"ASensorEventQueue_disableSensor", reinterpret_cast<void*>(&bionic_ASensorEventQueue_disableSensor)},
    {"ASensorEventQueue_setEventRate", reinterpret_cast<void*>(&bionic_ASensorEventQueue_setEventRate)},
    {"ASensorEventQueue_hasEvents", reinterpret_cast<void*>(&bionic_ASensorEventQueue_hasEvents)},
    {"ASensorEventQueue_getEvents", reinterpret_cast<void*>(&bionic_ASensorEventQueue_getEvents)},
    {"ASensor_getName", reinterpret_cast<void*>(&bionic_ASensor_getName)},
    {"ASensor_getVendor", reinterpret_cast<void*>(&bionic_ASensor_getVendor)},
    {"ASensor_getType", reinterpret_cast<void*>(&bionic_ASensor_getType)},
    {"ASensor_getResolution", reinterpret_cast<void*>(&bionic_ASensor_getResolution)},
    {"ASensor_getMinDelay", reinterpret_cast<void*>(&bionic_ASensor_getMinDelay)},
    {"pthread_condattr_init", reinterpret_cast<void*>(&bionic_pthread_condattr_init)},
    {"pthread_condattr_destroy", reinterpret_cast<void*>(&bionic_pthread_condattr_destroy)},
    {"pthread_mutexattr_init", reinterpret_cast<void*>(&bionic_pthread_mutexattr_init)},
    {"pthread_mutexattr_destroy", reinterpret_cast<void*>(&bionic_pthread_mutexattr_destroy)},
    {"pthread_mutexattr_settype", reinterpret_cast<void*>(&bionic_pthread_mutexattr_settype)},
    {"pthread_mutex_trylock", reinterpret_cast<void*>(&bionic_pthread_mutex_trylock)},
    {"pthread_key_create", reinterpret_cast<void*>(&bionic_pthread_key_create)},
    {"pthread_setspecific", reinterpret_cast<void*>(&bionic_pthread_setspecific)},
    {"pthread_getspecific", reinterpret_cast<void*>(&bionic_pthread_getspecific)},
    {"pthread_key_delete", reinterpret_cast<void*>(&bionic_pthread_key_delete)},
    {"pthread_once", reinterpret_cast<void*>(&bionic_pthread_once)},
    {"sigaction", reinterpret_cast<void*>(&bionic_sigaction)},
    {"sigaltstack", reinterpret_cast<void*>(&bionic_sigaltstack)},
    {"futex", reinterpret_cast<void*>(&bionic_futex)},
    {"__futex", reinterpret_cast<void*>(&bionic_futex)},
    {"futex_time64", reinterpret_cast<void*>(&bionic_futex)},
    {"__futex_time64", reinterpret_cast<void*>(&bionic_futex)},
    {"mremap", reinterpret_cast<void*>(&bionic_mremap)},
    {"gettid", reinterpret_cast<void*>(&bionic_gettid)},
    {"getauxval", reinterpret_cast<void*>(&bionic_getauxval)},
    {"timerfd_create", reinterpret_cast<void*>(&bionic_timerfd_create)},
    {"timerfd_settime", reinterpret_cast<void*>(&bionic_timerfd_settime)},
    {"eventfd", reinterpret_cast<void*>(&bionic_eventfd)},
    {"epoll_create", reinterpret_cast<void*>(&bionic_epoll_create)},
    {"epoll_create1", reinterpret_cast<void*>(&bionic_epoll_create1)},
    {"epoll_ctl", reinterpret_cast<void*>(&bionic_epoll_ctl)},
    {"epoll_wait", reinterpret_cast<void*>(&bionic_epoll_wait)},
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
    {"snprintf", reinterpret_cast<void*>(&snprintf)},
    {"memcpy", reinterpret_cast<void*>(&memcpy)},
    {"dlopen", reinterpret_cast<void*>(&bionic_dlopen)},
    {"dlsym", reinterpret_cast<void*>(&bionic_dlsym)},
    {"dlclose", reinterpret_cast<void*>(&bionic_dlclose)},
    {"dlerror", reinterpret_cast<void*>(&bionic_dlerror)},
    {"malloc", reinterpret_cast<void*>(&bionic_malloc)},
    {"calloc", reinterpret_cast<void*>(&calloc)},
    {"realloc", reinterpret_cast<void*>(&realloc)},
    {"free", reinterpret_cast<void*>(&bionic_free)},
    {"mmap", reinterpret_cast<void*>(&bionic_mmap)},
    {"mmap64", reinterpret_cast<void*>(&bionic_mmap64)},
    {"mprotect", reinterpret_cast<void*>(&bionic_mprotect)},
    {"madvise", reinterpret_cast<void*>(&bionic_madvise)},
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
    {"ioctl", reinterpret_cast<void*>(&bionic_ioctl)},
    {"prctl", reinterpret_cast<void*>(&bionic_prctl)},
    {"getrandom", reinterpret_cast<void*>(&bionic_getrandom)},
    {"clock_gettime", reinterpret_cast<void*>(&::clock_gettime)},
    {"__clock_gettime", reinterpret_cast<void*>(&::clock_gettime)},
    {"clock_gettime64", reinterpret_cast<void*>(&::clock_gettime)},
    {"gettimeofday", reinterpret_cast<void*>(&::gettimeofday)},
    {"ashmem_create_region", reinterpret_cast<void*>(&bionic_ashmem_create_region)},
    {"ashmem_set_prot_region", reinterpret_cast<void*>(&bionic_ashmem_set_prot_region)},
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

    // Exceptions and Unwinding
    {"_Unwind_Resume", reinterpret_cast<void*>(&_Unwind_Resume)},
    {"_Unwind_RaiseException", reinterpret_cast<void*>(&_Unwind_RaiseException)},
    {"_Unwind_DeleteException", reinterpret_cast<void*>(&_Unwind_DeleteException)},
    {"_Unwind_GetLanguageSpecificData", reinterpret_cast<void*>(&_Unwind_GetLanguageSpecificData)},
    {"_Unwind_GetRegionStart", reinterpret_cast<void*>(&_Unwind_GetRegionStart)},
    {"_Unwind_SetGR", reinterpret_cast<void*>(&_Unwind_SetGR)},
    {"_Unwind_SetIP", reinterpret_cast<void*>(&_Unwind_SetIP)},
    {"_Unwind_GetIP", reinterpret_cast<void*>(&_Unwind_GetIP)},
    {"_Unwind_GetGR", reinterpret_cast<void*>(&_Unwind_GetGR)},
    {"_Unwind_Backtrace", reinterpret_cast<void*>(&_Unwind_Backtrace)},
    {"__cxa_allocate_exception", reinterpret_cast<void*>(&abi::__cxa_allocate_exception)},
    {"__cxa_free_exception", reinterpret_cast<void*>(&abi::__cxa_free_exception)},
    {"__cxa_throw", reinterpret_cast<void*>(&abi::__cxa_throw)},
    {"__cxa_begin_catch", reinterpret_cast<void*>(&abi::__cxa_begin_catch)},
    {"__cxa_end_catch", reinterpret_cast<void*>(&abi::__cxa_end_catch)},
    {"__gxx_personality_v0", reinterpret_cast<void*>(&__gxx_personality_v0)},

    // ALooper (Android event loop)
    {"ALooper_prepare", reinterpret_cast<void*>(&bionic_ALooper_prepare)},
    {"ALooper_forThread", reinterpret_cast<void*>(&bionic_ALooper_forThread)},
    {"ALooper_pollAll", reinterpret_cast<void*>(&bionic_ALooper_pollAll)},
    {"ALooper_pollOnce", reinterpret_cast<void*>(&bionic_ALooper_pollOnce)},
    {"ALooper_addFd", reinterpret_cast<void*>(&bionic_ALooper_addFd)},
    {"ALooper_removeFd", reinterpret_cast<void*>(&bionic_ALooper_removeFd)},
    {"ALooper_wake", reinterpret_cast<void*>(&bionic_ALooper_wake)},
    {"ALooper_acquire", reinterpret_cast<void*>(&bionic_ALooper_acquire)},
    {"ALooper_release", reinterpret_cast<void*>(&bionic_ALooper_release)},

    // Memory alignment
    {"memalign", reinterpret_cast<void*>(&bionic_memalign)},

    // Android property system
    {"__system_property_get", reinterpret_cast<void*>(&bionic_system_property_get)},
    {"__system_property_find", reinterpret_cast<void*>(&bionic_system_property_find)},
    {"__system_property_read_callback", reinterpret_cast<void*>(&bionic_system_property_read_callback)},

    // ELF iteration
    {"dl_iterate_phdr", reinterpret_cast<void*>(&bionic_dl_iterate_phdr)},

    // 64-bit file operations
    {"lseek64", reinterpret_cast<void*>(&bionic_lseek64)},

    // Logging
    {"__android_log_vprint", reinterpret_cast<void*>(&bionic_android_log_vprint)},
    {"__android_log_write", reinterpret_cast<void*>(&bionic_android_log_write)},
    {"__android_log_buf_write", reinterpret_cast<void*>(&bionic_android_log_buf_write)},

    // Character classification
    {"__ctype_get_mb_cur_max", reinterpret_cast<void*>(&bionic_ctype_get_mb_cur_max)},
    {"_ctype_", reinterpret_cast<void*>(&_ctype_)},

    // pthread extensions
    {"pthread_condattr_setclock", reinterpret_cast<void*>(&bionic_pthread_condattr_setclock)},

    // Google internal
    {"__google_potentially_blocking_region_begin", reinterpret_cast<void*>(&bionic_google_potentially_blocking_region_begin)},
    {"__google_potentially_blocking_region_end", reinterpret_cast<void*>(&bionic_google_potentially_blocking_region_end)},
};

} // namespace

extern "C" uint64_t kudroid_universal_dummy() {
    trace("WARNING: kudroid_universal_dummy called!");
    return 0;
}

void* resolve_bionic_symbol(const char* name) {
    if (name) {
        for (const auto& symbol : kSymbols) {
            if (strcmp(name, symbol.name) == 0) {
                char traceMessage[256];
                snprintf(traceMessage, sizeof(traceMessage),
                              "bound %s -> %p", name, symbol.address);
                trace(traceMessage);
                return symbol.address;
            }
        }

        // Try host dlsym
        void* host_ptr = ::dlsym(RTLD_DEFAULT, name);
        if (host_ptr) {
            char traceMessage[256];
            snprintf(traceMessage, sizeof(traceMessage),
                          "bound %s -> %p (host)", name, host_ptr);
            trace(traceMessage);
            return host_ptr;
        }

        fprintf(stderr, "Missing Bionic symbol: %s\n", name);
        char traceMessage[256];
        snprintf(traceMessage, sizeof(traceMessage),
                  "missing %s -> universal dummy", name);
        trace(traceMessage);
        return reinterpret_cast<void*>(&kudroid_universal_dummy);
    } else {
        fprintf(stderr, "Missing Bionic symbol: <null>\n");
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
