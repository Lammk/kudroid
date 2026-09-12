#include "kudroid/VFSPathRemapper.h"
#include "kudroid/cacert_data.h"
#include "kudroid/DeviceProfile.h"
#include "kudroid/platform/AssetShim.h"
#include "kudroid/platform/CpuInfo.h"
#include "kudroid/platform/MemoryInfo.h"

#include <cerrno>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <limits.h>
#include <sstream>
#include <unistd.h>
#include <vector>
#include <zlib.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>

// Defined in SyscallShim.cpp. Declared here rather than through a header because the
// remapper otherwise has no reason to depend on the syscall layer; it needs this only to
// record what device figures the pseudo-files were written from.
extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);

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

VFSPathRemapper& VFSPathRemapper::getInstance() {
    static VFSPathRemapper instance;
    // Cheap after the first call: initialize() returns the cached result. Kept here so
    // that whichever vfs_* function runs first still finds the tree in place.
    (void)instance.initialize();
    return instance;
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

void VFSPathRemapper::setDocumentsDirectory(const std::string& documentsDirectory) {
    {
        std::lock_guard<std::mutex> lock(initMutex_);
        documentsDirectory_ = documentsDirectory;
        androidRoot_ = documentsDirectory_ + "/android_root";
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

    return init_pseudo_files();
}

// Device figures a guest can read, rendered into the Linux formats it expects.
//
// These used to be string literals: /proc/meminfo said MemTotal 8192000 kB, /proc/cpuinfo
// listed exactly 8 processors, /sys/.../present said 0-7. Unity reported
// "Cores = 0, Memory = 8000mb" on a 4 GB iPhone — and 8192000/1024 is 8000 exactly, so
// that figure came from here rather than from anything measured. MemoryInfo.cpp was
// already querying the real sysctl; nothing asked it.
//
// A wrong number here is not cosmetic. Engines size worker pools from the core count and
// texture/chunk caches from the memory figure, so over-reporting gets the process killed
// mid-load and under-reporting makes it run degraded on hardware that could do better.
namespace {

std::string DecimalRange(uint32_t count) {
    if (count <= 1) return "0\n";
    return "0-" + std::to_string(count - 1) + "\n";
}

// One /proc/cpuinfo block per core.
//
// The feature list and implementer IDs stay fixed — they describe the arm64 ISA level
// KuDroid presents, not the individual core — but the processor count follows the device.
// A guest that counts "processor" lines and a guest that reads sysconf must agree.
std::string BuildCpuInfo(const CpuTopology& cpu) {
    // Engines re-read /proc/cpuinfo per worker thread (790 reads in one boot).
    // The content depends only on the topology numbers, so key the cache on
    // those (not the object address — reset-for-test mutates it in place).
    static std::mutex cpuInfoMtx;
    static std::string cpuInfoCache;
    static uint32_t cachedTotal = 0, cachedPerf = 0, cachedEff = 0;
    std::lock_guard<std::mutex> lock(cpuInfoMtx);
    if (!cpuInfoCache.empty() && cachedTotal == cpu.total_cores &&
        cachedPerf == cpu.performance_cores && cachedEff == cpu.efficiency_cores) {
        return cpuInfoCache;
    }
    static const char kFeatures[] =
        "BogoMIPS\t: 38.40\n"
        "Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp "
        "cpuid asimdrdm jscvt fcma lrcpc dcpop sha3 sm3 sm4 asimddp sha512 asimdfhm dit "
        "uscat ilrcpc flagm ssbs sb paca pacg dcpodp flagm2 frint\n"
        "CPU implementer\t: 0x41\n"
        "CPU architecture: 8\n"
        "CPU variant\t: 0x1\n";

    std::string out;
    for (uint32_t i = 0; i < cpu.total_cores; ++i) {
        // CPU part distinguishes the two classes, which is the second way a guest can
        // tell them apart when it does not read cpufreq. 0xd46 is Cortex-A510-class
        // (efficiency), 0xd47 A710-class (performance) — the specific IDs matter less
        // than that cores in one class share an ID and the classes differ.
        const bool performance = i < cpu.performance_cores;
        out += "processor\t: " + std::to_string(i) + "\n";
        out += kFeatures;
        out += performance ? "CPU part\t: 0xd47\n" : "CPU part\t: 0xd46\n";
        out += "CPU revision\t: 0\n\n";
    }
    out += "Hardware\t: KuDroid arm64\n";
    cpuInfoCache = out;
    cachedTotal = cpu.total_cores;
    cachedPerf = cpu.performance_cores;
    cachedEff = cpu.efficiency_cores;
    return out;
}

// /proc/stat, with one line per core.
//
// The jiffy counts are synthetic and equal across cores. What a guest uses this for is
// counting cpuN lines and computing load deltas; a missing core here contradicts
// /proc/cpuinfo, and that contradiction is the bug this file class had.
std::string BuildProcStat(const CpuTopology& cpu) {
    const uint64_t per_core_user = 125;
    const uint64_t per_core_system = 125;
    const uint64_t per_core_idle = 6250;

    std::string out = "cpu  " + std::to_string(per_core_user * cpu.total_cores) + " 0 " +
                      std::to_string(per_core_system * cpu.total_cores) + " " +
                      std::to_string(per_core_idle * cpu.total_cores) + " 0 0 0 0 0 0\n";
    for (uint32_t i = 0; i < cpu.total_cores; ++i) {
        out += "cpu" + std::to_string(i) + " " + std::to_string(per_core_user) + " 0 " +
               std::to_string(per_core_system) + " " + std::to_string(per_core_idle) +
               " 0 0 0 0 0 0\n";
    }
    out += "intr 0\nctxt 1000\nbtime 1700000000\nprocesses 100\nprocs_running 1\n"
           "procs_blocked 0\n";
    return out;
}

// /proc/meminfo from the real device figures.
//
// MemAvailable is what Android's ActivityManager reports and what an adaptive cache
// reads. The relationship MemFree <= MemAvailable <= MemTotal is maintained explicitly:
// a guest computing used = MemTotal - MemAvailable underflows otherwise, and a guest
// comparing MemFree against MemAvailable concludes the file is corrupt.
std::string BuildMemInfo(const SystemMemory& mem) {
    const uint64_t total_kb = mem.total_bytes / 1024ull;
    uint64_t available_kb = mem.available_bytes / 1024ull;
    if (available_kb > total_kb) available_kb = total_kb;
    // Free is the part not backed by reclaimable caches. Reported as a fraction of
    // available rather than measured: Darwin's free_count alone excludes the inactive and
    // purgeable pages that AvailableMemory deliberately counts, so using it here would
    // contradict the available figure derived from the same query.
    const uint64_t free_kb = available_kb / 2;
    const uint64_t cached_kb = available_kb - free_kb;

    std::string out;
    out += "MemTotal:       " + std::to_string(total_kb) + " kB\n";
    out += "MemFree:        " + std::to_string(free_kb) + " kB\n";
    out += "MemAvailable:   " + std::to_string(available_kb) + " kB\n";
    out += "Buffers:           24576 kB\n";
    out += "Cached:         " + std::to_string(cached_kb) + " kB\n";
    out += "SwapCached:            0 kB\n";
    // iOS has no swap a guest can use. Reporting a swap total would tell an engine it can
    // overcommit, which on iOS ends in a jetsam kill rather than in paging.
    out += "SwapTotal:             0 kB\n";
    out += "SwapFree:              0 kB\n";
    out += "Active:         " + std::to_string(cached_kb / 2) + " kB\n";
    out += "Inactive:       " + std::to_string(cached_kb / 2) + " kB\n";
    return out;
}

}  // namespace

bool VFSPathRemapper::init_pseudo_files() {
    const std::string root = androidRoot_;
    const CpuTopology& cpu = query_cpu_topology();
    const SystemMemory mem = query_system_memory();

    // Whether the content describes the DEVICE, and must therefore be rewritten rather
    // than preserved.
    //
    // The loop below keeps whatever is already on disk when a file is non-empty, which is
    // right for something a user may edit but wrong for a device figure: a file written by
    // an older build would survive this fix forever, and the user would still see
    // "Memory = 8000mb" from a stale /proc/meminfo with no way to tell why. Device files
    // are regenerated every run; they also go stale WITHIN a run, since available memory
    // changes constantly.
    struct PseudoFile {
        const char* path;
        std::string content;
        bool authoritative;
    };

    std::vector<PseudoFile> files;
    files.push_back({"system/build.prop",
         "ro.build.version.release=" KUDROID_ANDROID_RELEASE "\n"
         "ro.build.version.sdk=" KUDROID_SDK_INT_STR "\n"
         "ro.build.version.codename=REL\n"
         "ro.build.version.incremental=6000000\n"
         "ro.build.type=user\n"
         "ro.build.tags=release-keys\n"
         "ro.build.fingerprint=" KUDROID_DEVICE_BRAND "/" KUDROID_DEVICE_NAME "/" KUDROID_DEVICE_BOARD
         ":" KUDROID_ANDROID_RELEASE "/QP1A.190711.020/6000000:user/release-keys\n"
         "ro.product.model=" KUDROID_DEVICE_MODEL "\n"
         "ro.product.manufacturer=" KUDROID_DEVICE_MANUFACTURER "\n"
         "ro.product.brand=" KUDROID_DEVICE_BRAND "\n"
         "ro.product.name=" KUDROID_DEVICE_NAME "\n"
         "ro.product.device=" KUDROID_DEVICE_BOARD "\n"
         "ro.product.cpu.abi=" KUDROID_DEVICE_ABI "\n"
         "ro.product.cpu.abilist=" KUDROID_DEVICE_ABI "\n"
         "ro.product.cpu.abilist64=" KUDROID_DEVICE_ABI "\n"
         "ro.hardware=kudroid\n"
         "ro.board.platform=kudroid\n"
         "ro.boot.hardware=kudroid\n"
         "ro.sf.lcd_density=480\n"
         "ro.opengles.version=196610\n"
         "ro.debuggable=0\n"
         "persist.sys.timezone=UTC\n"
         "persist.sys.locale=en-US\n"
         "sys.boot_completed=1\n"
         "gsm.version.baseband=1.0\n", false});
    files.push_back({"proc/cpuinfo", BuildCpuInfo(cpu), true});
    files.push_back({"proc/meminfo", BuildMemInfo(mem), true});
    files.push_back({"proc/version",
         "Linux version 5.15.0-kudroid (clang 17.0.0) #1 SMP PREEMPT 2026\n", false});
    const std::string pkg =
        packageName_.empty() ? "com.kudroid.app" : packageName_;
    files.push_back({"proc/self/cmdline", std::string(pkg.c_str(), pkg.size() + 1), false});
    files.push_back({"proc/self/stat",
         "1 (" + pkg +
         ") S 0 1 1 0 -1 4194560 0 0 0 0 0 0 0 0 20 0 1 0 0 0 0 0 0 0 "
         "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n",
         false});
    // Stock debuggerd signal mask (ABRT/BUS/FPE/ILL/SEGV/STKFLT/TRAP) with
    // nothing pending/blocked and TracerPid 0: what RASP scanners compare
    // against. Guest-installed handlers are dispatched underneath regardless.
    files.push_back({"proc/self/status",
         "Name:\t" + pkg +
         "\nState:\tS (sleeping)\nTgid:\t1\nPid:\t1\nPPid:\t0\n"
         "TracerPid:\t0\nUid:\t10000\t10000\t10000\t10000\n"
         "Gid:\t10000\t10000\t10000\t10000\nThreads:\t1\n"
         "SigQ:\t0/2080\nSigPnd:\t0000000000000000\n"
         "SigBlk:\t0000000000000000\nSigIgn:\t0000000000001000\n"
         "SigCgt:\t00000000000085f8\n",
         false});
    files.push_back({"sys/devices/system/cpu/possible", DecimalRange(cpu.total_cores), true});
    files.push_back({"sys/devices/system/cpu/present", DecimalRange(cpu.total_cores), true});
    files.push_back({"sys/devices/system/cpu/online", DecimalRange(cpu.total_cores), true});
    files.push_back({"sys/devices/system/cpu/kernel_max",
                     std::to_string(cpu.total_cores > 0 ? cpu.total_cores - 1 : 0) + "\n", true});
    files.push_back({"proc/stat", BuildProcStat(cpu), true});

    // Per-core cpufreq and online entries.
    //
    // Only cpu0 had a cpufreq directory before, and that is how the big.LITTLE split went
    // missing: a guest classifies cores by comparing cpuinfo_max_freq across all of them,
    // so with one file there was nothing to compare and Unity reported
    // "0 big (mask: 0x0), 0 little (mask: 0x0)". Every core gets the full set, and the
    // performance class reports a higher ceiling than the efficiency class.
    //
    // The paths are built into std::string and kept alive in `owned` — PseudoFile holds a
    // const char*, and a temporary would dangle before the write loop runs.
    std::vector<std::string> owned;
    owned.reserve(static_cast<size_t>(cpu.total_cores) * 6);
    for (uint32_t i = 0; i < cpu.total_cores; ++i) {
        const bool performance = i < cpu.performance_cores;
        const uint32_t max_khz =
            performance ? cpu.performance_max_khz : cpu.efficiency_max_khz;
        // A plausible floor and a current value inside the range. A guest that reads
        // scaling_cur_freq outside [min, max] treats the file as unusable.
        const uint32_t min_khz = max_khz / 4;
        const uint32_t cur_khz = max_khz / 2 + min_khz;
        const std::string base =
            "sys/devices/system/cpu/cpu" + std::to_string(i);

        owned.push_back(base + "/cpufreq/cpuinfo_max_freq");
        files.push_back({owned.back().c_str(), std::to_string(max_khz) + "\n", true});
        owned.push_back(base + "/cpufreq/cpuinfo_min_freq");
        files.push_back({owned.back().c_str(), std::to_string(min_khz) + "\n", true});
        owned.push_back(base + "/cpufreq/scaling_cur_freq");
        files.push_back({owned.back().c_str(), std::to_string(cur_khz) + "\n", true});
        owned.push_back(base + "/online");
        files.push_back({owned.back().c_str(), "1\n", true});
        // Capacity and package: the last per-core files guests probe for
        // scheduling class. Capacity is relative (1024 = fastest class),
        // scaled by ceiling frequency; single-package SoC reports 0.
        const uint32_t capacity =
            (performance || cpu.performance_max_khz == 0)
                ? 1024
                : std::max(1u, (1024u * cpu.efficiency_max_khz) /
                                   cpu.performance_max_khz);
        owned.push_back(base + "/cpu_capacity");
        files.push_back({owned.back().c_str(),
                         std::to_string(capacity) + "\n", true});
        owned.push_back(base + "/topology/physical_package_id");
        files.push_back({owned.back().c_str(), "0\n", true});
    }

    files.push_back({"proc/mounts",
         "rootfs / rootfs rw 0 0\n/dev/block/bootdevice/by-name/system /system ext4 "
         "ro,seclabel,nodev,relatime 0 0\n/dev/block/bootdevice/by-name/userdata /data "
         "ext4 rw,seclabel,nosuid,nodev,noatime 0 0\n/data/media /sdcard fuse "
         "rw,nosuid,nodev,noexec,relatime 0 0\n/data/media /storage/emulated/0 fuse "
         "rw,nosuid,nodev,noexec,relatime 0 0\ntmpfs /dev tmpfs "
         "rw,seclabel,nosuid,relatime,mode=755 0 0\ndevpts /dev/pts devpts "
         "rw,seclabel,relatime,mode=600 0 0\nproc /proc proc rw,relatime 0 0\nsysfs /sys "
         "sysfs rw,seclabel,relatime 0 0\n", false});
    files.push_back({"sys/class/power_supply/battery/capacity", "100\n", false});
    files.push_back({"sys/class/power_supply/battery/status", "Charging\n", false});
    files.push_back({"sys/class/thermal/thermal_zone0/temp", "35000\n", false});
    files.push_back({"sys/class/thermal/thermal_zone0/type", "tsens_tz_sensor\n", false});
    files.push_back({"system/etc/hosts",
         "127.0.0.1\tlocalhost\n::1\t\tip6-localhost ip6-loopback\n", false});
    files.push_back({"system/etc/resolv.conf",
         "nameserver 8.8.8.8\nnameserver 8.8.4.4\n", false});
    files.push_back({"system/etc/permissions/handheld_core_hardware.xml",
         "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<permissions>\n    <feature "
         "name=\"android.hardware.camera\" />\n    <feature "
         "name=\"android.hardware.location\" />\n    <feature "
         "name=\"android.hardware.sensor.accelerometer\" />\n    <feature "
         "name=\"android.hardware.sensor.compass\" />\n</permissions>\n", false});

    // What the device figures came out as, once per process.
    //
    // Without this the numbers a guest sees can only be recovered by reading files off the
    // device, and when they are wrong there is nothing to compare against. The last round
    // of this investigation had "Memory = 8000mb" from Unity and no KuDroid-side record of
    // what KuDroid believed — so the 8192000 kB literal had to be found by matching the
    // arithmetic backwards.
    {
        char line[320];
        std::snprintf(line, sizeof(line),
                      "device-info cores=%u perf=%u eff=%u perf_khz=%u eff_khz=%u "
                      "cpu_measured=%d mem_total_mb=%llu mem_avail_mb=%llu "
                      "mem_measured=%d",
                      cpu.total_cores, cpu.performance_cores, cpu.efficiency_cores,
                      cpu.performance_max_khz, cpu.efficiency_max_khz,
                      cpu.measured ? 1 : 0,
                      static_cast<unsigned long long>(mem.total_bytes / (1024ull * 1024ull)),
                      static_cast<unsigned long long>(mem.available_bytes / (1024ull * 1024ull)),
                      mem.measured ? 1 : 0);
        kudroid_android_log_message(4, "KuDroidDevice", line);
    }

    for (const auto& entry : files) {
        std::string path = root + "/" + entry.path;
        std::filesystem::path parent = std::filesystem::path(path).parent_path();
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);

        std::string current;
        if (!entry.authoritative) {
            std::ifstream input(path, std::ios::binary);
            if (input) {
                std::stringstream buffer;
                buffer << input.rdbuf();
                current = buffer.str();
            }
            input.close();
        }

        if (current.empty() ||
            (std::string(entry.path) == "system/build.prop" && current.find("ro.build.fingerprint") == std::string::npos && current.find("ro.custom.edit") == std::string::npos)) {
            current = entry.content;
        } else if (std::string(entry.path) == "proc/mounts") {

            std::string required = entry.content;
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
    
    // write certificate package ca
    std::filesystem::path cacertPath = std::filesystem::path(root) / "system/etc/security/cacerts/cacert.pem";
    if (!std::filesystem::exists(cacertPath)) {
        std::ofstream cacertOut(cacertPath, std::ios::binary);
        if (cacertOut) {
            cacertOut.write(reinterpret_cast<const char*>(cacert_pem), cacert_pem_len);
        }
    }
    
    return true;
}

// ── jar:/file:archive!/entry fallback: extract straight out of the archive ZIP ──
//
// The three loose-file candidates above assume every asset was extracted to disk.
// A Unity game that ships its whole content stream inside the APK (Addressables
// over Play Asset Delivery) has entries that exist in no loose directory: the URL
// then 404s and the engine boots to a splash with nothing to load — ULTRAKILL's
// GameBuildSettings.json was exactly this, and the 500 MB behind it never flowed.
// The archive is a ZIP that is already on disk, so the entry can be served from it.

constexpr uint64_t kJarEntryMaxUncompressed = 512ull * 1024 * 1024;  // 512 MB cap

uint64_t fnv1a64_str(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string url_percent_decode(const std::string& in) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size() && hex(in[i + 1]) >= 0 && hex(in[i + 2]) >= 0) {
            out.push_back(static_cast<char>((hex(in[i + 1]) << 4) | hex(in[i + 2])));
            i += 2;
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

// "jar:file:///a/b.apk!/x/y" -> {"/a/b.apk", "x/y"}; also "file:///a/b.apk!/x" and
// "jar:/a/b.apk!/x". Both empty when the shape is none of these.
std::pair<std::string, std::string> split_archive_url(const char* originalPath) {
    std::string p = originalPath;
    // Callers above prepend "/" (absolute-path normalization) before jar: URLs
    // reach here: "/jar:file://...". Strip it — a bare "/data/..." path never
    // starts with "/jar:" so plain paths are unaffected.
    while (p.size() > 4 && p[0] == '/' && p.rfind("/jar:", 0) == 0) p.erase(0, 1);
    if (p.rfind("jar:", 0) == 0) p.erase(0, 4);
    const size_t bang = p.find("!/");
    if (bang == std::string::npos) return {};
    std::string archive = url_percent_decode(p.substr(0, bang));
    std::string entry = url_percent_decode(p.substr(bang + 2));
    if (archive.rfind("file://", 0) == 0) archive.erase(0, 7);
    else if (archive.rfind("file:", 0) == 0) archive.erase(0, 5);
    while (!archive.empty() && archive.back() == '/') archive.pop_back();
    while (!entry.empty() && entry[0] == '/') entry.erase(0, 1);
    if (archive.empty() || entry.empty()) return {};
    return {archive, entry};
}

uint16_t zip_read16(const std::uint8_t* b, size_t o) {
    return static_cast<uint16_t>(b[o] | (b[o + 1] << 8));
}
uint32_t zip_read32(const std::uint8_t* b, size_t o) {
    return static_cast<uint32_t>(b[o]) | (static_cast<uint32_t>(b[o + 1]) << 8) |
           (static_cast<uint32_t>(b[o + 2]) << 16) | (static_cast<uint32_t>(b[o + 3]) << 24);
}

// Locate the End Of Central Directory record: its byte offset in the file, or npos.
size_t zip_find_eocd(std::FILE* f, long long file_size) {
    constexpr size_t kMaxComment = 65535;
    const size_t kScan =
        static_cast<size_t>(std::min<long long>(file_size, kMaxComment + 22));
    std::vector<uint8_t> tail(kScan);
    if (std::fseek(f, static_cast<long>(file_size - static_cast<long long>(kScan)), SEEK_SET) != 0)
        return std::string::npos;
    if (std::fread(tail.data(), 1, kScan, f) != kScan) return std::string::npos;
    const size_t base = static_cast<size_t>(file_size - static_cast<long long>(kScan));
    // EOCD is at least 22 bytes and sits at the very end, comment notwithstanding.
    for (size_t i = kScan - 22;; --i) {
        if (tail[i] == 0x50 && tail[i + 1] == 0x4B && tail[i + 2] == 0x05 && tail[i + 3] == 0x06) {
            const uint16_t comment_len = zip_read16(tail.data(), i + 20);
            if (base + i + 22 + comment_len == static_cast<size_t>(file_size)) {
                return base + i;
            }
        }
        if (i == 0) break;
    }
    return std::string::npos;
}

// Extract `entry` from the ZIP at `archivePath` into `destPath`. Returns the
// uncompressed byte count, or 0 on any failure (missing entry, unsupported method,
// IO error). A hand-rolled reader over stdio, not minizip: the extractor loads whole
// archives into memory, which is right for install and wrong for serving a single
// entry out of a 900 MB APK on demand.
uint64_t zip_extract_entry(const std::string& archivePath, const std::string& entry,
                           const std::string& destPath) {
    std::FILE* f = std::fopen(archivePath.c_str(), "rb");
    if (f == nullptr) return 0;
    struct FileCloser {
        std::FILE* f;
        ~FileCloser() { std::fclose(f); }
    } closer{f};

    if (std::fseek(f, 0, SEEK_END) != 0) return 0;
    const long long size = std::ftell(f);
    if (size < 22) return 0;
    const size_t eocd = zip_find_eocd(f, size);
    if (eocd == std::string::npos) return 0;
    uint8_t e[22];
    if (std::fseek(f, static_cast<long>(eocd), SEEK_SET) != 0) return 0;
    if (std::fread(e, 1, 22, f) != 22) return 0;
    const uint16_t total_entries = zip_read16(e, 10);
    const uint32_t cd_offset = zip_read32(e, 16);

    struct Hit {
        bool valid = false;
        uint16_t method = 0;
        uint32_t csize = 0;
        uint32_t usize = 0;
        uint32_t local_off = 0;
    };
    auto walk = [&](bool fold_case) -> Hit {
        if (std::fseek(f, static_cast<long>(cd_offset), SEEK_SET) != 0) return {};
        for (uint16_t n = 0; n < total_entries; ++n) {
            uint8_t h[46];
            if (std::fread(h, 1, 46, f) != 46) return {};
            if (!(h[0] == 'P' && h[1] == 'K' && h[2] == 1 && h[3] == 2)) return {};
            const uint16_t name_len = zip_read16(h, 28);
            const uint16_t extra_len = zip_read16(h, 30);
            const uint16_t comment_len = zip_read16(h, 32);
            Hit hit;
            hit.method = zip_read16(h, 10);
            hit.csize = zip_read32(h, 20);
            hit.usize = zip_read32(h, 24);
            hit.local_off = zip_read32(h, 42);
            std::string name(name_len, 0);
            if (name_len > 0 && std::fread(name.data(), 1, name_len, f) != name_len) return {};
            if (std::fseek(f, extra_len + comment_len, SEEK_CUR) != 0) return {};
            const bool eq = fold_case
                                ? name.size() == entry.size() &&
                                      std::equal(name.begin(), name.end(), entry.begin(),
                                                 [](char a, char b) {
                                                     return std::tolower(static_cast<unsigned char>(a)) ==
                                                            std::tolower(static_cast<unsigned char>(b));
                                                 })
                                : name == entry;
            if (eq) {
                hit.valid = true;
                return hit;
            }
        }
        return {};
    };
    Hit hit = walk(false);
    if (!hit.valid) hit = walk(true);  // ZIP writers disagree on case; Android's FS does not
    if (!hit.valid) return 0;
    if (hit.usize > kJarEntryMaxUncompressed) return 0;

    // Local header: sizes there can be zero when a data descriptor follows, so the
    // central-directory figures are the ones used; the local header only yields the
    // true start of the data.
    if (std::fseek(f, static_cast<long>(hit.local_off), SEEK_SET) != 0) return 0;
    uint8_t lh[30];
    if (std::fread(lh, 1, 30, f) != 30) return 0;
    if (!(lh[0] == 'P' && lh[1] == 'K' && lh[2] == 3 && lh[3] == 4)) return 0;
    const uint16_t l_nlen = zip_read16(lh, 26);
    const uint16_t l_elen = zip_read16(lh, 28);
    if (std::fseek(f, static_cast<long>(l_nlen) + static_cast<long>(l_elen), SEEK_CUR) != 0)
        return 0;

    std::vector<uint8_t> comp(hit.csize);
    if (hit.csize > 0 && std::fread(comp.data(), 1, hit.csize, f) != hit.csize) return 0;

    std::vector<uint8_t> data;
    if (hit.method == 0) {
        data = std::move(comp);
        data.resize(hit.usize);
    } else if (hit.method == 8) {
        data.resize(hit.usize > 0 ? hit.usize : 1);
        z_stream zs = {};
        zs.next_in = comp.data();
        zs.avail_in = static_cast<uInt>(comp.size());
        zs.next_out = data.data();
        zs.avail_out = static_cast<uInt>(data.size());
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return 0;
        const int rc = inflate(&zs, Z_FINISH);
        inflateEnd(&zs);
        if (rc != Z_STREAM_END || zs.total_out != hit.usize) return 0;
        data.resize(zs.total_out);
    } else {
        return 0;  // bzip2/encrypted: unsupported
    }

    std::FILE* out = std::fopen(destPath.c_str(), "wb");
    if (out == nullptr) return 0;
    const size_t wrote = data.empty() ? 0 : std::fwrite(data.data(), 1, data.size(), out);
    std::fclose(out);
    if (wrote != data.size()) {
        std::remove(destPath.c_str());
        return 0;
    }
    return data.size();
}

// A game resolves assets one file at a time, so the naive path here was fopen+EOCD+
// central-directory walk per stat and per listing — over 1,100 scans of the same APK on
// one cold start. One in-memory index per archive turns every later query into a hash
// lookup and lets zip_list_dir_entries stop re-reading the directory at all.
struct ZipEntryMeta {
    uint64_t payloadOffset = 0;
    uint32_t uncompressedSize = 0;
    uint16_t compressionMethod = 0;
};
struct ZipArchiveIndex {
    std::unordered_map<std::string, ZipEntryMeta> entries;
    // Directory prefix ("assets/shaders/") -> immediate child names, exactly the slice
    // zip_list_dir_entries serves. Built alongside the entry map so listings are free.
    std::unordered_map<std::string, std::vector<std::string>> dirChildren;
};

std::string zip_index_key(std::string_view entry) {
    std::string key(entry);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return key;
}

// Entry names are matched case-insensitively below, so names reaching the index are
// lower-cased too; dirChildren keys are lower-cased prefixes, values keep original case.
ZipArchiveIndex build_zip_index(std::FILE* f) {
    ZipArchiveIndex index;
    if (std::fseek(f, 0, SEEK_END) != 0) return index;
    const long long size = std::ftell(f);
    if (size < 22) return index;
    const size_t eocd = zip_find_eocd(f, size);
    if (eocd == std::string::npos) return index;
    uint8_t e[22];
    if (std::fseek(f, static_cast<long>(eocd), SEEK_SET) != 0) return index;
    if (std::fread(e, 1, 22, f) != 22) return index;
    const uint16_t total_entries = zip_read16(e, 10);
    const uint32_t cd_offset = zip_read32(e, 16);
    index.entries.reserve(total_entries * 2 + 1);

    if (std::fseek(f, static_cast<long>(cd_offset), SEEK_SET) != 0) return index;
    for (uint16_t n = 0; n < total_entries; ++n) {
        uint8_t h[46];
        if (std::fread(h, 1, 46, f) != 46) break;
        if (!(h[0] == 'P' && h[1] == 'K' && h[2] == 1 && h[3] == 2)) break;
        const uint16_t name_len = zip_read16(h, 28);
        const uint16_t extra_len = zip_read16(h, 30);
        const uint16_t comment_len = zip_read16(h, 32);
        std::string name(name_len, 0);
        if (name_len > 0 && std::fread(name.data(), 1, name_len, f) != name_len) break;
        if (std::fseek(f, extra_len + comment_len, SEEK_CUR) != 0) break;

        const bool is_dir = !name.empty() && name.back() == '/';
        ZipEntryMeta meta;
        meta.compressionMethod = zip_read16(h, 10);
        meta.uncompressedSize = zip_read32(h, 24);
        const uint32_t local_off = zip_read32(h, 42);

        // The central directory does not say where the data starts — the local header
        // does, and its name/extra lengths are allowed to differ from the CD's. Resolving
        // the payload offset here keeps every later stat a pure memory lookup.
        if (!is_dir) {
            const long saved = std::ftell(f);
            if (std::fseek(f, static_cast<long>(local_off), SEEK_SET) != 0) continue;
            uint8_t lh[30];
            if (std::fread(lh, 1, 30, f) != 30) continue;
            if (!(lh[0] == 'P' && lh[1] == 'K' && lh[2] == 3 && lh[3] == 4)) continue;
            const uint16_t l_nlen = zip_read16(lh, 26);
            const uint16_t l_elen = zip_read16(lh, 28);
            meta.payloadOffset = static_cast<uint64_t>(local_off) + 30 + l_nlen + l_elen;
            std::fseek(f, saved, SEEK_SET);
        }

        // First occurrence wins, matching the old sequential walk's first match.
        index.entries.try_emplace(zip_index_key(name), meta);
        if (is_dir) {
            std::string dir = name.substr(0, name.size() - 1);
            if (!dir.empty()) {
                const size_t slash = dir.rfind('/');
                const std::string parent = slash == std::string::npos ? "" : dir.substr(0, slash + 1);
                index.dirChildren[zip_index_key(parent)].push_back(dir.substr(slash + 1));
            }
        } else if (const size_t slash = name.rfind('/'); slash != std::string::npos) {
            index.dirChildren[zip_index_key(name.substr(0, slash + 1))].push_back(
                name.substr(slash + 1));
        }
    }
    return index;
}

// Build once per archive, guarded; a failed build is cached as an empty index so a
// broken file is re-probed at stat rate, not re-parsed at stat rate.
// Read-mostly: shared_mutex lets concurrent statters proceed in parallel; only
// the one-time build per archive takes the write lock.
const ZipArchiveIndex* get_or_build_zip_index(const std::string& archivePath) {
    struct Cache {
        std::shared_mutex mtx;
        std::unordered_map<std::string, ZipArchiveIndex> byArchive;
    };
    static Cache cache;
    {
        std::shared_lock<std::shared_mutex> lock(cache.mtx);
        const auto it = cache.byArchive.find(archivePath);
        if (it != cache.byArchive.end()) return &it->second;
    }
    std::unique_lock<std::shared_mutex> lock(cache.mtx);
    auto [it, inserted] = cache.byArchive.try_emplace(archivePath);
    if (inserted) {
        std::FILE* f = std::fopen(archivePath.c_str(), "rb");
        if (f != nullptr) {
            it->second = build_zip_index(f);
            std::fclose(f);
        }
    }
    return &it->second;
}

bool zip_stat_entry(const std::string& archivePath, const std::string& entry,
                    uint64_t* outOffset, uint64_t* outSize, uint16_t* outMethod) {
    const ZipArchiveIndex* index = get_or_build_zip_index(archivePath);
    if (index == nullptr) return false;
    const auto it = index->entries.find(zip_index_key(entry));
    if (it == index->entries.end()) return false;
    if (outOffset) *outOffset = it->second.payloadOffset;
    if (outSize) *outSize = it->second.uncompressedSize;
    if (outMethod) *outMethod = it->second.compressionMethod;
    return true;
}

std::vector<std::string> zip_list_dir_entries(const std::string& archivePath,
                                              const std::string& dirPrefix) {
    std::vector<std::string> result;
    const ZipArchiveIndex* index = get_or_build_zip_index(archivePath);
    if (index == nullptr) return result;

    std::string prefix = dirPrefix;
    while (!prefix.empty() && prefix[0] == '/') prefix.erase(0, 1);
    if (!prefix.empty() && prefix.back() != '/') prefix.push_back('/');
    const auto it = index->dirChildren.find(zip_index_key(prefix));
    if (it != index->dirChildren.end()) result = it->second;
    return result;
}

// Cache home for extracted jar entries. Lives under android_root/data/cache: inside
// the VFS root, writable, and already a remapped prefix a guest cannot see.
std::string extract_jar_entry_to_cache(const std::string& archivePath,
                                       const std::string& entryName,
                                       const std::string& androidRoot) {
    std::error_code ec;
    if (!std::filesystem::exists(archivePath, ec) ||
        !std::filesystem::is_regular_file(archivePath, ec)) {
        return {};
    }
    // Serialise extraction: the same URL can be resolved from several threads during
    // startup, and double-extracting the same entry is wasted IO.
    static std::mutex s_jarMtx;
    std::lock_guard<std::mutex> lock(s_jarMtx);

    const std::string dir = androidRoot + "/data/cache/jar_entries";
    std::filesystem::create_directories(dir, ec);
    if (ec) return {};

    // One cache file per (archive, entry) pair. The hash keeps the tree flat; the
    // entry's basename is kept so a log line reads like the asset it served.
    std::string base = entryName;
    const size_t slash = base.find_last_of('/');
    if (slash != std::string::npos) base = base.substr(slash + 1);
    if (base.size() > 64) base.resize(64);
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx",
                  static_cast<unsigned long long>(fnv1a64_str(archivePath + "\x01" + entryName)));
    const std::string dest = dir + "/" + hex + "_" + base;

    if (std::filesystem::exists(dest, ec) && std::filesystem::is_regular_file(dest, ec)) {
        return dest;
    }
    const uint64_t n = zip_extract_entry(archivePath, entryName, dest);
    if (n == 0) {
        // Leave no zero-byte marker: a stale cache file would be served forever after.
        std::error_code rm;
        std::filesystem::remove(dest, rm);
        return {};
    }
    return dest;
}

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
    }
    if (!found.empty()) {
        static std::atomic<int> s_obbFb{0};
        if (s_obbFb.load() < 10) {
            ++s_obbFb;
            std::fprintf(stderr, "[KuDroidVFS] obb fallback: %s -> %s\n",
                         mapped.c_str(), found.c_str());
        }
        vfsTrace("OBB fallback: " + mapped + " -> " + found);
    }
    return found;
}

std::string VFSPathRemapper::remap(const char* originalPath) const {
    if (!originalPath) return {};

    // Resolve jar: and file:archive!/entry URLs to loose asset files.
    if (std::strncmp(originalPath, "jar:", 4) == 0 || std::strstr(originalPath, "!/") != nullptr) {
        const char* excl = std::strchr(originalPath, '!');
        if (excl != nullptr) {
            const char* sub = excl + 1;
            while (*sub == '/') ++sub;
            const char* entry = sub;
            if (std::strncmp(entry, "assets/", 7) == 0) {
                entry += 7;
                while (*entry == '/') ++entry;
            }
            const char* assetsDir = kudroid_get_assets_dir();
            if (assetsDir && *assetsDir) {
                std::error_code ec;
                std::string candidate = normalizePathString(std::string(assetsDir) + "/" + entry);
                if (std::filesystem::exists(candidate, ec)) {
                    vfsTrace("Remapped JAR asset: " + std::string(originalPath) + " -> " + candidate);
                    return candidate;
                }
                std::string altCandidate = normalizePathString(std::string(assetsDir) + "/" + sub);
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
                                std::fprintf(stderr,
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
                        std::fprintf(stderr,
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
    if (traceOpen) {
        std::fprintf(stderr, "[KuDroidVFS] open(%s, flags=0x%x)\n", path, flags);
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
        std::fprintf(stderr, "[KuDroidVFS] open -> %d (%s)\n", result,
                     result >= 0 ? "OK" : std::strerror(errno));
    }
    return result;
}

int vfs_open64(const char* path, int flags, mode_t mode) { return vfs_open(path, flags, mode); }

static void track_apk_stream(FILE* f);

extern std::mutex g_freadVolMtx;
extern std::map<FILE*, std::string> g_freadPaths;

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
           (result ? "OK" : std::strerror(errno)));    if (path != nullptr &&
        (std::strstr(path, "assets/") != nullptr || std::strstr(path, ".apk") != nullptr ||
         std::strstr(path, ".bank") != nullptr || std::strstr(path, ".fsb") != nullptr ||
         std::strstr(path, ".mp3") != nullptr || std::strstr(path, ".ogg") != nullptr ||
         std::strstr(path, ".wav") != nullptr || std::strstr(path, ".mp4") != nullptr ||
         std::strstr(path, ".webm") != nullptr || std::strstr(path, "jar:") != nullptr ||
         std::strstr(path, ".json") != nullptr || std::strstr(path, "catalog") != nullptr ||
         std::strstr(path, "/files/") != nullptr || std::strstr(path, "/sdcard/") != nullptr)) {
        std::fprintf(stderr, "[KuDroidVFS] fopen(%s) -> %s\n", path,
                     result ? "OK" : std::strerror(errno));
    }
    if (result != nullptr && path != nullptr) {
        const size_t len = std::strlen(path);
        if (len >= 8 && std::strcmp(path + len - 8, "base.apk") == 0) {
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
            std::fprintf(stderr, "[KuDroidVFS] open miss: ~%s\n",
                         mapped.c_str() +
                             VFSPathRemapper::getInstance().androidRoot().size());
        }
    }
    return result;
}

FILE* vfs_fopen64(const char* path, const char* mode) { return vfs_fopen(path, mode); }

// Tracked APK streams: guest fread bypasses the syscall layer, so volume on
// base.apk (catalog/asset bytes) is otherwise invisible. Lock-free lookup,
// locked mutation (open/close are rare, reads are hot).
namespace {
constexpr int kTrackedStreams = 8;
std::atomic<uintptr_t> g_apkStreams[kTrackedStreams];
std::mutex g_apkStreamsMtx;

}  // namespace

static void track_apk_stream(FILE* f) {
    if (f == nullptr) return;
    std::lock_guard<std::mutex> lock(g_apkStreamsMtx);
    for (int i = 0; i < kTrackedStreams; ++i) {
        uintptr_t empty = 0;
        if (g_apkStreams[i].compare_exchange_strong(empty,
                                                    reinterpret_cast<uintptr_t>(f))) {
            return;
        }
    }
}

static bool is_apk_stream(FILE* f) {
    const auto p = reinterpret_cast<uintptr_t>(f);
    for (int i = 0; i < kTrackedStreams; ++i) {
        if (g_apkStreams[i].load(std::memory_order_relaxed) == p) return true;
    }
    return false;
}

// Read volume per FILE path: bulk flow through fread (Unity's main read path)
// shows here. The hot read path takes NO lock: each thread batches into a
// thread-local slot keyed by stream, merged under the mutex at 256KB or on
// stream change. A global fclose epoch guards FILE* address reuse — without
// it a recycled address would silently attribute bytes to the dead file's
// path. Open/close stay locked (rare); only the merge path locks.
std::mutex g_freadVolMtx;
std::map<FILE*, std::string> g_freadPaths;
std::map<std::string, std::pair<uint64_t, uint64_t>> g_freadVol;
std::atomic<uint64_t> g_freadVolTotal{0};
uint64_t g_freadVolNextLog = 5ULL * 1024 * 1024;
std::atomic<uint64_t> g_freadEpoch{0};

namespace {
// Merged under g_freadVolMtx. Copies the map for the top-5 sort OUTSIDE the
// lock: sorting under it stalled every reader each 5MB.
void fread_vol_report_locked() {
    using Entry = std::pair<std::string, std::pair<uint64_t, uint64_t>>;
    std::vector<Entry> top(g_freadVol.begin(), g_freadVol.end());
    std::string total = std::to_string(g_freadVolTotal.load(std::memory_order_relaxed)) + "B";
    {
        std::lock_guard<std::mutex> lock(g_freadVolMtx);
        top.assign(g_freadVol.begin(), g_freadVol.end());
        total = std::to_string(g_freadVolTotal.load(std::memory_order_relaxed)) + "B";
    }
    std::sort(top.begin(), top.end(),
              [](const Entry& a, const Entry& b) { return a.second.first > b.second.first; });
    std::string line = "fread total=" + total;
    for (size_t i = 0; i < top.size() && i < 5; ++i) {
        const std::string& p = top[i].first;
        line += " | " + (p.size() > 60 ? "..." + p.substr(p.size() - 57) : p) +
                "=" + std::to_string(top[i].second.first) + "B/" +
                std::to_string(top[i].second.second) + "ops";
    }
    std::fprintf(stderr, "[KuDroidIO] %s\n", line.c_str());
}

struct VolBatch {
    FILE* stream = nullptr;
    std::string path;
    uint64_t epoch = 0;
    uint64_t bytes = 0;
    uint64_t ops = 0;
};
thread_local VolBatch t_volbatch;

void fread_vol_flush() {
    if (t_volbatch.bytes == 0 || t_volbatch.path.empty()) {
        t_volbatch.bytes = 0;
        t_volbatch.ops = 0;
        return;
    }
    bool report = false;
    {
        std::lock_guard<std::mutex> lock(g_freadVolMtx);
        auto& e = g_freadVol[t_volbatch.path];
        e.first += t_volbatch.bytes;
        e.second += t_volbatch.ops;
        const uint64_t total =
            g_freadVolTotal.fetch_add(t_volbatch.bytes, std::memory_order_relaxed) +
            t_volbatch.bytes;
        if (total >= g_freadVolNextLog) {
            g_freadVolNextLog += 5ULL * 1024 * 1024;
            report = true;
        }
    }
    t_volbatch.bytes = 0;
    t_volbatch.ops = 0;
    if (report) fread_vol_report_locked();
}
}  // namespace

size_t vfs_fread(void* buf, size_t size, size_t count, FILE* stream) {
    const size_t n = std::fread(buf, size, count, stream);
    if (n > 0 && is_apk_stream(stream)) {
        static std::atomic<int> s_logged{0};
        static std::atomic<unsigned long long> s_bytes{0};
        static const auto s_start = std::chrono::steady_clock::now();
        const unsigned long long total =
            s_bytes.fetch_add(n * size) + n * size;
        const unsigned long long ms =
            static_cast<unsigned long long>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - s_start)
                    .count());
        // First 25 lines keep header detail; totals every 1MB after that so
        // later content reads (catalog, bundles) stay visible past the sniff.
        if (s_logged.load() < 25) {
            ++s_logged;
            std::fprintf(stderr, "[KuDroidApkF] fread bytes=%zu\n", n * size);
        } else if (total / (1024 * 1024) != (total - n * size) / (1024 * 1024)) {
            std::fprintf(stderr, "[KuDroidApkF] fread total=%lluMB t=%llums\n",
                         total / (1024 * 1024), ms);
        }
        // Magic sniff: FMOD rejects every bank it is handed ("Error loading
        // file") while meshes from the same APK render. The bytes are read
        // from the buffer just filled — zero extra I/O — and ftell is
        // userspace. Starts past 4MB of flow so startup probing (EOCD,
        // central directory, catalog) does not consume the budget; what
        // remains is bundle/clip territory. If clip reads land on
        // FSB5/OggS, the bytes are right and FMOD is at fault
        // environmentally; central-directory/EOCD/zeros instead means
        // wrong-file/wrong-offset upstream.
        static std::atomic<int> s_magic{0};
        if (total > 4ULL * 1024 * 1024 && s_magic.load() < 30 && buf != nullptr &&
            n * size >= 4) {
            ++s_magic;
            const long off = std::ftell(stream);
            const auto* b = static_cast<const unsigned char*>(buf);
            std::fprintf(stderr,
                         "[KuDroidApkF] magic off=%ld n=%zu %02x%02x%02x%02x '%c%c%c%c'\n",
                         off >= 0 ? off - static_cast<long>(n * size) : -1,
                         n * size, b[0], b[1], b[2], b[3],
                         b[0] >= 32 && b[0] < 127 ? b[0] : '.',
                         b[1] >= 32 && b[1] < 127 ? b[1] : '.',
                         b[2] >= 32 && b[2] < 127 ? b[2] : '.',
                         b[3] >= 32 && b[3] < 127 ? b[3] : '.');
        }
    }
    if (n > 0) {
        // Lock-free volume accounting (see above): resolve the path only on
        // stream change or fclose epoch bump, batch bytes/ops thread-locally.
        const uint64_t ep = g_freadEpoch.load(std::memory_order_relaxed);
        if (t_volbatch.stream != stream || t_volbatch.epoch != ep) {
            fread_vol_flush();
            t_volbatch.stream = stream;
            t_volbatch.epoch = ep;
            std::lock_guard<std::mutex> lock(g_freadVolMtx);
            const auto it = g_freadPaths.find(stream);
            t_volbatch.path = (it != g_freadPaths.end()) ? it->second : std::string();
        }
        if (!t_volbatch.path.empty()) {
            t_volbatch.bytes += n * size;
            ++t_volbatch.ops;
            if (t_volbatch.bytes >= 256ULL * 1024) fread_vol_flush();
        }
    }
    if (n * size >= 1048576) {
        static std::atomic<int> s_big{0};
        if (s_big.load() < 15) {
            ++s_big;
            std::fprintf(stderr, "[KuDroidIO] big fread bytes=%zu\n", n * size);
        }
    }
    return n;
}

int vfs_fclose(FILE* stream) {
    if (stream != nullptr) {
        // Flush this thread's batch for the stream before the map entry goes
        // away, then bump the epoch so other threads' stale FILE* caches
        // re-resolve instead of attributing to a recycled address.
        if (t_volbatch.stream == stream) fread_vol_flush();
        g_freadEpoch.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(g_apkStreamsMtx);
        for (int i = 0; i < kTrackedStreams; ++i) {
            if (g_apkStreams[i].load(std::memory_order_relaxed) ==
                reinterpret_cast<uintptr_t>(stream)) {
                g_apkStreams[i].store(0, std::memory_order_relaxed);
            }
        }
        std::lock_guard<std::mutex> vlock(g_freadVolMtx);
        g_freadPaths.erase(stream);
    }
    return std::fclose(stream);
}

int vfs_fseek(FILE* stream, long offset, int whence) {
    const int rc = std::fseek(stream, offset, whence);
    if (rc == 0 && is_apk_stream(stream)) {
        static std::atomic<int> s_logged{0};
        if (s_logged.load() < 40) {
            ++s_logged;
            std::fprintf(stderr, "[KuDroidApkF] fseek offset=%ld whence=%d\n", offset,
                         whence);
        }
    }
    return rc;
}

int vfs_fseeko(FILE* stream, off_t offset, int whence) {
    return ::fseeko(stream, offset, whence);
}

long vfs_ftell(FILE* stream) {
    return std::ftell(stream);
}

off_t vfs_ftello(FILE* stream) {
    return ::ftello(stream);
}

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

} // namespace kudroid

extern "C" void kudroid_run_vfs_self_test(void) {
    std::fprintf(stdout, "%s", kudroid::run_vfs_self_test().c_str());
}
