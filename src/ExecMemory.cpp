#include "kudroid/ExecMemory.h"

#include <cerrno>
#include <csetjmp>
#include <cstdio>
#include <cstring>

#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#if !defined(__aarch64__) && !defined(__arm64__)
// The fetch probe executes AArch64 code; only meaningful on arm64 hosts.
#define KUDROID_EXECMEM_PROBE_EXEC 0
#else
#define KUDROID_EXECMEM_PROBE_EXEC 1
#endif

#if defined(__APPLE__)
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#endif

namespace kudroid {

namespace {

// One probe page is enough: the test function is two instructions.
constexpr size_t kProbeSize = 4096;

// adrp x8, #0 ; ret — the ADRP matters: page-relative addressing inside freshly
// written memory is exactly the instruction class hardware monitors on strict-W^X
// regimes, so a probe without it can pass where real guest code faults.
constexpr uint32_t kProbeCode[] = {0x90000008u, 0xD65F03C0u};

void FlushIcache(void* start, size_t len) {
#if defined(__APPLE__)
    sys_icache_invalidate(start, len);
#else
    __builtin___clear_cache(reinterpret_cast<char*>(start),
                            reinterpret_cast<char*>(start) + len);
#endif
}

#if defined(__APPLE__)
// Thread-local JIT write-enable; ISB per the platform's toggle contract.
void EnableJitWrites(bool enable) {
    pthread_jit_write_protect_np(enable ? 0 : 1);
    asm volatile("isb sy" ::: "memory");
}

// Alias the same backing pages at a second virtual address with RX protection.
// Writes through the RW view are visible to fetches through the RX view; no
// runtime permission transition is ever performed.
bool TryDualMap(void* backing, size_t size, void** outAlias) {
    mach_vm_address_t alias = 0;
    kern_return_t kr = mach_vm_remap(mach_task_self(), &alias,
                                     reinterpret_cast<mach_vm_address_t>(backing),
                                     size, 0, VM_FLAGS_ANYWHERE, VM_INHERIT_DEFAULT);
    if (kr != KERN_SUCCESS) return false;
    kr = mach_vm_protect(mach_task_self(), alias, size, FALSE,
                         VM_PROT_READ | VM_PROT_EXECUTE);
    if (kr != KERN_SUCCESS) {
        mach_vm_deallocate(mach_task_self(), alias, size);
        return false;
    }
    *outAlias = reinterpret_cast<void*>(static_cast<uintptr_t>(alias));
    return true;
}

bool TryMapJit(void** out, size_t size) {
    void* p = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    if (p == MAP_FAILED) return false;
    *out = p;
    return true;
}
#elif defined(__linux__)
// Dual-map on Linux: one memfd backing both views. Writes through the RW
// mapping land in the same pages the RX mapping fetches from — W^X applies to
// mappings, not inodes, so no runtime permission transition ever occurs.
// MAP_PRIVATE would COW on write and break write visibility, so both mappings
// are MAP_SHARED.
bool TryDualMap(void* backing, size_t size, void** outAlias) {
    const int fd = static_cast<int>(::syscall(SYS_memfd_create, "kudroid-exec", 0));
    if (fd < 0) return false;
    if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
        ::close(fd);
        return false;
    }
    void* alias = ::mmap(nullptr, size, PROT_READ | PROT_EXEC, MAP_SHARED,
                         fd, 0);
    if (alias == MAP_FAILED) {
        ::close(fd);
        return false;
    }
    // Re-map the caller's backing over the memfd so both views share pages.
    void* rw = ::mmap(backing, size, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_FIXED, fd, 0);
    if (rw != backing) {
        ::munmap(alias, size);
        ::close(fd);
        return false;
    }
    ::close(fd);
    *outAlias = alias;
    return true;
}
#else
// No aliasing primitive on this platform; dual-map is unavailable.
bool TryDualMap(void*, size_t, void**) { return false; }
#endif  // __APPLE__ / __linux__

// Execute the probe code through `exec` under a SIGBUS/SIGSEGV guard. Returns
// true only if the fetch and both instructions completed.
bool FetchProbe(void* exec) {
    static sigjmp_buf probe_buf;

    struct sigaction sa {};
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = [](int, siginfo_t*, void*) {
        siglongjmp(probe_buf, 1);
    };
    struct sigaction oldbus {}, oldsegv {};
    sigaction(SIGBUS, &sa, &oldbus);
    sigaction(SIGSEGV, &sa, &oldsegv);

    volatile bool ok = false;
    if (sigsetjmp(probe_buf, 1) == 0) {
        using Fn = void (*)(void);
        reinterpret_cast<Fn>(exec)();
        ok = true;
    }

    sigaction(SIGBUS, &oldbus, nullptr);
    sigaction(SIGSEGV, &oldsegv, nullptr);
    return ok;
}

// Select the strategy by asking the kernel, once. Order: dual-map (no toggles,
// survives hardware-W^X regimes), toggle (official MAP_JIT pattern), legacy
// (anonymous RW -> RX; iOS 18-and-earlier behavior).
ExecMemMode ProbeMode(bool* fetchableOut) {
    *fetchableOut = false;
    ExecMemMode selected = ExecMemMode::kLegacy;

#if defined(__APPLE__)
#if KUDROID_EXECMEM_PROBE_EXEC
    {
        void* backing = nullptr;
        if (TryMapJit(&backing, kProbeSize)) {
            void* alias = nullptr;
            if (TryDualMap(backing, kProbeSize, &alias)) {
                std::memcpy(backing, kProbeCode, sizeof(kProbeCode));
                FlushIcache(alias, sizeof(kProbeCode));
                if (FetchProbe(alias)) {
                    selected = ExecMemMode::kDualMap;
                    *fetchableOut = true;
                }
                munmap(alias, kProbeSize);
            }
            munmap(backing, kProbeSize);
        }
    }
    if (!*fetchableOut) {
        void* backing = nullptr;
        if (TryMapJit(&backing, kProbeSize)) {
            EnableJitWrites(true);   // writes allowed on this thread
            std::memcpy(backing, kProbeCode, sizeof(kProbeCode));
            FlushIcache(backing, sizeof(kProbeCode));
            EnableJitWrites(false);  // fetch state
            if (FetchProbe(backing)) {
                selected = ExecMemMode::kToggle;
                *fetchableOut = true;
                EnableJitWrites(true);  // leave writable for callers
            }
            munmap(backing, kProbeSize);
        }
    }
#endif  // KUDROID_EXECMEM_PROBE_EXEC
#endif  // __APPLE__

    if (!*fetchableOut) {
        void* p = ::mmap(nullptr, kProbeSize, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p != MAP_FAILED) {
            std::memcpy(p, kProbeCode, sizeof(kProbeCode));
            if (mprotect(p, kProbeSize, PROT_READ | PROT_EXEC) == 0) {
#if KUDROID_EXECMEM_PROBE_EXEC
                FlushIcache(p, sizeof(kProbeCode));
                if (FetchProbe(p)) {
                    selected = ExecMemMode::kLegacy;
                    *fetchableOut = true;
                }
#else
                // Non-arm64 host: mprotect success is the best answer we can get.
                selected = ExecMemMode::kLegacy;
                *fetchableOut = true;
#endif
            }
            munmap(p, kProbeSize);
        }
    }

    return selected;
}

struct ProbeState {
    ExecMemMode mode = ExecMemMode::kLegacy;
    bool fetchable = false;
    bool initialized = false;
};

ProbeState& ProbeStateInstance() {
    static ProbeState state;
    if (!state.initialized) {
        state.mode = ProbeMode(&state.fetchable);
        state.initialized = true;
        std::fprintf(stderr, "[KuDroidExecMem] mode=%s fetchable=%d\n",
                     state.mode == ExecMemMode::kDualMap ? "dual-map"
                     : state.mode == ExecMemMode::kToggle ? "toggle" : "legacy",
                     state.fetchable ? 1 : 0);
    }
    return state;
}

size_t PageSize() {
    const long v = ::sysconf(_SC_PAGESIZE);
    return v > 0 ? static_cast<size_t>(v) : 4096u;
}

size_t RoundUp(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

}  // namespace

ExecMemMode ExecMemory::Mode() {
    return ProbeStateInstance().mode;
}

const char* ExecMemory::ModeName() {
    switch (ProbeStateInstance().mode) {
        case ExecMemMode::kDualMap: return "dual-map";
        case ExecMemMode::kToggle:  return "toggle";
        case ExecMemMode::kLegacy:  return "legacy";
    }
    return "legacy";
}

bool ExecMemory::IsFetchable() {
    return ProbeStateInstance().fetchable;
}

ExecMemory::Region ExecMemory::Allocate(size_t size, bool exec) {
    Region r;
    if (size == 0) return r;
    const ProbeState& st = ProbeStateInstance();
    if (exec && !st.fetchable) return r;

    const size_t rounded = RoundUp(size, PageSize());

#if defined(__APPLE__)
    if (exec && (st.mode == ExecMemMode::kDualMap || st.mode == ExecMemMode::kToggle)) {
        void* backing = nullptr;
        if (!TryMapJit(&backing, rounded)) return r;
        if (st.mode == ExecMemMode::kToggle) EnableJitWrites(true);
        if (st.mode == ExecMemMode::kDualMap) {
            void* alias = nullptr;
            if (!TryDualMap(backing, rounded, &alias)) {
                munmap(backing, rounded);
                if (st.mode == ExecMemMode::kToggle) EnableJitWrites(false);
                return r;
            }
            r.writeView = backing;
            r.execView = alias;
        } else {
            r.writeView = backing;
            r.execView = backing;
        }
        r.size = rounded;
        return r;
    }
#endif

    // Legacy (and non-exec scratch on every platform).
    void* p = ::mmap(nullptr, rounded, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return r;
    r.writeView = p;
    r.execView = p;
    r.size = rounded;
    return r;
}

void ExecMemory::Free(const Region& region) {
    if (region.writeView == nullptr || region.size == 0) return;
    if (region.execView != nullptr && region.execView != region.writeView) {
        munmap(region.execView, region.size);
    }
    munmap(region.writeView, region.size);
}

size_t ExecMemory::PageSize() {
    const long v = ::sysconf(_SC_PAGESIZE);
    return v > 0 ? static_cast<size_t>(v) : 4096u;
}

void ExecMemory::BeginWrite() {
#if defined(__APPLE__)
    if (ProbeStateInstance().mode == ExecMemMode::kToggle) {
        EnableJitWrites(true);
    }
#endif
}

void ExecMemory::EndWrite() {
#if defined(__APPLE__)
    if (ProbeStateInstance().mode == ExecMemMode::kToggle) {
        EnableJitWrites(false);
    }
#endif
}

ExecMemory::Region ExecMemory::Slice(const Region& parent, void* ptr, size_t size) {
    Region r;
    if (parent.writeView == nullptr || ptr == nullptr || size == 0) return r;
    const uintptr_t w = reinterpret_cast<uintptr_t>(parent.writeView);
    const uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
    if (p < w || p + size > w + parent.size) return r;
    r.writeView = ptr;
    r.execView = parent.execView != nullptr
        ? reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(parent.execView) +
                                  (p - w))
        : nullptr;
    r.size = size;
    return r;
}

void* ExecMemory::ExecPointer(const Region& region, const void* code) {
    if (region.writeView == nullptr || code == nullptr) return nullptr;
    const uintptr_t w = reinterpret_cast<uintptr_t>(region.writeView);
    const uintptr_t c = reinterpret_cast<uintptr_t>(code);
    if (c < w || c >= w + region.size) return nullptr;
    return region.execView != nullptr
        ? reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(region.execView) +
                                  (c - w))
        : nullptr;
}

