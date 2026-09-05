#include "kudroid/BionicShim.h"
#include "kudroid/abi/SyscallShim.h"
#include "kudroid/abi/GuestVarargs.h"
#include "kudroid/abi/BlockingWaitRegistry.h"
#include "kudroid/abi/GuestSignals.h"
#include "kudroid/debug/FrameWalk.h"
#include "kudroid/elf_loader.hpp"
#include "kudroid/DeviceProfile.h"
#include "kudroid/platform/BundledFramework.h"
#include "kudroid/platform/GraphicsShim.h"
#include "kudroid/platform/InputShim.h"
#include "kudroid/platform/AudioShim.h"
#include "kudroid/platform/CpuInfo.h"
#include "kudroid/platform/MemoryInfo.h"
#include "kudroid/VFSPathRemapper.h"
#include <filesystem>

#include <cmath>
#include <ctime>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <poll.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/resource.h>
#if defined(__linux__)
#include <sys/inotify.h>
#include <sys/signalfd.h>
#endif
#include <unwind.h>
#include <cxxabi.h>
#include <chrono>
#include <algorithm>
#include <sys/socket.h>
#include <condition_variable>
#include <limits.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/vm_region.h>
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif
#if defined(__APPLE__)
// Direct declaration of libobjc autorelease pool functions to avoid header search path
// issues on certain macOS SDK runners while remaining ABI-compatible.
extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void* pool);
#endif

extern "C" void __gxx_personality_v0();

// Mirror log lines to crash buffer in kudroid_bridge.cpp for pre-crash diagnostics.
extern "C" void kudroid_append_crash_log(const char* text, size_t len);

// Guest symbol lookup hook: queries loaded .so modules via LibraryManager.
// Allows dummy handles (e.g. dlopen('libc.so')) to resolve exported symbols.
extern "C" {
void* (*kudroid_guest_symbol_lookup)(const char* name) = nullptr;

// Per-library handles for guest .so files LibraryManager already mapped.
//
// dlopen of a guest library used to fall through to DUMMY_HANDLE, which makes
// dlsym search every loaded library at once. That is wrong in the way that is
// hardest to see: two guest libraries exporting the same name resolve to whichever
// sorts first, so a caller that deliberately opened one gets the other's function.
// AGDK's GameActivity is the caller that matters — it dlopens the .so named by
// android.app.lib_name and then dlsyms its entry points out of that handle
// specifically.
//
// `open` returns an opaque handle or null when the name is not a guest library;
// `symbol` validates the handle against its own registry, so it is safe to call
// with any pointer and must be consulted BEFORE ::dlsym, which would dereference a
// handle it does not own.
void* (*kudroid_guest_library_open)(const char* filename) = nullptr;
void* (*kudroid_guest_library_symbol)(void* handle, const char* symbol) = nullptr;

// True when `handle` came from kudroid_guest_library_open. Needed because
// ::dlclose on it would hand the host loader a pointer it never issued.
int (*kudroid_guest_library_owns)(void* handle) = nullptr;
}

// Store guest abort message (android_set_abort_message) for crash reporting.
extern "C" void kudroid_store_abort_message(const char* msg);

// JNI & JVM Runtime declarations
extern "C" void* kudroid_jni_get_javavm(void);
extern "C" int JNI_GetCreatedJavaVMs(void** vmBuf, size_t bufLen, size_t* nVMs);
extern "C" int JNI_CreateJavaVM(void** p_vm, void** p_env, void* vm_args);

// Orientation, Haptic & Sensor Bridge declarations
extern "C" void kudroid_vibrate(int intensity);
extern "C" void kudroid_set_requested_orientation(int orientation);
extern "C" int kudroid_get_requested_orientation(void);
extern "C" void kudroid_inject_sensor_event(int sensorType, float x, float y, float z);

extern "C" int kudroid_check_permission(const char* packageName, const char* permissionName);
extern "C" void kudroid_set_group_permission(const char* packageName, const char* groupKey, int granted);
extern "C" int kudroid_is_group_granted(const char* packageName, const char* groupKey);
extern "C" void kudroid_grant_all_permissions(const char* packageName);
extern "C" const char* kudroid_get_app_permissions_json(const char* packageName);
extern "C" void kudroid_set_app_permissions_json(const char* packageName, const char* jsonStr);

// For Bionic pthread emulation
#include <cstdarg>
#include <semaphore.h>
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

// POLLERR/POLLHUP/POLLNVAL are bionic-specific names; define them if the host
// poll.h does not (e.g. glibc uses POLL_ERR/POLL_HUP).
#ifndef POLLERR
#define POLLERR 0x001
#endif
#ifndef POLLHUP
#define POLLHUP 0x002
#endif
#ifndef POLLNVAL
#define POLLNVAL 0x020
#endif

struct android_epoll_event {
    uint32_t events;
    uint64_t data;
} __attribute__((packed));
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <pthread.h>
#if !defined(__APPLE__)
#include <sys/syscall.h>
#endif
#include <unordered_map>
#include <map>
#include <set>
#include <mutex>
#include <shared_mutex>
#include <optional>
#include <atomic>
#include <vector>
#include <string>
#include <array>
#include <memory>
#include <new>
#include <dlfcn.h>
#include <thread>

extern const char* g_kudroid_log_dir_ptr;

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

#if defined(__APPLE__)
extern "C" __attribute__((weak)) void kudroid_remote_log_broadcast(int level, const char* tag, const char* message) {
    (void)level; (void)tag; (void)message;
}
#endif

int logAndroidMessage(int priority, const char* tag, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_logAndroidMutex);
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
    
    // Dump to standard output (Xcode/syslog)
    fprintf(stdout, "[AndroidLog][%s]: %s\n", tag ? tag : "unknown", message.c_str());
    
    // Broadcast via KDB WebSocket if connected
#if defined(__APPLE__)
    if (kudroid_remote_log_broadcast) {
        kudroid_remote_log_broadcast(priority, tag ? tag : "KuDroid", message.c_str());
    }
#endif

    // Also append to a file in Documents for the user to easily read
    if (::g_kudroid_log_dir_ptr && ::g_kudroid_log_dir_ptr[0] != '\0') {
        char log_path[1024];
        snprintf(log_path, sizeof(log_path), "%s/kudroid_android_logs.txt", ::g_kudroid_log_dir_ptr);
        FILE* fp = fopen(log_path, "a");
        if (fp) {
            fprintf(fp, "[%s] %s\n", tag ? tag : "unknown", message.c_str());
            fclose(fp);
        }
    }

    // Mirror into the crash buffer so kudroid_crash.log shows the game's own
    // Provide diagnostic log buffer up to crash event.
    {
        std::string full;
        if (tag) { full += '['; full += tag; full += "] "; }
        full += message;
        full += '\n';
        kudroid_append_crash_log(full.data(), full.size());
    }

    return 0;
}

#if defined(__aarch64__)
extern "C" int kudroid_android_log_print_trampoline();
// snprintf/sprintf handlers live in GuestVarargs.cpp, next to the formatter, so the
// freestanding arm64 test can link them without dragging in this file.
extern "C" int kudroid_snprintf_trampoline();
extern "C" int kudroid_sprintf_trampoline();
extern "C" int kudroid_snprintf_chk_trampoline();
extern "C" int kudroid_sprintf_chk_trampoline();

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


static std::unordered_map<void*, void*> gSyncRegistry;
static std::shared_mutex gSyncRegistryLock;

static inline unsigned long long current_thread_id() {
#if defined(__APPLE__)
    uint64_t tid = 0;
    ::pthread_threadid_np(nullptr, &tid);
    return static_cast<unsigned long long>(tid);
#else
    return static_cast<unsigned long long>(::syscall(SYS_gettid));
#endif
}

static void sync_diag(const char* operation, void* object, void* mutex, int result) {
    static std::atomic<unsigned> emitted{0};
    if (emitted.fetch_add(1, std::memory_order_relaxed) >= 256) return;
    char message[256];
    std::snprintf(message, sizeof(message),
                  "thread-sync op=%s cond=%p mutex=%p tid=%llu result=%d",
                  operation, object, mutex, current_thread_id(), result);
    logAndroidMessage(4, "KuDroidThread", message);
}

// Sync object types.
enum SyncType : int {
    SYNC_MUTEX = 1,
    SYNC_COND = 2,
    SYNC_RWLOCK = 3,
};

// Bionic mutex kinds, as stored in a guest pthread_mutexattr_t and decoded from
// a guest pthread_mutex_t. Values match PTHREAD_MUTEX_{NORMAL,RECURSIVE,ERRORCHECK}.
enum BionicMutexKind : int {
    BIONIC_MUTEX_NORMAL = 0,
    BIONIC_MUTEX_RECURSIVE = 1,
    BIONIC_MUTEX_ERRORCHECK = 2,
};

// Bionic keeps a mutex's kind in bits 14-15 of the first 32-bit word of
// pthread_mutex_t. That word is the ONLY record of the kind for a mutex built by
// PTHREAD_RECURSIVE_MUTEX_INITIALIZER, because a statically initialised mutex
// never calls pthread_mutex_init and so never reaches this shim before its first
// lock. Handing such a guest a default (non-recursive) host mutex turns its
// second re-entrant lock into a permanent self-deadlock inside
// pthread_mutex_lock — which from the outside is indistinguishable from a native
// method that simply never returns.
static constexpr uint32_t kBionicMutexTypeShift = 14;
static constexpr uint32_t kBionicMutexStaticNormal =
    static_cast<uint32_t>(BIONIC_MUTEX_NORMAL) << kBionicMutexTypeShift;
static constexpr uint32_t kBionicMutexStaticRecursive =
    static_cast<uint32_t>(BIONIC_MUTEX_RECURSIVE) << kBionicMutexTypeShift;
static constexpr uint32_t kBionicMutexStaticErrorcheck =
    static_cast<uint32_t>(BIONIC_MUTEX_ERRORCHECK) << kBionicMutexTypeShift;

// Deliberately an exact match against a pristine static initializer rather than a
// mask of bits 14-15. A mutex the guest initialised at runtime has a control word
// this shim never wrote, so its contents are unspecified; masking would read
// uninitialised memory as "recursive" and silently hide real double-lock bugs.
// An untouched static initializer is exactly one of these three values.
static inline int bionic_static_mutex_kind(const void* guest_mutex) {
    if (!guest_mutex) return BIONIC_MUTEX_NORMAL;
    uint32_t word = 0;
    std::memcpy(&word, guest_mutex, sizeof(word));
    if (word == kBionicMutexStaticRecursive) return BIONIC_MUTEX_RECURSIVE;
    if (word == kBionicMutexStaticErrorcheck) return BIONIC_MUTEX_ERRORCHECK;
    (void)kBionicMutexStaticNormal;
    return BIONIC_MUTEX_NORMAL;
}

// A guest mutex maps to a host mutex plus the owner this shim recorded for it.
//
// The owner is tracked because the host cannot answer "did THIS thread already
// lock this?" for a default mutex: pthread_mutex_trylock returns EBUSY whether the
// holder is this thread or another one. Only an ERRORCHECK mutex reports EDEADLK,
// and promoting every guest mutex to ERRORCHECK is not safe — bionic tolerates an
// unlock from a thread that is not the owner on a NORMAL mutex, and some guest
// code relies on that hand-off pattern, which ERRORCHECK would start rejecting
// with EPERM.
struct HostMutex {
    pthread_mutex_t mutex;
    // Only meaningful when track_owner is set. Relaxed ordering is enough: the
    // value is only ever compared against the current thread's own id, and a
    // thread's own writes are always visible to itself.
    std::atomic<unsigned long long> owner{0};
    bool track_owner = false;
};

static inline void* create_mutex_obj(int kind) {
    auto* host = new (std::nothrow) HostMutex();
    if (!host) return nullptr;
    int rc;
    if (kind == BIONIC_MUTEX_RECURSIVE || kind == BIONIC_MUTEX_ERRORCHECK) {
        // Both kinds detect owner re-entry in the host: RECURSIVE by allowing it,
        // ERRORCHECK by returning EDEADLK. Neither needs bookkeeping here.
        pthread_mutexattr_t ma;
        ::pthread_mutexattr_init(&ma);
        ::pthread_mutexattr_settype(&ma, kind == BIONIC_MUTEX_RECURSIVE
                                             ? PTHREAD_MUTEX_RECURSIVE
                                             : PTHREAD_MUTEX_ERRORCHECK);
        rc = ::pthread_mutex_init(&host->mutex, &ma);
        ::pthread_mutexattr_destroy(&ma);
        host->track_owner = false;
    } else {
        rc = ::pthread_mutex_init(&host->mutex, nullptr);
        host->track_owner = true;
    }
    if (rc != 0) {
        delete host;
        return nullptr;
    }
    return host;
}

static inline void* create_sync_obj(int type) {
    if (type == SYNC_MUTEX) {
        return create_mutex_obj(BIONIC_MUTEX_NORMAL);
    } else if (type == SYNC_COND) {
        auto* hostCond = static_cast<pthread_cond_t*>(std::malloc(sizeof(pthread_cond_t)));
        if (!hostCond) return nullptr;
        if (::pthread_cond_init(hostCond, nullptr) != 0) {
            std::free(hostCond);
            return nullptr;
        }
        return hostCond;
    } else if (type == SYNC_RWLOCK) {
        auto* hostRwlock = static_cast<pthread_rwlock_t*>(std::malloc(sizeof(pthread_rwlock_t)));
        if (!hostRwlock) return nullptr;
        if (::pthread_rwlock_init(hostRwlock, nullptr) != 0) {
            std::free(hostRwlock);
            return nullptr;
        }
        return hostRwlock;
    }
    return nullptr;
}

static inline void destroy_sync_obj(void* host_obj, int type) {
    if (!host_obj) return;
    if (type == SYNC_MUTEX) {
        auto* host = static_cast<HostMutex*>(host_obj);
        ::pthread_mutex_destroy(&host->mutex);
        delete host;
        return;
    }
    if (type == SYNC_COND) ::pthread_cond_destroy(static_cast<pthread_cond_t*>(host_obj));
    else if (type == SYNC_RWLOCK) ::pthread_rwlock_destroy(static_cast<pthread_rwlock_t*>(host_obj));
    std::free(host_obj);
}

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

    // A mutex reaching this point was never passed to pthread_mutex_init, so it is
    // a static initializer and its control word states the kind it must have.
    void* host_obj = type == SYNC_MUTEX
                         ? create_mutex_obj(bionic_static_mutex_kind(guest_ptr))
                         : create_sync_obj(type);
    if (!host_obj) return nullptr;

    gSyncRegistry[guest_ptr] = host_obj;
    return host_obj;
}

// Replace any existing mapping for guest_ptr, destroying the object it displaces.
// A guest re-initialising a sync object is legal, and leaking the previous host
// object leaks its kernel resources too, not just the malloc.
static inline void register_sync(void* guest_ptr, void* host_obj, int type) {
    std::unique_lock<std::shared_mutex> lock(gSyncRegistryLock);
    auto it = gSyncRegistry.find(guest_ptr);
    if (it != gSyncRegistry.end()) {
        destroy_sync_obj(it->second, type);
        it->second = host_obj;
        return;
    }
    gSyncRegistry[guest_ptr] = host_obj;
}

static inline void destroy_sync(void* guest_ptr, int type) {
    if (!guest_ptr) return;
    
    std::unique_lock<std::shared_mutex> lock(gSyncRegistryLock);
    auto it = gSyncRegistry.find(guest_ptr);
    if (it != gSyncRegistry.end()) {
        destroy_sync_obj(it->second, type);
        gSyncRegistry.erase(it);
    }
}

extern "C" int bionic_pthread_mutex_init(void* guestMutex, const void* attr) {
    if (!guestMutex) return EINVAL;

    // Bionic pthread_mutexattr_t: mutex kind (RECURSIVE=1, ERRORCHECK=2) in the
    // low 2 bits.
    int kind = BIONIC_MUTEX_NORMAL;
    if (attr) kind = static_cast<int>((*static_cast<const uint32_t*>(attr)) & 0x3u);

    void* hostMutex = create_mutex_obj(kind);
    if (!hostMutex) return EAGAIN;

    register_sync(guestMutex, hostMutex, SYNC_MUTEX);
    return 0;
}

extern "C" int bionic_pthread_cond_init(void* cond, const void* attr) {
    (void)attr;
    if (!cond) return EINVAL;
    void* hostCond = create_sync_obj(SYNC_COND);
    if (!hostCond) return EAGAIN;

    register_sync(cond, hostCond, SYNC_COND);
    return 0;
}

extern "C" int bionic_pthread_rwlock_init(void* rwlock, const void* attr) {
    (void)attr;
    if (!rwlock) return EINVAL;
    void* hostRwlock = create_sync_obj(SYNC_RWLOCK);
    if (!hostRwlock) return EAGAIN;

    register_sync(rwlock, hostRwlock, SYNC_RWLOCK);
    return 0;
}



// Mutex lock/unlock are far too hot to log per call. What is worth reporting is a
// lock that cannot succeed: a guest thread stuck in pthread_mutex_lock produces no
// output of its own, so the only symptom is a native method that never returns.
static void mutex_deadlock_diag(void* guestMutex) {
    static std::atomic<unsigned> emitted{0};
    if (emitted.fetch_add(1, std::memory_order_relaxed) >= 64) return;
    char message[256];
    std::snprintf(message, sizeof(message),
                  "mutex-self-deadlock guest=%p tid=%llu -> EDEADLK (would block forever)",
                  guestMutex, current_thread_id());
    logAndroidMessage(6, "KuDroidThread", message);
}

static inline HostMutex* host_mutex_for(void* guestMutex) {
    return static_cast<HostMutex*>(get_or_init_sync(guestMutex, SYNC_MUTEX));
}

// Do not trace mutex lock/unlock due to high frequency. Trace reserved for anomalous events.
extern "C" int bionic_pthread_mutex_lock(void* guestMutex) {
    HostMutex* host = host_mutex_for(guestMutex);
    if (!host) return EINVAL;

    if (host->track_owner) {
        // A NORMAL mutex re-locked by its own holder is undefined behaviour in
        // POSIX and an unrecoverable hang here: nothing will ever unlock it,
        // because the only thread that could is the one about to block. Report
        // EDEADLK — the guest's error path runs and the thread stays alive.
        if (host->owner.load(std::memory_order_relaxed) == current_thread_id()) {
            mutex_deadlock_diag(guestMutex);
            return EDEADLK;
        }
    }

    const int rc = [&] {
        // Tracked: a mutex whose holder never releases it parks this thread forever,
        // and nothing else in the log would say so.
        const BlockingWaitScope tracked(WaitKind::kMutex, guestMutex,
                                        guest_return_address(6));
        return ::pthread_mutex_lock(&host->mutex);
    }();
    if (rc == 0 && host->track_owner) {
        host->owner.store(current_thread_id(), std::memory_order_relaxed);
    } else if (rc == EDEADLK) {
        // An ERRORCHECK mutex reaches this instead of the check above.
        mutex_deadlock_diag(guestMutex);
    }
    return rc;
}

extern "C" int bionic_pthread_mutex_unlock(void* guestMutex) {
    HostMutex* host = host_mutex_for(guestMutex);
    if (!host) return EINVAL;
    // Clear the owner BEFORE unlocking: afterwards another thread may already hold
    // the mutex, and clearing then would erase its ownership instead of ours.
    if (host->track_owner &&
        host->owner.load(std::memory_order_relaxed) == current_thread_id()) {
        host->owner.store(0, std::memory_order_relaxed);
    }
    const int rc = ::pthread_mutex_unlock(&host->mutex);
    return rc;
}

extern "C" int bionic_pthread_mutex_destroy(void* guestMutex) {
    destroy_sync(guestMutex, SYNC_MUTEX);
    return 0;
}

// --- Cond and Rwlock Wrappers ---
extern "C" int bionic_pthread_cond_destroy(void* cond) {
    destroy_sync(cond, SYNC_COND);
    return 0;
}
extern "C" int bionic_pthread_cond_wait(void* cond, void* mutex) {
    pthread_cond_t* hostCond = static_cast<pthread_cond_t*>(get_or_init_sync(cond, SYNC_COND));
    HostMutex* host = host_mutex_for(mutex);
    if (!hostCond || !host) return EINVAL;
    // pthread_cond_wait releases the mutex while blocked, so this thread stops
    // being the owner for the duration and becomes it again on return.
    const bool track = host->track_owner;
    if (track) host->owner.store(0, std::memory_order_relaxed);
    sync_diag("cond-wait-enter", cond, mutex, 0);
    const int result = [&] {
        const BlockingWaitScope tracked(WaitKind::kCondition, cond,
                                        guest_return_address(6));
        return ::pthread_cond_wait(hostCond, &host->mutex);
    }();
    sync_diag("cond-wait-return", cond, mutex, result);
    if (track && result == 0) host->owner.store(current_thread_id(), std::memory_order_relaxed);
    return result;
}
extern "C" int bionic_pthread_cond_timedwait(void* cond, void* mutex, const struct timespec* abstime) {
    pthread_cond_t* hostCond = static_cast<pthread_cond_t*>(get_or_init_sync(cond, SYNC_COND));
    HostMutex* host = host_mutex_for(mutex);
    if (!hostCond || !host) return EINVAL;
    // TEMP DIAGNOSTIC: guest-vs-host clock domain for absolute timeouts.
    // A mismatch makes every timed wait fire instantly or sleep forever.
    {
        static std::atomic<int> s_logged{0};
        struct timespec rt{}, mo{};
        clock_gettime(CLOCK_REALTIME, &rt);
        clock_gettime(CLOCK_MONOTONIC, &mo);
        const long long ab = abstime != nullptr
                                 ? static_cast<long long>(abstime->tv_sec)
                                 : -1;
        const bool past_realtime = abstime != nullptr && ab < (long long)rt.tv_sec;
        const int n = s_logged.load();
        if (n < 5 || (past_realtime && n < 25)) {
            ++s_logged;
            std::fprintf(stderr,
                         "[KuDroidSync] timedwait abstime=%lld rt_now=%lld mo_now=%lld %s\n",
                         ab, (long long)rt.tv_sec, (long long)mo.tv_sec,
                         past_realtime ? "PAST-REALTIME-INSTANT-TIMEOUT" : "ok");
        }
    }
    const bool track = host->track_owner;
    if (track) host->owner.store(0, std::memory_order_relaxed);
    sync_diag("cond-timedwait-enter", cond, mutex, 0);
    const int result = [&] {
        const BlockingWaitScope tracked(WaitKind::kConditionTimed, cond,
                                        guest_return_address(6));
        return ::pthread_cond_timedwait(hostCond, &host->mutex, abstime);
    }();
    sync_diag("cond-timedwait-return", cond, mutex, result);
    // The mutex is reacquired on timeout too, so ownership is restored either way.
    if (track) host->owner.store(current_thread_id(), std::memory_order_relaxed);
    return result;
}
extern "C" int bionic_pthread_cond_signal(void* cond) {
    pthread_cond_t* hostCond = static_cast<pthread_cond_t*>(get_or_init_sync(cond, SYNC_COND));
    const int result = hostCond ? ::pthread_cond_signal(hostCond) : -1;
    sync_diag("cond-signal", cond, nullptr, result);
    return result;
}
extern "C" int bionic_pthread_cond_broadcast(void* cond) {
    pthread_cond_t* hostCond = static_cast<pthread_cond_t*>(get_or_init_sync(cond, SYNC_COND));
    const int result = hostCond ? ::pthread_cond_broadcast(hostCond) : -1;
    sync_diag("cond-broadcast", cond, nullptr, result);
    return result;
}

extern "C" int bionic_pthread_rwlock_destroy(void* rwlock) {
    destroy_sync(rwlock, SYNC_RWLOCK);
    return 0;
}
extern "C" int bionic_pthread_rwlock_rdlock(void* rwlock) {
    pthread_rwlock_t* hostRwlock = static_cast<pthread_rwlock_t*>(get_or_init_sync(rwlock, SYNC_RWLOCK));
    if (!hostRwlock) return -1;
    const BlockingWaitScope tracked(WaitKind::kRwlockRead, rwlock, guest_return_address(6));
    return ::pthread_rwlock_rdlock(hostRwlock);
}
extern "C" int bionic_pthread_rwlock_wrlock(void* rwlock) {
    pthread_rwlock_t* hostRwlock = static_cast<pthread_rwlock_t*>(get_or_init_sync(rwlock, SYNC_RWLOCK));
    if (!hostRwlock) return -1;
    // A write lock waits for every reader to leave, so one reader that never
    // releases parks this thread for good.
    const BlockingWaitScope tracked(WaitKind::kRwlockWrite, rwlock, guest_return_address(6));
    return ::pthread_rwlock_wrlock(hostRwlock);
}
extern "C" int bionic_pthread_rwlock_unlock(void* rwlock) {
    pthread_rwlock_t* hostRwlock = static_cast<pthread_rwlock_t*>(get_or_init_sync(rwlock, SYNC_RWLOCK));
    return hostRwlock ? ::pthread_rwlock_unlock(hostRwlock) : -1;
}

extern "C" void bionic_stack_chk_fail() {
    fprintf(stderr, "[BionicShim] FATAL: stack check failed (__stack_chk_fail)\n");
    kudroid_store_abort_message("Bionic shim: stack check failed (__stack_chk_fail)");
    std::abort();
}



// --- Pthread Overrides ---
extern "C" int vfs_fstat(int fd, void* info);
extern "C" int vfs_fstat64(int fd, void* info);

// Bionic pthread_attr_t (ARM64) — field layout matches bionic bits/pthread_types.h.
struct BionicPthreadAttr {
    uint32_t flags;        // bit 0 = PTHREAD_ATTR_FLAG_DETACHED
    uint32_t pad0;
    void* stack_base;
    size_t stack_size;
    size_t guard_size;
    int32_t sched_policy;
    int32_t sched_priority;
};

// Real implementation: initializes stack_size to prevent EINVAL during thread creation.
extern "C" int bionic_pthread_attr_init(void* attr) {
    auto* a = static_cast<BionicPthreadAttr*>(attr);
    if (!a) return -1;
    std::memset(a, 0, sizeof(BionicPthreadAttr));
    a->guard_size = 4096;   // default bionic
    a->sched_policy = -1;
    a->sched_priority = -1;
    return 0;
}
extern "C" int bionic_pthread_attr_destroy(void* attr) { (void)attr; return 0; }
extern "C" int bionic_pthread_attr_setstacksize(void* attr, size_t stacksize) {
    auto* a = static_cast<BionicPthreadAttr*>(attr);
    if (!a) return -1;
    a->stack_size = stacksize;
    return 0;
}
extern "C" int bionic_pthread_attr_getstack(void* attr, void** stackaddr, size_t* stacksize) {
    auto* a = static_cast<BionicPthreadAttr*>(attr);
    if (!a) return -1;
    // Populate output fields to return valid stackaddr and stacksize.
    if (stackaddr) *stackaddr = a->stack_base;
    if (stacksize) *stacksize = a->stack_size;
    return 0;
}
extern "C" int bionic_pthread_attr_setdetachstate(void* attr, int state) {
    auto* a = static_cast<BionicPthreadAttr*>(attr);
    if (!a) return -1;
    if (state == 1) a->flags |= 0x1;      // PTHREAD_CREATE_DETACHED
    else a->flags &= ~0x1;
    return 0;
}
extern "C" int bionic_pthread_getattr_np(pthread_t thread, void* attr) {
    auto* a = static_cast<BionicPthreadAttr*>(attr);
    if (!a) return -1;
    bionic_pthread_attr_init(attr);
    // stack_base is the LOWEST address of the stack, because that is what bionic means
    // by it: bionic's own pthread_attr_getstack hands this field back verbatim and every
    // guest computes the far end as stack_base + stack_size.
    //
    // Darwin's pthread_get_stackaddr_np answers the opposite question. It returns the
    // HIGHEST address — the stack grows down from there — so storing it here described a
    // region [top, top + size): entirely above the real stack, not one byte of it inside.
    // A guest that asks for its own bounds and then walks them (a GC scanning roots, a
    // runtime checking for overflow, an unwinder validating a frame pointer) reads or
    // writes past the top of its stack, and on Darwin that lands in the _pthread struct
    // or the guard page rather than anywhere it may touch.
    //
    // BlockingWaitRegistry does this same conversion for its own frame walk and gets it
    // right; the two disagreed about the same host API, which is how this survived.
#ifdef __APPLE__
    void* const stack_top = ::pthread_get_stackaddr_np(thread);
    const size_t ssize = ::pthread_get_stacksize_np(thread);
    if (ssize > 0) a->stack_size = ssize;
    if (stack_top != nullptr && ssize > 0) {
        a->stack_base = static_cast<char*>(stack_top) - ssize;
    }
#else
    pthread_attr_t hostAttr;
    if (::pthread_getattr_np(thread, &hostAttr) == 0) {
        void* saddr = nullptr;
        size_t ssize = 0;
        if (::pthread_attr_getstack(&hostAttr, &saddr, &ssize) == 0) {
            a->stack_base = saddr;
            a->stack_size = ssize;
        }
        ::pthread_attr_destroy(&hostAttr);
    }
#endif
    return 0;
}

} // namespace
} // namespace kudroid

