// test_cpu_info.cpp — the device's core count, and every surface that reports it.
//
// Why this exists. Unity printed "SystemInfo CPU = ARM64 FP ASIMD AES, Cores = 0,
// Memory = 8000mb" on a 4 GB, 6-core iPhone 11. Both numbers were wrong and both were
// wrong for the same reason: the figures a guest can read were string literals and
// hardcoded constants, written independently of each other and of the device.
//
//   /proc/meminfo said MemTotal 8192000 kB, and 8192000/1024 is 8000 exactly — so
//   Unity's memory figure came from that literal, not from anything measured.
//   MemoryInfo.cpp was already querying the real sysctl; nothing asked it.
//
//   __sched_cpucount was not in the shim table, so the ELF loader bound it to the
//   universal dummy and it returned 0. CPU_COUNT() is a macro over that function, so a
//   guest counting its own affinity mask got zero however many bits the mask had.
//
//   Only cpu0 had a cpufreq directory. A guest tells performance cores from efficiency
//   cores by comparing cpuinfo_max_freq across cores, so with one file there was nothing
//   to compare: "0 big (mask: 0x0), 0 little (mask: 0x0)".
//
// What these tests pin is CONSISTENCY, not particular numbers. The host running them is
// not the target device, so asserting "6 cores" would be asserting the wrong thing. What
// must hold on any machine is that every surface answers with the same topology — because
// an engine that sizes a thread pool from sysconf and partitions work by /proc/cpuinfo
// must not see two different machines.
#include "kudroid/platform/CpuInfo.h"
#include "kudroid/platform/MemoryInfo.h"
#include "kudroid/VFSPathRemapper.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <stdint.h>
#include <unistd.h>

extern "C" {
long bionic_sysconf(int name);
int bionic_sched_getaffinity(pid_t pid, size_t cpusetsize, void* mask);
int bionic___sched_cpucount(size_t setsize, const void* set);
int bionic_sched_getcpu(void);
int bionic_get_nprocs(void);
int bionic_get_nprocs_conf(void);
long bionic_get_phys_pages(void);
long bionic_get_avphys_pages(void);
long bionic_syscall(long number, ...);
}

// bionic's sysconf numbering, which is what a guest passes. Deliberately the bionic
// values and not the host's SC_* macros: the point of the shim is that the guest's
// constants differ from glibc's, and _SC_PHYS_PAGES is the case that was missing —
// bionic 98 vs glibc 85.
#define GUEST_SC_PAGESIZE 39
#define GUEST_SC_NPROCESSORS_CONF 96
#define GUEST_SC_NPROCESSORS_ONLN 97
#define GUEST_SC_PHYS_PAGES 98
#define GUEST_SC_AVPHYS_PAGES 99
#define GUEST_SYS_sched_getaffinity 123

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// How many "processor\t: N" lines /proc/cpuinfo has.
int CountProcessors(const std::string& cpuinfo) {
    int count = 0;
    size_t pos = 0;
    while ((pos = cpuinfo.find("processor\t: ", pos)) != std::string::npos) {
        ++count;
        pos += 12;
    }
    return count;
}

int CountProcStatCpuLines(const std::string& stat) {
    int count = 0;
    std::istringstream in(stat);
    std::string line;
    while (std::getline(in, line)) {
        // "cpu0", "cpu1", ... but not the "cpu " aggregate line.
        if (line.rfind("cpu", 0) == 0 && line.size() > 3 && std::isdigit(line[3])) ++count;
    }
    return count;
}

// ── the topology itself ──────────────────────────────────────────────────────

