#include "kudroid/VFSPathRemapper.h"
#include "kudroid/cacert_data.h"

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
#include <sys/socket.h>
#include <sys/mman.h>

namespace kudroid {
namespace {

void vfsLog(const std::string& message) {
    std::fprintf(stderr, "[kudroid_vfs] %s\n", message.c_str());
}

void vfsTrace(const std::string& message) {
#ifdef KUDROID_DEBUG
    std::fprintf(stderr, "[kudroid_vfs] %s\n", message.c_str());
#else
    (void)message;
#endif
}

std::string defaultDocumentsDirectory() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/Documents" : ".";
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
    // tạo các thư mục cơ sở (bố cục giống android).
    for (const auto& relative : {
        "data/data", "data/app", "data/local/tmp", "data/cache",
        "sdcard/Download", "sdcard/Android/data", "sdcard/Android/obb",
        "system", "proc/self", "sys", "mnt", "storage/emulated", "dev",
        "etc", "system/etc", "system/etc/security/cacerts", "system/etc/permissions"
    }) {
        std::filesystem::create_directories(std::filesystem::path(androidRoot_) / relative, error);
        if (error) {
            vfsLog("Failed to create " + androidRoot_ + "/" + relative + ": " + error.message());
            return false;
        }
    }
    
    // tạo liên kết mềm
    auto make_symlink = [&](const char* target, const char* linkpath) {
        std::error_code ec;
        std::filesystem::path fullLink = std::filesystem::path(androidRoot_) / linkpath;
        if (!std::filesystem::exists(fullLink)) {
            std::filesystem::create_directory_symlink(target, fullLink, ec);
        }
    };
    
    make_symlink("../sdcard", "mnt/sdcard");
    make_symlink("../../sdcard", "storage/emulated/0");
    std::filesystem::remove(std::filesystem::path(androidRoot_) / "etc", error);
    make_symlink("system/etc", "etc");
    make_symlink("/dev/fd", "proc/self/fd");

