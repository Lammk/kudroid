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

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
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
extern "C" void* bionic_AAssetManager_openFd(void* manager, const char* filename, void* outStart, void* outLength);
extern "C" int bionic_AAsset_openFileDescriptor(void* asset, void* outStart, void* outLength);
extern "C" int bionic_AAsset_seek(void* asset, long offset, int whence);
extern "C" int kudroid_asset_resolve_bytes(const char* filename, char** outPath,
                                          int64_t* outStart, int64_t* outLength);


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

// getBuffer maps the file instead of copying it.
//
// The contents being right is only half of it. A heap copy is DIRTY memory, and iOS does
// not swap — so it can only be reclaimed by killing the process. Minecraft ships 574 MB of
// assets across 36005 files with a largest single material of 20.9 MB, on top of ~330 MB of
// libminecraftpe image: a handful of copied buffers reaches the jetsam limit, and jetsam
// sends SIGKILL, which runs no handler and writes no crash log.
//
// A mapping is clean, so the kernel evicts and re-reads those pages instead.
void TestGetBufferIsMapped(const std::filesystem::path& assetsDir) {
    std::printf("-- getBuffer maps rather than copies --\n");

    // Large enough to span several pages, so a mapping is distinguishable from a small
    // allocation that happens to look page-aligned.
    std::string payload;
    payload.reserve(300 * 1024);
    for (int i = 0; i < 300 * 1024; ++i) {
        payload.push_back(static_cast<char>('A' + (i % 26)));
    }
    WriteFile(assetsDir / "big.bin", payload);
    kudroid_set_assets_dir(assetsDir.string().c_str());

    void* asset = bionic_AAssetManager_open(nullptr, "big.bin", 0);
    Check(asset != nullptr, "the large asset opens");
    if (asset == nullptr) return;

    Check(bionic_AAsset_getLength(asset) == static_cast<long>(payload.size()),
          "the length matches the file");

    const void* buffer = bionic_AAsset_getBuffer(asset);
    Check(buffer != nullptr, "getBuffer returns a pointer");
    if (buffer != nullptr) {
        Check(std::memcmp(buffer, payload.data(), payload.size()) == 0,
              "every byte of the mapped buffer matches the file");

        // A mapping always starts at a page boundary. malloc for 300 KB does not have to,
        // and in practice does not — so this distinguishes the two without reaching into
        // the implementation.
        const auto address = reinterpret_cast<uintptr_t>(buffer);
        const auto pageSize = static_cast<uintptr_t>(::sysconf(_SC_PAGESIZE));
        Check(pageSize > 0 && (address % pageSize) == 0,
              "the buffer is page-aligned, as a mapping is and a heap copy is not");
    }

    // Called twice, the same pointer comes back: the buffer is cached, so a game that asks
    // repeatedly does not map the file again each time.
    const void* second = bionic_AAsset_getBuffer(asset);
    Check(second == buffer, "a second getBuffer returns the cached pointer");

    // close() must release a mapping with munmap, not free. Getting that wrong is
    // undefined behaviour rather than a visible failure, so the check that it survives
    // close-then-reopen is the observable part.
    bionic_AAsset_close(asset);

    void* again = bionic_AAssetManager_open(nullptr, "big.bin", 0);
    Check(again != nullptr, "the asset reopens after close");
    if (again != nullptr) {
        const void* buf2 = bionic_AAsset_getBuffer(again);
        Check(buf2 != nullptr && std::memcmp(buf2, payload.data(), payload.size()) == 0,
              "the contents are still correct after a close and reopen");
        bionic_AAsset_close(again);
    }

    // An empty file has nothing to map. mmap of length 0 fails, and the result must be a
    // null buffer rather than a mapping of the wrong size.
    WriteFile(assetsDir / "empty.bin", "");
    void* empty = bionic_AAssetManager_open(nullptr, "empty.bin", 0);
    if (empty != nullptr) {
        Check(bionic_AAsset_getLength(empty) == 0, "an empty asset reports zero length");
        Check(bionic_AAsset_getBuffer(empty) == nullptr,
              "getBuffer on an empty asset returns null, not a zero-length mapping");
        bionic_AAsset_close(empty);
    }
}

