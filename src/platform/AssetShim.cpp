#include "kudroid/platform/AssetShim.h"
#include "kudroid/platform/ShimDefs.h"
#include "kudroid/platform/MemoryInfo.h"
#include "kudroid/DeviceProfile.h"
#include "kudroid/VFSPathRemapper.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

namespace kudroid {

// Defined in SyscallShim.cpp. Declared here rather than pulled in through a header because
// the asset shim otherwise has no reason to depend on the syscall layer.
extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// AAssetManager — reads assets files that have been extracted to disk by APKExtractor.
//
// The APK is extracted to <android_root>/data/app/<appName>/ with the directory tree
// remains the same, so assets are located at <appName>/assets/<path>. Game (Unity/Godot/SDL)
// call AAssetManager_open(manager, "bin/Data/...", mode) — path relative to
// assets folder. kudroid_set_assets_dir() is called before kudroid_run_apk
// Run the game to point at the right place.
// ─────────────────────────────────────────────────────────────────────────────

static std::string g_assetsDir;
static std::mutex g_assetsMtx;

extern "C" void kudroid_set_assets_dir(const char* dir) {
    if (!dir) return;
    std::lock_guard<std::mutex> lock(g_assetsMtx);
    g_assetsDir = dir;
}

// Valid until the next call; the caller must copy immediately.
extern "C" const char* kudroid_get_assets_dir(void) {
    std::lock_guard<std::mutex> lock(g_assetsMtx);
    return g_assetsDir.c_str();
}

static std::string current_assets_dir() {
    std::lock_guard<std::mutex> lock(g_assetsMtx);
    return g_assetsDir;
}

// Running totals for the buffers handed out, so the log says how much of the process
// footprint is asset data rather than leaving it to be inferred.
static std::mutex g_bufferStatsMtx;
static uint64_t g_bufferBytesLive = 0;
static uint64_t g_bufferBytesPeak = 0;
static uint64_t g_bufferCount = 0;

// Report an asset buffer against the process footprint.
//
// Minecraft ships 36005 assets totalling 574 MB uncompressed, and its largest single file
// is a 20.9 MB material. libminecraftpe.so is already ~330 MB of image, so a handful of
// those buffers is enough to reach the jetsam limit — and jetsam sends SIGKILL, which runs
// no handler and writes no crash log. Nothing in the log would say why the process
// vanished; this line is what makes that case identifiable rather than a guess.
static void trace_buffer(const char* what, const std::string& name, uint64_t bytes,
                         bool mapped) {
    uint64_t live = 0;
    uint64_t peak = 0;
    uint64_t count = 0;
    {
        std::lock_guard<std::mutex> lock(g_bufferStatsMtx);
        g_bufferBytesLive += bytes;
        if (g_bufferBytesLive > g_bufferBytesPeak) g_bufferBytesPeak = g_bufferBytesLive;
        ++g_bufferCount;
        live = g_bufferBytesLive;
        peak = g_bufferBytesPeak;
        count = g_bufferCount;
    }

    const SystemMemory mem = query_system_memory();
    char message[512];
    // headroom is os_proc_available_memory: what this process may still allocate before
    // being killed. It is NOT system-available memory, and on iOS it is the only figure
    // that predicts a jetsam kill.
    std::snprintf(message, sizeof(message),
                  "%s %s (%llu KB, %s) — buffers live %llu KB / peak %llu KB over %llu"
                  " assets; footprint %llu KB, headroom %llu KB",
                  what, name.c_str(),
                  static_cast<unsigned long long>(bytes / 1024),
                  mapped ? "mapped, clean" : "heap, dirty",
                  static_cast<unsigned long long>(live / 1024),
                  static_cast<unsigned long long>(peak / 1024),
                  static_cast<unsigned long long>(count),
                  static_cast<unsigned long long>(mem.process_resident_bytes / 1024),
                  static_cast<unsigned long long>(mem.process_available_bytes / 1024));
    trace_shim(message);
    // Mirrored to the Android log so it reaches kudroid_android_logs.txt: the shim trace
    // buffer is only dumped by the crash handler, and a SIGKILL never reaches it.
    kudroid_android_log_message(4, "AssetShim", message);
}

static void untrace_buffer(uint64_t bytes) {
    std::lock_guard<std::mutex> lock(g_bufferStatsMtx);
    if (g_bufferBytesLive >= bytes) g_bufferBytesLive -= bytes;
    else g_bufferBytesLive = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Read-only fd cache for archive-backed assets.
//
// A cold start opens the same archive for every asset it serves; without the cache that
// is one open/close syscall pair per asset. The fd is validated by (st_dev, st_ino) and
// dropped after a stale check fails, so replacing the archive invalidates it rather than
// serving bytes from the old file.
// ─────────────────────────────────────────────────────────────────────────────
struct CachedFd {
    int fd;
    dev_t dev;
    ino_t ino;
};
static std::mutex g_cachedFdMtx;
static std::unordered_map<std::string, CachedFd> g_cachedFds;

static bool stat_matches(int fd, dev_t dev, ino_t ino) {
    struct stat st {};
    return ::fstat(fd, &st) == 0 && st.st_dev == dev && st.st_ino == ino;
}

// Returns a read-only fd for `path`, cached across calls. The fd is owned by the cache
// and must not be closed or dup'd by callers seeking elsewhere.
static int cached_readonly_fd(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_cachedFdMtx);
    const auto it = g_cachedFds.find(path);
    if (it != g_cachedFds.end()) {
        if (stat_matches(it->second.fd, it->second.dev, it->second.ino)) {
            return it->second.fd;
        }
        ::close(it->second.fd);
        g_cachedFds.erase(it);
    }
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return -1;
    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        return -1;
    }
    g_cachedFds.emplace(path, CachedFd{fd, st.st_dev, st.st_ino});
    return fd;
}