namespace kudroid {
namespace {

extern "C" int bionic_ioctl(int fd, unsigned long request, ...) {
    (void)fd; (void)request;
    // Unsupported ioctl: return explicit ENOTTY instead of false 0 success.
    errno = ENOTTY;
    return -1;
}

#define PR_SET_NAME 15
#define PR_GET_NAME 16
#define PR_SET_VMA  0x53564d41

extern "C" int bionic_prctl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    if (option == PR_SET_NAME) {
        const int r =
#ifdef __APPLE__
            pthread_setname_np(reinterpret_cast<const char*>(arg2));
#else
            pthread_setname_np(pthread_self(), reinterpret_cast<const char*>(arg2));
#endif
        logAndroidMessage(4, "KuDroidSyscall", "prctl(PR_SET_NAME, \"" +
                          std::string(arg2 ? reinterpret_cast<const char*>(arg2) : "<null>") +
                          "\") -> " + (r == 0 ? "0" : std::to_string(r) + " errno=" + std::to_string(errno)));
        return r;
    } else if (option == PR_GET_NAME) {
        if (arg2) {
#ifdef __APPLE__
            pthread_getname_np(pthread_self(), reinterpret_cast<char*>(arg2), 16);
#else
            pthread_getname_np(pthread_self(), reinterpret_cast<char*>(arg2), 16);
#endif
        }
        return 0;
    } else if (option == PR_SET_VMA) {
        // Just fake it for PR_SET_VMA_ANON_NAME to prevent crashes
        logAndroidMessage(4, "KuDroidSyscall", "prctl(PR_SET_VMA) faked -> 0");
        return 0;
    }
    logAndroidMessage(4, "KuDroidSyscall", "prctl(option=0x" +
                      std::to_string((unsigned)option) + ") faked -> 0");
    return 0;
}

struct bionic_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

extern "C" int bionic_uname(struct bionic_utsname* buf) {
    if (!buf) return -1;
    strncpy(buf->sysname, "Linux", sizeof(buf->sysname) - 1);
    strncpy(buf->nodename, "localhost", sizeof(buf->nodename) - 1);
    strncpy(buf->release, "5.10.0-kudroid", sizeof(buf->release) - 1);
    strncpy(buf->version, "#1 SMP PREEMPT 2026", sizeof(buf->version) - 1);
    strncpy(buf->machine, "aarch64", sizeof(buf->machine) - 1);
    strncpy(buf->domainname, "(none)", sizeof(buf->domainname) - 1);
    return 0;
}

static int translate_linux_open_flags(int flags) {
#if defined(__APPLE__)
    int host_flags = 0;
    int acc = flags & 3;
    if (acc == 0) host_flags |= O_RDONLY;
    else if (acc == 1) host_flags |= O_WRONLY;
    else if (acc == 2) host_flags |= O_RDWR;

    if (flags & 0x40) host_flags |= O_CREAT;
    if (flags & 0x80) host_flags |= O_EXCL;
    if (flags & 0x100) host_flags |= O_NOCTTY;
    if (flags & 0x200) host_flags |= O_TRUNC;
    if (flags & 0x400) host_flags |= O_APPEND;
    if (flags & 0x800) host_flags |= O_NONBLOCK;
#if defined(O_CLOEXEC)
    if (flags & 0x80000) host_flags |= O_CLOEXEC;
#endif
    return host_flags;
#else
    return flags;
#endif
}

static int translate_linux_dirfd(int dirfd) {
#if defined(__APPLE__)
    if (dirfd == -100) { // Linux AT_FDCWD
        return AT_FDCWD; // Darwin AT_FDCWD (-2)
    }
#endif
    return dirfd;
}

// True when an *at() call's path should go through the remapper.
//
// A relative path is resolved by the kernel against `dirfd`, so remapping it is wrong
// twice over: remap() would treat it as a stray relative path and root it under
// data/local/tmp, and the kernel would then resolve that already-absolute result
// against dirfd anyway. The guest's intent — "this name, inside the directory I already
// opened" — is preserved by leaving it alone, because the fd it names was itself
// obtained through a remapped open.
//
// AT_FDCWD is the exception: there is no directory fd, so a relative path is relative to
// the process working directory, and remap() rooting it inside the VFS is the correct
// interpretation of what the guest asked for.
static bool at_path_needs_remap(int dirfd, const char* pathname) {
    if (pathname == nullptr || *pathname == '\0') return false;
    if (pathname[0] == '/') return true;
    return translate_linux_dirfd(dirfd) == AT_FDCWD;
}

extern "C" int bionic_openat(int dirfd, const char* pathname, int flags, mode_t mode) {
    if (!pathname) return -1;
    const int host_dirfd = translate_linux_dirfd(dirfd);
    const bool remapPath = at_path_needs_remap(dirfd, pathname);
    const std::string remapped =
        remapPath ? kudroid::VFSPathRemapper::getInstance().remap(pathname)
                  : std::string(pathname);
    const int host_flags = translate_linux_open_flags(flags);
    if (host_flags & O_CREAT) {
        std::error_code ec;
        // Only meaningful for an absolute result; for a path relative to dirfd the
        // parent is the directory the fd already refers to, which exists by definition.
        if (remapPath) {
            std::filesystem::create_directories(std::filesystem::path(remapped).parent_path(), ec);
        }
    }
    return ::openat(host_dirfd, remapped.c_str(), host_flags, mode);
}

struct bionic_stat64 {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint64_t __pad1;
    int64_t  st_size;
    int32_t  st_blksize;
    int32_t  __pad2;
    int64_t  st_blocks;
    int64_t  __st_atime_sec;
    uint64_t __st_atime_nsec;
    int64_t  __st_mtime_sec;
    uint64_t __st_mtime_nsec;
    int64_t  __st_ctime_sec;
    uint64_t __st_ctime_nsec;
    uint32_t __unused4;
    uint32_t __unused5;
};

extern "C" int bionic_newfstatat(int dirfd, const char* pathname, struct bionic_stat64* statbuf, int flags) {
    if (!statbuf) return -1;
    struct stat host_st;
    memset(&host_st, 0, sizeof(host_st));
    int res = 0;
    const int host_dirfd = translate_linux_dirfd(dirfd);
    if (pathname && *pathname) {
        const std::string remapped =
            at_path_needs_remap(dirfd, pathname)
                ? kudroid::VFSPathRemapper::getInstance().remap(pathname)
                : std::string(pathname);
        res = ::fstatat(host_dirfd, remapped.c_str(), &host_st, flags);
    } else {
        res = ::fstat(dirfd, &host_st);
    }
    if (res != 0) return res;

    memset(statbuf, 0, sizeof(*statbuf));
    statbuf->st_dev = static_cast<uint64_t>(host_st.st_dev);
    statbuf->st_ino = static_cast<uint64_t>(host_st.st_ino);
    statbuf->st_mode = static_cast<uint32_t>(host_st.st_mode);
    statbuf->st_nlink = static_cast<uint32_t>(host_st.st_nlink);
    statbuf->st_uid = static_cast<uint32_t>(host_st.st_uid);
    statbuf->st_gid = static_cast<uint32_t>(host_st.st_gid);
    statbuf->st_rdev = static_cast<uint64_t>(host_st.st_rdev);
    statbuf->st_size = static_cast<int64_t>(host_st.st_size);
    statbuf->st_blksize = static_cast<int32_t>(host_st.st_blksize);
    statbuf->st_blocks = static_cast<int64_t>(host_st.st_blocks);
#if defined(__APPLE__)
    statbuf->__st_atime_sec = host_st.st_atimespec.tv_sec;
    statbuf->__st_atime_nsec = host_st.st_atimespec.tv_nsec;
    statbuf->__st_mtime_sec = host_st.st_mtimespec.tv_sec;
    statbuf->__st_mtime_nsec = host_st.st_mtimespec.tv_nsec;
    statbuf->__st_ctime_sec = host_st.st_ctimespec.tv_sec;
    statbuf->__st_ctime_nsec = host_st.st_ctimespec.tv_nsec;
#else
    statbuf->__st_atime_sec = host_st.st_atim.tv_sec;
    statbuf->__st_atime_nsec = host_st.st_atim.tv_nsec;
    statbuf->__st_mtime_sec = host_st.st_mtim.tv_sec;
    statbuf->__st_mtime_nsec = host_st.st_mtim.tv_nsec;
    statbuf->__st_ctime_sec = host_st.st_ctim.tv_sec;
    statbuf->__st_ctime_nsec = host_st.st_ctim.tv_nsec;
#endif
    return 0;
}

// bionic's struct statfs for arm64. Darwin's is a different shape and a different size,
// so forwarding the guest's buffer to ::statfs would have it read f_bsize out of what is
// actually f_type, and f_blocks out of padding.
//
// Apps ask this before writing anything substantial — Minecraft checks free space before
// downloading a world, installers check before unpacking — and a garbage answer reads as
// "disk full" or, worse, as a huge amount of free space followed by a failed write.
struct bionic_statfs64 {
    uint64_t f_type;
    uint64_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    struct { int32_t val[2]; } f_fsid;
    uint64_t f_namelen;
    uint64_t f_frsize;
    uint64_t f_flags;
    uint64_t f_spare[4];
};

static int fill_bionic_statfs(const struct statvfs& src, struct bionic_statfs64* dst) {
    memset(dst, 0, sizeof(*dst));
    // EXT4_SUPER_MAGIC. The value matters: apps branch on f_type to decide whether a
    // path is on external storage, and an unrecognised filesystem is treated as
    // read-only by some of them.
    dst->f_type = 0xEF53u;
    dst->f_bsize = src.f_bsize;
    dst->f_frsize = src.f_frsize != 0 ? src.f_frsize : src.f_bsize;
    dst->f_blocks = src.f_blocks;
    dst->f_bfree = src.f_bfree;
    dst->f_bavail = src.f_bavail;
    dst->f_files = src.f_files;
    dst->f_ffree = src.f_ffree;
    dst->f_namelen = src.f_namemax;
    dst->f_flags = src.f_flag;
    return 0;
}

extern "C" int bionic_statfs(const char* path, struct bionic_statfs64* buf) {
    if (!path || !buf) {
        errno = EFAULT;
        return -1;
    }
    const std::string remapped = kudroid::VFSPathRemapper::getInstance().remap(path);
    struct statvfs host {};
    if (::statvfs(remapped.c_str(), &host) != 0) return -1;
    return fill_bionic_statfs(host, buf);
}

extern "C" int bionic_fstatfs(int fd, struct bionic_statfs64* buf) {
    if (!buf) {
        errno = EFAULT;
        return -1;
    }
    struct statvfs host {};
    if (::fstatvfs(fd, &host) != 0) return -1;
    return fill_bionic_statfs(host, buf);
}

// Check in bionic_mmap: ashmem fd may only map with granted prot.
static bool ashmem_prot_allows(int fd, int prot);
// Fake ashmem fd (iOS fallback): return granted region or nullptr when not a fake fd.
extern "C" void* bionic_ashmem_mmap_fd(int fd, size_t length);

// Memory mapping wrappers to strip Linux specific flags
extern "C" void* bionic_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    // ashmem fd: only mappable with granted prot.
    if (!ashmem_prot_allows(fd, prot)) {
        errno = EACCES;
        return MAP_FAILED;
    }
    if (prot & PROT_EXEC) {
        logAndroidMessage(4, "KuDroidSyscall", "bionic_mmap: allocating executable memory (JIT/library), length=" + std::to_string(length));
    }
    logAndroidMessage(3, "KuDroidSyscall", "mmap(addr=" + (addr ? std::to_string((uintptr_t)addr) : std::string("NULL")) +
                      ", len=" + std::to_string(length) + ", prot=0x" +
                      [](int p){ char b[16]; snprintf(b, sizeof(b), "%x", p); return std::string(b); }(prot) +
                      ", flags=0x" +
                      [](int f){ char b[16]; snprintf(b, sizeof(b), "%x", f); return std::string(b); }(flags) +
                      ", fd=" + std::to_string(fd) + ")");

    // ashmem fake fd (iOS fallback): return granted region; no flag translation needed.
    if (void* region = bionic_ashmem_mmap_fd(fd, length)) {
        logAndroidMessage(3, "KuDroidSyscall", "mmap -> ashmem region " +
                          std::to_string(reinterpret_cast<uintptr_t>(region)) + " (fake fd)");
        return region;
    }

#ifdef __APPLE__
    // Android/Linux and Darwin use different numeric values for mmap flags.
    // Translate them explicitly instead of forwarding the raw Linux bits.
    // Linux: MAP_SHARED=0x01 MAP_PRIVATE=0x02 MAP_FIXED=0x10 MAP_ANONYMOUS=0x20
    constexpr int LINUX_MAP_SHARED    = 0x01;
    constexpr int LINUX_MAP_PRIVATE   = 0x02;
    constexpr int LINUX_MAP_FIXED     = 0x10;
    constexpr int LINUX_MAP_ANONYMOUS = 0x20;
    constexpr int LINUX_MAP_SHARED_VALIDATE = 0x03;

    // Flags that only exist on Linux; forwarding them raw to Darwin causes
    // EINVAL (e.g. MAP_NORESERVE=0x4000 collides with a reserved Darwin bit)
    // or misparsing. Strip them before calling host mmap.
    constexpr int LINUX_ONLY_MAP_FLAGS =
        0x0100    | // MAP_GROWSDOWN
        0x0800    | // MAP_DENYWRITE
        0x1000    | // MAP_EXECUTABLE (collides with Darwin MAP_ANON bit)
        0x4000    | // MAP_NORESERVE
        0x8000    | // MAP_POPULATE
        0x10000   | // MAP_NONBLOCK
        0x20000   | // MAP_STACK
        0x40000   | // MAP_HUGETLB
        0x80000   | // MAP_SYNC
        0x100000  | // MAP_FIXED_NOREPLACE
        0x40000000; // MAP_UNINITIALIZED

    // IMPORTANT: strip Linux-only flags from the input flags, not from darwin_flags
    // after mapping — both use bit 0x1000, so stripping after would drop MAP_ANON.
    const int clean_flags = flags & ~(LINUX_ONLY_MAP_FLAGS | 0xFC000000);

    int darwin_flags = 0;
    if (clean_flags & LINUX_MAP_SHARED)          darwin_flags |= MAP_SHARED;
    if (clean_flags & LINUX_MAP_PRIVATE)         darwin_flags |= MAP_PRIVATE;
    if (clean_flags & LINUX_MAP_FIXED)           darwin_flags |= MAP_FIXED;
    if (clean_flags & LINUX_MAP_SHARED_VALIDATE) darwin_flags |= MAP_SHARED;

    int darwin_fd = fd;
    if (clean_flags & LINUX_MAP_ANONYMOUS) {
        darwin_flags |= MAP_ANON;
        darwin_fd = -1;  // Darwin requires fd == -1 for anonymous mappings
    }
    return ::mmap(addr, length, prot, darwin_flags, darwin_fd, offset);
#else
    return ::mmap(addr, length, prot, flags, fd, offset);
#endif
}

extern "C" void* bionic_mmap64(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    return bionic_mmap(addr, length, prot, flags, fd, offset);
}

extern "C" int bionic_mprotect(void *addr, size_t len, int prot) {
    if (!addr || len == 0) return 0;
    int r = ::mprotect(addr, len, prot);
#if defined(__APPLE__)
    if (r != 0 && (prot & PROT_EXEC)) {
        // PROT_EXEC was refused, which on iOS means this process has no JIT
        // entitlement and is not being debugged. Report the failure instead of
        // downgrading to read/write and returning success.
        //
        // Silently succeeding is worse than failing here. A guest JIT (V8, Mono,
        // IL2CPP mixed mode) takes the 0 as permission to write code into the region
        // and jump to it, and the process then dies with SIGSEGV or SIGBUS at an
        // address inside that region — no mention of mprotect, nothing to connect it
        // to the missing entitlement. Under LiveContainer's JITLess mode this is the
        // normal case, not a rare one.
        //
        // The region is left readable and writable (mprotect made no change on
        // failure), so a caller that checks the return value can fall back to an
        // interpreter, which is exactly the decision this lets it make.
        static std::once_flag once;
        std::call_once(once, [] {
            logAndroidMessage(6, "KuDroidSyscall",
                              "mprotect(PROT_EXEC) refused: this process has no JIT"
                              " permission (no CS_DEBUGGED, no allow-jit entitlement)."
                              " Guest code that generates machine code at runtime"
                              " cannot run. Under LiveContainer, enable JIT.");
        });
        errno = EPERM;
        return -1;
    }
#endif
    return r;
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

extern "C" int bionic_clock_gettime(int clock_id, struct timespec *tp) {
    if (!tp) {
        errno = EFAULT;
        return -1;
    }
    int darwin_clock_id = clock_id;
#ifdef __APPLE__
    switch(clock_id) {
        case 0: darwin_clock_id = 0; break; // CLOCK_REALTIME
        case 1: darwin_clock_id = 6; break; // CLOCK_MONOTONIC (Darwin 6)
        case 2: darwin_clock_id = 12; break; // CLOCK_PROCESS_CPUTIME_ID
        case 3: darwin_clock_id = 16; break; // CLOCK_THREAD_CPUTIME_ID
        case 4: darwin_clock_id = 4; break; // CLOCK_MONOTONIC_RAW
        case 7: darwin_clock_id = 6; break; // CLOCK_BOOTTIME -> MONOTONIC
        case 5: darwin_clock_id = 0; break; // CLOCK_REALTIME_COARSE -> REALTIME
        case 6: darwin_clock_id = 6; break; // CLOCK_MONOTONIC_COARSE -> MONOTONIC
        default: darwin_clock_id = 6; break; // Default fallback to MONOTONIC
    }
#endif
    return ::clock_gettime(static_cast<clockid_t>(darwin_clock_id), tp);
}

extern "C" int bionic_clock_gettime64(int clock_id, struct timespec *tp) {
    return bionic_clock_gettime(clock_id, tp);
}

#ifdef __APPLE__
static size_t prot_prefix_len(uintptr_t addr, size_t len, vm_prot_t required);
#endif

// The guest's sigaltstack, through the guest's own stack_t layout.
//
// Linux's is {sp, flags, size}; Darwin's is {sp, size, flags}. Taking a `stack_t*`
// here and dereferencing it read those two fields swapped: a guest asking for a 64KB
// alternate stack passed ss_flags=65536, which is neither SS_ONSTACK nor SS_DISABLE,
// the host rejected it — and the old shim logged the failure then returned 0 anyway, so
// the guest went on believing it had an alternate stack it did not have. A stack
// overflow then had nowhere to run its handler, which is exactly the case an alternate
// stack exists for.
extern "C" int bionic_sigaltstack(const void* ss, void* oss) {
#ifdef __APPLE__
    // A direct guest SVC can carry a pointer that is plausible but not accessible in
    // this address space. GuestSignals rejects null and the low page; the Mach-based
    // protection check lives here.
    //
    // Each side is checked for what it is actually used for: `ss` is read from and `oss`
    // is written to. Checking both for readability would approve an `oss` in a read-only
    // page and fault on the store instead. Both must be accessible ENTIRELY — unlike a
    // bulk copy there is no meaningful partial stack_t, so a short prefix is EFAULT.
    if ((ss != nullptr && prot_prefix_len(reinterpret_cast<uintptr_t>(ss), 24,
                                          VM_PROT_READ) != 24) ||
        (oss != nullptr && prot_prefix_len(reinterpret_cast<uintptr_t>(oss), 24,
                                           VM_PROT_WRITE) != 24)) {
        errno = EFAULT;
        return -1;
    }
#endif
    return kudroid::guest_sigaltstack(ss, oss);
}

#ifndef __APPLE__
#include <sys/syscall.h>
#endif

// bionic: long syscall(long number, ...)
// Guest libc/libc++ call syscall() directly with Linux numbers; map them to host
// behaviour and return ENOSYS for unknown numbers (safer than running the wrong call).
#define KUDROID_SYS_gettid 178
#define KUDROID_SYS_getpid 39
#define KUDROID_SYS_futex 98
#define KUDROID_SYS_process_vm_readv 270

// Registry tid -> pthread_t: guest tgkill uses tids from our gettid().
static std::mutex g_tidRegistryMtx;
static std::unordered_map<long, pthread_t> g_tidRegistry;

static void tid_registry_record(long tid) {
    if (tid <= 0) return;
    pthread_t self = pthread_self();
    std::lock_guard<std::mutex> lock(g_tidRegistryMtx);
    g_tidRegistry[tid] = self;
}

extern "C" pid_t bionic_gettid() {
#ifdef __APPLE__
    uint64_t tid = 0;
    pthread_threadid_np(NULL, &tid);
    const pid_t result = static_cast<pid_t>(tid);
    tid_registry_record(static_cast<long>(result));
    return result;
#else
    const pid_t result = static_cast<pid_t>(::syscall(SYS_gettid));
    tid_registry_record(static_cast<long>(result));
    return result;
#endif
}

#ifdef __APPLE__
// How many bytes from `addr` carry every protection bit in `required`.
//
// Returns a LENGTH, not a yes/no, because the kernel this stands in for answers with a
// length: a read that starts in a committed page and runs into a PROT_NONE reservation
// returns the readable prefix and no error. Verified against the real syscall on Linux —
// a 16-byte read straddling the boundary returns 6, not EFAULT. Answering yes/no would
// either approve the faulting half or reject a request the guest is entitled to have
// partly served.
//
// Protection, not mere mappedness, is the question. Getting that wrong is what turned a
// probe meant to PREVENT a segfault into the cause of one: a PROT_NONE reservation IS a
// mapped region — vm_region_64 reports it with a base, a size and protection 0 — so a
// range check answered "safe to read" for memory that faults on its first byte.
//
// That is not an edge case on iOS, it is the common case. Unity reserves address space in
// half-gigabyte PROT_NONE chunks before committing any of it; the ULTRAKILL log has
// eleven `mmap(len=536854528, prot=0x0, flags=0x22)` calls in a row, and 0x5500000000 —
// the address that faulted — has exactly that shape. libunity's own crash reporter probed
// it, this said yes, and the memcpy took the fault inside _platform_memmove with lr in
// bionic_syscall.
static size_t prot_prefix_len(uintptr_t addr, size_t len, vm_prot_t required) {
    if (len == 0) return 0;
    if (addr > static_cast<uintptr_t>(-1) - len) return 0;
    const uintptr_t end = addr + len;
    uintptr_t reached = addr;
    vm_address_t region_addr = static_cast<vm_address_t>(addr);
    while (reached < end) {
        vm_size_t region_size = 0;
        // The real info struct, not an integer buffer. The protection bits are the whole
        // point of this call now, and VM_REGION_BASIC_INFO_COUNT_64 is DEFINED as
        // sizeof(vm_region_basic_info_data_64_t)/sizeof(int) — so the struct and the
        // count cannot disagree, which is what the integer buffer was guarding against.
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object_name = MACH_PORT_NULL;
        const kern_return_t kr = vm_region_64(mach_task_self(), &region_addr, &region_size,
                                              VM_REGION_BASIC_INFO_64,
                                              reinterpret_cast<vm_region_info_t>(&info),
                                              &count, &object_name);
        if (kr != KERN_SUCCESS) break;
        const uintptr_t rstart = static_cast<uintptr_t>(region_addr);
        const uintptr_t rend = rstart + region_size;
        if (region_size == 0 || rend <= rstart) break; // overflow
        // vm_region_64 returns the region containing the address OR the next one after
        // it. A gap before the point reached means the rest is unmapped.
        if (reached < rstart) break;
        if ((info.protection & required) != required) break;
        reached = rend;
        region_addr = rend;
    }
    if (reached <= addr) return 0;
    const size_t covered = static_cast<size_t>(reached - addr);
    return covered < len ? covered : len;
}
#endif

// Guest libs share the host address space, so only pid==self is meaningful —
// used by fbjni/Hermes to probe readability before reading.
extern "C" long bionic_process_vm_readv(pid_t pid, const struct iovec* local_iov, unsigned long liovcnt,
                                         const struct iovec* remote_iov, unsigned long riovcnt,
                                         unsigned long flags) {
#ifdef __linux__
    // Host Linux has a real syscall — handle every pid safely.
    return static_cast<long>(::syscall(SYS_process_vm_readv, pid, local_iov, liovcnt,
                                       remote_iov, riovcnt, flags));
#else
    (void)riovcnt;
    if (flags != 0) { errno = EINVAL; return -1; }
    if (pid != static_cast<pid_t>(::getpid())) {
        errno = pid < 1 ? ESRCH : EPERM;
        return -1;
    }
    if (liovcnt == 0) return 0;
    if (liovcnt > 1024 || riovcnt > 1024) { errno = EINVAL; return -1; }
    long total = 0;
    for (unsigned long i = 0; i < liovcnt && i < riovcnt; ++i) {
        const uint8_t* src = static_cast<const uint8_t*>(remote_iov[i].iov_base);
        uint8_t* dst = static_cast<uint8_t*>(local_iov[i].iov_base);
        const size_t n = std::min(remote_iov[i].iov_len, local_iov[i].iov_len);
        if (n == 0) continue;
        if (!src || !dst) {
            if (total == 0) { errno = EFAULT; return -1; }
            return total;
        }
        // The guest is asking "can I read here?" precisely because it does NOT know, so
        // the answer has to be established before the memcpy rather than discovered by
        // faulting during it.
        //
        // Protection, not mappedness — the distinction this used to miss, and it cost a
        // crash: a PROT_NONE reservation is mapped, Unity makes hundreds of gigabytes of
        // them at startup, and approving one turned libunity's own crash-reporter probe
        // into a fault inside _platform_memmove.
        //
        // The DESTINATION is checked too, which it was not before and which is just as
        // fatal: a local_iov pointing into a read-only or reserved page faults on the
        // write side of the same memcpy, at an address that looks like the guest's own
        // buffer with nothing to suggest this shim chose to write there.
        //
        // A partly-accessible range copies its accessible PREFIX, matching the kernel
        // rather than a convenient simplification. Checked against the real syscall on
        // Linux: a 16-byte read spanning a committed page and the PROT_NONE mapping after
        // it returns 6 and no error. Refusing the whole request would make a guest whose
        // probe legitimately runs off the end of a region conclude the memory is
        // unreadable when it is not.
        size_t copy = prot_prefix_len(reinterpret_cast<uintptr_t>(src), n, VM_PROT_READ);
        const size_t writable = prot_prefix_len(reinterpret_cast<uintptr_t>(dst), copy,
                                                VM_PROT_WRITE);
        if (writable < copy) copy = writable;
        if (copy == 0) {
            if (total == 0) { errno = EFAULT; return -1; }
            return total;
        }
        std::memcpy(dst, src, copy);
        total += static_cast<long>(copy);
        // A short copy means the range ran out of accessible memory, so there is nothing
        // further to read in this iovec and no point starting the next.
        if (copy < n) return total;
    }
    return total;
#endif
}

// futex via syscall(): libc++ calls syscall(98) for atomic wait/notify; route into bionic_futex.
extern "C" int bionic_futex(uint32_t* uaddr, int futex_op, uint32_t val,
                             const struct timespec* timeout, uint32_t* uaddr2, uint32_t val3);

// Declared at namespace scope — `extern "C"` is invalid inside a function.
#if defined(__aarch64__)
extern "C" bool kudroid_lookup_guest_module(void* addr, char* out, std::size_t outSize);
#endif

// DIAG gate: guard/TLS diagnostics run only when investigating (KUDROID_GUARD_DIAG=1).
// Off by default — per-call frame walks cost real runtime; futex logging stays unconditional but capped.
[[maybe_unused]] static bool guard_diag_enabled() {
    static const int enabled = []() {
        const char* v = std::getenv("KUDROID_GUARD_DIAG");
        return v && v[0] == '1';
    }();
    return enabled != 0;
}

static long emulate_futex_direct(uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4, uintptr_t arg5, uintptr_t arg6) {
    uint32_t* uaddr = reinterpret_cast<uint32_t*>(arg1);
    const int futex_op = static_cast<int>(arg2);
    const uint32_t val = static_cast<uint32_t>(arg3);
    const struct timespec* timeout = reinterpret_cast<const struct timespec*>(arg4);
    uint32_t* uaddr2 = reinterpret_cast<uint32_t*>(arg5);
    const uint32_t val3 = static_cast<uint32_t>(arg6);
#if defined(__aarch64__)
    // Futex waits are logged unconditionally but capped at 16 distinct addresses.
    {
        const int cmd = futex_op & 127;
        if (cmd == 0 /* FUTEX_WAIT */ || cmd == 9 /* FUTEX_WAIT_BITSET */) {
            static std::mutex s_seenMtx;
            static void* s_seen[16];
            static int s_seenN = 0;
            bool dup = false;
            {
                // Guarded: this used to race, and two threads entering an unseen
                // futex at once could both write s_seen[s_seenN] and lose one.
                std::lock_guard<std::mutex> lock(s_seenMtx);
                for (int i = 0; i < s_seenN; ++i) {
                    if (s_seen[i] == uaddr) { dup = true; break; }
                }
                if (!dup && s_seenN < 16) s_seen[s_seenN++] = uaddr;
                else dup = true;  // table full: stop logging rather than grow
            }
            if (!dup) {
                char mod[256] = {0};
                kudroid_lookup_guest_module(uaddr, mod, sizeof(mod));
                char msg[352];
                // tid, because without it this line cannot be joined to anything. The
                // captured ULTRAKILL log had two futex_wait lines and one stall report,
                // and no way to tell whether the reported thread was one of the two:
                // the stall said tid=2844404, these said nothing at all.
                snprintf(msg, sizeof(msg),
                         "futex_wait uaddr=0x%llx [%s] val=%u op=%d tid=%llu",
                         (unsigned long long)(uintptr_t)uaddr, mod, val, futex_op,
                         current_thread_id());
                logAndroidMessage(4, "KuDroidSyscall", msg);
            }
        }
    }
#endif
    return static_cast<long>(bionic_futex(uaddr, futex_op, val, timeout, uaddr2, val3));
}

// Diagnostic for __cxa_guard_acquire recursion: logs in-progress guards with module + caller.
#if defined(__aarch64__)
extern "C" bool kudroid_lookup_guest_module(void* addr, char* out, std::size_t outSize);
__attribute__((noinline))
static void log_guard_acquire_diag(long tid, uintptr_t guard) {
    uintptr_t x29v = 0;
    asm volatile("mov %0, x29" : "=r"(x29v));
    // Walk from this frame; keep the deepest [fp+56] == guard match.
    uintptr_t gaCaller = 0;
    kudroid::FrameWalker walker(x29v, kudroid::cached_thread_stack_bounds());
    for (int i = 0; i < 6 && walker.valid(); ++i) {
        uintptr_t candGuard = 0;
        if (walker.slot(56, &candGuard) && candGuard == guard) {
            uintptr_t caller = 0;
            if (walker.return_address(&caller)) gaCaller = caller;
        }
        if (!walker.next()) break;
    }
    char guardMod[256] = {0}, callerMod[256] = {0};
    kudroid_lookup_guest_module(reinterpret_cast<void*>(guard), guardMod, sizeof(guardMod));
    kudroid_lookup_guest_module(reinterpret_cast<void*>(gaCaller), callerMod, sizeof(callerMod));
    unsigned b0 = 0, b1 = 0, storedTid = 0, inProgress = 0, sameTid = 0;
    // The guard lives in a guest module's .bss; confirm via the module registry.
    if (guardMod[0] != '\0') {
        const volatile uint8_t* g = reinterpret_cast<const volatile uint8_t*>(guard);
        b0 = g[0];
        b1 = g[1];
        storedTid = *reinterpret_cast<const volatile uint32_t*>(guard + 4);
        inProgress = (b1 & 0x2) != 0;
        sameTid = inProgress && (static_cast<long>(storedTid) == tid);
    }
    if (!inProgress) return; // new claim — not the case under investigation
    // Bounded to the first 16 reports; unconditional logging would sit on a hot path.
    static std::atomic<int> s_emitted{0};
    if (s_emitted.fetch_add(1, std::memory_order_relaxed) >= 16) return;
    char msg[640];
    snprintf(msg, sizeof(msg),
             "guard_diag %s tid=%ld guard=0x%llx [%s] b0=%u b1=%u stored_tid=%u "
             "call_site=0x%llx caller=0x%llx [%s]",
             sameTid ? "SAME_TID_RECURSION" : "waiting",
             tid, (unsigned long long)guard, guardMod, b0, b1, storedTid,
             (unsigned long long)(uintptr_t)__builtin_return_address(1),
             (unsigned long long)gaCaller, callerMod);
    logAndroidMessage(4, "KuDroidSyscall", msg);
}
#else
static void log_guard_acquire_diag(long tid, uintptr_t guard) { (void)tid; (void)guard; }
#endif

// __cxa_guard_acquire / release / abort — handle same-thread recursion.
// Same-thread re-entry clears in-progress and succeeds instead of aborting.
static std::mutex g_guardMtx;

// Cap same-tid recursions; after N pretend done so a failing init cannot hang forever.
static constexpr int kGuardMaxRecursions = 8;
static std::unordered_map<uintptr_t, int> g_guardRecursions;

static int guard_recursion_count(uintptr_t g) {
    int& n = g_guardRecursions[g];
    return ++n;
}

extern "C" int bionic___cxa_guard_acquire(uint64_t* g) {
    if (!g) return 1;
    // Fast path: done needs no lock.
    if (reinterpret_cast<const volatile uint8_t*>(g)[0] & 0x1) return 0;
    // Take tid before locking to keep lock order consistent.
    const long tid = static_cast<long>(bionic_gettid());
    // Declared outside the loop so one spin is one registry entry, however many
    // times it goes round, and so it clears on every exit path.
    std::optional<BlockingWaitScope> spinTracked;
    std::unique_lock<std::mutex> lock(g_guardMtx);
    for (;;) {
        volatile uint8_t* b0 = reinterpret_cast<volatile uint8_t*>(g);
        if (b0[0] & 0x1) return 0; // already initialised
        if (b0[1] & 0x2) {          // initialisation in progress
            const uint32_t stored = *reinterpret_cast<volatile uint32_t*>(b0 + 4);
            if (stored == static_cast<uint32_t>(tid)) {
                // Same thread re-entered → clear and retry instead of aborting.
                const int rec = guard_recursion_count(reinterpret_cast<uintptr_t>(g));
                if (rec > kGuardMaxRecursions) {
                    // Loop-cut: guard keeps failing; pretend done to escape.
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                             "guard_recursion_loop_cut guard=0x%llx tid=%ld "
                             "rec=%d -> return 0 (pretend done)",
                             (unsigned long long)(uintptr_t)g, tid, rec);
                    logAndroidMessage(4, "KuDroidSyscall", msg);
                    return 0;
                }
                b0[1] &= static_cast<uint8_t>(~0x6);
                *reinterpret_cast<volatile uint32_t*>(b0 + 4) = 0;
                lock.unlock();
                char msg[224];
                snprintf(msg, sizeof(msg),
                         "guard_recursion_tolerated guard=0x%llx tid=%ld -> re-init",
                         (unsigned long long)(uintptr_t)g, tid);
                logAndroidMessage(4, "KuDroidSyscall", msg);
                return 1;
            }
            // Another thread is initialising → mark waiting and spin.
            if (!spinTracked) {
                spinTracked.emplace(WaitKind::kGuardSpin, g, guest_return_address(6));
            }
            // The guard word carries the tid of whoever is running the initialiser.
            // Recording it turns "spinning on guard X" into "spinning on X, held by
            // tid Y", which pairs with Y's own stalled line and names the whole cycle
            // instead of half of it.
            blocking_wait_note_owner(*reinterpret_cast<volatile uint32_t*>(b0 + 4));
            blocking_wait_note_iteration();
            b0[1] |= 0x4;
            lock.unlock();
            ::sched_yield();
            lock.lock();
            continue;
        }
        // Claim: mark in-progress and record our tid.
        b0[1] |= 0x2;
        *reinterpret_cast<volatile uint32_t*>(b0 + 4) = static_cast<uint32_t>(tid);
        return 1;
    }
}

