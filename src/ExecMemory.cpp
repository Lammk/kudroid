#include "kudroid/ExecMemory.h"

#include <atomic>
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
constexpr size_t kArenaSize = 512ull << 20;
constexpr size_t kGuestArenaSize = 64ull << 20;

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

// Ask the debug script to allocate the executable region (x0=0 -> server
// `_M<size>,rx`). On iOS 26/27 only memory the debug server allocates is actually
// fetchable — an engine-created mapping is refused at fetch even when prepared, so
// the engine must not allocate the exec side itself. Returns the RX address, or
// nullptr when no script is attached.
void* PrepareRegionAlloc(size_t size) {
    return kudroid_jit26_prepare(nullptr, size);
}

// Writable alias of `src` sharing its physical pages at a new virtual address.
// The engine owns this half of W^X: writes land here, the server RX mapping
// fetches them. mach_vm_protect only lowers, and the alias' max is rw-, so RW is
// always within reach.
bool RemapWritable(void* src, size_t size, void** outRW) {
    mach_vm_address_t addr = 0;
    vm_prot_t cur = VM_PROT_NONE, max = VM_PROT_NONE;
    kern_return_t kr = mach_vm_remap(mach_task_self(), &addr, size, 0,
                                     VM_FLAGS_ANYWHERE, mach_task_self(),
                                     reinterpret_cast<mach_vm_address_t>(src),
                                     FALSE, &cur, &max, VM_INHERIT_DEFAULT);
    if (kr != KERN_SUCCESS) return false;
    kr = mach_vm_protect(mach_task_self(), addr, size, FALSE,
                         VM_PROT_READ | VM_PROT_WRITE);
    if (kr != KERN_SUCCESS) {
        mach_vm_deallocate(mach_task_self(), addr, size);
        return false;
    }
    *outRW = reinterpret_cast<void*>(static_cast<uintptr_t>(addr));
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
    std::atomic<size_t> used{0};
    bool active = false;
};

#if defined(__APPLE__)
PreparedArena& ArenaState() {
    static PreparedArena arena;
    return arena;
}

struct GuestArena {
    void* write = nullptr;
    void* address = nullptr;
    size_t size = 0;
    std::atomic<size_t> used{0};
    std::atomic<bool> active{false};
};

GuestArena& GuestArenaState() {
    static GuestArena arena;
    return arena;
}

struct GuestTransitionRecord {
    const char* phase = "before-detach";
    int prot = -1;
    int result = 0;
    int error = 0;
    vm_prot_t beforeCurrent = 0;
    vm_prot_t beforeMaximum = 0;
    vm_prot_t current = 0;
    vm_prot_t maximum = 0;
    bool valid = false;
};

struct GuestProbeStatus : GuestTransitionRecord {
    const char* stage = "not-probed";
    GuestTransitionRecord transitions[8]{};
    size_t transitionCount = 0;
};

GuestProbeStatus& LastGuestProbe() {
    static GuestProbeStatus status;
    return status;
}

bool ClaimArenaBytes(std::atomic<size_t>& used, size_t capacity, size_t size,
                     size_t* offset) {
    size_t current = used.load(std::memory_order_relaxed);
    do {
        if (current > capacity || size > capacity - current) return false;
    } while (!used.compare_exchange_weak(current, current + size,
                                         std::memory_order_relaxed));
    *offset = current;
    return true;
}

bool ArenaCarve(size_t size, void** outWrite, void** outExec, size_t* outSize) {
    PreparedArena& a = ArenaState();
    if (!a.active) return false;
    if (size == 0 || size > SIZE_MAX - (PageSize() - 1)) return false;
    const size_t rounded = RoundUp(size, PageSize());
    size_t offset = 0;
    if (!ClaimArenaBytes(a.used, a.size, rounded, &offset)) return false;
    *outWrite = static_cast<char*>(a.backing) + offset;
    *outExec = static_cast<char*>(a.alias) + offset;
    *outSize = rounded;
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
    // mach_vm_region answers a gap address with the NEXT region: require
    // containment, or an unmapped address reads as its successor's mapping.
    // Subtraction form, so a wrapping base+size cannot pass.
    {
        const uint64_t base = static_cast<uint64_t>(a);
        const uint64_t size = static_cast<uint64_t>(sz);
        const uint64_t fault = reinterpret_cast<uint64_t>(addr);
        if (fault < base || fault - base >= size) return false;
    }
    if (cur) *cur = info.protection;
    if (max) *max = info.max_protection;
    return true;
}

