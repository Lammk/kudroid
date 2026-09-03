// Host test for APK extraction, specifically that the archive itself is kept.
//
// Extracting entries as loose files is not enough for every app. A Unity game opens
// its own APK as a ZIP and reads assets/bin/Data/* out of the central directory — it
// never looks at loose files — so it needs the archive present at the path
// getPackageCodePath() reports, which is /data/app/<pkg>/base.apk. Without it Unity
// logs "ApkAddCentralDirectory : Unable to open" and starts with no scenes, no
// textures and no audio: a black screen and silence, with nothing near the cause.
//
// The re-install case has teeth too: installing from a path that already IS the
// destination must not truncate the source out from under itself.
#include "kudroid/APKExtractor.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

void put16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
}
void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    put16(v, static_cast<std::uint16_t>(x & 0xFFFF));
    put16(v, static_cast<std::uint16_t>(x >> 16));
}

struct Entry {
    std::string name;
    std::string content;
};

// A minimal ZIP with stored (uncompressed) entries. The extractor never verifies
// CRCs, so they are left zero — the point here is the directory structure, which is
// what an app's own ZIP reader walks.
std::vector<std::uint8_t> BuildZip(const std::vector<Entry>& entries) {
    std::vector<std::uint8_t> out;
    std::vector<std::uint32_t> localOffsets;

    for (const Entry& e : entries) {
        localOffsets.push_back(static_cast<std::uint32_t>(out.size()));
        put32(out, 0x04034b50);                                   // local header sig
        put16(out, 20);                                           // version needed
        put16(out, 0);                                            // flags
        put16(out, 0);                                            // method: stored
        put16(out, 0);                                            // time
        put16(out, 0);                                            // date
        put32(out, 0);                                            // crc32
        put32(out, static_cast<std::uint32_t>(e.content.size())); // compressed
        put32(out, static_cast<std::uint32_t>(e.content.size())); // uncompressed
        put16(out, static_cast<std::uint16_t>(e.name.size()));
        put16(out, 0);                                            // extra length
        out.insert(out.end(), e.name.begin(), e.name.end());
        out.insert(out.end(), e.content.begin(), e.content.end());
    }

    const std::uint32_t centralOffset = static_cast<std::uint32_t>(out.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        put32(out, 0x02014b50);                                   // central header sig
        put16(out, 20);                                           // version made by
        put16(out, 20);                                           // version needed
        put16(out, 0);                                            // flags
        put16(out, 0);                                            // method: stored
        put16(out, 0);                                            // time
        put16(out, 0);                                            // date
        put32(out, 0);                                            // crc32
        put32(out, static_cast<std::uint32_t>(e.content.size()));
        put32(out, static_cast<std::uint32_t>(e.content.size()));
        put16(out, static_cast<std::uint16_t>(e.name.size()));
        put16(out, 0);                                            // extra
        put16(out, 0);                                            // comment
        put16(out, 0);                                            // disk start
        put16(out, 0);                                            // internal attrs
        put32(out, 0);                                            // external attrs
        put32(out, localOffsets[i]);
        out.insert(out.end(), e.name.begin(), e.name.end());
    }
    const std::uint32_t centralSize = static_cast<std::uint32_t>(out.size()) - centralOffset;

    put32(out, 0x06054b50);                                       // EOCD sig
    put16(out, 0);                                                // this disk
    put16(out, 0);                                                // central dir disk
    put16(out, static_cast<std::uint16_t>(entries.size()));
    put16(out, static_cast<std::uint16_t>(entries.size()));
    put32(out, centralSize);
    put32(out, centralOffset);
    put16(out, 0);                                                // comment length
    return out;
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
    std::printf("=== APK extraction: the archive is kept as base.apk ===\n");

    std::error_code ec;
    const auto root = std::filesystem::temp_directory_path(ec) / "kudroid_apk_extract_test";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    if (ec) {
        std::printf("  FAIL cannot create temp dir: %s\n=== FAILED ===\n", ec.message().c_str());
        return 1;
    }

    // An APK shaped like a Unity build: a manifest, a dex, and an asset the engine
    // would read back out of the ZIP rather than off disk.
    const std::vector<Entry> entries = {
        {"AndroidManifest.xml", std::string("\x03\x00\x08\x00not-real-axml", 17)},
        {"classes.dex", "dex\n035\0fake-dex-payload"},
        {"assets/bin/Data/unity_app_guid", "1234567890abcdef"},
        {"assets/bin/Data/data.unity3d", "UnityFS-payload-bytes"},
    };
    const std::vector<std::uint8_t> zip = BuildZip(entries);

    const auto apkPath = root / "com.example.game.apk";
    {
        std::ofstream f(apkPath, std::ios::binary);
        f.write(reinterpret_cast<const char*>(zip.data()),
                static_cast<std::streamsize>(zip.size()));
    }
    Check(std::filesystem::exists(apkPath), "wrote the source APK");

    const auto appDir = root / "data" / "app" / "com.example.game";

    // ── a normal install keeps the archive and the loose files ──
    {
        const bool ok = kudroid::APKExtractor::extract_apk(apkPath.string(), appDir.string());
        Check(ok, std::string("extract_apk succeeded: ") + kudroid::APKExtractor::lastError());

        const auto basePath = appDir / "base.apk";
        Check(std::filesystem::exists(basePath),
              "base.apk exists — the path getPackageCodePath() reports");

        // Byte-identical, not merely present: an app's ZIP reader walks the central
        // directory, so a re-zipped or truncated copy would fail in the same way as
        // no copy at all.
        Check(ReadFile(basePath) ==
                  std::string(reinterpret_cast<const char*>(zip.data()), zip.size()),
              "base.apk is byte-identical to the source archive");

        // The loose extraction must still happen — the ELF loader reads .so files off
        // disk, and app_info.json drives the launcher list.
        Check(std::filesystem::exists(appDir / "classes.dex"), "classes.dex was extracted");
        Check(std::filesystem::exists(appDir / "assets/bin/Data/unity_app_guid"),
              "assets were extracted as loose files too");
        Check(std::filesystem::exists(appDir / "app_info.json"), "app_info.json was written");
    }

    // ── re-installing from the destination must not destroy it ──
    // Installing an APK that already sits at <appDir>/base.apk is the natural way to
    // reinstall without re-copying by hand. copy_file(src, src) would truncate the
    // source, leaving an empty archive and an app that starts to a black screen.
    {
        const auto basePath = appDir / "base.apk";
        const std::string before = ReadFile(basePath);
        Check(!before.empty(), "base.apk has content before the re-install");

        const bool ok = kudroid::APKExtractor::extract_apk(basePath.string(), appDir.string());
        Check(ok, std::string("re-installing from base.apk succeeded: ") +
                      kudroid::APKExtractor::lastError());

        const std::string after = ReadFile(basePath);
        Check(after == before, "base.apk was not truncated by installing it over itself");
        Check(after.size() == zip.size(), "and it is still the full archive");
    }

    // ── a split must not overwrite the base archive ──
    // extract_split runs with extractManifest=false. If it wrote base.apk, the last
    // split processed would replace the base and the app would read the wrong ZIP.
    {
        const auto splitDir = root / "data" / "app" / "split_target";
        const std::vector<Entry> splitEntries = {
            {"assets/split-only.txt", "split-payload"},
        };
        const std::vector<std::uint8_t> splitZip = BuildZip(splitEntries);
        const auto splitApk = root / "config.arm64_v8a.apk";
        {
            std::ofstream f(splitApk, std::ios::binary);
            f.write(reinterpret_cast<const char*>(splitZip.data()),
                    static_cast<std::streamsize>(splitZip.size()));
        }

        const bool ok = kudroid::APKExtractor::extract_split(splitApk.string(), splitDir.string());
        Check(ok, std::string("extract_split succeeded: ") + kudroid::APKExtractor::lastError());
        Check(std::filesystem::exists(splitDir / "assets/split-only.txt"),
              "the split's entries were extracted");
        Check(!std::filesystem::exists(splitDir / "base.apk"),
              "a split does not leave a base.apk behind");
    }

    // ── re-installing an APK must delete orphaned .so and .dex from old versions ──
    {
        // Seed the app directory with an orphaned .so and an orphaned .dex from an older version
        const auto orphanSo = appDir / "lib/arm64-v8a/libold_orphan.so";
        const auto orphanDex = appDir / "classes99.dex";
        std::filesystem::create_directories(orphanSo.parent_path(), ec);
        {
            std::ofstream f(orphanSo, std::ios::binary);
            f << "old-native-code";
        }
        {
            std::ofstream f(orphanDex, std::ios::binary);
            f << "old-dex-bytecode";
        }
        Check(std::filesystem::exists(orphanSo), "created orphan .so before re-install");
        Check(std::filesystem::exists(orphanDex), "created orphan .dex before re-install");

        // Now re-install the APK (which does NOT have libold_orphan.so or classes99.dex)
        const bool ok = kudroid::APKExtractor::extract_apk(apkPath.string(), appDir.string());
        Check(ok, "re-installing APK succeeded");

        // Verify orphaned code files were purged
        Check(!std::filesystem::exists(orphanSo), "orphaned .so from old app was purged on update");
        Check(!std::filesystem::exists(orphanDex), "orphaned .dex from old app was purged on update");

        // Verify current APK's code files are still present
        Check(std::filesystem::exists(appDir / "classes.dex"), "current classes.dex is preserved");
        Check(std::filesystem::exists(appDir / "assets/bin/Data/unity_app_guid"), "current assets are preserved");
    }

    std::filesystem::remove_all(root, ec);

    std::printf("=== %s (%d error) ===\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
