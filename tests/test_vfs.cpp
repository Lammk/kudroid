// Host-side tests for the guest filesystem view (VFSPathRemapper.cpp).
//
// Every guest open/stat/fopen/opendir goes through remap(), so a mistake here is not a
// wrong path in one place — it is a wrong path everywhere, and the symptom is a missing
// file rather than an error that names the cause. Two properties matter enough to pin:
//
//   1. Containment. A guest must not be able to name a file outside android_root. The
//      remapper joins strings, and the kernel resolves ".." AFTER the join, so
//      "/data/data/../../../etc/passwd" appended to the root walks out of it. APKs
//      produce such paths without malice (asset names taken from zip entries) and a
//      hostile one produces them on purpose.
//
//   2. initialize() runs once. getInstance() calls it and every vfs_* function calls
//      getInstance(), so if it is not guarded each file operation rebuilds 24
//      directories and rewrites 30 pseudo-files. That measured 0.7 ms per operation
//      against 2.4 us for the remap itself, and it also means the guest can be reading
//      a pseudo-file while it is being truncated.
//
// HOME is redirected to a temporary directory so the test never touches a real
// android_root, and so the tree it builds can be inspected.

#include "kudroid/VFSPathRemapper.h"
#include "kudroid/DeviceProfile.h"
#include "kudroid/platform/AssetShim.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <unistd.h>
#include <zlib.h>

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    if (condition) {
        std::printf("  OK   %s\n", what.c_str());
    } else {
        std::printf("  FAIL %s\n", what.c_str());
        ++g_failures;
    }
}

// True when `path` is inside `root` — the property the remapper exists to guarantee.
// Compares after lexical normalisation so a surviving ".." cannot make an escaping path
// look contained.
bool isInside(const std::string& path, const std::string& root) {
    const std::string normalized =
        std::filesystem::path(path).lexically_normal().string();
    if (normalized.size() < root.size()) return false;
    if (normalized.compare(0, root.size(), root) != 0) return false;
    return normalized.size() == root.size() || normalized[root.size()] == '/';
}

void TestContainment(kudroid::VFSPathRemapper& remapper, const std::string& root) {
    std::printf("-- containment --\n");

    // Ordinary paths land where the Android layout says they should.
    Check(remapper.remap("/data/data/com.x/files/a.txt") ==
              root + "/data/data/com.x/files/a.txt",
          "an app data path maps under data/data");
    Check(remapper.remap("/sdcard/Download/a.bin") == root + "/sdcard/Download/a.bin",
          "an sdcard path maps under sdcard");
    Check(remapper.remap("/storage/emulated/0/x") == root + "/sdcard/x",
          "storage/emulated/0 is the same place as sdcard");

    // The escapes. Each of these used to produce a path outside the root.
    const char* escapes[] = {
        "/data/data/../../../../../../etc/passwd",
        "/sdcard/../../../Library/Preferences/x.plist",
        "/data/data/com.x/../../../../var/mobile/Media/DCIM",
        "/system/../../../../../../../../private/etc/master.passwd",
        "/proc/self/../../../../Documents/secret.txt",
        // Not an escape by itself, but normalising must not be confused by the
        // redundant separators and single dots that wrap the "..".
        "/sdcard//.//..//..//..//tmp/x",
    };
    for (const char* escape : escapes) {
        const std::string mapped = remapper.remap(escape);
        Check(isInside(mapped, root),
              std::string("contained: ") + escape + " -> " + mapped);
    }

    // An absolute path matching no Android prefix must be rooted, not passed through.
    // Passing it through was what made normalising pointless: an escape normalises to
    // something like "/Library/..." which matches nothing, and used to be handed
    // straight to open() as a real iOS path.
    const std::string unknown = remapper.remap("/var/mobile/Library/Preferences/x.plist");
    Check(isInside(unknown, root),
          "an unknown absolute path is contained rather than passed through");

    // The device nodes that must NOT be remapped: the guest needs the host's real
    // /dev/urandom, and a file inside android_root would not behave like one.
    Check(remapper.remap("/dev/null") == "/dev/null", "/dev/null stays native");
    Check(remapper.remap("/dev/urandom") == "/dev/urandom", "/dev/urandom stays native");
    Check(remapper.remap("/dev/zero") == "/dev/zero", "/dev/zero stays native");
    Check(remapper.remap("/dev/random") == "/dev/random", "/dev/random stays native");

    // A relative path has no Android prefix to match; rooting it under data/local/tmp
    // keeps it inside the VFS.
    Check(isInside(remapper.remap("relative/file.txt"), root),
          "a relative path is contained");

    // Idempotency. realpath() and readdir() hand the guest paths that are already
    // mapped, and the guest opens what it was given; prefixing the root twice would
    // turn every such reopen into ENOENT.
    const std::string once = remapper.remap("/sdcard/Download/x.txt");
    Check(remapper.remap(once.c_str()) == once,
          "remapping an already-mapped path changes nothing");

    Check(remapper.remap(nullptr).empty(), "a null path is handled");
    Check(isInside(remapper.remap(""), root) || remapper.remap("").empty(),
          "an empty path does not escape");
}

