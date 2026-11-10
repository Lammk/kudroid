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
    // Time-throttled, not lifetime-capped: a fixed 256-line budget was all
    // spent in the one-second burst when Unity spawns its 22 Job.Workers, and
    // the tail of that burst then sat in the crash ring — 177 of the last
    // ~200 lines before a crash 3 minutes later were thread-sync noise,
    // pushing out everything the crash actually needed. One line per second
    // still traces a sync-object story across a whole session while capping
    // the ring cost at ~8KB/minute.
    static std::atomic<long long> s_nextNs{0};
    const long long nowNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    long long due = s_nextNs.load(std::memory_order_relaxed);
    if (nowNs < due ||
        !s_nextNs.compare_exchange_strong(due, nowNs + 1000LL * 1000 * 1000,
                                          std::memory_order_relaxed)) {
        return;
    }
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

// A guest condition variable maps to a host one plus the clock its absolute timeouts are
// measured on. Bionic reads that clock from the attribute given to pthread_cond_init
// (CLOCK_REALTIME by default); the host condition only understands the wall clock, so
// timeouts on a monotonic condition are converted when the wait starts.
struct HostCond {
    pthread_cond_t cond;
    std::atomic<bool> monotonic{false};
};

static inline void* create_sync_obj(int type) {
    if (type == SYNC_MUTEX) {
        return create_mutex_obj(BIONIC_MUTEX_NORMAL);
    } else if (type == SYNC_COND) {
        auto* hostCond = new (std::nothrow) HostCond();
        if (!hostCond) return nullptr;
        if (::pthread_cond_init(&hostCond->cond, nullptr) != 0) {
            delete hostCond;
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
    if (type == SYNC_COND) {
        auto* host = static_cast<HostCond*>(host_obj);
        ::pthread_cond_destroy(&host->cond);
        delete host;
        return;
    }
    if (type == SYNC_RWLOCK) ::pthread_rwlock_destroy(static_cast<pthread_rwlock_t*>(host_obj));
    std::free(host_obj);
}

// Error numbers as the guest's libc defines them (Linux). A guest compares errno and
// pthread return codes against its own constants, and the host's differ for several:
// EAGAIN 35/11, EDEADLK 11/35, ETIMEDOUT 60/110. A shim that reports a failure itself
// must therefore use these, or a guest that only tolerates a timeout (110) aborts on
// the host's number instead.
static inline int guest_errno(int hostErrno) {
#if defined(__APPLE__)
    if (hostErrno == EDEADLK) return 35;
    return kudroid::SocketErrnoToGuest(hostErrno);
#else
    return hostErrno;
#endif
}

// Bumped whenever a registered object is replaced or destroyed, so per-thread lookup
// caches never hand out a pointer that was retired.
static std::atomic<uint64_t> gSyncGeneration{1};

// Direct-mapped per-thread cache in front of the registry: lock/unlock run thousands of
// times a frame and every one of them used to take the shared registry lock.
struct SyncLookupCache {
    static constexpr unsigned kSlots = 128;
    uint64_t generation = 0;
    void* guest[kSlots] = {};
    void* host[kSlots] = {};
};
static thread_local SyncLookupCache t_syncCache;

static inline void* get_or_init_sync(void* guest_ptr, int type) {
    if (!guest_ptr) return nullptr;

    SyncLookupCache& cache = t_syncCache;
    const uint64_t generation = gSyncGeneration.load(std::memory_order_acquire);
    if (cache.generation != generation) {
        std::memset(cache.guest, 0, sizeof(cache.guest));
        cache.generation = generation;
    }
    const unsigned slot = static_cast<unsigned>(
        (reinterpret_cast<uintptr_t>(guest_ptr) >> 3) % SyncLookupCache::kSlots);
    if (cache.guest[slot] == guest_ptr) return cache.host[slot];

    {
        std::shared_lock<std::shared_mutex> lock(gSyncRegistryLock);
        auto it = gSyncRegistry.find(guest_ptr);
        if (it != gSyncRegistry.end()) {
            cache.guest[slot] = guest_ptr;
            cache.host[slot] = it->second;
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
        gSyncGeneration.fetch_add(1, std::memory_order_release);
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
        gSyncGeneration.fetch_add(1, std::memory_order_release);
    }
}

extern "C" int bionic_pthread_mutex_init(void* guestMutex, const void* attr) {
    if (!guestMutex) return EINVAL;

    // Bionic pthread_mutexattr_t: mutex kind (RECURSIVE=1, ERRORCHECK=2) in the
    // low 2 bits.
    int kind = BIONIC_MUTEX_NORMAL;
    if (attr) kind = static_cast<int>((*static_cast<const uint32_t*>(attr)) & 0x3u);

    void* hostMutex = create_mutex_obj(kind);
    if (!hostMutex) return guest_errno(EAGAIN);

    register_sync(guestMutex, hostMutex, SYNC_MUTEX);
    return 0;
}

// Bionic stores a condition attribute's clock in bit 1 of its first word
// (0 = CLOCK_REALTIME, 1 = CLOCK_MONOTONIC).
static constexpr uint32_t kBionicCondClockMonotonic = 2u;

extern "C" int bionic_pthread_cond_init(void* cond, const void* attr) {
    if (!cond) return EINVAL;
    void* hostCond = create_sync_obj(SYNC_COND);
    if (!hostCond) return guest_errno(EAGAIN);
    if (attr != nullptr) {
        uint32_t word = 0;
        std::memcpy(&word, attr, sizeof(word));
        static_cast<HostCond*>(hostCond)->monotonic.store((word & kBionicCondClockMonotonic) != 0,
                                                          std::memory_order_relaxed);
    }

    register_sync(cond, hostCond, SYNC_COND);
    return 0;
}

extern "C" int bionic_pthread_rwlock_init(void* rwlock, const void* attr) {
    (void)attr;
    if (!rwlock) return EINVAL;
    void* hostRwlock = create_sync_obj(SYNC_RWLOCK);
    if (!hostRwlock) return guest_errno(EAGAIN);

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
            return guest_errno(EDEADLK);
        }
    }

    // Uncontended locks take the fast path: the wait registry and the caller walk are
    // only worth their cost when this thread is about to block.
    int rc = ::pthread_mutex_trylock(&host->mutex);
    if (rc == EBUSY) rc = [&] {
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
    return rc != 0 ? guest_errno(rc) : 0;
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
static inline HostCond* host_cond_for(void* guestCond) {
    return static_cast<HostCond*>(get_or_init_sync(guestCond, SYNC_COND));
}

extern "C" int bionic_pthread_cond_destroy(void* cond) {
    destroy_sync(cond, SYNC_COND);
    return 0;
}
extern "C" int bionic_pthread_cond_wait(void* cond, void* mutex) {
    HostCond* hostCondObj = host_cond_for(cond);
    pthread_cond_t* hostCond = hostCondObj ? &hostCondObj->cond : nullptr;
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
    HostCond* hostCondObj = host_cond_for(cond);
    pthread_cond_t* hostCond = hostCondObj ? &hostCondObj->cond : nullptr;
    HostMutex* host = host_mutex_for(mutex);
    if (!hostCond || !host) return EINVAL;
    if (abstime == nullptr) return EINVAL;
    // The host condition measures absolute time on the wall clock. A monotonic guest
    // condition hands over a CLOCK_MONOTONIC instant, which on the wall clock is decades
    // in the past: every wait would time out at once and its caller would spin.
    struct timespec deadline = *abstime;
    if (hostCondObj->monotonic.load(std::memory_order_relaxed)) {
        struct timespec mono{}, wall{};
        clock_gettime(kudroid::host_clock_for_guest(1), &mono);
        clock_gettime(CLOCK_REALTIME, &wall);
        long long sec = static_cast<long long>(abstime->tv_sec) - mono.tv_sec;
        long long nsec = static_cast<long long>(abstime->tv_nsec) - mono.tv_nsec;
        // Bound the distance so the sum below cannot overflow time_t.
        constexpr long long kMaxAheadSec = 1ll << 31;
        if (sec > kMaxAheadSec) sec = kMaxAheadSec;
        if (sec < -kMaxAheadSec) sec = -kMaxAheadSec;
        sec += wall.tv_sec;
        nsec += wall.tv_nsec;
        while (nsec >= 1000000000ll) { nsec -= 1000000000ll; ++sec; }
        while (nsec < 0) { nsec += 1000000000ll; --sec; }
        deadline.tv_sec = static_cast<time_t>(sec);
        deadline.tv_nsec = static_cast<long>(nsec);
    }
    const bool track = host->track_owner;
    if (track) host->owner.store(0, std::memory_order_relaxed);
    sync_diag("cond-timedwait-enter", cond, mutex, 0);
    const int result = [&] {
        const BlockingWaitScope tracked(WaitKind::kConditionTimed, cond,
                                        guest_return_address(6));
        return ::pthread_cond_timedwait(hostCond, &host->mutex, &deadline);
    }();
    sync_diag("cond-timedwait-return", cond, mutex, result);
    // The mutex is reacquired on timeout too, so ownership is restored either way.
    if (track) host->owner.store(current_thread_id(), std::memory_order_relaxed);
    return result != 0 ? guest_errno(result) : 0;
}
extern "C" int bionic_pthread_cond_signal(void* cond) {
    HostCond* hostCondObj = host_cond_for(cond);
    pthread_cond_t* hostCond = hostCondObj ? &hostCondObj->cond : nullptr;
    const int result = hostCond ? ::pthread_cond_signal(hostCond) : -1;
    sync_diag("cond-signal", cond, nullptr, result);
    return result;
}
extern "C" int bionic_pthread_cond_broadcast(void* cond) {
    HostCond* hostCondObj = host_cond_for(cond);
    pthread_cond_t* hostCond = hostCondObj ? &hostCondObj->cond : nullptr;
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

