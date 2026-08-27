// Memory allocation for object/metadata c a DEX runtime.
//
// NO GC: allocated and kept until app exit. L  do ch p nh n  c   trong
// KuDroid, code Java ch  ch y l c onCreate v  khi h ng touch event r i  y
// xu ng C++; game loop n m ho n to n trong libminecraftpe.so. L ng r c Java
// sinh ra kh ng  ng k  so v i v ng  i m t l n ch y app.
//
// C p theo block l n r i bump pointer: nhanh h n malloc t ng object v  gi
// object c a c ng m t class g n nhau (locality t t cho interpreter).
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

    // Tr  v  v ng   zero-fill, align 8. nullptr n u h t b  nh .
    void* Allocate(size_t bytes);

    template <typename T>
    T* New() {
        void* mem = Allocate(sizeof(T));
        return mem != nullptr ? new (mem) T() : nullptr;
    }

    // Chu i cho descriptor/t n   copy v o heap   DexClass gi  con tr  b n.
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