void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
    put16(v, static_cast<uint16_t>(x & 0xFFFF));
    put16(v, static_cast<uint16_t>(x >> 16));
}

struct TestZipEntry {
    std::string name;
    std::string content;
};

std::vector<uint8_t> BuildTestZip(const std::vector<TestZipEntry>& entries) {
    std::vector<uint8_t> out;
    std::vector<uint32_t> localOffsets;

    for (const auto& e : entries) {
        localOffsets.push_back(static_cast<uint32_t>(out.size()));
        put32(out, 0x04034b50);                                   // local header sig
        put16(out, 20);                                           // version needed
        put16(out, 0);                                            // flags
        put16(out, 0);                                            // method: stored
        put16(out, 0);                                            // time
        put16(out, 0);                                            // date
        put32(out, 0);                                            // crc32
        put32(out, static_cast<uint32_t>(e.content.size()));      // compressed
        put32(out, static_cast<uint32_t>(e.content.size()));      // uncompressed
        put16(out, static_cast<uint16_t>(e.name.size()));
        put16(out, 0);                                            // extra length
        out.insert(out.end(), e.name.begin(), e.name.end());
        out.insert(out.end(), e.content.begin(), e.content.end());
    }

    const uint32_t centralOffset = static_cast<uint32_t>(out.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        put32(out, 0x02014b50);                                   // central header sig
        put16(out, 20);                                           // version made by
        put16(out, 20);                                           // version needed
        put16(out, 0);                                            // flags
        put16(out, 0);                                            // method: stored
        put16(out, 0);                                            // time
        put16(out, 0);                                            // date
        put32(out, 0);                                            // crc32
        put32(out, static_cast<uint32_t>(e.content.size()));
        put32(out, static_cast<uint32_t>(e.content.size()));
        put16(out, static_cast<uint16_t>(e.name.size()));
        put16(out, 0);                                            // extra
        put16(out, 0);                                            // comment
        put16(out, 0);                                            // disk start
        put16(out, 0);                                            // internal attrs
        put32(out, 0);                                            // external attrs
        put32(out, localOffsets[i]);
        out.insert(out.end(), e.name.begin(), e.name.end());
    }
    const uint32_t centralSize = static_cast<uint32_t>(out.size()) - centralOffset;

    put32(out, 0x06054b50);                                       // EOCD sig
    put16(out, 0);                                                // this disk
    put16(out, 0);                                                // central dir disk
    put16(out, static_cast<uint16_t>(entries.size()));
    put16(out, static_cast<uint16_t>(entries.size()));
    put32(out, centralSize);
    put32(out, centralOffset);
    put16(out, 0);                                                // comment length
    return out;
}

