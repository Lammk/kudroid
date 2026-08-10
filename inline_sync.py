import re

with open('src/BionicShim.cpp', 'r') as f:
    content = f.read()

# Add <atomic> to the includes
content = re.sub(r'#include <unordered_map>', '#include <unordered_map>\n#include <atomic>', content)

# 1. Remove gMutexRegistryLock and gGuestMutexes
content = re.sub(r'pthread_mutex_t gMutexRegistryLock = PTHREAD_MUTEX_INITIALIZER;\n', '', content)
content = re.sub(r'std::unordered_map<void\*, pthread_mutex_t\*> gGuestMutexes;\n', '', content)
content = re.sub(r'static std::unordered_map<void\*, pthread_cond_t\*> gGuestConds;\n', '', content)
content = re.sub(r'static std::unordered_map<void\*, pthread_rwlock_t\*> gGuestRwlocks;\n', '', content)

# 2. Insert the InlineGuestSync definition right before `extern "C" int bionic_pthread_mutex_init`
inline_def = """
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
"""

content = re.sub(
    r'(extern "C" int bionic_pthread_mutex_init\(void\* guestMutex)',
    inline_def + r'\n\1',
    content
)

# 3. Replace all the pthread synchronization implementations
replacements = {
    r'extern "C" int bionic_pthread_mutex_init\(void\* guestMutex,.*?return 0;\n\}': """extern "C" int bionic_pthread_mutex_init(void* guestMutex, const void* attr) {
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
}""",
    r'extern "C" int bionic_pthread_cond_init\(void\* cond, const void\* attr\).*?return 0;\n\}': """extern "C" int bionic_pthread_cond_init(void* cond, const void* attr) {
    (void)attr;
    auto* hostCond = static_cast<pthread_cond_t*>(std::malloc(sizeof(pthread_cond_t)));
    if (!hostCond) return -1;
    const int result = ::pthread_cond_init(hostCond, nullptr);
    if (result != 0) { std::free(hostCond); return result; }
    auto* sync = reinterpret_cast<InlineGuestSync*>(cond);
    sync->host_ptr = hostCond;
    sync->magic.store(KUDROID_SYNC_MAGIC, std::memory_order_release);
    return 0;
}""",
    r'extern "C" int bionic_pthread_rwlock_init\(void\* rwlock, const void\* attr\).*?return 0;\n\}': """extern "C" int bionic_pthread_rwlock_init(void* rwlock, const void* attr) {
    (void)attr;
    auto* hostRwlock = static_cast<pthread_rwlock_t*>(std::malloc(sizeof(pthread_rwlock_t)));
    if (!hostRwlock) return -1;
    const int result = ::pthread_rwlock_init(hostRwlock, nullptr);
    if (result != 0) { std::free(hostRwlock); return result; }
    auto* sync = reinterpret_cast<InlineGuestSync*>(rwlock);
    sync->host_ptr = hostRwlock;
    sync->magic.store(KUDROID_SYNC_MAGIC, std::memory_order_release);
    return 0;
}""",
    r'pthread_mutex_t\* findGuestMutex\(void\* guestMutex\).*?return hostMutex;\n\}': "",
    r'extern "C" int bionic_pthread_mutex_lock\(void\* guestMutex\).*?return result;\n\}': """extern "C" int bionic_pthread_mutex_lock(void* guestMutex) {
    trace("pthread_mutex_lock()");
    pthread_mutex_t* hostMutex = static_cast<pthread_mutex_t*>(get_or_init_sync(guestMutex, 1));
    const int result = hostMutex ? ::pthread_mutex_lock(hostMutex) : -1;
    trace(result == 0 ? "pthread_mutex_lock() -> 0" : "pthread_mutex_lock() -> error");
    return result;
}""",
    r'extern "C" int bionic_pthread_mutex_unlock\(void\* guestMutex\).*?return result;\n\}': """extern "C" int bionic_pthread_mutex_unlock(void* guestMutex) {
    trace("pthread_mutex_unlock()");
    pthread_mutex_t* hostMutex = static_cast<pthread_mutex_t*>(get_or_init_sync(guestMutex, 1));
    const int result = hostMutex ? ::pthread_mutex_unlock(hostMutex) : -1;
    trace(result == 0 ? "pthread_mutex_unlock() -> 0" : "pthread_mutex_unlock() -> error");
    return result;
}""",
    r'extern "C" int bionic_pthread_mutex_destroy\(void\* guestMutex\).*?return result;\n\}': """extern "C" int bionic_pthread_mutex_destroy(void* guestMutex) {
    trace("pthread_mutex_destroy()");
    destroy_sync(guestMutex, 1);
    trace("pthread_mutex_destroy() -> 0");
    return 0;
}""",
    r'extern "C" int bionic_pthread_cond_wait\(void\* cond, void\* mutex\).*?return ::pthread_cond_wait\(hostCond, hostMutex\);\n\}': """extern "C" int bionic_pthread_cond_wait(void* cond, void* mutex) {
    pthread_cond_t* hostCond = static_cast<pthread_cond_t*>(get_or_init_sync(cond, 2));
    pthread_mutex_t* hostMutex = static_cast<pthread_mutex_t*>(get_or_init_sync(mutex, 1));
    if (!hostCond || !hostMutex) return -1;
    return ::pthread_cond_wait(hostCond, hostMutex);
}""",
    r'extern "C" int bionic_pthread_cond_timedwait\(void\* cond, void\* mutex, const struct timespec\* abstime\).*?return ::pthread_cond_timedwait\(hostCond, hostMutex, abstime\);\n\}': """extern "C" int bionic_pthread_cond_timedwait(void* cond, void* mutex, const struct timespec* abstime) {
    pthread_cond_t* hostCond = static_cast<pthread_cond_t*>(get_or_init_sync(cond, 2));
    pthread_mutex_t* hostMutex = static_cast<pthread_mutex_t*>(get_or_init_sync(mutex, 1));
    if (!hostCond || !hostMutex) return -1;
    return ::pthread_cond_timedwait(hostCond, hostMutex, abstime);
}""",
    r'extern "C" int bionic_pthread_cond_signal\(void\* cond\).*?return hostCond \? ::pthread_cond_signal\(hostCond\) : -1;\n\}': """extern "C" int bionic_pthread_cond_signal(void* cond) {
    pthread_cond_t* hostCond = static_cast<pthread_cond_t*>(get_or_init_sync(cond, 2));
    return hostCond ? ::pthread_cond_signal(hostCond) : -1;
}""",
    r'extern "C" int bionic_pthread_cond_broadcast\(void\* cond\).*?return hostCond \? ::pthread_cond_broadcast\(hostCond\) : -1;\n\}': """extern "C" int bionic_pthread_cond_broadcast(void* cond) {
    pthread_cond_t* hostCond = static_cast<pthread_cond_t*>(get_or_init_sync(cond, 2));
    return hostCond ? ::pthread_cond_broadcast(hostCond) : -1;
}""",
    r'extern "C" int bionic_pthread_cond_destroy\(void\* cond\).*?return (res|result);\n\}': """extern "C" int bionic_pthread_cond_destroy(void* cond) {
    destroy_sync(cond, 2);
    return 0;
}""",
    r'extern "C" int bionic_pthread_rwlock_rdlock\(void\* rwlock\).*?return hostRwlock \? ::pthread_rwlock_rdlock\(hostRwlock\) : -1;\n\}': """extern "C" int bionic_pthread_rwlock_rdlock(void* rwlock) {
    pthread_rwlock_t* hostRwlock = static_cast<pthread_rwlock_t*>(get_or_init_sync(rwlock, 3));
    return hostRwlock ? ::pthread_rwlock_rdlock(hostRwlock) : -1;
}""",
    r'extern "C" int bionic_pthread_rwlock_wrlock\(void\* rwlock\).*?return hostRwlock \? ::pthread_rwlock_wrlock\(hostRwlock\) : -1;\n\}': """extern "C" int bionic_pthread_rwlock_wrlock(void* rwlock) {
    pthread_rwlock_t* hostRwlock = static_cast<pthread_rwlock_t*>(get_or_init_sync(rwlock, 3));
    return hostRwlock ? ::pthread_rwlock_wrlock(hostRwlock) : -1;
}""",
    r'extern "C" int bionic_pthread_rwlock_unlock\(void\* rwlock\).*?return hostRwlock \? ::pthread_rwlock_unlock\(hostRwlock\) : -1;\n\}': """extern "C" int bionic_pthread_rwlock_unlock(void* rwlock) {
    pthread_rwlock_t* hostRwlock = static_cast<pthread_rwlock_t*>(get_or_init_sync(rwlock, 3));
    return hostRwlock ? ::pthread_rwlock_unlock(hostRwlock) : -1;
}""",
    r'extern "C" int bionic_pthread_rwlock_destroy\(void\* rwlock\).*?return (res|result);\n\}': """extern "C" int bionic_pthread_rwlock_destroy(void* rwlock) {
    destroy_sync(rwlock, 3);
    return 0;
}""",
    r'extern "C" int bionic_pthread_mutex_trylock\(void\* guestMutex\).*?return hostMutex \? ::pthread_mutex_trylock\(hostMutex\) : -1;\n\}': """extern "C" int bionic_pthread_mutex_trylock(void* guestMutex) {
    pthread_mutex_t* hostMutex = static_cast<pthread_mutex_t*>(get_or_init_sync(guestMutex, 1));
    return hostMutex ? ::pthread_mutex_trylock(hostMutex) : -1;
}"""
}

# Apply replacements
for pattern, repl in replacements.items():
    content = re.sub(pattern, repl, content, flags=re.DOTALL)

with open('src/BionicShim.cpp', 'w') as f:
    f.write(content)

