// Wrappers previously bound to the dummy; on arm64/Linux these match the host signatures.
namespace {

// Which host fds point where: lets the pread tap attribute reads without
// slowing every call (path resolved once per fd, forgotten at close).
// Empty string = resolved non-APK; absent = unresolved.
std::mutex g_apkFdMtx;
std::map<int, std::string> g_apkFd;
constexpr int kApkFdUntracked = 64;

// Read volume per path: bulk asset flow (bundles, scenes) shows here without
// flooding the log with one line per read.
std::mutex g_ioVolMtx;
std::map<std::string, std::pair<uint64_t, uint64_t>> g_ioVol;
uint64_t g_ioVolTotal = 0;
uint64_t g_ioVolNextLog = 5ULL * 1024 * 1024;
// Monotonic-ns of the last tracked I/O byte. Lock-free readers (the watchdog)
// only need a race-tolerant hint, so an atomic store under the volume mutex is
// enough — the write side already serializes.
std::atomic<uint64_t> g_ioVolLastNs{0};

static std::string short_path(const std::string& p) {
    return p.size() > 60 ? "..." + p.substr(p.size() - 57) : p;
}

// Returned BY VALUE on purpose: the reference version dangled when a
// concurrent close erased the map entry (or the fd number was recycled),
// turning volume accounting into a heap UAF. One small string copy per
// read/pread is cheaper than that.
std::string fd_path(int fd) {
    {
        std::lock_guard<std::mutex> lock(g_apkFdMtx);
        const auto it = g_apkFd.find(fd);
        if (it != g_apkFd.end()) return it->second;
        if (g_apkFd.size() >= static_cast<size_t>(kApkFdUntracked) * 16) {
            static const std::string kEmpty;
            return kEmpty;
        }
    }
    std::string path;
    char buf[1024] = {0};
#if defined(__APPLE__)
    if (::fcntl(fd, F_GETPATH, buf) != -1) path = buf;
#else
    char proc[64];
    std::snprintf(proc, sizeof(proc), "/proc/self/fd/%d", fd);
    const ssize_t n = ::readlink(proc, buf, sizeof(buf) - 1);
    if (n > 0) path.assign(buf, static_cast<size_t>(n));
#endif
    std::lock_guard<std::mutex> lock(g_apkFdMtx);
    auto it = g_apkFd.find(fd);
    if (it != g_apkFd.end()) return it->second;
    return g_apkFd.emplace(fd, std::move(path)).first->second;
}

bool fd_is_apk(int fd) {
    const std::string& p = fd_path(fd);
    const size_t n = p.size();
    return n >= 8 && p.compare(n - 8, 8, "base.apk") == 0;
}

// extern "C" bridge over the namespace-local predicate, for callers earlier in the
// file (bionic_mmap) that sit outside this anonymous namespace.
extern "C" int kudroid_fd_is_apk(int fd) { return fd_is_apk(fd) ? 1 : 0; }

// dir: "read" or "write". A save that never lands and a load that never
// happens look identical when both are reported as a read, which is exactly
// the question a PlayerPrefs-style "my flag did not save" report turns on.
static void io_volume_add(const std::string& path, uint64_t bytes, const char* dir) {
    if (path.empty() || bytes == 0) return;
    // Copy the table for the top-5 sort OUTSIDE the lock: sorting up to 256
    // entries under it stalled every reader each 5MB.
    bool report = false;
    using Entry = std::pair<std::string, std::pair<uint64_t, uint64_t>>;
    std::vector<Entry> top;
    uint64_t total = 0;
    {
        std::lock_guard<std::mutex> lock(g_ioVolMtx);
        if (g_ioVol.size() >= 256 && g_ioVol.find(path) == g_ioVol.end()) return;
        // First bytes from a path: names exactly which files the engine pulls.
        // A run where bundles never appear here is a run where content never
        // arrived, whatever the engine logs claim about loading.
        if (g_ioVol.find(path) == g_ioVol.end()) {
            static int s_firstLogged = 0;
            if (s_firstLogged < 80) {
                ++s_firstLogged;
                std::fprintf(stderr, "[KuDroidIO] first-%s %s\n", dir,
                             short_path(path).c_str());
            }
        }
        auto& e = g_ioVol[path];
        e.first += bytes;
        e.second += 1;
        g_ioVolTotal += bytes;
        // Last time ANY byte flowed. The watchdog reads this: a load that stops making
        // I/O progress while the engine keeps rendering is the signature of a loader
        // wedged somewhere no shim observes — the exact blind spot thread sampling
        // exists for but nothing was firing it into.
        g_ioVolLastNs.store(static_cast<uint64_t>(std::chrono::steady_clock::now()
                                                    .time_since_epoch()
                                                    .count()),
                            std::memory_order_relaxed);
        if (g_ioVolTotal >= g_ioVolNextLog) {
            g_ioVolNextLog += 5ULL * 1024 * 1024;
            top.assign(g_ioVol.begin(), g_ioVol.end());
            total = g_ioVolTotal;
            report = true;
        }
    }
    if (!report) return;
    std::sort(top.begin(), top.end(), [](const Entry& a, const Entry& b) {
        return a.second.first > b.second.first;
    });
    std::string line = "pread total=" + std::to_string(total) + "B";
    for (size_t i = 0; i < top.size() && i < 5; ++i) {
        line += " | " + short_path(top[i].first) + "=" +
                std::to_string(top[i].second.first) + "B/" +
                std::to_string(top[i].second.second) + "ops";
    }
    std::fprintf(stderr, "[KuDroidIO] %s\n", line.c_str());
}

void fd_forget_apk(int fd) {
    std::lock_guard<std::mutex> lock(g_apkFdMtx);
    g_apkFd.erase(fd);
}

}  // namespace