void test_topology_is_self_consistent() {
    std::printf("[cpu] the topology is internally consistent\n");
    const kudroid::CpuTopology& cpu = kudroid::query_cpu_topology();

    std::printf("  cores = %u (perf %u, eff %u), measured = %s\n", cpu.total_cores,
                cpu.performance_cores, cpu.efficiency_cores,
                cpu.measured ? "yes" : "no");

    // Zero is the value that made Unity create no workers at all.
    Check(cpu.total_cores > 0, "the core count is never zero");
    Check(cpu.total_cores <= 64,
          "the count fits in a cpu_set_t word, so the mask can describe every core");

    // The invariant every consumer relies on. A partial sysctl result could otherwise
    // produce classes that do not add up, and the /proc/cpuinfo and /sys writers would
    // then describe different machines.
    Check(cpu.performance_cores + cpu.efficiency_cores == cpu.total_cores,
          "performance + efficiency == total");

    Check(cpu.performance_cores > 0,
          "there is at least one performance core to schedule latency-sensitive work on");

    // A guest classifies cores by comparing frequencies, so a uniform machine must
    // report ONE frequency: two distinct values would invent an asymmetry that is not
    // there and split scheduling across a boundary that does not exist.
    if (cpu.efficiency_cores == 0) {
        Check(cpu.performance_max_khz == cpu.efficiency_max_khz,
              "a uniform machine reports one frequency for every core");
    } else {
        Check(cpu.performance_max_khz > cpu.efficiency_max_khz,
              "performance cores report a higher ceiling than efficiency cores");
    }
    Check(cpu.performance_max_khz > 0, "the reported frequency is not zero");
}

void test_topology_is_stable() {
    std::printf("[cpu] the topology does not change between calls\n");
    const kudroid::CpuTopology first = kudroid::query_cpu_topology();
    const kudroid::CpuTopology second = kudroid::query_cpu_topology();
    Check(first.total_cores == second.total_cores, "the core count is stable");
    Check(first.performance_cores == second.performance_cores,
          "the performance count is stable");

    // Concurrent first-touch must not produce two different answers: the cache is
    // reached from guest file opens on any thread.
    kudroid::cpu_topology_reset_for_test();
    uint32_t seen[8] = {0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([i, &seen] {
            seen[i] = kudroid::query_cpu_topology().total_cores;
        });
    }
    for (auto& t : threads) t.join();
    bool all_equal = true;
    for (int i = 1; i < 8; ++i) {
        if (seen[i] != seen[0]) all_equal = false;
    }
    Check(all_equal && seen[0] > 0,
          "eight threads racing the first query all see the same count");
}

void test_mask_matches_the_count() {
    std::printf("[cpu] the affinity mask names exactly the cores that exist\n");
    const uint32_t cores = kudroid::query_cpu_topology().total_cores;
    const uint64_t mask = kudroid::cpu_online_mask();

    Check(mask != 0, "the mask is not empty");
    Check(__builtin_popcountll(mask) == static_cast<int>(cores),
          std::string("the mask has one bit per core: ") +
              std::to_string(__builtin_popcountll(mask)) + " bits for " +
              std::to_string(cores) + " cores");
    // Bit 0 must be set, or a guest pinning to "the first CPU" finds none available.
    Check((mask & 1ull) != 0, "CPU 0 is in the mask");
}

// ── the surfaces a guest actually reads ──────────────────────────────────────

void test_every_surface_agrees_on_the_core_count() {
    std::printf("[cpu] sysconf, get_nprocs and the affinity mask agree\n");
    const uint32_t cores = kudroid::query_cpu_topology().total_cores;

    // bionic's numbers, not glibc's. Using the host macros here would test the wrong
    // constants — a guest passes 96/97, and those used to be handled by a separate code
    // path from everything else.
    Check(bionic_sysconf(GUEST_SC_NPROCESSORS_ONLN) == static_cast<long>(cores),
          "sysconf(_SC_NPROCESSORS_ONLN) reports the topology count");
    Check(bionic_sysconf(GUEST_SC_NPROCESSORS_CONF) == static_cast<long>(cores),
          "sysconf(_SC_NPROCESSORS_CONF) reports the same");
    Check(bionic_get_nprocs() == static_cast<int>(cores),
          "get_nprocs agrees — it used to bind to the dummy and return 0");
    Check(bionic_get_nprocs_conf() == static_cast<int>(cores),
          "get_nprocs_conf agrees");

    // sched_getcpu must return a VALID core index: a guest indexing a per-core array
    // with it goes out of bounds otherwise.
    const int cpu = bionic_sched_getcpu();
    Check(cpu >= 0 && cpu < static_cast<int>(cores),
          "sched_getcpu returns an index inside the core count");
}

