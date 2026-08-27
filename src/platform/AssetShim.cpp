#include "kudroid/platform/AssetShim.h"
#include "kudroid/platform/ShimDefs.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <filesystem>
#include <unistd.h>

namespace kudroid {
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

static std::string current_assets_dir() {
    std::lock_guard<std::mutex> lock(g_assetsMtx);
    return g_assetsDir;
}

// Opaque handles (bionic returns cursor without revealing content).
struct DummyAssetManager { int dummy; };
static DummyAssetManager g_manager;

// AAsset: open with FILE*; getBuffer reads the entire thing into the heap.
struct AAssetImpl {
    FILE* file;
    long length;
    long offset;
    std::string name;
    void* buffer;      // cache cho AAsset_getBuffer
    size_t bufferSize;
};

struct AAssetDirImpl {
    std::vector<std::string> names;  // filename (not recursive, like real AAssetDir)
    size_t cursor = 0;
};

static AAssetImpl* open_asset(const char* filename) {
    if (!filename || !*filename) return nullptr;
    const std::string base = current_assets_dir();
    if (base.empty()) {
        trace_shim("AAssetManager_open: assets dir not set (kudroid_set_assets_dir)");
        return nullptr;
    }
    // Remove the "assets/" prefix if the game transmits it incorrectly (path must be relative).
    std::string rel = filename;
    if (rel.rfind("assets/", 0) == 0) rel = rel.substr(7);

    std::error_code ec;
    const auto full = std::filesystem::path(base) / rel;
    if (!std::filesystem::exists(full, ec)) {
        trace_shim("AAssetManager_open: not found (fallback safe NULL)");
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

    std::string rel = dirName ? dirName : "";
    if (rel.rfind("assets/", 0) == 0) rel = rel.substr(7);

    std::error_code ec;
    const auto full = rel.empty() ? std::filesystem::path(base)
                                  : std::filesystem::path(base) / rel;
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
    a->offset = static_cast<long>(got);
    return buf;
}

extern "C" void bionic_AAsset_close(void* asset) {
    auto* a = static_cast<AAssetImpl*>(asset);
    if (!a) return;
    if (a->buffer) std::free(a->buffer);
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
    c->sdkVersion = 30;
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
    return c ? c->sdkVersion : 30;
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