void ProtStr(vm_prot_t p, char out[4]) {
    out[0] = (p & VM_PROT_READ) ? 'r' : '-';
    out[1] = (p & VM_PROT_WRITE) ? 'w' : '-';
    out[2] = (p & VM_PROT_EXECUTE) ? 'x' : '-';
    out[3] = '\0';
}

// Print a mapping's current/max protection. Two buffers: ProtStr into one
// thread-local slot twice would let both %s read the second value.
void PrintProt(const char* what, const void* addr) {
    vm_prot_t cur = 0, max = 0;
    if (RegionProt(addr, &cur, &max)) {
        char cs[4], ms[4];
        ProtStr(cur, cs);
        ProtStr(max, ms);
        std::fprintf(stderr, "[KuDroidExecMem] %s: cur=%s max=%s\n", what, cs, ms);
    } else {
        std::fprintf(stderr, "[KuDroidExecMem] %s: region query failed\n", what);
    }
    std::fflush(stderr);
}

// True when the region's MAX protection includes execute — the honest "this page
// can ever be fetched" answer. A succeeded mprotect does not imply it (it can be
// silently clamped), but max_protection cannot lie.
[[maybe_unused]] bool RegionMaxExec(const void* addr) {
    vm_prot_t max = 0;
    return RegionProt(addr, nullptr, &max) && (max & VM_PROT_EXECUTE) != 0;
}

void PrintGuestTransition(const GuestTransitionRecord& record) {
    std::fprintf(stderr,
                 "[KuDroidExecMem] guest transition phase=%s prot=0x%x ret=%d errno=%d before_cur=0x%x before_max=0x%x cur=0x%x max=0x%x valid=%d\n",
                 record.phase, record.prot, record.result, record.error,
                 record.beforeCurrent, record.beforeMaximum,
                 record.current, record.maximum, record.valid ? 1 : 0);
}

bool GuestTransition(void* address, int prot, const char* phase = "before-detach") {
    GuestTransitionRecord record;
    record.phase = phase;
    record.prot = prot;
    const bool queriedBefore = RegionProt(address, &record.beforeCurrent,
                                         &record.beforeMaximum);
    vm_prot_t current = 0, maximum = 0;
    const int result = ::mprotect(address, PageSize(), prot);
    const int failure = result == 0 ? 0 : errno;
    const bool queried = RegionProt(address, &current, &maximum);
    record.result = result;
    record.error = failure;
    record.current = current;
    record.maximum = maximum;
    // Every step must retain the capability for the next RW/RX transition.
    record.valid = result == 0 && queriedBefore && queried &&
                   (current & VM_PROT_ALL) == prot &&
                   (record.beforeMaximum & VM_PROT_ALL) == VM_PROT_ALL &&
                   (maximum & VM_PROT_ALL) == VM_PROT_ALL;
    GuestProbeStatus& status = LastGuestProbe();
    static_cast<GuestTransitionRecord&>(status) = record;
    if (status.transitionCount < sizeof(status.transitions) / sizeof(status.transitions[0])) {
        status.transitions[status.transitionCount++] = record;
    }
    PrintGuestTransition(record);
    return record.valid;
}

