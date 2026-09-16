#include "kudroid/ExecMemory.h"

#include <cerrno>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

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
#include <dlfcn.h>
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#include <sys/sysctl.h>
#include "kudroid/MachVmDecls.h"
#elif defined(__linux__)
#include <sys/syscall.h>
#endif

#if defined(__APPLE__)
// csops() is a private but stable API for code-signing status. Declared at global
// scope (the call sites use the `::`-qualified name).
extern "C" int csops(pid_t pid, unsigned int ops, void* useraddr, size_t usersize);
#endif

namespace kudroid {

namespace {

// One probe page is enough: the test function is two instructions.
constexpr size_t kProbeSize = 4096;

// Virtual reservation for the prepared exec arena. Only touched pages commit, so
// the size covers every guest exec prefix plus JIT code at no resident cost.
constexpr size_t kArenaSize = 256ull << 20;

// Defined below, used by the arena helpers above them.
size_t PageSize();
size_t RoundUp(size_t n, size_t align);

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
using JitProtectFn = void (*)(int);

// macOS declares pthread_jit_write_protect_np directly. iOS marks it
// unavailable in the SDK headers even though the symbol ships in libpthread,
// so resolve it at runtime and treat absence as "toggle mode cannot run".
JitProtectFn JitProtectFnInstance() {
#if TARGET_OS_OSX
    return &pthread_jit_write_protect_np;
#else
    static JitProtectFn fn =
        reinterpret_cast<JitProtectFn>(::dlsym(RTLD_DEFAULT,
                                               "pthread_jit_write_protect_np"));
    return fn;
#endif
}

bool ToggleWritesAvailable() { return JitProtectFnInstance() != nullptr; }

// Thread-local JIT write-enable; ISB per the platform's toggle contract.
void EnableJitWrites(bool enable) {
    if (JitProtectFn fn = JitProtectFnInstance()) {
        fn(enable ? 0 : 1);
        asm volatile("isb sy" ::: "memory");
    }
}

// Alias the same backing pages at a second virtual address with RX protection.
// Writes through the RW view are visible to fetches through the RX view; no
// runtime permission transition is ever performed.
bool TryDualMap(void* backing, size_t size, void** outAlias) {
    mach_vm_address_t alias = 0;
    vm_prot_t curProt = VM_PROT_NONE, maxProt = VM_PROT_NONE;
    kern_return_t kr = mach_vm_remap(mach_task_self(), &alias,
                                     size, 0, VM_FLAGS_ANYWHERE,
                                     mach_task_self(),
                                     reinterpret_cast<mach_vm_address_t>(backing),
                                     FALSE, &curProt, &maxProt,
                                     VM_INHERIT_DEFAULT);
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

// ── iOS 26+ TXM/SPTM breakpoint protocol ────────────────────────────────────
//
// On the hardware-monitored W^X regimes (iOS 26+, A13+/M1+) a debugger attach is
// not enough: every executable region must be "prepared" over the debug
// connection before any instruction is fetched from it. StikDebug attaches with
// the universal script, which services these traps (x16 selects the command):
//
//   mov x16,#1 ; brk #0xf00d   prepare [x0, x0+x1) RX; return the same x0
//   mov x16,#0 ; brk #0xf00d   detach (the region set is final)
//
// The script does NOT allocate when x0 is non-null — it only *prepares* the
// mapping it is handed (x0==0 asks the server to allocate one, but then the
// server owns the pages and our writes would not alias them). So the engine must
// create the RX alias itself (mach_vm_remap of the MAP_JIT backing) and pass it
// here for preparation. Success is then proven by fetching through the returned
// address, not by the trap returning.
//
// The debugger owns the stop while attached, so the process never sees these
// traps. An attach WITHOUT the script delivers them to us as SIGTRAP; see
// bionic_handle_jit26_trap, which steps over them so such a run degrades instead
// of dying on a breakpoint it never intended to service.
extern "C" __attribute__((naked, noinline)) void* kudroid_jit26_prepare(void*, size_t) {
    __asm__ volatile(
        "mov x16, #1\n"
        "brk #0xf00d\n"
        "ret\n");
}

extern "C" __attribute__((naked, noinline)) void kudroid_jit26_detach(void) {
    __asm__ volatile(
        "mov x16, #0\n"
        "brk #0xf00d\n"
        "ret\n");
}

// Hand `alias` (an existing RX mapping aliasing writable backing pages) to the
// debug script for preparation. Returns false only when the call clearly did not
// prepare (wrong address back, or no script) — the caller still has to fetch-probe.
bool PrepareExecRegion(void* alias, size_t size) {
    if (alias == nullptr || size == 0) return false;
    void* got = kudroid_jit26_prepare(alias, size);
    return got == alias;
}

void DetachJitScript() { kudroid_jit26_detach(); }
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

// ── Prepared exec arena (iOS 26+ TXM/SPTM) ──────────────────────────────────
//
// One RW backing aliased RX, prepared over the debug connection once and then
// detached. Callers carve writable/executable sub-regions from it, so no exec
// region is ever created after the detach. Present only where prepare works; the
// accessors no-op elsewhere so the rest of the file compiles unchanged.
struct PreparedArena {
    void* backing = nullptr;  // RW view
    void* alias = nullptr;    // RX view, prepared
    size_t size = 0;
    size_t used = 0;
    bool active = false;
};

#if defined(__APPLE__)
PreparedArena& ArenaState() {
    static PreparedArena arena;
    return arena;
}

bool ArenaCarve(size_t size, void** outWrite, void** outExec, size_t* outSize) {
    PreparedArena& a = ArenaState();
    if (!a.active) return false;
    const size_t rounded = RoundUp(size, PageSize());
    if (a.used + rounded > a.size) return false;
    *outWrite = static_cast<char*>(a.backing) + a.used;
    *outExec = static_cast<char*>(a.alias) + a.used;
    *outSize = rounded;
    a.used += rounded;
    return true;
}

bool ArenaContains(const void* p) {
    const PreparedArena& a = ArenaState();
    if (!a.active || p == nullptr) return false;
    const char* c = static_cast<const char*>(p);
    return c >= static_cast<const char*>(a.backing) &&
           c < static_cast<const char*>(a.backing) + a.size;
}

#else
bool ArenaCarve(size_t, void**, void**, size_t*) { return false; }
bool ArenaContains(const void*) { return false; }
#endif  // __APPLE__

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

// ── Diagnostics (no behavior change) ────────────────────────────────────────
// The TXM/SPTM regime on iOS 26+ (A13+/M1+) rejects fetches from exec memory a
// debug script has not prepared, so the capability probe legitimately fails there
// no matter what attached. Report the environment so a run can tell "probe failed
// on a TXM device" from "probe never ran / permission never arrived".

#if defined(__APPLE__)
std::string SysctlString(const char* name) {
    size_t size = 0;
    if (::sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) return {};
    std::string value(size, '\0');
    if (::sysctlbyname(name, value.data(), &size, nullptr, 0) != 0) return {};
    if (!value.empty() && value.back() == '\0') value.pop_back();
    return value;
}

int OsMajorVersion() {
    const std::string v = SysctlString("kern.osproductversion");
    int major = 0;
    std::sscanf(v.c_str(), "%d", &major);
    return major;
}

// A13+ / Apple-silicon SoCs carry the TXM monitor; older ones do not, and there a
// debugger attach alone still enables JIT. Device identifiers: iPhone12,x is A13,
// iPad12,x is A13. The Simulator enforces no such regime.
bool TxmEstimate() {
#if TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR
    if (OsMajorVersion() < 26) return false;
    const std::string mach = SysctlString("hw.machine");
    int n = 0;
    if (mach.rfind("iPhone", 0) == 0) {
        std::sscanf(mach.c_str() + 6, "%d", &n);
        return n >= 12;
    }
    if (mach.rfind("iPad", 0) == 0) {
        std::sscanf(mach.c_str() + 4, "%d", &n);
        return n >= 12;
    }
    // Macs running the same regime are all Apple silicon (M1+).
    return mach.rfind("Mac", 0) == 0;
#else
    return false;
#endif
}

bool CsDebugged() {
    constexpr unsigned int kStatus = 0;             // CS_OPS_STATUS
    constexpr unsigned int kDebugged = 0x10000000;  // CS_DEBUGGED
    unsigned int flags = 0;
    return ::csops(::getpid(), kStatus, &flags, sizeof(flags)) == 0 &&
           (flags & kDebugged) != 0;
}

const char* RuntimeSummaryInner() {
    static const std::string s = [] {
        std::string v = SysctlString("kern.osproductversion");
        std::string m = SysctlString("hw.machine");
        return "ios=" + (v.empty() ? std::string("?") : v) +
               " machine=" + (m.empty() ? std::string("?") : m) +
               " txm~=" + (TxmEstimate() ? "1" : "0");
    }();
    return s.c_str();
}
#else
bool TxmEstimate() { return false; }
bool CsDebugged() { return false; }
const char* RuntimeSummaryInner() { return "ios=n/a machine=n/a txm~=0"; }
#endif  // __APPLE__

#if defined(__APPLE__)
// Current/max protection of the VM region holding `addr`. Executes nothing, so
// unlike FetchProbe it cannot kill the process on a TXM device (where a refused
// execute fetch is a codesigning SIGKILL, not a catchable fault).
bool RegionProt(const void* addr, vm_prot_t* cur, vm_prot_t* max) {
    if (addr == nullptr) return false;
    mach_vm_address_t a = reinterpret_cast<mach_vm_address_t>(addr);
    mach_vm_size_t sz = 0;
    kudroid_vm_region_basic_info_64_t info{};
    mach_msg_type_number_t count = KUDROID_VM_REGION_BASIC_INFO_64_COUNT;
    mach_port_t obj = MACH_PORT_NULL;
    const kern_return_t kr = mach_vm_region(
        mach_task_self(), &a, &sz, KUDROID_VM_REGION_BASIC_INFO_64_FLAVOR,
        reinterpret_cast<vm_region_info_t>(&info), &count, &obj);
    if (obj != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), obj);
    if (kr != KERN_SUCCESS) return false;
    if (cur) *cur = info.protection;
    if (max) *max = info.max_protection;
    return true;
}

const char* ProtStr(vm_prot_t p) {
    static thread_local char b[4];
    b[0] = (p & VM_PROT_READ) ? 'r' : '-';
    b[1] = (p & VM_PROT_WRITE) ? 'w' : '-';
    b[2] = (p & VM_PROT_EXECUTE) ? 'x' : '-';
    b[3] = '\0';
    return b;
}

// Print a mapping's current/max protection. `ok` is false when the region query
// itself failed, which the survey reports separately.
void PrintProt(const char* what, const void* addr) {
    vm_prot_t cur = 0, max = 0;
    if (RegionProt(addr, &cur, &max)) {
        std::fprintf(stderr, "[KuDroidExecMem] %s: cur=%s max=%s\n",
                     what, ProtStr(cur), ProtStr(max));
    } else {
        std::fprintf(stderr, "[KuDroidExecMem] %s: region query failed\n", what);
    }
}

// True when the region's MAX protection includes execute — the honest "this page
// can ever be fetched" answer. A succeeded mprotect does not imply it (it can be
// silently clamped), but max_protection cannot lie.
bool RegionMaxExec(const void* addr) {
    vm_prot_t max = 0;
    return RegionProt(addr, nullptr, &max) && (max & VM_PROT_EXECUTE) != 0;
}

// One-shot survey of what this device lets us allocate, executing nothing. The
// result decides how the loader maps guest code under TXM. Self-checks the region
// layout against our own executable text first, so bogus numbers are never read
// as real.
void RunCapabilitySurvey() {
    static std::once_flag once;
    std::call_once(once, [] {
        {
            vm_prot_t cur = 0, max = 0;
            const bool ok = RegionProt(
                reinterpret_cast<const void*>(&RunCapabilitySurvey), &cur, &max);
            std::fprintf(stderr,
                         "[KuDroidExecMem] survey self-check %s own-text cur=%s max=%s\n",
                         (ok && (max & VM_PROT_EXECUTE)) ? "ok" : "FAILED",
                         ok ? ProtStr(cur) : "?", ok ? ProtStr(max) : "?");
        }
        const size_t ps = PageSize();
        struct Cand { const char* name; int prot; };
        const Cand cands[] = {
            {"anon-rw", PROT_READ | PROT_WRITE},
            {"anon-rwx", PROT_READ | PROT_WRITE | PROT_EXEC},
            {"anon-rx", PROT_READ | PROT_EXEC},
        };
        for (const Cand& c : cands) {
            void* p = ::mmap(nullptr, ps, c.prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED) {
                std::fprintf(stderr, "[KuDroidExecMem] survey %s: mmap failed (%s)\n",
                             c.name, std::strerror(errno));
                continue;
            }
            PrintProt(c.name, p);
            ::munmap(p, ps);
        }
        {
            void* p = ::mmap(nullptr, ps, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p != MAP_FAILED) {
                const int mr = ::mprotect(p, ps, PROT_READ | PROT_EXEC);
                std::fprintf(stderr, "[KuDroidExecMem] survey rw->mprotect(rx): ret=%d\n", mr);
                PrintProt("rw->mprotect(rx)", p);
                ::munmap(p, ps);
            }
        }
        {
            void* p = ::mmap(nullptr, ps, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
            if (p == MAP_FAILED) {
                std::fprintf(stderr, "[KuDroidExecMem] survey map-jit: mmap failed (%s)\n",
                             std::strerror(errno));
            } else {
                PrintProt("map-jit", p);
                ::munmap(p, ps);
            }
        }
    });
}
#endif  // __APPLE__

#if defined(__APPLE__)
// Build and prepare the exec arena. Readiness is the prepare handshake plus the
// region's MAX protection including execute — never a probe fetch, which on TXM
// is a codesigning kill rather than a catchable fault.
bool CreatePreparedArena(size_t size) {
    PreparedArena& a = ArenaState();
    if (a.active) return true;
    if (!(TxmEstimate() && CsDebugged())) return false;

    // Ask for execute at ALLOCATION time: max protection is fixed then, and a
    // region allocated without PROT_EXEC can never be made executable later
    // (mprotect then clamps silently, which is what made the old arena's alias
    // r--/rw-). Fall back to RW only if the kernel refuses the request outright.
    void* backing = ::mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const char* route = "rwx";
    if (backing == MAP_FAILED) {
        backing = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        route = "rw";
    }
    if (backing == MAP_FAILED) {
        std::fprintf(stderr, "[KuDroidExecMem] arena: backing mmap failed: %s\n",
                     std::strerror(errno));
        return false;
    }
    PrintProt("arena backing", backing);

    void* alias = nullptr;
    if (!TryDualMap(backing, size, &alias)) {
        std::fprintf(stderr, "[KuDroidExecMem] arena: rx alias failed\n");
        ::munmap(backing, size);
        return false;
    }
    PrintProt("arena alias", alias);

    if (!PrepareExecRegion(alias, size)) {
        std::fprintf(stderr, "[KuDroidExecMem] arena: prepare failed\n");
        ::munmap(alias, size);
        ::munmap(backing, size);
        return false;
    }
    PrintProt("arena alias after prepare", alias);

    if (!RegionMaxExec(alias)) {
        std::fprintf(stderr,
                     "[KuDroidExecMem] arena: prepare ok but max protection has no "
                     "execute (route=%s); this device cannot fetch here\n", route);
        ::munmap(alias, size);
        ::munmap(backing, size);
        return false;
    }

    // The region set is final: nothing executable is created after this point.
    DetachJitScript();

    // Decisive for the loader, and execution-free: does an ADDITIONAL alias of the
    // prepared pages report execute in its max protection? If yes, a per-image
    // span can remap an arena slice into its exec prefix and prepare is paid once.
    // If no, every image alias must be prepared while the script is attached.
    {
        void* second = nullptr;
        if (TryDualMap(alias, PageSize(), &second)) {
            const bool follows = RegionMaxExec(second);
            PrintProt("arena prepared-ness follows alias", second);
            std::fprintf(stderr, "[KuDroidExecMem] prepared-ness follows alias=%d\n",
                         follows ? 1 : 0);
            ::munmap(second, PageSize());
        } else {
            std::fprintf(stderr, "[KuDroidExecMem] prepared-ness follows alias=n/a\n");
        }
    }

    a.backing = backing;
    a.alias = alias;
    a.size = size;
    a.used = 0;
    a.active = true;
    std::fprintf(stderr, "[KuDroidExecMem] prepared arena %zu MiB ready (route=%s)\n",
                 size >> 20, route);
    return true;
}
#endif  // __APPLE__

// Select the strategy by asking the kernel, once. Order: dual-map (no toggles,
// survives hardware-W^X regimes), toggle (official MAP_JIT pattern), legacy
// (anonymous RW -> RX; iOS 18-and-earlier behavior).
ExecMemMode ProbeMode(bool* fetchableOut) {
    *fetchableOut = false;
    ExecMemMode selected = ExecMemMode::kLegacy;

#if defined(__APPLE__)
    // TXM/SPTM (iOS 26+): a debugger attach is not enough; the region must be
    // prepared over the debug connection. The arena does that and is the only
    // path that can fetch there, so try it before the attach-only strategies.
    if (ExecMemory::TxmScriptReady() && CreatePreparedArena(kArenaSize)) {
        *fetchableOut = true;
        return ExecMemMode::kPrepared;
    }
    if (TxmEstimate()) {
        // Every strategy below proves itself by executing written code. On TXM a
        // refused fetch is a codesigning SIGKILL, so none of them may run — the
        // survey reports what the kernel allows without executing anything, and
        // the arena (above) is the only path that can succeed here.
        RunCapabilitySurvey();
        std::fprintf(stderr,
                     "[KuDroidExecMem] txm: no prepared arena; probe strategies "
                     "skipped (executing to test would kill the process)\n");
        return ExecMemMode::kLegacy;
    }
#endif
#if defined(__APPLE__)
#if KUDROID_EXECMEM_PROBE_EXEC
    {
        void* backing = nullptr;
        const bool mapJit = TryMapJit(&backing, kProbeSize);
        void* alias = nullptr;
        bool remap = false, fetch = false;
        if (mapJit) {
            remap = TryDualMap(backing, kProbeSize, &alias);
            if (remap) {
                std::memcpy(backing, kProbeCode, sizeof(kProbeCode));
                FlushIcache(alias, sizeof(kProbeCode));
                fetch = FetchProbe(alias);
                if (fetch) {
                    selected = ExecMemMode::kDualMap;
                    *fetchableOut = true;
                }
                munmap(alias, kProbeSize);
            }
            munmap(backing, kProbeSize);
        }
        if (!*fetchableOut) {
            std::fprintf(stderr, "[KuDroidExecMem] probe dual-map: map_jit=%d remap=%d fetch=%d\n",
                         mapJit ? 1 : 0, remap ? 1 : 0, fetch ? 1 : 0);
        }
    }
    if (!*fetchableOut) {
        void* backing = nullptr;
        const bool haveFn = ToggleWritesAvailable();
        const bool mapJit = haveFn && TryMapJit(&backing, kProbeSize);
        bool fetch = false;
        if (mapJit) {
            EnableJitWrites(true);   // writes allowed on this thread
            std::memcpy(backing, kProbeCode, sizeof(kProbeCode));
            FlushIcache(backing, sizeof(kProbeCode));
            EnableJitWrites(false);  // fetch state
            fetch = FetchProbe(backing);
            if (fetch) {
                selected = ExecMemMode::kToggle;
                *fetchableOut = true;
                EnableJitWrites(true);  // leave writable for callers
            }
            munmap(backing, kProbeSize);
        }
        if (!*fetchableOut) {
            std::fprintf(stderr, "[KuDroidExecMem] probe toggle: func=%d map_jit=%d fetch=%d\n",
                         haveFn ? 1 : 0, mapJit ? 1 : 0, fetch ? 1 : 0);
        }
    }
#endif  // KUDROID_EXECMEM_PROBE_EXEC
#endif  // __APPLE__

    if (!*fetchableOut) {
        void* p = ::mmap(nullptr, kProbeSize, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        bool mprot = false, fetch = false;
        if (p != MAP_FAILED) {
            std::memcpy(p, kProbeCode, sizeof(kProbeCode));
            if (mprotect(p, kProbeSize, PROT_READ | PROT_EXEC) == 0) {
                mprot = true;
#if KUDROID_EXECMEM_PROBE_EXEC
                FlushIcache(p, sizeof(kProbeCode));
                if (FetchProbe(p)) {
                    selected = ExecMemMode::kLegacy;
                    *fetchableOut = true;
                    fetch = true;
                }
#else
                // Non-arm64 host: mprotect success is the best answer we can get.
                selected = ExecMemMode::kLegacy;
                *fetchableOut = true;
                fetch = true;
#endif
            }
            munmap(p, kProbeSize);
        }
        if (!*fetchableOut) {
            std::fprintf(stderr, "[KuDroidExecMem] probe legacy: mprotect=%d fetch=%d\n",
                         mprot ? 1 : 0, fetch ? 1 : 0);
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
        std::fprintf(stderr, "[KuDroidExecMem] probe %s cs_debugged=%d\n",
                     RuntimeSummaryInner(), CsDebugged() ? 1 : 0);
        state.mode = ProbeMode(&state.fetchable);
        state.initialized = true;
        std::fprintf(stderr, "[KuDroidExecMem] mode=%s fetchable=%d %s\n",
                     state.mode == ExecMemMode::kDualMap ? "dual-map"
                     : state.mode == ExecMemMode::kToggle ? "toggle"
                     : state.mode == ExecMemMode::kPrepared ? "prepared" : "legacy",
                     state.fetchable ? 1 : 0, RuntimeSummaryInner());
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
        case ExecMemMode::kPrepared: return "prepared";
        case ExecMemMode::kLegacy:  return "legacy";
    }
    return "legacy";
}

bool ExecMemory::IsFetchable() {
    return ProbeStateInstance().fetchable;
}

bool ExecMemory::Reprobe() {
    ProbeState& st = ProbeStateInstance();
    if (st.fetchable) return true;  // never downgrade a positive result
    // A debugger attaching mid-run grants JIT permissions the first probe
    // legitimately lacked. Reset and ask the kernel again; the cached mode is
    // recomputed with it.
    st.initialized = false;
    const ProbeState& fresh = ProbeStateInstance();
    return fresh.fetchable;
}

bool ExecMemory::TxmPresent() {
    return TxmEstimate();
}

const char* ExecMemory::RuntimeSummary() {
    return RuntimeSummaryInner();
}

bool ExecMemory::TxmScriptReady() {
#if defined(__APPLE__)
    return TxmEstimate() && CsDebugged();
#else
    return false;
#endif
}

bool ExecMemory::PrepareRegion(void* alias, size_t size) {
#if defined(__APPLE__)
    return PrepareExecRegion(alias, size);
#else
    (void)alias;
    (void)size;
    return false;
#endif
}

void ExecMemory::DetachScript() {
#if defined(__APPLE__)
    DetachJitScript();
#endif
}

ExecMemory::Region ExecMemory::Allocate(size_t size, bool exec) {
    Region r;
    if (size == 0) return r;
    const ProbeState& st = ProbeStateInstance();
    if (exec && !st.fetchable) return r;

    const size_t rounded = RoundUp(size, PageSize());

#if defined(__APPLE__)
    if (exec && st.mode == ExecMemMode::kPrepared) {
        void* w = nullptr;
        void* e = nullptr;
        size_t s = 0;
        if (ArenaCarve(rounded, &w, &e, &s)) {
            r.writeView = w;
            r.execView = e;
            r.size = s;
        }
        return r;  // arena lifetime is process-long; no per-region mapping
    }
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
    // Arena sub-regions share one mapping; releasing one would break the rest.
    if (ArenaContains(region.writeView)) return;
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
    if (st.mode == ExecMemMode::kPrepared) {
        void* w = nullptr;
        void* e = nullptr;
        size_t s = 0;
        if (ArenaCarve(rounded, &w, &e, &s)) {
            r.writeView = w;
            r.execView = e;
            r.size = s;
        }
        return r;
    }
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
#if defined(__APPLE__)
    // Arena sub-regions are already aliased and prepared: the alias is the same
    // offset into the arena's RX view.
    if (ProbeStateInstance().mode == ExecMemMode::kPrepared &&
        ArenaContains(backing) && outAlias != nullptr) {
        const PreparedArena& a = ArenaState();
        *outAlias = static_cast<char*>(a.alias) +
                    (static_cast<char*>(backing) - static_cast<char*>(a.backing));
        return true;
    }
#endif
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
