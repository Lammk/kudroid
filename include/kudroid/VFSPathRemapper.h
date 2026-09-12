#pragma once

#include <cstdio>
#include <dirent.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>

namespace kudroid {

class VFSPathRemapper {
public:
    static VFSPathRemapper& getInstance();

    void setDocumentsDirectory(const std::string& documentsDirectory);
    [[nodiscard]] std::string remap(const char* originalPath) const;
    [[nodiscard]] const std::string& androidRoot() const { return androidRoot_; }

    // Create the Android directory tree and the pseudo-files, once.
    //
    // Repeat calls return the first result without touching the filesystem. This is
    // not an optimisation detail: getInstance() calls this, every vfs_* function calls
    // getInstance(), so without the guard each guest open/stat/fopen rebuilt 24
    // directories and rewrote 30 pseudo-files — 0.7 ms per file operation against 2.4 us
    // for the remap itself, plus continuous writes to files the guest may be reading.
    [[nodiscard]] bool initialize();
    [[nodiscard]] bool init_pseudo_files();

    // Stable per-install ANDROID_ID (16 lowercase hex). Generated once into
    // <androidRoot>/android_id; never the well-known emulator constant.
    [[nodiscard]] std::string android_id();

    // The running app's package for /proc/self/{cmdline,stat}. Pseudo-files
    // saying com.kudroid.app break self-identification (crash reporters,
    // license checks, Unity init); rewritten on the spot, not next init.
    void setPackageName(const std::string& packageName);

private:
    VFSPathRemapper();
    // Does the work initialize() guards. Called with initMutex_ held.
    bool initializeLocked();

    // OBB fallback: guest path /sdcard/Android/obb/<pkg>/<file> whose mapped
    // host file is absent. Scans data/app/<pkg>/ then any staged .obb
    // carrying the package name. Empty when nothing matches (cached).
    [[nodiscard]] std::string resolveObbFallback(const std::string& mapped) const;

    std::string documentsDirectory_;
    std::string androidRoot_;
    std::string packageName_;

    mutable std::mutex initMutex_;
    bool initialized_ = false;
    bool initResult_ = false;

    // OBB fallback cache: guest OBB path -> resolved host path. Filled on
    // first miss so the directory scans below run once per install, not once
    // per open. Mutex-guarded; remap() is const and re-entrant across guest
    // threads.
    mutable std::mutex obbMutex_;
    mutable std::unordered_map<std::string, std::string> obbResolved_;
    mutable bool obbScanned_ = false;
    mutable std::vector<std::string> obbFiles_;  // host paths of known .obb files
};

int vfs_open(const char* path, int flags, mode_t mode = 0);
int vfs_open64(const char* path, int flags, mode_t mode = 0);
FILE* vfs_fopen(const char* path, const char* mode);
FILE* vfs_fopen64(const char* path, const char* mode);
FILE* vfs_freopen(const char* path, const char* mode, FILE* stream);
size_t vfs_fread(void* buf, size_t size, size_t count, FILE* stream);
int vfs_fclose(FILE* stream);
int vfs_fseek(FILE* stream, long offset, int whence);
int vfs_fseeko(FILE* stream, off_t offset, int whence);
long vfs_ftell(FILE* stream);
off_t vfs_ftello(FILE* stream);
int vfs_access(const char* path, int mode);
int vfs_stat(const char* path, void* info);
int vfs_stat64(const char* path, void* info);
int vfs_lstat(const char* path, void* info);
int vfs_lstat64(const char* path, void* info);
int vfs_chmod(const char* path, mode_t mode);
int vfs_chown(const char* path, uid_t owner, gid_t group);
int vfs_unlink(const char* path);
int vfs_remove(const char* path);
int vfs_rename(const char* oldPath, const char* newPath);
int vfs_mkdir(const char* path, mode_t mode);
int vfs_rmdir(const char* path);
DIR* vfs_opendir(const char* path);
struct dirent* vfs_readdir(DIR* directory);
int vfs_closedir(DIR* directory);
ssize_t vfs_readlink(const char* path, char* buffer, size_t size);
char* vfs_realpath(const char* path, char* resolved);

std::string extract_jar_entry_to_cache(const std::string& archivePath,
                                       const std::string& entryName,
                                       const std::string& androidRoot);

bool zip_stat_entry(const std::string& archivePath, const std::string& entry,
                    uint64_t* outOffset, uint64_t* outSize, uint16_t* outMethod);

std::vector<std::string> zip_list_dir_entries(const std::string& archivePath,
                                              const std::string& dirPrefix);

std::string run_vfs_self_test();
std::string run_vfs_extended_test();

} // namespace kudroid

extern "C" void kudroid_run_vfs_self_test(void);