extern "C" void bionic___cxa_guard_release(uint64_t* g) {
    if (!g) return;
    std::lock_guard<std::mutex> lock(g_guardMtx);
    volatile uint8_t* b0 = reinterpret_cast<volatile uint8_t*>(g);
    b0[0] |= 0x1;                                    // done
    b0[1] &= static_cast<uint8_t>(~0x6);             // clear in-progress + waiting
    // Spinners re-check under the lock — no signal needed.
}

extern "C" void bionic___cxa_guard_abort(uint64_t* g) {
    if (!g) return;
    std::lock_guard<std::mutex> lock(g_guardMtx);
    volatile uint8_t* b0 = reinterpret_cast<volatile uint8_t*>(g);
    b0[1] &= static_cast<uint8_t>(~0x6);
    *reinterpret_cast<volatile uint32_t*>(b0 + 4) = 0;
}

extern "C" int bionic_epoll_create1(int flags);
extern "C" int bionic_epoll_ctl(int epfd, int op, int fd, void *event_ptr);
extern "C" int bionic_epoll_wait(int epfd, void *events_ptr, int maxevents, int timeout);
extern "C" int bionic_pipe2(int pipefd[2], int flags);
extern "C" int bionic_clock_gettime(int clock_id, struct timespec *tp);
extern "C" void* bionic_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
extern "C" int bionic_mprotect(void *addr, size_t len, int prot);
extern "C" int bionic_close(int fd);
extern "C" ssize_t bionic_getrandom(void *buf, size_t buflen, unsigned int flags);
// Defined further down, next to the rest of the guest signal glue.
extern "C" int bionic_rt_sigaction(int signum, const void* act, void* oldact,
                                   size_t sigsetsize);
// The outbound signal senders, reached from bionic_syscall below as well as from the
// symbol table. Declared here because the syscall switch comes first in this file.
extern "C" int bionic_tgkill(int pid, int tid, int sig);
extern "C" int bionic_tkill(int tid, int sig);

extern "C" long bionic_syscall(long number, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    uintptr_t entryX19 = 0;
#if defined(__aarch64__)
    // x19 at entry is the guard pointer when this syscall came from
    // __cxa_guard_acquire (see log_guard_acquire_diag). Reading a register is free;
    // the expensive part is the frame walk, which only happens for an in-progress
    // guard and only for the first few.
    asm volatile("mov %0, x19" : "=r"(entryX19));
#endif

    switch (number) {
        // Epoll & Descriptors
        case 20: // epoll_create1
            return bionic_epoll_create1(static_cast<int>(a1));

        case 21: // epoll_ctl
            return bionic_epoll_ctl(static_cast<int>(a1), static_cast<int>(a2), static_cast<int>(a3), reinterpret_cast<void*>(a4));

        case 22: // epoll_pwait / epoll_wait
            return bionic_epoll_wait(static_cast<int>(a1), reinterpret_cast<void*>(a2), static_cast<int>(a3), static_cast<int>(a4));

        case 23: // dup
            return ::dup(static_cast<int>(a1));

        case 24: // dup3
            return ::dup2(static_cast<int>(a1), static_cast<int>(a2));

        case 34: { // mkdirat
            const char* path = reinterpret_cast<const char*>(a2);
            if (!path) return -1;
            const int dirfd = static_cast<int>(a1);
            const std::string remapped =
                at_path_needs_remap(dirfd, path)
                    ? kudroid::VFSPathRemapper::getInstance().remap(path)
                    : std::string(path);
            const int host_dirfd = translate_linux_dirfd(dirfd);
            return ::mkdirat(host_dirfd, remapped.c_str(), static_cast<mode_t>(a3));
        }
        case 35: { // unlinkat
            const char* path = reinterpret_cast<const char*>(a2);
            if (!path) return -1;
            const int dirfd = static_cast<int>(a1);
            const std::string remapped =
                at_path_needs_remap(dirfd, path)
                    ? kudroid::VFSPathRemapper::getInstance().remap(path)
                    : std::string(path);
            const int host_dirfd = translate_linux_dirfd(dirfd);
            return ::unlinkat(host_dirfd, remapped.c_str(), static_cast<int>(a3));
        }
        case 46: // ftruncate
            return ::ftruncate(static_cast<int>(a1), static_cast<off_t>(a2));

        case 56: // openat
            return bionic_openat(static_cast<int>(a1), reinterpret_cast<const char*>(a2), static_cast<int>(a3), static_cast<mode_t>(a4));

        case 57: // close
            return bionic_close(static_cast<int>(a1));

        case 59: // pipe2
            return bionic_pipe2(reinterpret_cast<int*>(a1), static_cast<int>(a2));

        case 62: // lseek
            return ::lseek(static_cast<int>(a1), static_cast<off_t>(a2), static_cast<int>(a3));

        case 63: // read
            return ::read(static_cast<int>(a1), reinterpret_cast<void*>(a2), static_cast<size_t>(a3));

        case 64: // write
            return ::write(static_cast<int>(a1), reinterpret_cast<const void*>(a2), static_cast<size_t>(a3));

        case 67: // pread64
            return ::pread(static_cast<int>(a1), reinterpret_cast<void*>(a2), static_cast<size_t>(a3), static_cast<off_t>(a4));

        case 68: // pwrite64
            return ::pwrite(static_cast<int>(a1), reinterpret_cast<const void*>(a2), static_cast<size_t>(a3), static_cast<off_t>(a4));

        case 79: // newfstatat
            return bionic_newfstatat(static_cast<int>(a1), reinterpret_cast<const char*>(a2), reinterpret_cast<struct bionic_stat64*>(a3), static_cast<int>(a4));

        case 98: // futex
            return emulate_futex_direct(a1, a2, a3, a4, a5, a6);

        case 101: // nanosleep
            return ::nanosleep(reinterpret_cast<const struct timespec*>(a1), reinterpret_cast<struct timespec*>(a2));

        case 113: // clock_gettime
            return bionic_clock_gettime(static_cast<int>(a1), reinterpret_cast<struct timespec*>(a2));

        case 124: // sched_yield
            return ::sched_yield();

        case 132: // sigaltstack (Linux arm64 syscall number)
            return bionic_sigaltstack(reinterpret_cast<const void*>(a1),
                                      reinterpret_cast<void*>(a2));

        // rt_sigaction (Linux arm64 syscall 134). Missing before, so a guest that used
        // syscall() rather than the libc wrapper got ENOSYS and silently ran without
        // the handler it thought it had installed.
        case 134:
            return bionic_rt_sigaction(static_cast<int>(a1), reinterpret_cast<const void*>(a2),
                                       reinterpret_cast<void*>(a3), static_cast<size_t>(a4));

        // The outbound signal syscalls, Linux arm64 numbering.
        //
        // A managed runtime does not always go through the libc wrapper: Mono's thread
        // suspension issues the syscall directly on some builds, and until now every one
        // of these fell through to the ENOSYS default. That is not a silent no-op — the
        // guest reads -1/ENOSYS from a suspend it believes it sent, and waits for a
        // thread that was never signalled.
        case 129: // kill(pid, sig)
            return kudroid::guest_kill(static_cast<int>(a1), static_cast<int>(a2));

        case 130: // tkill(tid, sig)
            return bionic_tkill(static_cast<int>(a1), static_cast<int>(a2));

        case 131: // tgkill(tgid, tid, sig)
            return bionic_tgkill(static_cast<int>(a1), static_cast<int>(a2),
                                 static_cast<int>(a3));

        // rt_sigprocmask(how, set, oldset, sigsetsize). Both `how` and the mask need
        // translating; see bionic_sigprocmask.
        case 135:
            if (static_cast<size_t>(a4) != sizeof(uint64_t)) { errno = EINVAL; return -1; }
            return kudroid::guest_sigprocmask(static_cast<int>(a1),
                                              reinterpret_cast<const uint64_t*>(a2),
                                              reinterpret_cast<uint64_t*>(a3));

        case 133: // rt_sigsuspend(mask, sigsetsize)
            if (static_cast<size_t>(a2) != sizeof(uint64_t)) { errno = EINVAL; return -1; }
            return kudroid::guest_sigsuspend(reinterpret_cast<const uint64_t*>(a1));

        // rt_sigpending(set, sigsetsize): which signals are pending, in the GUEST's
        // numbering. Reported raw, the guest tests bits against its own constants and
        // reads the wrong ones — the same error as the send, one step later.
        case 136: {
            if (static_cast<size_t>(a2) != sizeof(uint64_t)) { errno = EINVAL; return -1; }
            sigset_t host_pending;
            sigemptyset(&host_pending);
            if (::sigpending(&host_pending) != 0) return -1;
            if (a1 != 0) {
                uint64_t guest_pending = 0;
                for (int host_sig = 1; host_sig < NSIG; ++host_sig) {
                    if (sigismember(&host_pending, host_sig) != 1) continue;
                    const int guest_sig = kudroid::host_signal_to_guest(host_sig);
                    if (guest_sig >= 1 && guest_sig < 65) {
                        guest_pending |= (1ull << (guest_sig - 1));
                    }
                }
                *reinterpret_cast<uint64_t*>(a1) = guest_pending;
            }
            return 0;
        }

        case 160: // uname
            return bionic_uname(reinterpret_cast<struct bionic_utsname*>(a1));

        case 167: // prctl
            return bionic_prctl(static_cast<int>(a1), a2, a3, a4, a5);

        case 169: // gettimeofday
            return ::gettimeofday(reinterpret_cast<struct timeval*>(a1), reinterpret_cast<struct timezone*>(a2));

        case 172: // getpid
            return static_cast<long>(::getpid());

        case 174: // getuid
            return static_cast<long>(::getuid());

        case 175: // geteuid
            return static_cast<long>(::geteuid());

        case 176: // getgid
            return static_cast<long>(::getgid());

        case 177: // getegid
            return static_cast<long>(::getegid());

        case 178: { // gettid
#ifdef __APPLE__
            uint64_t tid = 0;
            pthread_threadid_np(NULL, &tid);
            const long result = static_cast<long>(tid);
#else
            const long result = static_cast<long>(::syscall(SYS_gettid));
#endif
            tid_registry_record(result);
            log_guard_acquire_diag(result, entryX19);
            return result;
        }
        case 198: // socket
            return ::socket(static_cast<int>(a1), static_cast<int>(a2), static_cast<int>(a3));

        case 200: // bind
            return ::bind(static_cast<int>(a1), reinterpret_cast<const struct sockaddr*>(a2), static_cast<socklen_t>(a3));

        case 203: // connect
            return ::connect(static_cast<int>(a1), reinterpret_cast<const struct sockaddr*>(a2), static_cast<socklen_t>(a3));

        case 204: // getsockname
            return ::getsockname(static_cast<int>(a1), reinterpret_cast<struct sockaddr*>(a2), reinterpret_cast<socklen_t*>(a3));

        case 206: // sendto
            return ::sendto(static_cast<int>(a1), reinterpret_cast<const void*>(a2), static_cast<size_t>(a3), static_cast<int>(a4), reinterpret_cast<const struct sockaddr*>(a5), static_cast<socklen_t>(a6));

        case 207: // recvfrom
            return ::recvfrom(static_cast<int>(a1), reinterpret_cast<void*>(a2), static_cast<size_t>(a3), static_cast<int>(a4), reinterpret_cast<struct sockaddr*>(a5), reinterpret_cast<socklen_t*>(a6));

        case 122: // sched_setaffinity (Linux arm64 syscall)
            // iOS does not support binding threads to CPU cores; return 0 (success)
            return 0;

        case 123: // sched_getaffinity (Linux arm64 syscall)
            // a1=pid, a2=cpusetsize, a3=mask pointer.
            //
            // Returns the NUMBER OF BYTES WRITTEN, which is what the kernel does and
            // what bionic's wrapper relies on:
            //
            //     int rc = __sched_getaffinity(pid, size, set);
            //     if (rc == -1) return -1;
            //     if ((size_t)rc < size) memset((char*)set + rc, 0, size - rc);
            //     return 0;
            //
            // Returning 0 here therefore made bionic memset the ENTIRE mask to zero,
            // handing the guest an affinity set with no CPUs in it. Unity read that
            // back as "Cores = 0" and "0 big (mask: 0x0), 0 little (mask: 0x0)", then
            // sized its job system from it.
            if (a3 != 0 && a2 > 0) {
                const size_t requested = static_cast<size_t>(a2);
                const size_t written = requested < sizeof(unsigned long)
                                           ? requested
                                           : sizeof(unsigned long);
                // Zero only the part being reported: bionic clears the remainder
                // itself, and a mask larger than one word must not be pre-cleared
                // past `written` or the return value would be a lie.
                memset(reinterpret_cast<void*>(a3), 0, written);
                // Same topology as the wrapper, sysconf and /proc/cpuinfo. This was
                // 0xFF regardless of the device.
                const unsigned long online =
                    static_cast<unsigned long>(kudroid::cpu_online_mask());
                memcpy(reinterpret_cast<void*>(a3), &online, written);
                return static_cast<long>(written);
            }
            errno = EFAULT;
            return -1;

        case 215: // munmap
            return ::munmap(reinterpret_cast<void*>(a1), static_cast<size_t>(a2));

        case 222: // mmap
            return (long)bionic_mmap(reinterpret_cast<void*>(a1), static_cast<size_t>(a2), static_cast<int>(a3), static_cast<int>(a4), static_cast<int>(a5), static_cast<off_t>(a6));

        case 226: // mprotect
            return bionic_mprotect(reinterpret_cast<void*>(a1), static_cast<size_t>(a2), static_cast<int>(a3));

        case 233: // madvise
            return ::madvise(reinterpret_cast<void*>(a1), static_cast<size_t>(a2), static_cast<int>(a3));

        case 278: // getrandom
            return bionic_getrandom(reinterpret_cast<void*>(a1), static_cast<size_t>(a2), static_cast<unsigned int>(a3));

        case KUDROID_SYS_process_vm_readv: {
            const pid_t pid = static_cast<pid_t>(a1);
            const struct iovec* local_iov = reinterpret_cast<const struct iovec*>(a2);
            const unsigned long liovcnt = static_cast<unsigned long>(a3);
            const struct iovec* remote_iov = reinterpret_cast<const struct iovec*>(a4);
            const unsigned long riovcnt = static_cast<unsigned long>(a5);
            const unsigned long flags = static_cast<unsigned long>(a6);
            return bionic_process_vm_readv(pid, local_iov, liovcnt, remote_iov, riovcnt, flags);
        }
        default:
            break;
    }

    // Unmapped — log once per number, then ENOSYS (never run the wrong host call).
    static long s_unknownSeen[64];
    static int s_unknownCount = 0;
    bool known = false;
    for (int i = 0; i < s_unknownCount; ++i) {
        if (s_unknownSeen[i] == number) { known = true; break; }
    }
    if (!known && s_unknownCount < 64) {
        s_unknownSeen[s_unknownCount++] = number;
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "bionic_syscall: unhandled Linux syscall %ld -> ENOSYS", number);
        logAndroidMessage(4, "KuDroidSyscall", msg);
    }
    errno = ENOSYS;
    return -1;
}

// A guest library may issue `svc #0` directly instead of calling the imported
// libc syscall wrapper. Darwin delivers SIGSYS for that Linux syscall ABI; on
// Apple, emulate the request from the signal context and resume after SVC.
// This keeps the boundary generic for all Linux/Android arm64 guests.
extern "C" bool bionic_handle_guest_syscall_trap(void* context) {
#if defined(__APPLE__) && defined(__aarch64__)
    if (context == nullptr) return false;
    ucontext_t* uc = static_cast<ucontext_t*>(context);
    uint32_t* pc = reinterpret_cast<uint32_t*>(uc->uc_mcontext->__ss.__pc);
    uintptr_t svc_pc = reinterpret_cast<uintptr_t>(pc);
    uint32_t inst = *pc;
    if ((inst & 0xFFE0001F) != 0xD4000001) {
        if (svc_pc < 4) return false;
        pc--;
        inst = *pc;
        if ((inst & 0xFFE0001F) != 0xD4000001) return false;
        svc_pc -= 4;
    }
    if (uc->uc_mcontext->__ss.__x[8] > 1000) return false;
    const long result = bionic_syscall(
        static_cast<long>(uc->uc_mcontext->__ss.__x[8]),
        uc->uc_mcontext->__ss.__x[0], uc->uc_mcontext->__ss.__x[1],
        uc->uc_mcontext->__ss.__x[2], uc->uc_mcontext->__ss.__x[3],
        uc->uc_mcontext->__ss.__x[4], uc->uc_mcontext->__ss.__x[5]);
    uc->uc_mcontext->__ss.__x[0] = static_cast<uint64_t>(result);
    uc->uc_mcontext->__ss.__pc = svc_pc + 4;
    return true;
#else
    (void)context;
    return false;
#endif
}

// Wrappers previously bound to the dummy; on arm64/Linux these match the host signatures.
extern "C" ssize_t bionic_pread64(int fd, void* buf, size_t count, off_t offset) {
    const auto t0 = std::chrono::steady_clock::now();
    const ssize_t ret = ::pread(fd, buf, count, offset);
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0)
                             .count();
    // Diagnostic: short reads on asset/bank streaming corrupt async loads.
    if ((ret >= 0 && static_cast<size_t>(ret) < count) || ret < 0) {
        static std::atomic<int> s_logged{0};
        if (s_logged.load() < 30) {
            ++s_logged;
            std::fprintf(stderr,
                         "[KuDroidIO] pread64 fd=%d count=%zu offset=%lld ret=%zd%s\n",
                         fd, count, static_cast<long long>(offset), ret,
                         ret < 0 ? " ERR" : " SHORT");
        }
    }
    // Diagnostic: slow asset reads starve async loaders (mixer hits pending voices).
    if (ms >= 5 && count >= 65536) {
        static std::atomic<int> s_slow{0};
        if (s_slow.load() < 20) {
            ++s_slow;
            std::fprintf(stderr,
                         "[KuDroidIO] slow pread64 fd=%d count=%zu ret=%zd took=%lldms\n",
                         fd, count, ret, ms);
        }
    }
    return ret;
}
extern "C" ssize_t bionic_pwrite64(int fd, const void* buf, size_t count, off_t offset) {
    return ::pwrite(fd, buf, count, offset);
}
extern "C" int bionic_ftruncate64(int fd, off_t length) {
    return ::ftruncate(fd, length);
}

// pipe2: macOS lacks it — pipe() + fcntl. Linux O_NONBLOCK=0x800, O_CLOEXEC=0x80000.
extern "C" int bionic_pipe2(int pipefd[2], int flags) {
#ifdef __linux__
    return ::pipe2(pipefd, flags);
#else
    if (::pipe(pipefd) != 0) return -1;
    if (flags & 0x800) { // O_NONBLOCK
        for (int i = 0; i < 2; ++i) {
            const int fl = ::fcntl(pipefd[i], F_GETFL);
            if (fl < 0 || ::fcntl(pipefd[i], F_SETFL, fl | O_NONBLOCK) != 0) {
                const int saved = errno;
                ::close(pipefd[0]); ::close(pipefd[1]);
                errno = saved;
                return -1;
            }
        }
    }
    if (flags & 0x80000) { // O_CLOEXEC
        for (int i = 0; i < 2; ++i) {
            const int fl = ::fcntl(pipefd[i], F_GETFD);
            if (fl < 0 || ::fcntl(pipefd[i], F_SETFD, fl | FD_CLOEXEC) != 0) {
                const int saved = errno;
                ::close(pipefd[0]); ::close(pipefd[1]);
                errno = saved;
                return -1;
            }
        }
    }
    return 0;
#endif
}

// clock_nanosleep: macOS has only relative nanosleep; use it for both clocks.
extern "C" int bionic_clock_nanosleep(int clock_id, int flags, const struct timespec* req,
                                       struct timespec* rem) {
    (void)clock_id;
    if (!req) { errno = EFAULT; return -1; }
    struct timespec r = *req;
    if (flags & 1) { // TIMER_ABSTIME
        struct timespec now;
        ::clock_gettime(CLOCK_REALTIME, &now);
        r.tv_sec -= now.tv_sec;
        r.tv_nsec -= now.tv_nsec;
        if (r.tv_nsec < 0) { r.tv_sec -= 1; r.tv_nsec += 1000000000; }
        if (r.tv_sec < 0) { r.tv_sec = 0; r.tv_nsec = 0; }
    }
#ifdef __linux__
    return ::clock_nanosleep(static_cast<clockid_t>(clock_id), 0, &r, rem);
#else
    return ::nanosleep(&r, rem);
#endif
}

// usleep: forward to the host; previously missing so resolution was unreliable.
extern "C" int bionic_usleep(unsigned int usecs) {
    return ::usleep(usecs);
}

// tgkill(pid, tid, sig): look up the tid->pthread_t recorded at gettid.
// `sig` is a LINUX number and must be translated before it reaches the host, exactly as
// sigaction translates it on the way in. This used to forward it raw, and the asymmetry
// is what broke il2cpp's thread suspension: the guest installs a handler for Linux
// SIGPWR (30), which lands on host SIGINFO (29), and then sends 30 — Darwin's SIGUSR1,
// an empty slot whose default action terminates the process. The install and the send
// both reported success.
extern "C" int bionic_tgkill(int pid, int tid, int sig) {
    if (pid != static_cast<int>(::getpid())) { errno = ESRCH; return -1; }
    pthread_t target;
    {
        std::lock_guard<std::mutex> lock(g_tidRegistryMtx);
        auto it = g_tidRegistry.find(static_cast<long>(tid));
        if (it == g_tidRegistry.end()) { errno = ESRCH; return -1; }
        target = it->second;
    }
    if (sig == 0) return 0; // kim tra thread tn ti
    unsigned long thread_bits = 0;
    std::memcpy(&thread_bits, &target,
                sizeof(thread_bits) < sizeof(target) ? sizeof(thread_bits) : sizeof(target));
    return kudroid::guest_pthread_kill(thread_bits, sig);
}

// The rest of the outbound signal surface. All four were previously unshimmed, so the
// guest's Linux number went straight to the host — see the note on bionic_tgkill above.
extern "C" int bionic_raise(int sig) {
    return kudroid::guest_raise(sig);
}

extern "C" int bionic_kill(pid_t pid, int sig) {
    return kudroid::guest_kill(static_cast<int>(pid), sig);
}

extern "C" int bionic_pthread_kill(pthread_t thread, int sig) {
    unsigned long thread_bits = 0;
    std::memcpy(&thread_bits, &thread,
                sizeof(thread_bits) < sizeof(thread) ? sizeof(thread_bits) : sizeof(thread));
    return kudroid::guest_pthread_kill(thread_bits, sig);
}

extern "C" int bionic_tkill(int tid, int sig) {
    return bionic_tgkill(static_cast<int>(::getpid()), tid, sig);
}

// abort(), so the guest's own abort message and thread reach the log before the process
// goes down.
//
// Not a translation — SIGABRT is 6 on both platforms — but the one place where a guest
// says "I am giving up" with a reason it has already stored, and routing it through
// guest_raise means the fatal-signal breadcrumb is written by KuDroid's handler with
// that reason attached. Falling through to the host's abort() reached the same handler
// but named nothing about the guest.
extern "C" void bionic_abort(void) {
    // Unblock SIGABRT first: abort() must not be silently swallowed by a guest that
    // blocked it, which is what the C standard requires of a conforming abort().
    sigset_t only_abort;
    sigemptyset(&only_abort);
    sigaddset(&only_abort, SIGABRT);
    ::pthread_sigmask(SIG_UNBLOCK, &only_abort, nullptr);

    kudroid::guest_raise(6);  // Linux SIGABRT

    // A handler that returned, or one that was ignored: abort() may not return, so fall
    // back to the host's, which restores SIG_DFL and re-raises.
    ::abort();
}

// sigprocmask / pthread_sigmask, where BOTH the mask and `how` need translating: Linux
// numbers SIG_BLOCK/UNBLOCK/SETMASK 0/1/2 and Darwin numbers them 1/2/3, so forwarding
// the guest's value selects a different operation rather than failing.
extern "C" int bionic_sigprocmask(int how, const void* set, void* oldset) {
    return kudroid::guest_sigprocmask(how, static_cast<const uint64_t*>(set),
                                      static_cast<uint64_t*>(oldset));
}

extern "C" int bionic_pthread_sigmask(int how, const void* set, void* oldset) {
    return kudroid::guest_sigprocmask(how, static_cast<const uint64_t*>(set),
                                      static_cast<uint64_t*>(oldset));
}

extern "C" int bionic_sigsuspend(const void* mask) {
    return kudroid::guest_sigsuspend(static_cast<const uint64_t*>(mask));
}

extern "C" int bionic_sigwait(const void* set, int* sig) {
    return kudroid::guest_sigwait(static_cast<const uint64_t*>(set), sig);
}

// signal(): sigaction with BSD flags, recorded through the same registry so a guest that
// installs with one and reads back with the other sees its own handler.
extern "C" void* bionic_signal(int sig, void* handler) {
    return kudroid::guest_signal(sig, handler);
}

