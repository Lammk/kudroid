// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by the BSD-style bundled V8 license.
#include "kudroid/RuntimeCageApi.h"
#include "src/utils/allocation.h"
#include "src/base/bounded-page-allocator.h"
#include <atomic>
#include <memory>

extern "C" __attribute__((visibility("default"))) const KudroidRuntimeCageRequirements
kudroid_v8_cage_requirements = {1,
    v8::internal::Internals::kPtrComprCageReservationSize,
    v8::internal::Internals::kPtrComprCageBaseAlignment};

namespace {
static_assert(sizeof(v8::internal::VirtualMemoryCage) == 48);
static_assert(sizeof(v8::internal::VirtualMemoryCage::ReservationParams) == 48);
static_assert(sizeof(v8::internal::VirtualMemory) == 24);
static_assert(sizeof(v8::base::BoundedPageAllocator) == 176);
KudroidRuntimeCageApi api{};
using Cage = v8::internal::VirtualMemoryCage;
using OriginalInit = bool (*)(Cage*, const Cage::ReservationParams&);

class ReservationAllocator final : public v8::PageAllocator {
public:
    ReservationAllocator(v8::PageAllocator* original, void* address, size_t length)
        : original_(original), address_(reinterpret_cast<uintptr_t>(address)), length_(length) {}
    size_t AllocatePageSize() override { return api.page_size; }
    size_t CommitPageSize() override { return api.page_size; }
    void SetRandomMmapSeed(int64_t seed) override { original_->SetRandomMmapSeed(seed); }
    void* GetRandomMmapAddr() override { return original_->GetRandomMmapAddr(); }
    void* AllocatePages(void* hint, size_t length, size_t alignment, Permission permissions) override {
        return original_->AllocatePages(hint, length, alignment, permissions);
    }
    bool FreePages(void* address, size_t length) override {
        if (reinterpret_cast<uintptr_t>(address) == address_ && length == length_.load()) {
            if (length && !api.release(api.context, address, length)) return false;
            // The VirtualMemory owner resets itself before this final release.
            delete this;
            return true;
        }
        return !overlaps(address, length) && original_->FreePages(address, length);
    }
    bool ReleasePages(void* address, size_t length, size_t new_length) override {
        if (reinterpret_cast<uintptr_t>(address) != address_ || length != length_.load()) {
            return !overlaps(address, length) && original_->ReleasePages(address, length, new_length);
        }
        if (new_length > length || new_length % api.page_size) return false;
        if (new_length == length) return true;
        if (!api.release(api.context, reinterpret_cast<void*>(address_ + new_length), length - new_length))
            return false;
        length_.store(new_length);
        return true;
    }
    bool SetPermissions(void* address, size_t length, Permission permissions) override {
        if (!contains(address, length))
            return !overlaps(address, length) && original_->SetPermissions(address, length, permissions);
        int protection;
        switch (permissions) {
            case kNoAccess: case kNoAccessWillJitLater: protection = 0; break;
            case kRead: protection = 1; break;
            case kReadWrite: protection = 3; break;
            case kReadWriteExecute: protection = 7; break;
            case kReadExecute: protection = 5; break;
            default: return false;
        }
        return api.protect(api.context, address, length, protection);
    }
    bool DiscardSystemPages(void* address, size_t length) override {
        if (!contains(address, length))
            return !overlaps(address, length) && original_->DiscardSystemPages(address, length);
        return api.discard(api.context, address, length);
    }
private:
    bool contains(void* pointer, size_t size) const {
        const auto address = reinterpret_cast<uintptr_t>(pointer);
        const size_t length = length_.load();
        return address >= address_ && address - address_ <= length && size <= length - (address - address_);
    }
    bool overlaps(void* pointer, size_t size) const {
        const auto address = reinterpret_cast<uintptr_t>(pointer);
        if (address > UINTPTR_MAX - size) return true;
        return address < address_ + length_.load() && address + size > address_;
    }
    v8::PageAllocator* original_;
    uintptr_t address_;
    std::atomic<size_t> length_;
};
}

extern "C" __attribute__((visibility("default"))) int kudroid_v8_cage_configure(
    const KudroidRuntimeCageApi* supplied, uint64_t* length, uint64_t* alignment) {
    if (api.version || !supplied || supplied->version != 1 || !supplied->page_size ||
        (supplied->page_size & (supplied->page_size - 1)) || !supplied->original_init ||
        !supplied->reserve || !supplied->release || !supplied->protect || !supplied->discard ||
        !length || !alignment) return 0;
    api = *supplied;
    *length = kudroid_v8_cage_requirements.length;
    *alignment = kudroid_v8_cage_requirements.alignment;
    return 1;
}

__attribute__((visibility("default"))) bool v8::internal::VirtualMemoryCage::InitReservation(
    const ReservationParams& params) {
    if (!api.version || IsReserved()) return false;
    const size_t native_page = api.page_size;
    const bool compatible = params.page_allocator && params.reservation_size &&
        params.reservation_size % native_page == 0 && params.base_bias_size == 0 &&
        params.base_alignment >= native_page && !(params.base_alignment & (params.base_alignment - 1)) &&
        params.page_size >= native_page && !(params.page_size & (params.page_size - 1));
    if (!compatible) return reinterpret_cast<OriginalInit>(api.original_init)(this, params);

    void* address = api.reserve(api.context, params.reservation_size, params.base_alignment,
                               reinterpret_cast<void*>(params.requested_start_hint));
    if (!address) return false;
    // The reservation controls the allocator lifetime, including failed construction.
    auto* allocator = new ReservationAllocator(params.page_allocator, address, params.reservation_size);
    VirtualMemory reservation(allocator, reinterpret_cast<Address>(address), params.reservation_size);
    const auto start = reinterpret_cast<Address>(address);
    if (start > UINTPTR_MAX - (params.page_size - 1)) return false;
    const Address allocatable_base = (start + params.page_size - 1) & ~(params.page_size - 1);
    const size_t displacement = allocatable_base - start;
    if (displacement > params.reservation_size) return false;
    const size_t allocatable_size = (params.reservation_size - displacement) & ~(params.page_size - 1);
    auto bounded = std::make_unique<base::BoundedPageAllocator>(allocator,
        allocatable_base, allocatable_size,
        params.page_size);
    reservation_ = std::move(reservation);
    page_allocator_ = std::move(bounded);
    base_ = reinterpret_cast<Address>(address);
    return true;
}