// AAsset on a stream: a stdio FILE* whose reads are preads against the cached fd, so a
// position lives in the stream rather than in the shared file description. Needed because
// an fd handed out by openFileDescriptor must keep its own offset while the asset reads.
struct PreadCookie {
    int fd;
    off_t pos;
};

static off_t pread_stream_size(int fd) {
    struct stat st {};
    return ::fstat(fd, &st) == 0 ? st.st_size : 0;
}

#if defined(__APPLE__)
static int pread_readfn(void* cookie, char* buf, int len) {
    auto* s = static_cast<PreadCookie*>(cookie);
    if (len <= 0) return 0;
    const ssize_t n = ::pread(s->fd, buf, static_cast<size_t>(len), s->pos);
    if (n > 0) s->pos += static_cast<off_t>(n);
    return static_cast<int>(n);
}

static off_t pread_seekfn(void* cookie, off_t offset, int whence) {
    auto* s = static_cast<PreadCookie*>(cookie);
    off_t target = s->pos;
    if (whence == SEEK_SET) target = offset;
    else if (whence == SEEK_CUR) target = s->pos + offset;
    else if (whence == SEEK_END) target = pread_stream_size(s->fd) + offset;
    else {
        errno = EINVAL;
        return -1;
    }
    if (target < 0) {
        errno = EINVAL;
        return -1;
    }
    s->pos = target;
    return target;
}

static int pread_closefn(void* cookie) {
    delete static_cast<PreadCookie*>(cookie);
    return 0;
}
#else
static ssize_t pread_cookie_read(void* cookie, char* buf, size_t len) {
    auto* s = static_cast<PreadCookie*>(cookie);
    if (len == 0) return 0;
    const ssize_t n = ::pread(s->fd, buf, len, s->pos);
    if (n > 0) s->pos += static_cast<off_t>(n);
    return n;
}

static int pread_cookie_seek(void* cookie, off64_t* offsetp, int whence) {
    auto* s = static_cast<PreadCookie*>(cookie);
    off64_t target = s->pos;
    if (whence == SEEK_SET) target = *offsetp;
    else if (whence == SEEK_CUR) target = s->pos + *offsetp;
    else if (whence == SEEK_END) target = pread_stream_size(s->fd) + *offsetp;
    else {
        errno = EINVAL;
        return -1;
    }
    if (target < 0) {
        errno = EINVAL;
        return -1;
    }
    s->pos = target;
    *offsetp = target;
    return 0;
}

static int pread_cookie_close(void* cookie) {
    delete static_cast<PreadCookie*>(cookie);
    return 0;
}
#endif

// Open `path` as a read-only FILE* backed by pread on the cached fd. *outFd (may be
// null) receives the underlying fd so getBuffer can still mmap the archive slice —
// mmap takes an explicit offset and does not disturb the shared file description.
static FILE* open_pread_stream(const std::string& path, int* outFd) {
    const int fd = cached_readonly_fd(path);
    if (fd < 0) return nullptr;
    auto* cookie = new PreadCookie{fd, 0};
    FILE* f = nullptr;
#if defined(__APPLE__)
    f = ::funopen(cookie, pread_readfn, nullptr, pread_seekfn, pread_closefn);
#else
    static const cookie_io_functions_t kFns = {
        pread_cookie_read, nullptr, pread_cookie_seek, pread_cookie_close};
    f = ::fopencookie(cookie, "r", kFns);
#endif
    if (f == nullptr) {
        delete cookie;
        return nullptr;
    }
    if (outFd) *outFd = fd;
    return f;
}

// Opaque handles (bionic returns cursor without revealing content).
struct DummyAssetManager { int dummy; };
static DummyAssetManager g_manager;

// AAsset: opened as a FILE* for reads; getBuffer maps the file rather than copying it.
struct AAssetImpl {
    FILE* file;
    long length;
    long offset;
    long startOffset;  // base offset in file (for uncompressed APK assets)
    std::string name;
    std::string path;  // file the handle reads from (loose file, cache copy, or base.apk)
    void* buffer;      // cache for AAsset_getBuffer
    size_t bufferSize;
    void* mmapBase;    // base pointer for page-aligned mmap
    size_t mmapSize;   // total mapped size
    bool bufferMapped;
    // Valid only when `file` is a pread-backed stream over the fd cache; -1 for a
    // plain fopen handle (whose fileno is used directly).
    int streamFd = -1;
};

struct AAssetDirImpl {
    std::vector<std::string> names;  // filename (not recursive, like real AAssetDir)
    size_t cursor = 0;
};

