#include "kudroid/platform/MemoryInfo.h"

#include <cstdio>
#include <cstring>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/task.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#if defined(__has_include)
#if __has_include(<os/proc.h>)
#include <os/proc.h>
#define KUDROID_HAVE_OS_PROC 1
#endif
#endif
#else
#include <cstdlib>
#endif

namespace kudroid {
namespace {

constexpr uint64_t kMiB = 1024ull * 1024ull;
constexpr uint64_t kGiB = 1024ull * kMiB;

// Used only when the platform tells us nothing. Deliberately mid-range: too high
// and an app allocates past what the device has, too low and it runs degraded.
constexpr uint64_t kFallbackTotal = 4 * kGiB;
constexpr uint64_t kFallbackAvailable = 2 * kGiB;

#if defined(__APPLE__)

uint64_t PhysicalMemory() {
    uint64_t bytes = 0;
    size_t len = sizeof(bytes);
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    if (sysctl(mib, 2, &bytes, &len, nullptr, 0) == 0 && bytes > 0) return bytes;
    return 0;
}

// System-wide reclaimable memory.
//
// "Available" is not just free pages: inactive and purgeable pages are handed over
// on demand, and ignoring them under-reports by gigabytes on a device that has been
// running a while — which would make every app think it is nearly out of memory.
uint64_t AvailableMemory() {
    mach_port_t host = mach_host_self();
    vm_size_t page_size = 0;
    if (host_page_size(host, &page_size) != KERN_SUCCESS || page_size == 0) {
        page_size = 4096;
    }

    vm_statistics64_data_t vm{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    const kern_return_t kr =
        host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count);
    if (kr != KERN_SUCCESS) return 0;

    const uint64_t reclaimable =
        static_cast<uint64_t>(vm.free_count) + static_cast<uint64_t>(vm.inactive_count) +
        static_cast<uint64_t>(vm.purgeable_count);

    return reclaimable * static_cast<uint64_t>(page_size);
}

// Bytes this process may still allocate before jetsam kills it.
//
// The distinction from system-available memory is the whole point on iOS: the
// per-process limit is a fraction of physical RAM, so an app that sizes itself from
// system-available memory allocates its way into a kill. Available since iOS 13.
uint64_t ProcessAvailableMemory() {
#if defined(KUDROID_HAVE_OS_PROC) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    if (__builtin_available(iOS 13.0, *)) {
        const size_t remaining = os_proc_available_memory();
        if (remaining > 0) return static_cast<uint64_t>(remaining);
    }
#endif
    return 0;
}

uint64_t ProcessResidentMemory() {
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info),
                  &count) != KERN_SUCCESS) {
        return 0;
    }
    // phys_footprint is what jetsam measures, not resident_size: it counts
    // compressed and IOKit memory the process is charged for. Using resident_size
    // would under-report and make the headroom calculation optimistic.
    return static_cast<uint64_t>(info.phys_footprint);
}

#else  // Linux / host tests

// /proc/meminfo, so host builds report the real machine instead of a constant and
// the same code path is exercised in tests.
bool ReadMemInfo(uint64_t* total, uint64_t* available) {
    FILE* f = std::fopen("/proc/meminfo", "r");
    if (f == nullptr) return false;

    uint64_t total_kb = 0;
    uint64_t available_kb = 0;
    uint64_t free_kb = 0;
    uint64_t cached_kb = 0;
    char line[256];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        uint64_t value = 0;
        if (std::sscanf(line, "MemTotal: %llu kB",
                        reinterpret_cast<unsigned long long*>(&value)) == 1) {
            total_kb = value;
        } else if (std::sscanf(line, "MemAvailable: %llu kB",
                               reinterpret_cast<unsigned long long*>(&value)) == 1) {
            available_kb = value;
        } else if (std::sscanf(line, "MemFree: %llu kB",
                               reinterpret_cast<unsigned long long*>(&value)) == 1) {
            free_kb = value;
        } else if (std::sscanf(line, "Cached: %llu kB",
                               reinterpret_cast<unsigned long long*>(&value)) == 1) {
            cached_kb = value;
        }
    }
    std::fclose(f);

    if (total_kb == 0) return false;
    // MemAvailable is the kernel's own estimate and the right answer; older kernels
    // do not export it, hence the free+cached approximation.
    if (available_kb == 0) available_kb = free_kb + cached_kb;

    *total = total_kb * 1024ull;
    *available = available_kb * 1024ull;
    return true;
}

