#pragma once

#include <cstdio>
#include <dirent.h>
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
    [[nodiscard]] bool initialize();
    [[nodiscard]] bool init_pseudo_files();

private:
    VFSPathRemapper();
    std::string documentsDirectory_;
    std::string androidRoot_;
};

int vfs_open(const char* path, int flags, mode_t mode = 0);
int vfs_open64(const char* path, int flags, mode_t mode = 0);
FILE* vfs_fopen(const char* path, const char* mode);
FILE* vfs_fopen64(const char* path, const char* mode);
FILE* vfs_freopen(const char* path, const char* mode, FILE* stream);
int vfs_access(const char* path, int mode);
int vfs_stat(const char* path, struct stat* info);
int vfs_stat64(const char* path, struct stat* info);
int vfs_lstat(const char* path, struct stat* info);
int vfs_lstat64(const char* path, struct stat* info);
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
