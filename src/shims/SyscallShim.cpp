#include "kudroid/BionicShim.h"
#include "kudroid/shims/SyscallShim.h"
#include "kudroid/shims/GraphicsShim.h"
#include "kudroid/shims/InputShim.h"
#include "kudroid/shims/AudioShim.h"
#include "kudroid/VFSPathRemapper.h"

#include <cmath>
#include <ctime>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
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
// Khai báo trực tiếp 2 hàm autorelease pool của libobjc thay vì include
// <objc/objc-autoreleasepool.h> — header này không nằm trong search path của
// SDK trên một số runner macOS, còn 2 symbol này là ABI public (ARC dựa vào
// chúng) nên khai báo extern "C" là an toàn trên cả iOS lẫn macOS.
extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void* pool);
#endif

extern "C" void __gxx_personality_v0();

// Mirror một dòng log vào crash buffer (kudroid_bridge.cpp) để kudroid_crash.log
// chứa log của game ngay trước khi crash — trước đây chỉ có log của kudroid_core.
extern "C" void kudroid_append_crash_log(const char* text, size_t len);

// Lưu abort message (android_set_abort_message) — crash handler in nó ra.
extern "C" void kudroid_store_abort_message(const char* msg);

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
#include <cstring>
#include <cerrno>
#include <pthread.h>
#include <unordered_map>
#include <map>
#include <set>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <vector>
#include <string>
#include <array>
#include <memory>
#include <dlfcn.h>

extern const char* g_kudroid_log_dir_ptr;

