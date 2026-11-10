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
    void* callback;
    void* data;
    uint64_t sequence;
};

struct ALooper {
    std::vector<ALooperFd> fds;
    std::mutex mtx;
    unsigned refCount = 1;
    bool attached = true;
    uint64_t nextSequence = 1;
    int wakePipe[2] = {-1, -1};

    ~ALooper() {
        for (int fd : wakePipe) if (fd >= 0) ::close(fd);
    }
};

static std::mutex g_looperMtx;

static void release_looper(ALooper* looper, bool detach = false) {
    bool destroy = false;
    {
        std::lock_guard<std::mutex> lock(g_looperMtx);
        if (detach) looper->attached = false;
        if (looper->refCount > (looper->attached ? 1u : 0u)) {
            destroy = --looper->refCount == 0;
        }
    }
    if (destroy) delete looper;
}

struct LooperThreadState {
    ALooper* looper = nullptr;
    ~LooperThreadState() {
        if (looper == nullptr) return;
        kudroid::frame_pacer_release_native_thread();
        release_looper(looper, true);
    }
};
static thread_local LooperThreadState t_looper;

// Lazy creation also supplies the native peer of an interpreted Java looper.
static ALooper* GetOrCreateThreadLooper() {
    if (t_looper.looper != nullptr) return t_looper.looper;
    auto* looper = new (std::nothrow) ALooper();
    if (looper == nullptr) return nullptr;
    if (::pipe(looper->wakePipe) != 0) {
        delete looper;
        return nullptr;
    }
    for (int fd : looper->wakePipe) {
        const int flags = ::fcntl(fd, F_GETFL);
        if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
            delete looper;
            return nullptr;
        }
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    t_looper.looper = looper;
    return looper;
}

extern "C" void* bionic_ALooper_prepare(int opts) {
    (void)opts;
    return GetOrCreateThreadLooper();
}

extern "C" void* bionic_ALooper_forThread() {
    return GetOrCreateThreadLooper();
}

extern "C" void bionic_ALooper_acquire(void* looper) {
    if (looper == nullptr) return;
    std::lock_guard<std::mutex> lock(g_looperMtx);
    ++static_cast<ALooper*>(looper)->refCount;
}

extern "C" void bionic_ALooper_release(void* looper) {
    if (looper != nullptr) release_looper(static_cast<ALooper*>(looper));
}

extern "C" void bionic_ALooper_wake(void* looper) {
    auto* l = static_cast<ALooper*>(looper);
    if (l == nullptr) return;
    const uint8_t byte = 1;
    while (::write(l->wakePipe[1], &byte, 1) < 0 && errno == EINTR) {}
}

extern "C" int bionic_ALooper_addFd(void* looper, int fd, int ident, int events,
                                    void* callback, void* data) {
    if (looper == nullptr || fd < 0 || (callback == nullptr && ident < 0)) return -1;
    auto* l = static_cast<ALooper*>(looper);
    {
        std::lock_guard<std::mutex> lock(l->mtx);
        ALooperFd entry{fd, ident, events, callback, data, l->nextSequence++};
        auto existing = std::find_if(l->fds.begin(), l->fds.end(),
                                      [fd](const ALooperFd& f) { return f.fd == fd; });
        if (existing != l->fds.end()) *existing = entry;
        else l->fds.push_back(entry);
    }
    bionic_ALooper_wake(l);
    return 1;
}

extern "C" int bionic_ALooper_removeFd(void* looper, int fd) {
    if (looper == nullptr || fd < 0) return -1;
    auto* l = static_cast<ALooper*>(looper);
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(l->mtx);
        auto existing = std::find_if(l->fds.begin(), l->fds.end(),
                                      [fd](const ALooperFd& f) { return f.fd == fd; });
        if (existing != l->fds.end()) {
            l->fds.erase(existing);
            removed = true;
        }
    }
    if (removed) bionic_ALooper_wake(l);
    return removed ? 1 : 0;
}

