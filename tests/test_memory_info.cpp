// Host-side check of platform/MemoryInfo.cpp: the figures must come from the
// machine, be self-consistent, and never be zero.
//
// A zero here is not a cosmetic bug — an app reading 0 available memory concludes
// the device is out of memory and degrades or refuses to start — so every field is
// checked for plausibility, not just for being present.
#include "kudroid/platform/MemoryInfo.h"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

constexpr uint64_t kMiB = 1024ull * 1024ull;

}  // namespace

int main() {
    std::printf("=== platform memory figures ===\n");

    const kudroid::SystemMemory mem = kudroid::query_system_memory();

    std::printf("  total     = %llu MiB\n", (unsigned long long)(mem.total_bytes / kMiB));
    std::printf("  available = %llu MiB\n", (unsigned long long)(mem.available_bytes / kMiB));
    std::printf("  proc avail= %llu MiB\n",
                (unsigned long long)(mem.process_available_bytes / kMiB));
    std::printf("  proc rss  = %llu MiB\n",
                (unsigned long long)(mem.process_resident_bytes / kMiB));
    std::printf("  low_memory= %s\n", mem.low_memory ? "true" : "false");
    std::printf("  measured  = %s\n", mem.measured ? "true" : "false");

    // On Linux (host and CI) /proc/meminfo always exists, so a fallback here means
    // the reader is broken rather than the platform being unsupported.
#if !defined(__APPLE__)
    Check(mem.measured, "figures were measured, not guessed");
#endif

    // Never zero: that is the value that makes apps refuse to run.
    Check(mem.total_bytes > 0, "total is non-zero");
    Check(mem.available_bytes > 0, "available is non-zero");

    // A machine that can build this has more than 256 MiB, and reporting less than
    // that would make any app decide it cannot run.
    Check(mem.total_bytes > 256 * kMiB,
          std::string("total is plausible (> 256 MiB), got ") +
              std::to_string(mem.total_bytes / kMiB) + " MiB");

    // available <= total, or callers computing used = total - available underflow.
    Check(mem.available_bytes <= mem.total_bytes, "available never exceeds total");

    Check(mem.process_resident_bytes <= mem.total_bytes,
          "process footprint never exceeds total RAM");

    // The heap class is what apps size caches from; Android's own range.
    const int cls = kudroid::memory_class_mb();
    std::printf("  memory class       = %d MiB\n", cls);
    Check(cls >= 32 && cls <= 512,
          std::string("memory class within Android's range, got ") + std::to_string(cls));

    const int large = kudroid::large_memory_class_mb();
    std::printf("  large memory class = %d MiB\n", large);
    // largeHeap must not be smaller than the normal heap, or an app opting in gets
    // less than it had.
    Check(large >= cls, "large memory class is at least the normal one");
    Check(large <= 1024, "large memory class stays bounded");

    // Two consecutive reads must not disagree wildly: the value is polled while an
    // app is running, and a figure that swings by gigabytes between calls makes
    // adaptive caches thrash.
    const kudroid::SystemMemory again = kudroid::query_system_memory();
    Check(again.total_bytes == mem.total_bytes, "total is stable across calls");

    if (g_failures == 0) {
        std::printf("=== platform memory PASSED ===\n");
        return 0;
    }
    std::printf("=== platform memory FAILED (%d) ===\n", g_failures);
    return 1;
}
