#include "kudroid/platform/AssetShim.h"
#include "kudroid/platform/ShimDefs.h"
#include "kudroid/platform/MemoryInfo.h"
#include "kudroid/DeviceProfile.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <filesystem>
#include <unistd.h>
#include <sys/mman.h>

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

// Opaque handles (bionic returns cursor without revealing content).
struct DummyAssetManager { int dummy; };
static DummyAssetManager g_manager;

// AAsset: opened as a FILE* for reads; getBuffer maps the file rather than copying it.
struct AAssetImpl {
    FILE* file;
    long length;
    long offset;
    std::string name;
    void* buffer;      // cache for AAsset_getBuffer
    size_t bufferSize;
    // True when `buffer` came from mmap rather than malloc, so close() knows how to
    // release it. The two are not interchangeable: munmap on heap memory and free on a
    // mapping are both undefined.
    bool bufferMapped;
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
    if (!resolve_asset_path(base, filename, &rel, &full)) {
        // Name the path that was looked for, not just the fact of failure. The old message
        // said only "not found", and the game's own log line prints the name it asked for
        // — so when the two differ, as they did for every nested asset, nothing on either
        // side showed the path that was actually tried.
        std::string message = "AAssetManager_open: not found '";
        message += filename;
        message += "' under ";
        message += base;
        trace_shim(message.c_str());
        // Misses are otherwise invisible outside crashes; a wrong prefix here
        // stalls asset loads with no error on either side.
        static std::atomic<int> s_missLogged{0};
        if (s_missLogged.load() < 15) {
            ++s_missLogged;
            kudroid_android_log_message(4, "AssetShim", message.c_str());
        }
        return nullptr;
    }

    FILE* f = std::fopen(full.string().c_str(), "rb");
    if (!f) return nullptr;

    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    auto* asset = new AAssetImpl();
    asset->file = f;
    asset->length = len;
    asset->offset = 0;
    asset->name = rel;
    asset->buffer = nullptr;
    asset->bufferSize = 0;
    asset->bufferMapped = false;
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

extern "C" void* bionic_AAssetManager_openFd(void* /*manager*/, const char* filename,
                                             void* outStart, void* outLength) {
    // The asset is already on disk as a separate file (not packaged in an APK), so
    // fd points directly to the file and the offset is always 0 — in accordance with the API contract.
    auto* asset = open_asset(filename);
    if (!asset) return nullptr;
    if (outStart) *static_cast<off_t*>(outStart) = 0;
    if (outLength) *static_cast<off_t*>(outLength) = asset->length;
    return asset;
}

extern "C" int bionic_AAsset_openFileDescriptor(void* asset, void* outStart, void* outLength) {
    auto* a = static_cast<AAssetImpl*>(asset);
    if (!a || !a->file) return -1;
    const int fd = ::dup(::fileno(a->file));
    if (fd < 0) return -1;
    if (outStart) *static_cast<off_t*>(outStart) = 0;
    if (outLength) *static_cast<off_t*>(outLength) = a->length;
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

    // The same nesting rule as open_asset: the literal path first, the stripped form only
    // as a fallback. Stripping unconditionally listed the wrong directory for a nested
    // assets/ folder — and returned an EMPTY dir rather than an error, so a game
    // enumerating its assets simply found nothing and reported no failure.
    std::error_code ec;
    const std::string requested = dirName ? dirName : "";
    auto full = requested.empty() ? std::filesystem::path(base)
                                  : std::filesystem::path(base) / requested;
    if (!std::filesystem::is_directory(full, ec) && requested.rfind("assets/", 0) == 0) {
        full = std::filesystem::path(base) / requested.substr(7);
    }
    if (!std::filesystem::is_directory(full, ec)) return dir;
    for (const auto& entry : std::filesystem::directory_iterator(full, ec)) {
        if (entry.is_regular_file(ec)) {
            dir->names.push_back(entry.path().filename().string());
        }
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
    if (!a || !buf) return -1;
    const size_t n = std::fread(buf, 1, count, a->file);
    a->offset += static_cast<long>(n);
    return static_cast<int>(n);
}

extern "C" int bionic_AAsset_seek(void* asset, long offset, int whence) {
    auto* a = static_cast<AAssetImpl*>(asset);
    if (!a) return -1;
    if (std::fseek(a->file, offset, whence) != 0) return -1;
    a->offset = std::ftell(a->file);
    return 0;
}

extern "C" int64_t bionic_AAsset_seek64(void* asset, int64_t offset, int whence) {
    auto* a = static_cast<AAssetImpl*>(asset);
    if (!a) return -1;
    if (::fseeko(a->file, static_cast<off_t>(offset), whence) != 0) return -1;
    a->offset = static_cast<long>(::ftello(a->file));
    return static_cast<int64_t>(a->offset);
}

extern "C" const void* bionic_AAsset_getBuffer(void* asset) {
    auto* a = static_cast<AAssetImpl*>(asset);
    if (!a) return nullptr;
    if (a->buffer) return a->buffer;
    if (a->length <= 0) return nullptr;

    // mmap, not malloc + read.
    //
    // The contract is a pointer to the whole asset, valid until AAsset_close, and a
    // mapping satisfies it as well as a heap copy does — but the pages are CLEAN, so
    // under memory pressure the kernel can evict them and read them back from disk.
    // Heap pages are dirty; iOS does not swap, so they can only be reclaimed by killing
    // the process.
    //
    // The scale is what makes this decisive rather than tidy. Minecraft ships 574 MB of
    // assets across 36005 files, its largest single material is 20.9 MB, and
    // libminecraftpe.so is already ~330 MB of image. A handful of buffers reaches the
    // jetsam limit, and jetsam sends SIGKILL: no handler runs, no crash log is written,
    // and nothing in the log says why the process vanished.
    //
    // MAP_PRIVATE so a guest writing through the buffer — which the API does not permit
    // but does not prevent — cannot modify the extracted asset on disk.
    const int fd = ::fileno(a->file);
    void* mapped = fd >= 0 ? ::mmap(nullptr, static_cast<size_t>(a->length), PROT_READ,
                                    MAP_PRIVATE, fd, 0)
                           : MAP_FAILED;
    if (mapped != MAP_FAILED) {
        a->buffer = mapped;
        a->bufferSize = static_cast<size_t>(a->length);
        a->bufferMapped = true;
        // The API says the read cursor is unspecified after getBuffer; leaving it at the
        // end matches what the copying implementation did, so a caller that mixes
        // getBuffer with read() sees no change in behaviour.
        a->offset = a->length;
        trace_buffer("AAsset_getBuffer mapped", a->name,
                     static_cast<uint64_t>(a->length), /*mapped=*/true);
        return mapped;
    }

    // Falling back to a copy keeps the API working where mmap cannot (a filesystem that
    // does not support it). Logged as dirty, because that is the case that counts against
    // the footprint.
    void* buf = std::malloc(static_cast<size_t>(a->length));
    if (!buf) return nullptr;
    std::fseek(a->file, 0, SEEK_SET);
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

extern "C" void bionic_AAsset_close(void* asset) {
    auto* a = static_cast<AAssetImpl*>(asset);
    if (!a) return;
    if (a->buffer) {
        // munmap and free are not interchangeable; using the wrong one is undefined and
        // would corrupt the allocator or the address space rather than fail visibly.
        if (a->bufferMapped) ::munmap(a->buffer, a->bufferSize);
        else std::free(a->buffer);
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
    {"AAsset_openFileDescriptor64", reinterpret_cast<void*>(&bionic_AAsset_openFileDescriptor)},
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