void TestBaseApkFallback(const std::filesystem::path& appDir) {
    std::printf("-- fallback to base.apk when assets are not unpacked on disk --\n");

    const auto assetsDir = appDir / "assets";
    // Do not create assetsDir on disk!
    const auto baseApkPath = appDir / "base.apk";

    std::vector<TestZipEntry> entries = {
        {"assets/bin/Data/data.unity3d", "UNITY_DATA_BLOB_STORED"},
        {"assets/bin/Data/settings.xml", "<config>ok</config>"},
        {"assets/sub/foo.txt", "bar"}
    };
    const auto zipBytes = BuildTestZip(entries);
    std::filesystem::create_directories(appDir);
    {
        std::ofstream out(baseApkPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(zipBytes.data()), static_cast<std::streamsize>(zipBytes.size()));
    }

    kudroid_set_assets_dir(assetsDir.string().c_str());

    // 1. Test reading asset stored inside base.apk
    Check(ReadAsset("bin/Data/data.unity3d") == "UNITY_DATA_BLOB_STORED",
          "ReadAsset resolves entry stored inside base.apk");
    Check(ReadAsset("assets/bin/Data/settings.xml") == "<config>ok</config>",
          "ReadAsset resolves entry with 'assets/' prefix from base.apk");

    // 2. Test openFd and file descriptor reading
    off_t start = -1, len = -1;
    void* asset = bionic_AAssetManager_openFd(nullptr, "bin/Data/data.unity3d", &start, &len);
    Check(asset != nullptr, "openFd returns non-null asset");
    Check(start > 0, "openFd outStart is non-zero offset into base.apk");
    Check(len == 22, "openFd outLength matches stored payload size");

    if (asset != nullptr) {
        off_t start2 = -1, len2 = -1;
        int fd = bionic_AAsset_openFileDescriptor(asset, &start2, &len2);
        Check(fd >= 0, "openFileDescriptor returns valid fd");
        Check(start2 == start && len2 == len, "openFileDescriptor start/len match openFd");
        if (fd >= 0) {
            std::string buf(static_cast<size_t>(len2), '\0');
            ::lseek(fd, start2, SEEK_SET);
            const ssize_t bytesRead = ::read(fd, buf.data(), buf.size());
            Check(bytesRead == len2, "read from base.apk fd at start offset matches len");
            Check(buf == "UNITY_DATA_BLOB_STORED", "data read via openFileDescriptor fd matches content");
            ::close(fd);
        }
        bionic_AAsset_close(asset);
    }

    // 3. Test openDir fallback to base.apk
    void* dir = bionic_AAssetManager_openDir(nullptr, "bin/Data");
    Check(dir != nullptr, "openDir returns dir handle from base.apk");
    int count = 0;
    bool sawUnity3d = false;
    bool sawSettings = false;
    for (const char* name = bionic_AAssetDir_getNextFileName(dir); name != nullptr;
         name = bionic_AAssetDir_getNextFileName(dir)) {
        ++count;
        if (std::strcmp(name, "data.unity3d") == 0) sawUnity3d = true;
        if (std::strcmp(name, "settings.xml") == 0) sawSettings = true;
    }
    bionic_AAssetDir_close(dir);
    Check(count == 2, "openDir finds both entries in bin/Data from base.apk");
    Check(sawUnity3d && sawSettings, "openDir reports both data.unity3d and settings.xml");
}

void TestAssetSeekAndRead(const std::filesystem::path& assetsDir) {
    std::printf("-- seek and read consistency on extracted assets --\n");
    const std::string content = "0123456789ABCDEF0123456789ABCDEF";
    WriteFile(assetsDir / "sound.bin", content);
    kudroid_set_assets_dir(assetsDir.string().c_str());

    void* asset = bionic_AAssetManager_open(nullptr, "sound.bin", 0);
    Check(asset != nullptr, "sound asset opens");

    char buf[8] = {0};
    bionic_AAsset_seek(asset, 10, SEEK_SET);
    int n1 = bionic_AAsset_read(asset, buf, 6);
    Check(n1 == 6 && std::string(buf, 6) == "ABCDEF", "read at offset 10 matches");

    bionic_AAsset_seek(asset, 0, SEEK_SET);
    int n2 = bionic_AAsset_read(asset, buf, 4);
    Check(n2 == 4 && std::string(buf, 4) == "0123", "read at offset 0 matches after seek backwards");

    off_t start = 0, len = 0;
    int fd = bionic_AAsset_openFileDescriptor(asset, &start, &len);
    Check(fd >= 0, "openFileDescriptor returns valid fd");
    Check(start == 0, "start is 0 for loose asset");
    Check(len == static_cast<off_t>(content.size()), "length matches file size");

    char fdBuf[5] = {0};
    ssize_t fdRead = ::read(fd, fdBuf, 4);
    Check(fdRead == 4 && std::string(fdBuf, 4) == "0123", "fd read from start matches");
    ::close(fd);

    // The fd must be INDEPENDENT of the asset's FILE*: the shim seeks and reads that
    // FILE on every AAsset_read, and on a shared open file description those moves land
    // in the caller's fd too — a decoder streaming from the fd then reads from wherever
    // the asset's cursor last was, which is exactly the corrupted audio symptom.
    int fd2 = bionic_AAsset_openFileDescriptor(asset, &start, &len);
    Check(fd2 >= 0, "a second openFileDescriptor returns valid fd");
    if (fd2 >= 0) {
        int n3 = bionic_AAsset_read(asset, buf, 4);  // continues at [4:8], moves the FILE
        Check(n3 == 4 && std::string(buf, 4) == "4567", "the asset read continues from its own cursor");
        char fdBuf2[5] = {0};
        const ssize_t got2 = ::read(fd2, fdBuf2, 4);
        Check(got2 == 4 && std::string(fdBuf2, 4) == "0123",
              "an asset read does not move the caller's fd position");
        ::close(fd2);
    }

    bionic_AAsset_close(asset);
}

