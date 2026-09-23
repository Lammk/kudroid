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
#include <cstdarg>
#include <cstdio>
#include <map>
#include <memory>
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
#include <pthread.h>
#include <sstream>
#include <unistd.h>
#include <vector>
#include <zlib.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "kudroid/kudroid_bridge.h"

// Defined in SyscallShim.cpp. Declared here rather than through a header because the
// remapper otherwise has no reason to depend on the syscall layer; it needs this only to
// record what device figures the pseudo-files were written from.
extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);

// One diagnostic line: boot-relative time and the guest thread name in front of the
// caller's text, so the VFS/audio stream can be placed between the [KuDroidBoot] phase
// marks and beside the Unity side of the log. A single fprintf keeps the line whole —
// two writes would let another thread interleave into the middle of it. The formats
// already end in '\n'.
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
static void ktraceLine(const char* fmt, ...) {
    char body[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "%s%s", kudroid_trace_stamp(), body);
}

// Declared in elf_loader.cpp; spelled C so the name cannot be captured by a namespace.
extern "C" bool kudroid_lookup_guest_module(void* addr, char* out, std::size_t outSize);

// " from=libunity.so+0x1a2b40" for the guest frame that called into the file layer.
//
// The guest's fopen/fread/open/lseek are bound straight to the functions below (see the
// symbol table in SyscallShim), so __builtin_return_address(0) *there* is the guest's own
// return address — the code that asked for the file. Without it a file trace says what was
// read but never who read it, and for audio that is the whole question: Unity's resource
// reader and FMOD's own loader look byte-for-byte identical in the trace. The loader
// already knows the module ranges, so this is a lookup, not a per-app guess.
static const char* trace_caller_text(const void* ra) {
    if (ra == nullptr) return "";
    static thread_local char text[192];
    char full[384];
    full[0] = '\0';
    if (kudroid_lookup_guest_module(const_cast<void*>(ra), full, sizeof(full))) {
        const char* slash = std::strrchr(full, '/');
        std::snprintf(text, sizeof(text), " from=%s", slash != nullptr ? slash + 1 : full);
    } else {
        std::snprintf(text, sizeof(text), " from=host:%p", ra);
    }
    return text;
}

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
    if (::fseeko(f, file_size - static_cast<long long>(kScan), SEEK_SET) != 0)
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
    const long long size = ::ftello(f);
    if (size < 22) return 0;
    const size_t eocd = zip_find_eocd(f, size);
    if (eocd == std::string::npos) return 0;
    uint8_t e[22];
    if (::fseeko(f, static_cast<off_t>(eocd), SEEK_SET) != 0) return 0;
    if (std::fread(e, 1, 22, f) != 22) return 0;
    const uint16_t total_entries = zip_read16(e, 10);
    const uint32_t cd_offset = zip_read32(e, 16);
    // Zip64 uses 0xFFFF/0xFFFFFFFF sentinels pointing at a Zip64 EOCD locator
    // this reader does not parse: say so loudly instead of walking garbage as
    // a central directory and 404ing every entry in it.
    if (total_entries == 0xFFFF || cd_offset == 0xFFFFFFFFu) {
        ktraceLine("[KuDroidVFS] zip64 archive not supported: %s\n",
                     archivePath.c_str());
        return 0;
    }

    struct Hit {
        bool valid = false;
        uint16_t method = 0;
        uint32_t crc = 0;
        uint32_t csize = 0;
        uint32_t usize = 0;
        uint32_t local_off = 0;
    };
    auto walk = [&](bool fold_case) -> Hit {
        if (::fseeko(f, static_cast<off_t>(cd_offset), SEEK_SET) != 0) return {};
        for (uint16_t n = 0; n < total_entries; ++n) {
            uint8_t h[46];
            if (std::fread(h, 1, 46, f) != 46) return {};
            if (!(h[0] == 'P' && h[1] == 'K' && h[2] == 1 && h[3] == 2)) return {};
            const uint16_t name_len = zip_read16(h, 28);
            const uint16_t extra_len = zip_read16(h, 30);
            const uint16_t comment_len = zip_read16(h, 32);
            Hit hit;
            hit.method = zip_read16(h, 10);
            hit.crc = zip_read32(h, 16);  // central dir: crc32 at 16 (14 is mod date)
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
    if (::fseeko(f, static_cast<off_t>(hit.local_off), SEEK_SET) != 0) return 0;
    uint8_t lh[30];
    if (std::fread(lh, 1, 30, f) != 30) return 0;
    if (!(lh[0] == 'P' && lh[1] == 'K' && lh[2] == 3 && lh[3] == 4)) return 0;
    const uint16_t l_nlen = zip_read16(lh, 26);
    const uint16_t l_elen = zip_read16(lh, 28);
    if (::fseeko(f, static_cast<off_t>(l_nlen) + static_cast<off_t>(l_elen), SEEK_CUR) != 0)
        return 0;
    const long long dataOff = ::ftello(f);
    if (dataOff < 0) return 0;
    // Cap the compressed input too: the usize cap below does not bound csize,
    // and a lying header would otherwise malloc first and fail later.
    if (hit.csize > 512ull * 1024 * 1024) return 0;

    // STORED input streams straight off the archive in 1MB chunks (no full
    // copy: a 500MB entry would cost 500MB of heap). DEFLATED still buffers
    // its input — bounded by the cap above — while output streams.
    std::vector<uint8_t> comp;
    if (hit.method == 8 && hit.csize > 0) {
        comp.resize(hit.csize);
        if (std::fread(comp.data(), 1, hit.csize, f) != hit.csize) return 0;
    }

    // Stream to a side file and rename over the destination only after the
    // bytes verify: a crash or kill mid-extract must never leave a partial
    // file that the next run serves as good. That was the "loads a few
    // segments then dies forever after" shape — one torn 108MB bundle cached
    // once, trusted on every later hit by mere existence. Chunked 1MB writes
    // also cap RAM (the old code held compressed+output fully: ~1GB for
    // data.unity3d). Resume is at file granularity and automatic: a torn run
    // leaves only the .part orphan, and the next open re-extracts from the
    // archive. Byte-level resume across runs would need a crash-safe offset
    // journal for a local-disk copy that takes seconds — not worth the
    // failure modes it adds.
    const std::string part = destPath + ".part";
    std::remove(part.c_str());
    std::FILE* out = std::fopen(part.c_str(), "wb");
    if (out == nullptr) return 0;
    bool ok = false;
    uint64_t written = 0;
    uint32_t crc = crc32(0L, Z_NULL, 0);
    constexpr size_t kChunk = 1 << 20;
    std::vector<uint8_t> chunk(kChunk);
    if (hit.method == 0) {
        ok = true;
        uint64_t remaining = hit.csize;
        long long pos = dataOff;
        while (ok && remaining > 0) {
            const size_t n = static_cast<size_t>(
                std::min<uint64_t>(kChunk, remaining));
            if (::fseeko(f, static_cast<off_t>(pos), SEEK_SET) != 0) {
                ok = false;
                break;
            }
            const size_t got = std::fread(chunk.data(), 1, n, f);
            if (got == 0) {
                ok = false;
                break;
            }
            if (std::fwrite(chunk.data(), 1, got, out) != got) {
                ok = false;
                break;
            }
            crc = crc32(crc, chunk.data(), static_cast<uInt>(got));
            pos += static_cast<long long>(got);
            remaining -= got;
            written += got;
        }
        // STORED entries whose central size runs past EOF pad as zeros.
        while (ok && written < hit.usize) {
            const size_t n = static_cast<size_t>(
                std::min<uint64_t>(kChunk, hit.usize - written));
            std::memset(chunk.data(), 0, n);
            if (std::fwrite(chunk.data(), 1, n, out) != n) {
                ok = false;
                break;
            }
            crc = crc32(crc, chunk.data(), static_cast<uInt>(n));
            written += n;
        }
    } else if (hit.method == 8) {
        z_stream zs = {};
        zs.next_in = comp.data();
        zs.avail_in = static_cast<uInt>(comp.size());
        if (inflateInit2(&zs, -MAX_WBITS) == Z_OK) {
            ok = true;
            int rc = Z_OK;
            while (ok && rc != Z_STREAM_END) {
                zs.next_out = chunk.data();
                zs.avail_out = static_cast<uInt>(chunk.size());
                rc = inflate(&zs, Z_NO_FLUSH);
                if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
                    ok = false;
                    break;
                }
                const size_t have = chunk.size() - zs.avail_out;
                if (have > 0) {
                    if (std::fwrite(chunk.data(), 1, have, out) != have) {
                        ok = false;
                        break;
                    }
                    crc = crc32(crc, chunk.data(), static_cast<uInt>(have));
                    written += have;
                }
                if (rc == Z_BUF_ERROR && zs.avail_in == 0) break;
            }
            inflateEnd(&zs);
            if (rc != Z_STREAM_END || written != hit.usize) ok = false;
        }
    } else {
        std::fclose(out);
        std::remove(part.c_str());
        return 0;  // bzip2/encrypted: unsupported
    }
    // verify, then commit: size AND central-directory CRC before the bytes
    // become servable. Rename is atomic; a crash anywhere above leaves at
    // most the .part orphan, never a trusted partial. A zero CRC in the
    // central directory means the writer never filled it (legal for streamed
    // archives): size+fstat is the floor, crc only when known.
    if (ok && (written != hit.usize || (hit.crc != 0 && crc != hit.crc))) ok = false;
    if (ok) {
        // A counter is not proof: ENOSPC or a torn page can leave fewer bytes
        // than counted. Flush, sync, then believe fstat — not the accumulator.
        if (std::fflush(out) != 0) ok = false;
#if !defined(_WIN32)
        if (ok && ::fsync(fileno(out)) != 0) ok = false;
#endif
        if (ok) {
            struct stat st;
            if (::fstat(fileno(out), &st) != 0 ||
                static_cast<uint64_t>(st.st_size) != written) {
                ok = false;
            }
        }
    }
    std::fclose(out);
    if (!ok) {
        std::remove(part.c_str());
        return 0;
    }
    std::error_code renameEc;
    std::filesystem::rename(part, destPath, renameEc);
    if (renameEc) {
        std::remove(part.c_str());
        return 0;
    }
    return written;
}

