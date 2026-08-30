// Host-side tests for the AAssetManager shim (src/platform/AssetShim.cpp).
//
// A game reads its packaged data through this API, so a path resolved wrongly here is not
// one missing file — it is every file, and the symptom is the game reporting a missing
// asset rather than an error that names the cause.
//
// The regression that motivated these: an APK may nest a directory of the same name inside
// its assets, and Minecraft does — 36001 of its 36005 asset entries live under
// `assets/assets/`. An AAssetManager path is relative to the APK's assets/ folder, so the
// file at zip entry `assets/assets/bootstrap.json` is requested as `assets/bootstrap.json`.
// The shim stripped a leading "assets/" unconditionally, calling it a path the game
// "transmits incorrectly", which turned the one path that resolves into the one that does
// not. On device the game logged "Unable to locate asset: bootstrap.json" and then called
// std::terminate — with an abort message of just "terminating", i.e. no exception at all,
// so it read as a crash inside the game rather than as a shim returning null.

#include "kudroid/platform/AssetShim.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

// ─── Shims under test (extern "C", defined in AssetShim.cpp) ────────────────
extern "C" void* bionic_AAssetManager_open(void* manager, const char* filename, int mode);
extern "C" int bionic_AAsset_read(void* asset, void* buf, size_t count);
extern "C" long bionic_AAsset_getLength(void* asset);
extern "C" const char* bionic_AAsset_getFileName(void* asset);
extern "C" const void* bionic_AAsset_getBuffer(void* asset);
extern "C" void bionic_AAsset_close(void* asset);
extern "C" void* bionic_AAssetManager_openDir(void* manager, const char* dirName);
extern "C" const char* bionic_AAssetDir_getNextFileName(void* dir);
extern "C" void bionic_AAssetDir_close(void* dir);

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (condition) {
        std::printf("  OK   %s\n", what.c_str());
    } else {
        std::printf("  FAIL %s\n", what.c_str());
        ++g_failures;
    }
}

