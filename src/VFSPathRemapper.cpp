#include "kudroid/VFSPathRemapper.h"
#include "kudroid/cacert_data.h"
#include "kudroid/DeviceProfile.h"
#include "kudroid/platform/CpuInfo.h"
#include "kudroid/platform/MemoryInfo.h"

#include <cerrno>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <limits.h>
#include <sstream>
#include <unistd.h>
#include <vector>
#include <sys/socket.h>
#include <sys/mman.h>

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
std::string normalizePathString(std::string_view path) {
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
        "sdcard/Download", "sdcard/Documents", "sdcard/Pictures", "sdcard/DCIM",
        "sdcard/Music", "sdcard/Movies", "sdcard/Android/data", "sdcard/Android/obb", "sdcard/Android/media",
        "system", "proc/self", "sys", "mnt", "storage/emulated", "dev",
        "etc", "system/etc", "system/etc/security/cacerts", "system/etc/permissions"
    }) {
        std::filesystem::create_directories(std::filesystem::path(androidRoot_) / relative, error);
        if (error) {
            vfsLog("Failed to create " + androidRoot_ + "/" + relative + ": " + error.message());
            return false;
        }
    }
    
    // create soft links
    auto make_symlink = [&](const char* target, const char* linkpath) {
        std::error_code ec;
        std::filesystem::path fullLink = std::filesystem::path(androidRoot_) / linkpath;
        if (!std::filesystem::exists(fullLink)) {
            std::filesystem::create_directory_symlink(target, fullLink, ec);
        }
    };
    
    make_symlink("../sdcard", "mnt/sdcard");
    make_symlink("../../sdcard", "storage/emulated/0");
    make_symlink("../../sdcard", "storage/self/primary");
    std::filesystem::remove(std::filesystem::path(androidRoot_) / "etc", error);
    make_symlink("system/etc", "etc");
    make_symlink("/dev/fd", "proc/self/fd");

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
         "ro.product.model=" KUDROID_DEVICE_MODEL "\n"
         "ro.product.manufacturer=" KUDROID_DEVICE_MANUFACTURER "\n"
         "ro.product.brand=" KUDROID_DEVICE_BRAND "\n"
         "ro.product.name=" KUDROID_DEVICE_NAME "\n"
         "ro.product.device=" KUDROID_DEVICE_BOARD "\n"
         "ro.product.cpu.abi=" KUDROID_DEVICE_ABI "\n"
         "ro.product.cpu.abilist=" KUDROID_DEVICE_ABI "\n", false});
    files.push_back({"proc/cpuinfo", BuildCpuInfo(cpu), true});
    files.push_back({"proc/meminfo", BuildMemInfo(mem), true});
    files.push_back({"proc/version",
         "Linux version 5.15.0-kudroid (clang 17.0.0) #1 SMP PREEMPT 2026\n", false});
    files.push_back({"proc/self/cmdline", std::string("com.kudroid.app\0", 16), false});
    files.push_back({"proc/self/stat",
         "1 (com.kudroid.app) S 0 1 1 0 -1 4194560 0 0 0 0 0 0 0 0 20 0 1 0 0 0 0 0 0 0 "
         "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n", false});
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
    owned.reserve(static_cast<size_t>(cpu.total_cores) * 4);
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

        if (current.empty()) {
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

std::string VFSPathRemapper::remap(const char* originalPath) const {
    if (!originalPath) return {};

    // Normalise before matching a prefix, not after. "/sdcard/../../x" must not be
    // treated as an sdcard path and then joined to the root with the ".." intact — the
    // kernel would resolve it outside android_root. After this, the path contains no
    // "." or ".." at all, so the join below cannot escape.
    const std::string normalized = normalizePathString(std::string_view(originalPath));
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
         std::strstr(path, ".webm") != nullptr);
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
    // for o_creat, make sure the root directory exists (avoid enoent problem).
    if (flags & O_CREAT) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(mapped).parent_path(), ec);
    }
    const int result = (flags & O_CREAT) ? ::open(mapped.c_str(), flags, mode)
                                         : ::open(mapped.c_str(), flags);
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
         std::strstr(path, ".webm") != nullptr)) {
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
    if (result == nullptr && !mapped.empty() &&
        mapped.rfind(VFSPathRemapper::getInstance().androidRoot(), 0) == 0) {
        static std::atomic<int> s_miss{0};
        if (s_miss.load() < 30) {
            ++s_miss;
            std::fprintf(stderr, "[KuDroidVFS] open miss: %s\n", mapped.c_str());
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
// shows here. Open/close are rare, reads are hot: path recorded under lock at
// open, lock-free map read per fread would still serialize — a single mutex is
// fine (uncontended ~20ns vs microsecond reads).
std::mutex g_freadVolMtx;
std::map<FILE*, std::string> g_freadPaths;
std::map<std::string, std::pair<uint64_t, uint64_t>> g_freadVol;
uint64_t g_freadVolTotal = 0;
uint64_t g_freadVolNextLog = 50ULL * 1024 * 1024;

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
    }
    if (n > 0) {
        std::lock_guard<std::mutex> lock(g_freadVolMtx);
        auto it = g_freadPaths.find(stream);
        if (it != g_freadPaths.end()) {
            auto& e = g_freadVol[it->second];
            e.first += n * size;
            e.second += 1;
            g_freadVolTotal += n * size;
            if (g_freadVolTotal >= g_freadVolNextLog) {
                g_freadVolNextLog += 50ULL * 1024 * 1024;
                using Entry = std::pair<std::string, std::pair<uint64_t, uint64_t>>;
                std::vector<Entry> top(g_freadVol.begin(), g_freadVol.end());
                std::sort(top.begin(), top.end(), [](const Entry& a, const Entry& b) {
                    return a.second.first > b.second.first;
                });
                std::string line =
                    "fread total=" + std::to_string(g_freadVolTotal) + "B";
                for (size_t i = 0; i < top.size() && i < 5; ++i) {
                    const std::string& p = top[i].first;
                    line += " | " + (p.size() > 60 ? "..." + p.substr(p.size() - 57) : p) +
                            "=" + std::to_string(top[i].second.first) + "B/" +
                            std::to_string(top[i].second.second) + "ops";
                }
                std::fprintf(stderr, "[KuDroidIO] %s\n", line.c_str());
            }
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

struct dirent* vfs_readdir(DIR* directory) {
    if (!directory) return nullptr;
    struct dirent* host_entry = ::readdir(directory);
    if (!host_entry) return nullptr;

#if defined(__APPLE__)
    static thread_local bionic_dirent_layout bionic_entry;
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
int vfs_closedir(DIR* directory) { return ::closedir(directory); }
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