namespace kudroid {
namespace {

uintptr_t gStackCheckGuard = 0x4b7544726f696421ULL;

// Trace dùng chung với BionicShim.cpp — KHÔNG khai báo gShimTrace riêng ở đây
// (trước đây có 2 thread_local khác nhau → bionic_shim_trace() luôn trống).
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

[[maybe_unused]] std::string formatGuestLog(const char* format,
                                            const uint64_t* arguments,
                                            const uint64_t* stackArguments) {
    std::string output;
    if (!format) return output;
    std::size_t argumentIndex = 0;
    // Giới hạn số argument đọc từ guest: 5 register (arguments) + tối đa 11 từ
    // stack. Format có nhiều specifier hơn argument (format lỗi/không khớp) sẽ
    // không đọc quá đà vào stack guest (trước đây đọc vô hạn → có thể chạm
    // vùng nhớ không map gây crash). Ngoài giới hạn trả 0 an toàn.
    constexpr std::size_t kMaxLogArguments = 16;
    auto nextArgument = [&]() -> uint64_t {
        if (argumentIndex >= kMaxLogArguments ||
            (argumentIndex >= 5 && !stackArguments)) {
            return 0;
        }
        const uint64_t value = argumentIndex < 5 ? arguments[argumentIndex]
                                                 : stackArguments[argumentIndex - 5];
        ++argumentIndex;
        return value;
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
        // Bỏ qua cờ/flags/width/precision (vd %.9ld, %-5d, %+d, %08x, %8x) —
        // game thường dùng %lld, %d, %s, %p, %x, %.9ld... Không parse chính xác
        // 100% nhưng đủ để log không hỏng.
        while (*cursor == '-' || *cursor == '+' || *cursor == ' ' ||
               *cursor == '#' || *cursor == '0' || *cursor == '\'') ++cursor;
        // width (vd %8x, %08x — chữ số đứng sau flags, trước precision)
        while (*cursor >= '0' && *cursor <= '9') ++cursor;
        while (*cursor == '.') {
            ++cursor;
            while (*cursor >= '0' && *cursor <= '9') ++cursor;
            if (*cursor == '*') ++cursor;
        }
        while (*cursor == 'l' || *cursor == 'z' || *cursor == 'j' ||
               *cursor == 't' || *cursor == 'h') ++cursor;
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
    {
        std::string full = "android log message: ";
        full += message;
        trace_shim(full.c_str());
    }
    
    // Dump to standard output (Xcode/syslog)
    fprintf(stdout, "[AndroidLog][%s]: %s\n", tag ? tag : "unknown", message.c_str());
    
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
    // log up to the crash (trước đây phần "log up to crash" luôn trống).
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

extern "C" int bionic_cxa_atexit(void (*)(void*), void*, void*) {
    trace("__cxa_atexit() -> no-op");
    return 0;
}

extern "C" void bionic_runtime_noop() {
    trace("weak compiler runtime hook -> no-op");
}


static std::unordered_map<void*, void*> gSyncRegistry;
static std::shared_mutex gSyncRegistryLock;

// Sync object types.
enum SyncType : int {
    SYNC_MUTEX = 1,
    SYNC_COND = 2,
    SYNC_RWLOCK = 3,
};

static inline void* create_sync_obj(int type) {
    if (type == SYNC_MUTEX) {
        auto* hostMutex = static_cast<pthread_mutex_t*>(std::malloc(sizeof(pthread_mutex_t)));
        if (!hostMutex) return nullptr;
        if (::pthread_mutex_init(hostMutex, nullptr) != 0) {
            std::free(hostMutex);
            return nullptr;
        }
        return hostMutex;
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
    if (type == SYNC_MUTEX) ::pthread_mutex_destroy(static_cast<pthread_mutex_t*>(host_obj));
    else if (type == SYNC_COND) ::pthread_cond_destroy(static_cast<pthread_cond_t*>(host_obj));
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

    void* host_obj = create_sync_obj(type);
    if (!host_obj) return nullptr;

    gSyncRegistry[guest_ptr] = host_obj;
    return host_obj;
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
    trace("pthread_mutex_init()");

    // Bionic pthread_mutexattr_t: kiểu (PTHREAD_MUTEX_RECURSIVE=1, ERRORCHECK=2)
    // nằm ở 2 bit thấp của từ 4 byte đầu (little-endian). Nếu game tạo mutex
    // recursive mà host dùng mutex thường → tự khóa chính nó → deadlock treo.
    int type = 0;
    if (attr) type = (*static_cast<const uint32_t*>(attr)) & 0x3;

    void* hostMutex = create_sync_obj(SYNC_MUTEX);
    if (!hostMutex) return -1;
    if (type == 1 || type == 2) { // RECURSIVE hoặc ERRORCHECK
        pthread_mutexattr_t ma;
        ::pthread_mutexattr_init(&ma);
        ::pthread_mutexattr_settype(&ma, type == 1 ? PTHREAD_MUTEX_RECURSIVE
                                                   : PTHREAD_MUTEX_ERRORCHECK);
        const int rc = ::pthread_mutex_init(static_cast<pthread_mutex_t*>(hostMutex), &ma);
        ::pthread_mutexattr_destroy(&ma);
        if (rc != 0) {
            std::free(hostMutex);
            return rc;
        }
    }

    std::unique_lock<std::shared_mutex> lock(gSyncRegistryLock);
    gSyncRegistry[guestMutex] = hostMutex;
    trace("pthread_mutex_init() -> 0");
    return 0;
}

extern "C" int bionic_pthread_cond_init(void* cond, const void* attr) {
    (void)attr;
    void* hostCond = create_sync_obj(SYNC_COND);
    if (!hostCond) return -1;
    
    std::unique_lock<std::shared_mutex> lock(gSyncRegistryLock);
    gSyncRegistry[cond] = hostCond;
    return 0;
}

extern "C" int bionic_pthread_rwlock_init(void* rwlock, const void* attr) {
    (void)attr;
    void* hostRwlock = create_sync_obj(SYNC_RWLOCK);
    if (!hostRwlock) return -1;
    
    std::unique_lock<std::shared_mutex> lock(gSyncRegistryLock);
    gSyncRegistry[rwlock] = hostRwlock;
    return 0;
}



extern "C" int bionic_pthread_mutex_lock(void* guestMutex) {
    trace("pthread_mutex_lock()");
    pthread_mutex_t* hostMutex = static_cast<pthread_mutex_t*>(get_or_init_sync(guestMutex, SYNC_MUTEX));
    const int result = hostMutex ? ::pthread_mutex_lock(hostMutex) : -1;
    trace(result == 0 ? "pthread_mutex_lock() -> 0" : "pthread_mutex_lock() -> error");
    return result;
}

extern "C" int bionic_pthread_mutex_unlock(void* guestMutex) {
    trace("pthread_mutex_unlock()");
    pthread_mutex_t* hostMutex = static_cast<pthread_mutex_t*>(get_or_init_sync(guestMutex, SYNC_MUTEX));
    const int result = hostMutex ? ::pthread_mutex_unlock(hostMutex) : -1;
    trace(result == 0 ? "pthread_mutex_unlock() -> 0" : "pthread_mutex_unlock() -> error");
    return result;
}

extern "C" int bionic_pthread_mutex_destroy(void* guestMutex) {
    trace("pthread_mutex_destroy()");
    destroy_sync(guestMutex, SYNC_MUTEX);
    trace("pthread_mutex_destroy() -> 0");
    return 0;
}

// --- Cond and Rwlock Wrappers ---
extern "C" int bionic_pthread_cond_destroy(void* cond) {
    destroy_sync(cond, SYNC_COND);
    return 0;
}
extern "C" int bionic_pthread_cond_wait(void* cond, void* mutex) {
    pthread_cond_t* hostCond = static_cast<pthread_cond_t*>(get_or_init_sync(cond, SYNC_COND));
    pthread_mutex_t* hostMutex = static_cast<pthread_mutex_t*>(get_or_init_sync(mutex, SYNC_MUTEX));
    if (!hostCond || !hostMutex) return -1;
    return ::pthread_cond_wait(hostCond, hostMutex);
}
extern "C" int bionic_pthread_cond_timedwait(void* cond, void* mutex, const struct timespec* abstime) {
    pthread_cond_t* hostCond = static_cast<pthread_cond_t*>(get_or_init_sync(cond, SYNC_COND));
    pthread_mutex_t* hostMutex = static_cast<pthread_mutex_t*>(get_or_init_sync(mutex, SYNC_MUTEX));
    if (!hostCond || !hostMutex) return -1;
    return ::pthread_cond_timedwait(hostCond, hostMutex, abstime);
}
extern "C" int bionic_pthread_cond_signal(void* cond) {
    pthread_cond_t* hostCond = static_cast<pthread_cond_t*>(get_or_init_sync(cond, SYNC_COND));
    return hostCond ? ::pthread_cond_signal(hostCond) : -1;
}
extern "C" int bionic_pthread_cond_broadcast(void* cond) {
    pthread_cond_t* hostCond = static_cast<pthread_cond_t*>(get_or_init_sync(cond, SYNC_COND));
    return hostCond ? ::pthread_cond_broadcast(hostCond) : -1;
}

extern "C" int bionic_pthread_rwlock_destroy(void* rwlock) {
    destroy_sync(rwlock, SYNC_RWLOCK);
    return 0;
}
extern "C" int bionic_pthread_rwlock_rdlock(void* rwlock) {
    pthread_rwlock_t* hostRwlock = static_cast<pthread_rwlock_t*>(get_or_init_sync(rwlock, SYNC_RWLOCK));
    return hostRwlock ? ::pthread_rwlock_rdlock(hostRwlock) : -1;
}
extern "C" int bionic_pthread_rwlock_wrlock(void* rwlock) {
    pthread_rwlock_t* hostRwlock = static_cast<pthread_rwlock_t*>(get_or_init_sync(rwlock, SYNC_RWLOCK));
    return hostRwlock ? ::pthread_rwlock_wrlock(hostRwlock) : -1;
}
extern "C" int bionic_pthread_rwlock_unlock(void* rwlock) {
    pthread_rwlock_t* hostRwlock = static_cast<pthread_rwlock_t*>(get_or_init_sync(rwlock, SYNC_RWLOCK));
    return hostRwlock ? ::pthread_rwlock_unlock(hostRwlock) : -1;
}

extern "C" void bionic_stack_chk_fail() {
    fprintf(stderr, "Bionic shim: stack check failed\n");
    std::abort();
}



// --- Pthread Overrides ---
extern "C" int vfs_fstat(int fd, void* info);
extern "C" int vfs_fstat64(int fd, void* info);

// Bionic pthread_attr_t (arm64) — thứ tự các trường theo bionic bits/pthread_types.h.
struct BionicPthreadAttr {
    uint32_t flags;        // bit 0 = PTHREAD_ATTR_FLAG_DETACHED
    uint32_t pad0;
    void* stack_base;
    size_t stack_size;
    size_t guard_size;
    int32_t sched_policy;
    int32_t sched_priority;
};

// THẬT (trước đây là dummy trả 0 không ghi gì): bionic_pthread_create đọc
// stack_size từ attr này — nếu không init, game đọc stack_size rác →
// pthread_attr_setstacksize(host, rác) → pthread_create fail EINVAL → game
// không tạo được thread (treo/crash).
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
    // GHI output — dummy cũ không ghi gì, game đọc stackaddr/stacksize rác.
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
    // Lấy stack size thật của thread host (guest thường gọi để tự quyết độ sâu stack).
    // pthread_getattr_np là API glibc/Linux — KHÔNG tồn tại trên macOS, dùng
    // pthread_get_stackaddr_np/get_stacksize_np (Apple) thay thế.
#ifdef __APPLE__
    void* saddr = ::pthread_get_stackaddr_np(thread);
    const size_t ssize = ::pthread_get_stacksize_np(thread);
    if (saddr) a->stack_base = saddr;
    if (ssize > 0) a->stack_size = ssize;
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
    // Không có ioctl nào được mô phỏng. Trả lỗi rõ ràng thay vì giả vờ thành công
    // với 0 — game query kích thước màn hình/display sẽ đọc rác từ buffer nếu
    // ta trả 0 mà không ghi gì.
    errno = ENOTTY;
    return -1;
}

#define PR_SET_NAME 15
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
    } else if (option == PR_SET_VMA) {
        // Just fake it for PR_SET_VMA_ANON_NAME to prevent crashes
        logAndroidMessage(4, "KuDroidSyscall", "prctl(PR_SET_VMA) faked -> 0");
        return 0;
    }
    logAndroidMessage(4, "KuDroidSyscall", "prctl(option=0x" +
                      std::to_string((unsigned)option) + ") faked -> 0");
    return 0;
}

// Kiểm tra trong bionic_mmap: fd ashmem chỉ map được với prot đã được cấp
// (định nghĩa ở khối ashmem phía dưới).
static bool ashmem_prot_allows(int fd, int prot);
// Fake ashmem fd (iOS fallback): trả vùng nhớ đã cấp hoặc nullptr nếu không
// phải fake fd (định nghĩa ở khối ashmem phía dưới).
extern "C" void* bionic_ashmem_mmap_fd(int fd, size_t length);

// Memory mapping wrappers to strip Linux specific flags
extern "C" void* bionic_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    // ashmem fd: chỉ map được với prot đã cấp (bionic_ashmem_set_prot_region).
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

