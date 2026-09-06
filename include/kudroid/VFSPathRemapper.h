#pragma once

#include <cstdio>
#include <dirent.h>
#include <mutex>
#include <string>
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

private:
    VFSPathRemapper();
    // Does the work initialize() guards. Called with initMutex_ held.
    bool initializeLocked();

    std::string documentsDirectory_;
    std::string androidRoot_;

    mutable std::mutex initMutex_;
    bool initialized_ = false;
    bool initResult_ = false;
};

int vfs_open(const char* path, int flags, mode_t mode = 0);
int vfs_open64(const char* path, int flags, mode_t mode = 0);
FILE* vfs_fopen(const char* path, const char* mode);
FILE* vfs_fopen64(const char* path, const char* mode);
FILE* vfs_freopen(const char* path, const char* mode, FILE* stream);
size_t vfs_fread(void* buf, size_t size, size_t count, FILE* stream);
int vfs_fclose(FILE* stream);
int vfs_fseek(FILE* stream, long offset, int whence);
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

std::string run_vfs_self_test();
std::string run_vfs_extended_test();

} // namespace kudroid

extern "C" void kudroid_run_vfs_self_test(void);
