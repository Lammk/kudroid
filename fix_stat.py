import re

with open('src/VFSPathRemapper.cpp', 'r') as f:
    content = f.read()

stat_impl = """
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
    dst->st_atime = src->st_atimespec.tv_sec;
    dst->st_atime_nsec = src->st_atimespec.tv_nsec;
    dst->st_mtime = src->st_mtimespec.tv_sec;
    dst->st_mtime_nsec = src->st_mtimespec.tv_nsec;
    dst->st_ctime = src->st_ctimespec.tv_sec;
    dst->st_ctime_nsec = src->st_ctimespec.tv_nsec;
}

int vfs_stat(const char* path, void* info) {
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

"""

# Replace the original stat implementations
content = re.sub(r'int vfs_stat\(const char\* path, struct stat\* info\).*?int vfs_lstat64\(const char\* path, struct stat\* info\) \{ return vfs_lstat\(path, info\); \}', stat_impl, content, flags=re.DOTALL)

with open('src/VFSPathRemapper.cpp', 'w') as f:
    f.write(content)