// bionic's sigset_t operations, on the GUEST's 64-bit mask.
//
// These take a pointer to the guest's sigset_t — a bare unsigned long on LP64 bionic —
// and the bit for Linux signal n is (n-1). The host's macros operate on the host's
// sigset_t, which on Darwin is also 32 bits wide but numbers its bits by HOST signal, so
// letting these bind to the host's implementations mixes the two numbering schemes in
// one word. A guest that builds a mask with sigaddset and hands it to sigprocmask would
// then have it translated a second time.
extern "C" int bionic_sigemptyset(void* set) {
    if (!set) { errno = EINVAL; return -1; }
    *static_cast<uint64_t*>(set) = 0;
    return 0;
}

extern "C" int bionic_sigfillset(void* set) {
    if (!set) { errno = EINVAL; return -1; }
    *static_cast<uint64_t*>(set) = ~0ull;
    return 0;
}

extern "C" int bionic_sigaddset(void* set, int sig) {
    if (!set || sig < 1 || sig >= 65) { errno = EINVAL; return -1; }
    *static_cast<uint64_t*>(set) |= (1ull << (sig - 1));
    return 0;
}

extern "C" int bionic_sigdelset(void* set, int sig) {
    if (!set || sig < 1 || sig >= 65) { errno = EINVAL; return -1; }
    *static_cast<uint64_t*>(set) &= ~(1ull << (sig - 1));
    return 0;
}

extern "C" int bionic_sigismember(const void* set, int sig) {
    if (!set || sig < 1 || sig >= 65) { errno = EINVAL; return -1; }
    return (*static_cast<const uint64_t*>(set) & (1ull << (sig - 1))) != 0 ? 1 : 0;
}

// sendfile: macOS signature khc hn Linux — emulate bng pread/write loop.
extern "C" ssize_t bionic_sendfile(int out_fd, int in_fd, off_t* offset, size_t count) {
    off_t pos = offset ? *offset : ::lseek(in_fd, 0, SEEK_CUR);
    if (pos < 0) return -1;
    char buf[65536];
    size_t total = 0;
    while (total < count) {
        const size_t chunk = std::min(sizeof(buf), count - total);
        const ssize_t n = ::pread(in_fd, buf, chunk, pos);
        if (n <= 0) break;
        const ssize_t w = ::write(out_fd, buf, static_cast<size_t>(n));
        if (w <= 0) break;
        pos += w;
        total += static_cast<size_t>(w);
        if (static_cast<size_t>(w) != static_cast<size_t>(n)) break;
    }
    if (offset) *offset = pos;
    return static_cast<ssize_t>(total);
}

extern "C" int bionic_sched_getscheduler(pid_t pid) {
    (void)pid;
    return 0; // SCHED_OTHER
}

extern "C" void bionic___assert2(const char* file, int line, const char* function, const char* message) {
    char buf[512];
    snprintf(buf, sizeof(buf), "ASSERTION FAILED in %s (%s:%d): %s",
             function ? function : "?", file ? file : "?", line, message ? message : "");
    logAndroidMessage(6, "KuDroidAssert", buf);
    fprintf(stderr, "[KuDroidAssert] %s\n", buf);
    abort();
}

// Report a guest assertion failure and abort, as bionic does.
//
// Split from the entry point below so the arm64 path can reach it after unpacking the
// guest's registers itself: __android_log_assert is variadic, and forwarding a guest's
// varargs to a host vsnprintf reads the wrong place (see GuestVarargs.h). The failure was
// visible in a crash log — strlen faulting on 0x3930 inside this function, which is a
// format argument that was never a pointer.
static void reportGuestAssert(const char* cond, const char* tag, const char* message) {
    logAndroidMessage(6, tag ? tag : "KuDroidAssert", message);
    fprintf(stderr, "[KuDroidAssert][%s] %s\n", tag ? tag : "assert", message);
    // The condition text is what names the failing invariant; bionic prints it too, and
    // without it the message alone often does not say which check tripped.
    if (cond && *cond) {
        fprintf(stderr, "[KuDroidAssert] condition: %s\n", cond);
    }
    abort();
}

#if defined(__aarch64__)
extern "C" void kudroid_log_assert_trampoline();

// __android_log_assert(cond, tag, fmt, ...): varargs start at the fourth integer register.
extern "C" int kudroid_log_assert_from_registers(const uint64_t* frame) {
    const auto* registers = reinterpret_cast<const GuestVarargs*>(frame);
    const char* cond = reinterpret_cast<const char*>(registers->gp[0]);
    const char* tag = reinterpret_cast<const char*>(registers->gp[1]);
    const char* format = reinterpret_cast<const char*>(registers->gp[2]);

    char message[512];
    if (format != nullptr) {
        FormatGuestVarargs(message, sizeof(message), format, registers, /*firstGpIndex=*/3);
    } else {
        FormatGuestVarargs(message, sizeof(message), "assertion failed", registers, 3);
    }
    reportGuestAssert(cond, tag, message);
    return 0;  // not reached: reportGuestAssert aborts
}
#else
extern "C" void bionic___android_log_assert(const char* cond, const char* tag, const char* fmt, ...) {
    char buf[512];
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
    } else {
        snprintf(buf, sizeof(buf), "assertion failed: %s", cond ? cond : "unknown");
    }
    reportGuestAssert(cond, tag, buf);
}
#endif

extern "C" ssize_t bionic_splice(int fd_in, off_t* off_in, int fd_out, off_t* off_out, size_t len, unsigned int flags) {
    (void)flags;
    char buf[16384];
    size_t total = 0;
    while (total < len) {
        size_t chunk = std::min(sizeof(buf), len - total);
        ssize_t n = off_in ? ::pread(fd_in, buf, chunk, *off_in) : ::read(fd_in, buf, chunk);
        if (n <= 0) break;
        if (off_in) *off_in += n;
        ssize_t w = off_out ? ::pwrite(fd_out, buf, n, *off_out) : ::write(fd_out, buf, n);
        if (w <= 0) break;
        if (off_out) *off_out += w;
        total += w;
        if (w != n) break;
    }
    return total > 0 ? static_cast<ssize_t>(total) : -1;
}

extern "C" ssize_t bionic_copy_file_range(int fd_in, off_t* off_in, int fd_out, off_t* off_out, size_t len, unsigned int flags) {
    return bionic_splice(fd_in, off_in, fd_out, off_out, len, flags);
}

extern "C" int bionic_omp_in_parallel() {
    return 0; // cha c OpenMP runtime — "ngoi vng parallel"
}

extern "C" char* bionic___strchr_chk(const char* s, int c, size_t dst_len) {
    if (!s) return nullptr;
    for (size_t i = 0; i < dst_len; ++i) {
        if (s[i] == static_cast<char>(c)) return const_cast<char*>(&s[i]);
        if (s[i] == '\0') return nullptr;
    }
    return nullptr;
}

// bionic __strncpy_chk2: copy at most n chars, bounded by src_len/dst_len (fortify).
extern "C" char* bionic___strncpy_chk2(char* dst, const char* src, size_t n,
                                       size_t dst_len, size_t src_len) {
    if (!dst || !src) return dst;
    size_t copy = n;
    if (copy > dst_len) { copy = dst_len; }
    if (src_len < copy) { copy = src_len; }
    if (copy > 0) std::memcpy(dst, src, copy);
    // strncpy pads the remainder with 0 when space remains.
    if (n > copy && dst_len > copy) std::memset(dst + copy, 0, std::min(n - copy, dst_len - copy));
    return dst;
}

extern "C" void bionic___FD_CLR_chk(int fd, fd_set* set) {
    if (fd < 0 || fd >= FD_SETSIZE || !set) {
        trace("__FD_CLR_chk: fd out of range");
        return;
    }
    FD_CLR(fd, set);
}

extern "C" struct cmsghdr* bionic___cmsg_nxthdr(struct msghdr* mhdr, struct cmsghdr* cmsg) {
    // msghdr/cmsghdr layout Linux v macOS ging nhau (msg_control@40,
    // msg_controllen@48; cmsghdr: len/level/type) nn CMSG_NXTHDR host read ng.
    return CMSG_NXTHDR(mhdr, cmsg);
}

// Linux MREMAP flags (asm-generic/mman.h)
#define MREMAP_MAYMOVE 1
#define MREMAP_FIXED 2

extern "C" void* bionic_mremap(void *old_address, size_t old_size, size_t new_size, int flags, void *new_address) {
#ifndef __APPLE__
    // Linux host: delegate to the real syscall.
    return ::mremap(old_address, old_size, new_size, flags, new_address);
#else
    (void)new_address;
    // No mremap on Darwin; emulate Linux semantics:
    if (new_size == old_size) return old_address; // no-op
    if (flags & (MREMAP_MAYMOVE | MREMAP_FIXED)) {
        void* new_ptr = mmap(NULL, new_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (new_ptr != MAP_FAILED) {
            // Copy only min(old,new) to avoid overflowing the new mapping.
            std::memcpy(new_ptr, old_address, std::min(old_size, new_size));
            munmap(old_address, old_size);
            return new_ptr;
        }
        errno = ENOMEM;
        return MAP_FAILED;
    }
    // Without MAYMOVE only shrinking in place succeeds; growing fails like Linux.
    if (new_size < old_size) return old_address;
    errno = ENOMEM;
    return MAP_FAILED;
#endif
}

#define AT_HWCAP 16
#define AT_PAGESZ 6
#define AT_RANDOM 25
#define AT_HWCAP2 26

#define HWCAP_FP (1 << 0)
#define HWCAP_ASIMD (1 << 1)
#define HWCAP_EVTSTRM (1 << 2)
#define HWCAP_AES (1 << 3)
#define HWCAP_PMULL (1 << 4)
#define HWCAP_SHA1 (1 << 5)
#define HWCAP_SHA2 (1 << 6)
#define HWCAP_CRC32 (1 << 7)

static const uint8_t g_kudroid_random_aux[16] = {
    0x4b, 0x75, 0x64, 0x72, 0x6f, 0x69, 0x64, 0x53,
    0x65, 0x63, 0x75, 0x72, 0x69, 0x74, 0x79, 0x21
};

extern "C" unsigned long bionic_getauxval(unsigned long type) {
    if (type == AT_HWCAP) {
        return HWCAP_FP | HWCAP_ASIMD | HWCAP_EVTSTRM | HWCAP_AES | HWCAP_PMULL | HWCAP_SHA1 | HWCAP_SHA2 | HWCAP_CRC32;
    }
    if (type == AT_HWCAP2) {
        return 0x00000fff; // ATOMICS, FPHP, ASIMDHP, FLAGM, JSCVT, FCMA, LRCPC, DCPOP, SHA3, SM3, SM4, ASIMDDP
    }
    if (type == AT_PAGESZ) {
        // Tr kch thc trang tht ca host — trnh engine read pagesize=0.
        long pagesize = ::sysconf(_SC_PAGESIZE);
        return pagesize > 0 ? static_cast<unsigned long>(pagesize) : 4096;
    }
    if (type == AT_RANDOM) {
        return reinterpret_cast<unsigned long>(g_kudroid_random_aux);
    }
    return 0;
}

extern "C" ssize_t bionic_getrandom(void *buf, size_t buflen, unsigned int flags) {
    (void)flags;
    if (!buf || buflen == 0) return 0;
#ifdef __APPLE__
    // arc4random_buf is the preferred secure random source on Apple platforms.
    arc4random_buf(buf, buflen);
    return static_cast<ssize_t>(buflen);
#else
    int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    ssize_t ret = ::read(fd, buf, buflen);
    ::close(fd);
    return ret;
#endif
}

// sysconf, through the GUEST's constant numbering.
//
// bionic and glibc do not agree on the values, and the guest is always bionic. The cases
// below therefore list both where they differ — and the pair that was missing is exactly
// the pair that matters here: bionic's _SC_PHYS_PAGES is 98 and _SC_AVPHYS_PAGES is 99,
// while only the glibc 85/86 were handled. A guest asking for physical memory fell
// through to Darwin's ::sysconf(98), which means something else entirely.
extern "C" long bionic_sysconf(int name) {
    switch (name) {
        case 0: // _SC_ARG_MAX
            return 131072;
        case 1: // _SC_CHILD_MAX
            return 1024;
        case 2: // _SC_CLK_TCK
            return 100;
        case 3: // _SC_NGROUPS_MAX
            return 65536;
        case 4: // _SC_OPEN_MAX
            return 32768;
        case 30: // _SC_PAGESIZE (glibc)
        case 39: // _SC_PAGESIZE (bionic)
        case 40: // _SC_PAGE_SIZE (bionic)
        {
            long pz = ::sysconf(_SC_PAGESIZE);
            return pz > 0 ? pz : 16384;
        }
        case 83: // _SC_NPROCESSORS_CONF (glibc)
        case 84: // _SC_NPROCESSORS_ONLN (glibc)
        case 96: // _SC_NPROCESSORS_CONF (bionic)
        case 97: // _SC_NPROCESSORS_ONLN (bionic)
        {
            // The same source as /proc/cpuinfo, /sys/.../present and the affinity mask.
            // It used to be std::thread::hardware_concurrency() with a fallback of 8,
            // independent of every other surface — so a guest cross-checking two of them
            // could see different machines.
            return static_cast<long>(kudroid::query_cpu_topology().total_cores);
        }
        case 85: // _SC_PHYS_PAGES (glibc)
        case 98: // _SC_PHYS_PAGES (bionic)
        {
            long pz = ::sysconf(_SC_PAGESIZE);
            if (pz <= 0) pz = 16384;
            // The real device figure, not a constant. This said 8 GB on every device.
            return static_cast<long>(kudroid::query_system_memory().total_bytes /
                                     static_cast<uint64_t>(pz));
        }
        case 86: // _SC_AVPHYS_PAGES (glibc)
        case 99: // _SC_AVPHYS_PAGES (bionic)
        {
            long pz = ::sysconf(_SC_PAGESIZE);
            if (pz <= 0) pz = 16384;
            return static_cast<long>(kudroid::query_system_memory().available_bytes /
                                     static_cast<uint64_t>(pz));
        }
        default:
            break;
    }
    return ::sysconf(name);
}

// ashmem (Android shared memory): iOS lacks it, so back it with anonymous mmap + fake fd.
static std::mutex g_ashmem_mtx;
static std::unordered_map<int, int> g_ashmem_prot;      // fd -> granted prot
static std::unordered_map<int, void*> g_ashmem_region;  // fake fd -> region
static std::unordered_map<int, size_t> g_ashmem_size;   // fake fd -> size
static std::atomic<int> g_ashmem_fake_fd{0x40000000};   // fake fds start high to avoid real fds

static void ashmem_forget(int fd) {
    std::lock_guard<std::mutex> lock(g_ashmem_mtx);
    g_ashmem_prot.erase(fd);
    auto it = g_ashmem_region.find(fd);
    if (it != g_ashmem_region.end()) {
        ::munmap(it->second, g_ashmem_size[fd]);
        g_ashmem_region.erase(it);
        g_ashmem_size.erase(fd);
    }
}

extern "C" int bionic_ashmem_create_region(const char* name, size_t size) {
    (void)name;
    if (size == 0) size = 1;
    // 1) Try POSIX shm (works on macOS).
    static std::atomic<uint32_t> counter{0};
    char shm_name[64];
    std::snprintf(shm_name, sizeof(shm_name), "/kudroid_ashmem_%d_%u", ::getpid(), counter.fetch_add(1));
    int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
        shm_unlink(shm_name);
        if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
            // fd stays valid; later mmap fails if the size overruns.
        }
        std::lock_guard<std::mutex> lock(g_ashmem_mtx);
        g_ashmem_prot[fd] = PROT_READ | PROT_WRITE;
        return fd;
    }

    // 2) Fallback (iOS): anonymous mmap as the backing region + fake fd.
    void* base = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return -1;
    std::memset(base, 0, size);
    const int fake = g_ashmem_fake_fd.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(g_ashmem_mtx);
        g_ashmem_prot[fake] = PROT_READ | PROT_WRITE;
        g_ashmem_region[fake] = base;
        g_ashmem_size[fake] = size;
    }
    logAndroidMessage(4, "KuDroidSyscall", "ashmem_create_region fallback: fake fd=" +
                      std::to_string(fake) + " size=" + std::to_string(size));
    return fake;
}

extern "C" int bionic_ashmem_set_name(int fd, const char* name) {
    (void)fd; (void)name;
    return 0; // debug name only — nothing to store
}

extern "C" int bionic_ashmem_set_prot_region(int fd, int prot) {
    std::lock_guard<std::mutex> lock(g_ashmem_mtx);
    g_ashmem_prot[fd] = prot;
    return 0;
}

static bool ashmem_prot_allows(int fd, int prot) {
    if (fd < 0) return true;
    std::lock_guard<std::mutex> lock(g_ashmem_mtx);
    auto it = g_ashmem_prot.find(fd);
    if (it == g_ashmem_prot.end()) return true; // not an ashmem fd
    return (prot & ~it->second) == 0;
}

// bionic_mmap helper: return the backing region for a fake ashmem fd.
extern "C" void* bionic_ashmem_mmap_fd(int fd, size_t length) {
    std::lock_guard<std::mutex> lock(g_ashmem_mtx);
    auto it = g_ashmem_region.find(fd);
    if (it == g_ashmem_region.end()) return nullptr;
    if (length > g_ashmem_size[fd]) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    return it->second;
}

// --- Linux-Specific Syscalls ---

struct FutexWaitQueue {
    std::mutex mtx;
    std::condition_variable cv;
    int waiters = 0; // guarded by g_futexGlobalMtx
};
// shared_ptr keeps the queue alive while a thread sleeps in cv.wait.
static std::unordered_map<uint32_t*, std::shared_ptr<FutexWaitQueue>> g_futexQueues;
static std::mutex g_futexGlobalMtx;

// Drop the waiter count; erase the entry when nobody remains.
// Must be called without g_futexGlobalMtx held (lock order: global before queue-mtx).
static void futex_leave(uint32_t* uaddr, const std::shared_ptr<FutexWaitQueue>& q) {
    std::unique_lock<std::mutex> lock(g_futexGlobalMtx);
    if (--q->waiters <= 0) {
        auto it = g_futexQueues.find(uaddr);
        if (it != g_futexQueues.end() && it->second == q) {
            g_futexQueues.erase(it);
        }
    }
}

// Futex command constants (Linux).
#define FUTEX_WAIT           0
#define FUTEX_WAKE           1
#define FUTEX_REQUEUE        3
#define FUTEX_CMP_REQUEUE    4
#define FUTEX_WAIT_BITSET    9
#define FUTEX_WAKE_BITSET    10
#define FUTEX_PRIVATE_FLAG   128
#define FUTEX_CLOCK_REALTIME 256
#define FUTEX_BITSET_MATCH_ANY 0xffffffff

extern "C" int bionic_futex(uint32_t *uaddr, int futex_op, uint32_t val, const struct timespec *timeout, uint32_t *uaddr2, uint32_t val3) {
    int cmd = futex_op & 127; // remove private flag
    (void)uaddr2;

    if (cmd == FUTEX_WAIT || cmd == FUTEX_WAIT_BITSET) {
        // FUTEX_WAIT_BITSET with a bitset of 0 is invalid.
        if (cmd == FUTEX_WAIT_BITSET && val3 == 0) { errno = EINVAL; return -1; }

        std::shared_ptr<FutexWaitQueue> q;
        {
            std::unique_lock<std::mutex> lock(g_futexGlobalMtx);
            auto& slot = g_futexQueues[uaddr];
            if (!slot) slot = std::make_shared<FutexWaitQueue>();
            q = slot;
            q->waiters++;
        }

        std::unique_lock<std::mutex> qLock(q->mtx);

        // Linux: nu *uaddr != val khi vo wait → EAGAIN ngay.
        if (*uaddr != val) {
            qLock.unlock(); // th queue-mtx before khi ly global (th t kha)
            futex_leave(uaddr, q);
            errno = EAGAIN;
            return -1;
        }

        if (timeout) {
            // FUTEX_WAIT takes a RELATIVE timeout. FUTEX_WAIT_BITSET takes an
            // ABSOLUTE deadline. This treated BOTH as absolute, and the difference is
            // not subtle on a device that has been up for a while.
            //
            // Unity's asset threads pass FUTEX_WAIT (op=128 = FUTEX_WAIT |
            // FUTEX_PRIVATE_FLAG) with a small relative timeout — tens of
            // milliseconds, so tv_sec is 0. Subtracting CLOCK_MONOTONIC from that
            // gave remSec = 0 - 528730 on a phone six days into its uptime, so every
            // wait returned ETIMEDOUT before waiting at all and the guest spun
            // instead of progressing. When tv_sec was large enough to survive the
            // subtraction the other branch ran and the 24h cap parked the thread for
            // a day. Either way AssetGarbageCollectorHelper never came back, and the
            // log ended on the prctl that named it.
            const bool absolute = (cmd == FUTEX_WAIT_BITSET);

            // The clock only matters for an absolute deadline. FUTEX_CLOCK_REALTIME
            // selects CLOCK_REALTIME; without it the deadline is CLOCK_MONOTONIC.
            const bool realtime = (futex_op & FUTEX_CLOCK_REALTIME) != 0;
            const clockid_t deadline_clock = realtime ? CLOCK_REALTIME : CLOCK_MONOTONIC;

            const BlockingWaitScope tracked(WaitKind::kFutexTimed, uaddr,
                                            guest_return_address(6));

            if (!absolute) {
                // Relative: wait exactly as long as asked. A negative or absurd value
                // is rejected the way the kernel does rather than being clamped into a
                // long sleep.
                if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
                    timeout->tv_nsec >= 1000000000) {
                    qLock.unlock();
                    futex_leave(uaddr, q);
                    errno = EINVAL;
                    return -1;
                }
                // Tell the registry how long this wait asked for. An idle worker that
                // requested a long timeout and is inside it is not stalled, and
                // reporting it as such is worse than saying nothing: the captured
                // ULTRAKILL log's only stall line was exactly that, an
                // AssetGarbageCollectorHelper doing what it asked to do, while the
                // wedged main thread went unmentioned.
                blocking_wait_note_budget(
                    static_cast<uint64_t>(timeout->tv_sec) * 1000ull +
                    static_cast<uint64_t>(timeout->tv_nsec) / 1000000ull);
                const auto duration = std::chrono::seconds(timeout->tv_sec) +
                                      std::chrono::nanoseconds(timeout->tv_nsec);
                if (q->cv.wait_for(qLock, duration) == std::cv_status::timeout) {
                    qLock.unlock();
                    futex_leave(uaddr, q);
                    errno = ETIMEDOUT;
                    return -1;
                }
            } else {
                // Absolute: re-derive the remaining time on every pass. Waiting on a
                // capped slice and looping keeps a far-future deadline from either
                // overflowing the duration or being silently shortened to the cap —
                // the previous code turned a distant deadline into a 24-hour sleep and
                // then reported a timeout that had not happened.
                for (bool first = true;; first = false) {
                    struct timespec now;
                    ::clock_gettime(deadline_clock, &now);
                    int64_t remSec = int64_t(timeout->tv_sec) - int64_t(now.tv_sec);
                    int64_t remNs = int64_t(timeout->tv_nsec) - int64_t(now.tv_nsec);
                    if (remNs < 0) { remSec -= 1; remNs += 1000000000; }
                    if (remSec < 0) {
                        qLock.unlock();
                        futex_leave(uaddr, q);
                        errno = ETIMEDOUT;
                        return -1;
                    }
                    // The budget is the time remaining ON THE FIRST PASS, which is the
                    // total the caller asked for. Refreshing it every pass would be
                    // wrong: the remaining time shrinks as elapsed time grows, so the
                    // two would cross at half the requested wait and the report would
                    // fire while the wait was still legitimate.
                    if (first) {
                        blocking_wait_note_budget(static_cast<uint64_t>(remSec) * 1000ull +
                                                  static_cast<uint64_t>(remNs) / 1000000ull);
                    }
                    int64_t sliceSec = remSec;
                    int64_t sliceNs = remNs;
                    const bool partial = sliceSec > 86400;
                    if (partial) { sliceSec = 86400; sliceNs = 0; }
                    const auto duration = std::chrono::seconds(sliceSec) +
                                          std::chrono::nanoseconds(sliceNs);
                    if (q->cv.wait_for(qLock, duration) != std::cv_status::timeout) {
                        break;  // woken (or spurious — the guest re-checks *uaddr)
                    }
                    if (!partial) {
                        qLock.unlock();
                        futex_leave(uaddr, q);
                        errno = ETIMEDOUT;
                        return -1;
                    }
                    // A slice expired but the deadline has not: keep waiting.
                }
            }
        } else {
            // No timeout: this returns only when someone wakes it. If the wake never
            // comes the thread is parked for the life of the process, which is what
            // the registry exists to make visible.
            const BlockingWaitScope tracked(WaitKind::kFutex, uaddr,
                                            guest_return_address(6));
            q->cv.wait(qLock);
        }
        qLock.unlock();
        futex_leave(uaddr, q);
        return 0;
    } else if (cmd == FUTEX_WAKE || cmd == FUTEX_WAKE_BITSET) {
        std::unique_lock<std::mutex> lock(g_futexGlobalMtx);
        auto it = g_futexQueues.find(uaddr);
        if (it != g_futexQueues.end()) {
            std::unique_lock<std::mutex> qLock(it->second->mtx);
            lock.unlock();
            if (val == 1) it->second->cv.notify_one();
            else it->second->cv.notify_all();
            return val;
        }
        return 0;
    } else if (cmd == FUTEX_REQUEUE || cmd == FUTEX_CMP_REQUEUE) {
        // FUTEX_CMP_REQUEUE requires *uaddr == val3 before requeueing.
        if (cmd == FUTEX_CMP_REQUEUE && *uaddr != val3) { errno = EAGAIN; return -1; }

        std::unique_lock<std::mutex> lock(g_futexGlobalMtx);
        auto it = g_futexQueues.find(uaddr);
        if (it != g_futexQueues.end()) {
            std::unique_lock<std::mutex> qLock(it->second->mtx);
            lock.unlock();
            if (val == 1) it->second->cv.notify_one();
            else it->second->cv.notify_all();
            return val;
        }
        return 0;
    }
    errno = ENOSYS;
    return -1;
}

// Dynamic Loading (dlfcn): serialize ANGLE/MoltenVK loads; concurrent Metal init aborts.
extern "C" void* bionic_dlopen(const char* filename, int flags) {
    static std::mutex g_gpuFrameworkMtx;
    (void)flags;
    logAndroidMessage(4, "KuDroidSyscall", std::string("bionic_dlopen: requested ") + (filename ? filename : "NULL"));

    if (!filename) {
        return RTLD_DEFAULT;
    }

    // Try a real host dlopen first for anything that might genuinely exist.
    void* real = ::dlopen(filename, flags ? flags : RTLD_NOW);
    if (real) {
        logAndroidMessage(4, "KuDroidSyscall", std::string("bionic_dlopen: resolved to real host handle for ") + filename);
        return real;
    }

    // Map Android GPU library requests directly to the embedded iOS frameworks.
    // We return the EXACT handle from iOS dlopen so that bionic_dlsym can search 
    // exactly within that framework's namespace, completely bypassing RTLD_DEFAULT issues.
    if (strstr(filename, "libEGL.so") || strstr(filename, "libGLESv2.so") || 
        strstr(filename, "libGLESv1_CM.so") || strstr(filename, "libGLESv3.so")) {
        // EGL and GLES are combined in the ANGLE frameworks, usually we'd load both, 
        // but EGL is enough for eglGetProcAddress, or we can just load the specific one requested.
        const char* fw = strstr(filename, "libEGL") ? "libEGL.framework/libEGL"
                                                    : "libGLESv2.framework/libGLESv2";

        std::lock_guard<std::mutex> gpuLock(g_gpuFrameworkMtx);
        // Goes through the shared loader so @loader_path is tried first: under
        // LiveContainer the guest runs as a dylib and @executable_path points at
        // LiveContainer's directory, not at the bundle holding these frameworks.
        void* handle = kudroid::dlopen_bundled_framework(fw, RTLD_NOW | RTLD_GLOBAL);
        if (handle) {
            logAndroidMessage(4, "KuDroidGPU", std::string("Successfully loaded ") + fw);
            return handle;
        }
    }
    
    if (strstr(filename, "libvulkan.so")) {
        std::lock_guard<std::mutex> gpuLock(g_gpuFrameworkMtx);
        void* handle = kudroid::dlopen_bundled_framework("MoltenVK.framework/MoltenVK",
                                                         RTLD_NOW | RTLD_GLOBAL);
        if (handle) {
            logAndroidMessage(4, "KuDroidGPU", "Successfully loaded MoltenVK.framework");
            return handle;
        }
    }

#define DUMMY_HANDLE ((void*)0x4B5544524F494421ULL) // "KUDROID!" as a handle

    // A guest .so LibraryManager already mapped gets its OWN handle, so a later
    // dlsym searches that library and nothing else.
    //
    // Without this the call fell through to DUMMY_HANDLE, where dlsym scans every
    // loaded guest library and returns the first match. Two libraries exporting the
    // same symbol then resolve to whichever sorts first, which is not what a caller
    // that named one library asked for. GameActivity does exactly that: it dlopens the
    // .so from android.app.lib_name and pulls its entry points out of that handle.
    if (kudroid_guest_library_open != nullptr) {
        if (void* guest = kudroid_guest_library_open(filename)) {
            logAndroidMessage(4, "KuDroidSyscall",
                              std::string("bionic_dlopen: resolved to guest library handle for ") + filename);
            return guest;
        }
    }

    // Emulate the Android linker: pretend the requested library resolved.
    logAndroidMessage(4, "KuDroidSyscall", std::string("bionic_dlopen: fallback returning DUMMY_HANDLE for ") + filename);
    return DUMMY_HANDLE;
}