void test_cpucount_counts_the_mask_it_is_given() {
    std::printf("[cpu] __sched_cpucount counts real bits — the 'Cores = 0' suspect\n");

    // The exact sequence a guest performs: fill a set through sched_getaffinity, then
    // count it with CPU_COUNT, which is a macro over __sched_cpucount. The count used to
    // be 0 for every mask because the symbol was missing and bound to the universal
    // dummy, so this pairing is the whole point of the test.
    unsigned long mask = 0;
    Check(bionic_sched_getaffinity(0, sizeof(mask), &mask) == 0,
          "sched_getaffinity fills the set");
    const uint32_t cores = kudroid::query_cpu_topology().total_cores;
    Check(bionic___sched_cpucount(sizeof(mask), &mask) == static_cast<int>(cores),
          std::string("__sched_cpucount reports ") + std::to_string(cores) +
              ", matching the mask it was handed");

    // A guest with a wider cpu_set_t must have all of its words counted, not just the
    // first: bionic counts over the CALLER's declared size.
    unsigned char wide[128] = {0};
    wide[0] = 0x0F;
    wide[64] = 0x03;
    Check(bionic___sched_cpucount(sizeof(wide), wide) == 6,
          "a 128-byte set is counted across every word, not just the first");

    // Degenerate inputs must answer 0 rather than dereference.
    Check(bionic___sched_cpucount(sizeof(mask), nullptr) == 0,
          "a null set counts zero instead of faulting");
    Check(bionic___sched_cpucount(0, &mask) == 0, "a zero-size set counts zero");

    // An empty mask genuinely has no CPUs, and must still say so — the fix must not
    // turn "count the bits" into "report the core count".
    unsigned long empty = 0;
    Check(bionic___sched_cpucount(sizeof(empty), &empty) == 0,
          "an empty mask still counts zero");
}

void test_raw_affinity_syscall_agrees_with_the_wrapper() {
    std::printf("[cpu] the raw syscall and the wrapper describe the same CPUs\n");
    const uint32_t cores = kudroid::query_cpu_topology().total_cores;

    // The two have OPPOSITE success conventions — the raw syscall returns bytes written,
    // the wrapper returns 0 — and they used to derive their masks from separate hardcoded
    // constants. Same content is what matters here.
    unsigned long raw = 0;
    const long written = bionic_syscall(GUEST_SYS_sched_getaffinity, 0, sizeof(raw), &raw);
    Check(written == static_cast<long>(sizeof(raw)),
          "the raw syscall returns the byte count bionic's wrapper expects");

    unsigned long wrapped = 0;
    Check(bionic_sched_getaffinity(0, sizeof(wrapped), &wrapped) == 0,
          "the wrapper succeeds");
    Check(raw == wrapped, "both report the identical mask");
    Check(__builtin_popcountl(raw) == static_cast<int>(cores),
          "and it has one bit per core");
}

void test_memory_surfaces_report_the_device() {
    std::printf("[mem] sysconf reports the measured device memory, not a constant\n");
    const kudroid::SystemMemory mem = kudroid::query_system_memory();

    const long page = bionic_sysconf(GUEST_SC_PAGESIZE);
    Check(page > 0, "the page size is positive");

    // bionic's _SC_PHYS_PAGES is 98; only glibc's 85 was handled, so this number fell
    // through to the host's sysconf(98), which means something unrelated.
    const long phys = bionic_sysconf(GUEST_SC_PHYS_PAGES);
    const long expected_phys = static_cast<long>(mem.total_bytes / static_cast<uint64_t>(page));
    Check(phys == expected_phys,
          std::string("sysconf(98) reports the measured total: ") + std::to_string(phys) +
              " pages vs " + std::to_string(expected_phys));

    const long avail = bionic_sysconf(GUEST_SC_AVPHYS_PAGES);
    Check(avail > 0, "sysconf(99) reports non-zero available pages");
    Check(avail <= phys, "available never exceeds total");

    Check(bionic_get_phys_pages() == phys, "get_phys_pages agrees with sysconf");
    const long avail2 = bionic_get_avphys_pages();
    Check(avail2 > 0 && avail2 <= phys && std::labs(avail2 - avail) <= 16384,
          "get_avphys_pages agrees with sysconf");

    // The specific wrong answer this replaces. 8 GB was reported on every device
    // regardless of what it had; on a 4 GB phone that is double.
    const uint64_t reported_total = static_cast<uint64_t>(phys) * static_cast<uint64_t>(page);
    Check(reported_total == mem.total_bytes,
          "the total derived from sysconf equals the measured total exactly");
}