    // ashmem fake fd (fallback iOS): trả lại vùng nhớ đã cấp — không cần
    // dịch flags, vùng là anonymous rồi.
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

    // QUAN TRỌNG: strip Linux-only flags từ INPUT (flags) TRƯỚC, không strip
    // darwin_flags SAU khi đã set — vì LINUX_ONLY_MAP_FLAGS chứa bit 0x1000
    // (MAP_EXECUTABLE) mà Darwin MAP_ANON cũng dùng bit 0x1000 → strip sau
    // sẽ xóa luôn MAP_ANON vừa set → mmap anonymous mất MAP_ANON với fd=-1
    // → EINVAL → MAP_FAILED (bug đã gặp trong syscall test).
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
    if (prot & PROT_EXEC) {
        logAndroidMessage(4, "KuDroidSyscall", "bionic_mprotect: setting PROT_EXEC on memory at " + std::to_string((uintptr_t)addr) + " len=" + std::to_string(len));
    }
    const int r = ::mprotect(addr, len, prot);
    logAndroidMessage(3, "KuDroidSyscall", "mprotect(addr=" + std::to_string((uintptr_t)addr) +
                      ", len=" + std::to_string(len) + ", prot=0x" +
                      [](int p){ char b[16]; snprintf(b, sizeof(b), "%x", p); return std::string(b); }(prot) +
                      ") -> " + (r == 0 ? "0" : "-1 errno=" + std::to_string(errno)));
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
    int darwin_clock_id = clock_id;
    logAndroidMessage(3, "KuDroidSyscall", "clock_gettime(clock_id=" + std::to_string(clock_id) + ")");
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
    }
#endif
    return ::clock_gettime(static_cast<clockid_t>(darwin_clock_id), tp);
}

extern "C" int bionic_clock_gettime64(int clock_id, struct timespec *tp) {
    return bionic_clock_gettime(clock_id, tp);
}