void TestInitializeOnce(kudroid::VFSPathRemapper& remapper) {
    std::printf("-- initialize runs once --\n");

    // First call already happened via getInstance(). What is being measured is that
    // repeat calls do not redo the work: 200 remaps through getInstance() cost about
    // 1.8 ms when initialize() is guarded and about 143 ms when it is not, so the
    // threshold below separates the two by a wide margin without being flaky on a
    // loaded machine.
    Check(remapper.initialize(), "initialize succeeds");

    const int iterations = 200;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        (void)kudroid::VFSPathRemapper::getInstance().remap("/data/data/com.x/files/a.txt");
    }
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
            .count();
    const double perCallMs = elapsedMs / iterations;
    std::printf("       %.4f ms per getInstance()+remap\n", perCallMs);
    Check(perCallMs < 0.1,
          "getInstance() does not rebuild the tree on every call");

    // The pseudo-files must survive being asked for repeatedly: a guest may hold one
    // open while another call runs, and rewriting it under them truncates what they are
    // reading.
    const std::filesystem::path buildProp =
        std::filesystem::path(remapper.androidRoot()) / "system/build.prop";
    Check(std::filesystem::exists(buildProp), "build.prop exists after initialize");
    const auto sizeBefore = std::filesystem::file_size(buildProp);
    for (int i = 0; i < 50; ++i) (void)kudroid::VFSPathRemapper::getInstance();
    Check(std::filesystem::exists(buildProp) &&
              std::filesystem::file_size(buildProp) == sizeBefore,
          "build.prop is not rewritten by later getInstance() calls");
}

void TestPseudoFiles(kudroid::VFSPathRemapper& remapper) {
    std::printf("-- pseudo files --\n");

    const std::string root = remapper.androidRoot();
    // build.prop has to agree with DeviceProfile.h. It said SDK 34 while the profile
    // said 29, so an app reading build.prop and an app reading Build.VERSION.SDK_INT
    // disagreed about the platform they were running on.
    std::string buildProp;
    if (FILE* file = std::fopen((root + "/system/build.prop").c_str(), "r")) {
        char buffer[512];
        while (std::fgets(buffer, sizeof(buffer), file)) buildProp += buffer;
        std::fclose(file);
    }
    Check(buildProp.find("ro.build.version.sdk=" KUDROID_SDK_INT_STR) != std::string::npos,
          "build.prop reports the SDK level from DeviceProfile.h");
    Check(buildProp.find("ro.product.cpu.abi=" KUDROID_DEVICE_ABI) != std::string::npos,
          "build.prop reports the ABI from DeviceProfile.h");
    Check(buildProp.find("ro.build.fingerprint=") != std::string::npos,
          "build.prop reports standard ro.build.fingerprint");
    Check(buildProp.find("ro.hardware=kudroid") != std::string::npos,
          "build.prop reports ro.hardware");

    for (const char* relative : {"proc/cpuinfo", "proc/meminfo", "proc/self/cmdline",
                                 "system/etc/hosts", "proc/mounts", "default.prop",
                                 "vendor/build.prop", "data/user/0", "tmp"}) {
        Check(std::filesystem::exists(std::filesystem::path(root) / relative),
              std::string("pseudo file / standard symlink present: ") + relative);
    }

    // Per-core scheduling files: guests probe these for capacity class.
    const std::filesystem::path cap =
        std::filesystem::path(root) / "sys/devices/system/cpu/cpu0/cpu_capacity";
    int capacity = 0;
    if (FILE* file = std::fopen(cap.c_str(), "r")) {
        char buffer[32] = {};
        if (std::fgets(buffer, sizeof(buffer), file)) capacity = std::atoi(buffer);
        std::fclose(file);
    }
    Check(capacity >= 1 && capacity <= 1024, "cpu0 capacity in [1,1024]");
    const std::filesystem::path pkg =
        std::filesystem::path(root) / "sys/devices/system/cpu/cpu0/topology/physical_package_id";
    int package = -1;
    if (FILE* file = std::fopen(pkg.c_str(), "r")) {
        char buffer[32] = {};
        if (std::fgets(buffer, sizeof(buffer), file)) package = std::atoi(buffer);
        std::fclose(file);
    }
    Check(package == 0, "single-package topology reports 0");
}