    return init_pseudo_files();
}

bool VFSPathRemapper::init_pseudo_files() {
    const std::string root = androidRoot_;
    const std::pair<const char*, const char*> files[] = {
        {"system/build.prop", "ro.build.version.release=14\nro.build.version.sdk=34\nro.product.model=KuDroid iPhone\nro.product.brand=Apple\nro.product.name=kudroid_arm64\nro.product.cpu.abi=arm64-v8a\n"},
        {"proc/cpuinfo", "Processor\t: AArch64 Processor rev 0 (aarch64)\nBogoMIPS\t: 38.40\nFeatures\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm jscvt fcma lrcpc dcpop sha3 sm3 sm4 asimddp sha512 asimdfhm dit uscat ilrcpc flagm ssbs sb paca pacg dcpodp flagm2 frint\nCPU implementer\t: 0x41\nCPU architecture: 8\nCPU variant\t: 0x1\nCPU part\t: 0xd46\nCPU revision\t: 0\n\nHardware\t: Qualcomm Snapdragon 8 Gen 2\n"},
        {"proc/meminfo", "MemTotal:        8192000 kB\nMemFree:         4096000 kB\nMemAvailable:    6000000 kB\nBuffers:           24576 kB\nCached:          2048000 kB\nSwapCached:            0 kB\nActive:          1024000 kB\nInactive:        1024000 kB\n"},
        {"proc/version", "Linux version 5.15.0-kudroid (clang 17.0.0) #1 SMP PREEMPT 2026\n"},
        {"proc/self/cmdline", "com.kudroid.app\0"},
        {"proc/self/stat", "1 (com.kudroid.app) S 0 1 1 0 -1 4194560 0 0 0 0 0 0 0 0 20 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"},
        {"sys/devices/system/cpu/possible", "0-7\n"},
        {"sys/devices/system/cpu/present", "0-7\n"},
        {"sys/devices/system/cpu/online", "0-7\n"},
        {"sys/class/power_supply/battery/capacity", "100\n"},
        {"sys/class/power_supply/battery/status", "Charging\n"},
        {"sys/class/thermal/thermal_zone0/temp", "35000\n"},
        {"sys/class/thermal/thermal_zone0/type", "tsens_tz_sensor\n"},
        {"system/etc/hosts", "127.0.0.1\tlocalhost\n::1\t\tip6-localhost ip6-loopback\n"},
        {"system/etc/resolv.conf", "nameserver 8.8.8.8\nnameserver 8.8.4.4\n"},
        {"system/etc/permissions/handheld_core_hardware.xml", "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<permissions>\n    <feature name=\"android.hardware.camera\" />\n    <feature name=\"android.hardware.location\" />\n    <feature name=\"android.hardware.sensor.accelerometer\" />\n    <feature name=\"android.hardware.sensor.compass\" />\n</permissions>\n"}
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
        if (!output) continue;
        output.write(current.data(), static_cast<std::streamsize>(current.size()));
        output.close();
        ::chmod(path.c_str(), 0644);
    }
    
    // ghi gói chứng chỉ ca
    std::filesystem::path cacertPath = std::filesystem::path(root) / "system/etc/security/cacerts/cacert.pem";
    if (!std::filesystem::exists(cacertPath)) {
        std::ofstream cacertOut(cacertPath, std::ios::binary);
        if (cacertOut) {
            cacertOut.write(reinterpret_cast<const char*>(cacert_pem), cacert_pem_len);
        }
    }
    
    return true;
}

std::string VFSPathRemapper::remap(const char* originalPath) const {
    if (!originalPath) return {};
    std::string_view original(originalPath);
    
    // ánh xạ trực tiếp các thiết bị /dev/ gốc của máy chủ sang ios
    if (original == "/dev/urandom" || original == "/dev/random" || 
        original == "/dev/null" || original == "/dev/zero") {
        return std::string(original);
    }

    std::string_view prefix;
    std::string_view rootName;
    if (original.find("/data/data/") == 0) { prefix = "/data/data/"; rootName = "data/data/"; }
    else if (original.find("/data/user/0/") == 0) { prefix = "/data/user/0/"; rootName = "data/data/"; }
    else if (original.find("/data/user_de/0/") == 0) { prefix = "/data/user_de/0/"; rootName = "data/data/"; }
    else if (original.find("/sdcard/") == 0) { prefix = "/sdcard/"; rootName = "sdcard/"; }
    else if (original.find("/storage/emulated/0/") == 0) { prefix = "/storage/emulated/0/"; rootName = "sdcard/"; }
    else if (original.find("/mnt/sdcard/") == 0) { prefix = "/mnt/sdcard/"; rootName = "sdcard/"; }
    else if (original.find("/storage/") == 0) { prefix = "/storage/"; rootName = "sdcard/"; }
    else if (original.find("/system/") == 0) { prefix = "/system/"; rootName = "system/"; }
    else if (original.find("/etc/") == 0) { prefix = "/etc/"; rootName = "etc/"; }
    else if (original.find("/proc/self/") == 0) { prefix = "/proc/self/"; rootName = "proc/self/"; }
    else if (original.find("/proc/") == 0) { prefix = "/proc/"; rootName = "proc/"; }
    else if (original.find("/sys/") == 0) { prefix = "/sys/"; rootName = "sys/"; }
    else if (original.find("/data/app/") == 0) { prefix = "/data/app/"; rootName = "data/app/"; }
    else if (original.find("/data/local/tmp/") == 0) { prefix = "/data/local/tmp/"; rootName = "data/local/tmp/"; }
    else if (original.find("/cache/") == 0) { prefix = "/cache/"; rootName = "data/cache/"; }
    else if (original.find("/dev/") == 0) { prefix = "/dev/"; rootName = "dev/"; }
    else {
        if (!original.empty() && original[0] != '/') {
            std::string mapped = androidRoot_ + "/data/local/tmp/" + std::string(original);
            vfsTrace("Remapped relative path: " + std::string(original) + " -> " + mapped);
            return mapped;
        }
        return std::string(original);
    }

    std::string mapped = androidRoot_ + "/" + std::string(rootName) + std::string(original.substr(prefix.size()));
    vfsTrace("Remapped: " + std::string(original) + " -> " + mapped);
    return mapped;
}

int vfs_open(const char* path, int flags, mode_t mode) {
    if (path && (std::strcmp(path, "/dev/binder") == 0 || 
                 std::strcmp(path, "/dev/mali0") == 0 ||
                 std::strcmp(path, "/dev/kgsl-3d0") == 0 ||
                 std::strcmp(path, "/dev/pvrsrvkm") == 0)) {
        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
            ::close(sv[1]);
            vfsTrace(std::string("open mock device (") + path + ") -> " + std::to_string(sv[0]));
            return sv[0];
        }
    }
    