// Resolve an AAssetManager path against the extracted assets directory.
//
// The path a game passes is relative to the APK's assets/ folder, so `bootstrap.json`
// means the zip entry `assets/bootstrap.json`. What makes this less obvious than it looks
// is that an APK may NEST a folder of the same name: Minecraft ships 36001 of its 36005
// asset entries under `assets/assets/`, so the file the game wants at
// `assets/assets/bootstrap.json` is requested as `assets/bootstrap.json`.
//
// This used to strip a leading "assets/" unconditionally, with a comment calling it a path
// the game "transmits incorrectly". It is not incorrect — it is a real directory inside
// assets/, and removing it turned the one path that resolves into the one that does not:
// `assets/bootstrap.json` became `bootstrap.json`, which exists nowhere.
//
// So the literal path is tried FIRST, and the strip is only a fallback for a game that
// really does pass the zip entry name. Both APK shapes then work, and a nested assets/
// folder is no longer shadowed by the guess.
static bool resolve_asset_path(const std::string& base, const char* filename,
                               std::string* relOut, std::filesystem::path* fullOut) {
    const std::string requested = filename;
    std::error_code ec;

    const auto literal = std::filesystem::path(base) / requested;
    if (std::filesystem::exists(literal, ec) &&
        !std::filesystem::is_directory(literal, ec)) {
        *relOut = requested;
        *fullOut = literal;
        return true;
    }

    // Fallback: the caller passed the zip entry name rather than an assets-relative path.
    if (requested.rfind("assets/", 0) == 0) {
        const std::string stripped = requested.substr(7);
        const auto alternate = std::filesystem::path(base) / stripped;
        if (std::filesystem::exists(alternate, ec) &&
            !std::filesystem::is_directory(alternate, ec)) {
            *relOut = stripped;
            *fullOut = alternate;
            return true;
        }
    }
    return false;
}

static AAssetImpl* open_asset(const char* filename) {
    if (!filename || !*filename) return nullptr;
    const std::string base = current_assets_dir();
    if (base.empty()) {
        trace_shim("AAssetManager_open: assets dir not set (kudroid_set_assets_dir)");
        return nullptr;
    }

    std::string rel;
    std::filesystem::path full;
    long startOffset = 0;
    long entryLength = 0;

    if (!resolve_asset_path(base, filename, &rel, &full)) {
        // Fallback: Check if base.apk exists in parent directory of assets dir
        const auto appDir = std::filesystem::path(base).parent_path();
        const auto baseApk = appDir / "base.apk";
        std::error_code ec;
        if (std::filesystem::exists(baseApk, ec)) {
            std::string entryName = filename;
            if (entryName.rfind("assets/", 0) != 0) {
                entryName = "assets/" + entryName;
            }
            uint64_t payloadOff = 0, payloadSize = 0;
            uint16_t method = 0;
            if (zip_stat_entry(baseApk.string(), entryName, &payloadOff, &payloadSize, &method)) {
                if (method == 0) {
                    rel = filename;
                    full = baseApk;
                    startOffset = static_cast<long>(payloadOff);
                    entryLength = static_cast<long>(payloadSize);
                } else {
                    const auto& remapper = VFSPathRemapper::getInstance();
                    const std::string cached = extract_jar_entry_to_cache(baseApk.string(), entryName, remapper.androidRoot());
                    if (!cached.empty()) {
                        rel = filename;
                        full = cached;
                    }
                }
            }
        }
    }

    if (full.empty() || !std::filesystem::exists(full)) {
        // Name the path that was looked for, not just the fact of failure.
        std::string message = "AAssetManager_open: not found '";
        message += filename;
        message += "' under ";
        message += base;
        trace_shim(message.c_str());
        static std::atomic<int> s_missLogged{0};
        if (s_missLogged.load() < 15) {
            ++s_missLogged;
            kudroid_android_log_message(4, "AssetShim", message.c_str());
        }
        return nullptr;
    }

    FILE* f = nullptr;
    int streamFd = -1;
    if (startOffset > 0) {
        // Archive-backed slice: pread on a cached fd, no per-asset open/close.
        f = open_pread_stream(full.string(), &streamFd);
    } else {
        f = std::fopen(full.string().c_str(), "rb");
    }
    if (!f) return nullptr;

    long len = entryLength;
    if (len <= 0) {
        std::fseek(f, 0, SEEK_END);
        len = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
    } else {
        std::fseek(f, startOffset, SEEK_SET);
    }

    auto* asset = new AAssetImpl();
    asset->file = f;
    asset->length = len;
    asset->offset = 0;
    asset->startOffset = startOffset;
    asset->name = rel;
    asset->path = full.string();
    asset->buffer = nullptr;
    asset->bufferSize = 0;
    asset->mmapBase = nullptr;
    asset->mmapSize = 0;
    asset->bufferMapped = false;
    asset->streamFd = streamFd;
    // Addressables-style manifest loads are rare; showing them proves the route.
    if (rel.find(".json") != std::string::npos || rel.find("aa/") != std::string::npos) {
        static std::atomic<int> s_hitLogged{0};
        if (s_hitLogged.load() < 10) {
            ++s_hitLogged;
            char hit[512];
            std::snprintf(hit, sizeof(hit), "AAssetManager_open: '%s' len=%ld",
                          rel.c_str(), len);
            kudroid_android_log_message(4, "AssetShim", hit);
        }
    }
    return asset;
}