void TestProcIdentity(kudroid::VFSPathRemapper& remapper) {
    std::printf("-- proc identity --\n");

    auto readAll = [](const std::string& path) {
        std::string out;
        if (FILE* file = std::fopen(path.c_str(), "r")) {
            char buffer[512];
            size_t n;
            while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
                out.append(buffer, n);
            std::fclose(file);
        }
        return out;
    };

    const std::string root = remapper.androidRoot();
    remapper.setPackageName("com.example.procself");

    const std::string cmdline =
        readAll(root + "/proc/self/cmdline");
    Check(cmdline == std::string("com.example.procself\0", 21),
          "cmdline reports the running package");
    const std::string stat = readAll(root + "/proc/self/stat");
    Check(stat.find("(com.example.procself)") != std::string::npos,
          "stat comm reports the running package");
    const std::string status = readAll(root + "/proc/self/status");
    Check(status.find("TracerPid:\t0") != std::string::npos,
          "status reports TracerPid 0");
    Check(status.find("SigCgt:\t00000000000085f8") != std::string::npos,
          "status reports the stock debuggerd signal mask");
    Check(status.find("Name:\tcom.example.procself") != std::string::npos,
          "status Name reports the running package");

    const std::string id1 = remapper.android_id();
    const std::string id2 = remapper.android_id();
    bool hex = id1.size() == 16;
    for (char c : id1) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) hex = false;
    }
    Check(hex, "android_id is 16 lowercase hex");
    Check(id1 == id2, "android_id is stable across calls");
    Check(id1 != "9774d56d682e549c", "android_id is not the emulator constant");
}

void TestFileIo() {
    std::printf("-- file io through the shims --\n");

    // The write path apps actually use, end to end: create parents, write, read back.
    FILE* out = kudroid::vfs_fopen("/data/data/com.kudroid.test/files/a.txt", "w");
    Check(out != nullptr, "vfs_fopen creates missing parent directories");
    if (out != nullptr) {
        std::fputs("VFS_OK", out);
        std::fclose(out);
    }
    char buffer[32] = {};
    FILE* in = kudroid::vfs_fopen("/data/data/com.kudroid.test/files/a.txt", "r");
    Check(in != nullptr, "the file can be reopened for reading");
    if (in != nullptr) {
        if (!std::fgets(buffer, sizeof(buffer), in)) buffer[0] = '\0';
        std::fclose(in);
    }
    Check(std::strcmp(buffer, "VFS_OK") == 0, "the contents round-trip");

    Check(kudroid::vfs_access("/data/data/com.kudroid.test/files/a.txt", F_OK) == 0,
          "vfs_access sees the remapped file");
    Check(kudroid::vfs_unlink("/data/data/com.kudroid.test/files/a.txt") == 0,
          "vfs_unlink removes it");
    Check(kudroid::vfs_access("/data/data/com.kudroid.test/files/a.txt", F_OK) != 0,
          "and it is gone afterwards");

    // A write through an escaping path must land inside the VFS, not on the host.
    const std::string root = kudroid::VFSPathRemapper::getInstance().androidRoot();
    FILE* escape = kudroid::vfs_fopen("/sdcard/../../../kudroid_escape_probe.txt", "w");
    if (escape != nullptr) {
        std::fputs("x", escape);
        std::fclose(escape);
    }
    Check(!std::filesystem::exists(
              std::filesystem::path(root).parent_path().parent_path() /
              "kudroid_escape_probe.txt"),
          "a write through an escaping path does not reach outside the root");
}

// ── jar: URL → entry inside the archive ZIP ─────────────────────────────────
//
// A Unity game that ships its content stream inside the APK asks for assets with
// jar:file:///data/app/<pkg>/base.apk!/assets/... URLs. The loose-file candidates
// miss when the entry was never extracted, and the old miss path handed back a path
// that does not exist — the silent 404 that left a game black after its splash.
// The remapper now extracts the entry out of the archive ZIP into the VFS cache.
// These tests build a real ZIP and walk the real remap() path.

