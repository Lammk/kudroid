#include "kudroid/VFSPathRemapper.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <limits.h>
#include <sstream>
#include <unistd.h>
#include <vector>

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
    for (const auto& relative : {"data/data", "sdcard/Download", "system", "proc/self", "sys"}) {
        std::filesystem::create_directories(std::filesystem::path(androidRoot_) / relative, error);
        if (error) {
            vfsLog("Failed to create " + androidRoot_ + "/" + relative + ": " + error.message());
            return false;
        }
    }
    return init_pseudo_files();
}

bool VFSPathRemapper::init_pseudo_files() {
    const std::string root = androidRoot_;
    const std::pair<const char*, const char*> files[] = {
        {"system/build.prop", "ro.build.version.release=14\nro.build.version.sdk=34\nro.product.model=KuDroid iPhone\nro.product.brand=Apple\nro.product.name=kudroid_arm64\nro.product.cpu.abi=arm64-v8a\n"},
        {"proc/cpuinfo", "Processor : AArch64 Processor rev 0 (aarch64)\nhardware : Apple Silicon ARM64\nFeatures : fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics\nCPU implementer : 0x61\n"},
        {"proc/meminfo", "MemTotal: 8192000 kB\nMemFree: 4096000 kB\nMemAvailable: 6000000 kB\n"},
        {"proc/version", "Linux version 5.15.0-kudroid (clang 17.0.0) #1 SMP PREEMPT 2026\n"},
        {"proc/self/cmdline", "com.kudroid.app\0"}
    };
    for (const auto& file : files) {
        const std::filesystem::path path = std::filesystem::path(root) / file.first;
        std::string current;
        if (std::ifstream input(path, std::ios::binary); input) {
            current.assign((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
        }
        const std::string required(file.second);
        if (current.empty()) {
            current = required;
        } else {
            std::istringstream existing(current);
            std::string line;
            std::vector<std::string> lines;
            while (std::getline(existing, line)) lines.push_back(line);
            std::istringstream defaults(required);
            while (std::getline(defaults, line)) {
                if (line.empty() || current.find(line) != std::string::npos) continue;
                lines.push_back(line);
            }
            current.clear();
            for (const auto& item : lines) current += item + "\n";
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(current.data(), static_cast<std::streamsize>(current.size()));
        output.close();
        ::chmod(path.c_str(), 0644);
    }
    return true;
}

std::string VFSPathRemapper::remap(const char* originalPath) const {
    if (!originalPath) return {};
    const std::string original(originalPath);
    std::string prefix;
    if (startsWith(original, "/data/data/")) prefix = "/data/data/";
    else if (startsWith(original, "/data/user/0/")) prefix = "/data/user/0/";
    else if (startsWith(original, "/sdcard/")) prefix = "/sdcard/";
    else if (startsWith(original, "/storage/emulated/0/")) prefix = "/storage/emulated/0/";
    else if (startsWith(original, "/mnt/sdcard/")) prefix = "/mnt/sdcard/";
    else if (startsWith(original, "/storage/")) prefix = "/storage/";
    else if (startsWith(original, "/system/")) prefix = "/system/";
    else if (startsWith(original, "/proc/self/")) prefix = "/proc/self/";
    else if (startsWith(original, "/proc/")) prefix = "/proc/";
    else if (startsWith(original, "/sys/")) prefix = "/sys/";
    else return original;

    std::string rootName = startsWith(prefix, "/data/") ? "data/data/" :
                           startsWith(prefix, "/system/") ? "system/" :
                           startsWith(prefix, "/proc/self/") ? "proc/self/" :
                           startsWith(prefix, "/proc/") ? "proc/" :
                           startsWith(prefix, "/sys/") ? "sys/" : "sdcard/";
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

int vfs_open64(const char* path, int flags, mode_t mode) { return vfs_open(path, flags, mode); }

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

FILE* vfs_fopen64(const char* path, const char* mode) { return vfs_fopen(path, mode); }

FILE* vfs_freopen(const char* path, const char* mode, FILE* stream) {
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    return std::freopen(mapped.c_str(), mode, stream);
}

int vfs_access(const char* path, int mode) {
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    return ::access(mapped.c_str(), mode);
}

int vfs_stat(const char* path, struct stat* info) {
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    return ::stat(mapped.c_str(), info);
}

int vfs_stat64(const char* path, struct stat* info) { return vfs_stat(path, info); }
int vfs_lstat(const char* path, struct stat* info) {
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    return ::lstat(mapped.c_str(), info);
}
int vfs_lstat64(const char* path, struct stat* info) { return vfs_lstat(path, info); }
int vfs_chmod(const char* path, mode_t mode) {
    return ::chmod(VFSPathRemapper::getInstance().remap(path).c_str(), mode);
}
int vfs_chown(const char* path, uid_t owner, gid_t group) {
    return ::chown(VFSPathRemapper::getInstance().remap(path).c_str(), owner, group);
}
int vfs_unlink(const char* path) { return ::unlink(VFSPathRemapper::getInstance().remap(path).c_str()); }
int vfs_remove(const char* path) { return std::remove(VFSPathRemapper::getInstance().remap(path).c_str()); }
int vfs_rename(const char* oldPath, const char* newPath) {
    const auto& remapper = VFSPathRemapper::getInstance();
    return std::rename(remapper.remap(oldPath).c_str(), remapper.remap(newPath).c_str());
}

int vfs_mkdir(const char* path, mode_t mode) {
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    return ::mkdir(mapped.c_str(), mode);
}

int vfs_rmdir(const char* path) { return ::rmdir(VFSPathRemapper::getInstance().remap(path).c_str()); }

DIR* vfs_opendir(const char* path) {
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    return ::opendir(mapped.c_str());
}
struct dirent* vfs_readdir(DIR* directory) { return ::readdir(directory); }
int vfs_closedir(DIR* directory) { return ::closedir(directory); }
ssize_t vfs_readlink(const char* path, char* buffer, size_t size) {
    return ::readlink(VFSPathRemapper::getInstance().remap(path).c_str(), buffer, size);
}
char* vfs_realpath(const char* path, char* resolved) {
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    return ::realpath(mapped.c_str(), resolved);
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
    if (!std::fgets(buffer, sizeof(buffer), input)) {
        buffer[0] = '\0';
    }
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

std::string run_vfs_extended_test() {
    std::string log = "[kudroid_core] ===== VFS Extended Self-Test =====\n";
    auto& remapper = VFSPathRemapper::getInstance();
    (void)remapper.init_pseudo_files();
    auto result = [&](int number, const char* name, bool pass, const std::string& detail) {
        log += "[kudroid_vfs_ext] [" + std::string(pass ? "PASS" : "FAIL") + "] Test " +
               std::to_string(number) + ": " + name + " (" + detail + ")\n";
    };

    struct stat info = {};
    bool statPass = vfs_stat64("/system/build.prop", &info) == 0 && info.st_size > 0;
    std::string buildProp;
    if (FILE* file = vfs_fopen64("/system/build.prop", "r")) {
        char buffer[512];
        while (std::fgets(buffer, sizeof(buffer), file)) buildProp += buffer;
        std::fclose(file);
    }
    statPass = statPass && buildProp.find("ro.build.version.sdk=34") != std::string::npos &&
               buildProp.find("ro.product.cpu.abi=arm64-v8a") != std::string::npos;
    result(1, "stat64 + build.prop keys", statPass, statPass ? "required keys verified" : "metadata/key verification failed");

    const std::string storage = remapper.remap("/storage/emulated/0/Download");
    char resolved[PATH_MAX] = {};
    const bool realpathPass = vfs_mkdir("/storage/emulated/0/Download", 0755) == 0 || errno == EEXIST;
    const bool resolvedPass = realpathPass && vfs_realpath("/storage/emulated/0/Download", resolved) != nullptr;
    const std::string linkPath = storage + "/kudroid_storage_link";
    ::unlink(linkPath.c_str());
    const bool linkCreated = ::symlink(".", linkPath.c_str()) == 0;
    char linkTarget[64] = {};
    const ssize_t linkLength = vfs_readlink("/storage/emulated/0/Download/kudroid_storage_link",
                                            linkTarget, sizeof(linkTarget) - 1);
    const bool linkPass = linkCreated && linkLength == 1 && linkTarget[0] == '.';
    ::unlink(linkPath.c_str());
    result(2, "storage readlink + realpath", resolvedPass && linkPass, storage);

    bool foundEntry = false;
    DIR* directory = vfs_opendir("/sdcard/");
    if (directory) {
        while (struct dirent* entry = vfs_readdir(directory)) {
            if (std::strcmp(entry->d_name, "Download") == 0) foundEntry = true;
        }
        vfs_closedir(directory);
    }
    result(3, "sdcard opendir/readdir", foundEntry, foundEntry ? "Download found" : "Download missing");

    FILE* nullFile = vfs_fopen("/dev/null", "w");
    FILE* randomFile = vfs_fopen("/dev/urandom", "r");
    unsigned char randomByte = 0;
    const bool devicePass = nullFile && randomFile && std::fread(&randomByte, 1, 1, randomFile) == 1;
    if (nullFile) std::fclose(nullFile);
    if (randomFile) std::fclose(randomFile);
    result(4, "native /dev/null and /dev/urandom", devicePass, devicePass ? "native paths preserved" : std::strerror(errno));
    return log;
}

} // namespace kudroid

extern "C" void kudroid_run_vfs_self_test(void) {
    std::fprintf(stdout, "%s", kudroid::run_vfs_self_test().c_str());
}
