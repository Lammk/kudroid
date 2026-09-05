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

// Map executable memory, or nullptr.
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

    return mem;
}

}  // namespace

JitCache& JitCache::Instance() {
    static JitCache instance;
    return instance;
}

size_t JitCache::EffectiveBudgetBytes(const SystemMemory& memory) {
    if (memory.process_available_bytes == 0) return kMaxTotalBytes;
    constexpr uint64_t kMiB = 1024ull * 1024ull;
    if (memory.process_available_bytes < 128ull * kMiB) return 0;
    if (memory.process_available_bytes < 256ull * kMiB) return 4ull * kMiB;
    if (memory.process_available_bytes < 512ull * kMiB) return 16ull * kMiB;
    return kMaxTotalBytes;
}

JitCache::~JitCache() {
    for (Block& b : blocks_) {
        if (b.memory != nullptr) ::munmap(b.memory, b.capacity);
    }
    blocks_.clear();
}

bool JitCache::IsAvailable() {
    // Cached; code-signing status is fixed at exec.
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
        // Probe the Commit transition, never RWX.
        const bool executable = ::mprotect(probe, size, PROT_READ | PROT_EXEC) == 0;
        if (executable) {
            (void)::mprotect(probe, size, PROT_READ | PROT_WRITE);
        }
        ::munmap(probe, size);
        if (!executable) {
            std::fprintf(stderr, "[KuART][JIT] executable transition unavailable; running interpreter only.\n");
            return false;
        }
        std::fprintf(stderr, "[KuART][JIT] executable memory available\n");
        return true;
    }();
    return available;
}

void* JitCache::Allocate(size_t size) {
    if (size == 0) return nullptr;
    if (!IsAvailable()) return nullptr;

    // One page-aligned allocation per method; sharing RX pages would fault.
    const size_t pageSize = PageSize();
    const size_t need = RoundUp(size, pageSize);
    if (need > kBlockSize) return nullptr;

    std::lock_guard<std::mutex> lock(mutex_);
    const size_t budget = EffectiveBudgetBytes(query_system_memory());
    if (budget == 0 || bytes_allocated_ + need > budget) return nullptr;

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

    // Page-align outwards for mprotect; covers only this method.
    const size_t pageSize = PageSize();
    auto addr = reinterpret_cast<uintptr_t>(code);
    const uintptr_t start = addr & ~(pageSize - 1);
    const uintptr_t end = RoundUp(addr + size, pageSize);

#if defined(__APPLE__) && TARGET_OS_OSX
    // Under MAP_JIT, restore write-protect after emitting code.
    pthread_jit_write_protect_np(1);
#endif

    if (::mprotect(reinterpret_cast<void*>(start), end - start,
                   PROT_READ | PROT_EXEC) != 0) {
        std::fprintf(stderr, "[KuART][JIT] mprotect(PROT_EXEC) failed: %s\n",
                     std::strerror(errno));
        return false;
    }

    // Flush caches so the CPU fetches the bytes just written.
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

size_t JitCache::BudgetBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return EffectiveBudgetBytes(query_system_memory());
}

}  // namespace kuart
}  // namespace kudroid
