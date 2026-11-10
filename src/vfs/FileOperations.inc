FILE* vfs_freopen(const char* path, const char* mode, FILE* stream) {
    const void* const caller = __builtin_return_address(0);
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    FILE* result = std::freopen(mapped.c_str(), mode, stream);
    // Keep the volume map truthful: freopen reuses the FILE* address for a
    // different path, and stale attribution is worse than none.
    if (result != nullptr) {
        std::lock_guard<std::mutex> vlock(g_freadVolMtx);
        g_freadPaths[result] = mapped;
        if (path != nullptr) {
            const size_t len = std::strlen(path);
            if (len >= 8 && std::strcmp(path + len - 8, "base.apk") == 0) {
                track_apk_stream(result);
            }
        }
    }
    if (audio_trace_take(-1)) {
        ktraceLine("[KuDroidVFS] AUDIO freopen(%s) -> %s%s\n", path ? path : "?",
                     result ? "OK" : std::strerror(errno), trace_caller_text(caller));
    }
    return result;
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
    // make sure dummy files exist before stat (e.g. /proc/self/maps).
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
    if (res == 0) {
        copy_stat((struct android_stat*)info, &host_st);
        // FMOD's fd path sizes its clip math from st_size on this same fd kind:
        // a raw base.apk fd makes every clip size off by the whole-file offset
        // (observed: an 884832-byte entry sized 889515831 minus its payload
        // offset). Name the question and the answer on the audio thread — the
        // AUDIO lines are the only window onto FMOD's own probe handles.
        static std::atomic<int> s_apkFstat{0};
        bool apkFd = false;
        {
            std::lock_guard<std::mutex> lock(g_apkStreamsMtx);
            for (int i = 0; i < kTrackedStreams && !apkFd; ++i) {
                apkFd = g_apkStreamFd[i].load(std::memory_order_relaxed) == fd;
            }
        }
        if (apkFd && s_apkFstat.load(std::memory_order_relaxed) < 80) {
            s_apkFstat.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr, "%s[KuDroidFd] AUDIO fstat fd=%d size=%lld\n",
                         kudroid_trace_stamp(), fd,
                         static_cast<long long>(host_st.st_size));
        }
    }
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
// rename(2) on paths that are already remapped. POSIX replaces an existing destination
// atomically, so nothing is deleted first: a failed move leaves the previous file in place.
// Only what rename cannot do itself is handled:
//  - a destination whose parent directory is missing (the source exists, so it is the parent);
//  - a move across file systems (EXDEV): the copy lands beside the destination and is renamed
//    into place, so the destination is never seen half written.
int vfs_rename_resolved(const char* src, const char* dst) {
    if (std::rename(src, dst) == 0) return 0;
    const int err = errno;
    struct stat st;
    if (::lstat(src, &st) != 0) {
        // Nothing to move: the destination stays untouched and no directory is created.
        errno = err;
        return -1;
    }
    if (err == ENOENT) {
        const std::filesystem::path parent = std::filesystem::path(dst).parent_path();
        std::error_code ec;
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
        if (!parent.empty() && !ec && std::rename(src, dst) == 0) return 0;
        errno = err;
        return -1;
    }
    if (err == EXDEV && S_ISREG(st.st_mode)) {
        const std::string staged = std::string(dst) + ".kudroid-move";
        std::error_code ec;
        std::filesystem::copy_file(src, staged, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec && std::rename(staged.c_str(), dst) == 0) {
            ::unlink(src);
            return 0;
        }
        ::unlink(staged.c_str());
    }
    errno = err;
    return -1;
}

int vfs_rename(const char* oldPath, const char* newPath) {
    const auto& remapper = VFSPathRemapper::getInstance();
    return vfs_rename_resolved(remapper.remap(oldPath).c_str(), remapper.remap(newPath).c_str());
}

int vfs_mkdir(const char* path, mode_t mode) {
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    return ::mkdir(mapped.c_str(), mode);
}

int vfs_rmdir(const char* path) { return ::rmdir(VFSPathRemapper::getInstance().remap(path).c_str()); }

DIR* vfs_opendir(const char* path) {
    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    DIR* directory = ::opendir(mapped.c_str());
    static std::atomic<unsigned> samples{0};
    if (samples.fetch_add(1, std::memory_order_relaxed) < 24) {
        ktraceLine("[KuDroidVFS] directory-open path=%s mapped=%s dir=%p errno=%d\n",
                   path ? path : "(null)", mapped.c_str(), directory, directory ? 0 : errno);
    }
    return directory;
}

struct bionic_dirent_layout {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[256];
};

#if defined(__APPLE__)
namespace {
// One translated entry buffer per open DIR (see vfs_readdir). Erased on close.
std::mutex g_readdir_mtx;
std::map<DIR*, bionic_dirent_layout> g_readdir_entries;
}  // namespace
#endif

