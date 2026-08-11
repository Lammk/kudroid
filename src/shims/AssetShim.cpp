#include "kudroid/shims/AssetShim.h"
#include "kudroid/shims/ShimDefs.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <filesystem>

namespace kudroid {
namespace {

// ─────────────────────────────────────────────────────────────────────────────
// AAssetManager — đọc các file assets đã được APKExtractor giải nén ra đĩa.
//
// APK được extract tới <android_root>/data/app/<appName>/ với cây thư mục
// giữ nguyên, nên assets nằm tại <appName>/assets/<path>. Game (Unity/Godot/SDL)
// gọi AAssetManager_open(manager, "bin/Data/...", mode) — path tương đối với
// thư mục assets. kudroid_set_assets_dir() được kudroid_run_apk gọi trước khi
// chạy game để trỏ đúng chỗ.
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

// Opaque handles (bionic trả con trỏ không tiết lộ nội dung).
struct DummyAssetManager { int dummy; };
static DummyAssetManager g_manager;

// AAsset: mở bằng FILE*; getBuffer đọc toàn bộ vào heap.
struct AAssetImpl {
    FILE* file;
    long length;
    long offset;
    std::string name;
    void* buffer;      // cache cho AAsset_getBuffer
    size_t bufferSize;
};

struct AAssetDirImpl {
    // Minimal: không liệt kê entry. Game vòng lặp tới khi getNextFileName()
    // trả nullptr — trả nullptr ngay là hành vi an toàn (thư mục rỗng).
    int dummy;
};

static AAssetImpl* open_asset(const char* filename) {
    if (!filename || !*filename) return nullptr;
    const std::string base = current_assets_dir();
    if (base.empty()) {
        trace_shim("AAssetManager_open: assets dir not set (kudroid_set_assets_dir)");
        return nullptr;
    }
    // Bỏ tiền tố "assets/" nếu game truyền nhầm (path phải tương đối).
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
    // Không hỗ trợ fd-mode; trả NULL an toàn (game tự fallback sang open()).
    (void)filename; (void)outStart; (void)outLength;
    return nullptr;
}

extern "C" void* bionic_AAssetManager_openDir(void* /*manager*/, const char* /*dirName*/) {
    return new AAssetDirImpl();
}

extern "C" const char* bionic_AAssetDir_getNextFileName(void* dir) {
    (void)dir;
    return nullptr; // thư mục rỗng — kết thúc vòng lặp ngay
}

extern "C" void bionic_AAssetDir_rewind(void* dir) {
    (void)dir;
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

extern "C" long bionic_AAsset_getRemainingLength(void* asset) {
    auto* a = static_cast<AAssetImpl*>(asset);
    return a ? (a->length - a->offset) : 0;
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
    {"AAsset_getRemainingLength", reinterpret_cast<void*>(&bionic_AAsset_getRemainingLength)},
    {"AAsset_read", reinterpret_cast<void*>(&bionic_AAsset_read)},
    {"AAsset_seek", reinterpret_cast<void*>(&bionic_AAsset_seek)},
    {"AAsset_getBuffer", reinterpret_cast<void*>(&bionic_AAsset_getBuffer)},
    {"AAsset_close", reinterpret_cast<void*>(&bionic_AAsset_close)},
};

} // namespace

const SymbolEntry* get_asset_symbols(size_t* count) {
    if (count) {
        *count = sizeof(kAssetSymbols) / sizeof(SymbolEntry);
    }
    return kAssetSymbols;
}

} // namespace kudroid