static int PollLooperOnce(int timeoutMillis, int* outFd, int* outEvents,
                          void** outData, bool* wasWoken) {
    constexpr int kWake = -1, kCallback = -2, kTimeout = -3, kError = -4;
    *wasWoken = false;
    ALooper* l = t_looper.looper;
    if (l == nullptr) return kError;
    if (outFd) *outFd = 0;
    if (outEvents) *outEvents = 0;
    if (outData) *outData = nullptr;
    std::vector<ALooperFd> snapshot;
    {
        std::lock_guard<std::mutex> lock(l->mtx);
        snapshot = l->fds;
    }
    std::vector<struct pollfd> pfds;
    for (const auto& f : snapshot) {
        short events = 0;
        if (f.events & ALOOPER_EVENT_INPUT) events |= POLLIN;
        if (f.events & ALOOPER_EVENT_OUTPUT) events |= POLLOUT;
        pfds.push_back({f.fd, events, 0});
    }
    pfds.push_back({l->wakePipe[0], POLLIN, 0});
    int ready;
    {
        const BlockingWaitScope tracked(WaitKind::kLooperPoll, l, guest_return_address(6));
        if (timeoutMillis >= 0) blocking_wait_note_budget(static_cast<uint64_t>(timeoutMillis));
        ready = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), timeoutMillis);
    }
    if (ready < 0) return errno == EINTR ? kWake : kError;
    if (ready == 0) return kTimeout;
    if (pfds.back().revents != 0) {
        *wasWoken = true;
        uint8_t bytes[64];
        while (::read(l->wakePipe[0], bytes, sizeof(bytes)) > 0) {}
    }
    bool called = false;
    int ident = kWake;
    for (size_t i = 0; i < snapshot.size(); ++i) {
        if (pfds[i].revents == 0) continue;
        const auto& f = snapshot[i];
        {
            std::lock_guard<std::mutex> lock(l->mtx);
            const auto current = std::find_if(l->fds.begin(), l->fds.end(),
                [&f](const ALooperFd& entry) { return entry.sequence == f.sequence; });
            if (current == l->fds.end()) continue;
        }
        int events = 0;
        if (pfds[i].revents & POLLIN) events |= ALOOPER_EVENT_INPUT;
        if (pfds[i].revents & POLLOUT) events |= ALOOPER_EVENT_OUTPUT;
        if (pfds[i].revents & POLLERR) events |= ALOOPER_EVENT_ERROR;
        if (pfds[i].revents & POLLHUP) events |= ALOOPER_EVENT_HANGUP;
        if (pfds[i].revents & POLLNVAL) events |= ALOOPER_EVENT_INVALID;
        if (f.callback != nullptr) {
            const int keep = reinterpret_cast<int (*)(int, int, void*)>(f.callback)(
                f.fd, events, f.data);
            called = true;
            if (keep == 0) {
                std::lock_guard<std::mutex> lock(l->mtx);
                const auto current = std::find_if(l->fds.begin(), l->fds.end(),
                    [&f](const ALooperFd& entry) { return entry.sequence == f.sequence; });
                if (current != l->fds.end()) l->fds.erase(current);
            }
        } else if (ident < 0) {
            ident = f.ident;
            if (outFd) *outFd = f.fd;
            if (outEvents) *outEvents = events;
            if (outData) *outData = f.data;
        }
    }
    return ident >= 0 ? ident : (called ? kCallback : kWake);
}

extern "C" int bionic_ALooper_pollOnce(int timeoutMillis, int* outFd, int* outEvents,
                                       void** outData) {
    bool wasWoken;
    return PollLooperOnce(timeoutMillis, outFd, outEvents, outData, &wasWoken);
}

extern "C" int bionic_ALooper_pollAll(int timeoutMillis, int* outFd, int* outEvents,
                                      void** outData) {
    const auto start = std::chrono::steady_clock::now();
    int remaining = timeoutMillis;
    for (;;) {
        bool wasWoken;
        const int result = PollLooperOnce(remaining, outFd, outEvents, outData, &wasWoken);
        if (result != -2) return result;
        if (wasWoken) return -1;
        if (timeoutMillis >= 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= timeoutMillis) return -3;
            remaining = timeoutMillis - static_cast<int>(elapsed);
        }
    }
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
    if (strerror_r(errnum, buf, buflen)) {}
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
// A semaphore that was never sem_init'd, or whose init arrived after a wait, is
// the shape a game engine's yield-style semaphore has: wait, post, wait, post, on
// an address the shim has no init for. Treating it as a blocking handshake parked
// the waiter for as long as the poster waited, and both threads stopped. A
// bounded try-wait keeps a real handshake working (the token is there when the
// post is ordered before the wait) and keeps a yield spinning.
static std::atomic<int> s_semUninitialised{0};
static std::atomic<int> s_semUninitialisedWarns{0};
static std::atomic<long long> s_semSpins{0};
static std::atomic<int> s_semSpinsWarned{0};