extern "C" void* bionic_AAssetManager_fromJava(void* /*env*/, void* /*assetManager*/) {
    return &g_manager;
}

extern "C" void* bionic_AAssetManager_open(void* /*manager*/, const char* filename,
                                           int /*mode*/) {
    return open_asset(filename);
}

// Defined below; openFd only needs the fd, not the asset object.
static int open_independent_fd(const AAssetImpl* a);
extern "C" void bionic_AAsset_close(void* asset);

extern "C" int bionic_AAssetManager_openFd(void* /*manager*/, const char* filename,
                                            void* outStart, void* outLength) {
    // NDK ABI: returns an fd positioned at the entry payload, or -1. AAssetImpl
    // stays internal — the guest only ever sees the integer fd.
    auto* asset = open_asset(filename);
    if (!asset) {
        trace_shim("AAssetManager_openFd: miss");
        return -1;
    }
    const off_t start = static_cast<off_t>(asset->startOffset);
    const off_t length = static_cast<off_t>(asset->length);
    const int fd = open_independent_fd(asset);
    bionic_AAsset_close(asset);
    if (fd < 0) return -1;
    ::lseek(fd, start, SEEK_SET);
    if (outStart) *static_cast<off_t*>(outStart) = start;
    if (outLength) *static_cast<off_t*>(outLength) = length;
    {
        static std::atomic<int> s_fdLogged{0};
        if (s_fdLogged.load(std::memory_order_relaxed) < 12) {
            s_fdLogged.fetch_add(1, std::memory_order_relaxed);
            char line[512];
            std::snprintf(line, sizeof(line), "AAssetManager_openFd: '%s' fd=%d start=%ld len=%ld",
                          filename != nullptr ? filename : "?", fd,
                          static_cast<long>(start), static_cast<long>(length));
            trace_shim(line);
        }
    }
    return fd;
}

// dup(fileno) shares the open file description with the asset's FILE*, so an fd read
// moves the position a later AAsset_read resumes from (and vice versa). A fresh fd on
// the backing file has an independent offset; for an entry that only exists inside
// base.apk that file IS base.apk, with the payload at startOffset.
static int open_independent_fd(const AAssetImpl* a) {
    if (!a->path.empty()) {
        const int fd = ::open(a->path.c_str(), O_RDONLY);
        if (fd >= 0) return fd;
    }
    return a->file ? ::dup(::fileno(a->file)) : -1;
}

extern "C" int bionic_AAsset_openFileDescriptor(void* asset, void* outStart, void* outLength) {
    auto* a = static_cast<AAssetImpl*>(asset);
    if (!a || !a->file) return -1;
    const int fd = open_independent_fd(a);
    if (fd < 0) return -1;
    ::lseek(fd, static_cast<off_t>(a->startOffset), SEEK_SET);
    if (outStart) *static_cast<off_t*>(outStart) = static_cast<off_t>(a->startOffset);
    if (outLength) *static_cast<off_t*>(outLength) = static_cast<off_t>(a->length);
    return fd;
}

extern "C" int bionic_AAsset_openFileDescriptor64(void* asset, void* outStart, void* outLength) {
    auto* a = static_cast<AAssetImpl*>(asset);
    if (!a || !a->file) return -1;
    const int fd = open_independent_fd(a);
    if (fd < 0) return -1;
    ::lseek(fd, static_cast<off_t>(a->startOffset), SEEK_SET);
    if (outStart) *static_cast<int64_t*>(outStart) = static_cast<int64_t>(a->startOffset);
    if (outLength) *static_cast<int64_t*>(outLength) = static_cast<int64_t>(a->length);
    return fd;
}

extern "C" int bionic_AAsset_isAllocated(void* asset) {
    auto* a = static_cast<AAssetImpl*>(asset);
    return (a && a->buffer) ? 1 : 0;
}

extern "C" void* bionic_AAssetManager_openDir(void* /*manager*/, const char* dirName) {
    auto* dir = new AAssetDirImpl();
    const std::string base = current_assets_dir();
    if (base.empty()) return dir;

    std::error_code ec;
    const std::string requested = dirName ? dirName : "";
    auto full = requested.empty() ? std::filesystem::path(base)
                                  : std::filesystem::path(base) / requested;
    if (!std::filesystem::is_directory(full, ec) && requested.rfind("assets/", 0) == 0) {
        full = std::filesystem::path(base) / requested.substr(7);
    }
    if (std::filesystem::is_directory(full, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(full, ec)) {
            if (entry.is_regular_file(ec)) {
                dir->names.push_back(entry.path().filename().string());
            }
        }
        return dir;
    }

    // Fallback: enumerate entries directly from base.apk when assets/ is not on disk
    const auto appDir = std::filesystem::path(base).parent_path();
    const auto baseApk = appDir / "base.apk";
    if (std::filesystem::exists(baseApk, ec)) {
        std::string prefix = requested;
        if (prefix.rfind("assets/", 0) != 0) {
            prefix = "assets/" + prefix;
        }
        dir->names = zip_list_dir_entries(baseApk.string(), prefix);
    }
    return dir;
}

