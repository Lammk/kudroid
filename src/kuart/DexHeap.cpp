#include "kudroid/kuart/DexHeap.h"

#include <cstdlib>
#include <cstring>

namespace kudroid {
namespace kuart {

namespace {
constexpr size_t kAlignment = 8;

size_t RoundUpToAlignment(size_t n) {
    return (n + kAlignment - 1) & ~(kAlignment - 1);
}
}  // namespace

DexHeap::~DexHeap() {
    for (Block& b : blocks_) {
        std::free(b.memory);
    }
    blocks_.clear();
}

void* DexHeap::Allocate(size_t bytes) {
    if (bytes == 0) return nullptr;
    const size_t need = RoundUpToAlignment(bytes);

    if (!blocks_.empty()) {
        Block& tail = blocks_.back();
        if (tail.used + need <= tail.capacity) {
            void* p = tail.memory + tail.used;
            tail.used += need;
            bytes_allocated_ += need;
            return p;
        }
    }

    // If the object is larger than the block, it will provide enough separate block without wasting the remainder.
    const size_t capacity = need > kBlockSize ? need : kBlockSize;
    auto* memory = static_cast<uint8_t*>(std::calloc(1, capacity));
    if (memory == nullptr) return nullptr;

    blocks_.push_back(Block{memory, need, capacity});
    bytes_allocated_ += need;
    return memory;
}

const char* DexHeap::InternString(const char* s) {
    if (s == nullptr) return nullptr;
    const size_t len = std::strlen(s);
    auto* copy = static_cast<char*>(Allocate(len + 1));
    if (copy == nullptr) return nullptr;
    std::memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

}  // namespace kuart
}  // namespace kudroid