extern "C" int bionic_sigaltstack(const stack_t *ss, stack_t *oss) {
    const int r = ::sigaltstack(ss, oss);
    if (r != 0) {
        // iOS có thể từ chối (thread đang dùng altstack, size lạ, v.v.) —
        // fake success để game không chết vì thiếu altstack; vẫn log để debug.
        logAndroidMessage(4, "KuDroidSyscall", "sigaltstack() -> -1 errno=" +
                          std::to_string(errno) + " (faked to 0)");
        return 0;
    }
    return 0;
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

// ─────────────────────────────────────────────────────────────────────────────
// bionic: long syscall(long number, ...)
// Guest libc/libc++ gọi syscall() TRỰC TIẾP với SỐ HIỆU LINUX. Trước đây
// `syscall` không có trong shim table → resolve qua dlsym(RTLD_DEFAULT) →
// syscall macOS chạy với số Linux → KÊU NHẦM syscall kernel (vd Linux 178 =
// SYS_gettid, nhưng macOS 178 = setrlimit!). Hệ quả nghiêm trọng: libc++abi
// (trong libc++_shared.so của Discord) lấy thread-id cho guard static bằng
// syscall(SYS_gettid) → trả rác/giá trị giống nhau cho MỌI thread →
// __cxa_guard_acquire thấy guard "đang init dở bởi chính thread này" → abort
// "recursive initialization" dù không hề đệ quy.
// Fix: map số Linux sang hành vi host đúng. Số chưa biết → ENOSYS (an toàn
// hơn chạy nhầm syscall macOS với arg guest).
// ─────────────────────────────────────────────────────────────────────────────
#ifndef SYS_gettid
#define SYS_gettid 178 // Linux arm64/x86_64 gettid
#endif
#ifndef SYS_getpid
#define SYS_getpid 39 // Linux getpid
#endif

extern "C" long bionic_syscall(long number, ...) {
    if (number == SYS_gettid) {
        // macOS không có gettid — pthread_threadid_np cho thread-id duy nhất,
        // ổn định (libc++abi chỉ cần so sánh bằng same-thread).
#ifdef __APPLE__
        uint64_t tid = 0;
        pthread_threadid_np(NULL, &tid);
        return static_cast<long>(tid);
#else
        return static_cast<long>(::syscall(SYS_gettid));
#endif
    }
    if (number == SYS_getpid) {
        return static_cast<long>(::getpid());
    }
    // Chưa map — log 1 lần mỗi số rồi ENOSYS (không chạy syscall macOS sai số).
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

// Linux MREMAP flags (asm-generic/mman.h)
#define MREMAP_MAYMOVE 1
#define MREMAP_FIXED 2

extern "C" void* bionic_mremap(void *old_address, size_t old_size, size_t new_size, int flags, void *new_address) {
#ifndef __APPLE__
    // Linux host: delegate thẳng tới syscall thật. mremap trên Linux mở rộng/
    // thu hẹp tại chỗ được khi vùng liền kề còn trống — không cần MAYMOVE
    // (trước đây luôn trả ENOMEM khi không có bit MAYMOVE).
    return ::mremap(old_address, old_size, new_size, flags, new_address);
#else
    (void)new_address;
    // Không có mremap trên Darwin. Mô phỏng sát ngữ nghĩa Linux:
    if (new_size == old_size) return old_address; // no-op
    if (flags & (MREMAP_MAYMOVE | MREMAP_FIXED)) {
        void* new_ptr = mmap(NULL, new_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (new_ptr != MAP_FAILED) {
            // Chỉ copy min(old,new) — copy old_size khi new_size nhỏ hơn sẽ tràn
            // mapping mới (corruption/crash).
            std::memcpy(new_ptr, old_address, std::min(old_size, new_size));
            munmap(old_address, old_size);
            return new_ptr;
        }
        errno = ENOMEM;
        return MAP_FAILED;
    }
    // Không MAYMOVE: Linux chỉ mở rộng tại chỗ nếu vùng liền kề trống; thu
    // hẹp luôn thành công tại chỗ. Thu hẹp: giữ mapping cũ (phần dư không dùng
    // nữa — harmless), không thể làm đúng hơn trên Darwin. Mở rộng không MAYMOVE
    // → ENOMEM như nhánh "không giãn được tại chỗ" của Linux.
    if (new_size < old_size) return old_address;
    errno = ENOMEM;
    return MAP_FAILED;
#endif
}

#define AT_HWCAP 16
#define AT_PAGESZ 6
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
    if (type == AT_PAGESZ) {
        // Trả kích thước trang thật của host — tránh engine đọc pagesize=0.
        long pagesize = ::sysconf(_SC_PAGESIZE);
        return pagesize > 0 ? static_cast<unsigned long>(pagesize) : 0;
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

// ── ashmem (Android shared memory) ──
// iOS không có ashmem. Trước đây fake bằng POSIX shm (shm_open) nhưng shm_open
// KHÔNG tồn tại trên iOS (POSIX shm là macOS-only; iOS trả -1/ENOSYS) →
// ashmem_create_region luôn fail. Fix: cấp phát anonymous mmap thật làm vùng
// nhớ chung + trả về fake fd; bionic_mmap trả lại chính vùng đó khi gặp fake fd.
// Trên macOS vẫn thử shm_open trước (fd thật, mmap thật) rồi mới fallback.
static std::mutex g_ashmem_mtx;
static std::unordered_map<int, int> g_ashmem_prot;      // fd -> prot cho phép
static std::unordered_map<int, void*> g_ashmem_region;  // fake fd -> vùng nhớ
static std::unordered_map<int, size_t> g_ashmem_size;   // fake fd -> kích thước
static std::atomic<int> g_ashmem_fake_fd{0x40000000};   // fake fd bắt đầu cao tránh fd thật

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
    // 1) Thử POSIX shm (hoạt động trên macOS).
    static std::atomic<uint32_t> counter{0};
    char shm_name[64];
    std::snprintf(shm_name, sizeof(shm_name), "/kudroid_ashmem_%d_%u", ::getpid(), counter.fetch_add(1));
    int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
        shm_unlink(shm_name);
        if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
            // fd vẫn hợp lệ; mmap sau sẽ lỗi nếu size vượt quá.
        }
        std::lock_guard<std::mutex> lock(g_ashmem_mtx);
        g_ashmem_prot[fd] = PROT_READ | PROT_WRITE;
        return fd;
    }

    // 2) Fallback (iOS): anonymous mmap làm region thật + fake fd.
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
    return 0; // tên chỉ để debug — không cần lưu
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
    if (it == g_ashmem_prot.end()) return true; // không phải ashmem fd
    return (prot & ~it->second) == 0;
}

// bionic_mmap gọi: với fake ashmem fd trả lại chính region đã cấp.
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
// shared_ptr để thread đang nằm trong cv.wait (hoặc giữa find và lock queue)
// không bao giờ thấy queue bị destroy khi entry bị erase.
static std::unordered_map<uint32_t*, std::shared_ptr<FutexWaitQueue>> g_futexQueues;
static std::mutex g_futexGlobalMtx;

// Giảm số waiter; nếu không còn ai (và entry vẫn là queue của chúng ta) thì
// xóa khỏi map — trước đây map không bao giờ được dọn, leak vô hạn mỗi uaddr
// mới. Phải gọi với g_futexGlobalMtx KHÔNG được giữ (thứ tự khóa: global
// trước queue-mtx, không bao giờ ngược lại).
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

        // Linux: nếu *uaddr != val khi vào wait → EAGAIN ngay.
        if (*uaddr != val) {
            qLock.unlock(); // thả queue-mtx trước khi lấy global (thứ tự khóa)
            futex_leave(uaddr, q);
            errno = EAGAIN;
            return -1;
        }

        if (timeout) {
            // Linux timeout là TUYỆT ĐỐI: CLOCK_MONOTONIC (FUTEX_WAIT) hoặc
            // CLOCK_REALTIME (có cờ FUTEX_CLOCK_REALTIME). Tính phần còn lại.
            const bool realtime = (futex_op & FUTEX_CLOCK_REALTIME) != 0;
            struct timespec now;
            ::clock_gettime(realtime ? CLOCK_REALTIME : CLOCK_MONOTONIC, &now);
            int64_t remSec = int64_t(timeout->tv_sec) - int64_t(now.tv_sec);
            int64_t remNs  = int64_t(timeout->tv_nsec) - int64_t(now.tv_nsec);
            if (remNs < 0) { remSec -= 1; remNs += 1000000000; }
            if (remSec < 0) {
                qLock.unlock();
                futex_leave(uaddr, q);
                errno = ETIMEDOUT;
                return -1;
            }
            if (remSec > 86400) remSec = 86400; // cap 24h tránh overflow duration
            const auto duration = std::chrono::seconds(remSec) +
                                  std::chrono::nanoseconds(remNs);
            if (q->cv.wait_for(qLock, duration) == std::cv_status::timeout) {
                qLock.unlock();
                futex_leave(uaddr, q);
                errno = ETIMEDOUT;
                return -1;
            }
        } else {
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

// --- Dynamic Loading (dlfcn) ---
// Serialize ANGLE/MoltenVK framework loads: cả hai chạy module initializer
// (khởi tạo Metal) bên trong ::dlopen. Guest chạy render thread (dlopen
// libEGL) SONG SONG với Vulkan JNI_OnLoad (dlopen libvulkan) → Metal init
// đồng thời → abort (SIGABRT không message — khớp crash log triangle).
// Mutex này đảm bảo mỗi framework được load + init xong trước khi cái kia
// bắt đầu.
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
        const char* fw_path = strstr(filename, "libEGL") ? 
            "@executable_path/Frameworks/libEGL.framework/libEGL" : 
            "@executable_path/Frameworks/libGLESv2.framework/libGLESv2";
        
        std::lock_guard<std::mutex> gpuLock(g_gpuFrameworkMtx);
        void* handle = ::dlopen(fw_path, RTLD_NOW | RTLD_LOCAL);
        if (handle) {
            logAndroidMessage(4, "KuDroidGPU", std::string("Successfully loaded ") + fw_path);
            return handle;
        } else {
            logAndroidMessage(5, "KuDroidGPU", std::string("Failed to load ") + fw_path + ": " + ::dlerror());
        }
    }
    
    if (strstr(filename, "libvulkan.so")) {
        std::lock_guard<std::mutex> gpuLock(g_gpuFrameworkMtx);
        void* handle = ::dlopen("@executable_path/Frameworks/MoltenVK.framework/MoltenVK", RTLD_NOW | RTLD_LOCAL);
        if (handle) {
            logAndroidMessage(4, "KuDroidGPU", "Successfully loaded MoltenVK.framework");
            return handle;
        } else {
            logAndroidMessage(5, "KuDroidGPU", std::string("Failed to load MoltenVK: ") + ::dlerror());
        }
    }

#define DUMMY_HANDLE ((void*)0x4B5544524F494421ULL) // "KUDROID!" as a handle

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

    // For a real host handle, prefer its own symbols first.
    if (handle && handle != RTLD_DEFAULT && handle != DUMMY_HANDLE) {
        if (void* real = ::dlsym(handle, symbol)) {
            logAndroidMessage(2, "KuDroidSyscall", std::string("bionic_dlsym: [") + symbol + "] found in real handle");
            return real;
        }
    }

    // Route through the shim symbol tables (syscall/graphics/input).
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
    if (handle == DUMMY_HANDLE) {
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
    // DUMMY_HANDLE là handle giả cho các lib Android không tồn tại trên host
    // (xem bionic_dlopen) — gọi ::dlclose với nó sẽ dereference con trỏ rác.
    if (!handle || handle == RTLD_DEFAULT || handle == DUMMY_HANDLE) return 0;
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

// FDs giả (inotify/signalfd emulate bằng loopback UDP) — bionic_close phải đóng
// đúng, không để rò fd.
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
        // Emulated fd (loopback UDP) — đăng ký để bionic_close bookkeeping đầy đủ.
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
        // Emulated fd (loopback UDP + GCD timer) — đăng ký để bionic_close
        // bookkeeping đầy đủ (giống inotify/signalfd).
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
    // Dọn entry prot + region của ashmem fd — không dọn thì fd number tái sử
    // dụng bị prot cũ khóa (leak + hành vi sai). Fake fd (iOS fallback) còn phải
    // munmap vùng nhớ.
    {
        std::lock_guard<std::mutex> lock(g_ashmem_mtx);
        const bool is_fake = g_ashmem_region.find(fd) != g_ashmem_region.end();
        if (is_fake) {
            ashmem_forget(fd);
            return 0; // fake fd không phải fd thật — đừng close fd number thật
        }
        g_ashmem_prot.erase(fd);
    }
    {
        // inotify/signalfd fd giả — đóng socket thật qua ::close.
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
    
    int n = kevent(epfd, NULL, 0, evlist, maxevents * 2, ts_ptr);
    int unique_events = 0;
    
    if (n > 0) {
        // Gộp theo FD (kevent.ident), không theo udata: hai fd khác nhau có thể
        // dùng chung data (vd ident 0) — gộp theo udata sẽ làm mất một event.
        // Còn một fd đọc+ghi được gộp thành một event EPOLLIN|EPOLLOUT như epoll.
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
                order.push_back(fd); // giữ thứ tự xuất hiện đầu tiên
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
    return ::epoll_wait(epfd, reinterpret_cast<struct epoll_event*>(events_ptr), maxevents, timeout);
#endif
}

extern "C" int bionic_pthread_condattr_init(void* attr) { (void)attr; return 0; }
extern "C" int bionic_pthread_condattr_destroy(void* attr) { (void)attr; return 0; }
extern "C" int bionic_pthread_mutexattr_init(void* attr) { (void)attr; return 0; }
extern "C" int bionic_pthread_mutexattr_destroy(void* attr) { (void)attr; return 0; }
extern "C" int bionic_pthread_mutexattr_settype(void* attr, int type) {
    // GHI kiểu vào attr guest (2 bit thấp từ đầu) — dummy cũ không ghi gì nên
    // pthread_mutex_init không bao giờ biết mutex là recursive.
    auto* p = static_cast<uint32_t*>(attr);
    if (!p) return -1;
    *p = (*p & ~0x3u) | (static_cast<uint32_t>(type) & 0x3u);
    return 0;
}


extern "C" int bionic_pthread_mutex_trylock(void* guestMutex) {
    pthread_mutex_t* hostMutex = static_cast<pthread_mutex_t*>(get_or_init_sync(guestMutex, SYNC_MUTEX));
    return hostMutex ? ::pthread_mutex_trylock(hostMutex) : -1;
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

// Bionic pthread_once_t: int 32-bit với 3 trạng thái — 0 = chưa chạy,
// 1 = đang chạy init_routine, 2 = đã xong. Control word nằm trong bộ nhớ
// guest nên ta dùng atomic trên chính guest_once (fast path không cần mutex).
//
// Trước đây dùng MỘT global mutex giữ trong suốt init_routine() → init gọi
// pthread_once trên control word khác sẽ tự khóa chính mình → deadlock. Giờ
// mỗi control word có mutex/cv riêng (map dùng shared_ptr để thread đang đợi
// không bị destroy khi entry bị xóa), đúng như pthread_once thật của bionic
// cho phép nested once trên control word khác nhau.
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

    // Fast path: đã chạy xong rồi.
    if (once_state_load(guest_once) == 2) return 0;

    std::shared_ptr<BionicOnceControl> ctl;
    {
        std::lock_guard<std::mutex> lock(g_once_map_mtx);
        auto& slot = g_once_controls[guest_once];
        if (!slot) slot = std::make_shared<BionicOnceControl>();
        ctl = slot;
    }

    std::unique_lock<std::mutex> lock(ctl->mtx);
    // Một thread khác đang chạy init_routine cho control word này → chờ.
    while (once_state_load(guest_once) == 1) {
        ctl->cv.wait(lock);
    }
    // Thread khác vừa chạy xong trong lúc ta chờ.
    if (once_state_load(guest_once) == 2) return 0;

    // Ta là thread đầu tiên: đánh dấu IN_PROGRESS rồi chạy init. Mutex được
    // giữ suốt init_routine, nhưng chỉ với control word NÀY — init gọi
    // pthread_once trên control word khác sẽ dùng mutex khác, không deadlock.
    once_state_store(guest_once, 1);
    init_routine();
    once_state_store(guest_once, 2);
    ctl->cv.notify_all();

    // Best-effort dọn map: entry không còn ai tham chiếu (thread đang đợi giữ
    // shared_ptr riêng nên vẫn sống). Fast path thường chặn các lần gọi sau.
    std::lock_guard<std::mutex> mapLock(g_once_map_mtx);
    auto it = g_once_controls.find(guest_once);
    if (it != g_once_controls.end() && it->second == ctl) {
        g_once_controls.erase(it);
    }
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

// Bionic/Linux sa_flags (asm-generic/signal.h) — guest truyền giá trị Linux.
constexpr int LINUX_SA_NOCLDSTOP = 0x00000001;
constexpr int LINUX_SA_NOCLDWAIT = 0x00000002;
constexpr int LINUX_SA_SIGINFO   = 0x00000004;
constexpr int LINUX_SA_ONSTACK   = 0x08000000;
constexpr int LINUX_SA_RESTART   = 0x10000000;
constexpr int LINUX_SA_NODEFER   = 0x40000000;
constexpr int LINUX_SA_RESETHAND = 0x80000000;

// Dịch sa_flags guest (Linux) → host. Trước đây chỉ bit SA_SIGINFO (0x4) được
// dịch sang Darwin, còn SA_RESTART/NODEFER/RESETHAND/ONSTACK/NOCLD* bị drop im
// lặng → game cần SA_RESTART (đa số game đặt nó) không được restart syscall.
static int sa_flags_guest_to_host(int flags) {
#ifdef __APPLE__
    int out = 0;
    if (flags & LINUX_SA_SIGINFO)   out |= SA_SIGINFO;
    if (flags & LINUX_SA_ONSTACK)   out |= SA_ONSTACK;
    if (flags & LINUX_SA_RESTART)   out |= SA_RESTART;
    if (flags & LINUX_SA_NODEFER)   out |= SA_NODEFER;
    if (flags & LINUX_SA_RESETHAND) out |= SA_RESETHAND;
    if (flags & LINUX_SA_NOCLDSTOP) out |= SA_NOCLDSTOP;
    if (flags & LINUX_SA_NOCLDWAIT) out |= SA_NOCLDWAIT;
    return out;
#else
    (void)flags;
    // Linux host: giá trị giống hệt, truyền thẳng.
    return flags;
#endif
}

static int sa_flags_host_to_guest(int flags) {
#ifdef __APPLE__
    int out = 0;
    if (flags & SA_SIGINFO)   out |= LINUX_SA_SIGINFO;
    if (flags & SA_ONSTACK)   out |= LINUX_SA_ONSTACK;
    if (flags & SA_RESTART)   out |= LINUX_SA_RESTART;
    if (flags & SA_NODEFER)   out |= LINUX_SA_NODEFER;
    if (flags & SA_RESETHAND) out |= LINUX_SA_RESETHAND;
    if (flags & SA_NOCLDSTOP) out |= LINUX_SA_NOCLDSTOP;
    if (flags & SA_NOCLDWAIT) out |= LINUX_SA_NOCLDWAIT;
    return out;
#else
    (void)flags;
    return flags;
#endif
}

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
        const int host_flags = sa_flags_guest_to_host(act->sa_flags);
        if (host_flags & SA_SIGINFO) {
            host_act.sa_sigaction = reinterpret_cast<void (*)(int, siginfo_t*, void*)>(act->android_sa_sigaction);
        } else {
            host_act.sa_handler = act->android_sa_handler;
        }
        host_act.sa_flags = host_flags;
        // Copy the signal mask (Android sa_mask is a 64-bit bitmask).
        sigemptyset(&host_act.sa_mask);
        for (int sig = 1; sig < 64; ++sig) {
            if (act->sa_mask & (1ULL << (sig - 1))) {
                sigaddset(&host_act.sa_mask, sig);
            }
        }
    }
    
    int ret = ::sigaction(signum, act ? &host_act : nullptr, oldact ? &host_oldact : nullptr);
    
    if (oldact && ret == 0) {
        std::memset(oldact, 0, sizeof(struct android_sigaction));
        oldact->sa_flags = sa_flags_host_to_guest(host_oldact.sa_flags);
        if (host_oldact.sa_flags & SA_SIGINFO) {
            oldact->android_sa_sigaction = reinterpret_cast<void (*)(int, void*, void*)>(host_oldact.sa_sigaction);
        } else {
            oldact->android_sa_handler = host_oldact.sa_handler;
        }
        // Copy the mask back.
        oldact->sa_mask = 0;
        for (int sig = 1; sig < 64; ++sig) {
            if (sigismember(&host_oldact.sa_mask, sig) == 1) {
                oldact->sa_mask |= (1ULL << (sig - 1));
            }
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

// Kích thước khối TLS guest và các offset chuẩn bionic (arm64).
// Thread pointer (tpidr_el0) trỏ vào vùng slot; slot N ở tpidr + N*8.
constexpr size_t kTlsBlockSize    = 65536;
constexpr size_t kTlsSlotOffset   = 32768; // TP = tls_base + kTlsSlotOffset
constexpr size_t kTlsModuleOffset = 4096;  // template TLS module, tương đối với TP
constexpr size_t kTlsStackGuardSlotOffset = 40; // slot 5

// Template TLS của module guest (PT_TLS), do elf_loader đăng ký sau khi map.
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

// Cấp phát một khối TLS guest đầy đủ: zero hóa, copy template TLS module vào
// vị trí tprel, đặt stack-guard cookie. Dùng chung cho main thread, thread mới
// và lazy-allocation trong bionic_handle_tpidr_trap.
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

    // Stack guard cookie tại slot 5 (offset 40 tính từ TP).
    *reinterpret_cast<uint64_t*>(static_cast<char*>(tls_base) + kTlsSlotOffset + kTlsStackGuardSlotOffset) =
        0x1337BEEFCAFECAFE;
    return tls_base;
}

struct BionicThreadArgs {
    void* (*start_routine)(void*);
    void* arg;
};

static void* bionic_thread_wrapper(void* rawArgs);

extern "C" void bionic_init_main_thread_tls(void) {
    ::pthread_once(&tls_key_once, init_tls_key);
    if (::pthread_getspecific(tls_key)) return; // đã có khối TLS
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
    
    fprintf(stderr, "[TLS_DIAG] tls_base=%p tls_ptr=%p\n", reinterpret_cast<void*>(tls_base), reinterpret_cast<void*>(tls_ptr));
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
                // Lazy allocation: thread do host tạo (JVM/Swift) chạy guest code
                // chưa có khối TLS. Cấp phát ngay để guest không đọc địa chỉ 0.
                // Đây là trap đồng bộ (BRK do guest thực thi) nên malloc ở đây
                // an toàn trong thực tế; khối được tls_destructor giải phóng khi
                // thread kết thúc.
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

    ::pthread_once(&tls_key_once, init_tls_key);

    // Allocate 64KB for Android TLS block and set tpidr_el0
    // Darwin uses tpidrro_el0, so tpidr_el0 is free for us!
    // KHÔNG fallback sang aligned_alloc trần: khối như vậy không có template
    // TLS module và không có stack-guard cookie → guest đọc rác. alloc_guest_tls_block
    // dùng chính allocator/kích thước đó; nếu nó OOM thì fallback cũng OOM.
    void* tls_base = alloc_guest_tls_block();
    if (tls_base) {
        ::pthread_setspecific(tls_key, tls_base);
#if defined(__aarch64__)
        __asm__ volatile("msr tpidr_el0, %0" : : "r"((char*)tls_base + kTlsSlotOffset));
#endif
    }

    // iOS: ANGLE/Metal/ObjC require an autorelease pool on EVERY thread that
    // touches them. Guest render threads (TriangleGLES render thread, Unity's
    // render thread...) are raw pthreads with NO pool — GPU test passes because
    // it runs on the main/GCD thread (pool có sẵn). Không có pool → abort()
    // ngay trong ANGLE eglInitialize (Metal device/queue creation).
#if defined(__APPLE__)
    void* pool = objc_autoreleasePoolPush();
#endif
    void* result = start_routine(arg);
#if defined(__APPLE__)
    objc_autoreleasePoolPop(pool);
#endif

    // No need to free(tls_base) here, the destructor will handle it automatically
    // when the thread terminates, even if it terminates via pthread_exit().
    return result;
}

extern "C" int bionic_pthread_create(pthread_t* thread, void* attr, void* (*start_routine)(void*), void* arg) {
    BionicThreadArgs* args = new BionicThreadArgs{start_routine, arg};
    if (!attr) {
        const int res = ::pthread_create(thread, nullptr, bionic_thread_wrapper, args);
        if (res != 0) delete args;
        return res;
    }

    // Truyền stack size + detach state từ attr guest (nếu có) sang pthread_create host.
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
    return res;
}

// errno accessor: Darwin exports `__error`, glibc/musl export `__errno_location`.
// Guarded so SyscallShim.o actually links on a Linux host (previously any host
// consumer that pulled this object failed with an undefined `__error`).
#ifdef __APPLE__
extern "C" int* __error(void);
#else
extern "C" int* __errno_location(void);
#endif

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
// __system_property_* — Android property system
// ============================================================================
// Bionic ABI: prop_info là con trỏ không trong suốt — game nhận từ
// __system_property_find rồi truyền lại cho __system_property_read/
// _read_callback. Ta lưu bảng property tĩnh và dùng con trỏ tới entry như
// prop_info*. Trước đây find trả 0 (not found) và read_callback là no-op —
// Unity đọc trực tiếp __system_property_read sẽ thấy rác.
namespace {
struct KudroidProp {
    const char* name;
    const char* value;
};
const KudroidProp kKnownProps[] = {
    {"ro.build.version.sdk", "29"},
    {"ro.build.version.release", "10"},
    {"ro.product.cpu.abi", "arm64-v8a"},
    {"ro.debuggable", "0"},
    {"persist.sys.timezone", "UTC"},
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
    return findProp(name); // prop_info* — nullptr nếu không có
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
extern "C" int bionic_sched_getaffinity(pid_t pid, size_t cpusetsize, void* mask) {
    (void)pid; (void)cpusetsize;
    // Return a mask with all CPUs set (0xFF for 8 CPUs).
    if (mask && cpusetsize >= 1) {
        static_cast<uint8_t*>(mask)[0] = 0xFF;
    }
    return 0;
}

// sched_setaffinity — set the CPU affinity mask (no-op).
extern "C" int bionic_sched_setaffinity(pid_t pid, size_t cpusetsize, const void* mask) {
    (void)pid; (void)cpusetsize; (void)mask;
    return 0;
}

// ── inotify / signalfd — không tồn tại trên iOS → emulate ────────────────
// Trước đây trả -1 → game (Unity hay dùng inotify để theo dõi asset) fail ngay.
// Giờ trả fd thật (loopback UDP — poll được, không bao giờ có event = "không có
// file thay đổi", đúng ngữ nghĩa khi không có ai ghi file). inotify_add_watch
// trả watch descriptor giả > 0 để game tin rằng watch đã được đăng ký.

// inotify_init1 — create an inotify instance (emulated: fd poll-able, never fires).
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

// inotify_add_watch — register a watch (emulated: trả wd giả dương).
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
// không bao giờ có signal — game poll không treo, chỉ không nhận signal hiếm).
extern "C" int bionic_signalfd(int fd, const void* mask, int flags) {
    (void)mask; (void)flags;
    if (fd >= 0) return fd; // signalfd(fd,...) với fd có sẵn — tái dùng
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
// Trước đây trả 0 KHÔNG fill old_limit → game đọc struct rlimit rác (giống bug
// statx cũ). Fill từ getrlimit thật, map chỉ số RLIMIT_* Linux → Darwin.
struct GuestRlimit64 { uint64_t rlim_cur; uint64_t rlim_max; }; // Linux rlimit64
extern "C" int bionic_prlimit64(pid_t pid, int resource, const void* new_limit, void* old_limit) {
    (void)pid;
#ifdef __APPLE__
    // Linux RLIMIT_* → Darwin RLIMIT_* (chỉ số khác nhau!)
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
        default: darwin_resource = -1; break;             // RSS/LOCKS/... không có
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
        return 0; // không map được — vẫn trả 0 để game không chết
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

// statx — extended stat. Trước đây trả thành công mà KHÔNG fill statxbuf → game
// đọc struct statx chưa init (rác). Fill cấu trúc statx chuẩn Linux UAPI từ stat().
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
        // Đường dẫn tương đối theo dirfd — xử lý best-effort qua /proc/self/fd.
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
    if (rc != 0) return -1; // errno đã set
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
    bool isInputPipe; // fd là wake pipe của AInputQueue (cần drain khi readable)
};

struct ALooper {
    std::vector<ALooperFd> fds;
    std::mutex mtx;
    int refCount;
};

static ALooper* g_mainLooper = nullptr;
static std::mutex g_looperMtx;

extern "C" void* bionic_ALooper_prepare(int opts) {
    (void)opts;
    std::lock_guard<std::mutex> lock(g_looperMtx);
    if (!g_mainLooper) {
        g_mainLooper = new ALooper();
        g_mainLooper->refCount = 1;
    } else {
        g_mainLooper->refCount++;
    }
    return g_mainLooper;
}

extern "C" void* bionic_ALooper_forThread() {
    std::lock_guard<std::mutex> lock(g_looperMtx);
    return g_mainLooper;
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
    if (--l->refCount <= 0) {
        delete l;
        if (g_mainLooper == l) g_mainLooper = nullptr;
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

// Đánh dấu một fd là wake pipe của AInputQueue — bionic_AInputQueue_attachLooper
// gọi hàm này sau khi đăng ký pipe với looper, để pollAll biết cần drain nó
// (nước không bao giờ được đọc nếu không, looper sẽ trả ready mãi mãi -> busy loop).
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

    // nfds_t là unsigned int trên Darwin — clamp tránh truncate khi pfds quá lớn.
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

        // Drain wake pipe của AInputQueue: nếu không đọc, pipe luôn readable
        // và pollAll trả ngay lập tức mãi mãi (busy loop).
        // Pipe này do InputShim tạo với O_NONBLOCK (xem ensure_wake_pipe) nên
        // read() trả EAGAIN khi cạn — drain cho tới khi EAGAIN. Cap rất cao chỉ
        // để phòng thủ fd không non-blocking (không thể xảy ra với pipe của ta).
        if (snapshot[i].isInputPipe && (pfds[i].revents & (POLLIN | POLLHUP))) {
            char drainBuf[4096];
            for (int drainIters = 0; drainIters < (1 << 20); ++drainIters) {
                const ssize_t n = ::read(snapshot[i].fd, drainBuf, sizeof(drainBuf));
                if (n <= 0) break; // EAGAIN hoặc đã cạn
            }
        }

        // Thực thi callback nếu có (giống ALooper thật). callback trả 0 = gỡ fd.
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
// dlsym(RTLD_DEFAULT). Trước đây chúng bị bind vào kudroid_universal_dummy
// (trả 0) — sincos trả rác, sem_timedwait trả 0 tức thì (mất đồng bộ thread),
// __strlen_chk trả 0 (sai độ dài buffer). Giờ là implementation thật.
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
// Fortify: s_len là kích thước object; bionic abort nếu strlen >= s_len. Ở đây
// trả strlen thật (đúng kết quả game cần) và chỉ cảnh báo — không abort để game
// không chết vì giới hạn kích thước tính từ layout bionic khác host.
extern "C" size_t bionic___strlen_chk(const char* s, size_t s_len) {
    const size_t len = ::strlen(s ? s : "");
    if (len >= s_len) {
        trace("__strlen_chk: string exceeds declared buffer size (fortify)");
    }
    return len;
}

// bionic: void __FD_SET_chk(int fd, fd_set* set, size_t set_size)
// set_size là kích thước byte của fd_set; fd hợp lệ nếu fd < set_size*8.
extern "C" void bionic___FD_SET_chk(int fd, fd_set* set, size_t set_size) {
    if (!set) return;
    if (fd < 0 || static_cast<size_t>(fd) >= set_size * 8) {
        trace("__FD_SET_chk: fd out of range for fd_set (fortify)");
        return;
    }
    FD_SET(fd, set);
}

// bionic: int __FD_ISSET_chk(int fd, const fd_set* set, size_t set_size)
// Trả về nonzero nếu fd có mặt trong set; dummy trước đây trả 0 khiến game
// tưởng fd không sẵn sàng → poll loop treo/đọc sai.
extern "C" int bionic___FD_ISSET_chk(int fd, const fd_set* set, size_t set_size) {
    if (!set) return 0;
    if (fd < 0 || static_cast<size_t>(fd) >= set_size * 8) {
        trace("__FD_ISSET_chk: fd out of range for fd_set (fortify)");
        return 0;
    }
    return FD_ISSET(fd, set) ? 1 : 0;
}

// bionic: int sem_timedwait(sem_t* sem, const struct timespec* abs_timeout)
// abs_timeout là thời điểm tuyệt đối (CLOCK_REALTIME). Host iOS có sem_timedwait
// nhưng không export qua dlsym(RTLD_DEFAULT), nên giả lập bằng sem_trywait + sleep.
extern "C" int bionic_sem_timedwait(sem_t* sem, const struct timespec* abs_timeout) {
    if (!sem || !abs_timeout) {
        errno = EINVAL;
        return -1;
    }
    for (;;) {
        if (::sem_trywait(sem) == 0) return 0;
        const int err = errno;
        if (err != EAGAIN) return -1; // EINTR / EINVAL

        struct timespec now;
        ::clock_gettime(CLOCK_REALTIME, &now);
        if (now.tv_sec > abs_timeout->tv_sec ||
            (now.tv_sec == abs_timeout->tv_sec && now.tv_nsec >= abs_timeout->tv_nsec)) {
            errno = ETIMEDOUT;
            return -1;
        }

        struct timespec slp = {0, 1000000}; // 1ms
        ::nanosleep(&slp, nullptr);
    }
}

// Bản ngoài "C" cho kudroid_jni.cpp: android.util.Log.println_native forward về
// pipeline log chuẩn của kudroid (stdout + file + crash buffer).
extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message) {
    return logAndroidMessage(priority, tag, std::string(message ? message : ""));
}

// ─────────────────────────────────────────────────────────────────────────────
// Họ fortify (__*_chk) — game build với _FORTIFY_SOURCE (NDK mặc định cho
// release) import rất nhiều hàm này. Trước đây rơi vào dummy (trả 0, không làm
// gì) — __memcpy_chk không copy, __read_chk không đọc... → dữ liệu hỏng âm thầm.
// Giờ là bản thật; khi vi phạm kích thước thì log cảnh báo thay vì abort.
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

extern "C" int bionic___vsnprintf_chk(char* s, size_t maxlen, int flag, size_t slen,
                                      const char* format, va_list ap) {
    (void)flag; (void)slen;
    return ::vsnprintf(s, maxlen, format, ap);
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
        n = dst_len; // clamp để không ghi tràn
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
// Bionic lưu msg để tombstone. Ở đây lưu lại để crash handler in ra — thường chứa
// lý do Unity abort (ví dụ "FATAL: ..."), rất giá trị khi chẩn đoán.
extern "C" void bionic_android_set_abort_message(const char* msg) {
    if (msg && *msg) {
        kudroid_store_abort_message(msg);
        char traceMessage[256];
        snprintf(traceMessage, sizeof(traceMessage),
                 "android_set_abort_message: %.200s", msg);
        trace(traceMessage);
    }
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
    {"syscall", reinterpret_cast<void*>(&bionic_syscall)},
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
    
#ifdef __APPLE__
    {"__errno", reinterpret_cast<void*>(&__error)},
#else
    {"__errno", reinterpret_cast<void*>(&__errno_location)},
#endif
    {"snprintf", reinterpret_cast<void*>(&snprintf)},
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
    {"ioctl", reinterpret_cast<void*>(&bionic_ioctl)},
    {"prctl", reinterpret_cast<void*>(&bionic_prctl)},
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
    {"closedir", reinterpret_cast<void*>(&vfs_closedir)},
    {"readlink", reinterpret_cast<void*>(&vfs_readlink)},
    {"realpath", reinterpret_cast<void*>(&vfs_realpath)},
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
    {"__cxa_atexit", reinterpret_cast<void*>(&bionic_cxa_atexit)},
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

    // Logging
    {"__android_log_vprint", reinterpret_cast<void*>(&bionic_android_log_vprint)},
    {"__android_log_write", reinterpret_cast<void*>(&bionic_android_log_write)},
    {"__android_log_buf_write", reinterpret_cast<void*>(&bionic_android_log_buf_write)},

    // Character classification
    {"__ctype_get_mb_cur_max", reinterpret_cast<void*>(&bionic_ctype_get_mb_cur_max)},
    {"_ctype_", reinterpret_cast<void*>(&g_ctype_ptr)},

    // Bionic libm / fortify / sync symbols the host cannot dlsym
    {"sincos", reinterpret_cast<void*>(&bionic_sincos)},
    {"sincosf", reinterpret_cast<void*>(&bionic_sincosf)},
    {"__strlen_chk", reinterpret_cast<void*>(&bionic___strlen_chk)},
    {"__FD_SET_chk", reinterpret_cast<void*>(&bionic___FD_SET_chk)},
    {"__FD_ISSET_chk", reinterpret_cast<void*>(&bionic___FD_ISSET_chk)},
    {"sem_timedwait", reinterpret_cast<void*>(&bionic_sem_timedwait)},
    {"android_set_abort_message", reinterpret_cast<void*>(&bionic_android_set_abort_message)},
    {"__memcpy_chk", reinterpret_cast<void*>(&bionic___memcpy_chk)},
    {"__memmove_chk", reinterpret_cast<void*>(&bionic___memmove_chk)},
    {"__memset_chk", reinterpret_cast<void*>(&bionic___memset_chk)},
    {"__read_chk", reinterpret_cast<void*>(&bionic___read_chk)},
    {"__write_chk", reinterpret_cast<void*>(&bionic___write_chk)},
    {"__snprintf_chk", reinterpret_cast<void*>(&bionic___snprintf_chk)},
    {"__vsnprintf_chk", reinterpret_cast<void*>(&bionic___vsnprintf_chk)},
    {"__sprintf_chk", reinterpret_cast<void*>(&bionic___sprintf_chk)},
    {"__strncpy_chk", reinterpret_cast<void*>(&bionic___strncpy_chk)},
    {"__strcpy_chk", reinterpret_cast<void*>(&bionic___strcpy_chk)},
    {"__strcat_chk", reinterpret_cast<void*>(&bionic___strcat_chk)},
    {"__fdelt_chk", reinterpret_cast<void*>(&bionic___fdelt_chk)},

    // pthread extensions
    {"pthread_condattr_setclock", reinterpret_cast<void*>(&bionic_pthread_condattr_setclock)},

    // Google internal
    {"__google_potentially_blocking_region_begin", reinterpret_cast<void*>(&bionic_google_potentially_blocking_region_begin)},
    {"__google_potentially_blocking_region_end", reinterpret_cast<void*>(&bionic_google_potentially_blocking_region_end)},

    // Additional Linux syscalls
    {"getcpu", reinterpret_cast<void*>(&bionic_getcpu)},
    {"sched_getaffinity", reinterpret_cast<void*>(&bionic_sched_getaffinity)},
    {"sched_setaffinity", reinterpret_cast<void*>(&bionic_sched_setaffinity)},
    {"inotify_init1", reinterpret_cast<void*>(&bionic_inotify_init1)},
    {"inotify_add_watch", reinterpret_cast<void*>(&bionic_inotify_add_watch)},
    {"inotify_rm_watch", reinterpret_cast<void*>(&bionic_inotify_rm_watch)},
    {"signalfd", reinterpret_cast<void*>(&bionic_signalfd)},
    {"eventfd2", reinterpret_cast<void*>(&bionic_eventfd2)},
    {"prlimit64", reinterpret_cast<void*>(&bionic_prlimit64)},
    {"statx", reinterpret_cast<void*>(&bionic_statx)},
};

} // namespace

const SymbolEntry* get_syscall_symbols(size_t* count) {
    if (count) {
        *count = sizeof(kSyscallSymbols) / sizeof(SymbolEntry);
    }
    return kSyscallSymbols;
}

} // namespace kudroid