// The C helper behind Java's AssetManager.openFd: it must hand back a real file, its
// in-file start offset and the payload length, for both the loose and the in-APK case.
void TestResolveBytesLoose(const std::filesystem::path& assetsDir) {
    std::printf("-- kudroid_asset_resolve_bytes: loose file --\n");
    WriteFile(assetsDir / "clip.ogg", "OGGS_ON_DISK");
    kudroid_set_assets_dir(assetsDir.string().c_str());

    char* path = nullptr;
    int64_t start = -1, length = -1;
    const int rc = kudroid_asset_resolve_bytes("clip.ogg", &path, &start, &length);
    Check(rc == 1, "a loose asset resolves");
    Check(path != nullptr && std::strstr(path, "clip.ogg") != nullptr,
          "the backing path names the file");
    Check(start == 0, "the loose file's payload starts at 0");
    Check(length == 12, "the payload length is the file size");
    if (path != nullptr) {
        std::ifstream in(path, std::ios::binary);
        std::string got;
        std::getline(in, got);
        Check(got == "OGGS_ON_DISK", "the backing file holds the payload");
        std::free(path);
    }

    Check(kudroid_asset_resolve_bytes("no/such.ogg", &path, &start, &length) == 0,
          "a missing asset resolves to 0");
}

void TestResolveBytesInApk(const std::filesystem::path& appDir) {
    std::printf("-- kudroid_asset_resolve_bytes: entry only inside base.apk --\n");
    const auto assetsDir = appDir / "assets";  // deliberately not created
    const auto baseApkPath = appDir / "base.apk";
    std::filesystem::create_directories(appDir);

    const std::vector<TestZipEntry> entries = {
        {"assets/res/audio/shot.ogg", "STORED_OGG_PAYLOAD"},
    };
    const auto zipBytes = BuildTestZip(entries);
    {
        std::ofstream out(baseApkPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(zipBytes.data()),
                  static_cast<std::streamsize>(zipBytes.size()));
    }
    kudroid_set_assets_dir(assetsDir.string().c_str());

    char* path = nullptr;
    int64_t start = -1, length = -1;
    const int rc = kudroid_asset_resolve_bytes("res/audio/shot.ogg", &path, &start, &length);
    Check(rc == 1, "an in-APK asset resolves");
    Check(path != nullptr, "the backing path is returned");
    if (path != nullptr) {
        // A stored entry is served from base.apk itself at its payload offset — the
        // same contract openFileDescriptor reports to the caller.
        Check(start > 0, "the payload offset points into base.apk");
        Check(length == 18, "the payload length matches the entry");
        std::string buf(static_cast<size_t>(length), '\0');
        std::ifstream in(path, std::ios::binary);
        in.seekg(static_cast<std::streamoff>(start));
        in.read(buf.data(), length);
        Check(buf == "STORED_OGG_PAYLOAD", "reading the backing file at start yields the payload");
        std::free(path);
    }

    // And the fd handed out alongside must read the same bytes.
    off_t fdStart = -1, fdLen = -1;
    void* asset = bionic_AAssetManager_openFd(nullptr, "res/audio/shot.ogg", &fdStart, &fdLen);
    Check(asset != nullptr, "openFd on an in-APK asset returns a handle");
    if (asset != nullptr) {
        int fd = bionic_AAsset_openFileDescriptor(asset, &fdStart, &fdLen);
        Check(fd >= 0, "openFileDescriptor on an in-APK asset returns fd");
        if (fd >= 0) {
            std::string buf(static_cast<size_t>(fdLen), '\0');
            const ssize_t got = ::pread(fd, buf.data(), buf.size(), fdStart);
            Check(got == static_cast<ssize_t>(fdLen) && buf == "STORED_OGG_PAYLOAD",
                  "the fd at the reported offset reads the payload");
            ::close(fd);
        }
        bionic_AAsset_close(asset);
    }
}

} // namespace