extern "C" const char* bionic_AAssetDir_getNextFileName(void* dir) {
    auto* d = static_cast<AAssetDirImpl*>(dir);
    if (!d || d->cursor >= d->names.size()) return nullptr;
    return d->names[d->cursor++].c_str();
}

extern "C" void bionic_AAssetDir_rewind(void* dir) {
    auto* d = static_cast<AAssetDirImpl*>(dir);
    if (d) d->cursor = 0;
}

extern "C" void bionic_AAssetDir_close(void* dir) {
    delete static_cast<AAssetDirImpl*>(dir);
}

extern "C" const char* bionic_AAsset_getFileName(void* asset) {
    auto* a = static_cast<AAssetImpl*>(asset);
    return a ? a->name.c_str() : nullptr;
}

extern "C" long bionic_AAsset_getLength(void* asset) {
    auto* a = static_cast<AAssetImpl*>(asset);
    return a ? a->length : 0;
}

extern "C" int64_t bionic_AAsset_getLength64(void* asset) {
    auto* a = static_cast<AAssetImpl*>(asset);
    return a ? static_cast<int64_t>(a->length) : 0;
}

extern "C" long bionic_AAsset_getRemainingLength(void* asset) {
    auto* a = static_cast<AAssetImpl*>(asset);
    return a ? (a->length - a->offset) : 0;
}

extern "C" int64_t bionic_AAsset_getRemainingLength64(void* asset) {
    auto* a = static_cast<AAssetImpl*>(asset);
    return a ? static_cast<int64_t>(a->length - a->offset) : 0;
}

extern "C" int bionic_AAsset_read(void* asset, void* buf, size_t count) {
    auto* a = static_cast<AAssetImpl*>(asset);
    if (!a || !buf || !a->file) return -1;
    if (a->offset >= a->length) return 0;
    const size_t to_read = std::min<size_t>(count, static_cast<size_t>(a->length - a->offset));
    if (to_read == 0) return 0;
    // Archive-backed: pread the cached fd so the shared stream position is untouched
    // (the fd handed out by openFileDescriptor must keep its own offset).
    const off_t filePos = static_cast<off_t>(a->startOffset) + static_cast<off_t>(a->offset);
    ssize_t n;
    if (a->streamFd >= 0) {
        n = ::pread(a->streamFd, buf, to_read, filePos);
    } else {
        ::fseeko(a->file, filePos, SEEK_SET);
        n = std::fread(buf, 1, to_read, a->file);
    }
    if (n < 0) return -1;
    a->offset += static_cast<long>(n);
    return static_cast<int>(n);
}

extern "C" int bionic_AAsset_seek(void* asset, long offset, int whence) {
    auto* a = static_cast<AAssetImpl*>(asset);
    if (!a) return -1;
    long target = 0;
    if (whence == SEEK_SET) target = offset;
    else if (whence == SEEK_CUR) target = a->offset + offset;
    else if (whence == SEEK_END) target = a->length + offset;
    else return -1;
    if (target < 0) return -1;
    a->offset = target;
    // Archive-backed with a pread stream: the FILE* holds the archive position
    // with no asset framing, so only the loose-file case seeks here.
    if (a->startOffset > 0 && a->streamFd < 0) {
        std::fseek(a->file, a->startOffset + a->offset, SEEK_SET);
    } else if (a->startOffset <= 0) {
        std::fseek(a->file, a->offset, SEEK_SET);
    }
    return static_cast<int>(a->offset);
}

extern "C" int64_t bionic_AAsset_seek64(void* asset, int64_t offset, int whence) {
    auto* a = static_cast<AAssetImpl*>(asset);
    if (!a) return -1;
    int64_t target = 0;
    if (whence == SEEK_SET) target = offset;
    else if (whence == SEEK_CUR) target = static_cast<int64_t>(a->offset) + offset;
    else if (whence == SEEK_END) target = static_cast<int64_t>(a->length) + offset;
    else return -1;
    if (target < 0) return -1;
    a->offset = static_cast<long>(target);
    // Archive-backed with a pread stream: the FILE* holds the archive position
    // with no asset framing, so only the loose-file case seeks here.
    if (a->startOffset > 0 && a->streamFd < 0) {
        ::fseeko(a->file, static_cast<off_t>(a->startOffset + a->offset), SEEK_SET);
    } else if (a->startOffset <= 0) {
        ::fseeko(a->file, static_cast<off_t>(a->offset), SEEK_SET);
    }
    return static_cast<int64_t>(a->offset);
}