struct dirent* vfs_readdir(DIR* directory) {
    if (!directory) return nullptr;
    struct dirent* host_entry = ::readdir(directory);
    static std::atomic<unsigned> samples{0};
    if (samples.fetch_add(1, std::memory_order_relaxed) < 48) {
        ktraceLine("[KuDroidVFS] directory-entry dir=%p name=%s type=%u\n", directory,
                   host_entry ? host_entry->d_name : "(end)", host_entry ? unsigned(host_entry->d_type) : 0);
    }
    if (!host_entry) return nullptr;

#if defined(__APPLE__)
    // Not a thread-local singleton: nested or interleaved enumeration of two
    // directories clobbered the first name before the guest copied it (valid
    // return, garbage d_name, empty scans).
    std::lock_guard<std::mutex> lock(g_readdir_mtx);
    bionic_dirent_layout& bionic_entry = g_readdir_entries[directory];
    std::memset(&bionic_entry, 0, sizeof(bionic_entry));
    bionic_entry.d_ino = static_cast<uint64_t>(host_entry->d_fileno);
    bionic_entry.d_off = static_cast<int64_t>(host_entry->d_seekoff);
    bionic_entry.d_reclen = static_cast<uint16_t>(sizeof(bionic_dirent_layout));
    bionic_entry.d_type = static_cast<uint8_t>(host_entry->d_type);
    std::strncpy(bionic_entry.d_name, host_entry->d_name, sizeof(bionic_entry.d_name) - 1);
    return reinterpret_cast<struct dirent*>(&bionic_entry);
#else
    return host_entry;
#endif
}
int vfs_closedir(DIR* directory) {
    const int rc = ::closedir(directory);
#if defined(__APPLE__)
    if (rc == 0) {
        std::lock_guard<std::mutex> lock(g_readdir_mtx);
        g_readdir_entries.erase(directory);
    }
#endif
    return rc;
}
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
    statPass = statPass &&
               buildProp.find("ro.build.version.sdk=" KUDROID_SDK_INT_STR) != std::string::npos &&
               buildProp.find("ro.product.cpu.abi=" KUDROID_DEVICE_ABI) != std::string::npos;
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

// ── FSB5 blob tracker diagnostics ─────────────────────────────────────────────
//
// What the guest actually asked for versus what reached it. The tracker can only
// say "this blob's declared bytes were all served" once every read on its archive
// is accounted; the counter of unaccounted reads is the number that separates "the
// guest read the wrong stream" from "the stream was right and the guest gave up".
//
// Two counters, because the two failures look identical from outside: `unmatched`
// is a read that landed on no blob at all, and `unread` is a blob whose bytes the
// guest never asked for. A slice the guest rejects after a handful of bytes ends
// with `unread`, not with `unmatched`.
// Call with g_freadVolMtx held.
FsbArchiveStats fsb_archive_stats_locked(const std::string& path) {
    FsbArchiveStats out;
    auto wit = g_fsbWindows.find(path);
    if (wit == g_fsbWindows.end()) return out;
    for (const auto& w : wit->second) {
        ++out.windows;
        const uint64_t blocks = (w.expected + kFsbBlock - 1) / kFsbBlock;
        if (w.blocks >= blocks) {
            ++out.complete;
        } else {
            ++out.unread;
            if (out.first_unread_off < 0) {
                for (uint64_t b = 0; b < blocks; ++b) {
                    const uint64_t word = (static_cast<std::size_t>(b >> 6) < w.covered.size())
                                              ? w.covered[b >> 6]
                                              : 0;
                    if (!(word & (1ull << (b & 63)))) {
                        out.first_unread_off =
                            static_cast<long long>(w.start) + static_cast<long long>(b * kFsbBlock);
                        break;
                    }
                }
            }
        }
    }
    return out;
}

// Unmatched reads since the last snapshot, keyed by archive.
std::mutex g_fsbUnmatchedMtx;
std::map<std::string, std::pair<uint64_t, uint64_t>> g_fsbUnmatched;  // path -> (reads, bytes)

void vfs_fsb_note_unmatched(const char* archivePath, size_t bytes) {
    if (!archivePath) return;
    std::lock_guard<std::mutex> lock(g_fsbUnmatchedMtx);
    auto& e = g_fsbUnmatched[archivePath];
    ++e.first;
    e.second += bytes;
}

std::string vfs_fsb_stats(const char* archivePath, int snapshot) {
    if (!archivePath) return {};
    std::string out;
    {
        std::lock_guard<std::mutex> lock(g_freadVolMtx);
        const FsbArchiveStats s = fsb_archive_stats_locked(archivePath);
        char line[256];
        std::snprintf(line, sizeof(line),
                      "windows=%d complete=%d unread=%d first_unread_off=%lld",
                      s.windows, s.complete, s.unread, s.first_unread_off);
        out = line;
    }
    std::lock_guard<std::mutex> lock(g_fsbUnmatchedMtx);
    auto it = g_fsbUnmatched.find(archivePath);
    if (it == g_fsbUnmatched.end()) return out;
    if (!snapshot) return out;
    out += " unmatched_reads=" + std::to_string(it->second.first) +
           " unmatched_bytes=" + std::to_string(it->second.second);
    it->second = {0, 0};
    return out;
}

} // namespace kudroid

extern "C" void kudroid_run_vfs_self_test(void) {
    std::fprintf(stdout, "%s", kudroid::run_vfs_self_test().c_str());
}