ExecMemory::Region ExecMemory::AllocateBacking(size_t size) {
    Region r;
    if (size == 0) return r;
    const size_t rounded = RoundUp(size, PageSize());
    const ProbeState& st = ProbeStateInstance();
#if defined(__APPLE__)
    void* backing = nullptr;
    if (TryMapJit(&backing, rounded)) {
        r.writeView = backing;
        r.execView = backing;
        r.size = rounded;
        return r;
    }
#else
    (void)st;
#endif
    void* p = ::mmap(nullptr, rounded, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return r;
    r.writeView = p;
    r.execView = p;
    r.size = rounded;
    return r;
}

bool ExecMemory::RemapExecAlias(void* backing, size_t size, void** outAlias) {
#if defined(__APPLE__) || defined(__linux__)
    return TryDualMap(backing, size, outAlias);
#else
    (void)backing;
    (void)size;
    (void)outAlias;
    return false;
#endif
}

bool ExecMemory::Commit(const Region& region, void* code, size_t size) {
    if (region.writeView == nullptr || code == nullptr || size == 0) return false;
    const uintptr_t w = reinterpret_cast<uintptr_t>(region.writeView);
    const uintptr_t c = reinterpret_cast<uintptr_t>(code);
    if (c < w || c + size > w + region.size) return false;

    const ProbeState& st = ProbeStateInstance();

#if defined(__APPLE__)
    if (st.mode == ExecMemMode::kToggle) {
        // Seal: switch the thread to fetch state.
        EndWrite();
    }
#endif

    // kDualMap: both views were established at allocation; nothing to transition.
    // kLegacy: seal the code's pages RX (the image-loader path flips page ranges
    // itself and does not call this).
    if (st.mode == ExecMemMode::kLegacy) {
        if (mprotect(reinterpret_cast<void*>(c & ~(PageSize() - 1)),
                     RoundUp(size, PageSize()), PROT_READ | PROT_EXEC) != 0) {
            std::fprintf(stderr, "[KuDroidExecMem] mprotect(RX) failed: %s\n",
                         strerror(errno));
            return false;
        }
    }

    void* execCode = region.execView != nullptr
        ? reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(region.execView) +
                                  (c - w))
        : code;
    FlushIcache(execCode, size);
    return true;
}

}  // namespace kudroid