extern "C" const void* bionic_AAsset_getBuffer(void* asset) {
    auto* a = static_cast<AAssetImpl*>(asset);
    if (!a) return nullptr;
    if (a->buffer) return a->buffer;
    if (a->length <= 0) return nullptr;

    const int fd = a->streamFd >= 0 ? a->streamFd : ::fileno(a->file);
    if (fd >= 0) {
        const long pageSize = ::sysconf(_SC_PAGE_SIZE);
        const off_t pageOffset = (static_cast<off_t>(a->startOffset) / pageSize) * pageSize;
        const off_t pageDiff = static_cast<off_t>(a->startOffset) - pageOffset;
        const size_t mapSize = static_cast<size_t>(a->length + pageDiff);

        void* mapped = ::mmap(nullptr, mapSize, PROT_READ, MAP_PRIVATE, fd, pageOffset);
        if (mapped != MAP_FAILED) {
            a->mmapBase = mapped;
            a->mmapSize = mapSize;
            a->buffer = static_cast<char*>(mapped) + pageDiff;
            a->bufferSize = static_cast<size_t>(a->length);
            a->bufferMapped = true;
            a->offset = a->length;
            trace_buffer("AAsset_getBuffer mapped", a->name,
                         static_cast<uint64_t>(a->length), /*mapped=*/true);
            return a->buffer;
        }
    }

    void* buf = std::malloc(static_cast<size_t>(a->length));
    if (!buf) return nullptr;
    std::fseek(a->file, a->startOffset, SEEK_SET);
    const size_t got = std::fread(buf, 1, static_cast<size_t>(a->length), a->file);
    if (got != static_cast<size_t>(a->length)) {
        std::free(buf);
        return nullptr;
    }
    a->buffer = buf;
    a->bufferSize = got;
    a->bufferMapped = false;
    a->offset = static_cast<long>(got);
    trace_buffer("AAsset_getBuffer copied", a->name, static_cast<uint64_t>(got),
                 /*mapped=*/false);
    return buf;
}

// Resolve an asset path the way open_asset() does and hand the C++ side the raw bytes:
// loose file under the assets dir, or the archive entry — extracted to cache when it is
// stored deflated, mmap-backed read from base.apk when stored uncompressed. Backs the
// Java-side AssetManager.openFd, whose caller (ParcelFileDescriptor) needs a real file.
// Returns 0 and fills nothing when the asset does not exist; -1 on extraction failure.
extern "C" int kudroid_asset_resolve_bytes(const char* filename, char** outPath,
                                            int64_t* outStart, int64_t* outLength) {
    if (outPath) *outPath = nullptr;
    if (outStart) *outStart = 0;
    if (outLength) *outLength = 0;
    auto* asset = open_asset(filename);
    if (!asset) return 0;
    const std::string path = asset->path;
    const long start = asset->startOffset;
    const long length = asset->length;
    // Manual close (no buffers were created yet), so this helper can sit above
    // bionic_AAsset_close without a forward declaration.
    if (asset->file) std::fclose(asset->file);
    delete asset;
    if (length <= 0) return -1;
    char* copy = static_cast<char*>(std::malloc(path.size() + 1));
    if (!copy) return -1;
    std::memcpy(copy, path.c_str(), path.size() + 1);
    if (outPath) *outPath = copy;
    if (outStart) *outStart = static_cast<int64_t>(start);
    if (outLength) *outLength = static_cast<int64_t>(length);
    return 1;
}

extern "C" void bionic_AAsset_close(void* asset) {
    auto* a = static_cast<AAssetImpl*>(asset);
    if (!a) return;
    if (a->buffer) {
        if (a->bufferMapped && a->mmapBase) {
            ::munmap(a->mmapBase, a->mmapSize);
        } else if (!a->bufferMapped) {
            std::free(a->buffer);
        }
        untrace_buffer(static_cast<uint64_t>(a->bufferSize));
    }
    if (a->file) std::fclose(a->file);
    delete a;
}

// ─────────────────────────────────────────────────────────────────────────────
// AConfiguration — device configuration (locale, screen size/density,
// orientation). The game reads to select assets and layout. Returns the actual number from CAMetalLayer
// and iOS locale instead of 0, because density=0 causes the game to divide by 0 or select incorrectly
// mip level.
// ─────────────────────────────────────────────────────────────────────────────

extern "C" int g_metalLayerWidth;
extern "C" int g_metalLayerHeight;
extern "C" float g_metalLayerDensity;

struct AConfigurationImpl {
    char language[3];   // ISO 639-1, not null terminated according to Android API
    char country[3];    // ISO 3166-1 alpha-2
    int32_t density;
    int32_t orientation;
    int32_t screenWidthDp;
    int32_t screenHeightDp;
    int32_t sdkVersion;
};

static void fill_current_config(AConfigurationImpl* c) {
    if (!c) return;
    // AConfiguration_getLanguage/Country returns pointer to 2 char not null-term.
    std::memcpy(c->language, "en", 2);
    std::memcpy(c->country, "US", 2);
    c->language[2] = '\0';
    c->country[2] = '\0';
    const char* lang = ::getenv("LANG");
    if (lang && std::strlen(lang) >= 5 && lang[2] == '_') {
        c->language[0] = lang[0]; c->language[1] = lang[1];
        c->country[0] = lang[3];  c->country[1] = lang[4];
    }

    const int w = g_metalLayerWidth > 0 ? g_metalLayerWidth : 1080;
    const int h = g_metalLayerHeight > 0 ? g_metalLayerHeight : 1920;
    const float scale = g_metalLayerDensity > 0.0f ? g_metalLayerDensity : 2.0f;
    c->density = static_cast<int32_t>(scale * 160.0f);       // ACONFIGURATION_DENSITY_*
    c->screenWidthDp = static_cast<int32_t>(w / scale);
    c->screenHeightDp = static_cast<int32_t>(h / scale);
    c->orientation = (w > h) ? 2 : 1;                        // LAND : PORT
    c->sdkVersion = KUDROID_SDK_INT;
}