// GraphicsShim MUST intercept Vulkan/EGL surface symbols BEFORE the real
// MoltenVK/ANGLE handle is consulted, otherwise the game gets the real
// vkGetInstanceProcAddr and never sees vkCreateAndroidSurfaceKHR. Every
// egl*/ANativeWindow* entry also goes through the shim: the wrappers forward
// to ANGLE internally but add NULL-guards and detailed logging (e.g.
// TriangleGLES calls eglInitialize(display, 0, 0) with NULL out-params which
// some ANGLE builds dereference). Anything not in the shim table falls
// through to the real handle below.
static bool isShimPriority(const char* s) {
    if (!s) return false;
    if (strncmp(s, "egl", 3) == 0) return true;
    if (strncmp(s, "gl", 2) == 0) return true;
    if (strncmp(s, "ANativeWindow", 13) == 0) return true;
    return strcmp(s, "vkGetInstanceProcAddr") == 0 ||
           strcmp(s, "vkEnumerateInstanceExtensionProperties") == 0 ||
           strcmp(s, "vkCreateAndroidSurfaceKHR") == 0;
}

extern "C" void* bionic_dlsym(void* handle, const char* symbol) {
    if (!symbol) return nullptr;

    if (isShimPriority(symbol)) {
        size_t count = 0;
        const SymbolEntry* symbols = get_graphics_symbols(&count);
        for (size_t i = 0; i < count; ++i) {
            if (strcmp(symbols[i].name, symbol) == 0) {
                logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via GraphicsShim (priority)");
                return symbols[i].address;
            }
        }
    }

    // Direct routing for graphics API prefixes
    if (symbol[0] == 'g' && symbol[1] == 'l') {
        if (void* f = kudroid::get_gl_func(symbol)) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via GraphicsShim (gl)");
            return f;
        }
    }
    if (symbol[0] == 'e' && symbol[1] == 'g' && symbol[2] == 'l') {
        if (void* f = kudroid::get_egl_func(symbol)) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via GraphicsShim (egl)");
            return f;
        }
    }
    if (symbol[0] == 'v' && symbol[1] == 'k') {
        if (void* f = kudroid::get_vk_func(symbol)) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via GraphicsShim (vk)");
            return f;
        }
    }

    // A guest library handle before ::dlsym, which would dereference a pointer it
    // does not own. The lookup validates the handle itself, so an unrelated one
    // simply returns null here.
    bool guest_handle = false;
    if (handle && handle != RTLD_DEFAULT && handle != DUMMY_HANDLE) {
        if (kudroid_guest_library_owns != nullptr && kudroid_guest_library_owns(handle)) {
            guest_handle = true;
            if (kudroid_guest_library_symbol != nullptr) {
                if (void* guest = kudroid_guest_library_symbol(handle, symbol)) {
                    logAndroidMessage(2, "KuDroidSyscall",
                                      std::string("bionic_dlsym: [") + symbol + "] found in guest library handle");
                    return guest;
                }
            }
        }
    }

    // For a real host handle, prefer its own symbols first. A guest handle is
    // excluded: it is a LibraryManager pointer, and ::dlsym would dereference it as
    // one of its own — a SIGSEGV rather than a miss.
    if (handle && handle != RTLD_DEFAULT && handle != DUMMY_HANDLE && !guest_handle) {
        if (void* real = ::dlsym(handle, symbol)) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] found in real handle");
            return real;
        }
    }

    // Route through the shim symbol tables (syscall/graphics/input/audio).
    size_t count = 0;
    const SymbolEntry* symbols = get_syscall_symbols(&count);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(symbols[i].name, symbol) == 0) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via SyscallShim");
            return symbols[i].address;
        }
    }
    symbols = get_graphics_symbols(&count);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(symbols[i].name, symbol) == 0) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via GraphicsShim");
            return symbols[i].address;
        }
    }
    symbols = get_input_symbols(&count);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(symbols[i].name, symbol) == 0) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via InputShim");
            return symbols[i].address;
        }
    }
    symbols = get_audio_symbols(&count);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(symbols[i].name, symbol) == 0) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via AudioShim");
            return symbols[i].address;
        }
    }

    // If it was a dummy handle, do NOT search RTLD_DEFAULT globally.
    // Fall back to guest libs; Android libs like libc.so have no host counterpart.
    if (handle == DUMMY_HANDLE) {
        if (kudroid_guest_symbol_lookup) {
            if (void* guest = kudroid_guest_symbol_lookup(symbol)) {
                // Distinguish a real resolution from the universal dummy.
                if (is_universal_dummy(guest)) {
                    logAndroidMessage(5, "KuDroidSyscall",
                                      std::string("bionic_dlsym: [") + symbol +
                                          "] NOT IMPLEMENTED — bound to the universal dummy, "
                                          "which takes no arguments and returns 0");
                } else {
                    logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via guest LibraryManager");
                }
                return guest;
            }
        }
        logAndroidMessage(5, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] NOT FOUND (in dummy handle)");
        return nullptr;
    }

    // Fall back to the host process image (ANGLE, MoltenVK, libc, ...).
    void* fallback = ::dlsym(RTLD_DEFAULT, symbol);
    if (fallback) {
        logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] resolved via RTLD_DEFAULT fallback");
    } else {
        logAndroidMessage(5, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] NOT FOUND");
    }
    return fallback;
}

extern "C" void* bionic_android_dlopen_ext(const char* filename, int flags, const void* extinfo) {
    (void)extinfo;
    return bionic_dlopen(filename, flags);
}

extern "C" int bionic_dlclose(void* handle) {
    // DUMMY_HANDLE is a fake handle for Android libs missing on the host.
    if (!handle || handle == RTLD_DEFAULT || handle == DUMMY_HANDLE) return 0;
    // A guest library handle is ours, not the host loader's. Guest mappings live for
    // the life of the process on purpose (unmapping one while another thread executes
    // inside it is the crash that made LibraryManager a process-wide singleton), so
    // report success without touching the mapping.
    if (kudroid_guest_library_owns != nullptr && kudroid_guest_library_owns(handle)) {
        return 0;
    }
    return ::dlclose(handle);
}

