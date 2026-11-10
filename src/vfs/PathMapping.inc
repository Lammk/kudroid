// OBB fallback resolution (see remap()). Separated for testability: takes
// the already-mapped host path, returns the fallback host path or empty.
std::string VFSPathRemapper::resolveObbFallback(const std::string& mapped) const {
    {
        std::lock_guard<std::mutex> lock(obbMutex_);
        const auto hit = obbResolved_.find(mapped);
        if (hit != obbResolved_.end()) return hit->second;
    }
    std::string found;
    std::error_code ec;
    // Guest path shape: <root>/sdcard/Android/obb/<pkg>/<file>.obb
    const std::string kObb = "/sdcard/Android/obb/";
    const size_t at = mapped.find(kObb);
    std::string pkg, file;
    if (at != std::string::npos) {
        const std::string rest = mapped.substr(at + kObb.size());
        const size_t slash = rest.find('/');
        if (slash != std::string::npos) {
            pkg = rest.substr(0, slash);
            file = rest.substr(slash + 1);
        }
    }
    if (!pkg.empty() && !file.empty()) {
        // 1. Same-package app dir: sideloads stage the .obb next to the APK.
        const std::string cand = androidRoot_ + "/data/app/" + pkg + "/" + file;
        if (std::filesystem::exists(cand, ec) &&
            std::filesystem::is_regular_file(cand, ec)) {
            found = cand;
        }
    }
    if (found.empty() && !pkg.empty()) {
        // 2. One scan per install: any .obb whose path carries the package
        // name (extractor staging, user copies). Cached in obbFiles_.
        std::lock_guard<std::mutex> lock(obbMutex_);
        if (!obbScanned_) {
            obbScanned_ = true;
            const std::string roots[] = {androidRoot_ + "/sdcard",
                                         androidRoot_ + "/data/app"};
            for (const auto& root : roots) {
                if (!std::filesystem::exists(root, ec)) continue;
                for (auto it = std::filesystem::recursive_directory_iterator(
                         root, std::filesystem::directory_options::skip_permission_denied, ec);
                     it != std::filesystem::recursive_directory_iterator(); ++it) {
                    if (!it->is_regular_file(ec)) continue;
                    const std::string p = it->path().string();
                    if (p.size() >= 4 &&
                        (p.compare(p.size() - 4, 4, ".obb") == 0 ||
                         p.compare(p.size() - 4, 4, ".OBB") == 0)) {
                        obbFiles_.push_back(p);
                    }
                }
            }
        }
        for (const auto& p : obbFiles_) {
            if (p.find(pkg) != std::string::npos) {
                found = p;
                break;
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(obbMutex_);
        obbResolved_[mapped] = found;  // empty = known miss, do not rescan
        // Bounded: guest-enumerable paths would otherwise grow this forever.
        constexpr size_t kMaxObbResolved = 1024;
        if (obbResolved_.size() > kMaxObbResolved) obbResolved_.clear();
    }
    if (!found.empty()) {
        static std::atomic<int> s_obbFb{0};
        if (s_obbFb.load() < 10) {
            ++s_obbFb;
            ktraceLine("[KuDroidVFS] obb fallback: %s -> %s\n",
                         mapped.c_str(), found.c_str());
        }
        vfsTrace("OBB fallback: " + mapped + " -> " + found);
    }
    return found;
}

std::string VFSPathRemapper::remap(const char* originalPath) const {
    if (!originalPath) return {};

    // Resolve jar: and file:archive!/entry URLs to loose asset files.
    // Callers sometimes prepend a '/' ("...open(\"/jar:file:///...\"") — seen live
    // as a jar miss for GameBuildSettings.json — so test past it, not just at [0].
    const char* jarTest = originalPath;
    while (*jarTest == '/') ++jarTest;
    if (std::strncmp(jarTest, "jar:", 4) == 0 || std::strstr(originalPath, "!/") != nullptr) {
        const char* excl = std::strchr(originalPath, '!');
        if (excl != nullptr) {
            const char* sub = excl + 1;
            while (*sub == '/') ++sub;
            const char* entry = sub;
            if (std::strncmp(entry, "assets/", 7) == 0) {
                entry += 7;
                while (*entry == '/') ++entry;
            }
            const std::string assetsDir = kudroid::kudroid_get_assets_dir_cpp();
            if (!assetsDir.empty()) {
                std::error_code ec;
                std::string candidate = normalizePathString(assetsDir + "/" + entry);
                if (std::filesystem::exists(candidate, ec)) {
                    vfsTrace("Remapped JAR asset: " + std::string(originalPath) + " -> " + candidate);
                    return candidate;
                }
                std::string altCandidate = normalizePathString(assetsDir + "/" + sub);
                if (std::filesystem::exists(altCandidate, ec)) {
                    vfsTrace("Remapped JAR asset (alt): " + std::string(originalPath) + " -> " + altCandidate);
                    return altCandidate;
                }
                std::filesystem::path appDir = std::filesystem::path(assetsDir).parent_path();
                std::string appCandidate = normalizePathString((appDir / sub).string());
                if (std::filesystem::exists(appCandidate, ec)) {
                    vfsTrace("Remapped JAR entry: " + std::string(originalPath) + " -> " + appCandidate);
                    return appCandidate;
                }
                // The loose candidates all missed. The URL names an archive and an
                // entry inside it, so serve the entry from the archive ZIP itself —
                // extracted once into the VFS cache, then served as a plain file.
                // Without this, content that exists only inside the APK is a silent
                // 404: the engine boots to a splash and the loader starves with no
                // error anywhere near the cause.
                {
                    const auto [archive, entry] = split_archive_url(originalPath);
                    if (!archive.empty() && !entry.empty()) {
                        // The archive half of the URL is an Android path — remap it
                        // through the prefix table before touching the filesystem,
                        // exactly as any other guest path would be.
                        const std::string zpath = remap(archive.c_str());
                        if (std::string served =
                                extract_jar_entry_to_cache(zpath, entry, androidRoot_);
                            !served.empty()) {
                            static std::atomic<int> s_jarZip{0};
                            if (s_jarZip.load() < 20) {
                                ++s_jarZip;
                                ktraceLine(
                                             "[KuDroidVFS] jar served from archive: %s -> %s\n",
                                             originalPath, served.c_str());
                            }
                            return served;
                        }
                    }
                }
                // Still nothing: the miss must stay visible — a silent 404 downstream
                // was exactly how the black screen hid its cause.
                {
                    static std::atomic<int> s_jarMiss{0};
                    if (s_jarMiss.load() < 10) {
                        ++s_jarMiss;
                        ktraceLine(
                                     "[KuDroidVFS] jar miss: %s (tried %s)\n",
                                     originalPath, candidate.c_str());
                    }
                }
                vfsTrace("Remapped JAR asset (default): " + std::string(originalPath) + " -> " + candidate);
                return candidate;
            }
        }
    }

    // Strip file: URI scheme if present.
    const char* pathWithoutScheme = originalPath;
    if (std::strncmp(pathWithoutScheme, "file://", 7) == 0) {
        pathWithoutScheme += 7;
        if (std::strncmp(pathWithoutScheme, "localhost/", 10) == 0) pathWithoutScheme += 9;
        else if (std::strncmp(pathWithoutScheme, "127.0.0.1/", 10) == 0) pathWithoutScheme += 9;
    } else if (std::strncmp(pathWithoutScheme, "file:", 5) == 0) {
        pathWithoutScheme += 5;
    }
    std::string pathBuffer;
    if (*pathWithoutScheme != '/' && *pathWithoutScheme != '\0') {
        pathBuffer = "/" + std::string(pathWithoutScheme);
        pathWithoutScheme = pathBuffer.c_str();
    }

    // Normalise before matching a prefix, not after. "/sdcard/../../x" must not be
    // treated as an sdcard path and then joined to the root with the ".." intact — the
    // kernel would resolve it outside android_root. After this, the path contains no
    // "." or ".." at all, so the join below cannot escape.
    const std::string normalized = normalizePathString(std::string_view(pathWithoutScheme));
    std::string_view original(normalized);
    
    // directly maps the server's root /dev/ devices to ios
    if (original == "/dev/urandom" || original == "/dev/random" || 
        original == "/dev/null" || original == "/dev/zero") {
        return std::string(original);
    }

    auto matches = [](std::string_view path, std::string_view prefix) -> bool {
        if (path == prefix) return true;
        if (path.size() > prefix.size() && path.find(prefix) == 0 && path[prefix.size()] == '/') return true;
        return false;
    };

    std::string_view prefix;
    std::string_view rootName;

    if (matches(original, "/data/data")) { prefix = "/data/data"; rootName = "data/data"; }
    else if (matches(original, "/data/user/0")) { prefix = "/data/user/0"; rootName = "data/data"; }
    else if (matches(original, "/data/user_de/0")) { prefix = "/data/user_de/0"; rootName = "data/data"; }
    else if (matches(original, "/data/local/tmp")) { prefix = "/data/local/tmp"; rootName = "data/local/tmp"; }
    else if (matches(original, "/data/app")) { prefix = "/data/app"; rootName = "data/app"; }
    else if (matches(original, "/data/cache")) { prefix = "/data/cache"; rootName = "data/cache"; }
    else if (matches(original, "/data")) { prefix = "/data"; rootName = "data"; }
    else if (matches(original, "/storage/emulated/0")) { prefix = "/storage/emulated/0"; rootName = "sdcard"; }
    else if (matches(original, "/storage/emulated")) { prefix = "/storage/emulated"; rootName = "storage/emulated"; }
    else if (matches(original, "/storage/self/primary")) { prefix = "/storage/self/primary"; rootName = "sdcard"; }
    else if (matches(original, "/storage")) { prefix = "/storage"; rootName = "storage"; }
    else if (matches(original, "/sdcard")) { prefix = "/sdcard"; rootName = "sdcard"; }
    else if (matches(original, "/mnt/sdcard")) { prefix = "/mnt/sdcard"; rootName = "sdcard"; }
    else if (matches(original, "/mnt")) { prefix = "/mnt"; rootName = "mnt"; }
    else if (matches(original, "/system")) { prefix = "/system"; rootName = "system"; }
    else if (matches(original, "/etc")) { prefix = "/etc"; rootName = "etc"; }
    else if (matches(original, "/proc/self")) { prefix = "/proc/self"; rootName = "proc/self"; }
    else if (matches(original, "/proc")) { prefix = "/proc"; rootName = "proc"; }
    else if (matches(original, "/sys")) { prefix = "/sys"; rootName = "sys"; }
    else if (matches(original, "/cache")) { prefix = "/cache"; rootName = "data/cache"; }
    else if (matches(original, "/dev")) { prefix = "/dev"; rootName = "dev"; }
    else {
        if (!original.empty() && original[0] != '/') {
            std::string mapped = androidRoot_ + "/data/local/tmp/" + std::string(original);
            vfsTrace("Remapped relative path: " + std::string(original) + " -> " + mapped);
            return mapped;
        }

        // Already inside android_root: return it unchanged.
        //
        // Required for idempotency, not a special case. realpath() and readdir() hand
        // the guest paths that are already mapped, and the guest opens what it was
        // given; prefixing the root a second time would turn every such reopen into
        // ENOENT.
        if (original.size() >= androidRoot_.size() &&
            original.compare(0, androidRoot_.size(), androidRoot_) == 0 &&
            (original.size() == androidRoot_.size() || original[androidRoot_.size()] == '/')) {
            return std::string(original);
        }
        // Canonical host paths must reopen under the same guest root.
        if (!canonicalRoot_.empty() && original.size() >= canonicalRoot_.size() &&
            original.compare(0, canonicalRoot_.size(), canonicalRoot_) == 0 &&
            (original.size() == canonicalRoot_.size() || original[canonicalRoot_.size()] == '/')) {
            return std::string(original);
        }

        // An absolute path outside every known Android prefix. Contain it instead of
        // passing it through.
        //
        // Passing it through was the hole that made normalisation above pointless:
        // "/sdcard/../../../Library/Preferences/x.plist" normalises to
        // "/Library/Preferences/x.plist", matches nothing, and used to be handed to
        // open() as a real iOS path. Rooting it keeps the guest inside the VFS, and
        // since no such directory exists there the call fails with ENOENT — which is
        // also what it would do on Android, where /Library is not a thing.
        std::string mapped = androidRoot_ + std::string(original);
        vfsTrace("Contained unknown absolute path: " + std::string(original) + " -> " + mapped);
        return mapped;
    }

    std::string remainder = "";
    if (original.size() > prefix.size()) {
        remainder = std::string(original.substr(prefix.size()));
        if (!remainder.empty() && remainder[0] != '/') {
            remainder = "/" + remainder;
        }
    }

    std::string mapped = androidRoot_ + "/" + std::string(rootName) + remainder;
    // OBB fallback: /sdcard/Android/obb/<pkg>/<file>.obb often is not where
    // the installer put it. Sideloaded installs leave the .obb next to the
    // APK under data/app/<pkg>/, or anywhere the extractor staged it; the
    // guest only ever looks in exactly one place and reports a GUID miss
    // otherwise (Unity: "Unable to load GUID ... main.1.<pkg>.obb", then
    // every streamed bank/clip downstream fails). Resolve once per install:
    // same-directory data/app scan first, then any known .obb whose package
    // segment matches, cached so the scans never repeat per open.
    if (rootName == "sdcard" &&
        remainder.compare(0, 13, "/Android/obb/") == 0) {
        std::error_code ec;
        if (!std::filesystem::exists(mapped, ec)) {
            const std::string resolved = resolveObbFallback(mapped);
            if (!resolved.empty()) return resolved;
        }
    }
    vfsTrace("Remapped: " + std::string(original) + " -> " + mapped);
    return mapped;
}

int vfs_open(const char* path, int flags, mode_t mode) {
    // TEMP DIAGNOSTIC (ULTRAKILL audio crash): FMOD deterministically builds a
    // voice with a null buffer row ~140 presents in. If its bank/stream file
    // opens fail or misbehave here, that is the trigger. Log game-data opens.
    const bool traceOpen = path != nullptr &&
        (std::strstr(path, "assets/") != nullptr || std::strstr(path, ".apk") != nullptr ||
         std::strstr(path, ".bank") != nullptr || std::strstr(path, ".fsb") != nullptr ||
         std::strstr(path, ".mp3") != nullptr || std::strstr(path, ".ogg") != nullptr ||
         std::strstr(path, ".wav") != nullptr || std::strstr(path, ".mp4") != nullptr ||
         std::strstr(path, ".webm") != nullptr || std::strstr(path, "jar:") != nullptr ||
         std::strstr(path, ".json") != nullptr || std::strstr(path, "catalog") != nullptr ||
         std::strstr(path, "/files/") != nullptr || std::strstr(path, "/sdcard/") != nullptr);
    const void* const caller = __builtin_return_address(0);
    if (traceOpen) {
        ktraceLine("[KuDroidVFS] open(%s, flags=0x%x)%s\n", path, flags, trace_caller_text(caller));
    }
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
        static std::atomic<int> ashmem_counter{0};
        char name[64];
        std::snprintf(name, sizeof(name), "/kudroid_ashmem_%d_%d", ::getpid(),
                      ashmem_counter.fetch_add(1, std::memory_order_relaxed));
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
            // record the fake map layout that satisfies the basic checks
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
            propFile << "ro.build.version.sdk=" KUDROID_SDK_INT_STR "\n"
                        "ro.product.cpu.abi=" KUDROID_DEVICE_ABI "\n";
            propFile.close();
        }
    }

    const std::string mapped = VFSPathRemapper::getInstance().remap(path);
    const int host_flags = translate_linux_open_flags(flags);
    // For O_CREAT, make sure the parent directory exists (avoid ENOENT problem).
    if (host_flags & O_CREAT) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(mapped).parent_path(), ec);
    }
    const int result = (host_flags & O_CREAT) ? ::open(mapped.c_str(), host_flags, mode)
                                              : ::open(mapped.c_str(), host_flags);
    vfsTrace("open(" + mapped + ") -> " + std::to_string(result));
    if (traceOpen) {
        ktraceLine("[KuDroidVFS] open -> %d (%s)%s\n", result,
                     result >= 0 ? "OK" : std::strerror(errno), trace_caller_text(caller));
    }
    return result;
}