extern "C" void* bionic_AConfiguration_new(void) {
    auto* c = new AConfigurationImpl();
    fill_current_config(c);
    return c;
}

extern "C" void bionic_AConfiguration_delete(void* config) {
    delete static_cast<AConfigurationImpl*>(config);
}

extern "C" void bionic_AConfiguration_fromAssetManager(void* config, void* /*am*/) {
    fill_current_config(static_cast<AConfigurationImpl*>(config));
}

extern "C" void bionic_AConfiguration_getLanguage(void* config, char* outLanguage) {
    auto* c = static_cast<AConfigurationImpl*>(config);
    if (!outLanguage) return;
    outLanguage[0] = c ? c->language[0] : 'e';
    outLanguage[1] = c ? c->language[1] : 'n';
}

extern "C" void bionic_AConfiguration_getCountry(void* config, char* outCountry) {
    auto* c = static_cast<AConfigurationImpl*>(config);
    if (!outCountry) return;
    outCountry[0] = c ? c->country[0] : 'U';
    outCountry[1] = c ? c->country[1] : 'S';
}

extern "C" int32_t bionic_AConfiguration_getDensity(void* config) {
    auto* c = static_cast<AConfigurationImpl*>(config);
    return c ? c->density : 320;
}

extern "C" int32_t bionic_AConfiguration_getOrientation(void* config) {
    auto* c = static_cast<AConfigurationImpl*>(config);
    return c ? c->orientation : 1;
}

extern "C" int32_t bionic_AConfiguration_getScreenWidthDp(void* config) {
    auto* c = static_cast<AConfigurationImpl*>(config);
    return c ? c->screenWidthDp : 411;
}

extern "C" int32_t bionic_AConfiguration_getScreenHeightDp(void* config) {
    auto* c = static_cast<AConfigurationImpl*>(config);
    return c ? c->screenHeightDp : 731;
}

extern "C" int32_t bionic_AConfiguration_getSdkVersion(void* config) {
    auto* c = static_cast<AConfigurationImpl*>(config);
    return c ? c->sdkVersion : KUDROID_SDK_INT;
}

extern "C" void bionic_AConfiguration_copy(void* dest, void* src) {
    auto* d = static_cast<AConfigurationImpl*>(dest);
    auto* s = static_cast<AConfigurationImpl*>(src);
    if (d && s) *d = *s;
}

extern "C" int32_t bionic_AConfiguration_diff(void* a, void* b) {
    auto* x = static_cast<AConfigurationImpl*>(a);
    auto* y = static_cast<AConfigurationImpl*>(b);
    if (!x || !y) return 0;
    int32_t diff = 0;
    if (std::memcmp(x->language, y->language, 2) != 0) diff |= 0x0004; // LOCALE
    if (x->orientation != y->orientation) diff |= 0x0080;              // ORIENTATION
    if (x->density != y->density) diff |= 0x0100;                      // DENSITY
    if (x->screenWidthDp != y->screenWidthDp ||
        x->screenHeightDp != y->screenHeightDp) diff |= 0x0400;        // SCREEN_SIZE
    return diff;
}

// ─────────────────────────────────────────────────────────────────────────────
// APerformanceHint — API 31 hint manager. iOS has no equivalent QoS API in
// this level of granularity, but the session must be a REAL handle: the game keeps it, calls
// report/update then close. Returning dummy 0 makes the game deref null or considered an error.
// ─────────────────────────────────────────────────────────────────────────────

struct APerfManagerImpl { int64_t preferredRateNanos; };
struct APerfSessionImpl { int64_t targetWorkDurationNanos; };

static APerfManagerImpl g_perfManager{16666666}; // 60 Hz

extern "C" void* bionic_APerformanceHint_getManager(void) {
    return &g_perfManager;
}

extern "C" void* bionic_APerformanceHint_createSession(void* /*manager*/,
                                                       const int32_t* /*threadIds*/,
                                                       size_t /*size*/,
                                                       int64_t initialTargetWorkDurationNanos) {
    auto* s = new APerfSessionImpl();
    s->targetWorkDurationNanos = initialTargetWorkDurationNanos;
    return s;
}

extern "C" int64_t bionic_APerformanceHint_getPreferredUpdateRateNanos(void* /*manager*/) {
    return g_perfManager.preferredRateNanos;
}

extern "C" int bionic_APerformanceHint_updateTargetWorkDuration(void* session,
                                                                int64_t targetDurationNanos) {
    auto* s = static_cast<APerfSessionImpl*>(session);
    if (!s || targetDurationNanos <= 0) return -EINVAL;
    s->targetWorkDurationNanos = targetDurationNanos;
    return 0;
}

