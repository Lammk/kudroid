#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>

namespace kudroid {
class AlignedVmReservations {
public:
    AlignedVmReservations();
    ~AlignedVmReservations();
    AlignedVmReservations(const AlignedVmReservations&) = delete;
    AlignedVmReservations& operator=(const AlignedVmReservations&) = delete;

    std::size_t pageSize() const { return pageSize_; }
    bool prepare(std::size_t length, std::size_t alignment);
    void* reserve(std::size_t length, std::size_t alignment, void* hint = nullptr);
    bool release(void* address, std::size_t length);
    bool protect(void* address, std::size_t length, int permissions);
    bool discard(void* address, std::size_t length);

private:
    bool valid(std::size_t length, std::size_t alignment) const;
    bool contains(std::uintptr_t address, std::size_t length) const;
    void* allocate(std::size_t length, std::size_t alignment, void* hint);
    std::size_t pageSize_;
    std::mutex mutex_;
    std::map<std::uintptr_t, std::size_t> spans_;
    void* prepared_ = nullptr;
    std::size_t preparedLength_ = 0;
    std::size_t preparedAlignment_ = 0;
};
}