    if (path && std::strcmp(path, "/dev/ashmem") == 0) {
        static int ashmem_counter = 0;
        char name[64];
        std::snprintf(name, sizeof(name), "/kudroid_ashmem_%d_%d", ::getpid(), ashmem_counter++);
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            vfsTrace(std::string("open mock ashmem (") + name + ") -> " + std::to_string(fd));
            return fd;
        }
    }

    if (path && std::strcmp(path, "/proc/self/maps") == 0) {
        std::string mapsPath = VFSPathRemapper::getInstance().remap("/proc/self/maps");
        std::ofstream mapsFile(mapsPath, std::ios::trunc);
        if (mapsFile) {
            // ghi bố cục bản đồ giả thỏa mãn các kiểm tra cơ bản
            mapsFile << "5500000000-5500100000 r-xp 00000000 103:02 12345 /system/bin/app_process64\n";
            mapsFile << "5500100000-5500110000 r--p 00100000 103:02 12345 /system/bin/app_process64\n";
            mapsFile << "5500110000-5500120000 rw-p 00110000 103:02 12345 /system/bin/app_process64\n";
            mapsFile << "7f00000000-7f00100000 rw-p 00000000 00:00 0 [stack]\n";
            mapsFile.close();
        }
    }

    if (path && std::strcmp(path, "/dev/__properties__") == 0) {
        std::string propPath = VFSPathRemapper::getInstance().remap("/dev/__properties__");
        if (!std::filesystem::exists(propPath)) {
            std::ofstream propFile(propPath, std::ios::trunc);
            propFile << "ro.build.version.sdk=34\nro.product.cpu.abi=arm64-v8a\n";
            propFile.close();
        }
    }

    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    // đối với o_creat, đảm bảo thư mục gốc tồn tại (tránh sự cố enoent).
    if (flags & O_CREAT) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(mapped).parent_path(), ec);
    }
    const int result = (flags & O_CREAT) ? ::open(mapped.c_str(), flags, mode)
                                         : ::open(mapped.c_str(), flags);
    vfsTrace("open(" + mapped + ") -> " + std::to_string(result));
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
            vfsTrace("Cannot create parent directory for " + mapped + ": " + error.message());
            return nullptr;
        }
    }
    FILE* result = std::fopen(mapped.c_str(), mode);
    vfsTrace("fopen(" + mapped + ", " + (mode ? mode : "<null>") + ") -> " +
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


#undef st_atime
#undef st_mtime
#undef st_ctime
#undef st_atime_nsec
#undef st_mtime_nsec
#undef st_ctime_nsec

struct android_stat {
  unsigned long st_dev;
  unsigned long st_ino;
  unsigned int st_mode;
  unsigned int st_nlink;
  unsigned int st_uid;
  unsigned int st_gid;
  unsigned long st_rdev;
  unsigned long __pad1;
  long st_size;
  int st_blksize;
  int __pad2;
  long st_blocks;
  long st_atime;
  unsigned long st_atime_nsec;
  long st_mtime;
  unsigned long st_mtime_nsec;
  long st_ctime;
  unsigned long st_ctime_nsec;
  unsigned int __unused4;
  unsigned int __unused5;
};

static void copy_stat(struct android_stat* dst, const struct stat* src) {
    std::memset(dst, 0, sizeof(struct android_stat));
    dst->st_dev = src->st_dev;
    dst->st_ino = src->st_ino;
    dst->st_mode = src->st_mode;
    dst->st_nlink = src->st_nlink;
    dst->st_uid = src->st_uid;
    dst->st_gid = src->st_gid;
    dst->st_rdev = src->st_rdev;
    dst->st_size = src->st_size;
    dst->st_blksize = src->st_blksize;
    dst->st_blocks = src->st_blocks;
    
#ifdef __APPLE__
    dst->st_atime = src->st_atimespec.tv_sec;
    dst->st_atime_nsec = src->st_atimespec.tv_nsec;
    dst->st_mtime = src->st_mtimespec.tv_sec;
    dst->st_mtime_nsec = src->st_mtimespec.tv_nsec;
    dst->st_ctime = src->st_ctimespec.tv_sec;
    dst->st_ctime_nsec = src->st_ctimespec.tv_nsec;
#else
    dst->st_atime = src->st_atim.tv_sec;
    dst->st_atime_nsec = src->st_atim.tv_nsec;
    dst->st_mtime = src->st_mtim.tv_sec;
    dst->st_mtime_nsec = src->st_mtim.tv_nsec;
    dst->st_ctime = src->st_ctim.tv_sec;
    dst->st_ctime_nsec = src->st_ctim.tv_nsec;
#endif
}

int vfs_stat(const char* path, void* info) {
    // đảm bảo các tệp giả tồn tại trước khi stat (ví dụ: /proc/self/maps).
    if (path && std::strcmp(path, "/proc/self/maps") == 0) {
        std::string mapsPath = VFSPathRemapper::getInstance().remap("/proc/self/maps");
        if (!std::filesystem::exists(mapsPath)) {
            std::ofstream mapsFile(mapsPath, std::ios::trunc);
            if (mapsFile) {
                mapsFile << "5500000000-5500100000 r-xp 00000000 103:02 12345 /system/bin/app_process64\n";
                mapsFile << "5500100000-5500110000 r--p 00100000 103:02 12345 /system/bin/app_process64\n";
                mapsFile << "5500110000-5500120000 rw-p 00110000 103:02 12345 /system/bin/app_process64\n";
                mapsFile << "7f00000000-7f00100000 rw-p 00000000 00:00 0 [stack]\n";
                mapsFile.close();
            }
        }
    }
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    struct stat host_st;
    int res = ::stat(mapped.c_str(), &host_st);
    if (res == 0) copy_stat((struct android_stat*)info, &host_st);
    return res;
}

int vfs_stat64(const char* path, void* info) { return vfs_stat(path, info); }

int vfs_lstat(const char* path, void* info) {
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    struct stat host_st;
    int res = ::lstat(mapped.c_str(), &host_st);
    if (res == 0) copy_stat((struct android_stat*)info, &host_st);
    return res;
}

int vfs_lstat64(const char* path, void* info) { return vfs_lstat(path, info); }

extern "C" int vfs_fstat(int fd, void* info) {
    struct stat host_st;
    int res = ::fstat(fd, &host_st);
    if (res == 0) copy_stat((struct android_stat*)info, &host_st);
    return res;
}
extern "C" int vfs_fstat64(int fd, void* info) { return vfs_fstat(fd, info); }


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
