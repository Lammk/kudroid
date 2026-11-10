#include "kudroid/FmodVorbisFallback.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <atomic>
#include <mutex>
#include <cerrno>

#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#include <signal.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#elif defined(__linux__)
#include <signal.h>
#include <ucontext.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#endif

namespace kudroid {

namespace {

struct FmodVorbisEntry {
    const void* p1;
    uint32_t len1;
    uint32_t crc;
    const void* p2;
    int32_t off2;
    int32_t len2;
};

#include "fmod/VorbisPayload.inc"
#include "fmod/VorbisEntries.inc"


struct FmodVorbisTrapSite {
    uintptr_t patchAddr;
    uintptr_t targetFound;
    uintptr_t resumeAfter;
    uint32_t  origInst;  // instruction the BRK replaced, emulated when the fallback misses
    uint32_t  probe;     // 1 = lookup loop head (reports the asked CRC), 0 = builtin-table miss
};
static constexpr size_t kMaxTrapSites = 8;
static std::atomic<size_t> g_fmod_num_sites{0};
static FmodVorbisTrapSite g_fmod_sites[kMaxTrapSites];
static std::mutex g_fmod_registration_mutex;

// A bank needs a handful of codebooks and every clip of a bank asks for the same
// CRC, so the budget is per distinct CRC, not per call: one retried clip must not
// spend the log on itself and hide the codebook a later bank needs.
constexpr int kMaxLoggedCrcs = 24;
std::atomic<uint64_t> g_fmod_probeCrcs[kMaxLoggedCrcs]{};
std::atomic<uint64_t> g_fmod_missCrcs[kMaxLoggedCrcs]{};
static_assert(std::atomic<uint64_t>::is_always_lock_free);

void fmod_vorbis_log_crc(uint32_t crc, bool probe, bool served) {
    auto* list = probe ? g_fmod_probeCrcs : g_fmod_missCrcs;
    const uint64_t key = uint64_t(crc) + 1;
    bool claimed = false;
    for (int i = 0; i < kMaxLoggedCrcs; ++i) {
        uint64_t expected = 0;
        if (list[i].compare_exchange_strong(expected, key, std::memory_order_relaxed)) {
            claimed = true;
            break;
        }
        if (expected == key) return;
    }
    if (!claimed) return;
    // Stack-only formatting and write avoid stdio locks in the signal handler.
    char line[128];
    size_t length = 0;
    auto append = [&](const char* s) {
        while (*s && length < sizeof(line)) line[length++] = *s++;
    };
    append(probe ? "[KuDroidFmod] setup lookup crc=0x" :
                   "[KuDroidFmod] builtin table missed crc=0x");
    constexpr char digits[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4) line[length++] = digits[(crc >> shift) & 15];
    append(probe ? (served ? " (fallback available)\n" : " (no fallback entry)\n") :
                   (served ? " -> served fallback setup\n" : " -> no fallback entry\n"));
    const int saved = errno;
    (void)::write(STDERR_FILENO, line, length);
    errno = saved;
}

} // namespace

const void* kudroid_fmod_vorbis_lookup(uint32_t crc) {
    for (size_t i = 0; i < sizeof(g_fmod_entries) / sizeof(g_fmod_entries[0]); ++i) {
        if (g_fmod_entries[i].crc == crc) {
            // FMOD expects pointer to entry + 0x10 in x28
            return reinterpret_cast<const char*>(&g_fmod_entries[i]) + 0x10;
        }
    }
    return nullptr;
}

bool kudroid_arm_fmod_vorbis_trap(uintptr_t patchAddr, uintptr_t targetFound,
                                  uintptr_t resumeAfter, uint32_t origInst, bool probe) {
    if (!patchAddr || !targetFound || !resumeAfter || (patchAddr & 3) ||
        (origInst != 0xB85FC389 && origInst != 0xF9400348)) return false;
    std::lock_guard<std::mutex> lock(g_fmod_registration_mutex);
    const size_t idx = g_fmod_num_sites.load(std::memory_order_relaxed);
    for (size_t i = 0; i < idx; ++i) {
        const auto& s = g_fmod_sites[i];
        if (s.patchAddr == patchAddr)
            return s.targetFound == targetFound && s.resumeAfter == resumeAfter &&
                   s.origInst == origInst && s.probe == (probe ? 1u : 0u);
    }
    if (idx >= kMaxTrapSites) return false;
    g_fmod_sites[idx] = {patchAddr, targetFound, resumeAfter, origInst, probe ? 1u : 0u};
    std::atomic_thread_fence(std::memory_order_release);
    g_fmod_num_sites.store(idx + 1, std::memory_order_release);
    return true;
}

namespace {

// Kernel-mediated reads fail cleanly instead of recursively faulting in a handler.
bool trap_read(uintptr_t address, void* out, size_t size) {
#if defined(__APPLE__)
    vm_size_t copied = 0;
    return vm_read_overwrite(mach_task_self(), static_cast<vm_address_t>(address),
                             static_cast<vm_size_t>(size),
                             reinterpret_cast<vm_address_t>(out), &copied) ==
               KERN_SUCCESS && copied == static_cast<vm_size_t>(size);
#elif defined(__linux__) && defined(SYS_process_vm_readv)
    const int saved = errno;
    iovec local{out, size};
    iovec remote{reinterpret_cast<void*>(address), size};
    const long copied = syscall(SYS_process_vm_readv, getpid(), &local, 1, &remote, 1, 0);
    errno = saved;
    return copied == static_cast<long>(size);
#else
    return false;
#endif
}

// Shared body: the two hosts differ only in how the register file is reached.
template <typename GetX, typename SetX, typename GetPC, typename SetPC>
bool fmod_vorbis_trap_apply(GetX getX, SetX setX, GetPC getPC, SetPC setPC) {
    const uintptr_t pc_val = getPC();
    if (!pc_val || (pc_val & 3) != 0) return false;
    const FmodVorbisTrapSite* site = nullptr;
    const size_t count = g_fmod_num_sites.load(std::memory_order_acquire);
    for (size_t s = 0; s < count && s < kMaxTrapSites; ++s) {
        if (g_fmod_sites[s].patchAddr == pc_val) {
            site = &g_fmod_sites[s];
            break;
        }
    }
    if (site == nullptr) return false;
    uint32_t inst = 0;
    if (!trap_read(pc_val, &inst, sizeof(inst)) ||
        inst != (0xd4200000u | (0x464du << 5))) return false;

    const uint32_t crc = static_cast<uint32_t>(getX(25));
    const void* entry = kudroid_fmod_vorbis_lookup(crc);
    if (!site->probe && entry != nullptr) {
        setX(28, reinterpret_cast<uint64_t>(entry));
        setPC(site->targetFound);
        fmod_vorbis_log_crc(crc, false, true);
        return true;
    }
    // Probes observe the builtin lookup; only the miss site substitutes data.
    if (site->origInst == 0xB85FC389) {  // ldur w9, [x28, #-4]
        const uint64_t base = getX(28);
        uint32_t value = 0;
        if (!trap_read(base - 4, &value, sizeof(value))) return false;
        setX(9, value);
    } else {  // ldr x8, [x26]
        const uint64_t addr = getX(26);
        uint64_t value = 0;
        if (!trap_read(addr, &value, sizeof(value))) return false;
        setX(8, value);
    }
    setPC(site->resumeAfter);
    fmod_vorbis_log_crc(crc, site->probe != 0, entry != nullptr);
    return true;
}

} // namespace

bool bionic_handle_fmod_vorbis_trap(void* ucontext) {
    if (!ucontext) return false;
#if defined(__aarch64__) && defined(__APPLE__)
    ucontext_t* uc = static_cast<ucontext_t*>(ucontext);
    if (!uc || !uc->uc_mcontext) return false;
    auto& ss = uc->uc_mcontext->__ss;
    return fmod_vorbis_trap_apply(
        [&](int n) { return static_cast<uint64_t>(ss.__x[n]); },
        [&](int n, uint64_t v) { ss.__x[n] = v; },
        [&]() { return static_cast<uintptr_t>(ss.__pc); },
        [&](uintptr_t v) { ss.__pc = v; });
#elif defined(__aarch64__) && defined(__linux__)
    ucontext_t* uc = static_cast<ucontext_t*>(ucontext);
    if (!uc) return false;
    auto& mc = uc->uc_mcontext;
    return fmod_vorbis_trap_apply(
        [&](int n) { return static_cast<uint64_t>(mc.regs[n]); },
        [&](int n, uint64_t v) { mc.regs[n] = v; },
        [&]() { return static_cast<uintptr_t>(mc.pc); },
        [&](uintptr_t v) { mc.pc = v; });
#else
    (void)ucontext;
    return false;
#endif
}

} // namespace kudroid