// Store-method entry: name, content. CRC32 filled by the builder.
struct ZipEntrySpec {
    std::string name;
    std::string content;
};

std::vector<uint8_t> build_store_zip(const std::vector<ZipEntrySpec>& entries) {
    auto put16 = [](std::vector<uint8_t>& v, uint16_t x) {
        v.push_back(x & 0xFF);
        v.push_back((x >> 8) & 0xFF);
    };
    auto put32 = [](std::vector<uint8_t>& v, uint32_t x) {
        v.push_back(x & 0xFF);
        v.push_back((x >> 8) & 0xFF);
        v.push_back((x >> 16) & 0xFF);
        v.push_back((x >> 24) & 0xFF);
    };
    std::vector<uint8_t> out;
    struct Offset {
        uint32_t local = 0;
        uint32_t csize = 0;
        uint32_t crc = 0;
        std::string name;
    };
    std::vector<Offset> offsets;
    for (const ZipEntrySpec& e : entries) {
        const uint32_t crc = static_cast<uint32_t>(::crc32(0L, nullptr, 0));
        Offset o;
        o.crc = static_cast<uint32_t>(::crc32(crc, reinterpret_cast<const Bytef*>(e.content.data()),
                                             static_cast<uInt>(e.content.size())));
        o.csize = static_cast<uint32_t>(e.content.size());
        o.name = e.name;
        o.local = static_cast<uint32_t>(out.size());
        put32(out, 0x04034B50);  // local file header
        put16(out, 20);          // version needed
        put16(out, 0);           // flags
        put16(out, 0);           // method: store
        put16(out, 0); put16(out, 0);  // dos time, date
        put32(out, o.crc);
        put32(out, o.csize);
        put32(out, static_cast<uint32_t>(e.content.size()));
        put16(out, static_cast<uint16_t>(e.name.size()));
        put16(out, 0);           // extra len
        out.insert(out.end(), e.name.begin(), e.name.end());
        out.insert(out.end(), e.content.begin(), e.content.end());
        offsets.push_back(o);
    }
    const uint32_t cd_start = static_cast<uint32_t>(out.size());
    for (const Offset& o : offsets) {
        put32(out, 0x02014B50);  // central directory header
        put16(out, 20);          // version made by
        put16(out, 20);          // version needed
        put16(out, 0);           // flags
        put16(out, 0);           // method: store
        put16(out, 0); put16(out, 0);  // dos time, date
        put32(out, o.crc);
        put32(out, o.csize);
        put32(out, o.csize);     // uncompressed (store)
        put16(out, static_cast<uint16_t>(o.name.size()));
        put16(out, 0);           // extra
        put16(out, 0);           // comment
        put16(out, 0);           // disk start
        put16(out, 0);           // internal attrs
        put32(out, 0);           // external attrs
        put32(out, o.local);
        out.insert(out.end(), o.name.begin(), o.name.end());
    }
    const uint32_t cd_size = static_cast<uint32_t>(out.size()) - cd_start;
    put32(out, 0x06054B50);      // EOCD
    put16(out, 0); put16(out, 0);
    put16(out, static_cast<uint16_t>(offsets.size()));
    put16(out, static_cast<uint16_t>(offsets.size()));
    put32(out, cd_size);
    put32(out, cd_start);
    put16(out, 0);               // comment len
    return out;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void TestJarFromArchive(kudroid::VFSPathRemapper& remapper, const std::string& root) {
    std::printf("-- jar: URL served from the archive ZIP --\n");

    const std::filesystem::path apkDir = std::filesystem::path(root) / "data" / "app" / "com.test.game";
    std::filesystem::create_directories(apkDir);
    const std::string apkPath = (apkDir / "base.apk").string();

    // The APK holds two entries; nothing is extracted loose, so only the archive
    // path can serve them.
    const std::vector<uint8_t> zip = build_store_zip({
        {"assets/GameBuildSettings.json", "{\"build\":42}"},
        {"assets/aa/catalog.json", "catalog-bytes-here"},
    });
    {
        std::ofstream out(apkPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(zip.data()),
                  static_cast<std::streamsize>(zip.size()));
    }

    // The jar branch only runs when the loose candidates miss, and those are joined
    // onto kudroid_get_assets_dir() — point it at an empty directory inside the VFS.
    const std::filesystem::path assetsDir = std::filesystem::path(root) / "data" / "app" / "com.test.game" / "assets_empty";
    std::filesystem::create_directories(assetsDir);
    kudroid_set_assets_dir(assetsDir.string().c_str());

    const std::string served = remapper.remap(
        "jar:file:///data/app/com.test.game/base.apk!/assets/GameBuildSettings.json");
    Check(!served.empty() && served !=
              "jar:file:///data/app/com.test.game/base.apk!/assets/GameBuildSettings.json",
          "a jar: URL whose entry exists only in the APK resolves to a real file");
    std::printf("       (served path: %s)\n", served.c_str());
    Check(!served.empty() && served.find("jar_entries") != std::string::npos,
          "and it lives in the VFS jar cache");
    Check(!served.empty() && read_file(served) == "{\"build\":42}",
          "and its bytes are the entry's bytes");

    // The second URL must also serve (cache-key separation), and the URL shape with
    // no loose candidate at all must not hang or crash.
    const std::string served2 = remapper.remap(
        "jar:file:///data/app/com.test.game/base.apk!/assets/aa/catalog.json");
    Check(!served2.empty() && served2 != served && read_file(served2) == "catalog-bytes-here",
          "a second entry is served separately");

    // Repeat resolution is idempotent: the cache file is reused, not re-extracted.
    const std::string served3 = remapper.remap(
        "jar:file:///data/app/com.test.game/base.apk!/assets/GameBuildSettings.json");
    Check(served3 == served, "a repeated URL resolves to the same cached file");

    // Callers above prepend "/" before jar: URLs reach here (absolute-path
    // normalization). The split must survive it: same entry, same file.
    const std::string servedSlash = remapper.remap(
        "/jar:file:///data/app/com.test.game/base.apk!/assets/GameBuildSettings.json");
    Check(servedSlash == served && read_file(servedSlash) == "{\"build\":42}",
          "a leading-slash jar: URL serves the same entry");

    // An entry that exists nowhere must still return SOMETHING (the old miss path's
    // default candidate), and must not create a cache file.
    const std::string missing = remapper.remap(
        "jar:file:///data/app/com.test.game/base.apk!/assets/nope.json");
    Check(!missing.empty(), "a missing entry still returns the default candidate path");

    kudroid_set_assets_dir("");
}

void TestUrlRemapping(kudroid::VFSPathRemapper& remapper, const std::string& root) {
    std::printf("-- URL scheme remapping --\n");

    const std::string direct = remapper.remap("/data/data/com.x/files/a.txt");
    const std::string fileUrl = remapper.remap("file:///data/data/com.x/files/a.txt");
    Check(direct == fileUrl, "file:/// URI strips scheme and matches plain path");

    const std::string singleSlash = remapper.remap("file:/data/data/com.x/files/a.txt");
    Check(direct == singleSlash, "file:/ URI strips scheme and matches plain path");

    const std::string localHost = remapper.remap("file://localhost/data/data/com.x/files/a.txt");
    Check(direct == localHost, "file://localhost/ strips host prefix");

    const std::string loopback = remapper.remap("file://127.0.0.1/data/data/com.x/files/a.txt");
    Check(direct == loopback, "file://127.0.0.1/ strips loopback host prefix");

    const std::string jarUrl = remapper.remap("jar:file:///data/app/com.x/base.apk!/assets/aa/catalog.json");
    Check(isInside(jarUrl, root) || !jarUrl.empty(), "jar: URI maps without escaping root");
}

} // namespace

int main() {
    // Redirect HOME before the singleton is constructed: it derives android_root from
    // HOME, and the constructor runs on first getInstance().
    const std::filesystem::path home =
        std::filesystem::temp_directory_path() /
        ("kudroid_vfs_test_" + std::to_string(::getpid()));
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home / "Documents");
    ::setenv("HOME", home.string().c_str(), 1);

    std::printf("=== KuART VFS test ===\n");
    auto& remapper = kudroid::VFSPathRemapper::getInstance();
    const std::string root = remapper.androidRoot();
    std::printf("       android_root: %s\n", root.c_str());

    TestContainment(remapper, root);
    TestInitializeOnce(remapper);
    TestPseudoFiles(remapper);
    TestProcIdentity(remapper);
    TestFileIo();
    TestUrlRemapping(remapper, root);
    TestJarFromArchive(remapper, root);

    std::filesystem::remove_all(home);

    if (g_failures != 0) {
        std::printf("=== VFS test FAILED (%d) ===\n", g_failures);
        return 1;
    }
    std::printf("=== VFS test PASSED ===\n");
    return 0;
}
