#pragma once

#include <cstdio>
#include <dirent.h>
#include <string>
#include <sys/stat.h>

namespace kudroid {

class VFSPathRemapper {
public:
    static VFSPathRemapper& getInstance();

    void setDocumentsDirectory(const std::string& documentsDirectory);
    [[nodiscard]] std::string remap(const char* originalPath) const;
    [[nodiscard]] const std::string& androidRoot() const { return androidRoot_; }
    [[nodiscard]] bool initialize();

private:
    VFSPathRemapper();
    std::string documentsDirectory_;
    std::string androidRoot_;
};

int vfs_open(const char* path, int flags, mode_t mode = 0);
FILE* vfs_fopen(const char* path, const char* mode);
int vfs_access(const char* path, int mode);
int vfs_stat(const char* path, struct stat* info);
int vfs_mkdir(const char* path, mode_t mode);
DIR* vfs_opendir(const char* path);

std::string run_vfs_self_test();

} // namespace kudroid

extern "C" void kudroid_run_vfs_self_test(void);
