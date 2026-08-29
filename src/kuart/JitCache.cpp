#include "kudroid/kuart/JitCache.h"

#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#endif

namespace kudroid {
namespace kuart {
namespace {

size_t PageSize() {
    const long v = ::sysconf(_SC_PAGESIZE);
    return v > 0 ? static_cast<size_t>(v) : 4096u;
}

size_t RoundUp(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

// Map `size` bytes that this process is permitted to execute, or nullptr.
//
// MAP_JIT is the sanctioned route under the hardened runtime, but only exists for
// macOS; on iOS the equivalent is to map RW and mprotect. Both are attempted because
// KuDroid's host tests run on macOS and Linux while the product target is iOS.
void* MapExecutable(size_t size) {
    const int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANON;
    void* mem = MAP_FAILED;
#if defined(__APPLE__) && TARGET_OS_OSX
    mem = ::mmap(nullptr, size, prot, flags | MAP_JIT, -1, 0);
    if (mem != MAP_FAILED) return mem;
#endif
    mem = ::mmap(nullptr, size, prot, flags, -1, 0);
    if (mem == MAP_FAILED) return nullptr;

    // Confirm the mapping can become executable NOW rather than at Commit() time.
    // Discovering it at Commit means a compiler has already produced code with nowhere
    // to put it, and the caller has no way back to the interpreter for that method.
    if (::mprotect(mem, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        ::munmap(mem, size);
        return nullptr;
    }
    // Back to writable: code is written before it is run, and leaving a block
    // permanently RWX is both unnecessary and the shape every W^X policy objects to.
    if (::mprotect(mem, size, PROT_READ | PROT_WRITE) != 0) {
        ::munmap(mem, size);
        return nullptr;
    }
    return mem;
}

}  // namespace

JitCache& JitCache::Instance() {
    static JitCache instance;
    return instance;
}

JitCache::~JitCache() {
    for (Block& b : blocks_) {
        if (b.memory != nullptr) ::munmap(b.memory, b.capacity);
    }
    blocks_.clear();
}

bool JitCache::IsAvailable() {
    // Cached: code-signing status is fixed at exec, so the answer cannot change, and
    // this is consulted on every method that becomes hot.
    static const bool available = [] {
        const size_t size = PageSize();
        void* probe = MapExecutable(size);
        if (probe == nullptr) {
            std::fprintf(stderr,
                         "[KuART][JIT] executable memory unavailable; running"
                         " interpreter only. Enable JIT (debugger attached,"
                         " LiveContainer JIT mode, or TrollStore) for compiled code.\n");
            return false;
        }
        ::munmap(probe, size);
        std::fprintf(stderr, "[KuART][JIT] executable memory available\n");
        return true;
    }();
    return available;
}

void* JitCache::Allocate(size_t size) {
    if (size == 0) return nullptr;
    if (!IsAvailable()) return nullptr;

    // One allocation per page, never two.
    //
    // Commit() has to work in whole pages because that is the granularity mprotect
    // operates on, so committing one method makes every byte of its pages read-only.
    // Packing the next method into the leftover space of such a page hands out memory
    // that is already RX, and the memcpy filling it takes SIGBUS — which is exactly
    // what happened: the first compiled method succeeded, the second faulted inside
    // memcpy with the freshly returned pointer as the destination address.
    //
    // Rounding the allocation out to a page boundary is what makes the two operations
    // agree. The alternative, mprotecting a page back to writable to reuse its tail,
    // would strip PROT_EXEC from finished code that another thread may be executing at
    // that moment; KuART has no safepoints, so there is no moment when that is known to
    // be safe. Wasting the remainder of a page costs address space and nothing else.
    const size_t pageSize = PageSize();
    const size_t need = RoundUp(size, pageSize);
    if (need > kBlockSize) return nullptr;

    std::lock_guard<std::mutex> lock(mutex_);
    if (bytes_allocated_ + need > kMaxTotalBytes) return nullptr;

    if (!blocks_.empty()) {
        Block& tail = blocks_.back();
        if (tail.used + need <= tail.capacity) {
            uint8_t* p = tail.memory + tail.used;
            tail.used += need;
            bytes_allocated_ += need;
            return p;
        }
    }

    const size_t capacity = RoundUp(kBlockSize, pageSize);
    auto* memory = static_cast<uint8_t*>(MapExecutable(capacity));
    if (memory == nullptr) return nullptr;

    blocks_.push_back(Block{memory, need, capacity});
    bytes_allocated_ += need;
    return memory;
}

bool JitCache::Commit(void* code, size_t size) {
    if (code == nullptr || size == 0) return false;

    // Page-align outwards: mprotect works on whole pages. Allocate() hands out whole
    // pages for exactly this reason, so the rounding here cannot reach a neighbouring
    // allocation — the range covers only this method's own pages.
    const size_t pageSize = PageSize();
    auto addr = reinterpret_cast<uintptr_t>(code);
    const uintptr_t start = addr & ~(pageSize - 1);
    const uintptr_t end = RoundUp(addr + size, pageSize);

#if defined(__APPLE__) && TARGET_OS_OSX
    // Under MAP_JIT the pages are already RX; what changes is the per-thread
    // write-protect state, which the compiler turned off to write this code.
    pthread_jit_write_protect_np(1);
#endif

    if (::mprotect(reinterpret_cast<void*>(start), end - start,
                   PROT_READ | PROT_EXEC) != 0) {
        std::fprintf(stderr, "[KuART][JIT] mprotect(PROT_EXEC) failed: %s\n",
                     std::strerror(errno));
        return false;
    }

    // Split I/D caches: the code just written is in the data cache while the
    // instruction cache may hold whatever occupied these addresses before. Executing
    // without the flush runs stale bytes, which faults at a pc that never held an
    // instruction — the hardest possible failure to read.
#if defined(__APPLE__)
    sys_icache_invalidate(reinterpret_cast<void*>(start), end - start);
#else
    __builtin___clear_cache(reinterpret_cast<char*>(start), reinterpret_cast<char*>(end));
#endif
    return true;
}

size_t JitCache::BytesAllocated() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bytes_allocated_;
}

size_t JitCache::BlockCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return blocks_.size();
}

}  // namespace kuart
}  // namespace kudroid