// Watchdog hook: monotonic-nanosecond timestamp of the last byte that flowed
// through any tracked I/O path (read, pread64, write). Exposed so the watchdog
// can dump thread stacks when loads stop making I/O progress on their own.
// Outside the anonymous namespace: NativeCallTelemetry links against it by name.
extern "C" uint64_t io_last_activity_ns() {
    return g_ioVolLastNs.load(std::memory_order_relaxed);
}

// A guest reads errno as Linux numbers (EAGAIN 11, EINPROGRESS 115, ...); the host sets
// Darwin's. Socket failures are translated on the way out so a client can tell "try again"
// and "connect in progress" from a real error.
template <typename T>
static inline T net_result(T rc) {
#if defined(__APPLE__)
    if (rc < 0) errno = kudroid::SocketErrnoToGuest(errno);
#endif
    return rc;
}

// Socket fds the guest owns, so reads/writes/polls on them can be traced by result.
static std::atomic<unsigned char> g_netFd[1024];
static inline bool net_fd(int fd) {
    return fd >= 0 && fd < 1024 && g_netFd[fd].load(std::memory_order_relaxed) != 0;
}
static inline void net_fd_set(int fd, bool on) {
    if (fd >= 0 && fd < 1024) g_netFd[fd].store(on ? 1 : 0, std::memory_order_relaxed);
}

// Bounded per-process trace of socket traffic results.
static bool net_trace_take() {
    static std::atomic<int> left{400};
    return left.fetch_sub(1, std::memory_order_relaxed) > 0;
}

static void net_trace_vector_io(const char* operation, int fd, size_t vectors, ssize_t result) {
    const int savedErrno = errno;
    if (net_fd(fd) && net_trace_take()) {
        std::fprintf(stderr, "[KuDroidNet] %s fd=%d vectors=%zu -> %zd errno=%d\n",
                     operation, fd, vectors, result, result < 0 ? savedErrno : 0);
    }
    errno = savedErrno;
}