// A game resolves assets one file at a time, so the naive path here was fopen+EOCD+
// central-directory walk per stat and per listing — over 1,100 scans of the same APK on
// one cold start. One in-memory index per archive turns every later query into a hash
// lookup and lets zip_list_dir_entries stop re-reading the directory at all.
struct ZipEntryMeta {
    uint64_t payloadOffset = 0;
    uint32_t uncompressedSize = 0;
    uint32_t compressedSize = 0;
    uint32_t crc32 = 0;
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

// Most-specific entry whose on-disk span contains `start`.
//
// The span is PHYSICAL (payloadOffset + compressedSize): a deflated entry
// occupies csize bytes in the archive, and spanning it by usize swallowed the
// entries that follow it — audio blobs were reported as global-metadata.dat
// because that deflated entry's uncompressed size reached past them. Among
// overlapping candidates (duplicate names in a repacked archive) the smallest
// span is the one whose bytes the read actually lands in.
static bool zip_entry_containing(const std::shared_ptr<const ZipArchiveIndex>& index,
                                 long long start, std::string& outName,
                                 ZipEntryMeta& outMeta) {
    if (!index) return false;
    bool found = false;
    long long bestSpan = 0;
    for (const auto& [name, meta] : index->entries) {
        const long long span = static_cast<long long>(
            meta.compressedSize != 0 ? meta.compressedSize : meta.uncompressedSize);
        if (span <= 0) continue;
        const long long eOff = static_cast<long long>(meta.payloadOffset);
        if (start >= eOff && start < eOff + span) {
            if (!found || span < bestSpan) {
                found = true;
                bestSpan = span;
                outName = name;
                outMeta = meta;
            }
        }
    }
    return found;
}

// Entry names are matched case-insensitively below, so names reaching the index are
// lower-cased too; dirChildren keys are lower-cased prefixes, values keep original case.
ZipArchiveIndex build_zip_index(std::FILE* f) {
    ZipArchiveIndex index;
    if (std::fseek(f, 0, SEEK_END) != 0) return index;
    const long long size = ::ftello(f);
    if (size < 22) return index;
    const size_t eocd = zip_find_eocd(f, size);
    if (eocd == std::string::npos) return index;
    uint8_t e[22];
    if (::fseeko(f, static_cast<off_t>(eocd), SEEK_SET) != 0) return index;
    if (std::fread(e, 1, 22, f) != 22) return index;
    const uint16_t total_entries = zip_read16(e, 10);
    const uint32_t cd_offset = zip_read32(e, 16);
    if (total_entries == 0xFFFF || cd_offset == 0xFFFFFFFFu) {
        ktraceLine("[KuDroidVFS] zip64 archive not supported (index build)\n");
        return index;
    }
    index.entries.reserve(total_entries * 2 + 1);

    if (::fseeko(f, static_cast<off_t>(cd_offset), SEEK_SET) != 0) return index;
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
        meta.compressedSize = zip_read32(h, 20);
        meta.uncompressedSize = zip_read32(h, 24);
        meta.crc32 = zip_read32(h, 16);
        const uint32_t local_off = zip_read32(h, 42);

        // The central directory does not say where the data starts — the local header
        // does, and its name/extra lengths are allowed to differ from the CD's. Resolving
        // the payload offset here keeps every later stat a pure memory lookup.
        if (!is_dir) {
            const off_t saved = ::ftello(f);
            if (::fseeko(f, static_cast<off_t>(local_off), SEEK_SET) != 0) continue;
            uint8_t lh[30];
            if (std::fread(lh, 1, 30, f) != 30) continue;
            if (!(lh[0] == 'P' && lh[1] == 'K' && lh[2] == 3 && lh[3] == 4)) continue;
            const uint16_t l_nlen = zip_read16(lh, 26);
            const uint16_t l_elen = zip_read16(lh, 28);
            meta.payloadOffset = static_cast<uint64_t>(local_off) + 30 + l_nlen + l_elen;
            ::fseeko(f, saved, SEEK_SET);
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
// the one-time build per archive takes the write lock. Indices are shared_ptr:
// a raw interior pointer would dangle when another archive's try_emplace rehashes
// the map while a first thread still reads its index.
std::shared_ptr<const ZipArchiveIndex> get_or_build_zip_index(const std::string& archivePath) {
    struct Cache {
        std::shared_mutex mtx;
        std::unordered_map<std::string, std::shared_ptr<const ZipArchiveIndex>> byArchive;
    };
    static Cache cache;
    {
        std::shared_lock<std::shared_mutex> lock(cache.mtx);
        const auto it = cache.byArchive.find(archivePath);
        if (it != cache.byArchive.end()) return it->second;
    }
    std::unique_lock<std::shared_mutex> lock(cache.mtx);
    auto [it, inserted] = cache.byArchive.try_emplace(archivePath);
    if (inserted) {
        auto built = std::make_shared<ZipArchiveIndex>();
        std::FILE* f = std::fopen(archivePath.c_str(), "rb");
        if (f != nullptr) {
            *built = build_zip_index(f);
            std::fclose(f);
        }
        it->second = std::move(built);
    }
    return it->second;
}

bool zip_stat_entry(const std::string& archivePath, const std::string& entry,
                    uint64_t* outOffset, uint64_t* outSize, uint16_t* outMethod) {
    const auto index = get_or_build_zip_index(archivePath);
    if (!index) return false;
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
    const auto index = get_or_build_zip_index(archivePath);
    if (!index) return result;

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
    // Serialise per entry, not globally: the same URL can be resolved from
    // several threads during startup (double-extracting it is wasted IO), but
    // a single mutex around a 500MB inflate stalls every other asset behind
    // it. Striped by entry hash, with double-checked existence inside.
    static std::mutex s_jarStripes[16];
    std::mutex& stripe =
        s_jarStripes[fnv1a64_str(archivePath + "\x01" + entryName) % 16];
    std::lock_guard<std::mutex> lock(stripe);

    const std::string dir = androidRoot + "/data/cache/jar_entries";
    std::filesystem::create_directories(dir, ec);
    if (ec) return {};

    // One cache file per (archive identity, entry) pair. Identity folds in the
    // archive's size+mtime — not just its path — so an updated APK with a
    // same-size entry cannot serve stale bytes forever (the old file simply
    // stops being addressed; it is not deleted, which keeps the extractor
    // lock-free on the cleanup path).
    struct stat archiveSt;
    uint64_t archiveIdHi = 0, archiveIdLo = 0;
    if (::stat(archivePath.c_str(), &archiveSt) == 0) {
        archiveIdHi = static_cast<uint64_t>(archiveSt.st_size);
#if defined(__APPLE__)
        archiveIdLo = static_cast<uint64_t>(archiveSt.st_mtimespec.tv_sec);
#else
        archiveIdLo = static_cast<uint64_t>(archiveSt.st_mtime);
#endif
    }
    std::string base = entryName;
    const size_t slash = base.find_last_of('/');
    if (slash != std::string::npos) base = base.substr(slash + 1);
    if (base.size() > 64) base.resize(64);
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx",
                  static_cast<unsigned long long>(fnv1a64_str(
                      archivePath + "\x01" + std::to_string(archiveIdHi) + "\x01" +
                      std::to_string(archiveIdLo) + "\x01" + entryName)));
    const std::string dest = dir + "/" + hex + "_" + base;

    if (std::filesystem::exists(dest, ec) && std::filesystem::is_regular_file(dest, ec)) {
        // Trust but verify: a torn cache file (killed mid-extract by an older
        // build, before atomic commit) would otherwise be served forever. Size
        // is the cheap check against the central directory; a mismatch deletes
        // and re-extracts below. Content CRC was verified at commit time, and
        // only the atomic rename makes a file servable, so no full reread here.
        uint64_t payloadOff = 0, payloadSize = 0;
        uint16_t method = 0;
        std::error_code sizeEc;
        const uint64_t cachedSize = std::filesystem::file_size(dest, sizeEc);
        if (!sizeEc && zip_stat_entry(archivePath, entryName, &payloadOff, &payloadSize, &method) &&
            cachedSize == payloadSize) {
            return dest;
        }
        std::filesystem::remove(dest, ec);
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
            ktraceLine("[KuDroidVFS] open miss: ~%s%s\n",
                         mapped.c_str() +
                             VFSPathRemapper::getInstance().androidRoot().size(),
                         trace_caller_text(caller));
        }
    }
    return result;
}

FILE* vfs_fopen64(const char* path, const char* mode) { return vfs_fopen(path, mode); }

// Tracked APK streams: guest fread bypasses the syscall layer, so volume on
// base.apk (catalog/asset bytes) is otherwise invisible. Lock-free lookup,
// locked mutation (open/close are rare, reads are hot). The table is small and
// recycled on fclose; a session that keeps more handles open than slots loses
// the FSB5 probe on the surplus, so it is sized for the engine's parallel
// reader count rather than for a single stream.
namespace {
constexpr int kTrackedStreams = 32;
std::atomic<uintptr_t> g_apkStreams[kTrackedStreams];
std::atomic<uint64_t> g_apkStreamOps[kTrackedStreams] = {};
std::atomic<int> g_apkStreamShortLogged[kTrackedStreams] = {};
// Op-by-op tracing is limited to the first few stream instances. A session recycles
// these slots thousands of times (the engine opens the apk once per asset read), and
// sixteen lines each made this trace the largest section of the log while saying the
// same thing every time. Short reads and slow ops keep their own budgets below.
constexpr int kOpTracedStreams = 16;
std::atomic<int> g_apkStreamTraceNo[kTrackedStreams] = {};
std::atomic<int> g_apkStreamInstances{0};
std::mutex g_apkStreamsMtx;

// Audio-side op tracing. FMOD reads banks from its own thread, and by then the
// stream table has recycled far past kOpTracedStreams, so the per-instance gate
// above shows nothing of the audio path. Audio IO is named per thread instead:
// every op from an FMOD thread is traced until both budgets run out (per stream,
// then per process), which bounds the log while still covering one whole
// createSound from open to the failing read.
constexpr int kAudioOpCapPerStream = 400;
constexpr int kAudioOpBudget = 3000;
std::atomic<int> g_audioStreamOps[kTrackedStreams] = {};
std::atomic<int> g_audioOpsTraced{0};

}  // namespace

// Thread names come from the host pthread (bionic_prctl forwards the guest's name
// request to it). Cached per thread because this runs on the read path; a thread
// may be named after its first op, so the cache expires every 256 calls rather
// than being taken once.
static bool audio_thread() {
    static thread_local unsigned calls = 0;
    static thread_local bool audio = false;
    if ((calls++ & 0xFFu) == 0) {
        char name[64] = {};
#if defined(__APPLE__)
        pthread_getname_np(pthread_self(), name, sizeof(name));
#else
        (void)pthread_getname_np(pthread_self(), name, sizeof(name));
#endif
        audio = name[0] != '\0' && std::strstr(name, "FMOD") != nullptr;
    }
    return audio;
}

// Same answer for the fd layer (SyscallShim), which cannot see this TU's statics.
extern "C" bool kudroid_audio_thread(void) { return audio_thread(); }

// True when this op should be traced, consuming one slot of both budgets.
// sid < 0 means "no stream yet" (open): only the process budget applies.
static bool audio_trace_take(int sid) {
    if (sid >= kTrackedStreams) return false;
    if (!audio_thread()) return false;
    if (sid >= 0 &&
        g_audioStreamOps[sid].load(std::memory_order_relaxed) >= kAudioOpCapPerStream) {
        return false;
    }
    if (g_audioOpsTraced.load(std::memory_order_relaxed) >= kAudioOpBudget) return false;
    if (sid >= 0) g_audioStreamOps[sid].fetch_add(1, std::memory_order_relaxed);
    g_audioOpsTraced.fetch_add(1, std::memory_order_relaxed);
    return true;
}

static void track_apk_stream(FILE* f) {
    if (f == nullptr) return;
    std::lock_guard<std::mutex> lock(g_apkStreamsMtx);
    for (int i = 0; i < kTrackedStreams; ++i) {
        uintptr_t empty = 0;
        if (g_apkStreams[i].compare_exchange_strong(empty,
                                                    reinterpret_cast<uintptr_t>(f))) {
            g_apkStreamOps[i].store(0, std::memory_order_relaxed);
            g_apkStreamShortLogged[i].store(0, std::memory_order_relaxed);
            g_audioStreamOps[i].store(0, std::memory_order_relaxed);
            const int instance = g_apkStreamInstances.fetch_add(1, std::memory_order_relaxed);
            g_apkStreamTraceNo[i].store(instance, std::memory_order_relaxed);
            // Lifecycle line: one apk stream is one consumer session (a clip
            // load, a bundle stream). open/close paired with the per-op trace
            // below names which session did what, and a full table explains
            // suddenly-missing IO visibility.
            if (instance < kOpTracedStreams) {
                ktraceLine("[KuDroidApkS] sid=%d open instance=%d\n", i, instance);
            }
            return;
        }
    }
    ktraceLine("[KuDroidApkS] stream table full — apk stream untracked\n");
}

static bool is_apk_stream(FILE* f) {
    const auto p = reinterpret_cast<uintptr_t>(f);
    for (int i = 0; i < kTrackedStreams; ++i) {
        if (g_apkStreams[i].load(std::memory_order_relaxed) == p) return true;
    }
    return false;
}

static int apk_stream_id(FILE* f) {
    const auto p = reinterpret_cast<uintptr_t>(f);
    for (int i = 0; i < kTrackedStreams; ++i) {
        if (g_apkStreams[i].load(std::memory_order_relaxed) == p) return i;
    }
    return -1;
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
// Post-FSB5 follow-up countdowns keyed per FILE* (probe in vfs_fread): the
// FMOD bank stream's next reads must be logged on that stream's own budget,
// not a global one — interleaved reads on unrelated handles (zip-walk
// 30-byte local-header probes on a second base.apk handle) ate the global
// counter and the probe showed the wrong stream.
std::map<FILE*, int> g_fsbFollowup;

// Audio-blob coverage, keyed by (archive, [blobStart, blobEnd)).
//
// Unity hands FMOD one FSB5 slice per clip and reads it out of the .resource
// entry in 2048-byte chunks, re-opening base.apk for almost every chunk. A
// per-FILE* chain therefore breaks on nearly every read and reported every
// blob as TRUNCATED -- a handle-churn artifact, not a fact about the bytes.
// The window is the stable thing, so coverage is counted per window across all
// handles and the verdict names the blob FMOD was actually handed.
struct FsbWindow {
    long start = 0;
    long end = 0;
    uint64_t expected = 0;
    uint64_t blocks = 0;            // distinct 2048-byte blocks seen
    std::vector<uint64_t> covered;  // bitmap, one bit per block
};
std::map<std::string, std::vector<FsbWindow>> g_fsbWindows;  // g_freadVolMtx

// Mark [lo, hi) of a window's byte range as covered. Call with g_freadVolMtx
// held. Shared by the read path and by the serve path, which seeds the window
// with the read that revealed the header: a window created after its own first
// block was accounted can never reach its declared size, so a blob read end to
// end still verdicts SHORT — the exact mis-read that made every audio run look
// like "the blob was never read".
void fsb_window_cover(FsbWindow& w, long lo, long hi) {
    if (hi <= lo) return;
    if (lo < w.start) lo = w.start;
    if (hi > w.end) hi = w.end;
    if (hi <= lo) return;
    const uint64_t blocks = (w.expected + 2047) / 2048;
    if (blocks == 0) return;
    if (w.covered.empty()) {
        w.covered.assign(static_cast<size_t>(blocks / 64 + 1), 0);
    }
    for (long b = (lo - w.start) / 2048; b <= (hi - 1 - w.start) / 2048; ++b) {
        const uint64_t ub = static_cast<uint64_t>(b);
        if (ub >= blocks) break;
        uint64_t& word = w.covered[static_cast<size_t>(ub) >> 6];
        const uint64_t bit = 1ull << (static_cast<unsigned>(ub) & 63);
        if (!(word & bit)) {
            word |= bit;
            ++w.blocks;
        }
    }
}

// Call with g_freadVolMtx held. Reports and drops every window whose declared
// bytes are all accounted for; `enforceCap` then reports the oldest unfinished
// window SHORT until the live set is under the cap, so a blob whose bytes never
// arrived is not silently forgotten.
void fsb_windows_report_locked(const std::string& path, bool enforceCap) {
    const auto it = g_fsbWindows.find(path);
    if (it == g_fsbWindows.end()) return;
    auto& list = it->second;
    for (size_t i = 0; i < list.size();) {
        const FsbWindow& w = list[i];
        if (w.blocks * 2048 >= w.expected) {
            ktraceLine(
                         "[KuDroidFmod] blob off=%ld size=%llu covered=%llu COMPLETE\n",
                         w.start, static_cast<unsigned long long>(w.expected),
                         static_cast<unsigned long long>(w.expected));
            list.erase(list.begin() + static_cast<long>(i));
        } else {
            ++i;
        }
    }
    while (enforceCap && list.size() >= 16) {
        const FsbWindow& w = list.front();
        const uint64_t covered = w.blocks * 2048;
        ktraceLine("[KuDroidFmod] blob off=%ld size=%llu covered=%llu SHORT\n",
                     w.start, static_cast<unsigned long long>(w.expected),
                     static_cast<unsigned long long>(
                         covered > w.expected ? w.expected : covered));
        list.erase(list.begin());
    }
    if (list.empty()) g_fsbWindows.erase(it);
}

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
    ktraceLine("[KuDroidIO] %s\n", line.c_str());
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
    const void* const caller = __builtin_return_address(0);
    const bool apk = is_apk_stream(stream);
    const std::chrono::steady_clock::time_point t0 =
        apk ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const size_t n = std::fread(buf, size, count, stream);
    if (apk) {
        const long long tookMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0)
                .count();
        const int sid = apk_stream_id(stream);
        uint64_t op = 0;
        if (sid >= 0) op = g_apkStreamOps[sid].fetch_add(1, std::memory_order_relaxed) + 1;
        // First ops of every stream: a clip load / bundle stream shows its
        // whole IO shape here (open -> seek -> header read -> data reads).
        // Per-volume counters and 16MB sampling cannot see this pattern.
        if (sid >= 0 && op <= 16 &&
            g_apkStreamTraceNo[sid].load(std::memory_order_relaxed) < kOpTracedStreams) {
            ktraceLine(
                         "[KuDroidApkS] sid=%d op=%llu fread want=%zu got=%zu pos=%ld\n",
                         sid, static_cast<unsigned long long>(op), size * count, n * size,
                         std::ftell(stream));
        } else if (audio_trace_take(sid)) {
            // Audio thread: its own sample of the same shape, past the instance cap.
            ktraceLine(
                         "[KuDroidApkS] sid=%d AUDIO op=%llu fread want=%zu got=%zu pos=%ld\n",
                         sid, static_cast<unsigned long long>(op), size * count, n * size,
                         std::ftell(stream));
        }
        // Short read: fewer elements than asked. On the archive stream that is
        // a truncated slice — the exact geometry that turns a valid FSB5 into
        // FMOD_ERR_FILE_BAD. Rare and decisive, so logged (capped per stream).
        if (n < count && sid >= 0 &&
            g_apkStreamShortLogged[sid].fetch_add(1, std::memory_order_relaxed) < 6) {
            ktraceLine(
                         "[KuDroidApkS] sid=%d SHORT want=%zu got=%zu pos=%ld eof=%d errno=%d\n",
                         sid, size * count, n * size, std::ftell(stream),
                         std::feof(stream) ? 1 : 0, errno);
        }
        // Slow read: the 90 s scene-load stall surfaced only as a gap between
        // volume milestones; naming the op that ate the wall-clock removes the
        // guesswork (locked VFS work, host FS pressure, guest-side spin).
        static std::atomic<int> s_slowLogged{0};
        if (tookMs >= 50 && s_slowLogged.load(std::memory_order_relaxed) < 24) {
            s_slowLogged.fetch_add(1, std::memory_order_relaxed);
            ktraceLine(
                         "[KuDroidApkS] sid=%d SLOW %lldms want=%zu got=%zu pos=%ld\n",
                         sid, tookMs, size * count, n * size, std::ftell(stream));
        }
    }
    if (n > 0 && apk) {
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
            ktraceLine("[KuDroidApkF] fread bytes=%zu\n", n * size);
        } else if (total / (1024 * 1024) != (total - n * size) / (1024 * 1024)) {
            ktraceLine("[KuDroidApkF] fread total=%lluMB t=%llums\n",
                         total / (1024 * 1024), ms);
        }
        // Magic sniff: FMOD rejects every bank it is handed ("Error loading
        // file") while meshes from the same APK render. The bytes are read
        // from the buffer just filled — zero extra I/O — and ftell is
        // userspace. Starts past 4MB of flow so startup probing (EOCD,
        // central directory, catalog) does not consume the budget.
        //
        // Sampled every 16MB of flow, not "first 30": a fixed early budget was
        // all eaten by bundle streaming before the audio-clip loads began, so
        // the one window that mattered (Cannot load audio data at 17:31:27)
        // had no samples left. A per-flow rate keeps samples landing across
        // the whole session — audio included — at the same small log volume.
        // The offset is mapped back to its ZIP entry via the in-memory index,
        // so the line names the file the guest is actually reading, not just
        // four bytes at a mystery offset.
        static std::atomic<unsigned long long> s_nextSniff{4ULL * 1024 * 1024};
        static std::atomic<int> s_magicCount{0};
        if (total >= s_nextSniff.load(std::memory_order_relaxed) && s_magicCount.load() < 64 &&
            buf != nullptr && n * size >= 4) {
            s_magicCount.fetch_add(1, std::memory_order_relaxed);
            s_nextSniff.fetch_add(16ULL * 1024 * 1024, std::memory_order_relaxed);
            const long off = std::ftell(stream);
            const long start = off >= 0 ? off - static_cast<long>(n * size) : -1;
            const auto* b = static_cast<const unsigned char*>(buf);
            // Which entry does this offset belong to? The stream path is the
            // APK the tracker recorded; a miss (unindexed archive, offset past
            // the entries) falls back to the raw-bytes line alone.
            std::string entryName;
            {
                std::lock_guard<std::mutex> vlock(g_freadVolMtx);
                const auto pit = g_freadPaths.find(stream);
                if (pit != g_freadPaths.end()) {
                    const auto index = get_or_build_zip_index(pit->second);
                    if (index && start >= 0) {
                        std::string name;
                        ZipEntryMeta meta;
                        if (zip_entry_containing(index, start, name, meta)) {
                            entryName = name;
                        }
                    }
                }
            }
            ktraceLine(
                         "[KuDroidApkF] magic off=%ld n=%zu %02x%02x%02x%02x '%c%c%c%c'%s%s\n",
                         start, n * size, b[0], b[1], b[2], b[3],
                         b[0] >= 32 && b[0] < 127 ? b[0] : '.',
                         b[1] >= 32 && b[1] < 127 ? b[1] : '.',
                         b[2] >= 32 && b[2] < 127 ? b[2] : '.',
                         b[3] >= 32 && b[3] < 127 ? b[3] : '.',
                         entryName.empty() ? "" : " entry=", entryName.c_str());
        }
        // Account this read's bytes into any audio-blob window on the same
        // archive, whichever handle carried them.
        {
            std::lock_guard<std::mutex> vlock(g_freadVolMtx);
            const long pos = std::ftell(stream);
            const long start = pos >= 0 ? pos - static_cast<long>(n * size) : -1;
            const auto pit = g_freadPaths.find(stream);
            if (pit != g_freadPaths.end() && start >= 0) {
                auto wit = g_fsbWindows.find(pit->second);
                if (wit != g_fsbWindows.end()) {
                    for (auto& w : wit->second) {
                        if (start >= w.end || pos <= w.start) continue;
                        // Every read inside a blob is one line, capped: the FMOD
                        // parse sequence lives or dies between these reads, and
                        // an absent data-read is the fact that convicts the
                        // parser (it stopped asking) over the stream (it
                        // served wrong bytes). Without this, silence between
                        // the header read and the error is unattributable.
                        static std::atomic<int> s_inblobLogged{0};
                        if (s_inblobLogged.load(std::memory_order_relaxed) < 60) {
                            s_inblobLogged.fetch_add(1, std::memory_order_relaxed);
                            ktraceLine(
                                         "[KuDroidFmod] in-blob read sid=%d off=%ld size=%zu pos=%ld "
                                         "blob=[%ld+%llu]%s\n",
                                         apk_stream_id(stream), start, n * size, pos, w.start,
                                         static_cast<unsigned long long>(w.expected),
                                         trace_caller_text(caller));
                        }
                        fsb_window_cover(w, start, pos);
                    }
                    fsb_windows_report_locked(pit->second, false);
                }
            }
        }
        // FSB5 probe: Unity hands an AudioClip's FSB slice to FMOD from the
        // resource it just read. If a read begins at an FSB5 header, record
        // the exact geometry so we can tell "FMOD was given good bytes and
        // still failed" from "Unity never streamed the FSB at all". The
        // served line names the containing ZIP entry plus its method and
        // size: a deflated slice served raw, or a slice whose entry span is
        // wrong, is instantly visible. Rate-limited: an FSB header appears
        // once per clip load.
        static std::atomic<int> s_fsbCount{0};
        if (buf != nullptr && n * size >= 4 && s_fsbCount.load() < 24) {
            const auto* f = static_cast<const unsigned char*>(buf);
            if (f[0] == 'F' && f[1] == 'S' && f[2] == 'B' && f[3] == '5') {
                s_fsbCount.fetch_add(1, std::memory_order_relaxed);
                const long off = std::ftell(stream);
                const long start = off >= 0 ? off - static_cast<long>(n * size) : -1;
                std::string entry;
                uint16_t method = 0xFFFF;
                uint32_t usize = 0;
                const int sid = apk_stream_id(stream);
                // Distance from the blob to its ZIP entry's bounds. A blob that
                // runs past the entry end is a slice FMOD can never read whole,
                // which is the one geometry that turns a valid FSB5 into
                // "Error loading file" without the bytes being wrong.
                long long entryOff = -1;
                if (start >= 0) {
                    std::string archivePath;
                    {
                        std::lock_guard<std::mutex> vlock(g_freadVolMtx);
                        const auto pit = g_freadPaths.find(stream);
                        if (pit != g_freadPaths.end()) archivePath = pit->second;
                    }
                    if (!archivePath.empty()) {
                        const auto index = get_or_build_zip_index(archivePath);
                        std::string name;
                        ZipEntryMeta meta;
                        if (zip_entry_containing(index, start, name, meta)) {
                            entry = name;
                            method = meta.compressionMethod;
                            usize = meta.uncompressedSize;
                            entryOff = static_cast<long long>(start) -
                                       static_cast<long long>(meta.payloadOffset);
                        }
                    }
                }
                // FSB5 header: magic(4), version u32@4, numSamples u32@8,
                // sampleHeadersSize u32@12, nameTableSize u32@16, dataSize
                // u32@20, mode u32@24. Header total is 60 bytes (zero u64@
                // 28, hash 16B@36, dummy u64@52); blob size = 60 + shs +
                // nts + dataSize. mode names the codec — a codec this FMOD
                // build lacks would reject every blob regardless of bytes.
                uint32_t ver = 0, numSamples = 0, shs = 0, nts = 0;
                uint32_t dataSize = 0, mode = 0;
                uint64_t blobTotal = 0;
                bool parsed = false;
                if (n * size >= 28) {
                    auto rd32 = [&](size_t o) -> uint32_t {
                        return static_cast<uint32_t>(f[o]) |
                               (static_cast<uint32_t>(f[o + 1]) << 8) |
                               (static_cast<uint32_t>(f[o + 2]) << 16) |
                               (static_cast<uint32_t>(f[o + 3]) << 24);
                    };
                    ver = rd32(4);
                    numSamples = rd32(8);
                    shs = rd32(12);
                    nts = rd32(16);
                    dataSize = rd32(20);
                    mode = rd32(24);
                    blobTotal = 60ull + shs + nts + dataSize;
                    parsed = true;
                }
                // Chain check from the same buffer: .resource files pack
                // blobs back to back, so a valid second header this close
                // proves the resource data itself is well-formed.
                char nextDesc[96] = "n/a";
                if (blobTotal > 0 && blobTotal + 28 <= n * size) {
                    const auto* g = f + blobTotal;
                    const bool magic2 = g[0] == 'F' && g[1] == 'S' && g[2] == 'B' && g[3] == '5';
                    auto rd32at = [&](const unsigned char* p, size_t o) -> uint32_t {
                        return static_cast<uint32_t>(p[o]) |
                               (static_cast<uint32_t>(p[o + 1]) << 8) |
                               (static_cast<uint32_t>(p[o + 2]) << 16) |
                               (static_cast<uint32_t>(p[o + 3]) << 24);
                    };
                    if (magic2) {
                        std::snprintf(nextDesc, sizeof(nextDesc),
                                      "FSB5@%llu num=%u dataSize=%u mode=%u",
                                      static_cast<unsigned long long>(blobTotal),
                                      rd32at(g, 8), rd32at(g, 20), rd32at(g, 24));
                    } else {
                        std::snprintf(nextDesc, sizeof(nextDesc),
                                      "none@%llu (%02x%02x%02x%02x)",
                                      static_cast<unsigned long long>(blobTotal),
                                      g[0], g[1], g[2], g[3]);
                    }
                }
                char hex[324] = {0};
                {
                    // 128 bytes: FSB5 header (60) + sample header area, where the
                    // Vorbis setup chunk lives. The old 32-byte dump never reached it.
                    const size_t hb = n * size < 128 ? n * size : 128;
                    size_t o = 0;
                    for (size_t i = 0; i < hb; ++i) {
                        o += static_cast<size_t>(
                            std::snprintf(hex + o, sizeof(hex) - o, "%02x", f[i]));
                    }
                }                // Sample header (first numSamples u64s after the 60-byte FSB5
                // v1 header): bit 0 = next-chunk flag, bits 1-4 = frequency,
                // bit 5 = channels-1, bits 6-33 = dataOffset/16, bits 34-63 =
                // sample count (python-fsb5 __init__.py). The chunk list
                // follows: u32 [next:1][size:24][type:7], VORBISDATA (11)
                // carries crc32 + unknown. Verifying crc membership and
                // chunk presence turns "valid header, FMOD fails" into a
                // parse-level fact.
                char sampleDesc[160] = "n/a";
                // 76 bytes reach: 60-byte header + u64 sample field + chunk
                // word + VORBISDATA crc. The old guard demanded blobTotal+28
                // served bytes — a streaming reader never satisfies that, so
                // every run logged sample=[n/a] no matter what it read.
                if (parsed && numSamples >= 1 && n * size >= 76) {
                    const auto* sh = f + 60;
                    auto rd64 = [&](size_t o) {
                        uint64_t v = 0;
                        for (int k = 7; k >= 0; --k) v = (v << 8) | sh[o + k];
                        return v;
                    };
                    const uint64_t raw0 = rd64(0);
                    const uint32_t freqIdx = static_cast<uint32_t>((raw0 >> 1) & 0xF);
                    static const uint32_t kFreqTable[10] = {0,  8000,  11000, 11025, 16000,
                                                            22050, 24000, 32000, 44100, 48000};
                    const uint32_t freq = freqIdx < 10 ? kFreqTable[freqIdx] : 0;
                    const uint32_t channels = static_cast<uint32_t>(((raw0 >> 5) & 0x1)) + 1;
                    const uint32_t dataOff = static_cast<uint32_t>((raw0 >> 6) & 0xFFFFFFF) * 16;
                    const uint32_t nSamples = static_cast<uint32_t>((raw0 >> 34) & 0x3FFFFFFF);
                    uint32_t chunkType = 0, chunkSize = 0, crc32 = 0;
                    if (shs >= 12) {
                        const uint32_t cw = rd64(8) & 0xFFFFFFFF;
                        chunkSize = (cw >> 1) & 0x7FFFFF;
                        chunkType = (cw >> 25) & 0x7F;
                    }
                    if (chunkType == 11 && shs >= 16) {
                        crc32 = static_cast<uint32_t>(rd64(12) & 0xFFFFFFFF);
                    }
                    std::snprintf(sampleDesc, sizeof(sampleDesc),
                                  "freq=%u ch=%u dataOff=%u(%sdataSize) n=%u chunk1=%u/%u%s crc=%08X",
                                  freq, channels, dataOff,
                                  dataOff <= dataSize ? "<=" : ">>",
                                  nSamples, chunkType, chunkSize,
                                  chunkType == 11 ? " VORBISDATA" : "", crc32);
                    (void)0;
                }
                ktraceLine(
                             "[KuDroidFmod] served FSB5 sid=%d at apk_off=%ld size=%zu "
                             "method=%u usize=%u entryOff=%lld entryRem=%lld "
                             "ver=%u num=%u shs=%u nts=%u "
                             "dataSize=%u mode=%u blobTotal=%llu next=[%s] hex=%s "
                             "sample=[%s] entry=%s%s\n",
                             sid, start, n * size, method, usize, entryOff,
                             entryOff >= 0
                                 ? static_cast<long long>(usize) - entryOff -
                                       static_cast<long long>(blobTotal)
                                 : -1,
                             ver, numSamples, shs, nts,
                             dataSize, mode, static_cast<unsigned long long>(blobTotal),
                             nextDesc, hex, sampleDesc, entry.empty() ? "(none)" : entry.c_str(),
                             trace_caller_text(caller));
                // Decisive follow-up: when FMOD accepts the bank it streams
                // through THIS handle; when it rejects the slice it reads
                // little or nothing on it. Keyed by handle so unrelated
                // concurrent streams cannot mask the FSB stream's reads.
                {
                    std::lock_guard<std::mutex> vlock(g_freadVolMtx);
                    g_fsbFollowup[stream] = 8;
                    if (parsed && start >= 0 && blobTotal > 0 &&
                        blobTotal <= (32u << 20)) {
                        const auto pit2 = g_freadPaths.find(stream);
                        if (pit2 != g_freadPaths.end()) {
                            auto& list = g_fsbWindows[pit2->second];
                            bool already = false;
                            for (auto& existing : list) {
                                if (existing.start == start) {
                                    fsb_window_cover(existing, start, start + static_cast<long>(n * size));
                                    already = true;
                                    break;
                                }
                            }
                            if (!already) {
                                fsb_windows_report_locked(pit2->second, true);
                                FsbWindow w;
                                w.start = start;
                                w.end = start + static_cast<long>(blobTotal);
                                w.expected = blobTotal;
                                fsb_window_cover(w, start, start + static_cast<long>(n * size));
                                list.push_back(w);
                            }
                        }
                    }
                }
            }
        }
        bool followLine = false;
        {
            std::lock_guard<std::mutex> vlock(g_freadVolMtx);
            const auto fit = g_fsbFollowup.find(stream);
            if (fit != g_fsbFollowup.end()) {
                if (fit->second > 0) {
                    --fit->second;
                    followLine = true;
                } else {
                    g_fsbFollowup.erase(fit);
                }
            }
        }
        if (followLine) {
            // req= names what the reader ASKED for, not just what arrived: a blob
            // reported SHORT is either a reader that stops asking (req is one small
            // chunk) or a stream that ends early (req is the whole blob, n is short).
            ktraceLine(
                         "[KuDroidFmod] post-FSB5 read req=%zu x %zu -> %zu bytes pos=%ld%s\n",
                         size, count, n, std::ftell(stream), trace_caller_text(caller));
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
            ktraceLine("[KuDroidIO] big fread bytes=%zu\n", n * size);
        }
    }
    // Audio thread on a stream that is not the archive: FMOD also reads banks it
    // opened itself (extracted cache, loose .fsb/.bank). Traced so a failing
    // createSound keeps a complete picture whichever file it actually read from.
    if (!apk && audio_trace_take(-1)) {
        ktraceLine("[KuDroidApkS] sid=-1 AUDIO fread want=%zu got=%zu pos=%ld\n", size * count,
                     n * size, std::ftell(stream));
    }
    return n;
}

