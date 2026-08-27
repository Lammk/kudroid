#pragma once

#include <cstddef>

namespace kudroid {

struct SymbolEntry;

// AAssetManager / AAsset / AAssetDir — Bionic asset API backed by the APK's
// extracted assets/ directory (android_root/data/app/<app>/assets).
// Games (Unity, Godot, SDL) read packaged files through this API; previously
// every call resolved to the universal dummy (returned 0), so AAssetManager_open
// "succeeded" with a null asset and reads produced nothing.
const SymbolEntry* get_asset_symbols(size_t* count);

} // namespace kudroid

#ifdef __cplusplus
extern "C" {
#endif

// Set the directory containing extracted assets (called from kudroid_run_apk / kudroid_load_apk).
void kudroid_set_assets_dir(const char* dir);

#ifdef __cplusplus
}
#endif
