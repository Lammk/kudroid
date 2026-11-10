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
    owned.reserve(static_cast<size_t>(cpu.total_cores) * 8);
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
        owned.push_back(base + "/topology/package_cpus_list");
        files.push_back({owned.back().c_str(), DecimalRange(cpu.total_cores), true});
        owned.push_back(base + "/topology/core_siblings_list");
        files.push_back({owned.back().c_str(), DecimalRange(cpu.total_cores), true});
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

