// Cấp phát bộ nhớ cho object/metadata của DEX runtime.
//
// KHÔNG CÓ GC: cấp rồi giữ tới khi đóng app. Lý do chấp nhận được — trong
// KuDroid, code Java chỉ chạy lúc onCreate và khi hứng touch event rồi đẩy
// xuống C++; game loop nằm hoàn toàn trong libminecraftpe.so. Lượng rác Java
// sinh ra không đáng kể so với vòng đời một lần chạy app.
//
// Cấp theo block lớn rồi bump pointer: nhanh hơn malloc từng object và giữ
// object của cùng một class gần nhau (locality tốt cho interpreter).
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

    // Trả về vùng đã zero-fill, align 8. nullptr nếu hết bộ nhớ.
    void* Allocate(size_t bytes);

    template <typename T>
    T* New() {
        void* mem = Allocate(sizeof(T));
        return mem != nullptr ? new (mem) T() : nullptr;
    }

    // Chuỗi cho descriptor/tên — copy vào heap để DexClass giữ con trỏ bền.
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