extern "C" char* bionic_dlerror() {
    return ::dlerror();
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

// Fake fds (emulated inotify/signalfd) — bionic_close must not leak them.
static std::set<int> g_fakefds;
static std::mutex g_fakefds_mtx;


extern "C" int bionic_eventfd(unsigned int initval, int flags) {
#ifdef __APPLE__
    (void)flags;
    int fd = create_loopback_udp();
    if (fd >= 0 && initval > 0) {
        uint64_t val = initval;
        write(fd, &val, sizeof(val));
    }
    if (fd >= 0) {
        // Emulated fd (loopback UDP) — register bionic_close bookkeeping y .
        std::lock_guard<std::mutex> lock(g_fakefds_mtx);
        g_fakefds.insert(fd);
    }
    return fd;
#else
    return ::eventfd(initval, flags);
#endif
}

extern "C" int bionic_timerfd_create(int clockid, int flags) {
#ifdef __APPLE__
    (void)clockid; (void)flags;
    const int fd = create_loopback_udp();
    if (fd >= 0) {
        // Emulated fd (loopback UDP + GCD timer) — register bionic_close
        // bookkeeping y (ging inotify/signalfd).
        std::lock_guard<std::mutex> lock(g_fakefds_mtx);
        g_fakefds.insert(fd);
    }
    return fd;
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

// close() wrapper — cleans up timerfd GCD timers when the fd is closed.
extern "C" int bionic_close(int fd) {
#ifdef __APPLE__
    {
        std::lock_guard<std::mutex> lock(g_timerfds_mtx);
        auto it = g_timerfds.find(fd);
        if (it != g_timerfds.end()) {
            dispatch_source_cancel(it->second);
            dispatch_release(it->second);
            g_timerfds.erase(it);
        }
    }
#endif
    // Drop ashmem prot/region entries; fake fds also need munmap.
    {
        std::lock_guard<std::mutex> lock(g_ashmem_mtx);
        const bool is_fake = g_ashmem_region.find(fd) != g_ashmem_region.end();
        if (is_fake) {
            ashmem_forget(fd);
            return 0; // fake fd is not a real fd — do not close it.
        }
        g_ashmem_prot.erase(fd);
    }
    {
        // Fake inotify/signalfd fd — close the real socket.
        std::lock_guard<std::mutex> lock(g_fakefds_mtx);
        g_fakefds.erase(fd);
    }
    return ::close(fd);
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
    
    // We fetch maxevents * 2 because read and write events for the same FD are separated in kqueue.
    // Use a bounded stack buffer for small requests, heap for large ones.
    const int kStackEvents = 64;
    struct kevent stackBuf[kStackEvents * 2];
    std::vector<struct kevent> heapBuf;
    struct kevent* evlist = stackBuf;
    if (maxevents > kStackEvents) {
        heapBuf.resize(static_cast<size_t>(maxevents) * 2);
        evlist = heapBuf.data();
    }
    
    struct timespec ts;
    struct timespec* ts_ptr = NULL;
    if (timeout >= 0) {
        ts.tv_sec = timeout / 1000;
        ts.tv_nsec = (timeout % 1000) * 1000000;
        ts_ptr = &ts;
    }
    
    // timeout < 0 means "wait indefinitely", which is the case worth naming: a guest
    // event loop whose fd never becomes ready sits here for the life of the process.
    // A bounded wait is left untracked, because a report for something that will end
    // on its own is noise.
    int n;
    if (timeout < 0) {
        const BlockingWaitScope tracked(WaitKind::kEpoll,
                                        reinterpret_cast<const void*>(
                                            static_cast<uintptr_t>(epfd)),
                                        guest_return_address(6));
        n = kevent(epfd, NULL, 0, evlist, maxevents * 2, ts_ptr);
    } else {
        n = kevent(epfd, NULL, 0, evlist, maxevents * 2, ts_ptr);
    }
    int unique_events = 0;
    
    if (n > 0) {
        // Coalesce by fd, not udata; one fd may report both read+write.
        std::vector<uint64_t> order;
        std::unordered_map<uint64_t, std::pair<uint32_t, uint64_t>> coalesced;
        for (int i = 0; i < n; i++) {
            const uint64_t fd = static_cast<uint64_t>(evlist[i].ident);
            uint32_t flags = 0;
            if (evlist[i].filter == EVFILT_READ) flags |= EPOLLIN;
            else if (evlist[i].filter == EVFILT_WRITE) flags |= EPOLLOUT;
            if (evlist[i].flags & EV_ERROR) flags |= EPOLLERR;
            if (evlist[i].flags & EV_EOF) flags |= EPOLLHUP;
            auto& entry = coalesced[fd];
            if (std::find(order.begin(), order.end(), fd) == order.end()) {
                order.push_back(fd); // keep first-seen order
            }
            entry.first |= flags;
            entry.second = reinterpret_cast<uint64_t>(evlist[i].udata);
        }
        
        for (const uint64_t fd : order) {
            if (unique_events >= maxevents) break;
            events[unique_events].events = coalesced[fd].first;
            events[unique_events].data = coalesced[fd].second;
            unique_events++;
        }
    }
    return n >= 0 ? unique_events : -1;
#else
    if (timeout < 0) {
        const BlockingWaitScope tracked(WaitKind::kEpoll,
                                        reinterpret_cast<const void*>(
                                            static_cast<uintptr_t>(epfd)),
                                        guest_return_address(6));
        return ::epoll_wait(epfd, reinterpret_cast<struct epoll_event*>(events_ptr), maxevents, timeout);
    }
    return ::epoll_wait(epfd, reinterpret_cast<struct epoll_event*>(events_ptr), maxevents, timeout);
#endif
}

extern "C" int bionic_pthread_condattr_init(void* attr) { (void)attr; return 0; }
extern "C" int bionic_pthread_condattr_destroy(void* attr) { (void)attr; return 0; }
extern "C" int bionic_pthread_mutexattr_init(void* attr) {
    // Must clear the kind bits. A guest typically declares pthread_mutexattr_t on
    // the stack, so leaving it untouched let pthread_mutex_init read stack garbage
    // as the mutex kind — a NORMAL mutex became RECURSIVE or vice versa depending
    // on what happened to be in that frame. Only the low 32 bits are written: the
    // shim reads nothing above them, and bionic's own type is wider.
    auto* p = static_cast<uint32_t*>(attr);
    if (!p) return EINVAL;
    *p = 0;  // PTHREAD_MUTEX_DEFAULT
    return 0;
}
extern "C" int bionic_pthread_mutexattr_destroy(void* attr) { (void)attr; return 0; }
extern "C" int bionic_pthread_mutexattr_settype(void* attr, int type) {
    // Store the kind in the guest attr's low 2 bits.
    auto* p = static_cast<uint32_t*>(attr);
    if (!p) return -1;
    *p = (*p & ~0x3u) | (static_cast<uint32_t>(type) & 0x3u);
    return 0;
}

extern "C" int bionic_pthread_mutexattr_gettype(void* attr, int* type) {
    const auto* p = static_cast<const uint32_t*>(attr);
    if (!p || !type) return EINVAL;
    *type = static_cast<int>(*p & 0x3u);
    return 0;
}


extern "C" int bionic_pthread_mutex_trylock(void* guestMutex) {
    HostMutex* host = host_mutex_for(guestMutex);
    if (!host) return EINVAL;
    const int rc = ::pthread_mutex_trylock(&host->mutex);
    if (rc == 0 && host->track_owner) {
        host->owner.store(current_thread_id(), std::memory_order_relaxed);
    }
    return rc;
}

extern "C" int bionic_pthread_key_create(void* guestKey, void (*destructor)(void*)) {
    if (!guestKey) return -1;
    pthread_key_t hostKey;
    int res = ::pthread_key_create(&hostKey, destructor);
    if (res == 0) {
        // Bionic pthread_key_t is a 32-bit int, while Darwin's pthread_key_t is
        // 64-bit. memcpy(sizeof(pthread_key_t)) would overflow the guest's
        // 4-byte key slot. Darwin keys are small integers, so truncation is safe.
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

// Bionic pthread_once: 0 = not run, 1 = running, 2 = done. Per-word mutex
// avoids deadlock on nested calls with different control words.
struct BionicOnceControl {
    std::mutex mtx;
    std::condition_variable cv;
};
static std::mutex g_once_map_mtx;
static std::unordered_map<int*, std::shared_ptr<BionicOnceControl>> g_once_controls;

static int once_state_load(int* guest_once) {
    return __atomic_load_n(guest_once, __ATOMIC_ACQUIRE);
}

static void once_state_store(int* guest_once, int state) {
    __atomic_store_n(guest_once, state, __ATOMIC_RELEASE);
}

extern "C" int bionic_pthread_once(int* guest_once, void (*init_routine)(void)) {
    if (!guest_once || !init_routine) return -1;

    // Fast path: already done.
    if (once_state_load(guest_once) == 2) return 0;

    std::shared_ptr<BionicOnceControl> ctl;
    {
        std::lock_guard<std::mutex> lock(g_once_map_mtx);
        auto& slot = g_once_controls[guest_once];
        if (!slot) slot = std::make_shared<BionicOnceControl>();
        ctl = slot;
    }

    std::unique_lock<std::mutex> lock(ctl->mtx);
    // Another thread is running init for this word → wait.
    if (once_state_load(guest_once) == 1) {
        // An initialiser that never finishes parks every other thread that needs the
        // same static, and nothing else in the log would say which one.
        const BlockingWaitScope tracked(WaitKind::kOnce, guest_once,
                                        guest_return_address(6));
        while (once_state_load(guest_once) == 1) {
            ctl->cv.wait(lock);
        }
    }
    // Another thread finished while we waited.
    if (once_state_load(guest_once) == 2) return 0;

    // We run first: mark in-progress and run init; other words use other mutexes.
    once_state_store(guest_once, 1);
    init_routine();
    once_state_store(guest_once, 2);
    ctl->cv.notify_all();

    // Best-effort map cleanup; later calls hit the fast path.
    std::lock_guard<std::mutex> mapLock(g_once_map_mtx);
    auto it = g_once_controls.find(guest_once);
    if (it != g_once_controls.end() && it->second == ctl) {
        g_once_controls.erase(it);
    }
    return 0;
}

#include <signal.h>

// Guest signal handling lives in GuestSignals.cpp.
//
// It used to live here, in a `struct android_sigaction` that declared bionic's ILP32
// field order on an LP64 guest. Both layouts are 32 bytes, so nothing failed a size
// check; what happened instead is that sa_flags was read as the handler pointer.
// ULTRAKILL's main thread ended up executing at pc=0x18000004 — which is
// SA_SIGINFO|SA_ONSTACK|SA_RESTART, the flags the guest had passed — spinning at 100%
// of one core inside _sigtramp, because every fault re-entered the same bad handler.
// The same bug also replaced KuDroid's SIGSEGV handler with that value, which is why
// the run produced no crash log at all.
//
// Three separate translations are needed and none of them is optional: the struct
// layout, the signal NUMBER (Linux and Darwin diverge after SIGFPE — guest SIGUSR1 is
// 10, which is Darwin's SIGBUS), and ownership of the signals KuDroid needs to keep
// working (SIGTRAP supplies guest TLS, SIGSYS emulates a raw `svc`). Doing that in one
// place, with the layouts pinned by static_assert, is the point.
extern "C" int bionic_sigaction(int signum, const void* act, void* oldact) {
    return kudroid::guest_sigaction(signum,
                                    static_cast<const kudroid::GuestSigaction*>(act),
                                    static_cast<kudroid::GuestSigaction*>(oldact));
}

// rt_sigaction is what a guest reaches through syscall(). Same call: bionic's
// sigaction() is a thin wrapper over it, and the sigset size argument only matters for
// ILP32, where sigset_t is too small for the real-time signals.
extern "C" int bionic_rt_sigaction(int signum, const void* act, void* oldact,
                                   size_t sigsetsize) {
    if (sigsetsize != sizeof(uint64_t)) {
        errno = EINVAL;
        return -1;
    }
    return bionic_sigaction(signum, act, oldact);
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

// Guest TLS block size and bionic offsets (arm64).
// Thread pointer (tpidr_el0) points into the slot area; slot N is tpidr + N*8.
constexpr size_t kTlsBlockSize    = 65536;
constexpr size_t kTlsSlotOffset   = 32768; // TP = tls_base + kTlsSlotOffset
constexpr size_t kTlsModuleOffset = 4096;  // guest TLS template offset relative to TP
constexpr size_t kTlsStackGuardSlotOffset = 40; // slot 5

// Guest module TLS template (PT_TLS), registered by elf_loader after mapping.
static const void* g_tls_template = nullptr;
static size_t g_tls_template_size = 0;
static std::mutex g_tls_template_mtx;

extern "C" void kudroid_tls_set_template(const void* tls_template, size_t tls_filesz) {
    std::lock_guard<std::mutex> lock(g_tls_template_mtx);
    g_tls_template = tls_template;
    g_tls_template_size = tls_filesz;
}

extern "C" size_t kudroid_tls_module_offset(void) {
    return kTlsModuleOffset;
}

// Allocate a guest TLS block: zero it, copy the TLS template, set guard cookie.
// Shared by the main thread, new threads, and lazy allocation in the trap handler.
static void* alloc_guest_tls_block(void) {
    void* tls_base = std::aligned_alloc(16, kTlsBlockSize);
    if (!tls_base) return nullptr;
    std::memset(tls_base, 0, kTlsBlockSize);

    {
        std::lock_guard<std::mutex> lock(g_tls_template_mtx);
        if (g_tls_template && g_tls_template_size > 0 &&
            g_tls_template_size <= kTlsBlockSize - kTlsSlotOffset - kTlsModuleOffset) {
            std::memcpy(static_cast<char*>(tls_base) + kTlsSlotOffset + kTlsModuleOffset,
                        g_tls_template, g_tls_template_size);
        }
    }

    // Stack guard cookie at slot 5 (offset 40 from TP).
    *reinterpret_cast<uint64_t*>(static_cast<char*>(tls_base) + kTlsSlotOffset + kTlsStackGuardSlotOffset) =
        kStackGuardCookie;
    return tls_base;
}

struct BionicThreadArgs {
    void* (*start_routine)(void*);
    void* arg;
};

static void* bionic_thread_wrapper(void* rawArgs);

extern "C" void bionic_init_main_thread_tls(void) {
    ::pthread_once(&tls_key_once, init_tls_key);
    if (::pthread_getspecific(tls_key)) return; // already has TLS
    void* tls_base = alloc_guest_tls_block();
    if (!tls_base) return;
    
    char* tls_ptr = static_cast<char*>(tls_base) + kTlsSlotOffset;
    (void)tls_ptr;
    
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
    
    // Diagnostic (gated): runs per new thread — log only when investigating TLS.
    if (guard_diag_enabled()) {
        fprintf(stderr, "[TLS_DIAG] tls_base=%p tls_ptr=%p\n", reinterpret_cast<void*>(tls_base), reinterpret_cast<void*>(tls_ptr));
        fprintf(stderr, "[TLS_DIAG] tpidr_el0 BEFORE=0x%llx AFTER=0x%llx\n", 
                (unsigned long long)old_tpidr, (unsigned long long)new_tpidr);
        fprintf(stderr, "[TLS_DIAG] stack_guard@offset40=0x%llx (expect 0x1337BEEFCAFECAFE)\n",
                (unsigned long long)guard_check);
        fprintf(stderr, "[TLS_DIAG] write %s\n", 
                (new_tpidr == (uint64_t)tls_ptr) ? "SUCCESS" : "FAILED");
    }
#endif
}

} // namespace
} // namespace kudroid

namespace kudroid {
bool bionic_handle_tpidr_trap(void* ucontext) {
    (void)ucontext;
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

            ::pthread_once(&tls_key_once, init_tls_key);
            void* tls_base = ::pthread_getspecific(tls_key);
            if (!tls_base) {
                // Lazy allocation for host-created threads running guest code.
                // Synchronous trap, so malloc here is safe; freed at thread exit.
                tls_base = alloc_guest_tls_block();
                if (tls_base) {
                    ::pthread_setspecific(tls_key, tls_base);
                }
            }
            char* tls_ptr = tls_base ? (static_cast<char*>(tls_base) + kTlsSlotOffset) : nullptr;
            
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
    sync_diag("thread-entry", reinterpret_cast<void*>(start_routine), nullptr, 0);

    ::pthread_once(&tls_key_once, init_tls_key);

    // Allocate 64KB for Android TLS block and set tpidr_el0
    // Darwin uses tpidrro_el0, so tpidr_el0 is free for us!
    // Never fall back to plain aligned_alloc: it lacks the TLS template and guard cookie.
    void* tls_base = alloc_guest_tls_block();
    if (tls_base) {
        ::pthread_setspecific(tls_key, tls_base);
#if defined(__aarch64__)
        __asm__ volatile("msr tpidr_el0, %0" : : "r"((char*)tls_base + kTlsSlotOffset));
#endif
    }

    // iOS: ANGLE/Metal/ObjC require an autorelease pool on EVERY thread that
    // touches them. Guest render threads are raw pthreads with NO pool — GPU test passes because
    // it runs on the main/GCD thread (pool present). Without a pool → abort()
    // inside ANGLE eglInitialize (Metal device/queue creation).
#if defined(__APPLE__)
    void* pool = objc_autoreleasePoolPush();
#endif
    void* result = start_routine(arg);
#if defined(__APPLE__)
    objc_autoreleasePoolPop(pool);
#endif

    // No need to free(tls_base) here, the destructor will handle it automatically
    // when the thread terminates, even if it terminates via pthread_exit().
    sync_diag("thread-exit", reinterpret_cast<void*>(start_routine), nullptr, 0);
    return result;
}

extern "C" int bionic_pthread_create(pthread_t* thread, void* attr, void* (*start_routine)(void*), void* arg) {
    BionicThreadArgs* args = new BionicThreadArgs{start_routine, arg};
    if (!attr) {
        const int res = ::pthread_create(thread, nullptr, bionic_thread_wrapper, args);
        if (res != 0) delete args;
        sync_diag("thread-create", reinterpret_cast<void*>(start_routine), nullptr, res);
        return res;
    }

    // Forward stack size + detach state from the guest attr when present.
    const auto* a = static_cast<const BionicPthreadAttr*>(attr);
    pthread_attr_t hostAttr;
    ::pthread_attr_init(&hostAttr);
    if (a->stack_size != 0) {
        ::pthread_attr_setstacksize(&hostAttr, a->stack_size);
    }
    if (a->flags & 0x1) { // PTHREAD_CREATE_DETACHED
        ::pthread_attr_setdetachstate(&hostAttr, PTHREAD_CREATE_DETACHED);
    }
    const int res = ::pthread_create(thread, &hostAttr, bionic_thread_wrapper, args);
    ::pthread_attr_destroy(&hostAttr);
    if (res != 0) delete args;
    sync_diag("thread-create", reinterpret_cast<void*>(start_routine), nullptr, res);
    return res;
}

// pthread_join blocks until the target exits; track it as a join wait.
extern "C" int bionic_pthread_join(pthread_t thread, void** retval) {
    const BlockingWaitScope tracked(WaitKind::kJoin, reinterpret_cast<const void*>(thread),
                                    guest_return_address(6));
    return ::pthread_join(thread, retval);
}

// errno accessor: Darwin exports `__error`, glibc/musl export `__errno_location`.
// Guarded so SyscallShim.o actually links on a Linux host (previously any host
// consumer that pulled this object failed with an undefined `__error`).
#ifdef __APPLE__
extern "C" int* __error(void);
#else
extern "C" int* __errno_location(void);
#endif

// memalign — aligned memory allocation.
extern "C" void* bionic_memalign(size_t alignment, size_t size) {
    void* ptr = nullptr;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    if (::posix_memalign(&ptr, alignment, size) != 0) return nullptr;
    return ptr;
}

// __system_property_* — Android property system.
// prop_info is an opaque pointer; find returns it and read/callback consume it.
namespace {
struct KudroidProp {
    const char* name;
    const char* value;
};
const KudroidProp kKnownProps[] = {
    // Version and identity come from DeviceProfile.h so all surfaces agree.
    {"ro.build.version.sdk", KUDROID_SDK_INT_STR},
    {"ro.build.version.release", KUDROID_ANDROID_RELEASE},
    {"ro.build.version.codename", "REL"},
    {"ro.build.version.incremental", "6000000"},
    {"ro.build.type", "user"},
    {"ro.build.tags", "release-keys"},
    {"ro.build.fingerprint",
     KUDROID_DEVICE_BRAND "/" KUDROID_DEVICE_NAME "/" KUDROID_DEVICE_BOARD
     ":" KUDROID_ANDROID_RELEASE "/QP1A.190711.020/6000000:user/release-keys"},
    // KuDroid, not a spoofed Pixel, so apps avoid device-specific paths.
    {"ro.product.model", KUDROID_DEVICE_MODEL},
    {"ro.product.manufacturer", KUDROID_DEVICE_MANUFACTURER},
    {"ro.product.brand", KUDROID_DEVICE_BRAND},
    {"ro.product.device", KUDROID_DEVICE_BOARD},
    {"ro.product.name", KUDROID_DEVICE_NAME},
    {"ro.product.cpu.abi", KUDROID_DEVICE_ABI},
    {"ro.product.cpu.abilist", KUDROID_DEVICE_ABI},
    {"ro.product.cpu.abilist64", KUDROID_DEVICE_ABI},
    {"ro.hardware", "kudroid"},
    {"ro.board.platform", "kudroid"},
    {"ro.boot.hardware", "kudroid"},
    {"ro.sf.lcd_density", "480"},
    {"ro.opengles.version", "196610"}, // OpenGL ES 3.2 (0x00030002)
    {"ro.debuggable", "0"},
    {"persist.sys.timezone", "UTC"},
    {"persist.sys.locale", "en-US"},
    {"sys.boot_completed", "1"},
    {"gsm.version.baseband", "1.0"},
};
constexpr size_t kKnownPropsCount = sizeof(kKnownProps) / sizeof(kKnownProps[0]);

const KudroidProp* findProp(const char* name) {
    if (!name) return nullptr;
    for (size_t i = 0; i < kKnownPropsCount; ++i) {
        if (std::strcmp(kKnownProps[i].name, name) == 0) return &kKnownProps[i];
    }
    return nullptr;
}
} // namespace

extern "C" int bionic_system_property_get(const char* name, char* value) {
    if (!name || !value) return 0;
    value[0] = '\0';
    const KudroidProp* p = findProp(name);
    if (!p) return 0;
    std::strcpy(value, p->value);
    return static_cast<int>(std::strlen(p->value));
}

extern "C" const void* bionic_system_property_find(const char* name) {
    return findProp(name); // prop_info* — nullptr when missing
}

// int __system_property_read(const prop_info* pi, char* name, char* value)
extern "C" int bionic_system_property_read(const void* pi, char* name, char* value) {
    if (!pi) return 0;
    const KudroidProp* p = static_cast<const KudroidProp*>(pi);
    if (name) std::strcpy(name, p->name);
    if (value) std::strcpy(value, p->value);
    return static_cast<int>(std::strlen(p->value));
}

extern "C" void bionic_system_property_read_callback(
    void* pi, void (*callback)(void*, const char*, const char*, unsigned), void* cookie) {
    if (!pi || !callback) return;
    const KudroidProp* p = static_cast<const KudroidProp*>(pi);
    callback(cookie, p->name, p->value, 0u);
}

// dl_iterate_phdr — needed by the guest unwinder to find its exception tables.
extern "C" int bionic_dl_iterate_phdr(
    int (*callback)(void* info, size_t size, void* data), void* data) {
    return kudroid_iterate_guest_phdrs(callback, data);
}

// lseek64 — 64-bit file seek (on Darwin/iOS lseek is already 64-bit)
extern "C" off_t bionic_lseek64(int fd, off_t offset, int whence) {
    return ::lseek(fd, offset, whence);
}

// The v*printf family — receives an already-built guest va_list.
#if defined(__aarch64__)
// `va_list` in these signatures is the HOST type, but the value a guest passes is a
// pointer to its own va_list — so it is reinterpreted rather than used. Taking the address
// of the parameter would give the address of a local copy of the pointer, one level too
// deep; the parameter's VALUE is the guest's pointer.
static const void* guestVaListPointer(va_list ap) {
    const void* p = nullptr;
    __builtin_memcpy(&p, &ap, sizeof(p));
    return p;
}

extern "C" int bionic_android_log_vprint(int prio, const char* tag, const char* fmt,
                                         va_list ap) {
    char message[1024];
    kudroid_format_guest_va_list(message, sizeof(message), fmt, guestVaListPointer(ap));
    return logAndroidMessage(prio, tag, message);
}

extern "C" int bionic_vsnprintf(char* s, size_t size, const char* format, va_list ap) {
    return static_cast<int>(
        kudroid_format_guest_va_list(s, size, format, guestVaListPointer(ap)));
}

// vsprintf has no bound; the guest promises the buffer is large enough, exactly as it does
// on Android. The cap only limits how far a runaway format string can go.
extern "C" int bionic_vsprintf(char* s, const char* format, va_list ap) {
    return static_cast<int>(
        kudroid_format_guest_va_list(s, 0x10000, format, guestVaListPointer(ap)));
}

extern "C" int bionic_vfprintf(FILE* stream, const char* format, va_list ap) {
    char buffer[2048];
    const size_t would =
        kudroid_format_guest_va_list(buffer, sizeof(buffer), format, guestVaListPointer(ap));
    std::fputs(buffer, stream ? stream : stderr);
    return static_cast<int>(would);
}

extern "C" int bionic_vprintf(const char* format, va_list ap) {
    return bionic_vfprintf(stdout, format, ap);
}

extern "C" int bionic_vasprintf(char** strp, const char* format, va_list ap) {
    if (strp == nullptr) return -1;
    // Format once to learn the length, exactly as the return contract allows, then once
    // more into a buffer that fits. The guest owns the result and frees it.
    char probe[1024];
    const void* guest = guestVaListPointer(ap);
    const size_t needed = kudroid_format_guest_va_list(probe, sizeof(probe), format, guest);
    char* buffer = static_cast<char*>(std::malloc(needed + 1));
    if (buffer == nullptr) { *strp = nullptr; return -1; }
    if (needed < sizeof(probe)) {
        __builtin_memcpy(buffer, probe, needed + 1);
    } else {
        kudroid_format_guest_va_list(buffer, needed + 1, format, guest);
    }
    *strp = buffer;
    return static_cast<int>(needed);
}

extern "C" int bionic___vsnprintf_chk(char* s, size_t maxlen, int flag, size_t slen,
                                      const char* format, va_list ap) {
    (void)flag; (void)slen;
    return static_cast<int>(
        kudroid_format_guest_va_list(s, maxlen, format, guestVaListPointer(ap)));
}

extern "C" int bionic___vsprintf_chk(char* s, int flag, size_t slen, const char* format,
                                     va_list ap) {
    (void)flag;
    return static_cast<int>(
        kudroid_format_guest_va_list(s, slen != 0 ? slen : 0x10000, format,
                                     guestVaListPointer(ap)));
}

extern "C" int bionic___vfprintf_chk(FILE* stream, int flag, const char* format,
                                     va_list ap) {
    (void)flag;
    return bionic_vfprintf(stream, format, ap);
}

// ── the scanf family ────────────────────────────────────────────────────────
//
// Same ABI split, opposite consequence: these WRITE through the guest's varargs. See
// GuestVarargs.h. The trampolines capture the registers; the character source for a FILE*
// lives here because GuestVarargs.cpp cannot include <cstdio>.
extern "C" int kudroid_sscanf_trampoline();
extern "C" int kudroid_isoc99_sscanf_trampoline();
extern "C" int kudroid_fscanf_trampoline();

namespace {
// ungetc is the only way to un-peek a FILE*, and it is guaranteed for one character —
// which is all the scanner's peek/advance interface needs.
int FilePeek(void* state) {
    FILE* f = static_cast<FILE*>(state);
    if (f == nullptr) return -1;
    const int c = std::fgetc(f);
    if (c == EOF) return -1;
    std::ungetc(c, f);
    return c;
}
void FileAdvance(void* state) {
    FILE* f = static_cast<FILE*>(state);
    if (f != nullptr) (void)std::fgetc(f);
}
}  // namespace

extern "C" int kudroid_fscanf_from_registers(const uint64_t* frame) {
    const auto* registers = reinterpret_cast<const GuestVarargs*>(frame);
    FILE* stream = reinterpret_cast<FILE*>(registers->gp[0]);
    const char* format = reinterpret_cast<const char*>(registers->gp[1]);
    if (stream == nullptr || format == nullptr) return -1;
    return ScanGuestVarargsFrom(&FilePeek, &FileAdvance, stream, format, registers,
                                /*firstGpIndex=*/2, /*firstFpIndex=*/0);
}

extern "C" int bionic_vsscanf(const char* s, const char* format, va_list ap) {
    return kudroid_scan_guest_va_list(s, format, guestVaListPointer(ap));
}

extern "C" int bionic_vfscanf(FILE* stream, const char* format, va_list ap) {
    if (stream == nullptr || format == nullptr) return -1;
    GuestVarargs registers;
    unsigned firstGp = 0;
    unsigned firstFp = 0;
    if (!GuestVarargsFromVaList(guestVaListPointer(ap), &registers, &firstGp, &firstFp)) {
        return -1;
    }
    return ScanGuestVarargsFrom(&FilePeek, &FileAdvance, stream, format, &registers, firstGp,
                                firstFp);
}
#else
extern "C" int bionic_android_log_vprint(int prio, const char* tag, const char* fmt, va_list ap) {
    char message[1024];
    std::vsnprintf(message, sizeof(message), fmt ? fmt : "", ap);
    return logAndroidMessage(prio, tag, message);
}

extern "C" int bionic_vsscanf(const char* s, const char* format, va_list ap) {
    return ::vsscanf(s, format, ap);
}

extern "C" int bionic_vfscanf(FILE* stream, const char* format, va_list ap) {
    return ::vfscanf(stream ? stream : stdin, format, ap);
}

extern "C" int bionic_vsnprintf(char* s, size_t size, const char* format, va_list ap) {
    return ::vsnprintf(s, size, format, ap);
}

extern "C" int bionic_vsprintf(char* s, const char* format, va_list ap) {
    return ::vsprintf(s, format, ap);
}

extern "C" int bionic_vfprintf(FILE* stream, const char* format, va_list ap) {
    return ::vfprintf(stream ? stream : stderr, format, ap);
}

extern "C" int bionic_vprintf(const char* format, va_list ap) {
    return ::vprintf(format, ap);
}

extern "C" int bionic_vasprintf(char** strp, const char* format, va_list ap) {
    return ::vasprintf(strp, format, ap);
}

extern "C" int bionic___vsnprintf_chk(char* s, size_t maxlen, int flag, size_t slen,
                                      const char* format, va_list ap) {
    (void)flag; (void)slen;
    return ::vsnprintf(s, maxlen, format, ap);
}

extern "C" int bionic___vsprintf_chk(char* s, int flag, size_t slen, const char* format,
                                     va_list ap) {
    (void)flag;
    return ::vsnprintf(s, slen, format, ap);
}

extern "C" int bionic___vfprintf_chk(FILE* stream, int flag, const char* format,
                                     va_list ap) {
    (void)flag;
    return ::vfprintf(stream ? stream : stderr, format, ap);
}

// On a non-arm64 host the guest IS the host, so plain forwarding is correct.
extern "C" int bionic_sscanf(const char* s, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    const int r = ::vsscanf(s, format, ap);
    va_end(ap);
    return r;
}

extern "C" int bionic_fscanf(FILE* stream, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    const int r = ::vfscanf(stream ? stream : stdin, format, ap);
    va_end(ap);
    return r;
}
#endif

extern "C" int bionic_android_log_write(int prio, const char* tag, const char* text) {
    // Routed through logAndroidMessage rather than stderr alone so a guest that logs via
    // __android_log_write lands in kudroid_android_logs.txt and the crash buffer with
    // everything else. It used to go only to stderr, which the crash log does not carry.
    return logAndroidMessage(prio, tag, text ? text : "");
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
const unsigned short* g_ctype_ptr = &g_bionic_ctype_[1];

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
    (void)prepare; (void)parent; (void)child; (void)dso_handle;
#ifdef __APPLE__
    return pthread_atfork(prepare, parent, child);
#else
    return 0; // iOS does not really support fork(), so just return success
#endif
}

// ============================================================================
// Additional Linux syscalls commonly used by games
// ============================================================================

// getcpu — return the current CPU and NUMA node.
extern "C" int bionic_getcpu(unsigned* cpu, unsigned* node, void* tcache) {
    (void)tcache;
#ifdef __APPLE__
    if (cpu) *cpu = 0;
    if (node) *node = 0;
    return 0;
#else
    return ::syscall(SYS_getcpu, cpu, node, tcache);
#endif
}

// sched_getaffinity — return the CPU affinity mask.
//
// This is the WRAPPER, not the raw syscall, so success is 0 and the caller reads
// the mask out of `mask`. The raw syscall (number 123 in bionic_syscall) has the
// opposite contract and returns the byte count instead; see the note there.
extern "C" int bionic_sched_getaffinity(pid_t pid, size_t cpusetsize, void* mask) {
    (void)pid;
    if (!mask || cpusetsize == 0) {
        errno = EINVAL;
        return -1;
    }
    // Zero the whole set, then report the online cores in the low word. Zeroing
    // everything is correct here because nothing downstream reinterprets the return value
    // as a length.
    //
    // The mask comes from the same topology as sysconf and /proc/cpuinfo. It was 0xFF —
    // eight cores, regardless of what the device had and regardless of what every other
    // surface reported.
    memset(mask, 0, cpusetsize);
    const unsigned long online = static_cast<unsigned long>(kudroid::cpu_online_mask());
    memcpy(mask, &online, cpusetsize < sizeof(online) ? cpusetsize : sizeof(online));
    return 0;
}

// sched_setaffinity — set the CPU affinity mask (no-op).
extern "C" int bionic_sched_setaffinity(pid_t pid, size_t cpusetsize, const void* mask) {
    (void)pid; (void)cpusetsize; (void)mask;
    return 0;
}

// __sched_cpucount — count the set bits in a cpu_set_t.
//
// This is the prime suspect for "Cores = 0". It was not in the shim table at all, so the
// ELF loader bound it to kudroid_universal_dummy, which returns 0 — and CPU_COUNT() is a
// macro over this function, so every guest asking how many CPUs its affinity mask names
// got zero no matter what the mask actually contained. That is the exact shape of Unity's
// "Cores = 0" alongside a mask this shim had filled in.
//
// bionic's signature is `int __sched_cpucount(size_t setsize, const cpu_set_t* set)`, and
// it counts over the CALLER's declared size rather than a fixed width: a guest with a
// larger cpu_set_t must have all of its words counted.
extern "C" int bionic___sched_cpucount(size_t setsize, const void* set) {
    if (set == nullptr || setsize == 0) return 0;
    const unsigned char* bytes = static_cast<const unsigned char*>(set);
    int count = 0;
    for (size_t i = 0; i < setsize; ++i) {
        count += __builtin_popcount(static_cast<unsigned>(bytes[i]));
    }
    return count;
}

// sched_getcpu — return 0; iOS has no query and 0 is always a valid index.
extern "C" int bionic_sched_getcpu(void) {
#ifdef __APPLE__
    return 0;
#else
    const int cpu = ::sched_getcpu();
    return cpu >= 0 ? cpu : 0;
#endif
}

// get_nprocs / get_nprocs_conf — core-count helpers sharing the sysconf source.
extern "C" int bionic_get_nprocs(void) {
    return static_cast<int>(kudroid::query_cpu_topology().total_cores);
}

extern "C" int bionic_get_nprocs_conf(void) {
    return static_cast<int>(kudroid::query_cpu_topology().total_cores);
}

// get_phys_pages / get_avphys_pages — memory in pages.
extern "C" long bionic_get_phys_pages(void) {
    long pz = ::sysconf(_SC_PAGESIZE);
    if (pz <= 0) pz = 16384;
    return static_cast<long>(kudroid::query_system_memory().total_bytes /
                             static_cast<uint64_t>(pz));
}

extern "C" long bionic_get_avphys_pages(void) {
    long pz = ::sysconf(_SC_PAGESIZE);
    if (pz <= 0) pz = 16384;
    return static_cast<long>(kudroid::query_system_memory().available_bytes /
                             static_cast<uint64_t>(pz));
}

// inotify / signalfd — not on iOS; emulate with a pollable fd that never fires.
extern "C" int bionic_inotify_init1(int flags) {
    (void)flags;
#ifdef __APPLE__
    const int fd = create_loopback_udp();
#else
    const int fd = ::inotify_init1(flags);
#endif
    if (fd >= 0) {
        std::lock_guard<std::mutex> lock(g_fakefds_mtx);
        g_fakefds.insert(fd);
    }
    logAndroidMessage(4, "KuDroidSyscall", "inotify_init1 -> fd=" + std::to_string(fd) + " (emulated)");
    return fd;
}

// inotify_add_watch — register a watch (emulated: returns dummy descriptor).
extern "C" int bionic_inotify_add_watch(int fd, const char* pathname, uint32_t mask) {
    (void)fd; (void)pathname; (void)mask;
    static int s_next_wd = 1;
    const int wd = s_next_wd++;
    logAndroidMessage(4, "KuDroidSyscall", "inotify_add_watch(\"" +
                      std::string(pathname ? pathname : "<null>") + "\", mask=0x" +
                      [](uint32_t m){ char b[16]; snprintf(b, sizeof(b), "%x", (unsigned)m); return std::string(b); }(mask) +
                      ") -> wd=" + std::to_string(wd) + " (emulated)");
    return wd;
}

// inotify_rm_watch — remove a watch (emulated success).
extern "C" int bionic_inotify_rm_watch(int fd, int wd) {
    (void)fd; (void)wd;
    return 0;
}

// signalfd — create a file descriptor for signal delivery (emulated: fd poll-able,
// no pending signals — poll loop returns without blocking.
extern "C" int bionic_signalfd(int fd, const void* mask, int flags) {
    (void)mask; (void)flags;
    if (fd >= 0) return fd; // signalfd(fd,...) with existing fd — reuse
#ifdef __APPLE__
    const int newfd = create_loopback_udp();
#else
    const int newfd = ::signalfd(-1, reinterpret_cast<const sigset_t*>(mask), flags);
#endif
    if (newfd >= 0) {
        std::lock_guard<std::mutex> lock(g_fakefds_mtx);
        g_fakefds.insert(newfd);
    }
    logAndroidMessage(4, "KuDroidSyscall", "signalfd -> fd=" + std::to_string(newfd) + " (emulated)");
    return newfd;
}

// eventfd2 — same as eventfd (already implemented as bionic_eventfd).
extern "C" int bionic_eventfd2(unsigned int initval, int flags) {
    return bionic_eventfd(initval, flags);
}

// prlimit64 — get/set process resource limits.
// Previously returned 0 without filling old_limit -> uninitialized struct rlimit.
// Now populated from host getrlimit with Linux->Darwin resource index mapping.
struct GuestRlimit64 { uint64_t rlim_cur; uint64_t rlim_max; }; // Linux rlimit64
extern "C" int bionic_prlimit64(pid_t pid, int resource, const void* new_limit, void* old_limit) {
    (void)pid;
#ifdef __APPLE__
    // Linux RLIMIT_* -> Darwin RLIMIT_* (index mapping)
    // Linux: CPU=0 FSIZE=1 DATA=2 STACK=3 CORE=4 RSS=5 NPROC=6 NOFILE=7
    //        MEMLOCK=8 AS=9 LOCKS=10 SIGPENDING=11 MSGQUEUE=12 NICE=13 RTPRIO=14
    // Darwin: CPU=0 FSIZE=1 DATA=2 STACK=3 CORE=4 AS=5 MEMLOCK=6 NPROC=7 NOFILE=8
    int darwin_resource = -1;
    switch (resource) {
        case 0: darwin_resource = RLIMIT_CPU; break;      // 0 == 0
        case 1: darwin_resource = RLIMIT_FSIZE; break;    // 1 == 1
        case 2: darwin_resource = RLIMIT_DATA; break;     // 2 == 2
        case 3: darwin_resource = RLIMIT_STACK; break;    // 3 == 3
        case 4: darwin_resource = RLIMIT_CORE; break;     // 4 == 4
        case 6: darwin_resource = RLIMIT_NPROC; break;    // 6 -> 7
        case 7: darwin_resource = RLIMIT_NOFILE; break;   // 7 -> 8
        case 8: darwin_resource = RLIMIT_MEMLOCK; break;  // 8 -> 6
        case 9: darwin_resource = RLIMIT_AS; break;       // 9 -> 5
        default: darwin_resource = -1; break;             // RSS/LOCKS/... unsupported on Darwin
    }
#else
    const int darwin_resource = resource;
#endif
    struct rlimit host;
    if (darwin_resource < 0 || ::getrlimit(darwin_resource, &host) != 0) {
        if (old_limit) {
            auto* out = static_cast<GuestRlimit64*>(old_limit);
            out->rlim_cur = out->rlim_max = 0;
        }
        return 0; // unmapped resource — return 0 gracefully
    }
    if (new_limit) {
        const auto* in = static_cast<const GuestRlimit64*>(new_limit);
        host.rlim_cur = static_cast<rlim_t>(in->rlim_cur);
        host.rlim_max = static_cast<rlim_t>(in->rlim_max);
        ::setrlimit(darwin_resource, &host);
    }
    if (old_limit) {
        auto* out = static_cast<GuestRlimit64*>(old_limit);
        out->rlim_cur = static_cast<uint64_t>(host.rlim_cur);
        out->rlim_max = static_cast<uint64_t>(host.rlim_max);
    }
    return 0;
}

// statx — extended stat. Emulated via host stat() into Linux UAPI struct statx.
struct GuestStatxTimestamp {
    int64_t tv_sec;
    uint32_t tv_nsec;
    int32_t __reserved;
};
struct GuestStatx {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t __spare0[1];
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;
    GuestStatxTimestamp stx_atime, stx_btime, stx_ctime, stx_mtime;
    uint32_t stx_rdev_major, stx_rdev_minor;
    uint32_t stx_dev_major, stx_dev_minor;
    uint64_t stx_mnt_id;
    uint32_t stx_dio_mem_align;
    uint32_t stx_dio_offset_align;
    uint64_t __spare3[12];
};
static_assert(sizeof(GuestStatx) == 256, "statx layout must be 256 bytes (Linux UAPI)");

static void fill_statx_from_stat(struct GuestStatx* sx, const struct stat& st) {
    std::memset(sx, 0, sizeof(*sx));
    sx->stx_mask = 0x7ff; // STATX_BASIC_STATS
    sx->stx_blksize = uint32_t(st.st_blksize);
    sx->stx_nlink = uint32_t(st.st_nlink);
    sx->stx_uid = st.st_uid;
    sx->stx_gid = st.st_gid;
    sx->stx_mode = uint16_t(st.st_mode);
    sx->stx_ino = uint64_t(st.st_ino);
    sx->stx_size = uint64_t(st.st_size);
    sx->stx_blocks = uint64_t(st.st_blocks);
#ifdef __APPLE__
    sx->stx_atime = {st.st_atimespec.tv_sec, uint32_t(st.st_atimespec.tv_nsec), 0};
    sx->stx_ctime = {st.st_ctimespec.tv_sec, uint32_t(st.st_ctimespec.tv_nsec), 0};
    sx->stx_mtime = {st.st_mtimespec.tv_sec, uint32_t(st.st_mtimespec.tv_nsec), 0};
#else
    sx->stx_atime = {st.st_atim.tv_sec, uint32_t(st.st_atim.tv_nsec), 0};
    sx->stx_ctime = {st.st_ctim.tv_sec, uint32_t(st.st_ctim.tv_nsec), 0};
    sx->stx_mtime = {st.st_mtim.tv_sec, uint32_t(st.st_mtim.tv_nsec), 0};
#endif
    sx->stx_dev_major = uint32_t(st.st_dev >> 8);
    sx->stx_dev_minor = uint32_t(st.st_dev & 0xff);
}

extern "C" int bionic_statx(int dirfd, const char* pathname, int flags, unsigned mask, void* statxbuf) {
    (void)mask;
    if (!pathname || !statxbuf) { errno = EFAULT; return -1; }
    struct stat st;
    std::string path = pathname;
    if (dirfd != AT_FDCWD && pathname[0] != '/') {
        // Relative path with dirfd — best-effort resolution via /proc/self/fd.
        char link[64];
        std::snprintf(link, sizeof(link), "/proc/self/fd/%d", dirfd);
        char resolved[PATH_MAX];
        const ssize_t n = ::readlink(link, resolved, sizeof(resolved) - 1);
        if (n > 0) {
            resolved[n] = '\0';
            path = std::string(resolved) + "/" + pathname;
        }
    }
    const int rc = (flags & AT_SYMLINK_NOFOLLOW)
                       ? ::lstat(path.c_str(), &st)
                       : ::stat(path.c_str(), &st);
    if (rc != 0) return -1; // errno already set
    fill_statx_from_stat(static_cast<GuestStatx*>(statxbuf), st);
    return 0;
}

// ============================================================================
// ALooper — Android event loop (used by native games like Teapot).
//
// Games call ALooper_prepare() to create a looper, ALooper_addFd() to register
// file descriptors (e.g. the input queue), and ALooper_pollAll()/pollOnce() to
// wait for events. We implement a minimal version backed by poll().
// ============================================================================

#define ALOOPER_PREPARE_ALLOW_NON_CALLBACKS 1
#define ALOOPER_EVENT_INPUT 0x0001
#define ALOOPER_EVENT_OUTPUT 0x0002
#define ALOOPER_EVENT_ERROR 0x0004
#define ALOOPER_EVENT_HANGUP 0x0008
#define ALOOPER_EVENT_INVALID 0x0010

struct ALooperFd {
    int fd;
    int ident;
    int events;
    void* callback; // ALooper_callbackFunc
    void* data;
    bool isInputPipe; // fd is AInputQueue wake pipe (drained when readable)
};

struct ALooper {
    std::vector<ALooperFd> fds;
    std::mutex mtx;
    int refCount;
};

static ALooper* g_mainLooper = nullptr;
static std::mutex g_looperMtx;

// Create the main thread's looper if it does not exist yet, and return it.
//
// Android's main thread always has a looper: ActivityThread prepares one before any
// activity is created, so by the time native code runs ALooper_forThread() cannot fail.
// KuART's main loop is Java (Looper.loop() in ActivityThread) and never touched the native
// ALooper, so forThread() returned null and AGDK's initializeNativeCode aborted with
// "Unable to retrieve native ALooper" — an UnsatisfiedLinkError that stopped onCreate
// before a surface existed, leaving the app on a black screen with no crash.
//
// Created on demand rather than at startup so a process that never loads native code does
// not carry one, and so this holds whichever thread asks first — which is the main thread,
// because that is where the activity is created.
static ALooper* GetOrCreateMainLooper() {
    std::lock_guard<std::mutex> lock(g_looperMtx);
    if (g_mainLooper == nullptr) {
        g_mainLooper = new ALooper();
        // One reference for the looper's own existence. Android's main looper is never
        // released, and ALooper_release() deletes at zero — without this an acquire/release
        // pair from guest code would free the main looper and turn every later
        // forThread() into a dangling pointer rather than a null one.
        g_mainLooper->refCount = 1;
    }
    return g_mainLooper;
}

extern "C" void* bionic_ALooper_prepare(int opts) {
    (void)opts;
    ALooper* looper = GetOrCreateMainLooper();
    std::lock_guard<std::mutex> lock(g_looperMtx);
    looper->refCount++;
    return looper;
}

// The looper for the calling thread, creating the main one if nothing has yet.
//
// Returning null here is what broke AGDK. Android guarantees a looper on the main thread,
// so guest code treats null as a fatal error rather than something to prepare around.
extern "C" void* bionic_ALooper_forThread() {
    return GetOrCreateMainLooper();
}

extern "C" void bionic_ALooper_acquire(void* looper) {
    if (!looper) return;
    ALooper* l = static_cast<ALooper*>(looper);
    std::lock_guard<std::mutex> lock(g_looperMtx);
    l->refCount++;
}

extern "C" void bionic_ALooper_release(void* looper) {
    if (!looper) return;
    ALooper* l = static_cast<ALooper*>(looper);
    std::lock_guard<std::mutex> lock(g_looperMtx);
    // The main looper outlives every reference to it, as it does on Android: its baseline
    // reference is never released. Deleting it on an unbalanced release from guest code
    // would leave g_mainLooper dangling for any thread already inside pollOnce, and the
    // fault would appear far from the release that caused it.
    if (l == g_mainLooper) {
        if (l->refCount > 1) --l->refCount;
        return;
    }
    if (--l->refCount <= 0) {
        delete l;
    }
}

extern "C" int bionic_ALooper_addFd(void* looper, int fd, int ident, int events,
                                    void* callback, void* data) {
    if (!looper || fd < 0) return -1;
    ALooper* l = static_cast<ALooper*>(looper);
    std::lock_guard<std::mutex> lock(l->mtx);
    // Replace existing entry for this fd.
    for (auto& f : l->fds) {
        if (f.fd == fd) {
            f.ident = ident;
            f.events = events;
            f.callback = callback;
            f.data = data;
            return 1;
        }
    }
    ALooperFd nf;
    nf.fd = fd;
    nf.ident = ident;
    nf.events = events;
    nf.callback = callback;
    nf.data = data;
    nf.isInputPipe = false;
    l->fds.push_back(nf);
    return 1;
}

// Mark fd as AInputQueue wake pipe — bionic_AInputQueue_attachLooper
// called after registering pipe with looper, ensuring pollAll drains it to prevent busy loops.
extern "C" void bionic_ALooper_markInputPipe(void* looper, int fd) {
    if (!looper || fd < 0) return;
    ALooper* l = static_cast<ALooper*>(looper);
    std::lock_guard<std::mutex> lock(l->mtx);
    for (auto& f : l->fds) {
        if (f.fd == fd) {
            f.isInputPipe = true;
            return;
        }
    }
}

extern "C" int bionic_ALooper_removeFd(void* looper, int fd) {
    if (!looper || fd < 0) return -1;
    ALooper* l = static_cast<ALooper*>(looper);
    std::lock_guard<std::mutex> lock(l->mtx);
    for (size_t i = 0; i < l->fds.size(); ++i) {
        if (l->fds[i].fd == fd) {
            l->fds.erase(l->fds.begin() + i);
            return 1;
        }
    }
    return 0;
}

extern "C" void bionic_ALooper_wake(void* looper) {
    (void)looper;
    // No-op; poll uses a timeout so wake is not strictly needed.
}

// Poll for events. Returns the ident of the first ready fd, or ALOOPER_POLL_TIMEOUT (-2)
// on timeout, ALOOPER_POLL_ERROR (-1) on error, ALOOPER_POLL_WAKE (-3) on wake.
extern "C" int bionic_ALooper_pollAll(int timeoutMillis, int* outFd, int* outEvents, void** outData);
extern "C" int bionic_ALooper_pollOnce(int timeoutMillis, int* outFd, int* outEvents, void** outData) {
    return bionic_ALooper_pollAll(timeoutMillis, outFd, outEvents, outData);
}

extern "C" int bionic_ALooper_pollAll(int timeoutMillis, int* outFd, int* outEvents, void** outData) {
    ALooper* l;
    {
        std::lock_guard<std::mutex> lock(g_looperMtx);
        l = g_mainLooper;
    }
    if (!l) return -1;

    // Build pollfd array.
    std::vector<struct pollfd> pfds;
    std::vector<ALooperFd> snapshot;
    {
        std::lock_guard<std::mutex> lock(l->mtx);
        snapshot = l->fds;
    }
    for (const auto& f : snapshot) {
        struct pollfd pfd;
        pfd.fd = f.fd;
        pfd.events = 0;
        if (f.events & ALOOPER_EVENT_INPUT) pfd.events |= POLLIN;
        if (f.events & ALOOPER_EVENT_OUTPUT) pfd.events |= POLLOUT;
        pfd.revents = 0;
        pfds.push_back(pfd);
    }

    if (pfds.empty()) {
        // No fds registered; sleep for the timeout.
        if (timeoutMillis > 0) {
            struct timespec ts;
            ts.tv_sec = timeoutMillis / 1000;
            ts.tv_nsec = (timeoutMillis % 1000) * 1000000;
            nanosleep(&ts, nullptr);
        }
        return -2; // ALOOPER_POLL_TIMEOUT
    }

    // nfds_t is unsigned int on Darwin — clamp to avoid truncation.
    const nfds_t nfds = pfds.size() > static_cast<size_t>(INT_MAX)
                           ? static_cast<nfds_t>(INT_MAX)
                           : static_cast<nfds_t>(pfds.size());
    int ret = ::poll(pfds.data(), nfds, timeoutMillis);
    if (ret < 0) return -1; // ALOOPER_POLL_ERROR
    if (ret == 0) return -2; // ALOOPER_POLL_TIMEOUT

    // Find the first ready fd.
    for (size_t i = 0; i < pfds.size(); ++i) {
        if (pfds[i].revents == 0) continue;

        const int ev = [&]() {
            int e = 0;
            if (pfds[i].revents & POLLIN) e |= ALOOPER_EVENT_INPUT;
            if (pfds[i].revents & POLLOUT) e |= ALOOPER_EVENT_OUTPUT;
            if (pfds[i].revents & POLLERR) e |= ALOOPER_EVENT_ERROR;
            if (pfds[i].revents & POLLHUP) e |= ALOOPER_EVENT_HANGUP;
            if (pfds[i].revents & POLLNVAL) e |= ALOOPER_EVENT_INVALID;
            return e;
        }();

        // Drain AInputQueue wake pipe to prevent looper busy looping on unread pipe.
        // Created with O_NONBLOCK: drain until EAGAIN to prevent blocking.
        if (snapshot[i].isInputPipe && (pfds[i].revents & (POLLIN | POLLHUP))) {
            char drainBuf[4096];
            for (int drainIters = 0; drainIters < (1 << 20); ++drainIters) {
                const ssize_t n = ::read(snapshot[i].fd, drainBuf, sizeof(drainBuf));
                if (n <= 0) break; // EAGAIN or empty
            }
        }

        // Execute looper callback (return 0 unregisters fd).
        if (snapshot[i].callback) {
            const int keep = reinterpret_cast<int (*)(int, int, void*)>(snapshot[i].callback)(
                snapshot[i].fd, ev, snapshot[i].data);
            if (keep == 0) {
                std::lock_guard<std::mutex> lock(l->mtx);
                for (size_t k = 0; k < l->fds.size(); ++k) {
                    if (l->fds[k].fd == snapshot[i].fd) {
                        l->fds.erase(l->fds.begin() + k);
                        break;
                    }
                }
            }
            return -4; // ALOOPER_POLL_CALLBACK
        }

        if (outFd) *outFd = pfds[i].fd;
        if (outEvents) *outEvents = ev;
        if (outData) *outData = snapshot[i].data;
        return snapshot[i].ident;
    }
    return -2; // ALOOPER_POLL_TIMEOUT
}

// ─────────────────────────────────────────────────────────────────────────────
// Bionic symbols game .so commonly import that the host cannot provide via
// Resolved via real implementations rather than universal dummy stubs to ensure thread sync and valid bounds checking.
// ─────────────────────────────────────────────────────────────────────────────

// bionic: void sincos(double x, double* s, double* c)
extern "C" void bionic_sincos(double x, double* s, double* c) {
    if (s) *s = ::sin(x);
    if (c) *c = ::cos(x);
}

// bionic: void sincosf(float x, float* s, float* c)
extern "C" void bionic_sincosf(float x, float* s, float* c) {
    if (s) *s = ::sinf(x);
    if (c) *c = ::cosf(x);
}

// bionic: size_t __strlen_chk(const char* s, size_t s_len)
// FORTIFY: return real strlen with bounds warning rather than hard aborting on layout mismatches.
extern "C" size_t bionic___strlen_chk(const char* s, size_t s_len) {
    const size_t len = ::strlen(s ? s : "");
    if (len >= s_len) {
        trace("__strlen_chk: string exceeds declared buffer size (fortify)");
    }
    return len;
}

// bionic: void __FD_SET_chk(int fd, fd_set* set, size_t set_size)
// set_size is byte size of fd_set; valid if fd < set_size*8.
extern "C" void bionic___FD_SET_chk(int fd, fd_set* set, size_t set_size) {
    if (!set) return;
    if (fd < 0 || static_cast<size_t>(fd) >= set_size * 8) {
        trace("__FD_SET_chk: fd out of range for fd_set (fortify)");
        return;
    }
    FD_SET(fd, set);
}

// bionic: int __FD_ISSET_chk(int fd, const fd_set* set, size_t set_size)
// Return non-zero if fd is present in set; prevents polling loops from hanging.
extern "C" int bionic___FD_ISSET_chk(int fd, const fd_set* set, size_t set_size) {
    if (!set) return 0;
    if (fd < 0 || static_cast<size_t>(fd) >= set_size * 8) {
        trace("__FD_ISSET_chk: fd out of range for fd_set (fortify)");
        return 0;
    }
    return FD_ISSET(fd, set) ? 1 : 0;
}

extern "C" int bionic___open_2(const char* path, int flags) {
    if (flags & O_CREAT) {
        return bionic_openat(AT_FDCWD, path, flags, 0666);
    }
    return bionic_openat(AT_FDCWD, path, flags, 0);
}

extern "C" int bionic___openat_2(int dirfd, const char* path, int flags) {
    if (flags & O_CREAT) {
        return bionic_openat(dirfd, path, flags, 0666);
    }
    return bionic_openat(dirfd, path, flags, 0);
}

extern "C" mode_t bionic___umask_chk(mode_t mask) {
    return ::umask(mask);
}

extern "C" char* bionic___strrchr_chk(const char* s, int c, size_t s_len) {
    (void)s_len;
    return const_cast<char*>(::strrchr(s, c));
}

struct bionic_sysinfo_struct {
    long uptime;
    unsigned long loads[3];
    unsigned long totalram;
    unsigned long freeram;
    unsigned long sharedram;
    unsigned long bufferram;
    unsigned long totalswap;
    unsigned long freeswap;
    unsigned short procs;
    unsigned short pad;
    unsigned long totalhigh;
    unsigned long freehigh;
    unsigned int mem_unit;
    char _f[8];
};

extern "C" int bionic_sysinfo(struct bionic_sysinfo_struct* info) {
    if (!info) return -1;
    memset(info, 0, sizeof(*info));
    const kudroid::SystemMemory mem = kudroid::query_system_memory();
    info->uptime = 3600;
    // The real device figures. These were constants — 4 GB total, 2 GB free — which
    // happened to match one test device and no other.
    info->totalram = static_cast<unsigned long>(mem.total_bytes);
    info->freeram = static_cast<unsigned long>(mem.available_bytes);
    info->procs = 100;
    // No swap on iOS. A guest told it has swap will overcommit, and on iOS that ends in a
    // jetsam kill rather than in paging.
    info->totalswap = 0;
    info->freeswap = 0;
    info->mem_unit = 1;
    return 0;
}

struct AndroidBitmapInfo {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    int32_t  format; // ANDROID_BITMAP_FORMAT_RGBA_8888 = 1
    uint32_t flags;
};

extern "C" int bionic_AndroidBitmap_getInfo(void* env, void* jbitmap, AndroidBitmapInfo* info) {
    if (info) {
        info->width = 128;
        info->height = 128;
        info->stride = 128 * 4;
        info->format = 1; // RGBA_8888
        info->flags = 0;
    }
    return 0; // ANDROID_BITMAP_RESULT_SUCCESS
}

static uint32_t s_dummy_bitmap_pixels[1024 * 1024];

extern "C" int bionic_AndroidBitmap_lockPixels(void* env, void* jbitmap, void** addrPtr) {
    if (addrPtr) {
        *addrPtr = s_dummy_bitmap_pixels;
    }
    return 0;
}

extern "C" int bionic_AndroidBitmap_unlockPixels(void* env, void* jbitmap) {
    return 0;
}

extern "C" char* bionic___gnu_strerror_r(int errnum, char* buf, size_t buflen) {
    if (!buf || buflen == 0) return const_cast<char*>("");
    strerror_r(errnum, buf, buflen);
    return buf;
}

extern "C" size_t bionic___fwrite_chk(const void* buf, size_t size, size_t count, FILE* stream, size_t buf_size) {
    return ::fwrite(buf, size, count, stream);
}

// ── POSIX unnamed semaphores ────────────────────────────────────────────────
//
// Darwin DECLARES sem_init/sem_wait/sem_post/sem_destroy in <semaphore.h> but does
// not implement them: each sets ENOSYS and returns -1. Only named semaphores
// (sem_open) work. So an unshimmed guest call fell through BionicShim's
// dlsym(RTLD_DEFAULT) straight to libSystem and failed.
//
// libunity.so imports exactly sem_init, sem_wait, sem_post and sem_destroy — all
// four unnamed. Unity starts a helper thread, sets its name, then blocks on a
// semaphore waiting for the spawner's handshake. sem_wait returning -1/ENOSYS is
// not a blocking wait, so the helper raced ahead while the spawner waited for a
// sem_post that the failed pair could never deliver. The log stopped on
// prctl(PR_SET_NAME, "AssetGarbageCollectorHelper") — the last thing that happened
// before the handshake — with nothing to say a semaphore had failed.
//
// The guest's sem_t cannot hold this state: bionic's is 4 bytes, the guest embeds
// it in its own structs, and its layout is not ours to reinterpret. State is
// therefore kept beside it, keyed by address, the same way g_futexQueues does.
struct GuestSemaphore {
    std::mutex mtx;
    std::condition_variable cv;
    unsigned int value = 0;
    bool destroyed = false;
};

static std::map<const void*, std::shared_ptr<GuestSemaphore>> g_semaphores;
static std::mutex g_semaphoresMtx;

// Look up, optionally creating. A wait on a semaphore we never saw initialised is
// treated as a semaphore initialised to 0, which is what a handshake uses and the
// only interpretation that can be correct; it is logged once because it means an
// init slipped past this shim.
static std::shared_ptr<GuestSemaphore> guest_sem_find(const void* sem, bool create) {
    std::lock_guard<std::mutex> lock(g_semaphoresMtx);
    auto it = g_semaphores.find(sem);
    if (it != g_semaphores.end()) return it->second;
    if (!create) return nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        logAndroidMessage(5, "KuDroidSyscall",
                          "sem_wait/sem_post on a semaphore that was never sem_init'd;"
                          " treating it as initialised to 0");
    });
    auto created = std::make_shared<GuestSemaphore>();
    g_semaphores.emplace(sem, created);
    return created;
}

extern "C" int bionic_sem_init(sem_t* sem, int pshared, unsigned int value) {
    if (!sem) {
        errno = EINVAL;
        return -1;
    }
    // pshared is accepted and ignored: KuDroid runs the guest in one process, so a
    // process-shared semaphore and a thread-shared one behave identically here.
    // Rejecting it would fail guests that pass 1 out of habit.
    (void)pshared;
    auto s = std::make_shared<GuestSemaphore>();
    s->value = value;
    {
        std::lock_guard<std::mutex> lock(g_semaphoresMtx);
        g_semaphores[sem] = s;  // re-init at the same address replaces the old state
    }
    return 0;
}

extern "C" int bionic_sem_destroy(sem_t* sem) {
    if (!sem) {
        errno = EINVAL;
        return -1;
    }
    std::shared_ptr<GuestSemaphore> s;
    {
        std::lock_guard<std::mutex> lock(g_semaphoresMtx);
        auto it = g_semaphores.find(sem);
        if (it == g_semaphores.end()) return 0;  // destroying an unknown one is harmless
        s = it->second;
        g_semaphores.erase(it);
    }
    // Release anyone still parked. Destroying a semaphore with waiters is undefined
    // in POSIX, but leaving a guest thread blocked forever during shutdown turns an
    // exit into a hang, which is the failure this whole shim exists to avoid.
    {
        std::lock_guard<std::mutex> lock(s->mtx);
        s->destroyed = true;
    }
    s->cv.notify_all();
    return 0;
}

extern "C" int bionic_sem_wait(sem_t* sem) {
    if (!sem) {
        errno = EINVAL;
        return -1;
    }
    auto s = guest_sem_find(sem, /*create=*/true);
    std::unique_lock<std::mutex> lock(s->mtx);
    {
        // A handshake whose post never arrives parks this thread for good. This is
        // where Unity's helper threads wait, so it is the first thing worth naming
        // when a launch stops making progress.
        const BlockingWaitScope tracked(WaitKind::kSemaphore, sem,
                                        guest_return_address(6));
        s->cv.wait(lock, [&s] { return s->value > 0 || s->destroyed; });
    }
    if (s->destroyed) {
        errno = EINVAL;
        return -1;
    }
    --s->value;
    return 0;
}

extern "C" int bionic_sem_trywait(sem_t* sem) {
    if (!sem) {
        errno = EINVAL;
        return -1;
    }
    auto s = guest_sem_find(sem, /*create=*/true);
    std::unique_lock<std::mutex> lock(s->mtx);
    if (s->value == 0) {
        errno = EAGAIN;
        return -1;
    }
    --s->value;
    return 0;
}

extern "C" int bionic_sem_post(sem_t* sem) {
    if (!sem) {
        errno = EINVAL;
        return -1;
    }
    auto s = guest_sem_find(sem, /*create=*/true);
    {
        std::lock_guard<std::mutex> lock(s->mtx);
        ++s->value;
    }
    // notify_one, not all: a post releases exactly one waiter, and waking the rest
    // only to re-block them turns a handshake between many threads into a stampede.
    s->cv.notify_one();
    return 0;
}

extern "C" int bionic_sem_getvalue(sem_t* sem, int* out) {
    if (!sem || !out) {
        errno = EINVAL;
        return -1;
    }
    auto s = guest_sem_find(sem, /*create=*/true);
    std::lock_guard<std::mutex> lock(s->mtx);
    *out = static_cast<int>(s->value);
    return 0;
}

// bionic: int sem_timedwait(sem_t* sem, const struct timespec* abs_timeout)
// abs_timeout is absolute CLOCK_REALTIME.
//
// Previously this called the host ::sem_trywait on the guest's sem_t in a sleep
// loop, which on Darwin fails with ENOSYS on the very first iteration and then
// spins until the deadline. It now waits on the same emulated state as the rest.
extern "C" int bionic_sem_timedwait(sem_t* sem, const struct timespec* abs_timeout) {
    if (!sem || !abs_timeout) {
        errno = EINVAL;
        return -1;
    }
    auto s = guest_sem_find(sem, /*create=*/true);

    // The deadline is absolute CLOCK_REALTIME; convert to a duration so it can be
    // waited on without assuming the host clock's epoch matches.
    struct timespec now;
    ::clock_gettime(CLOCK_REALTIME, &now);
    int64_t remSec = int64_t(abs_timeout->tv_sec) - int64_t(now.tv_sec);
    int64_t remNs = int64_t(abs_timeout->tv_nsec) - int64_t(now.tv_nsec);
    if (remNs < 0) {
        remSec -= 1;
        remNs += 1000000000;
    }

    std::unique_lock<std::mutex> lock(s->mtx);
    auto ready = [&s] { return s->value > 0 || s->destroyed; };
    if (remSec < 0) {
        // Already expired. POSIX still requires the semaphore be taken if it is
        // available, so check before reporting a timeout.
        if (!ready()) {
            errno = ETIMEDOUT;
            return -1;
        }
    } else {
        if (remSec > 86400) remSec = 86400;  // cap so the duration cannot overflow
        const auto duration =
            std::chrono::seconds(remSec) + std::chrono::nanoseconds(remNs);
        const BlockingWaitScope tracked(WaitKind::kSemaphoreTimed, sem,
                                        guest_return_address(6));
        // Same reasoning as the timed futex: a wait inside its own deadline is doing
        // what it asked for, and reporting it as stalled buries the real one.
        blocking_wait_note_budget(static_cast<uint64_t>(remSec) * 1000ull +
                                  static_cast<uint64_t>(remNs) / 1000000ull);
        if (!s->cv.wait_for(lock, duration, ready)) {
            errno = ETIMEDOUT;
            return -1;
        }
    }
    if (s->destroyed) {
        errno = EINVAL;
        return -1;
    }
    --s->value;
    return 0;
}

// Forward android.util.Log.println_native to standard KuDroid log pipeline.
extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message) {
    return logAndroidMessage(priority, tag, std::string(message ? message : ""));
}

// ─────────────────────────────────────────────────────────────────────────────
// Real FORTIFY (__*_chk) implementations to prevent silent data corruption.
// ─────────────────────────────────────────────────────────────────────────────

extern "C" void* bionic___memcpy_chk(void* dst, const void* src, size_t n, size_t dst_len) {
    if (n > dst_len) trace("__memcpy_chk: destination overflow (fortify)");
    return ::memcpy(dst, src, n);
}

extern "C" void* bionic___memmove_chk(void* dst, const void* src, size_t n, size_t dst_len) {
    if (n > dst_len) trace("__memmove_chk: destination overflow (fortify)");
    return ::memmove(dst, src, n);
}

extern "C" void* bionic___memset_chk(void* s, int c, size_t n, size_t s_len) {
    if (n > s_len) trace("__memset_chk: destination overflow (fortify)");
    return ::memset(s, c, n);
}

extern "C" ssize_t bionic___read_chk(int fd, void* buf, size_t nbytes, size_t buflen) {
    (void)buflen;
    return ::read(fd, buf, nbytes);
}

extern "C" ssize_t bionic___write_chk(int fd, const void* buf, size_t count, size_t buflen) {
    (void)buflen;
    return ::write(fd, buf, count);
}

extern "C" int bionic___snprintf_chk(char* s, size_t maxlen, int flag, size_t slen,
                                     const char* format, ...) {
    (void)flag; (void)slen;
    va_list args;
    va_start(args, format);
    const int r = ::vsnprintf(s, maxlen, format, args);
    va_end(args);
    return r;
}

extern "C" int bionic___sprintf_chk(char* s, int flag, size_t slen, const char* format, ...) {
    (void)flag;
    va_list args;
    va_start(args, format);
    const int r = ::vsnprintf(s, slen, format, args);
    va_end(args);
    return r;
}

extern "C" char* bionic___strncpy_chk(char* dst, const char* src, size_t n, size_t dst_len) {
    if (n > dst_len) {
        trace("__strncpy_chk: destination overflow (fortify)");
        n = dst_len; // clamp to prevent buffer overflow
    }
    return ::strncpy(dst, src, n);
}

extern "C" char* bionic___strcpy_chk(char* dst, const char* src, size_t dst_len) {
    if (::strlen(src) >= dst_len) trace("__strcpy_chk: destination overflow (fortify)");
    return ::strcpy(dst, src);
}

extern "C" char* bionic___strcat_chk(char* dst, const char* src, size_t dst_len) {
    if (::strlen(dst) + ::strlen(src) >= dst_len) {
        trace("__strcat_chk: destination overflow (fortify)");
    }
    return ::strcat(dst, src);
}

extern "C" unsigned long bionic___fdelt_chk(unsigned long fd) {
    if (fd >= FD_SETSIZE) {
        trace("__fdelt_chk: fd exceeds FD_SETSIZE (fortify)");
    }
    return fd / (8UL * sizeof(unsigned long));
}

// bionic: void android_set_abort_message(const char* msg)
// Store guest abort messages for tombstone crash logs (e.g. Unity FATAL messages).
extern "C" void bionic_android_set_abort_message(const char* msg) {
    if (msg && *msg) {
        kudroid_store_abort_message(msg);
        char traceMessage[256];
        snprintf(traceMessage, sizeof(traceMessage),
                 "android_set_abort_message: %.200s", msg);
        trace(traceMessage);
    }
}

extern "C" uint64_t bionic_ZSTD_trace_compress_begin(const void* cctx) {
    (void)cctx;
    return 0;
}

extern "C" void bionic_ZSTD_trace_compress_end(uint64_t handle, const void* cctx) {
    (void)handle;
    (void)cctx;
}

extern "C" uint64_t bionic_ZSTD_trace_decompress_begin(const void* dctx) {
    (void)dctx;
    return 0;
}

extern "C" void bionic_ZSTD_trace_decompress_end(uint64_t handle, const void* dctx) {
    (void)handle;
    (void)dctx;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bionic malloc extensions queried from host libmalloc.
// ─────────────────────────────────────────────────────────────────────────────

extern "C" size_t bionic_malloc_usable_size(const void* ptr) {
    if (!ptr) return 0;
#if defined(__APPLE__)
    return ::malloc_size(ptr);
#else
    return ::malloc_usable_size(const_cast<void*>(ptr));
#endif
}

// Bionic struct mallinfo (10 size_t entries returned by value).
struct BionicMallinfo {
    size_t arena;     // total non-mmap heap space
    size_t ordblks;   // number of free blocks
    size_t smblks;    // always 0 on Bionic
    size_t hblks;     // number of mmap regions
    size_t hblkhd;    // total mmap bytes
    size_t usmblks;   // always 0
    size_t fsmblks;   // always 0
    size_t uordblks;  // total allocated bytes
    size_t fordblks;  // total free bytes
    size_t keepcost;  // reclaimable bytes
};

extern "C" BionicMallinfo bionic_mallinfo(void) {
    BionicMallinfo mi = {};
#if defined(__APPLE__)
    malloc_statistics_t st = {};
    ::malloc_zone_statistics(::malloc_default_zone(), &st);
    mi.arena = st.size_allocated;
    mi.hblks = st.blocks_in_use;
    mi.hblkhd = st.max_size_in_use;
    mi.uordblks = st.size_in_use;
    mi.fordblks = st.size_allocated > st.size_in_use
                      ? st.size_allocated - st.size_in_use : 0;
    mi.keepcost = mi.fordblks;
    mi.ordblks = 1;
#endif
    return mi;
}

// jemalloc sized-delete: size is an optimization hint, mapped to standard free().
extern "C" void bionic_sdallocx(void* ptr, size_t size, int flags) {
    (void)size;
    (void)flags;
    ::free(ptr);
}

// BoringSSL memory hooks mapped to malloc/free.
extern "C" void* bionic_OPENSSL_memory_alloc(size_t size) {
    return ::malloc(size);
}

extern "C" void bionic_OPENSSL_memory_free(void* ptr) {
    ::free(ptr);
}

extern "C" size_t bionic_OPENSSL_memory_get_size(void* ptr) {
    return bionic_malloc_usable_size(ptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// memfd_create emulation via immediate unlinked temporary file in NSTemporaryDirectory.
// ─────────────────────────────────────────────────────────────────────────────

extern "C" int bionic_memfd_create(const char* name, unsigned int flags) {
    const char* tmp = ::getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    char path[PATH_MAX];
    static std::atomic<unsigned> counter{0};
    std::snprintf(path, sizeof(path), "%s/kudroid-memfd-%s-%d-%u", tmp,
                  (name && *name) ? name : "anon", static_cast<int>(::getpid()),
                  counter.fetch_add(1, std::memory_order_relaxed));
    const int fd = ::open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return -1;
    ::unlink(path);
#ifdef FD_CLOEXEC
    // MFD_CLOEXEC == 1 in bionic.
    if (flags & 1u) ::fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
    return fd;
}

// ─────────────────────────────────────────────────────────────────────────────
// Two-parameter pthread_setname_np adapted for Darwin.
// ─────────────────────────────────────────────────────────────────────────────

extern "C" int bionic_pthread_setname_np2(pthread_t thread, const char* name) {
#if defined(__APPLE__)
    // Darwin pthread_setname_np only sets current thread name.
    if (::pthread_equal(thread, ::pthread_self())) {
        return ::pthread_setname_np(name ? name : "");
    }
    return 0;
#else
    // Linux host matches Bionic signature directly.
    return ::pthread_setname_np(thread, name ? name : "");
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// mallopt/malloc_info stubs for Darwin host.
// ─────────────────────────────────────────────────────────────────────────────

extern "C" int bionic_mallopt(int param, int value) {
    (void)param; (void)value;
    return 1;
}

extern "C" int bionic_malloc_info(int options, FILE* fp) {
    (void)options;
    if (!fp) { errno = EINVAL; return -1; }
    errno = ENOSYS;
    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// setprogname/getprogname fallback using static buffer on Linux hosts.
// ─────────────────────────────────────────────────────────────────────────────

static char g_progName[256] = "";

extern "C" void bionic_setprogname(const char* name) {
#if defined(__APPLE__)
    ::setprogname(name);
#else
    if (name) {
        std::strncpy(g_progName, name, sizeof(g_progName) - 1);
        g_progName[sizeof(g_progName) - 1] = '\0';
    }
#endif
}

extern "C" const char* bionic_getprogname() {
#if defined(__APPLE__)
    return ::getprogname();
#else
    if (g_progName[0]) return g_progName;
    // Default: argv[0] basename from /proc/self/cmdline.
    static char buf[256] = "unknown";
    FILE* f = std::fopen("/proc/self/cmdline", "rb");
    if (f) {
        size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
        std::fclose(f);
        if (n > 0) {
            buf[n] = '\0';
            char* slash = std::strrchr(buf, '/');
            if (slash) std::memmove(buf, slash + 1, std::strlen(slash + 1) + 1);
        }
    }
    return buf;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// pthread_gettid_np — retrieve tid for specific thread.
// ─────────────────────────────────────────────────────────────────────────────

extern "C" pid_t bionic_pthread_gettid_np(pthread_t thread) {
#if defined(__APPLE__)
    uint64_t tid = 0;
    if (::pthread_threadid_np(thread, &tid) != 0) return -1;
    return static_cast<pid_t>(tid);
#else
    if (::pthread_equal(thread, ::pthread_self())) return bionic_gettid();
    return -1; // Linux has no direct API to get tid from non-current pthread_t
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// __pthread_cleanup_push/pop: per-thread cleanup handler stack.
// ─────────────────────────────────────────────────────────────────────────────

struct BionicCleanup {
    BionicCleanup* prev;
    void (*routine)(void*);
    void* arg;
};

static thread_local BionicCleanup* g_cleanupHead = nullptr;

extern "C" void bionic___pthread_cleanup_push(BionicCleanup* c,
                                              void (*routine)(void*), void* arg) {
    if (!c) return;
    c->routine = routine;
    c->arg = arg;
    c->prev = g_cleanupHead;
    g_cleanupHead = c;
}

extern "C" void bionic___pthread_cleanup_pop(BionicCleanup* c, int execute) {
    if (!c) return;
    g_cleanupHead = c->prev;
    if (execute && c->routine) c->routine(c->arg);
}

// ─────────────────────────────────────────────────────────────────────────────
// __fgets_chk — bounds-checked fgets (FORTIFY).
// ─────────────────────────────────────────────────────────────────────────────

extern "C" char* bionic___fgets_chk(char* dst, int supplied_size, FILE* stream,
                                    size_t dst_len_from_compiler) {
    if (!dst || !stream || supplied_size <= 0) return nullptr;
    if (dst_len_from_compiler != static_cast<size_t>(-1) &&
        static_cast<size_t>(supplied_size) > dst_len_from_compiler) {
        // Real buffer overflow — abort like Bionic rather than corrupting memory.
        bionic___assert2(__FILE__, __LINE__, "__fgets_chk",
                         "fgets: prevented write past end of buffer");
        return nullptr;
    }
    return ::fgets(dst, supplied_size, stream);
}

const SymbolEntry kSyscallSymbols[] = {
    {"__register_atfork", reinterpret_cast<void*>(&bionic_register_atfork)},
    {"pthread_create", reinterpret_cast<void*>(&bionic_pthread_create)},
    {"pthread_attr_init", reinterpret_cast<void*>(&bionic_pthread_attr_init)},
    {"pthread_attr_destroy", reinterpret_cast<void*>(&bionic_pthread_attr_destroy)},
    {"pthread_attr_setstacksize", reinterpret_cast<void*>(&bionic_pthread_attr_setstacksize)},
    {"pthread_attr_getstack", reinterpret_cast<void*>(&bionic_pthread_attr_getstack)},
    {"pthread_attr_setdetachstate", reinterpret_cast<void*>(&bionic_pthread_attr_setdetachstate)},
    {"pthread_getattr_np", reinterpret_cast<void*>(&bionic_pthread_getattr_np)},
    {"pthread_condattr_init", reinterpret_cast<void*>(&bionic_pthread_condattr_init)},
    {"pthread_condattr_destroy", reinterpret_cast<void*>(&bionic_pthread_condattr_destroy)},
    {"pthread_mutexattr_init", reinterpret_cast<void*>(&bionic_pthread_mutexattr_init)},
    {"pthread_mutexattr_destroy", reinterpret_cast<void*>(&bionic_pthread_mutexattr_destroy)},
    {"pthread_mutexattr_settype", reinterpret_cast<void*>(&bionic_pthread_mutexattr_settype)},
    {"pthread_mutexattr_gettype", reinterpret_cast<void*>(&bionic_pthread_mutexattr_gettype)},
    {"pthread_mutex_trylock", reinterpret_cast<void*>(&bionic_pthread_mutex_trylock)},
    {"pthread_key_create", reinterpret_cast<void*>(&bionic_pthread_key_create)},
    {"pthread_setspecific", reinterpret_cast<void*>(&bionic_pthread_setspecific)},
    {"pthread_getspecific", reinterpret_cast<void*>(&bionic_pthread_getspecific)},
    {"pthread_key_delete", reinterpret_cast<void*>(&bionic_pthread_key_delete)},
    {"pthread_once", reinterpret_cast<void*>(&bionic_pthread_once)},
    {"sigaction", reinterpret_cast<void*>(&bionic_sigaction)},
    {"sigaltstack", reinterpret_cast<void*>(&bionic_sigaltstack)},
    // rt_sigaction is the symbol bionic's own sigaction() is built on, and some guests
    // import it directly.
    {"rt_sigaction", reinterpret_cast<void*>(&bionic_rt_sigaction)},
    // The OUTBOUND half of signal translation, which was missing entirely.
    //
    // sigaction translated the number on the way in, so a handler for Linux SIGPWR (30)
    // was installed on the host signal that carries it — Darwin's SIGINFO (29). raise,
    // kill and pthread_kill were not here at all, so they bound to the HOST libc through
    // dlsym(RTLD_DEFAULT) and the guest's Linux number went straight through:
    // pthread_kill(tid, 30) delivered Darwin signal 30, which is SIGUSR1, an unhandled
    // slot whose default action terminates the process.
    //
    // That is Mono's SIG_SUSPEND — how il2cpp stops threads for GC. Twelve of the
    // thirty-one named signals are misdirected this way, and neither side can see it: the
    // install succeeds and so does the send.
    {"raise", reinterpret_cast<void*>(&bionic_raise)},
    {"kill", reinterpret_cast<void*>(&bionic_kill)},
    {"pthread_kill", reinterpret_cast<void*>(&bionic_pthread_kill)},
    {"tkill", reinterpret_cast<void*>(&bionic_tkill)},
    {"abort", reinterpret_cast<void*>(&bionic_abort)},
    {"signal", reinterpret_cast<void*>(&bionic_signal)},
    {"bsd_signal", reinterpret_cast<void*>(&bionic_signal)},
    {"sysv_signal", reinterpret_cast<void*>(&bionic_signal)},
    // Masks: both the signal numbers in the set AND `how` differ. Linux numbers
    // SIG_BLOCK/UNBLOCK/SETMASK 0/1/2, Darwin numbers them 1/2/3, so an unshimmed
    // sigprocmask does not fail — it performs a different operation. A runtime that
    // blocks its suspend signal and unblocks it after ends up blocking it twice.
    {"sigprocmask", reinterpret_cast<void*>(&bionic_sigprocmask)},
    {"rt_sigprocmask", reinterpret_cast<void*>(&bionic_sigprocmask)},
    {"pthread_sigmask", reinterpret_cast<void*>(&bionic_pthread_sigmask)},
    {"sigsuspend", reinterpret_cast<void*>(&bionic_sigsuspend)},
    {"rt_sigsuspend", reinterpret_cast<void*>(&bionic_sigsuspend)},
    {"sigwait", reinterpret_cast<void*>(&bionic_sigwait)},
    // sigset_t operations on the GUEST's mask. Bit (n-1) is Linux signal n; the host's
    // macros number their bits by host signal, so binding these to the host would mix
    // the two schemes inside one word and have sigprocmask translate it a second time.
    {"sigemptyset", reinterpret_cast<void*>(&bionic_sigemptyset)},
    {"sigfillset", reinterpret_cast<void*>(&bionic_sigfillset)},
    {"sigaddset", reinterpret_cast<void*>(&bionic_sigaddset)},
    {"sigdelset", reinterpret_cast<void*>(&bionic_sigdelset)},
    {"sigismember", reinterpret_cast<void*>(&bionic_sigismember)},
    {"futex", reinterpret_cast<void*>(&bionic_futex)},
    {"__futex", reinterpret_cast<void*>(&bionic_futex)},
    {"futex_time64", reinterpret_cast<void*>(&bionic_futex)},
    {"__futex_time64", reinterpret_cast<void*>(&bionic_futex)},
    {"mremap", reinterpret_cast<void*>(&bionic_mremap)},
    {"gettid", reinterpret_cast<void*>(&bionic_gettid)},
    {"syscall", reinterpret_cast<void*>(&bionic_syscall)},
    {"getauxval", reinterpret_cast<void*>(&bionic_getauxval)},
    {"sysconf", reinterpret_cast<void*>(&bionic_sysconf)},
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
    
#ifdef __APPLE__
    {"__errno", reinterpret_cast<void*>(&__error)},
#else
    {"__errno", reinterpret_cast<void*>(&__errno_location)},
#endif
    // Variadic: routed through a register-capturing trampoline on arm64, because a guest
    // built for Linux AAPCS64 puts varargs in x0-x7/v0-v7 while Apple's arm64 ABI puts
    // them all on the stack. Forwarding straight to the host's snprintf made it read the
    // stack and print unrelated data — plausible-looking numbers, never an error.
#if defined(__aarch64__)
    {"snprintf", reinterpret_cast<void*>(&kudroid_snprintf_trampoline)},
    {"sprintf", reinterpret_cast<void*>(&kudroid_sprintf_trampoline)},
#else
    {"snprintf", reinterpret_cast<void*>(&snprintf)},
    {"sprintf", reinterpret_cast<void*>(&sprintf)},
#endif
    {"memcpy", reinterpret_cast<void*>(&memcpy)},

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
    {"close", reinterpret_cast<void*>(&bionic_close)},
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
    // Free-space queries. statfs64/fstatfs64 are the same call on arm64 (bionic defines
    // both names against one 64-bit layout), so they share an implementation.
    {"statfs", reinterpret_cast<void*>(&bionic_statfs)},
    {"statfs64", reinterpret_cast<void*>(&bionic_statfs)},
    {"fstatfs", reinterpret_cast<void*>(&bionic_fstatfs)},
    {"fstatfs64", reinterpret_cast<void*>(&bionic_fstatfs)},
    {"ioctl", reinterpret_cast<void*>(&bionic_ioctl)},
    {"prctl", reinterpret_cast<void*>(&bionic_prctl)},
    {"uname", reinterpret_cast<void*>(&bionic_uname)},
    {"__uname", reinterpret_cast<void*>(&bionic_uname)},
    {"pipe2", reinterpret_cast<void*>(&bionic_pipe2)},
    {"openat", reinterpret_cast<void*>(&bionic_openat)},
    {"openat64", reinterpret_cast<void*>(&bionic_openat)},
    {"fstatat", reinterpret_cast<void*>(&bionic_newfstatat)},
    {"fstatat64", reinterpret_cast<void*>(&bionic_newfstatat)},
    {"newfstatat", reinterpret_cast<void*>(&bionic_newfstatat)},
    {"getrandom", reinterpret_cast<void*>(&bionic_getrandom)},
    {"clock_gettime", reinterpret_cast<void*>(&bionic_clock_gettime)},
    {"__clock_gettime", reinterpret_cast<void*>(&bionic_clock_gettime)},
    {"clock_gettime64", reinterpret_cast<void*>(&bionic_clock_gettime64)},
    {"gettimeofday", reinterpret_cast<void*>(&::gettimeofday)},
    {"ashmem_create_region", reinterpret_cast<void*>(&bionic_ashmem_create_region)},
    {"ashmem_set_prot_region", reinterpret_cast<void*>(&bionic_ashmem_set_prot_region)},
    {"ashmem_set_name", reinterpret_cast<void*>(&bionic_ashmem_set_name)},
    {"mkdir", reinterpret_cast<void*>(&vfs_mkdir)},
    {"rmdir", reinterpret_cast<void*>(&vfs_rmdir)},
    {"opendir", reinterpret_cast<void*>(&vfs_opendir)},
    {"readdir", reinterpret_cast<void*>(&vfs_readdir)},
    // readdir64 is a distinct symbol in bionic and the one 64-bit guest code is built
    // against. Left unshimmed it resolved through dlsym to Darwin's readdir, whose
    // struct dirent has a different layout — d_name lands at the wrong offset, so the
    // guest reads filenames out of the middle of other fields and sees garbage rather
    // than an error.
    {"readdir64", reinterpret_cast<void*>(&vfs_readdir)},
    {"closedir", reinterpret_cast<void*>(&vfs_closedir)},
    {"readlink", reinterpret_cast<void*>(&vfs_readlink)},
    {"realpath", reinterpret_cast<void*>(&vfs_realpath)},
    {"pthread_join", reinterpret_cast<void*>(&bionic_pthread_join)},
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
    {"__cxa_atexit", reinterpret_cast<void*>(&bionic_cxa_atexit)},
    {"__cxa_thread_atexit_impl", reinterpret_cast<void*>(&bionic___cxa_thread_atexit_impl)},
    {"__cxa_guard_acquire", reinterpret_cast<void*>(&bionic___cxa_guard_acquire)},
    {"__cxa_guard_release", reinterpret_cast<void*>(&bionic___cxa_guard_release)},
    {"__cxa_guard_abort", reinterpret_cast<void*>(&bionic___cxa_guard_abort)},
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
    {"ALooper_acquire", reinterpret_cast<void*>(&bionic_ALooper_acquire)},
    {"ALooper_release", reinterpret_cast<void*>(&bionic_ALooper_release)},
    {"ALooper_addFd", reinterpret_cast<void*>(&bionic_ALooper_addFd)},
    {"ALooper_removeFd", reinterpret_cast<void*>(&bionic_ALooper_removeFd)},
    {"ALooper_wake", reinterpret_cast<void*>(&bionic_ALooper_wake)},
    {"ALooper_pollOnce", reinterpret_cast<void*>(&bionic_ALooper_pollOnce)},
    {"ALooper_pollAll", reinterpret_cast<void*>(&bionic_ALooper_pollAll)},


    // Memory alignment
    {"memalign", reinterpret_cast<void*>(&bionic_memalign)},

    // Android property system
    {"__system_property_get", reinterpret_cast<void*>(&bionic_system_property_get)},
    {"__system_property_find", reinterpret_cast<void*>(&bionic_system_property_find)},
    {"__system_property_read", reinterpret_cast<void*>(&bionic_system_property_read)},
    {"__system_property_read_callback", reinterpret_cast<void*>(&bionic_system_property_read_callback)},

    // ELF iteration
    {"dl_iterate_phdr", reinterpret_cast<void*>(&bionic_dl_iterate_phdr)},

    // Dynamic loader (dlfcn) — must route through the shim so the guest never
    // calls the real host dlopen with a nonexistent Android .so path.
    {"dlopen", reinterpret_cast<void*>(&bionic_dlopen)},
    {"dlsym", reinterpret_cast<void*>(&bionic_dlsym)},
    {"dlclose", reinterpret_cast<void*>(&bionic_dlclose)},
    {"dlerror", reinterpret_cast<void*>(&bionic_dlerror)},
    {"android_dlopen_ext", reinterpret_cast<void*>(&bionic_android_dlopen_ext)},

    // 64-bit file operations
    {"lseek64", reinterpret_cast<void*>(&bionic_lseek64)},
    {"pread64", reinterpret_cast<void*>(&bionic_pread64)},
    {"pwrite64", reinterpret_cast<void*>(&bionic_pwrite64)},
    {"ftruncate64", reinterpret_cast<void*>(&bionic_ftruncate64)},
    {"pipe2", reinterpret_cast<void*>(&bionic_pipe2)},
    {"clock_nanosleep", reinterpret_cast<void*>(&bionic_clock_nanosleep)},
    {"usleep", reinterpret_cast<void*>(&bionic_usleep)},
    {"tgkill", reinterpret_cast<void*>(&bionic_tgkill)},
    {"sendfile", reinterpret_cast<void*>(&bionic_sendfile)},
    {"sched_getscheduler", reinterpret_cast<void*>(&bionic_sched_getscheduler)},
    {"omp_in_parallel", reinterpret_cast<void*>(&bionic_omp_in_parallel)},
    {"__strchr_chk", reinterpret_cast<void*>(&bionic___strchr_chk)},
    {"__strncpy_chk2", reinterpret_cast<void*>(&bionic___strncpy_chk2)},
    {"__FD_CLR_chk", reinterpret_cast<void*>(&bionic___FD_CLR_chk)},
    {"__cmsg_nxthdr", reinterpret_cast<void*>(&bionic___cmsg_nxthdr)},

    {"splice", reinterpret_cast<void*>(&bionic_splice)},
    {"copy_file_range", reinterpret_cast<void*>(&bionic_copy_file_range)},
    {"__assert2", reinterpret_cast<void*>(&bionic___assert2)},
#if defined(__aarch64__)
    {"__android_log_assert", reinterpret_cast<void*>(&kudroid_log_assert_trampoline)},
#else
    {"__android_log_assert", reinterpret_cast<void*>(&bionic___android_log_assert)},
#endif
    {"__android_log_vprint", reinterpret_cast<void*>(&bionic_android_log_vprint)},
    {"__android_log_write", reinterpret_cast<void*>(&bionic_android_log_write)},
    {"__android_log_buf_write", reinterpret_cast<void*>(&bionic_android_log_buf_write)},

    // Character classification
    {"__ctype_get_mb_cur_max", reinterpret_cast<void*>(&bionic_ctype_get_mb_cur_max)},
    {"_ctype_", reinterpret_cast<void*>(&g_ctype_ptr)},

    // Bionic libm math functions (host standard <cmath>)
    {"sin", reinterpret_cast<void*>(static_cast<double(*)(double)>(&sin))},
    {"sinf", reinterpret_cast<void*>(&sinf)},
    {"cos", reinterpret_cast<void*>(static_cast<double(*)(double)>(&cos))},
    {"cosf", reinterpret_cast<void*>(&cosf)},
    {"tan", reinterpret_cast<void*>(static_cast<double(*)(double)>(&tan))},
    {"tanf", reinterpret_cast<void*>(&tanf)},
    {"asin", reinterpret_cast<void*>(static_cast<double(*)(double)>(&asin))},
    {"asinf", reinterpret_cast<void*>(&asinf)},
    {"acos", reinterpret_cast<void*>(static_cast<double(*)(double)>(&acos))},
    {"acosf", reinterpret_cast<void*>(&acosf)},
    {"atan", reinterpret_cast<void*>(static_cast<double(*)(double)>(&atan))},
    {"atanf", reinterpret_cast<void*>(&atanf)},
    {"atan2", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&atan2))},
    {"atan2f", reinterpret_cast<void*>(&atan2f)},
    {"sinh", reinterpret_cast<void*>(static_cast<double(*)(double)>(&sinh))},
    {"sinhf", reinterpret_cast<void*>(&sinhf)},
    {"cosh", reinterpret_cast<void*>(static_cast<double(*)(double)>(&cosh))},
    {"coshf", reinterpret_cast<void*>(&coshf)},
    {"tanh", reinterpret_cast<void*>(static_cast<double(*)(double)>(&tanh))},
    {"tanhf", reinterpret_cast<void*>(&tanhf)},
    {"exp", reinterpret_cast<void*>(static_cast<double(*)(double)>(&exp))},
    {"expf", reinterpret_cast<void*>(&expf)},
    {"exp2", reinterpret_cast<void*>(static_cast<double(*)(double)>(&exp2))},
    {"exp2f", reinterpret_cast<void*>(&exp2f)},
    {"expm1", reinterpret_cast<void*>(static_cast<double(*)(double)>(&expm1))},
    {"expm1f", reinterpret_cast<void*>(&expm1f)},
    {"log", reinterpret_cast<void*>(static_cast<double(*)(double)>(&log))},
    {"logf", reinterpret_cast<void*>(&logf)},
    {"log2", reinterpret_cast<void*>(static_cast<double(*)(double)>(&log2))},
    {"log2f", reinterpret_cast<void*>(&log2f)},
    {"log10", reinterpret_cast<void*>(static_cast<double(*)(double)>(&log10))},
    {"log10f", reinterpret_cast<void*>(&log10f)},
    {"log1p", reinterpret_cast<void*>(static_cast<double(*)(double)>(&log1p))},
    {"log1pf", reinterpret_cast<void*>(&log1pf)},
    {"pow", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&pow))},
    {"powf", reinterpret_cast<void*>(&powf)},
    {"sqrt", reinterpret_cast<void*>(static_cast<double(*)(double)>(&sqrt))},
    {"sqrtf", reinterpret_cast<void*>(&sqrtf)},
    {"cbrt", reinterpret_cast<void*>(static_cast<double(*)(double)>(&cbrt))},
    {"cbrtf", reinterpret_cast<void*>(&cbrtf)},
    {"hypot", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&hypot))},
    {"hypotf", reinterpret_cast<void*>(&hypotf)},
    {"ceil", reinterpret_cast<void*>(static_cast<double(*)(double)>(&ceil))},
    {"ceilf", reinterpret_cast<void*>(&ceilf)},
    {"floor", reinterpret_cast<void*>(static_cast<double(*)(double)>(&floor))},
    {"floorf", reinterpret_cast<void*>(&floorf)},
    {"trunc", reinterpret_cast<void*>(static_cast<double(*)(double)>(&trunc))},
    {"truncf", reinterpret_cast<void*>(&truncf)},
    {"round", reinterpret_cast<void*>(static_cast<double(*)(double)>(&round))},
    {"roundf", reinterpret_cast<void*>(&roundf)},
    {"rint", reinterpret_cast<void*>(static_cast<double(*)(double)>(&rint))},
    {"rintf", reinterpret_cast<void*>(&rintf)},
    {"nearbyint", reinterpret_cast<void*>(static_cast<double(*)(double)>(&nearbyint))},
    {"nearbyintf", reinterpret_cast<void*>(&nearbyintf)},
    {"fabs", reinterpret_cast<void*>(static_cast<double(*)(double)>(&fabs))},
    {"fabsf", reinterpret_cast<void*>(&fabsf)},
    {"fmod", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&fmod))},
    {"fmodf", reinterpret_cast<void*>(&fmodf)},
    {"remainder", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&remainder))},
    {"remainderf", reinterpret_cast<void*>(&remainderf)},
    {"fmin", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&fmin))},
    {"fminf", reinterpret_cast<void*>(&fminf)},
    {"fmax", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&fmax))},
    {"fmaxf", reinterpret_cast<void*>(&fmaxf)},
    {"fdim", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&fdim))},
    {"fdimf", reinterpret_cast<void*>(&fdimf)},
    {"fma", reinterpret_cast<void*>(static_cast<double(*)(double, double, double)>(&fma))},
    {"fmaf", reinterpret_cast<void*>(&fmaf)},
    {"ldexp", reinterpret_cast<void*>(static_cast<double(*)(double, int)>(&ldexp))},
    {"ldexpf", reinterpret_cast<void*>(&ldexpf)},
    {"frexp", reinterpret_cast<void*>(static_cast<double(*)(double, int*)>(&frexp))},
    {"frexpf", reinterpret_cast<void*>(&frexpf)},
    {"modf", reinterpret_cast<void*>(static_cast<double(*)(double, double*)>(&modf))},
    {"modff", reinterpret_cast<void*>(&modff)},
    {"scalbn", reinterpret_cast<void*>(static_cast<double(*)(double, int)>(&scalbn))},
    {"scalbnf", reinterpret_cast<void*>(&scalbnf)},
    {"copysign", reinterpret_cast<void*>(static_cast<double(*)(double, double)>(&copysign))},
    {"copysignf", reinterpret_cast<void*>(&copysignf)},
    {"nan", reinterpret_cast<void*>(&nan)},
    {"nanf", reinterpret_cast<void*>(&nanf)},
    {"sincos", reinterpret_cast<void*>(&bionic_sincos)},
    {"sincosf", reinterpret_cast<void*>(&bionic_sincosf)},
    {"__strlen_chk", reinterpret_cast<void*>(&bionic___strlen_chk)},
    {"__FD_SET_chk", reinterpret_cast<void*>(&bionic___FD_SET_chk)},
    {"__FD_ISSET_chk", reinterpret_cast<void*>(&bionic___FD_ISSET_chk)},
    {"sem_init", reinterpret_cast<void*>(&bionic_sem_init)},
    {"sem_destroy", reinterpret_cast<void*>(&bionic_sem_destroy)},
    {"sem_wait", reinterpret_cast<void*>(&bionic_sem_wait)},
    {"sem_trywait", reinterpret_cast<void*>(&bionic_sem_trywait)},
    {"sem_post", reinterpret_cast<void*>(&bionic_sem_post)},
    {"sem_getvalue", reinterpret_cast<void*>(&bionic_sem_getvalue)},
    {"sem_timedwait", reinterpret_cast<void*>(&bionic_sem_timedwait)},
    {"android_set_abort_message", reinterpret_cast<void*>(&bionic_android_set_abort_message)},
    {"__memcpy_chk", reinterpret_cast<void*>(&bionic___memcpy_chk)},
    {"__memmove_chk", reinterpret_cast<void*>(&bionic___memmove_chk)},
    {"__memset_chk", reinterpret_cast<void*>(&bionic___memset_chk)},
    {"__read_chk", reinterpret_cast<void*>(&bionic___read_chk)},
    {"__write_chk", reinterpret_cast<void*>(&bionic___write_chk)},
    // The _FORTIFY_SOURCE forms, which is what a release-built guest calls instead of
    // snprintf/sprintf. Same ABI problem as the plain versions, so the same trampoline
    // treatment. The v* forms take a guest va_list, which is a 32-byte composite passed by
    // reference where Apple's is a char* passed by value — the same split from the other
    // direction, handled by the shims above rather than by a trampoline.