int vfs_fclose(FILE* stream) {
    // fclose(NULL) is undefined behaviour — both host libcs trap on it — so the guard
    // has to cover the call as well as the tracking below it. Returning EOF keeps the
    // guard's promise instead of defeating it: the old code tested for null and then
    // called fclose(NULL) anyway.
    if (stream == nullptr) {
        errno = EINVAL;
        return EOF;
    }
    // Flush this thread's batch for the stream before the map entry goes
    // away, then bump the epoch so other threads' stale FILE* caches
    // re-resolve instead of attributing to a recycled address.
    if (t_volbatch.stream == stream) fread_vol_flush();
    g_freadEpoch.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_apkStreamsMtx);
        for (int i = 0; i < kTrackedStreams; ++i) {
            if (g_apkStreams[i].load(std::memory_order_relaxed) ==
                reinterpret_cast<uintptr_t>(stream)) {
                const uint64_t ops =
                    g_apkStreamOps[i].load(std::memory_order_relaxed);
                if (g_apkStreamTraceNo[i].load(std::memory_order_relaxed) < kOpTracedStreams) {
                    ktraceLine("[KuDroidApkS] sid=%d close after %llu ops\n", i,
                               static_cast<unsigned long long>(ops));
                } else if (audio_thread()) {
                    ktraceLine("[KuDroidApkS] sid=%d AUDIO close after %llu ops\n", i,
                               static_cast<unsigned long long>(ops));
                }
                g_apkStreams[i].store(0, std::memory_order_relaxed);
                break;
            }
        }
    }
    {
        std::lock_guard<std::mutex> vlock(g_freadVolMtx);
        g_freadPaths.erase(stream);
    }
    return std::fclose(stream);
}