static std::shared_ptr<GuestSemaphore> guest_sem_find(const void* sem, bool create) {
    std::lock_guard<std::mutex> lock(g_semaphoresMtx);
    auto it = g_semaphores.find(sem);
    if (it != g_semaphores.end()) return it->second;
    if (!create) return nullptr;
    const int n = s_semUninitialised.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 3) {
        logAndroidMessage(5, "KuDroidSyscall",
                          "sem_wait/sem_post on a semaphore that was never sem_init'd;"
                          " treating it as initialised to 0 and spinning (waiters so far: "
                          + std::to_string(n) + ")");
    } else if (n == 64 && s_semUninitialisedWarns.fetch_add(1, std::memory_order_relaxed) < 2) {
        logAndroidMessage(4, "KuDroidSyscall",
                          "sem_wait on uninitialised semaphores has passed 63 instances"
                          " (this one is #" + std::to_string(n) + "); a guest that expects"
                          " a blocking handshake here will spin instead of parking");
    }
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
    std::shared_ptr<GuestSemaphore> old;
    {
        std::lock_guard<std::mutex> lock(g_semaphoresMtx);
        auto it = g_semaphores.find(sem);
        if (it != g_semaphores.end()) old = it->second;
        g_semaphores[sem] = s;  // re-init at the same address replaces the old state
    }
    // Migrate parkers: waiters on the replaced object re-resolve the address on
    // wake instead of hanging on orphaned state.
    if (old) old->cv.notify_all();
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