#if defined(__aarch64__)
    {"__snprintf_chk", reinterpret_cast<void*>(&kudroid_snprintf_chk_trampoline)},
    {"__sprintf_chk", reinterpret_cast<void*>(&kudroid_sprintf_chk_trampoline)},
#else
    {"__snprintf_chk", reinterpret_cast<void*>(&bionic___snprintf_chk)},
    {"__sprintf_chk", reinterpret_cast<void*>(&bionic___sprintf_chk)},
#endif
    {"__vsnprintf_chk", reinterpret_cast<void*>(&bionic___vsnprintf_chk)},
    {"__vsprintf_chk", reinterpret_cast<void*>(&bionic___vsprintf_chk)},
    {"__vfprintf_chk", reinterpret_cast<void*>(&bionic___vfprintf_chk)},
    // The plain v* family. Previously absent from every table, so they fell through to
    // dlsym(RTLD_DEFAULT) and reached the HOST implementation directly — the worst case,
    // because the host reads a guest va_list as a stack cursor and prints plausible
    // rubbish rather than failing.
    {"vsnprintf", reinterpret_cast<void*>(&bionic_vsnprintf)},
    {"vsprintf", reinterpret_cast<void*>(&bionic_vsprintf)},
    {"vfprintf", reinterpret_cast<void*>(&bionic_vfprintf)},
    {"vprintf", reinterpret_cast<void*>(&bionic_vprintf)},
    {"vasprintf", reinterpret_cast<void*>(&bionic_vasprintf)},
    // The scanf family. These were absent too, and reaching the host implementation is
    // worse here than for printf: scanf WRITES through its varargs. Minecraft's UUID parse
    // passes eleven output pointers, and the host took five of them from stack words that
    // were never pointers — one held a jobject (reported later as clazz=0xf8ec9809), and
    // the next store went to address 0.