// The cold-start shape: thousands of opens served from the same base.apk in one run.
// Each open must be an O(1) index lookup over a cached fd, not a fresh open/EOCD/
// central-directory scan of the archive — the version that did the latter measured
// whole seconds of disk I/O before the first frame.
void TestRepeatedArchiveOpens(const std::filesystem::path& appDir) {
    std::printf("-- repeated opens against one archive (cold-start shape) --\n");

    const auto assetsDir = appDir / "assets";
    const auto baseApkPath = appDir / "base.apk";
    std::vector<TestZipEntry> entries;
    for (int i = 0; i < 200; ++i) {
        entries.push_back({"assets/bulk/file" + std::to_string(i),
                           "payload-" + std::to_string(i)});
    }
    const auto zipBytes = BuildTestZip(entries);
    std::filesystem::create_directories(appDir);
    {
        std::ofstream out(baseApkPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(zipBytes.data()),
                  static_cast<std::streamsize>(zipBytes.size()));
    }
    kudroid_set_assets_dir(assetsDir.string().c_str());

    constexpr int kRepeats = 3;
    const auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < kRepeats; ++r) {
        for (int i = 0; i < 200; ++i) {
            const std::string name = "bulk/file" + std::to_string(i);
            const std::string expected = "payload-" + std::to_string(i);
            if (ReadAsset(name.c_str()) != expected) {
                Check(false, "bulk iteration r" + std::to_string(r) + " reads " + name +
                                 " correctly");
                kudroid_set_assets_dir("");
                return;
            }
        }
    }
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - t0)
                               .count();
    Check(true, "600 repeated archive opens return correct bytes");
    // No per-open correctness expectation on time, but the whole loop must stay far
    // below the seconds the pre-index path cost. Generous margin so a loaded CI box
    // never flaks the build.
    Check(elapsedMs < 2000, "600 archive opens finish in under 2s (took " +
                                std::to_string(elapsedMs) + "ms)");

    kudroid_set_assets_dir("");
}

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
    TestGetBufferIsMapped(root / "buffers");
    TestBaseApkFallback(root / "apk_fallback");
    TestAssetSeekAndRead(root / "seek_read");
    TestResolveBytesLoose(root / "resolve_loose");
    TestResolveBytesInApk(root / "resolve_apk");
    TestRepeatedArchiveOpens(root / "repeat_apk");

    std::filesystem::remove_all(root);


    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    if (g_failures != 0) {
        std::printf("=== AAssetManager shim test FAILED ===\n");
        return 1;
    }
    std::printf("=== AAssetManager shim test PASSED ===\n");
    return 0;
}