uint64_t ProcessResidentMemoryLinux() {
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (f == nullptr) return 0;
    unsigned long long size_pages = 0;
    unsigned long long resident_pages = 0;
    const int got = std::fscanf(f, "%llu %llu", &size_pages, &resident_pages);
    std::fclose(f);
    if (got != 2) return 0;
    return static_cast<uint64_t>(resident_pages) * 4096ull;
}

#endif

}  // namespace

SystemMemory query_system_memory() {
    SystemMemory out;

#if defined(__APPLE__)
    out.total_bytes = PhysicalMemory();
    out.available_bytes = AvailableMemory();
    out.process_available_bytes = ProcessAvailableMemory();
    out.process_resident_bytes = ProcessResidentMemory();
    out.measured = out.total_bytes > 0 && out.available_bytes > 0;

    // Memory pressure, judged against what this PROCESS may still allocate.
    //
    // The previous test was `vm.free_count < 256 MiB`, which is permanently true on
    // iOS: the kernel keeps almost nothing on the free list and serves allocations
    // from the inactive and purgeable lists instead. So every log line in the captured
    // ULTRAKILL session carried low_memory=1 while reporting available=1.2 GB and
    // process_headroom=2.0 GB beside it — three fields, one contradicting the other
    // two, on 188 consecutive lines.
    //
    // That is not merely noise. The same flag is what ActivityManager.MemoryInfo
    // reports through nativeIsLowMemory(), so a guest reading it drops its caches and
    // runs degraded on a device with two spare gigabytes.
    //
    // jetsam kills on the per-process footprint, so the process limit is the figure
    // that decides whether this app is in trouble. An eighth is the point where iOS
    // starts sending memory warnings in practice, and it is a fraction rather than a
    // constant because the limit itself scales with the device.
    if (out.process_available_bytes > 0) {
        const uint64_t footprint_limit =
            out.process_available_bytes + out.process_resident_bytes;
        out.low_memory = out.process_available_bytes < footprint_limit / 8;
    } else {
        // No per-process figure: fall back to the system-wide one, on the same tenth
        // that the Linux path uses, so the two platforms answer the same question.
        out.low_memory = out.total_bytes > 0 && out.available_bytes < out.total_bytes / 10;
    }
#else
    uint64_t total = 0;
    uint64_t available = 0;
    if (ReadMemInfo(&total, &available)) {
        out.total_bytes = total;
        out.available_bytes = available;
        // No per-process cap on a normal Linux host, so the process may use what
        // the system has.
        out.process_available_bytes = available;
        out.process_resident_bytes = ProcessResidentMemoryLinux();
        out.low_memory = available < (total / 10);
        out.measured = true;
    }
#endif

    // Never report zero. An app that reads 0 available concludes the device is out
    // of memory and either degrades or refuses to start, so an approximate figure
    // is strictly better than an honest zero here.
    if (out.total_bytes == 0) out.total_bytes = kFallbackTotal;
    if (out.available_bytes == 0) {
        out.available_bytes = out.total_bytes / 2;
        if (out.available_bytes == 0) out.available_bytes = kFallbackAvailable;
    }
    // Available can exceed total on a fallback path; clamp so callers computing
    // used = total - available never see a negative.
    if (out.available_bytes > out.total_bytes) out.available_bytes = out.total_bytes;

    return out;
}

int memory_class_mb() {
    const SystemMemory mem = query_system_memory();

    // When the platform reports a per-process limit, that IS the heap budget: it is
    // the number that gets the process killed when exceeded.
    if (mem.process_available_bytes > 0) {
        const uint64_t budget =
            (mem.process_available_bytes + mem.process_resident_bytes) / kMiB;
        // Keep it inside the range Android itself uses. Values outside it are not
        // something apps are written to handle: a huge memory class makes them
        // preallocate absurd caches, a tiny one makes them refuse to run.
        if (budget < 32) return 32;
        if (budget > 512) return 512;
        return static_cast<int>(budget);
    }

    // Otherwise mirror Android's tiering by total RAM.
    const uint64_t gib = mem.total_bytes / kGiB;
    if (gib >= 8) return 512;
    if (gib >= 6) return 384;
    if (gib >= 4) return 256;
    if (gib >= 3) return 192;
    if (gib >= 2) return 128;
    if (gib >= 1) return 96;
    return 48;
}

int large_memory_class_mb() {
    // largeHeap raises the cap; Android's factor is roughly 2-3x, capped by what the
    // device physically has.
    const int base = memory_class_mb();
    const SystemMemory mem = query_system_memory();
    const int by_ram = static_cast<int>((mem.total_bytes / kMiB) / 4);
    int large = base * 3;
    if (by_ram > 0 && large > by_ram) large = by_ram;
    if (large < base) large = base;
    if (large > 1024) large = 1024;
    return large;
}

}  // namespace kudroid