#if defined(__aarch64__)
    {"sscanf", reinterpret_cast<void*>(&kudroid_sscanf_trampoline)},
    {"__isoc99_sscanf", reinterpret_cast<void*>(&kudroid_isoc99_sscanf_trampoline)},
    {"fscanf", reinterpret_cast<void*>(&kudroid_fscanf_trampoline)},
    {"__isoc99_fscanf", reinterpret_cast<void*>(&kudroid_fscanf_trampoline)},
#else
    {"sscanf", reinterpret_cast<void*>(&bionic_sscanf)},
    {"__isoc99_sscanf", reinterpret_cast<void*>(&bionic_sscanf)},
    {"fscanf", reinterpret_cast<void*>(&bionic_fscanf)},
    {"__isoc99_fscanf", reinterpret_cast<void*>(&bionic_fscanf)},
#endif
    {"vsscanf", reinterpret_cast<void*>(&bionic_vsscanf)},
    {"__isoc99_vsscanf", reinterpret_cast<void*>(&bionic_vsscanf)},
    {"vfscanf", reinterpret_cast<void*>(&bionic_vfscanf)},
    {"__strncpy_chk", reinterpret_cast<void*>(&bionic___strncpy_chk)},
    {"__strcpy_chk", reinterpret_cast<void*>(&bionic___strcpy_chk)},
    {"__strcat_chk", reinterpret_cast<void*>(&bionic___strcat_chk)},
    {"__fdelt_chk", reinterpret_cast<void*>(&bionic___fdelt_chk)},
    {"__open_2", reinterpret_cast<void*>(&bionic___open_2)},
    {"__openat_2", reinterpret_cast<void*>(&bionic___openat_2)},
    {"__umask_chk", reinterpret_cast<void*>(&bionic___umask_chk)},
    {"__strrchr_chk", reinterpret_cast<void*>(&bionic___strrchr_chk)},
    {"sysinfo", reinterpret_cast<void*>(&bionic_sysinfo)},

    // pthread extensions
    {"pthread_condattr_setclock", reinterpret_cast<void*>(&bionic_pthread_condattr_setclock)},

    // Google internal
    {"__google_potentially_blocking_region_begin", reinterpret_cast<void*>(&bionic_google_potentially_blocking_region_begin)},
    {"__google_potentially_blocking_region_end", reinterpret_cast<void*>(&bionic_google_potentially_blocking_region_end)},

    // Additional Linux syscalls
    {"getcpu", reinterpret_cast<void*>(&bionic_getcpu)},
    {"sched_getaffinity", reinterpret_cast<void*>(&bionic_sched_getaffinity)},
    {"sched_setaffinity", reinterpret_cast<void*>(&bionic_sched_setaffinity)},
    // CPU_COUNT() expands to this. Missing here meant it bound to the universal dummy
    // and returned 0 for every mask, which is the shape of Unity's "Cores = 0".
    {"__sched_cpucount", reinterpret_cast<void*>(&bionic___sched_cpucount)},
    {"sched_getcpu", reinterpret_cast<void*>(&bionic_sched_getcpu)},
    {"get_nprocs", reinterpret_cast<void*>(&bionic_get_nprocs)},
    {"get_nprocs_conf", reinterpret_cast<void*>(&bionic_get_nprocs_conf)},
    {"get_phys_pages", reinterpret_cast<void*>(&bionic_get_phys_pages)},
    {"get_avphys_pages", reinterpret_cast<void*>(&bionic_get_avphys_pages)},
    {"inotify_init1", reinterpret_cast<void*>(&bionic_inotify_init1)},
    {"inotify_add_watch", reinterpret_cast<void*>(&bionic_inotify_add_watch)},
    {"inotify_rm_watch", reinterpret_cast<void*>(&bionic_inotify_rm_watch)},
    {"signalfd", reinterpret_cast<void*>(&bionic_signalfd)},
    {"eventfd2", reinterpret_cast<void*>(&bionic_eventfd2)},
    {"prlimit64", reinterpret_cast<void*>(&bionic_prlimit64)},
    {"statx", reinterpret_cast<void*>(&bionic_statx)},

    // JNI and JVM Runtime Bridge
    {"kudroid_jni_get_javavm", reinterpret_cast<void*>(&kudroid_jni_get_javavm)},
    {"JNI_GetCreatedJavaVMs", reinterpret_cast<void*>(&JNI_GetCreatedJavaVMs)},
    {"JNI_CreateJavaVM", reinterpret_cast<void*>(&JNI_CreateJavaVM)},

    // Android Runtime Permissions Bridge
    {"kudroid_check_permission", reinterpret_cast<void*>(&kudroid_check_permission)},
    {"kudroid_set_group_permission", reinterpret_cast<void*>(&kudroid_set_group_permission)},
    {"kudroid_is_group_granted", reinterpret_cast<void*>(&kudroid_is_group_granted)},
    {"kudroid_grant_all_permissions", reinterpret_cast<void*>(&kudroid_grant_all_permissions)},
    {"kudroid_get_app_permissions_json", reinterpret_cast<void*>(&kudroid_get_app_permissions_json)},
    {"kudroid_set_app_permissions_json", reinterpret_cast<void*>(&kudroid_set_app_permissions_json)},

    // Zstandard Compression Tracing Symbols
    {"ZSTD_trace_compress_begin", reinterpret_cast<void*>(&bionic_ZSTD_trace_compress_begin)},
    {"ZSTD_trace_compress_end", reinterpret_cast<void*>(&bionic_ZSTD_trace_compress_end)},
    {"ZSTD_trace_decompress_begin", reinterpret_cast<void*>(&bionic_ZSTD_trace_decompress_begin)},
    {"ZSTD_trace_decompress_end", reinterpret_cast<void*>(&bionic_ZSTD_trace_decompress_end)},

    // Android NDK Bitmap API
    {"AndroidBitmap_getInfo", reinterpret_cast<void*>(&bionic_AndroidBitmap_getInfo)},
    {"AndroidBitmap_lockPixels", reinterpret_cast<void*>(&bionic_AndroidBitmap_lockPixels)},
    {"AndroidBitmap_unlockPixels", reinterpret_cast<void*>(&bionic_AndroidBitmap_unlockPixels)},

    // Bionic Fortify / String / Assertion Shims
    {"__gnu_strerror_r", reinterpret_cast<void*>(&bionic___gnu_strerror_r)},
    {"__fwrite_chk", reinterpret_cast<void*>(&bionic___fwrite_chk)},
    {"__strchr_chk", reinterpret_cast<void*>(&bionic___strchr_chk)},
    {"omp_in_parallel", reinterpret_cast<void*>(&bionic_omp_in_parallel)},
    {"copy_file_range", reinterpret_cast<void*>(&bionic_copy_file_range)},
    {"splice", reinterpret_cast<void*>(&bionic_splice)},
    {"__assert2", reinterpret_cast<void*>(&bionic___assert2)},
#if defined(__aarch64__)
    {"__android_log_assert", reinterpret_cast<void*>(&kudroid_log_assert_trampoline)},
#else
    {"__android_log_assert", reinterpret_cast<void*>(&bionic___android_log_assert)},
#endif

    // KuDroid Screen Orientation, Haptic Vibrator & Sensor Bridge
    {"kudroid_vibrate", reinterpret_cast<void*>(&kudroid_vibrate)},
    {"kudroid_set_requested_orientation", reinterpret_cast<void*>(&kudroid_set_requested_orientation)},
    {"kudroid_get_requested_orientation", reinterpret_cast<void*>(&kudroid_get_requested_orientation)},
    {"kudroid_inject_sensor_event", reinterpret_cast<void*>(&kudroid_inject_sensor_event)},

    // Bionic malloc extensions (jemalloc/BoringSSL)
    {"malloc_usable_size", reinterpret_cast<void*>(&bionic_malloc_usable_size)},
    {"mallinfo", reinterpret_cast<void*>(&bionic_mallinfo)},
    {"sdallocx", reinterpret_cast<void*>(&bionic_sdallocx)},
    {"OPENSSL_memory_alloc", reinterpret_cast<void*>(&bionic_OPENSSL_memory_alloc)},
    {"OPENSSL_memory_free", reinterpret_cast<void*>(&bionic_OPENSSL_memory_free)},
    {"OPENSSL_memory_get_size", reinterpret_cast<void*>(&bionic_OPENSSL_memory_get_size)},

    // Linux-only syscalls emulated on iOS
    {"memfd_create", reinterpret_cast<void*>(&bionic_memfd_create)},

    // pthread / stdio extensions
    {"pthread_gettid_np", reinterpret_cast<void*>(&bionic_pthread_gettid_np)},
    {"__pthread_cleanup_push", reinterpret_cast<void*>(&bionic___pthread_cleanup_push)},
    {"__pthread_cleanup_pop", reinterpret_cast<void*>(&bionic___pthread_cleanup_pop)},
    {"__fgets_chk", reinterpret_cast<void*>(&bionic___fgets_chk)},
    {"freopen64", reinterpret_cast<void*>(&vfs_freopen)},

    // ── Added after Minecraft/libmaesdk null-dereference fixes ─────────────────────
    // strlcpy/strlcat: available natively in Darwin libc (BSD origin).
    {"strlcpy", reinterpret_cast<void*>(&::strlcpy)},
    {"strlcat", reinterpret_cast<void*>(&::strlcat)},
    // isatty/getpagesize/tkill: mapped to host libc equivalents.
    {"isatty", reinterpret_cast<void*>(&::isatty)},
    {"getpagesize", reinterpret_cast<void*>(&::getpagesize)},
    {"tkill", reinterpret_cast<void*>(&bionic_tkill)},
    // pthread_setname_np: 2-parameter Bionic overload wrapper.
    {"pthread_setname_np", reinterpret_cast<void*>(&bionic_pthread_setname_np2)},
    // setprogname/getprogname: Darwin native / Linux host fallback.
    {"setprogname", reinterpret_cast<void*>(&bionic_setprogname)},
    {"getprogname", reinterpret_cast<void*>(&bionic_getprogname)},
    // mallopt/malloc_info stubs for platform parity.
    {"mallopt", reinterpret_cast<void*>(&bionic_mallopt)},
    {"malloc_info", reinterpret_cast<void*>(&bionic_malloc_info)},
};

} // namespace

const SymbolEntry* get_syscall_symbols(size_t* count) {
    if (count) {
        *count = sizeof(kSyscallSymbols) / sizeof(SymbolEntry);
    }
    return kSyscallSymbols;
}

} // namespace kudroid