int vfs_open64(const char* path, int flags, mode_t mode) { return vfs_open(path, flags, mode); }

static void track_apk_stream(FILE* f);

// Audio-side tracing (defined next to the stream table): FMOD's own thread does
// not go through the per-instance op gate, so it is identified by thread name.
static bool audio_thread();
static bool audio_trace_take(int sid);

extern std::mutex g_freadVolMtx;
extern std::map<FILE*, std::string> g_freadPaths;

FILE* vfs_fopen(const char* path, const char* mode) {
    const void* const caller = __builtin_return_address(0);
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
    if (mapped == "/dev/urandom" || mapped == "/dev/random") {
        static std::atomic<int> entropyOpens{0};
        if (entropyOpens.fetch_add(1, std::memory_order_relaxed) < 16) {
            const int savedErrno = errno;
            ktraceLine("[KuDroidNet] entropy fopen(%s) -> %s errno=%d\n", mapped.c_str(),
                       result != nullptr ? "OK" : "FAIL", result != nullptr ? 0 : savedErrno);
            errno = savedErrno;
        }
    }
    vfsTrace("fopen(" + mapped + ", " + (mode ? mode : "<null>") + ") -> " +
           (result ? "OK" : std::strerror(errno)));    if (path != nullptr &&
        (std::strstr(path, "assets/") != nullptr || std::strstr(path, ".apk") != nullptr ||
         std::strstr(path, ".bank") != nullptr || std::strstr(path, ".fsb") != nullptr ||
         std::strstr(path, ".mp3") != nullptr || std::strstr(path, ".ogg") != nullptr ||
         std::strstr(path, ".wav") != nullptr || std::strstr(path, ".mp4") != nullptr ||
         std::strstr(path, ".webm") != nullptr || std::strstr(path, "jar:") != nullptr ||
         std::strstr(path, ".json") != nullptr || std::strstr(path, "catalog") != nullptr ||
         std::strstr(path, "/files/") != nullptr || std::strstr(path, "/sdcard/") != nullptr ||
         std::strstr(path, ".bundle") != nullptr || std::strstr(path, ".resource") != nullptr ||
         std::strstr(path, "sharedassets") != nullptr || std::strstr(path, "data.unity3d") != nullptr ||
         std::strstr(path, "assets/bin/") != nullptr)) {
        // Rate-limited: the engine opens base.apk thousands of times per session and
        // each line is a synchronous stderr write on the reader's thread — plain log
        // volume inside the hot asset path. First 40, then one per 256 further opens.
        static std::atomic<int> s_fopenLogged{0};
        static std::atomic<int> s_fopenSeen{0};
        const int seen = s_fopenSeen.fetch_add(1, std::memory_order_relaxed) + 1;
        const int logged = s_fopenLogged.load(std::memory_order_relaxed);
        if (logged < 40 || (seen - logged) >= 256) {
            s_fopenLogged.store(seen, std::memory_order_relaxed);
            ktraceLine("[KuDroidVFS] fopen(%s) -> %s (n=%d)%s\n", path,
                         result ? "OK" : std::strerror(errno), seen,
                         trace_caller_text(caller));
        }
    }
    // Audio thread opens are traced unconditionally (bounded by the audio budget,
    // not by the asset-path rate limiter above): a clip load that fails starts at
    // the open, and the generic gate would hide it past the first 40 lines.
    if (audio_trace_take(-1)) {
        ktraceLine("[KuDroidVFS] AUDIO fopen(%s) -> %s%s\n", path ? path : "?",
                     result ? "OK" : std::strerror(errno), trace_caller_text(caller));
    }
    if (result != nullptr && path != nullptr) {
        const size_t len = std::strlen(path);
        if ((len >= 8 && std::strcmp(path + len - 8, "base.apk") == 0) ||
            (len >= 4 && std::strcmp(path + len - 4, ".apk") == 0) ||
            (len >= 4 && std::strcmp(path + len - 4, ".obb") == 0)) {
            track_apk_stream(result);
        }
        std::lock_guard<std::mutex> vlock(g_freadVolMtx);
        if (g_freadPaths.size() < 512) g_freadPaths[result] = mapped;
    }
    // Misses under /data are silent stalls when Unity looks in the wrong place.
    // Print guest-relative (the container UUID changes every install and only
    // adds noise to the log).
    if (result == nullptr && !mapped.empty() &&
        mapped.rfind(VFSPathRemapper::getInstance().androidRoot(), 0) == 0) {
        static std::atomic<int> s_miss{0};
        if (s_miss.load() < 30) {
            ++s_miss;
            ktraceLine("[KuDroidVFS] open miss: ~%s%s\n",
                         mapped.c_str() +
                             VFSPathRemapper::getInstance().androidRoot().size(),
                         trace_caller_text(caller));
        }
    }
    return result;
}

FILE* vfs_fopen64(const char* path, const char* mode) { return vfs_fopen(path, mode); }

