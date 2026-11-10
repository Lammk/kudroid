namespace kudroid {
namespace {

void vfsLog(const std::string& message) {
    ktraceLine("[kudroid_vfs] %s\n", message.c_str());
}
void vfsTrace(const std::string& message) {
#ifdef KUDROID_DEBUG
    ktraceLine("[kudroid_vfs] %s\n", message.c_str());
#else
    (void)message;
#endif
}

std::string defaultDocumentsDirectory() {
    // HOME, not a computed bundle path.
    //
    // This is the fallback for anything that touches the remapper before Swift calls
    // kudroid_set_documents_dir, and it has to land in the same place that call would.
    // It does: iOS sets HOME to the app container, and LiveContainer rewrites HOME (and
    // CFFIXED_USER_HOME, which is what FileManager reads) to the nested per-guest
    // container before it loads the guest — so both sides agree in either deployment,
    // and no orphaned android_root is created at a stale path.
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') return std::string(home) + "/Documents";
    // No HOME at all should not happen on iOS. "." would be the process working
    // directory, which is "/" and unwritable, so say so rather than failing later with
    // a permission error on every file the remapper tries to create.
    vfsLog("HOME is unset; falling back to the working directory, which is not"
           " writable on iOS. Call kudroid_set_documents_dir before using the VFS.");
    return ".";
}

static int translate_linux_open_flags(int flags) {
#if defined(__APPLE__)
    int host_flags = 0;
    int acc = flags & 3;
    if (acc == 0) host_flags |= O_RDONLY;
    else if (acc == 1) host_flags |= O_WRONLY;
    else if (acc == 2) host_flags |= O_RDWR;

    if (flags & 0x40) host_flags |= O_CREAT;
    if (flags & 0x80) host_flags |= O_EXCL;
    if (flags & 0x100) host_flags |= O_NOCTTY;
    if (flags & 0x200) host_flags |= O_TRUNC;
    if (flags & 0x400) host_flags |= O_APPEND;
    if (flags & 0x800) host_flags |= O_NONBLOCK;
#if defined(O_CLOEXEC)
    if (flags & 0x80000) host_flags |= O_CLOEXEC;
#endif
    return host_flags;
#else
    return flags;
#endif
}

// Resolve ".", ".." and duplicate separators without touching the filesystem.
//
// This has to happen BEFORE the path is joined to android_root, because the kernel
// resolves ".." after the join: "/data/data/../../../../etc/passwd" concatenated onto
// the root walks straight out of it and lands in the real iOS container. Guests produce
// such paths routinely (asset names taken from zip entries, config strings), and a
// hostile APK can produce them deliberately.
//
// Done on the string rather than with std::filesystem::weakly_canonical because that
// consults the filesystem and follows symlinks — and android_root deliberately contains
// symlinks that point outside itself (proc/self/fd -> /dev/fd), so canonicalising would
// defeat the containment this exists to provide.
//
// A ".." that would climb above the top of an absolute path is dropped, which is what
// the kernel does at "/" as well.
// Direct-mapped memo for normalizePathString: bounded to 512 entries, no eviction
// policy. The guest re-normalizes the same long paths thousands of times on one cold
// start (shader cache, per-asset remaps) and the split/join dominates that cost.
// Sharded 16-way: every remap (every open/stat, every guest thread) hits this,
// and one global mutex would serialize all guest I/O threads on each other.
struct NormalizeMemoSlot {
    std::string input;
    std::string output;
};
struct NormalizeMemoShard {
    std::mutex mtx;
    static constexpr size_t kSlots = 32;
    NormalizeMemoSlot slots[kSlots];
};
struct NormalizeMemo {
    static constexpr size_t kShards = 16;
    NormalizeMemoShard shards[kShards];
};

std::string normalizePathString(std::string_view path) {
    static NormalizeMemo memo;
    const size_t h = std::hash<std::string_view>{}(path);
    NormalizeMemoShard& shard = memo.shards[h % NormalizeMemo::kShards];
    const size_t idx = (h / NormalizeMemo::kShards) % NormalizeMemoShard::kSlots;
    {
        std::lock_guard<std::mutex> lock(shard.mtx);
        const std::string& in = shard.slots[idx].input;
        if (in.size() == path.size() && std::memcmp(in.data(), path.data(), path.size()) == 0) {
            return shard.slots[idx].output;
        }
    }

    const bool absolute = !path.empty() && path[0] == '/';
    std::vector<std::string_view> parts;
    size_t i = 0;
    while (i < path.size()) {
        while (i < path.size() && path[i] == '/') ++i;
        const size_t start = i;
        while (i < path.size() && path[i] != '/') ++i;
        if (i == start) break;
        const std::string_view component = path.substr(start, i - start);
        if (component == ".") continue;
        if (component == "..") {
            // A relative path may legitimately begin above itself ("../x"), so a
            // leading ".." is kept there; an absolute path cannot rise above "/".
            if (!parts.empty() && parts.back() != "..") {
                parts.pop_back();
            } else if (!absolute) {
                parts.push_back(component);
            }
            continue;
        }
        parts.push_back(component);
    }

    std::string result;
    if (absolute) result = "/";
    for (size_t n = 0; n < parts.size(); ++n) {
        if (n != 0) result += '/';
        result.append(parts[n]);
    }

    {
        std::lock_guard<std::mutex> lock(shard.mtx);
        shard.slots[idx].input.assign(path);
        shard.slots[idx].output = result;
    }
    return result;
}

} // namespace

VFSPathRemapper& VFSPathRemapper::getInstance(bool initializeTree) {
    static VFSPathRemapper instance;
    // Root configuration must precede the first directory-tree creation.
    if (initializeTree) (void)instance.initialize();
    return instance;
}

std::string VFSPathRemapper::packageName() const {
    std::lock_guard<std::mutex> lock(initMutex_);
    return packageName_;
}

void VFSPathRemapper::setPackageName(const std::string& packageName) {
    if (packageName.empty()) return;
    {
        std::lock_guard<std::mutex> lock(initMutex_);
        packageName_ = packageName;
    }
    // Write through immediately: init may have already materialized generics.
    const std::string cmdline = androidRoot_ + "/proc/self/cmdline";
    if (FILE* out = std::fopen(cmdline.c_str(), "w")) {
        std::fwrite(packageName.c_str(), 1, packageName.size() + 1, out);
        std::fclose(out);
    }
    const std::string stat =
        androidRoot_ + "/proc/self/stat";
    if (FILE* out = std::fopen(stat.c_str(), "w")) {
        std::fprintf(out,
                     "1 (%s) S 0 1 1 0 -1 4194560 0 0 0 0 0 0 0 0 20 0 1 0 0 0 0 0 0 0 "
                     "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n",
                     packageName.c_str());
        std::fclose(out);
    }
    const std::string status = androidRoot_ + "/proc/self/status";
    if (FILE* out = std::fopen(status.c_str(), "w")) {
        std::fprintf(out,
                     "Name:\t%s\nState:\tS (sleeping)\nTgid:\t1\nPid:\t1\nPPid:\t0\n"
                     "TracerPid:\t0\nUid:\t10000\t10000\t10000\t10000\n"
                     "Gid:\t10000\t10000\t10000\t10000\nThreads:\t1\n"
                     "SigQ:\t0/2080\nSigPnd:\t0000000000000000\n"
                     "SigBlk:\t0000000000000000\nSigIgn:\t0000000000001000\n"
                     "SigCgt:\t00000000000085f8\n",
                     packageName.c_str());
        std::fclose(out);
    }
}

std::string VFSPathRemapper::android_id() {
    static std::mutex mtx;
    static std::string cached;
    std::lock_guard<std::mutex> lock(mtx);
    if (!cached.empty()) return cached;
    const std::string file = androidRoot_ + "/android_id";
    {
        char buffer[32] = {};
        if (FILE* in = std::fopen(file.c_str(), "r")) {
            const size_t n = std::fread(buffer, 1, sizeof(buffer) - 1, in);
            std::fclose(in);
            if (n == 16) {
                bool hex = true;
                for (size_t i = 0; i < 16; ++i) {
                    const char c = buffer[i];
                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                        hex = false;
                }
                if (hex) {
                    cached.assign(buffer, 16);
                    return cached;
                }
            }
        }
    }
    // Generate: 16 lowercase hex from /dev/urandom (never the blocklisted
    // emulator constant).
    char fresh[17];
    {
        unsigned char raw[8] = {};
        if (FILE* ur = std::fopen("/dev/urandom", "r")) {
            std::fread(raw, 1, sizeof(raw), ur);
            std::fclose(ur);
        }
        static const char* digits = "0123456789abcdef";
        for (int i = 0; i < 8; ++i) {
            fresh[2 * i] = digits[(raw[i] >> 4) & 0xF];
            fresh[2 * i + 1] = digits[raw[i] & 0xF];
        }
        fresh[16] = '\0';
    }
    if (FILE* out = std::fopen(file.c_str(), "w")) {
        std::fwrite(fresh, 1, 16, out);
        std::fclose(out);
    }
    cached.assign(fresh, 16);
    return cached;
}

VFSPathRemapper::VFSPathRemapper()
    : documentsDirectory_(defaultDocumentsDirectory()),
      androidRoot_(documentsDirectory_ + "/android_root") {}

void VFSPathRemapper::setDocumentsDirectory(const std::string& documentsDirectory, bool flatRoot) {
    {
        std::lock_guard<std::mutex> lock(initMutex_);
        documentsDirectory_ = documentsDirectory;
        androidRoot_ = flatRoot ? documentsDirectory_ : documentsDirectory_ + "/android_root";
        canonicalRoot_.clear();
        // The tree has to be built under the new root, so the previous run does not
        // count. Swift calls this after the container path is known, which is normally
        // before any guest file access.
        initialized_ = false;
    }
    (void)initialize();
}

bool VFSPathRemapper::initialize() {
    std::lock_guard<std::mutex> lock(initMutex_);
    if (initialized_) return initResult_;
    initResult_ = initializeLocked();
    initialized_ = true;
    return initResult_;
}

bool VFSPathRemapper::initializeLocked() {
    std::error_code error;
    // create base folders (android-like layout).
    for (const auto& relative : {
        "data/data", "data/app", "data/local/tmp", "data/cache",
        "data/user", "data/user_de",
        "sdcard/Download", "sdcard/Documents", "sdcard/Pictures", "sdcard/DCIM",
        "sdcard/Music", "sdcard/Movies", "sdcard/Android/data", "sdcard/Android/obb", "sdcard/Android/media",
        "system", "proc/self", "sys", "mnt", "storage/emulated", "storage/self", "dev",
        "vendor", "etc", "system/etc", "system/etc/security/cacerts", "system/etc/permissions"
    }) {
        std::filesystem::create_directories(std::filesystem::path(androidRoot_) / relative, error);
        if (error) {
            vfsLog("Failed to create " + androidRoot_ + "/" + relative + ": " + error.message());
            return false;
        }
    }
    
    // create soft links
    auto make_symlink = [&](const char* target, const char* linkpath, bool isDir = true) {
        std::error_code ec;
        std::filesystem::path fullLink = std::filesystem::path(androidRoot_) / linkpath;
        if (!std::filesystem::exists(fullLink, ec)) {
            if (isDir) {
                std::filesystem::create_directory_symlink(target, fullLink, ec);
            } else {
                std::filesystem::create_symlink(target, fullLink, ec);
            }
        }
    };
    
    make_symlink("../sdcard", "mnt/sdcard");
    make_symlink("../../sdcard", "storage/emulated/0");
    make_symlink("../../sdcard", "storage/self/primary");
    make_symlink("../data", "data/user/0");
    make_symlink("../data", "data/user_de/0");
    make_symlink("data/local/tmp", "tmp");
    std::filesystem::remove(std::filesystem::path(androidRoot_) / "etc", error);
    make_symlink("system/etc", "etc");
    make_symlink("/dev/fd", "proc/self/fd");
    make_symlink("system/build.prop", "default.prop", false);
    make_symlink("../system/build.prop", "vendor/build.prop", false);

    char* canonical = ::realpath(androidRoot_.c_str(), nullptr);
    if (canonical) {
        canonicalRoot_ = canonical;
        std::free(canonical);
    }
    return init_pseudo_files();
}

