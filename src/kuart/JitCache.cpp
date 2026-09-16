#include "kudroid/kuart/JitCache.h"

#include <cstdio>

namespace kudroid {
namespace kuart {

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
        ExecMemory::Free(b.region);
    }
    blocks_.clear();
}

bool JitCache::IsAvailable() {
    // Cached; the probe result is fixed at exec: which memory strategies the
    // kernel sanctions cannot change while the process runs. The probe executes
    // written code, so its answer covers the whole pipeline this cache serves.
    static const bool available = ExecMemory::IsFetchable();
    if (!available) {
        std::fprintf(stderr,
                     "[KuART][JIT] executable memory unavailable; running"
                     " interpreter only. Enable JIT (debugger attached,"
                     " LiveContainer JIT mode, or TrollStore) for compiled code.\n");
    }
    return available;
}

ExecMemory::Region JitCache::Allocate(size_t size) {
    if (size == 0) return {};
    if (!IsAvailable()) return {};

    // One page-aligned allocation per method; sharing pages would seal a later
    // method's memory when an earlier one commits.
    const size_t pageSize = ExecMemory::PageSize();
    const size_t need = (size + pageSize - 1) & ~(pageSize - 1);
    if (need > kBlockSize) return {};

    std::lock_guard<std::mutex> lock(mutex_);
    const size_t budget = EffectiveBudgetBytes(query_system_memory());
    if (budget == 0 || bytes_allocated_ + need > budget) return {};

    if (!blocks_.empty()) {
        Block& tail = blocks_.back();
        if (tail.used + need <= tail.region.size) {
            uint8_t* base = static_cast<uint8_t*>(tail.region.writeView);
            uint8_t* p = base + tail.used;
            tail.used += need;
            bytes_allocated_ += need;
            return ExecMemory::Slice(tail.region, p, need);
        }
    }

    ExecMemory::Region block = ExecMemory::Allocate(kBlockSize, /*exec=*/true);
    if (block.writeView == nullptr) return {};

    uint8_t* p = static_cast<uint8_t*>(block.writeView);
    blocks_.push_back(Block{block, need});
    bytes_allocated_ += need;
    return ExecMemory::Slice(block, p, need);
}

void* JitCache::Commit(const ExecMemory::Region& region, void* code, size_t size) {
    if (region.writeView == nullptr || code == nullptr || size == 0) return nullptr;
    if (!ExecMemory::Commit(region, code, size)) {
        std::fprintf(stderr, "[KuART][JIT] executable transition failed\n");
        return nullptr;
    }
    return ExecMemory::ExecPointer(region, code);
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
