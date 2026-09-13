#pragma once

#include <cstddef>
#include <string>

namespace kudroid {

struct SymbolEntry;

// AAssetManager / AAsset / AAssetDir — Bionic asset API backed by the APK's
// extracted assets/ directory (android_root/data/app/<app>/assets).
// Games (Unity, Godot, SDL) read packaged files through this API; previously
// every call resolved to the universal dummy (returned 0), so AAssetManager_open
// "succeeded" with a null asset and reads produced nothing.
const SymbolEntry* get_asset_symbols(size_t* count);

// Thread-safe copy of the assets dir; prefer over the C accessor below.
std::string kudroid_get_assets_dir_cpp(void);

} // namespace kudroid

#ifdef __cplusplus
extern "C" {
#endif

// Set the directory containing extracted assets (called from kudroid_run_apk / kudroid_load_apk).
void kudroid_set_assets_dir(const char* dir);

// Get the directory containing extracted assets (or nullptr if not set).
// Legacy: valid only until the next set; C++ callers use the copy above.
const char* kudroid_get_assets_dir(void);

#ifdef __cplusplus
}
#endif