// ── the pseudo-files ─────────────────────────────────────────────────────────

void test_pseudo_files_describe_the_same_machine() {
    std::printf("[vfs] /proc and /sys describe the topology, not a literal\n");

    // A private root so the test never touches a real android_root, and so the files are
    // written fresh from the current topology.
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("kudroid_cpuinfo_test_" + std::to_string(::getpid()));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    kudroid::VFSPathRemapper& remapper = kudroid::VFSPathRemapper::getInstance();
    remapper.setDocumentsDirectory(root.string());
    Check(remapper.init_pseudo_files(), "the pseudo-files were written");

    const std::string base = remapper.androidRoot();
    const kudroid::CpuTopology& cpu = kudroid::query_cpu_topology();
    const kudroid::SystemMemory mem = kudroid::query_system_memory();

    // /proc/cpuinfo: one block per core. It listed exactly 8, always.
    const std::string cpuinfo = ReadFile(base + "/proc/cpuinfo");
    Check(!cpuinfo.empty(), "/proc/cpuinfo exists");
    Check(CountProcessors(cpuinfo) == static_cast<int>(cpu.total_cores),
          std::string("/proc/cpuinfo lists ") + std::to_string(cpu.total_cores) +
              " processors, got " + std::to_string(CountProcessors(cpuinfo)));

    // /sys/.../present must name the same range. A guest that parses this and a guest
    // that counts cpuinfo lines have to reach the same number.
    const std::string present = ReadFile(base + "/sys/devices/system/cpu/present");
    const std::string expected_range =
        cpu.total_cores <= 1 ? "0\n" : "0-" + std::to_string(cpu.total_cores - 1) + "\n";
    Check(present == expected_range,
          std::string("/sys/.../present says ") + expected_range.substr(0, expected_range.size() - 1) +
              ", got " + present.substr(0, present.empty() ? 0 : present.size() - 1));
    Check(ReadFile(base + "/sys/devices/system/cpu/online") == expected_range,
          "/sys/.../online agrees");
    Check(ReadFile(base + "/sys/devices/system/cpu/possible") == expected_range,
          "/sys/.../possible agrees");

    // /proc/stat needs a line per core too, or a guest reading load per CPU finds fewer
    // CPUs than /proc/cpuinfo advertised.
    const std::string stat = ReadFile(base + "/proc/stat");
    Check(CountProcStatCpuLines(stat) == static_cast<int>(cpu.total_cores),
          "/proc/stat has one cpuN line per core");

    // EVERY core needs a cpufreq directory. Only cpu0 had one, which is why the
    // big.LITTLE split came out as "0 big, 0 little": there was nothing to compare.
    bool all_have_cpufreq = true;
    bool classes_differ = cpu.efficiency_cores == 0;  // uniform: nothing to distinguish
    for (uint32_t i = 0; i < cpu.total_cores; ++i) {
        const std::string dir =
            base + "/sys/devices/system/cpu/cpu" + std::to_string(i) + "/cpufreq";
        const std::string max = ReadFile(dir + "/cpuinfo_max_freq");
        const std::string min = ReadFile(dir + "/cpuinfo_min_freq");
        const std::string cur = ReadFile(dir + "/scaling_cur_freq");
        if (max.empty() || min.empty() || cur.empty()) {
            all_have_cpufreq = false;
            continue;
        }
        const long max_khz = std::atol(max.c_str());
        const long min_khz = std::atol(min.c_str());
        const long cur_khz = std::atol(cur.c_str());
        // A guest that reads scaling_cur_freq outside [min, max] treats the file as
        // unusable and falls back to guessing.
        if (!(min_khz > 0 && min_khz <= cur_khz && cur_khz <= max_khz)) {
            all_have_cpufreq = false;
        }
        if (cpu.efficiency_cores > 0) {
            const bool performance = i < cpu.performance_cores;
            const long expected =
                performance ? cpu.performance_max_khz : cpu.efficiency_max_khz;
            if (max_khz != expected) classes_differ = false;
        }
    }
    Check(all_have_cpufreq,
          "every core has cpufreq with min <= cur <= max — not just cpu0");
    Check(classes_differ,
          "cpufreq_max_freq distinguishes the core classes, so big.LITTLE is detectable");

    // /proc/meminfo: the literal that produced "Memory = 8000mb".
    const std::string meminfo = ReadFile(base + "/proc/meminfo");
    Check(meminfo.find("MemTotal:") != std::string::npos, "/proc/meminfo has MemTotal");
    Check(meminfo.find("8192000 kB") == std::string::npos,
          "the 8192000 kB literal is gone — that is where 8000mb came from");

    uint64_t total_kb = 0;
    uint64_t avail_kb = 0;
    uint64_t free_kb = 0;
    {
        std::istringstream in(meminfo);
        std::string line;
        while (std::getline(in, line)) {
            unsigned long long v = 0;
            if (std::sscanf(line.c_str(), "MemTotal: %llu kB", &v) == 1) total_kb = v;
            else if (std::sscanf(line.c_str(), "MemAvailable: %llu kB", &v) == 1) avail_kb = v;
            else if (std::sscanf(line.c_str(), "MemFree: %llu kB", &v) == 1) free_kb = v;
        }
    }
    Check(total_kb == mem.total_bytes / 1024ull,
          std::string("MemTotal is the measured total: ") + std::to_string(total_kb) +
              " kB vs " + std::to_string(mem.total_bytes / 1024ull));
    Check(avail_kb > 0, "MemAvailable is non-zero");
    // A guest computing used = MemTotal - MemAvailable underflows if this is violated,
    // and one comparing MemFree against MemAvailable concludes the file is corrupt.
    Check(avail_kb <= total_kb, "MemAvailable never exceeds MemTotal");
    Check(free_kb <= avail_kb, "MemFree never exceeds MemAvailable");

    // The Java-visible figure must agree with the file. An app sizing a pool from
    // Runtime.availableProcessors and a native pool from sysconf must not disagree.
    Check(bionic_sysconf(GUEST_SC_NPROCESSORS_ONLN) == CountProcessors(cpuinfo),
          "sysconf and /proc/cpuinfo report the same count");

    std::filesystem::remove_all(root, ec);
}

