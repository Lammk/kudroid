// Tracked APK streams: guest fread bypasses the syscall layer, so volume on
// base.apk (catalog/asset bytes) is otherwise invisible. Lock-free lookup,
// locked mutation (open/close are rare, reads are hot). Sized for parallel readers.
namespace {
constexpr int kTrackedStreams = 32;
std::atomic<uintptr_t> g_apkStreams[kTrackedStreams];
std::atomic<int> g_apkStreamFd[kTrackedStreams] = {};
std::atomic<off_t> g_apkStreamSize[kTrackedStreams] = {};
// One cursor per FILE*, not one per thread.
//
// A C FILE has exactly one file position, and bionic serialises every operation
// on it behind an internal lock, so two guest threads sharing a FILE* take turns
// advancing the same cursor. The guest depends on that: Unity hands FMOD the
// loader's own handle for an FSB slice and both threads read it (observed live --
// sid=2 read by Loading.PreloadManager and by the FMOD nonblocking thread), and
// each thread's next implicit fread has to continue where the stream actually is.
// A per-thread cursor gave each thread a private position instead, so a thread
// picking up the stream mid-slice continued from a stale offset and read
// well-formed bytes from the wrong place -- FMOD's "Error loading file" with no
// short read anywhere in the trace. An authoritative cursor under a per-stream
// lock reproduces the host's semantics, and the lock also makes each op's
// read-then-advance atomic, which the old shared atomic could not do.
std::atomic<off_t> g_apkStreamPos[kTrackedStreams] = {};
std::atomic<int> g_apkStreamEof[kTrackedStreams] = {};
std::atomic<int> g_apkStreamErr[kTrackedStreams] = {};
std::atomic<uint64_t> g_apkStreamOps[kTrackedStreams] = {};
std::atomic<int> g_apkStreamShortLogged[kTrackedStreams] = {};
std::mutex g_apkStreamMtx[kTrackedStreams];
constexpr int kOpTracedStreams = 16;
std::atomic<int> g_apkStreamTraceNo[kTrackedStreams] = {};
std::atomic<int> g_apkStreamInstances{0};
std::mutex g_apkStreamsMtx;

// Reserved-slot marker. A slot publishes the FILE* only after its record is
// complete, so a reader can never match a half-initialised stream.
constexpr uintptr_t kApkStreamReserved = 1;

constexpr int kAudioOpCapPerStream = 400;
constexpr int kAudioOpBudget = 3000;
std::atomic<int> g_audioStreamOps[kTrackedStreams] = {};
std::atomic<int> g_audioOpsTraced{0};
// The decoder's own threads get a separate budget: the preload burst spends the
// shared one in its first seconds, after which a clip load that fails for a
// file-level reason would be untraceable for the rest of the session. FMOD's I/O
// is what names the geometry it was handed, so it stays visible on its own cap.
constexpr int kFmodOpCapPerStream = 120;
constexpr int kFmodOpBudget = 800;
std::atomic<int> g_fmodStreamOps[kTrackedStreams] = {};
std::atomic<int> g_fmodOpsTraced{0};
std::atomic<int> g_apkStreamDeadLogged[kTrackedStreams] = {};

}  // namespace

// Thread names come from the host pthread (bionic_prctl forwards the guest's name
// request to it). Cached per thread because this runs on the read path; a thread
// may be named after its first op, so the cache expires every 256 calls rather
// than being taken once.
static bool audio_thread() {
    static thread_local unsigned calls = 0;
    static thread_local bool audio = false;
    if ((calls++ & 0xFFu) == 0) {
        char name[64] = {};
#if defined(__APPLE__)
        pthread_getname_np(pthread_self(), name, sizeof(name));
#else
        (void)pthread_getname_np(pthread_self(), name, sizeof(name));
#endif
        audio = name[0] != '\0' && (std::strstr(name, "FMOD") != nullptr ||
                                    std::strstr(name, "Preload") != nullptr ||
                                    std::strstr(name, "Audio") != nullptr);
    }
    return audio;
}

