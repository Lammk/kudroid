// Real CPU topology for the host device.
//
// Every surface a guest can read its core count from has to give the same answer, and
// they did not: /proc/cpuinfo listed 8 processors, /sys/devices/system/cpu/present said
// 0-7, sysconf(_SC_NPROCESSORS_ONLN) returned std::thread::hardware_concurrency(), and
// __sched_cpucount was not implemented at all — so it bound to the universal dummy and
// returned 0. Unity printed "Cores = 0, Memory = 8000mb" and sized its job system from
// it.
//
// Apps do not read these numbers for display. Unity derives worker-thread counts,
// il2cpp sizes its GC, and any engine with a job system partitions work by core class,
// so a wrong count either leaves the device idle or oversubscribes it. A wrong count of
// ZERO makes a job system create no workers at all.
//
// big.LITTLE matters separately from the total. An engine schedules latency-sensitive
// work on the performance cores and background work on the efficiency ones, and it
// discovers which is which by comparing cpufreq/cpuinfo_max_freq across cores. Only cpu0
// had a cpufreq directory, so nothing could be classified: hence "0 big (mask: 0x0),
// 0 little (mask: 0x0)".
//
// Nothing here is app-specific: the numbers describe the device.
#ifndef KUDROID_PLATFORM_CPUINFO_H
#define KUDROID_PLATFORM_CPUINFO_H

#include <cstdint>

namespace kudroid {

struct CpuTopology {
    // Logical cores the OS will schedule on. Never zero.
    uint32_t total_cores = 0;

    // Performance ("big") and efficiency ("little") core counts.
    //
    // performance_cores + efficiency_cores == total_cores always holds. On a machine
    // with no asymmetry — or one that will not report it — every core is counted as a
    // performance core, because that is what a uniform SMP machine is: there are no
    // efficiency cores to schedule background work onto, and claiming some exist would
    // make an engine park work on cores that are no different.
    uint32_t performance_cores = 0;
    uint32_t efficiency_cores = 0;

    // Peak frequency in kHz for each class, as cpufreq reports it. Used to synthesise
    // /sys/.../cpufreq/cpuinfo_max_freq, which is how a guest tells the classes apart.
    //
    // Derived from the class split rather than measured: iOS exposes no per-core
    // frequency. The ratio is what a guest acts on, not the absolute value.
    uint32_t performance_max_khz = 0;
    uint32_t efficiency_max_khz = 0;

    // False when the platform reported nothing and these are fallbacks.
    bool measured = false;
};

// Query the host. Cached after the first call: topology does not change while a process
// runs, and this is reached from paths that run per guest file open.
const CpuTopology& query_cpu_topology();

// Affinity mask with one bit set per online core, as a Linux cpu_set_t word.
//
// Everything a guest does with affinity starts here: sched_getaffinity fills a set from
// it, and __sched_cpucount counts its bits. Both used to answer from unrelated
// hardcoded values, which is how a mask could say 0xFF while the count said 0.
uint64_t cpu_online_mask();

// Test seam: drop the cache so a test can observe a fresh query.
void cpu_topology_reset_for_test();

// Test seam: answer with `topology` until reset.
//
// The asymmetric case cannot be reached any other way from a host test. A Linux CI runner
// reports a uniform machine, so the code that gives performance and efficiency cores
// different frequencies — the code a guest relies on to tell the classes apart, and the
// code whose absence produced "0 big (mask: 0x0), 0 little (mask: 0x0)" — would otherwise
// be the one path never executed anywhere before it shipped.
void cpu_topology_override_for_test(const CpuTopology& topology);

}  // namespace kudroid

#endif  // KUDROID_PLATFORM_CPUINFO_H