void test_device_files_are_regenerated_not_preserved() {
    std::printf("[vfs] a stale device file from an older build is overwritten\n");

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("kudroid_cpuinfo_stale_" + std::to_string(::getpid()));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    kudroid::VFSPathRemapper& remapper = kudroid::VFSPathRemapper::getInstance();
    remapper.setDocumentsDirectory(root.string());
    Check(remapper.init_pseudo_files(), "the files were written once");
    const std::string base = remapper.androidRoot();

    // Exactly what an older build left behind. The writer used to preserve any non-empty
    // file, so this would survive forever and the user would keep seeing 8000mb with
    // nothing to explain why.
    {
        std::ofstream out(base + "/proc/meminfo", std::ios::binary | std::ios::trunc);
        out << "MemTotal:        8192000 kB\nMemFree: 1 kB\nMemAvailable: 2 kB\n";
    }
    Check(remapper.init_pseudo_files(), "the files were written again");
    const std::string meminfo = ReadFile(base + "/proc/meminfo");
    Check(meminfo.find("8192000 kB") == std::string::npos,
          "the stale literal was replaced, not kept");

    // A file that is NOT a device figure is still preserved: build.prop is something a
    // user may edit, and rewriting it every run would discard that.
    {
        std::ofstream out(base + "/system/build.prop", std::ios::binary | std::ios::trunc);
        out << "ro.custom.edit=1\n";
    }
    Check(remapper.init_pseudo_files(), "and again");
    Check(ReadFile(base + "/system/build.prop").find("ro.custom.edit=1") != std::string::npos,
          "a non-device file the user edited is preserved");

    std::filesystem::remove_all(root, ec);
}