extern "C" ssize_t bionic_read(int fd, void* buf, size_t count) {
    const ssize_t ret = ::read(fd, buf, count);
    const int hostErr = errno;
    if (ret > 0) {
        const std::string path = fd_path(fd);
        io_volume_add(path, static_cast<uint64_t>(ret), "read");
        if (path == "/dev/urandom" || path == "/dev/random") {
            static std::atomic<int> entropyReads{0};
            if (entropyReads.fetch_add(1, std::memory_order_relaxed) < 16)
                std::fprintf(stderr, "[KuDroidNet] entropy read fd=%d count=%zu -> %zd\n", fd, count, ret);
        }
        // Trace the APK itself: Unity's zip-reader drains base.apk through raw
        // read() (1793 ops / 46MB in one session with zero pread64 traffic),
        // and no per-op trace existed here — the widest remaining blind spot
        // for locating which consumer reads which offset region.
        if (kudroid_fd_is_apk(fd)) {
            static std::atomic<int> s_apkReads{0};
            if (s_apkReads.load(std::memory_order_relaxed) < 200) {
                s_apkReads.fetch_add(1, std::memory_order_relaxed);
                std::fprintf(stderr,
                             "[KuDroidApk] read fd=%d count=%zu ret=%zd\n",
                             fd, count, ret);
            }
        }
    }
    if (audio_fd_trace_take()) {
        const std::string p = fd_path(fd);
        std::fprintf(stderr,
                     "%s[KuDroidFd] AUDIO read fd=%d apk=%d path=%s count=%zu ret=%zd\n",
                     kudroid_trace_stamp(), fd, kudroid_fd_is_apk(fd) ? 1 : 0,
                     p.empty() ? "?" : p.c_str(), count, ret);
    }
    if (net_fd(fd) && net_trace_take()) {
        std::fprintf(stderr, "[KuDroidNet] read fd=%d count=%zu -> %zd errno=%d\n", fd, count,
                     ret, ret < 0 ? hostErr : 0);
    }
    errno = hostErr;
    return net_result(ret);
}

extern "C" ssize_t bionic_write(int fd, const void* buf, size_t count) {
    const ssize_t ret = ::write(fd, buf, count);
    const int hostErr = errno;
    if (ret > 0) {
        io_volume_add(std::string(fd_path(fd)), static_cast<uint64_t>(ret), "write");
    }
    if (net_fd(fd) && net_trace_take()) {
        std::fprintf(stderr, "[KuDroidNet] write fd=%d count=%zu -> %zd errno=%d\n", fd, count,
                     ret, ret < 0 ? hostErr : 0);
    }
    errno = hostErr;
    return net_result(ret);
}

// poll() takes the host path unchanged (same pollfd layout and event bits); the shim only
// reports results for sockets, since a connect that never completes shows up here.
extern "C" int bionic_poll(struct pollfd* fds, unsigned long nfds, int timeout) {
    const int rc = ::poll(fds, static_cast<nfds_t>(nfds), timeout);
    const int hostErr = errno;
    if (fds != nullptr && nfds > 0 && (net_fd(fds[0].fd) || rc < 0) && (rc != 0 || timeout >= 1000) &&
        net_trace_take()) {
        std::fprintf(stderr,
                     "[KuDroidNet] poll n=%lu fd=%d ev=0x%x timeout=%d -> %d rev=0x%x errno=%d\n", nfds,
                     fds[0].fd, static_cast<unsigned>(fds[0].events), timeout, rc,
                     static_cast<unsigned>(fds[0].revents), rc < 0 ? hostErr : 0);
    }
    errno = hostErr;
    return net_result(rc);
}

// stdio writes, which never reach bionic_write: the guest's fwrite is the host's
// fwrite, and that lands on the host write(2) directly. So a save through
// fopen/fwrite -- which is how Unity persists PlayerPrefs and save files -- was
// invisible: the path showed "first-read" and never a write, and "my flag did
// not save" and "the flag was never written" were indistinguishable.
extern "C" size_t kudroid_fwrite(const void* buf, size_t size, size_t nmemb,
                                 FILE* stream) {
    const size_t ret = std::fwrite(buf, size, nmemb, stream);
    if (ret > 0 && stream != nullptr) {
        const int fd = ::fileno(stream);
        if (fd >= 0) {
            io_volume_add(std::string(fd_path(fd)),
                          static_cast<uint64_t>(ret) * size, "write");
        }
    }
    return ret;
}

