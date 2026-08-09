#include "kudroid/VFSPathRemapper.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <sstream>
#include <unistd.h>

namespace kudroid {
namespace {

void vfsLog(const std::string& message) {
    std::fprintf(stderr, "[kudroid_vfs] %s\n", message.c_str());
}

std::string defaultDocumentsDirectory() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/Documents" : ".";
}

bool startsWith(const std::string& value, const char* prefix) {
    return value.rfind(prefix, 0) == 0;
}

} // namespace

VFSPathRemapper& VFSPathRemapper::getInstance() {
    static VFSPathRemapper instance;
    (void)instance.initialize();
    return instance;
}

VFSPathRemapper::VFSPathRemapper()
    : documentsDirectory_(defaultDocumentsDirectory()),
      androidRoot_(documentsDirectory_ + "/android_root") {}

void VFSPathRemapper::setDocumentsDirectory(const std::string& documentsDirectory) {
    documentsDirectory_ = documentsDirectory;
    androidRoot_ = documentsDirectory_ + "/android_root";
    (void)initialize();
}

bool VFSPathRemapper::initialize() {
    std::error_code error;
    for (const auto& relative : {"data/data", "sdcard/Download", "system", "proc"}) {
        std::filesystem::create_directories(std::filesystem::path(androidRoot_) / relative, error);
        if (error) {
            vfsLog("Failed to create " + androidRoot_ + "/" + relative + ": " + error.message());
            return false;
        }
    }
    return true;
}

std::string VFSPathRemapper::remap(const char* originalPath) const {
    if (!originalPath) return {};
    const std::string original(originalPath);
    std::string prefix;
    if (startsWith(original, "/data/")) prefix = "/data/";
    else if (startsWith(original, "/sdcard/")) prefix = "/sdcard/";
    else if (startsWith(original, "/storage/")) prefix = "/storage/";
    else if (startsWith(original, "/system/")) prefix = "/system/";
    else if (startsWith(original, "/proc/")) prefix = "/proc/";
    else return original;

    std::string rootName = prefix == "/data/" ? "data/" :
                           prefix == "/system/" ? "system/" :
                           prefix == "/proc/" ? "proc/" : "sdcard/";
    const std::string mapped = androidRoot_ + "/" + rootName + original.substr(prefix.size());
    vfsLog("Remapped: " + original + " -> " + mapped);
    return mapped;
}

int vfs_open(const char* path, int flags, mode_t mode) {
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    const int result = (flags & O_CREAT) ? ::open(mapped.c_str(), flags, mode)
                                         : ::open(mapped.c_str(), flags);
    vfsLog("open(" + mapped + ") -> " + std::to_string(result));
    return result;
}

FILE* vfs_fopen(const char* path, const char* mode) {
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    if (mode && (std::strchr(mode, 'w') || std::strchr(mode, 'a'))) {
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(mapped).parent_path(), error);
        if (error) {
            errno = EIO;
            vfsLog("Cannot create parent directory for " + mapped + ": " + error.message());
            return nullptr;
        }
    }
    FILE* result = std::fopen(mapped.c_str(), mode);
    vfsLog("fopen(" + mapped + ", " + (mode ? mode : "<null>") + ") -> " +
           (result ? "OK" : std::strerror(errno)));
    return result;
}

int vfs_access(const char* path, int mode) {
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    return ::access(mapped.c_str(), mode);
}

int vfs_stat(const char* path, struct stat* info) {
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    return ::stat(mapped.c_str(), info);
}

int vfs_mkdir(const char* path, mode_t mode) {
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    return ::mkdir(mapped.c_str(), mode);
}

DIR* vfs_opendir(const char* path) {
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    return ::opendir(mapped.c_str());
}

namespace {

bool writeAndRead(const char* virtualPath, const char* expected, std::string& detail) {
    FILE* output = vfs_fopen(virtualPath, "w");
    if (!output) {
        detail = std::strerror(errno);
        return false;
    }
    std::fputs(expected, output);
    std::fclose(output);

    FILE* input = vfs_fopen(virtualPath, "r");
    if (!input) {
        detail = std::strerror(errno);
        return false;
    }
    char buffer[256] = {};
    std::fgets(buffer, sizeof(buffer), input);
    std::fclose(input);
    if (std::string(buffer) != expected) {
        detail = "content mismatch: " + std::string(buffer);
        return false;
    }
    return true;
}

void appendResult(std::string& log, int number, const char* name, bool passed,
                  const std::string& detail) {
    log += "[kudroid_vfs_test] [" + std::string(passed ? "PASS" : "FAIL") + "] Test " +
           std::to_string(number) + ": " + name;
    if (!detail.empty()) log += " (" + detail + ")";
    log += "\n";
}

} // namespace

std::string run_vfs_self_test() {
    std::string log = "[kudroid_core] ===== VFS Self-Test =====\n";
    auto& remapper = VFSPathRemapper::getInstance();
    log += "[kudroid_vfs_test] Android root: " + remapper.androidRoot() + "\n";
    log += "[kudroid_vfs_test] Creating Android directory tree...\n";
    log += remapper.initialize() ? "[kudroid_vfs_test] Directory tree: OK\n"
                                 : "[kudroid_vfs_test] Directory tree: FAIL\n";

    std::string detail;
    bool passed = writeAndRead("/data/data/com.kudroid.test/files/test_data.txt",
                               "VFS_DATA_OK", detail);
    appendResult(log, 1, "Data Path Redirect & I/O", passed, detail);

    detail.clear();
    passed = writeAndRead("/sdcard/Download/test_sdcard.txt", "VFS_SDCARD_OK", detail);
    appendResult(log, 2, "SDCard Path Redirect & I/O", passed, detail);

    const std::string systemPath = remapper.androidRoot() + "/system/build.prop";
    const std::string procPath = remapper.androidRoot() + "/proc/cpuinfo";
    {
        std::ofstream system(systemPath);
        system << "ro.build.version.release=14\n";
        std::ofstream proc(procPath);
        proc << "Hardware: Apple Silicon ARM64\n";
    }

    detail.clear();
    passed = writeAndRead("/system/build.prop", "ro.build.version.release=14\n", detail);
    appendResult(log, 3, "System Fake build.prop", passed, detail);

    detail.clear();
    passed = writeAndRead("/proc/cpuinfo", "Hardware: Apple Silicon ARM64\n", detail);
    appendResult(log, 4, "Proc Fake cpuinfo", passed, detail);
    return log;
}

} // namespace kudroid

extern "C" void kudroid_run_vfs_self_test(void) {
    std::fprintf(stdout, "%s", kudroid::run_vfs_self_test().c_str());
}