void WriteFile(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

// Read a whole asset through the shim, so the check covers open + read + length rather
// than only that a handle came back.
std::string ReadAsset(const char* name) {
    void* asset = bionic_AAssetManager_open(nullptr, name, 0);
    if (asset == nullptr) return {};
    const long length = bionic_AAsset_getLength(asset);
    std::string out(static_cast<size_t>(length), '\0');
    const int got = bionic_AAsset_read(asset, out.data(), static_cast<size_t>(length));
    bionic_AAsset_close(asset);
    if (got != length) return {};
    return out;
}

// ─── the nested assets/ layout, as Minecraft ships it ───────────────────────
//
//   <assets_dir>/assets/bootstrap.json     <- zip entry assets/assets/bootstrap.json
//   <assets_dir>/dexopt/baseline.prof      <- zip entry assets/dexopt/baseline.prof
//
// A game asking for "assets/bootstrap.json" must reach the first; one asking for
// "dexopt/baseline.prof" must reach the second. Both paths are relative to <assets_dir>,
// and neither may be rewritten.
void TestNestedAssetsDirectory(const std::filesystem::path& assetsDir) {
    std::printf("-- a nested assets/ directory --\n");

    WriteFile(assetsDir / "assets" / "bootstrap.json", "{\"nested\":true}");
    WriteFile(assetsDir / "dexopt" / "baseline.prof", "profile-data");
    kudroid_set_assets_dir(assetsDir.string().c_str());

    // The exact request from the device log. Stripping "assets/" looked for
    // <assets_dir>/bootstrap.json, which does not exist.
    Check(ReadAsset("assets/bootstrap.json") == "{\"nested\":true}",
          "a path into a nested assets/ folder resolves literally");

    // A sibling directory, to show the fix is not special-casing one name.
    Check(ReadAsset("dexopt/baseline.prof") == "profile-data",
          "a path that needs no rewriting still resolves");

    void* asset = bionic_AAssetManager_open(nullptr, "assets/bootstrap.json", 0);
    Check(asset != nullptr, "the nested asset opens");
    if (asset != nullptr) {
        Check(bionic_AAsset_getLength(asset) == 15, "its length is the file's length");
        const char* name = bionic_AAsset_getFileName(asset);
        Check(name != nullptr && std::strcmp(name, "assets/bootstrap.json") == 0,
              "getFileName reports the path as asked for, not the rewritten one");
        const void* buffer = bionic_AAsset_getBuffer(asset);
        Check(buffer != nullptr &&
                  std::memcmp(buffer, "{\"nested\":true}", 15) == 0,
              "getBuffer returns the nested file's contents");
        bionic_AAsset_close(asset);
    }
}

// A flat APK — the common shape — must keep working. This is what the unconditional strip
// was there for, so the fallback has to cover it.
void TestFlatLayoutFallback(const std::filesystem::path& assetsDir) {
    std::printf("-- a flat layout, and the zip-entry fallback --\n");

    WriteFile(assetsDir / "config.txt", "flat");
    kudroid_set_assets_dir(assetsDir.string().c_str());

    Check(ReadAsset("config.txt") == "flat", "a plain relative path resolves");

    // A caller that passes the zip entry name rather than an assets-relative path. There is
    // no <assets_dir>/assets/config.txt, so the stripped form is the only one that can
    // resolve — and it must still be reached.
    Check(ReadAsset("assets/config.txt") == "flat",
          "a zip entry name falls back to the stripped path");
}

// When BOTH interpretations exist, the literal path wins. This is the case that decides
// which of the two orders is correct, and getting it backwards is exactly the old bug: the
// nested file is the one the game means.
void TestLiteralPathWins(const std::filesystem::path& assetsDir) {
    std::printf("-- an ambiguous path prefers the literal one --\n");

    WriteFile(assetsDir / "shadow.txt", "outer");
    WriteFile(assetsDir / "assets" / "shadow.txt", "inner");
    kudroid_set_assets_dir(assetsDir.string().c_str());

    Check(ReadAsset("assets/shadow.txt") == "inner",
          "the nested file wins over the stripped interpretation");
    Check(ReadAsset("shadow.txt") == "outer",
          "the unprefixed path still reaches the outer file");
}

void TestMissingAndDegenerate(const std::filesystem::path& assetsDir) {
    std::printf("-- missing and degenerate paths --\n");

    kudroid_set_assets_dir(assetsDir.string().c_str());

    Check(bionic_AAssetManager_open(nullptr, "no/such/file.bin", 0) == nullptr,
          "a genuinely missing asset returns null");
    Check(bionic_AAssetManager_open(nullptr, "", 0) == nullptr,
          "an empty name returns null");
    Check(bionic_AAssetManager_open(nullptr, nullptr, 0) == nullptr,
          "a null name returns null rather than faulting");

    // A directory is not an asset. Returning a handle for one would have AAsset_read fail
    // in a way the caller cannot distinguish from an empty file.
    WriteFile(assetsDir / "adir" / "inside.txt", "x");
    Check(bionic_AAssetManager_open(nullptr, "adir", 0) == nullptr,
          "a directory is not opened as an asset");
}

void TestOpenDirNesting(const std::filesystem::path& assetsDir) {
    std::printf("-- openDir follows the same rule --\n");

    WriteFile(assetsDir / "assets" / "sounds" / "click.fsb", "a");
    WriteFile(assetsDir / "assets" / "sounds" / "step.fsb", "b");
    kudroid_set_assets_dir(assetsDir.string().c_str());

    // Stripping unconditionally listed <assets_dir>/sounds, which does not exist — and
    // openDir returns an empty directory rather than an error, so a game enumerating its
    // assets found nothing and reported no failure at all.
    void* dir = bionic_AAssetManager_openDir(nullptr, "assets/sounds");
    Check(dir != nullptr, "openDir returns a handle");
    int count = 0;
    bool sawClick = false;
    bool sawStep = false;
    for (const char* name = bionic_AAssetDir_getNextFileName(dir); name != nullptr;
         name = bionic_AAssetDir_getNextFileName(dir)) {
        ++count;
        if (std::strcmp(name, "click.fsb") == 0) sawClick = true;
        if (std::strcmp(name, "step.fsb") == 0) sawStep = true;
    }
    bionic_AAssetDir_close(dir);
    Check(count == 2, "a nested directory lists its files");
    Check(sawClick && sawStep, "both entries are reported by name");
}

} // namespace

int main() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("kudroid_assets_test_" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);

    std::printf("=== AAssetManager shim test ===\n");

    // A separate directory per case, so one case's files cannot satisfy another's lookup.
    TestNestedAssetsDirectory(root / "nested");
    TestFlatLayoutFallback(root / "flat");
    TestLiteralPathWins(root / "ambiguous");
    TestMissingAndDegenerate(root / "missing");
    TestOpenDirNesting(root / "dirs");

    std::filesystem::remove_all(root);

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    if (g_failures != 0) {
        std::printf("=== AAssetManager shim test FAILED ===\n");
        return 1;
    }
    std::printf("=== AAssetManager shim test PASSED ===\n");
    return 0;
}