extern "C" int bionic_APerformanceHint_reportActualWorkDuration(void* session,
                                                                int64_t actualDurationNanos) {
    if (!session || actualDurationNanos <= 0) return -EINVAL;
    return 0; // There is no scheduler hint on iOS — acknowledge and return OK
}

extern "C" void bionic_APerformanceHint_closeSession(void* session) {
    delete static_cast<APerfSessionImpl*>(session);
}

const SymbolEntry kAssetSymbols[] = {
    {"AAssetManager_fromJava", reinterpret_cast<void*>(&bionic_AAssetManager_fromJava)},
    {"AAssetManager_open", reinterpret_cast<void*>(&bionic_AAssetManager_open)},
    {"AAssetManager_openFd", reinterpret_cast<void*>(&bionic_AAssetManager_openFd)},
    {"AAssetManager_openDir", reinterpret_cast<void*>(&bionic_AAssetManager_openDir)},
    {"AAssetDir_getNextFileName", reinterpret_cast<void*>(&bionic_AAssetDir_getNextFileName)},
    {"AAssetDir_rewind", reinterpret_cast<void*>(&bionic_AAssetDir_rewind)},
    {"AAssetDir_close", reinterpret_cast<void*>(&bionic_AAssetDir_close)},
    {"AAsset_getFileName", reinterpret_cast<void*>(&bionic_AAsset_getFileName)},
    {"AAsset_getLength", reinterpret_cast<void*>(&bionic_AAsset_getLength)},
    {"AAsset_getLength64", reinterpret_cast<void*>(&bionic_AAsset_getLength64)},
    {"AAsset_getRemainingLength", reinterpret_cast<void*>(&bionic_AAsset_getRemainingLength)},
    {"AAsset_getRemainingLength64", reinterpret_cast<void*>(&bionic_AAsset_getRemainingLength64)},
    {"AAsset_read", reinterpret_cast<void*>(&bionic_AAsset_read)},
    {"AAsset_seek", reinterpret_cast<void*>(&bionic_AAsset_seek)},
    {"AAsset_seek64", reinterpret_cast<void*>(&bionic_AAsset_seek64)},
    {"AAsset_getBuffer", reinterpret_cast<void*>(&bionic_AAsset_getBuffer)},
    {"AAsset_close", reinterpret_cast<void*>(&bionic_AAsset_close)},
    {"AAsset_openFileDescriptor", reinterpret_cast<void*>(&bionic_AAsset_openFileDescriptor)},
    {"AAsset_openFileDescriptor64", reinterpret_cast<void*>(&bionic_AAsset_openFileDescriptor64)},
    {"AAsset_isAllocated", reinterpret_cast<void*>(&bionic_AAsset_isAllocated)},

    // AConfiguration
    {"AConfiguration_new", reinterpret_cast<void*>(&bionic_AConfiguration_new)},
    {"AConfiguration_delete", reinterpret_cast<void*>(&bionic_AConfiguration_delete)},
    {"AConfiguration_fromAssetManager", reinterpret_cast<void*>(&bionic_AConfiguration_fromAssetManager)},
    {"AConfiguration_getLanguage", reinterpret_cast<void*>(&bionic_AConfiguration_getLanguage)},
    {"AConfiguration_getCountry", reinterpret_cast<void*>(&bionic_AConfiguration_getCountry)},
    {"AConfiguration_getDensity", reinterpret_cast<void*>(&bionic_AConfiguration_getDensity)},
    {"AConfiguration_getOrientation", reinterpret_cast<void*>(&bionic_AConfiguration_getOrientation)},
    {"AConfiguration_getScreenWidthDp", reinterpret_cast<void*>(&bionic_AConfiguration_getScreenWidthDp)},
    {"AConfiguration_getScreenHeightDp", reinterpret_cast<void*>(&bionic_AConfiguration_getScreenHeightDp)},
    {"AConfiguration_getSdkVersion", reinterpret_cast<void*>(&bionic_AConfiguration_getSdkVersion)},
    {"AConfiguration_copy", reinterpret_cast<void*>(&bionic_AConfiguration_copy)},
    {"AConfiguration_diff", reinterpret_cast<void*>(&bionic_AConfiguration_diff)},

    // APerformanceHint
    {"APerformanceHint_getManager", reinterpret_cast<void*>(&bionic_APerformanceHint_getManager)},
    {"APerformanceHint_createSession", reinterpret_cast<void*>(&bionic_APerformanceHint_createSession)},
    {"APerformanceHint_getPreferredUpdateRateNanos", reinterpret_cast<void*>(&bionic_APerformanceHint_getPreferredUpdateRateNanos)},
    {"APerformanceHint_updateTargetWorkDuration", reinterpret_cast<void*>(&bionic_APerformanceHint_updateTargetWorkDuration)},
    {"APerformanceHint_reportActualWorkDuration", reinterpret_cast<void*>(&bionic_APerformanceHint_reportActualWorkDuration)},
    {"APerformanceHint_closeSession", reinterpret_cast<void*>(&bionic_APerformanceHint_closeSession)},
};

} // namespace

const SymbolEntry* get_asset_symbols(size_t* count) {
    if (count) {
        *count = sizeof(kAssetSymbols) / sizeof(SymbolEntry);
    }
    return kAssetSymbols;
}

} // namespace kudroid