// One-shot survey of what this device lets us allocate, executing nothing. The
// result decides how the loader maps guest code under TXM. Self-checks the region
// layout against our own executable text first, so bogus numbers are never read
// as real.
void RunCapabilitySurvey() {
    static bool done = false;
    if (done) return;
    done = true;
    {
        vm_prot_t cur = 0, max = 0;
        const bool ok = RegionProt(
            reinterpret_cast<const void*>(&RunCapabilitySurvey), &cur, &max);
        char cs[4] = "?", ms[4] = "?";
        if (ok) { ProtStr(cur, cs); ProtStr(max, ms); }
        std::fprintf(stderr,
                     "[KuDroidExecMem] survey self-check %s own-text cur=%s max=%s\n",
                     (ok && (max & VM_PROT_EXECUTE)) ? "ok" : "FAILED", cs, ms);
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
    std::fflush(stderr);
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
    GuestProbeStatus& status = LastGuestProbe();
    status = GuestProbeStatus{};
    status.stage = "server-allocation";

    // Let the debug script allocate the executable region. An engine-created
    // mapping — even one prepared over the connection — is still refused at fetch
    // on iOS 26/27 (device: SIGBUS on the first instruction of an arena image), so
    // the exec side must be server-allocated. The engine owns the writable alias.
    void* rx = PrepareRegionAlloc(size);
    if (!rx) {
        std::fprintf(stderr, "[KuDroidExecMem] arena: server rx alloc failed "
                             "(no script?)\n");
        DetachJitScript();  // never leave the script attached
        return false;
    }
    PrintProt("arena exec (server rx)", rx);

    status.stage = "write-alias";
    void* rw = nullptr;
    if (!RemapWritable(rx, size, &rw)) {
        std::fprintf(stderr, "[KuDroidExecMem] arena: rw alias of exec region failed\n");
        DetachJitScript();
        return false;
    }
    PrintProt("arena write alias", rw);
    status.stage = "mapping-permissions";
    vm_prot_t writeCurrent = 0, writeMaximum = 0;
    vm_prot_t execCurrent = 0, execMaximum = 0;
    const vm_prot_t rwProt = VM_PROT_READ | VM_PROT_WRITE;
    const vm_prot_t rxProt = VM_PROT_READ | VM_PROT_EXECUTE;
    if (!RegionProt(rw, &writeCurrent, &writeMaximum) ||
        !RegionProt(rx, &execCurrent, &execMaximum) ||
        (writeCurrent & rwProt) != rwProt || (writeMaximum & rwProt) != rwProt ||
        (execCurrent & rxProt) != rxProt || (execMaximum & rxProt) != rxProt) {
        std::fprintf(stderr, "[KuDroidExecMem] arena: required permissions unavailable\n");
        DetachJitScript();
        ::munmap(rw, size);
        ::munmap(rx, size);
        return false;
    }

    // Validate transitions on an unused page before exposing guest addresses.
    const size_t guestSize = size > kGuestArenaSize ? kGuestArenaSize : 0;
    void* guestAddress = static_cast<char*>(rx) + size - guestSize;
    void* probeAddress = static_cast<char*>(rx) + size - PageSize();
    auto* probeWrite = reinterpret_cast<volatile uint32_t*>(
        static_cast<char*>(rw) + size - PageSize());
    bool stable = guestSize != 0;
    status.stage = "protection-transition";
    const int transitions[] = {PROT_NONE, PROT_READ | PROT_WRITE,
                               PROT_READ | PROT_EXEC, PROT_READ | PROT_WRITE,
                               PROT_READ | PROT_EXEC};
    for (int prot : transitions) {
        if (!stable || !GuestTransition(probeAddress, prot)) {
            stable = false;
            break;
        }
        if (prot & PROT_WRITE) {
            auto* guestWrite = reinterpret_cast<volatile uint32_t*>(probeAddress);
            *guestWrite = 0x13579bdfu;
            if (*probeWrite != 0x13579bdfu) {
                status.stage = "write-sharing";
                stable = false;
                break;
            }
            *probeWrite = 0x2468ace0u;
            if (*guestWrite != 0x2468ace0u) {
                status.stage = "write-sharing";
                stable = false;
                break;
            }
        } else if ((prot & PROT_EXEC) &&
                   *reinterpret_cast<volatile uint32_t*>(probeAddress) != 0x2468ace0u) {
            stable = false;
            status.stage = "write-sharing";
            break;
        }
    }

    // The region set is final: nothing executable is created after this point.
    DetachJitScript();

    if (stable) {
        status.stage = "post-detach-transition";
        stable = GuestTransition(probeAddress, PROT_READ | PROT_WRITE, "after-detach") &&
                 GuestTransition(probeAddress, PROT_READ | PROT_EXEC, "after-detach");
    }
    const bool protectGuestPool = stable;
    if (stable && ::mprotect(guestAddress, guestSize, PROT_NONE) != 0) {
        status.stage = "pool-protection";
        stable = false;
    }

    a.backing = rw;   // writes go here
    a.alias = rx;     // fetches go here
    a.size = size - (protectGuestPool ? guestSize : PageSize());
    a.used = 0;
    a.active = true;
    if (stable) {
        status.stage = "ready";
        GuestArena& guest = GuestArenaState();
        guest.write = static_cast<char*>(rw) + a.size;
        guest.address = guestAddress;
        guest.size = guestSize;
        guest.active.store(true, std::memory_order_release);
    }
    std::fprintf(stderr, "[KuDroidExecMem] guest same-address arena=%s size=%zu MiB\n",
                 stable ? "ready" : "unsupported", stable ? guestSize >> 20 : 0);
    std::fprintf(stderr,
                 "[KuDroidExecMem] prepared arena %zu MiB ready (server rx + rw alias)\n",
                 size >> 20);
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
    std::atomic<ExecMemMode> mode{ExecMemMode::kLegacy};
    std::atomic<bool> fetchable{false};
    std::atomic<bool> initialized{false};
};

// The probe swaps process-wide fault handlers and uses one jump buffer, so only
// one thread may run it at a time.
std::recursive_mutex& ProbeMutex() {
    static std::recursive_mutex mtx;
    return mtx;
}

// Caller holds ProbeMutex().
void RunProbeLocked(ProbeState& state) {
    std::fprintf(stderr, "[KuDroidExecMem] probe %s cs_debugged=%d\n",
                 RuntimeSummaryInner(), CsDebugged() ? 1 : 0);
    bool fetchable = false;
    const ExecMemMode mode = ProbeMode(&fetchable);
    state.mode.store(mode, std::memory_order_release);
    state.fetchable.store(fetchable, std::memory_order_release);
    state.initialized.store(true, std::memory_order_release);
    std::fprintf(stderr, "[KuDroidExecMem] mode=%s fetchable=%d %s\n",
                 mode == ExecMemMode::kDualMap ? "dual-map"
                 : mode == ExecMemMode::kToggle ? "toggle"
                 : mode == ExecMemMode::kPrepared ? "prepared" : "legacy",
                 fetchable ? 1 : 0, RuntimeSummaryInner());
}

ProbeState& ProbeStateInstance() {
    static ProbeState state;
    if (!state.initialized.load(std::memory_order_acquire)) {
        std::lock_guard<std::recursive_mutex> lock(ProbeMutex());
        if (!state.initialized.load(std::memory_order_relaxed)) RunProbeLocked(state);
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

// Exported form of the region probe for signal-handler diagnostics: the fault
// handler needs to know whether a faulting address is mapped, and with which
// permissions, to tell a missing commit (cur lacks the needed bit, max has it)
// from a wild pointer (no region at all).
#if defined(__APPLE__)
bool QueryRegionProt(const void* addr, char cur[4], char max[4],
                     uint64_t* regionBase, uint64_t* regionSize) {
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
    // Same gap quirk as RegionProt above: a null-page fault would otherwise
    // report as the main binary's r-x (observed: fault 0x160 diagnosed as
    // region 0x100bec000 cur=r-x).
    {
        const uint64_t base = static_cast<uint64_t>(a);
        const uint64_t size = static_cast<uint64_t>(sz);
        const uint64_t fault = reinterpret_cast<uint64_t>(addr);
        if (fault < base || fault - base >= size) return false;
    }
    ProtStr(info.protection, cur);
    ProtStr(info.max_protection, max);
    if (regionBase) *regionBase = static_cast<uint64_t>(a);
    if (regionSize) *regionSize = static_cast<uint64_t>(sz);
    return true;
}
#else
bool QueryRegionProt(const void*, char cur[4], char max[4],
                     uint64_t*, uint64_t*) {
    if (cur != nullptr) cur[0] = '\0';
    if (max != nullptr) max[0] = '\0';
    return false;
}
#endif

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
    if (st.fetchable.load(std::memory_order_acquire)) return true;  // never downgrade
    // A debugger attaching mid-run grants JIT permissions the first probe
    // legitimately lacked. Ask the kernel again under the probe lock; a concurrent
    // caller that already succeeded makes this a no-op.
    std::lock_guard<std::recursive_mutex> lock(ProbeMutex());
    if (st.fetchable.load(std::memory_order_relaxed)) return true;
    RunProbeLocked(st);
    return st.fetchable.load(std::memory_order_acquire);
}

bool ExecMemory::TxmPresent() {
    return TxmEstimate();
}

const char* ExecMemory::RuntimeSummary() {
    return RuntimeSummaryInner();
}

void ExecMemory::LogState() {
    const ProbeState& state = ProbeStateInstance();
    std::lock_guard<std::recursive_mutex> lock(ProbeMutex());
    std::fprintf(stderr, "[KuDroidExecMem] run state mode=%s fetchable=%d guest_pool=%d %s\n",
                 ModeName(), state.fetchable.load(std::memory_order_acquire) ? 1 : 0,
                 GuestReservationsAvailable() ? 1 : 0, RuntimeSummaryInner());
#if defined(__APPLE__)
    const GuestProbeStatus& status = LastGuestProbe();
    std::fprintf(stderr,
                 "[KuDroidExecMem] guest capability stage=%s prot=0x%x ret=%d errno=%d before_cur=0x%x before_max=0x%x cur=0x%x max=0x%x\n",
                 status.stage, status.prot, status.result, status.error,
                 status.beforeCurrent, status.beforeMaximum,
                 status.current, status.maximum);
    for (size_t i = 0; i < status.transitionCount; ++i) {
        PrintGuestTransition(status.transitions[i]);
    }
#endif
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

bool ExecMemory::GuestReservationsAvailable() {
#if defined(__APPLE__)
    return GuestArenaState().active.load(std::memory_order_acquire);
#else
    return false;
#endif
}

void* ExecMemory::GuestWritePointer(void* addr, size_t size) {
#if defined(__APPLE__)
    const GuestArena& a = GuestArenaState();
    if (!a.active.load(std::memory_order_acquire) || size == 0) return nullptr;
    const uintptr_t base = reinterpret_cast<uintptr_t>(a.address);
    const uintptr_t p = reinterpret_cast<uintptr_t>(addr);
    const size_t used = a.used.load(std::memory_order_relaxed);
    if (p < base || p - base >= used || size > used - (p - base)) return nullptr;
    return static_cast<char*>(a.write) + (p - base);
#else
    (void)addr;
    (void)size;
    return nullptr;
#endif
}

bool ExecMemory::GuestMemoryOverlaps(const void* addr, size_t size) {
#if defined(__APPLE__)
    const GuestArena& a = GuestArenaState();
    if (!a.active.load(std::memory_order_acquire) || size == 0) return false;
    const uintptr_t base = reinterpret_cast<uintptr_t>(a.address);
    const uintptr_t p = reinterpret_cast<uintptr_t>(addr);
    return p >= base ? p - base < a.size : size > base - p;
#else
    (void)addr;
    (void)size;
    return false;
#endif
}

void* ExecMemory::AllocateGuest(size_t size, int prot) {
#if defined(__APPLE__)
    GuestArena& a = GuestArenaState();
    if (!a.active.load(std::memory_order_acquire)) { errno = ENOTSUP; return MAP_FAILED; }
    if (size == 0 || size > SIZE_MAX - (PageSize() - 1)) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    const size_t rounded = RoundUp(size, PageSize());
    size_t offset = 0;
    if (!ClaimArenaBytes(a.used, a.size, rounded, &offset)) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    void* address = static_cast<char*>(a.address) + offset;
    std::memset(static_cast<char*>(a.write) + offset, 0, rounded);
    if (::mprotect(address, rounded, prot) != 0) return MAP_FAILED;
    return address;
#else
    (void)size;
    (void)prot;
    errno = ENOTSUP;
    return MAP_FAILED;
#endif
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
