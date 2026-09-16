// Process-wide executable memory.
//
// Some platforms enforce hardware W^X on dynamically written code and refuse a plain
// RW->RX mprotect transition at instruction-fetch time even when the syscall succeeds.
// Three allocation strategies cover the whole range, selected by probing what the
// kernel actually allows — never by checking OS version strings:
//
//   kDualMap  Backing MAP_JIT memory aliased at a second virtual address with
//             VM_PROT_READ | VM_PROT_EXECUTE. Code is written through the RW view,
//             fetched through the RX view; no runtime permission toggles. This is
//             the only path known to survive the hardware-monitored W^X regimes.
//   kToggle   MAP_JIT + pthread_jit_write_protect_np around writes (the official
//             thread-local JIT write-enable pattern).
//   kLegacy   Plain anonymous RW pages flipped to RX with mprotect — the behavior
//             of iOS 18 and earlier, macOS without hardened runtime, and Linux.
//   kPrepared iOS 26+ TXM/SPTM: one RW backing aliased RX, prepared over the
//             debug connection (JIT26PrepareRegion) before any fetch, then
//             detached. No permission transition is performed at runtime.
//
// The probe executes real instructions (including an ADRP) from the transitioned
// memory under a SIGBUS/SIGSEGV recovery guard, because a succeeded mprotect does not
// prove the fetch is sanctioned.
#ifndef KUDROID_EXECMEMORY_H
#define KUDROID_EXECMEMORY_H

#include <cstddef>
#include <cstdint>

namespace kudroid {

enum class ExecMemMode {
    kLegacy = 0,   // anonymous RW -> mprotect RX
    kToggle = 1,   // MAP_JIT + pthread_jit_write_protect_np
    kDualMap = 2,  // MAP_JIT backing + RX alias via mach_vm_remap
    kPrepared = 3, // TXM: RW backing + RX alias prepared over the debug connection
};

class ExecMemory {
public:
    struct Region {
        // View code is WRITTEN through (always writable at allocation time).
        void* writeView = nullptr;
        // View code is FETCHED through. Equals writeView under kLegacy.
        void* execView = nullptr;
        size_t size = 0;
    };

    // Probe once per process which strategy the kernel sanctions and cache it.
    // Prints one "[KuDroidExecMem] mode=..." line on first use. Never throws.
    static ExecMemMode Mode();

    // Human-readable name ("dual-map" | "toggle" | "legacy") for diagnostics.
    static const char* ModeName();

    // True when the probe proved the selected mode actually executes written
    // code. Allocation for executable regions refuses when this is false.
    static bool IsFetchable();

    // Re-run the capability probe. For processes whose JIT permission arrives
    // after launch (a debugger attaching mid-run), the first probe legitimately
    // failed and must be retried once the kernel's view has changed. Never
    // downgrades a previous positive result. Returns the current fetchability.
    static bool Reprobe();

    // Diagnostics. True when the device is estimated to enforce the TXM/SPTM
    // regime (iOS 26+ on A13+/M1+), where a debugger attach alone is not enough
    // and every executable region must be prepared over the debug connection.
    // The estimate comes from OS version + SoC; the prepare handshake itself is
    // the authoritative answer. False on non-Apple and on the Simulator.
    static bool TxmPresent();

    // One-line environment summary for logs: "ios=<ver> machine=<id> txm~=<0|1>".
    static const char* RuntimeSummary();

    // True when the TXM regime is present AND a debugger is attached — the only
    // state in which the prepare handshake can be attempted. Attempting a
    // protocol brk without an attached script raises a real SIGTRAP.
    static bool TxmScriptReady();

    // Hand `alias` (an RX mapping aliasing writable backing pages) to the debug
    // script for preparation under TXM. On every other platform this returns
    // false and the caller uses the plain aliasing path. Success still has to be
    // proven by fetching through the region.
    static bool PrepareRegion(void* alias, size_t size);

    // Tells the script the executable-region set is final. No further exec
    // region may be created after this call on a TXM device.
    static void DetachScript();

    // Platform page size used to round allocations.
    static size_t PageSize();

    // In toggle mode, enable writes on the calling thread for MAP_JIT-backed
    // regions (no-op elsewhere). Every write phase into a region must be
    // bracketed: BeginWrite before, Commit or EndWrite after.
    static void BeginWrite();

    // In toggle mode, switch the calling thread back to fetch state (no-op
    // elsewhere). Called once when a load/emit phase is complete.
    static void EndWrite();

    // Sub-region of `parent` covering [ptr, ptr+size): the exec view is sliced
    // by the same offset as the write view. ptr must lie inside parent's
    // writeView. Returns an empty Region when out of bounds.
    static Region Slice(const Region& parent, void* ptr, size_t size);

    // The exec-view counterpart of a pointer written through `region`.
    // Returns nullptr when `code` lies outside the region.
    static void* ExecPointer(const Region& region, const void* code);

    // Raw MAP_JIT-backed (or plain, under kLegacy) writable region without an
    // exec alias — for callers that build their own aliasing (image loaders).
    static Region AllocateBacking(size_t size);

    // Map the same physical pages of [backing, backing+size) at a new virtual
    // address with VM_PROT_READ | VM_PROT_EXECUTE. Writes through the backing
    // view become fetchable through the returned alias. Apple platforms only;
    // returns false elsewhere.
    static bool RemapExecAlias(void* backing, size_t size, void** outAlias);

    // Allocate `size` bytes using the selected strategy.
    // Returns a Region with distinct write/exec views under kDualMap, and
    // writeView == execView otherwise. Returns {} when allocation fails.
    // `exec` requests an executable region (false = plain RW scratch).
    static Region Allocate(size_t size, bool exec);

    // Release a region returned by Allocate().
    static void Free(const Region& region);

    // Make [code, code+size) in `region` executable and flush the instruction
    // cache. Under kDualMap this is a no-op on the mappings themselves (both
    // views were established at allocation) plus the icache flush. Returns
    // false only when the platform declined the transition.
    static bool Commit(const Region& region, void* code, size_t size);

    // Offset of the exec view relative to the write view (0 except kDualMap,
    // where execView - writeView is constant for the process).
    static ptrdiff_t ExecViewDelta();

    ExecMemory() = delete;
};

}  // namespace kudroid

#endif  // KUDROID_EXECMEMORY_H
