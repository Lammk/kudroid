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

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include <unistd.h>

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

    for (const char* relative : {"proc/cpuinfo", "proc/meminfo", "proc/self/cmdline",
                                 "system/etc/hosts", "proc/mounts"}) {
        Check(std::filesystem::exists(std::filesystem::path(root) / relative),
              std::string("pseudo file present: ") + relative);
    }
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
    TestFileIo();

    std::filesystem::remove_all(home);

    if (g_failures != 0) {
        std::printf("=== VFS test FAILED (%d) ===\n", g_failures);
        return 1;
    }
    std::printf("=== VFS test PASSED ===\n");
    return 0;
}