// Decoder threads only. Same cached classification as audio_thread().
static bool fmod_thread() {
    static thread_local unsigned calls = 0;
    static thread_local bool fmod = false;
    if ((calls++ & 0xFFu) == 0) {
        char name[64] = {};
#if defined(__APPLE__)
        pthread_getname_np(pthread_self(), name, sizeof(name));
#else
        (void)pthread_getname_np(pthread_self(), name, sizeof(name));
#endif
        fmod = name[0] != '\0' && std::strstr(name, "FMOD") != nullptr;
    }
    return fmod;
}

// Same answer for the fd layer (SyscallShim), which cannot see this TU's statics.
extern "C" bool kudroid_audio_thread(void) { return audio_thread(); }

// True when this op should be traced, consuming one slot of both budgets.
// sid < 0 means "no stream yet" (open): only the process budget applies.
static bool audio_trace_take(int sid) {
    if (!kudroid::log::trace_hot()) return false;
    if (sid >= kTrackedStreams) return false;
    if (!audio_thread()) return false;
    if (fmod_thread()) {
        if (sid >= 0 &&
            g_fmodStreamOps[sid].load(std::memory_order_relaxed) >= kFmodOpCapPerStream) {
            return false;
        }
        if (g_fmodOpsTraced.load(std::memory_order_relaxed) >= kFmodOpBudget) return false;
        if (sid >= 0) g_fmodStreamOps[sid].fetch_add(1, std::memory_order_relaxed);
        g_fmodOpsTraced.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    if (sid >= 0 &&
        g_audioStreamOps[sid].load(std::memory_order_relaxed) >= kAudioOpCapPerStream) {
        return false;
    }
    if (g_audioOpsTraced.load(std::memory_order_relaxed) >= kAudioOpBudget) return false;
    if (sid >= 0) g_audioStreamOps[sid].fetch_add(1, std::memory_order_relaxed);
    g_audioOpsTraced.fetch_add(1, std::memory_order_relaxed);
    return true;
}

static void track_apk_stream(FILE* f) {
    if (f == nullptr) return;
    const int fd = ::fileno(f);
    off_t fsize = 0;
    if (fd >= 0) {
        struct stat st{};
        if (::fstat(fd, &st) == 0) {
            fsize = st.st_size;
        }
    }
    std::lock_guard<std::mutex> lock(g_apkStreamsMtx);
    for (int i = 0; i < kTrackedStreams; ++i) {
        uintptr_t empty = 0;
        if (g_apkStreams[i].compare_exchange_strong(empty, kApkStreamReserved,
                                                    std::memory_order_acq_rel)) {
            {
                std::lock_guard<std::mutex> slock(g_apkStreamMtx[i]);
                g_apkStreamPos[i].store(0, std::memory_order_relaxed);
                g_apkStreamEof[i].store(0, std::memory_order_relaxed);
                g_apkStreamErr[i].store(0, std::memory_order_relaxed);
            }
            g_apkStreamFd[i].store(fd, std::memory_order_relaxed);
            g_apkStreamSize[i].store(fsize, std::memory_order_relaxed);
            g_apkStreamOps[i].store(0, std::memory_order_relaxed);
            g_apkStreamShortLogged[i].store(0, std::memory_order_relaxed);
            g_audioStreamOps[i].store(0, std::memory_order_relaxed);
            const int instance = g_apkStreamInstances.fetch_add(1, std::memory_order_relaxed);
            g_apkStreamTraceNo[i].store(instance, std::memory_order_relaxed);
            // Release: a thread that resolves this FILE* to sid sees the record above.
            g_apkStreams[i].store(reinterpret_cast<uintptr_t>(f), std::memory_order_release);
            if (kudroid::log::trace_hot() && instance < kOpTracedStreams) {
                ktraceLine("[KuDroidApkS] sid=%d open instance=%d (fd=%d size=%lld)\n",
                           i, instance, fd, static_cast<long long>(fsize));
            }
            return;
        }
    }
    ktraceLine("[KuDroidApkS] stream table full — apk stream untracked\n");
}


// The slot still holds this FILE*: fclose can release a slot while a reader is
// inside the syscall, and a slot that has since been reused must not absorb the
// dead stream's read.
static bool apk_stream_live(int sid, FILE* f) {
    return g_apkStreams[sid].load(std::memory_order_acquire) ==
           reinterpret_cast<uintptr_t>(f);
}


static int apk_stream_id(FILE* f) {
    const auto p = reinterpret_cast<uintptr_t>(f);
    for (int i = 0; i < kTrackedStreams; ++i) {
        if (g_apkStreams[i].load(std::memory_order_relaxed) == p) return i;
    }
    return -1;
}
[[maybe_unused]] static bool is_apk_stream(FILE* f) {
    return apk_stream_id(f) >= 0;
}

// Read volume per FILE path: bulk flow through fread (Unity's main read path)
// shows here. The hot read path takes NO lock: each thread batches into a
// thread-local slot keyed by stream, merged under the mutex at 256KB or on
// stream change. A global fclose epoch guards FILE* address reuse — without
// it a recycled address would silently attribute bytes to the dead file's
// path. Open/close stay locked (rare); only the merge path locks.
std::mutex g_freadVolMtx;
std::map<FILE*, std::string> g_freadPaths;
std::map<std::string, std::pair<uint64_t, uint64_t>> g_freadVol;
std::atomic<uint64_t> g_freadVolTotal{0};
uint64_t g_freadVolNextLog = 5ULL * 1024 * 1024;
std::atomic<uint64_t> g_freadEpoch{0};
// Post-FSB5 follow-up countdowns keyed per FILE* (probe in vfs_fread): the
// FMOD bank stream's next reads must be logged on that stream's own budget,
// not a global one — interleaved reads on unrelated handles (zip-walk
// 30-byte local-header probes on a second base.apk handle) ate the global
// counter and the probe showed the wrong stream.
std::map<FILE*, int> g_fsbFollowup;

// Audio-blob coverage, keyed by (archive, [blobStart, blobEnd)).
//
// Unity hands FMOD one FSB5 slice per clip and reads it out of the .resource
// entry in 2048-byte chunks, re-opening base.apk for almost every chunk. A
// per-FILE* chain therefore breaks on nearly every read and reported every
// blob as TRUNCATED -- a handle-churn artifact, not a fact about the bytes.
// The window is the stable thing, so coverage is counted per window across all
// handles and the verdict names the blob FMOD was actually handed.
constexpr int kFsbBlock = 2048;
// How long a blob may go untouched before it is announced SHORT and retired. Long
// enough to cover a clip the guest loads later (a bank is walked in order, so early
// clips are touched minutes before the last one), short enough that the list stays
// bounded over a long session.
constexpr uint64_t kFsbStaleMs = 120000;
// Chain walk inside one read: a packed bank has hundreds of blobs, and the guest
// typically reads one blob at a time, so this only has to cover a read that happens
// to span many headers.
constexpr unsigned kFsbChainMax = 4096;
struct FsbWindow {
    long start = 0;
    long end = 0;
    uint64_t expected = 0;
    uint64_t blocks = 0;            // distinct kFsbBlock-byte blocks seen
    std::vector<uint64_t> covered;  // bitmap, one bit per block
    // The blob's own header, kept so reads that overlap only a few bytes of a blob
    // are charged to that blob and not to a neighbour that starts at the same 2048
    // block. The bitmap does that job only while every block boundary is aligned to
    // the blob's start; unaligned it did not, and the accounting was off by whole
    // blocks.
    uint8_t hdr[60];
    // Verdict already announced. A guest that fails to play a clip re-reads the
    // same blob for as long as it keeps retrying, and erasing the window on
    // COMPLETE made every retry re-register and re-announce it: one 576-byte
    // blob was reported 262 times in a single run, two unbuffered stderr lines
    // each, on the guest's synchronous read path, while the player was trying to
    // respond to a touch. The window is kept instead, so the registration path
    // recognises the blob and leaves it alone.
    bool reported = false;
    // Boot-relative ms of the last read or seek that touched this blob, and the
    // per-blob read/seek line budgets. The budgets are per window rather than
    // per process: a process-global budget is spent by the first few clips of a
    // bank and then every later clip loads unobserved, which is how a whole
    // burst of failing clips left no trace in the log.
    uint64_t last_touch = 0;
    int reads_logged = 0;
    int seeks_logged = 0;
};
// A whole FSB5 header: magic(4), version u32@4, numSamples u32@8,
// sampleHeadersSize u32@12, nameTableSize u32@16, dataSize u32@20, mode u32@24,
// zero u64@28, hash 16B@36, dummy u64@52. Blob size = 60 + shs + nts + dataSize.
// A blob the guest never reads to the end is a clip it could not play, and the
// walk is the only place a late blob's length is learned.
struct FsbHeader {
    uint32_t version = 0, numSamples = 0, shs = 0, nts = 0, dataSize = 0, mode = 0;
    uint64_t total = 0;  // 0 = not enough bytes to parse
};
inline uint32_t fsb_rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline FsbHeader fsb_parse_header(const uint8_t* p) {
    FsbHeader h;
    if (std::memcmp(p, "FSB5", 4) != 0) return h;
    h.version = fsb_rd32(p + 4);
    h.numSamples = fsb_rd32(p + 8);
    h.shs = fsb_rd32(p + 12);
    h.nts = fsb_rd32(p + 16);
    h.dataSize = fsb_rd32(p + 20);
    h.mode = fsb_rd32(p + 24);
    h.total = 60ull + h.shs + h.nts + h.dataSize;
    return h;
}
// Largest plausible blob, so a run of header-shaped noise in arbitrary data cannot
// turn one read into an unbounded walk.
constexpr uint64_t kFsbBlobMax = 32ull << 20;

// Whether this FSB5 header shape has not been described yet. Shape is what
// decides whether a blob can be decoded at all, so describing one blob per shape
// describes every blob that shares it. A flat cap of the first 24 blobs instead
// spent the whole budget on arrival order: a run with 455 blobs described only
// the 24 that came first, which left a bank that loads and a bank that fails
// impossible to compare. Fixed table, bounded output.
bool fsb_shape_is_new(const char* key) {
    static std::mutex mtx;
    static std::string seen[16];
    static size_t count = 0;
    std::lock_guard<std::mutex> lk(mtx);
    for (size_t i = 0; i < count; ++i) {
        if (seen[i] == key) return false;
    }
    if (count < 16) seen[count++] = key;
    return true;
}
std::map<std::string, std::vector<FsbWindow>> g_fsbWindows;  // g_freadVolMtx

// What the guest actually asked for versus what reached it, for one archive.
// "complete" blobs had every declared byte served; "unread" did not.
struct FsbArchiveStats {
    int windows = 0;
    int complete = 0;
    int unread = 0;
    long long first_unread_off = -1;
};
FsbArchiveStats fsb_archive_stats_locked(const std::string& path);

// Boot-relative milliseconds, for window age. Same clock as ktraceLine's stamp.
uint64_t fsb_now_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// Mark [lo, hi) of a window's byte range as covered. Call with g_freadVolMtx
// held. Shared by the read path and by the serve path, which seeds the window
// with the read that revealed the header: a window created after its own first
// block was accounted can never reach its declared size, so a blob read end to
// end still verdicts SHORT — the exact mis-read that made every audio run look
// like "the blob was never read".
void fsb_window_cover(FsbWindow& w, long lo, long hi) {
    if (hi <= lo) return;
    if (lo < w.start) lo = w.start;
    if (hi > w.end) hi = w.end;
    if (hi <= lo) return;
    w.last_touch = fsb_now_ms();
    const uint64_t blocks = (w.expected + kFsbBlock - 1) / kFsbBlock;
    if (blocks == 0) return;
    if (w.covered.empty()) {
        w.covered.assign(static_cast<size_t>(blocks / 64 + 1), 0);
    }
    for (long b = (lo - w.start) / kFsbBlock; b <= (hi - 1 - w.start) / kFsbBlock; ++b) {
        const uint64_t ub = static_cast<uint64_t>(b);
        if (ub >= blocks) break;
        uint64_t& word = w.covered[static_cast<size_t>(ub) >> 6];
        const uint64_t bit = 1ull << (static_cast<unsigned>(ub) & 63);
        if (!(word & bit)) {
            word |= bit;
            ++w.blocks;
        }
    }
}

// Register a blob at `start` unless it is already tracked. `hdr` is its 60-byte
// header. `bodyLo`/`bodyHi`, when given, is a span of its BODY the current read
// carried -- the header itself is never part of that span, or a header-only read
// would count as reading a clip's audio. Call with g_freadVolMtx held.
void fsb_window_register(std::vector<FsbWindow>& list, long start, const FsbHeader& h,
                         const uint8_t* hdr, long bodyLo, long bodyHi) {
    for (auto& existing : list) {
        if (existing.start == start) {
            fsb_window_cover(existing, start, start + 60);
            return;
        }
    }
    FsbWindow w;
    w.start = start;
    w.end = start + static_cast<long>(h.total);
    w.expected = h.total;
    std::memcpy(w.hdr, hdr, sizeof(w.hdr));
    fsb_window_cover(w, start, start + 60);
    if (bodyHi > bodyLo) fsb_window_cover(w, bodyLo, bodyHi);
    list.push_back(w);
}

// The blob a read or seek over [lo, hi) belongs to. Windows are sorted by start, so
// the search is a binary search on the last header at or before `lo`, and the
// header bytes decide the two neighbours: a packed bank places the next blob's
// header inside the previous blob's last block, and a range test alone charged
// those header bytes to the previous blob. Call with g_freadVolMtx held.
FsbWindow* fsb_window_at(std::vector<FsbWindow>& list, long lo, long hi) {
    if (list.empty()) return nullptr;
    std::size_t lo_i = 0, hi_i = list.size();
    while (lo_i < hi_i) {
        const std::size_t mid = lo_i + (hi_i - lo_i) / 2;
        if (list[mid].start <= lo) lo_i = mid + 1; else hi_i = mid;
    }
    if (lo_i == 0) {
        // The range begins before the first blob: charged to the first one when it
        // reaches into it (a read of the whole resource from offset 0).
        return hi > list[0].start ? &list[0] : nullptr;
    }
    FsbWindow& cand = list[lo_i - 1];
    const long candEnd = cand.start + 60;
    if (hi > cand.start && lo < candEnd) return &cand;
    // Bytes past this blob's header belong to it, unless the NEXT blob's header
    // starts inside the range: a packed bank places it in the previous blob's last
    // block, and those header bytes are not the previous clip's audio.
    if (lo < cand.end) {
        if (lo_i >= list.size()) return &cand;
        const FsbWindow& next = list[lo_i];
        if (next.start >= hi) return &cand;
        return next.start >= candEnd ? &list[lo_i] : &cand;
    }
    // The read began in the gap between two blobs: it is the next blob's header.
    if (lo_i < list.size() && hi > list[lo_i].start) return &list[lo_i];
    return nullptr;
}

int fsb_window_log_read(FsbWindow& w) {
    if (w.reads_logged >= 8) return 0;
    ++w.reads_logged;
    return 1;
}
int fsb_window_log_seek(FsbWindow& w) {
    if (w.seeks_logged >= 4) return 0;
    ++w.seeks_logged;
    return 1;
}

// Announce each window whose declared bytes are all accounted for -- once per
// window. Marked, not erased: erasing let every re-read of the same blob
// re-register it and announce it again, which turned a guest's clip-load
// retry loop into an unbounded log loop on its synchronous read path.
// Call with g_freadVolMtx held.
void fsb_windows_report_locked(const std::string& path) {
    const auto it = g_fsbWindows.find(path);
    if (it == g_fsbWindows.end()) return;
    auto& list = it->second;
    for (auto& w : list) {
        if (w.reported) continue;
        if (w.blocks * kFsbBlock >= w.expected) {
            KTRACE_HOT(
                         "[KuDroidFmod] blob off=%ld size=%llu covered=%llu COMPLETE\n",
                         w.start, static_cast<unsigned long long>(w.expected),
                         static_cast<unsigned long long>(w.expected));
            w.reported = true;
        }
    }
    // Bounded by SIZE, not by how many have been announced. A bank is a few
    // hundred clips and every one of them is a fact the per-blob lines and the
    // bank summary are asked about; pruning to a fixed number of announced
    // windows dropped the tail of a 300-clip bank and its reads became
    // unaccounted. Age retires them (fsb_windows_evict_locked), and this is the
    // backstop for an archive that never ages out.
    constexpr std::size_t kMaxWindows = 4096;
    if (list.size() > kMaxWindows) {
        // Oldest first: blobs are appended in read order and a bank is walked in
        // order, so the front of the list is the part a guest is done with.
        const std::size_t drop = list.size() - kMaxWindows;
        list.erase(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(drop));
    }
    if (list.empty()) g_fsbWindows.erase(it);
}

// Retire blobs whose bytes never arrived. The trigger is AGE, not a live-set
// count: a count retires the blob a guest is reading right now the moment a
// bank-sized clip set fills the list, and then reports that as a read problem
// when it is bookkeeping. Age cannot do that — a blob touched by the last read
// is live by definition. Only called from fclose and the FSB5 serve path, never
// from the read path the guest is blocked in.
// Call with g_freadVolMtx held.
void fsb_windows_evict_locked(const std::string& path, uint64_t stale_ms) {
    const auto it = g_fsbWindows.find(path);
    if (it == g_fsbWindows.end()) return;
    auto& list = it->second;
    const uint64_t now = fsb_now_ms();
    for (std::size_t i = 0; i < list.size(); ++i) {
        FsbWindow& w = list[i];
        if (!w.reported && now >= w.last_touch && now - w.last_touch > stale_ms) {
            const uint64_t covered = w.blocks * kFsbBlock;
            ktraceLine("[KuDroidFmod] blob off=%ld size=%llu covered=%llu SHORT "
                       "retired_after_idle=%llums\n",
                       w.start, static_cast<unsigned long long>(w.expected),
                       static_cast<unsigned long long>(
                           covered > w.expected ? w.expected : covered),
                       static_cast<unsigned long long>(now - w.last_touch));
            w.reported = true;
        }
    }
    fsb_windows_report_locked(path);
}

namespace {
// Merged under g_freadVolMtx. Copies the map for the top-5 sort OUTSIDE the
// lock: sorting under it stalled every reader each 5MB.
void fread_vol_report_locked() {
    using Entry = std::pair<std::string, std::pair<uint64_t, uint64_t>>;
    std::vector<Entry> top(g_freadVol.begin(), g_freadVol.end());
    std::string total = std::to_string(g_freadVolTotal.load(std::memory_order_relaxed)) + "B";
    {
        std::lock_guard<std::mutex> lock(g_freadVolMtx);
        top.assign(g_freadVol.begin(), g_freadVol.end());
        total = std::to_string(g_freadVolTotal.load(std::memory_order_relaxed)) + "B";
    }
    std::sort(top.begin(), top.end(),
              [](const Entry& a, const Entry& b) { return a.second.first > b.second.first; });
    std::string line = "fread total=" + total;
    for (size_t i = 0; i < top.size() && i < 5; ++i) {
        const std::string& p = top[i].first;
        line += " | " + (p.size() > 60 ? "..." + p.substr(p.size() - 57) : p) +
                "=" + std::to_string(top[i].second.first) + "B/" +
                std::to_string(top[i].second.second) + "ops";
    }
    ktraceLine("[KuDroidIO] %s\n", line.c_str());
}

struct VolBatch {
    FILE* stream = nullptr;
    std::string path;
    uint64_t epoch = 0;
    uint64_t bytes = 0;
    uint64_t ops = 0;
};
thread_local VolBatch t_volbatch;

void fread_vol_flush() {
    if (t_volbatch.bytes == 0 || t_volbatch.path.empty()) {
        t_volbatch.bytes = 0;
        t_volbatch.ops = 0;
        return;
    }
    bool report = false;
    {
        std::lock_guard<std::mutex> lock(g_freadVolMtx);
        auto& e = g_freadVol[t_volbatch.path];
        e.first += t_volbatch.bytes;
        e.second += t_volbatch.ops;
        const uint64_t total =
            g_freadVolTotal.fetch_add(t_volbatch.bytes, std::memory_order_relaxed) +
            t_volbatch.bytes;
        if (total >= g_freadVolNextLog) {
            g_freadVolNextLog += 5ULL * 1024 * 1024;
            report = true;
        }
    }
    t_volbatch.bytes = 0;
    t_volbatch.ops = 0;
    if (report) fread_vol_report_locked();
}
}  // namespace

