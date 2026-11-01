#include "kudroid/AlignedVmReservations.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>

#if defined(__APPLE__)
#include "kudroid/MachVmDecls.h"
#endif

namespace kudroid {
namespace {
bool unmap(void* address, std::size_t length) {
#if defined(__APPLE__)
    return mach_vm_deallocate(mach_task_self(), reinterpret_cast<mach_vm_address_t>(address),
                              length) == KERN_SUCCESS;
#else
    return ::munmap(address, length) == 0;
#endif
}
}

AlignedVmReservations::AlignedVmReservations() {
    const long page = ::sysconf(_SC_PAGESIZE);
    pageSize_ = page > 0 ? static_cast<std::size_t>(page) : 0;
}

AlignedVmReservations::~AlignedVmReservations() {
    for (const auto& span : spans_) unmap(reinterpret_cast<void*>(span.first), span.second);
}

bool AlignedVmReservations::valid(std::size_t length, std::size_t alignment) const {
    return pageSize_ && length && length % pageSize_ == 0 &&
           alignment >= pageSize_ && (alignment & (alignment - 1)) == 0;
}

bool AlignedVmReservations::contains(std::uintptr_t address, std::size_t length) const {
    auto it = spans_.upper_bound(address);
    if (it == spans_.begin()) return false;
    --it;
    return address - it->first <= it->second && length <= it->second - (address - it->first);
}

void* AlignedVmReservations::allocate(std::size_t length, std::size_t alignment, void* hint) {
    if (!valid(length, alignment)) return nullptr;
    void* result = nullptr;
#if defined(__APPLE__)
    auto attempt = [&](void* requested) {
        mach_vm_address_t address = reinterpret_cast<mach_vm_address_t>(requested);
        const auto kr = mach_vm_map(mach_task_self(), &address, length, alignment - 1,
            VM_FLAGS_ANYWHERE, MACH_PORT_NULL, 0, FALSE, VM_PROT_NONE,
            VM_PROT_ALL, VM_INHERIT_DEFAULT);
        std::fprintf(stderr, "[KuDroidCage] map length=%zu alignment=%zu hint=%p result=%d base=0x%llx\n",
            length, alignment, requested, kr, static_cast<unsigned long long>(address));
        return kr == KERN_SUCCESS ? reinterpret_cast<void*>(address) : nullptr;
    };
    if (hint) result = attempt(hint);
    // The address is advisory; retain the exact size and alignment on retry.
    if (!result) result = attempt(nullptr);
#else
    if (length > SIZE_MAX - (alignment - pageSize_)) return nullptr;
    const std::size_t total = length + alignment - pageSize_;
    void* raw = ::mmap(hint, total, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (raw == MAP_FAILED) return nullptr;
    const auto base = reinterpret_cast<std::uintptr_t>(raw);
    if (base > UINTPTR_MAX - (alignment - 1)) { unmap(raw, total); return nullptr; }
    const auto aligned = (base + alignment - 1) & ~(alignment - 1);
    const std::size_t prefix = aligned - base;
    const std::size_t suffix = total - prefix - length;
    if (prefix && !unmap(raw, prefix)) { unmap(raw, total); return nullptr; }
    if (suffix && !unmap(reinterpret_cast<void*>(aligned + length), suffix)) {
        unmap(reinterpret_cast<void*>(aligned), total - prefix);
        return nullptr;
    }
    result = reinterpret_cast<void*>(aligned);
#endif
    const auto address = reinterpret_cast<std::uintptr_t>(result);
    if (!result || (address & (alignment - 1)) || address > UINTPTR_MAX - length) {
        if (result) unmap(result, length);
        return nullptr;
    }
    try {
        if (!spans_.emplace(address, length).second) { unmap(result, length); return nullptr; }
    } catch (...) {
        unmap(result, length);
        return nullptr;
    }
    return result;
}

bool AlignedVmReservations::prepare(std::size_t length, std::size_t alignment) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (prepared_) return preparedLength_ == length && preparedAlignment_ == alignment;
    prepared_ = allocate(length, alignment, nullptr);
    if (!prepared_) return false;
    preparedLength_ = length;
    preparedAlignment_ = alignment;
    return true;
}

void* AlignedVmReservations::reserve(std::size_t length, std::size_t alignment, void* hint) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (prepared_ && length == preparedLength_ && alignment == preparedAlignment_) {
        void* result = prepared_;
        prepared_ = nullptr;
        return result;
    }
    return allocate(length, alignment, hint);
}

bool AlignedVmReservations::release(void* pointer, std::size_t length) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pageSize_ || !length || address % pageSize_ || length % pageSize_ ||
        !contains(address, length)) return false;
    auto it = spans_.upper_bound(address);
    --it;
    // Tail releases preserve a single owned interval.
    if (address - it->first + length != it->second || !unmap(pointer, length)) return false;
    it->second -= length;
    if (prepared_ == reinterpret_cast<void*>(it->first)) prepared_ = nullptr;
    if (!it->second) spans_.erase(it);
    return true;
}

bool AlignedVmReservations::protect(void* pointer, std::size_t length, int permissions) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pageSize_ || address % pageSize_ || length % pageSize_ ||
        (permissions & ~7) || !contains(address, length)) return false;
    if (!length) return true;
    if (::mprotect(pointer, length, permissions) != 0) return false;
#if defined(__APPLE__)
    // Match the runtime's discard/reuse accounting after permission changes.
    ::madvise(pointer, length, permissions ? MADV_FREE_REUSE : MADV_FREE_REUSABLE);
#else
    if (!permissions) ::madvise(pointer, length, MADV_FREE);
#endif
    return true;
}

bool AlignedVmReservations::discard(void* pointer, std::size_t length) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pageSize_ || address % pageSize_ || length % pageSize_ || !contains(address, length))
        return false;
#if defined(__APPLE__)
    return !length || ::madvise(pointer, length, MADV_FREE_REUSABLE) == 0;
#else
    return !length || ::madvise(pointer, length, MADV_FREE) == 0;
#endif
}
}
