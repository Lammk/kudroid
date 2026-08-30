// Executable memory for KuART's JIT.
//
// iOS refuses PROT_EXEC on anonymous memory unless the process is being debugged or
// carries the JIT entitlement, and under LiveContainer's JITLess mode neither holds.
// That is not an error to work around — it is a permission the platform declines to
// grant, and the only correct response is to run the interpreter instead. So every
// entry point here is null-safe and `IsAvailable()` is the gate callers check once.
//
// Allocation is bump-pointer within large blocks, and nothing is ever freed. A JIT that
// frees code needs to know no thread is executing inside it, which needs safepoints;
// KuART has no safepoints, and a Java method compiled once is executed for the life of
// the process anyway. Bounding total size is the substitute for freeing.
#ifndef KUDROID_KUART_JITCACHE_H
#define KUDROID_KUART_JITCACHE_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "kudroid/platform/MemoryInfo.h"

namespace kudroid {
namespace kuart {

class JitCache {
public:
    // The process-wide cache. Compiled code outlives any one class loader, and two
    // caches would each pay the per-block minimum.
    static JitCache& Instance();

    // True when this process may execute memory it wrote.
    //
    // Probed once by actually mapping a page and calling mprotect, because that is the
    // question being asked. Indirect checks (CS_DEBUGGED, an entitlement in the
    // signature) can disagree with the kernel in either direction, and under
    // LiveContainer the guest inherits the host's signing status, so KuDroid's own
    // entitlements say nothing about what it is allowed to do.
    static bool IsAvailable();

    // Return the current append-only budget. A zero process headroom means the
    // platform did not expose a per-process figure, so the hard cap remains the
    // fallback. Existing RX code is never reclaimed by this policy.
    static size_t EffectiveBudgetBytes(const SystemMemory& memory);

    // Reserve writable memory for at least `size` bytes of code.
    //
    // The returned block is page-aligned and owns its pages exclusively, because
    // Commit() can only change protection a page at a time: if two methods shared a
    // page, committing the first would make the second's memory read-only before it was
    // written. The remainder of the last page is left unused, which costs address space
    // and nothing else.
    //
    // Returns nullptr when JIT is unavailable, when `size` exceeds what a block can
    // hold, or when the total budget is exhausted. The memory is writable but NOT yet
    // executable — call Commit() when the code is complete.
    void* Allocate(size_t size);

    // Make previously allocated code executable and flush the instruction cache.
    //
    // arm64 has split instruction and data caches: freshly written code sits in the
    // data cache while the instruction cache still holds whatever was there before, so
    // without the flush the CPU executes stale bytes. That is a fault with no
    // diagnostic — the pc lands in the middle of an instruction that was never
    // written.
    bool Commit(void* code, size_t size);

    size_t BytesAllocated() const;
    size_t BlockCount() const;
    size_t BudgetBytes() const;

    // Total code the cache will ever hand out. Reached in practice only by a workload
    // that compiles tens of thousands of methods; the interpreter takes over after.
    static constexpr size_t kMaxTotalBytes = 32u * 1024u * 1024u;

private:
    JitCache() = default;
    ~JitCache();
    JitCache(const JitCache&) = delete;
    JitCache& operator=(const JitCache&) = delete;

    static constexpr size_t kBlockSize = 256u * 1024u;

    struct Block {
        uint8_t* memory = nullptr;
        size_t used = 0;
        size_t capacity = 0;
    };

    mutable std::mutex mutex_;
    std::vector<Block> blocks_;
    size_t bytes_allocated_ = 0;
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_JITCACHE_H
