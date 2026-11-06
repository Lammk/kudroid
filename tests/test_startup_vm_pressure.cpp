#include "kudroid/AlignedVmReservations.h"

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <sys/mman.h>
#include <sys/resource.h>

int main() {
#if defined(__linux__)
    kudroid::AlignedVmReservations owner;
    size_t pages = 0;
    std::ifstream("/proc/self/statm") >> pages;
    rlimit limit{};
    constexpr size_t gib = size_t{1} << 30;
    if (!pages || getrlimit(RLIMIT_AS, &limit)) return 2;
    const size_t budget = pages * owner.pageSize() + 6 * gib;
    if (limit.rlim_max != RLIM_INFINITY && limit.rlim_max < budget) return 2;
    limit.rlim_cur = budget;
    if (setrlimit(RLIMIT_AS, &limit)) return 2;
    if (!owner.prepare(4 * gib, owner.pageSize())) return 1;
    errno = 0;
    void* blocked = mmap(nullptr, 3 * gib, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool exhausted = blocked == MAP_FAILED && errno == ENOMEM;
    if (blocked != MAP_FAILED) munmap(blocked, 3 * gib);
    if (!owner.releasePrepared()) return 1;
    auto* recovered = static_cast<unsigned char*>(
        mmap(nullptr, 3 * gib, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    const bool restored = recovered != MAP_FAILED;
    bool fullSpan = false;
    if (restored) {
        auto* tail = recovered + 3 * gib - owner.pageSize();
        fullSpan = mprotect(tail, owner.pageSize(), PROT_READ | PROT_WRITE) == 0;
        if (fullSpan) {
            recovered[3 * gib - 1] = 0x5a;
            fullSpan = recovered[3 * gib - 1] == 0x5a;
        }
        munmap(recovered, 3 * gib);
    }
    std::printf("VM pressure: reservation exhausted budget=%d, cleanup restored allocation=%d, full tail=%d\n",
                exhausted, restored, fullSpan);
    return exhausted && restored && fullSpan ? 0 : 1;
#else
    return 2;
#endif
}