// A 2+4 asymmetric device — an iPhone 11 / A13 — described by hand.
//
// This is the ONLY way to exercise the big.LITTLE path from a host test. The CI runner is
// a uniform x86 machine, so without an override the code that gives the two classes
// different frequencies never runs anywhere before it ships — and that code is exactly
// what was missing when Unity reported "0 big (mask: 0x0), 0 little (mask: 0x0)". A test
// that only ever sees a uniform machine cannot catch its absence.
void test_asymmetric_device_is_described_correctly() {
    std::printf("[cpu] a 2+4 big.LITTLE device is described so a guest can classify it\n");

    kudroid::CpuTopology a13;
    a13.total_cores = 6;
    a13.performance_cores = 2;
    a13.efficiency_cores = 4;
    a13.performance_max_khz = 2800000;
    a13.efficiency_max_khz = 1800000;
    a13.measured = true;
    kudroid::cpu_topology_override_for_test(a13);

    Check(bionic_sysconf(GUEST_SC_NPROCESSORS_ONLN) == 6,
          "sysconf reports all six cores, not just the performance ones");
    Check(kudroid::cpu_online_mask() == 0x3F,
          "the mask names six cores (0x3F)");
    unsigned long mask = 0;
    bionic_sched_getaffinity(0, sizeof(mask), &mask);
    Check(bionic___sched_cpucount(sizeof(mask), &mask) == 6,
          "__sched_cpucount counts six — the number Unity printed as 0");

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("kudroid_cpuinfo_a13_" + std::to_string(::getpid()));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    kudroid::VFSPathRemapper& remapper = kudroid::VFSPathRemapper::getInstance();
    remapper.setDocumentsDirectory(root.string());
    Check(remapper.init_pseudo_files(), "the pseudo-files were written for the A13");
    const std::string base = remapper.androidRoot();

    Check(CountProcessors(ReadFile(base + "/proc/cpuinfo")) == 6,
          "/proc/cpuinfo lists six processors");
    Check(ReadFile(base + "/sys/devices/system/cpu/present") == "0-5\n",
          "/sys/.../present says 0-5");

    // The classification a guest performs: read cpuinfo_max_freq for every core, group by
    // value. Two distinct values with the right counts is what makes "2 big, 4 little"
    // discoverable at all.
    int fast = 0;
    int slow = 0;
    bool every_core_readable = true;
    for (int i = 0; i < 6; ++i) {
        const std::string max = ReadFile(base + "/sys/devices/system/cpu/cpu" +
                                        std::to_string(i) + "/cpufreq/cpuinfo_max_freq");
        if (max.empty()) { every_core_readable = false; continue; }
        const long khz = std::atol(max.c_str());
        if (khz == 2800000) ++fast;
        else if (khz == 1800000) ++slow;
    }
    Check(every_core_readable, "all six cores expose cpuinfo_max_freq");
    Check(fast == 2 && slow == 4,
          std::string("grouping by frequency yields 2 performance and 4 efficiency cores, got ") +
              std::to_string(fast) + " and " + std::to_string(slow));

    // The CPU part IDs are the second route to the same conclusion, for a guest that does
    // not read cpufreq. They must not contradict the frequencies.
    const std::string cpuinfo = ReadFile(base + "/proc/cpuinfo");
    int perf_parts = 0;
    size_t pos = 0;
    while ((pos = cpuinfo.find("CPU part\t: 0xd47", pos)) != std::string::npos) {
        ++perf_parts;
        pos += 16;
    }
    Check(perf_parts == 2,
          "the CPU part IDs agree with the frequencies about which cores are which");

    std::filesystem::remove_all(root, ec);
    kudroid::cpu_topology_reset_for_test();
}

}  // namespace

int main() {
    std::printf("=== device CPU and memory figures ===\n");

    test_topology_is_self_consistent();
    test_topology_is_stable();
    test_mask_matches_the_count();
    test_every_surface_agrees_on_the_core_count();
    test_cpucount_counts_the_mask_it_is_given();
    test_raw_affinity_syscall_agrees_with_the_wrapper();
    test_memory_surfaces_report_the_device();
    test_pseudo_files_describe_the_same_machine();
    test_device_files_are_regenerated_not_preserved();
    test_asymmetric_device_is_described_correctly();

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
