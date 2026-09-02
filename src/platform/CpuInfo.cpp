#include "kudroid/platform/CpuInfo.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#else
#include <unistd.h>
#endif

namespace kudroid {
namespace {

// Frequencies reported for the two core classes.
//
// iOS exposes no per-core frequency at all, and these numbers are not a claim about the
// silicon. What a guest acts on is the RATIO: it reads cpuinfo_max_freq for every core
// and groups them, so what matters is that performance cores report a higher value than
// efficiency cores and that cores within a class agree. Values in the range real ARM
// devices report, so a guest sanity-checking the magnitude is not surprised.
constexpr uint32_t kPerformanceMaxKhz = 2800000;
constexpr uint32_t kEfficiencyMaxKhz = 1800000;

// Used only when the platform reports nothing at all.
//
// Six, not eight: it is the count on the devices KuDroid targets, and over-reporting is
// the worse error of the two. An engine that creates eight workers on a four-core device
// oversubscribes every one of them, while four workers on a six-core device leaves
// headroom. Two of the six are efficiency cores, matching the asymmetry every phone SoC
// in this class has — a guest that finds none will schedule background work onto
// performance cores.
constexpr uint32_t kFallbackTotal = 6;
constexpr uint32_t kFallbackEfficiency = 2;

#if defined(__APPLE__)

// One sysctl by name, as a 32-bit count. Returns 0 when the key does not exist, which is
// the normal case for the perflevel keys on older systems rather than an error.
uint32_t SysctlCount(const char* name) {
    // int32 rather than int64: hw.logicalcpu and the perflevel counts are ints, and
    // asking for eight bytes on a four-byte key fails with ENOMEM.
    int32_t value = 0;
    size_t len = sizeof(value);
    if (sysctlbyname(name, &value, &len, nullptr, 0) != 0) return 0;
    if (value <= 0) return 0;
    return static_cast<uint32_t>(value);
}

CpuTopology QueryApple() {
    CpuTopology out;

    // hw.perflevel*.logicalcpu is the only interface that reports the asymmetry, and it
    // exists from iOS 14 / macOS 12. perflevel0 is the FASTEST class — the numbering is
    // by performance order, not by physical layout — so on a 2+4 A13 perflevel0 is the
    // two Lightning cores and perflevel1 the four Thunder ones.
    const uint32_t levels = SysctlCount("hw.nperflevels");
    if (levels >= 2) {
        const uint32_t fast = SysctlCount("hw.perflevel0.logicalcpu");
        const uint32_t slow = SysctlCount("hw.perflevel1.logicalcpu");
        if (fast > 0 && slow > 0) {
            out.performance_cores = fast;
            out.efficiency_cores = slow;
            out.total_cores = fast + slow;
            out.measured = true;
        }
    }

    // No perflevel data: fall back to the total, which every version reports. The
    // machine is then described as uniform rather than having its asymmetry guessed at —
    // a guess would tell a guest to park background work on cores that may be identical.
    if (!out.measured) {
        uint32_t total = SysctlCount("hw.logicalcpu");
        if (total == 0) total = SysctlCount("hw.logicalcpu_max");
        if (total == 0) total = SysctlCount("hw.ncpu");
        if (total > 0) {
            out.total_cores = total;
            out.performance_cores = total;
            out.efficiency_cores = 0;
            out.measured = true;
        }
    }

    return out;
}

#else

CpuTopology QueryPosix() {
    CpuTopology out;
    long online = ::sysconf(_SC_NPROCESSORS_ONLN);
    if (online <= 0) online = ::sysconf(_SC_NPROCESSORS_CONF);
    if (online <= 0) {
        const unsigned hinted = std::thread::hardware_concurrency();
        online = hinted > 0 ? static_cast<long>(hinted) : 0;
    }
    if (online > 0) {
        out.total_cores = static_cast<uint32_t>(online);
        // A host build is treated as uniform. Reading the real big.LITTLE split on Linux
        // means parsing cpu_capacity across every core, and it would only be exercised
        // on a CI runner that has none — so it would be untested code standing in for a
        // platform this does not ship on.
        out.performance_cores = out.total_cores;
        out.efficiency_cores = 0;
        out.measured = true;
    }
    return out;
}

#endif

CpuTopology QueryOnce() {
#if defined(__APPLE__)
    CpuTopology out = QueryApple();
#else
    CpuTopology out = QueryPosix();
#endif

    if (out.total_cores == 0) {
        out.total_cores = kFallbackTotal;
        out.efficiency_cores = kFallbackEfficiency;
        out.performance_cores = kFallbackTotal - kFallbackEfficiency;
        out.measured = false;
    }

    // Clamp to what a Linux cpu_set_t word can name.
    //
    // cpu_online_mask() is a 64-bit word and every consumer — sched_getaffinity,
    // __sched_cpucount, the /sys/.../present string — derives from it. A count above 64
    // would produce a mask that does not describe the cores being advertised, and the
    // two would disagree in exactly the way this file exists to prevent.
    if (out.total_cores > 64) {
        out.total_cores = 64;
        if (out.efficiency_cores > out.total_cores) out.efficiency_cores = out.total_cores / 2;
        out.performance_cores = out.total_cores - out.efficiency_cores;
    }

    // The invariant every consumer relies on. Enforced rather than assumed: a partial
    // sysctl result could otherwise produce classes that do not add up, and the
    // /proc/cpuinfo and /sys writers would then describe different machines.
    if (out.performance_cores + out.efficiency_cores != out.total_cores) {
        out.performance_cores = out.total_cores - std::min(out.efficiency_cores, out.total_cores);
        out.efficiency_cores = out.total_cores - out.performance_cores;
    }

    out.performance_max_khz = kPerformanceMaxKhz;
    // A uniform machine reports ONE frequency for every core. Handing out two distinct
    // values would invent an asymmetry the device does not have, and a guest comparing
    // them would split its scheduling across a boundary that is not there.
    out.efficiency_max_khz = out.efficiency_cores > 0 ? kEfficiencyMaxKhz : kPerformanceMaxKhz;

    return out;
}

std::mutex g_mutex;
CpuTopology g_cached;
bool g_valid = false;

}  // namespace

const CpuTopology& query_cpu_topology() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_valid) {
        g_cached = QueryOnce();
        g_valid = true;
    }
    return g_cached;
}

uint64_t cpu_online_mask() {
    const uint32_t cores = query_cpu_topology().total_cores;
    if (cores >= 64) return ~0ull;
    return (1ull << cores) - 1ull;
}

void cpu_topology_reset_for_test() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_valid = false;
}

void cpu_topology_override_for_test(const CpuTopology& topology) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cached = topology;
    g_valid = true;
}

}  // namespace kudroid
