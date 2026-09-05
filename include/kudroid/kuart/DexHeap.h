// Bump-pointer allocator for DEX objects/metadata (no GC; kept until app exit).
// Java runs only briefly, so the retained memory stays bounded.
#ifndef KUDROID_KUART_DEXHEAP_H
#define KUDROID_KUART_DEXHEAP_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kudroid {
namespace kuart {

class DexHeap {
public:
    DexHeap() = default;
    ~DexHeap();

    DexHeap(const DexHeap&) = delete;
    DexHeap& operator=(const DexHeap&) = delete;

    // Return zero-filled, 8-aligned memory, or nullptr when out of memory.
    void* Allocate(size_t bytes);

    template <typename T>
    T* New() {
        void* mem = Allocate(sizeof(T));
        return mem != nullptr ? new (mem) T() : nullptr;
    }

    // Descriptor/name strings are copied into the heap for stable pointers.
    const char* InternString(const char* s);

    size_t BytesAllocated() const { return bytes_allocated_; }
    size_t BlockCount() const { return blocks_.size(); }

private:
    static constexpr size_t kBlockSize = 1 << 20;  // 1MB

    struct Block {
        uint8_t* memory = nullptr;
        size_t used = 0;
        size_t capacity = 0;
    };

    std::vector<Block> blocks_;
    size_t bytes_allocated_ = 0;
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXHEAP_H