int vfs_fseek(FILE* stream, long offset, int whence) {
    const void* const caller = __builtin_return_address(0);
    const long before = std::ftell(stream);
    const int rc = std::fseek(stream, offset, whence);
    if (rc == 0 && is_apk_stream(stream)) {
        // Per-stream op trace (shared counter with fread): the first ops of a
        // stream show the seek pattern that headers and clip data produce.
        const int sid = apk_stream_id(stream);
        if (sid >= 0) {
            const uint64_t op = g_apkStreamOps[sid].fetch_add(1, std::memory_order_relaxed) + 1;
            if (op <= 16 &&
                g_apkStreamTraceNo[sid].load(std::memory_order_relaxed) < kOpTracedStreams) {
                ktraceLine(
                             "[KuDroidApkS] sid=%d op=%llu seek %ld whence=%d -> %ld\n",
                             sid, static_cast<unsigned long long>(op), offset, whence,
                             std::ftell(stream));
            } else if (audio_trace_take(sid)) {
                ktraceLine(
                             "[KuDroidApkS] sid=%d AUDIO op=%llu seek %ld whence=%d -> %ld\n",
                             sid, static_cast<unsigned long long>(op), offset, whence,
                             std::ftell(stream));
            }
        }
        // Seeks are how FMOD walks its FSB (header read, then jump to the Vorbis
        // setup/data). A seek that lands outside the blob it was reading is the
        // exact moment the parse goes wrong, so position, target, and result are
        // all part of one line. Capped per process: header hunting alone can
        // seek thousands of times before the first FSB appears.
        static std::atomic<int> s_logged{0};
        if (s_logged.load() < 40) {
            ++s_logged;
            ktraceLine("[KuDroidApkF] fseek offset=%ld whence=%d\n", offset,
                         whence);
        }
        {
            std::lock_guard<std::mutex> vlock(g_freadVolMtx);
            const auto pit = g_freadPaths.find(stream);
            if (pit != g_freadPaths.end()) {
                auto wit = g_fsbWindows.find(pit->second);
                if (wit != g_fsbWindows.end()) {
                    for (const auto& w : wit->second) {
                        if ((before >= w.start && before < w.end) ||
                            (offset >= w.start && offset < w.end) ||
                            (before >= w.start && before < w.end + 65536)) {
                            static std::atomic<int> s_inblobSeek{0};
                            if (s_inblobSeek.load(std::memory_order_relaxed) < 60) {
                                s_inblobSeek.fetch_add(1, std::memory_order_relaxed);
                                ktraceLine(
                                             "[KuDroidFmod] in-blob seek sid=%d %ld -> %ld whence=%d "
                                             "blob=[%ld+%llu]%s\n",
                                             sid, before, offset, whence, w.start,
                                             static_cast<unsigned long long>(w.expected),
                                             trace_caller_text(caller));
                            }
                            break;
                        }
                    }
                }
            }
        }
        // SEEK_END resolves to the whole-archive size; a consumer taking it
        // as the current entry's size streams garbage past the entry end.
        static std::atomic<int> s_endLogged{0};
        if (whence == SEEK_END && s_endLogged.load() < 16) {
            ++s_endLogged;
            ktraceLine("[KuDroidApkF] fseek END on apk stream -> %ld\n",
                         std::ftell(stream));
        }
    } else {
        // Audio-thread seeks outside the tracked-apk case: FMOD also seeks streams it
        // opened itself (extracted cache, separate banks), and a failed seek is as
        // informative as a successful one.
        const int sid = apk_stream_id(stream);
        if (audio_trace_take(sid)) {
            ktraceLine(
                         "[KuDroidApkS] sid=%d AUDIO seek %ld whence=%d from=%ld -> %ld rc=%d\n",
                         sid, offset, whence, before, std::ftell(stream), rc);
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