// A wait on a semaphore we never saw sem_init'd: yield with backoff. A
// condition-variable wait would park the thread for as long as the poster waits,
// and an engine that uses this semaphore to hand work between job workers
// (wait, post, wait, post, never initialised through the PLT) would then have
// every worker parked at once. Yielding keeps the handshake working when the post
// is already there and costs nothing when it is not; the backoff keeps a real
// producer's post from being missed.
static int bionic_sem_wait_uninitialised(const void* sem) {
    constexpr int kSpins = 64;
    for (int i = 0; i < kSpins; ++i) {
        if (i < 8) {
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        {
            auto s = guest_sem_find(sem, /*create=*/true);
            std::lock_guard<std::mutex> lock(s->mtx);
            if (s->value > 0) {
                --s->value;
                return 0;
            }
        }
    }
    // Parked, and named: a yield loop that ends in a park is a real wait, and a
    // stall report has to say so rather than time out silently.
    const long long spins = s_semSpins.fetch_add(1, std::memory_order_relaxed) + 1;
    if (spins == 1 || spins == 8 || (spins > 8 && (spins & 7) == 0 &&
        s_semSpinsWarned.fetch_add(1, std::memory_order_relaxed) < 64)) {
        logAndroidMessage(5, "KuDroidSyscall",
                          "sem_wait on an uninitialised semaphore exhausted its "
                          "yield window and is now parked (parkings so far: " +
                              std::to_string(spins) + "); a stall report names the caller");
    }
    auto s = guest_sem_find(sem, /*create=*/true);
    std::unique_lock<std::mutex> lock(s->mtx);
    {
        const BlockingWaitScope tracked(WaitKind::kSemaphore, sem, guest_return_address(6));
        for (int i = 0; i < kSpins; ++i) blocking_wait_note_iteration();
        s->cv.wait(lock, [&s] { return s->value > 0 || s->destroyed; });
    }
    if (s->value > 0) {
        --s->value;
        return 0;
    }
    return 0;  // destroyed or re-init'd: the loop in the initialised path re-resolves
}

extern "C" int bionic_sem_wait(sem_t* sem) {
    if (!sem) {
        errno = EINVAL;
        return -1;
    }
    // Bionic parks on the address, not on an object: destroy/reinit under a
    // waiter is harmless there. Re-resolve instead of failing with EINVAL,
    // which aborts guests that treat it as fatal.
    for (;;) {
        bool known = false;
        {
            std::lock_guard<std::mutex> lock(g_semaphoresMtx);
            known = g_semaphores.find(sem) != g_semaphores.end();
        }
        if (!known) return bionic_sem_wait_uninitialised(sem);
        auto s = guest_sem_find(sem, /*create=*/false);
        bool hasToken = false;
        {
            std::unique_lock<std::mutex> lock(s->mtx);
            const BlockingWaitScope tracked(WaitKind::kSemaphore, sem,
                                            guest_return_address(6));
            s->cv.wait(lock, [&s] { return s->value > 0 || s->destroyed; });
            if (s->value > 0) {
                --s->value;
                hasToken = true;
            }
        }
        if (hasToken) return 0;
        // Woken without a token: spurious, destroyed, or reinited. A spurious
        // wakeup still parks on the live object; destroy/reinit migrates or ends.
        bool current = false;
        bool replaced = false;
        {
            std::lock_guard<std::mutex> lock(g_semaphoresMtx);
            auto it = g_semaphores.find(sem);
            current = (it != g_semaphores.end() && it->second == s);
            replaced = (it != g_semaphores.end() && it->second != s);
        }
        if (current) continue;
        if (replaced) continue;  // loop migrates to the new object
        // Truly destroyed with no successor: succeed spuriously. EINVAL here
        // aborts guests at shutdown; hanging wedges teardown. A token from
        // nothing is the least harmful of the three in UB territory.
        return 0;
    }
}

extern "C" int bionic_sem_trywait(sem_t* sem) {
    if (!sem) {
        errno = EINVAL;
        return -1;
    }
    auto s = guest_sem_find(sem, /*create=*/true);
    std::unique_lock<std::mutex> lock(s->mtx);
    if (s->value == 0) {
        errno = guest_errno(EAGAIN);
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
    // The deadline is absolute CLOCK_REALTIME; convert to a duration so it can be
    // waited on without assuming the host clock's epoch matches.
    for (;;) {
        struct timespec now;
        ::clock_gettime(CLOCK_REALTIME, &now);
        int64_t remSec = int64_t(abs_timeout->tv_sec) - int64_t(now.tv_sec);
        int64_t remNs = int64_t(abs_timeout->tv_nsec) - int64_t(now.tv_nsec);
        if (remNs < 0) {
            remSec -= 1;
            remNs += 1000000000;
        }

        // Re-resolve every round: a destroy/reinit under us migrates the waiter
        // to the new object instead of failing with EINVAL.
        auto s = guest_sem_find(sem, /*create=*/true);
        bool hasToken = false;
        {
            std::unique_lock<std::mutex> lock(s->mtx);
            auto ready = [&s] { return s->value > 0 || s->destroyed; };
            if (remSec < 0) {
                // Already expired. POSIX still requires the semaphore be taken if it is
                // available, so check before reporting a timeout.
                if (!ready()) {
                    errno = guest_errno(ETIMEDOUT);
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
                    errno = guest_errno(ETIMEDOUT);
                    return -1;
                }
            }
            if (s->value > 0) {
                --s->value;
                hasToken = true;
            }
        }
        if (hasToken) return 0;
        // Woken without a token: spurious, destroyed, or reinited. Same rule as
        // sem_wait: spurious re-waits, reinit migrates, true destroy succeeds
        // spuriously rather than EINVAL-aborting the guest.
        bool current = false;
        bool replaced = false;
        {
            std::lock_guard<std::mutex> lock(g_semaphoresMtx);
            auto it = g_semaphores.find(sem);
            current = (it != g_semaphores.end() && it->second == s);
            replaced = (it != g_semaphores.end() && it->second != s);
        }
        if (current || replaced) continue;  // loop re-resolves; deadline rechecked
        return 0;
    }
}
