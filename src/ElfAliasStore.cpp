#include "kudroid/ElfAliasStore.h"

#include <cstring>
#include <limits>

namespace kudroid {
namespace {
ElfWritableAliases aliases;
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);

int64_t SignedImmediate(uint32_t value, unsigned bits) {
    return (value & (1u << (bits - 1))) ? int64_t(value) - (int64_t(1) << bits) : value;
}

void StoreLane(void* destination, const void* source, unsigned bytes) {
    uint64_t value = 0;
    if (bytes <= 8) std::memcpy(&value, source, bytes);
    if ((reinterpret_cast<uintptr_t>(destination) & (bytes - 1)) == 0) {
        switch (bytes) {
            case 1: __atomic_store_n(static_cast<uint8_t*>(destination), uint8_t(value), __ATOMIC_RELAXED); return;
            case 2: __atomic_store_n(static_cast<uint16_t*>(destination), uint16_t(value), __ATOMIC_RELAXED); return;
            case 4: __atomic_store_n(static_cast<uint32_t*>(destination), uint32_t(value), __ATOMIC_RELAXED); return;
            case 8: __atomic_store_n(static_cast<uint64_t*>(destination), value, __ATOMIC_RELAXED); return;
        }
    }
#if defined(__aarch64__) || defined(__arm64__)
    if (bytes == 16) {
        using Vector = uint8_t __attribute__((vector_size(16)));
        Vector value;
        std::memcpy(&value, source, 16);
        __asm__ volatile("str %q0, [%1]" :: "w"(value), "r"(destination) : "memory");
        return;
    }
#endif
    std::memcpy(destination, source, bytes);
}

void StorePair(void* destination, const void* first, const void* second,
               unsigned bytes, bool vector) {
#if defined(__aarch64__) || defined(__arm64__)
    if (!vector) {
        uint64_t a = 0, b = 0;
        std::memcpy(&a, first, bytes);
        std::memcpy(&b, second, bytes);
        if (bytes == 8) {
            __asm__ volatile("stp %x0, %x1, [%2]" :: "r"(a), "r"(b), "r"(destination) : "memory");
        } else {
            __asm__ volatile("stp %w0, %w1, [%2]" :: "r"(a), "r"(b), "r"(destination) : "memory");
        }
        return;
    }
    using Vector = uint8_t __attribute__((vector_size(16)));
    Vector a{}, b{};
    std::memcpy(&a, first, bytes);
    std::memcpy(&b, second, bytes);
    if (bytes == 16) {
        __asm__ volatile("stp %q0, %q1, [%2]" :: "w"(a), "w"(b), "r"(destination) : "memory");
    } else if (bytes == 8) {
        __asm__ volatile("stp %d0, %d1, [%2]" :: "w"(a), "w"(b), "r"(destination) : "memory");
    } else {
        __asm__ volatile("stp %s0, %s1, [%2]" :: "w"(a), "w"(b), "r"(destination) : "memory");
    }
#else
    (void)vector;
    StoreLane(destination, first, bytes);
    StoreLane(static_cast<char*>(destination) + bytes, second, bytes);
#endif
}
}  // namespace

Arm64StorePlan DecodeArm64Store(uint32_t w, const Arm64StoreState& state) {
    Arm64StorePlan p;
    p.base = (w >> 5) & 31;
    p.first = w & 31;
    p.vector = (w & (1u << 26)) != 0;
    const uint64_t base = state.x[p.base];
    if (p.base == 31 && (base & 15)) return p;
    const unsigned size = w >> 30;
    if ((w & 0x3A000000u) == 0x38000000u) {
        const unsigned opc = (w >> 22) & 3;
        if ((!p.vector && opc != 0) || (p.vector && opc != 0 && opc != 2)) return p;
        if (p.vector && opc == 2 && size != 0) return p;
        const unsigned scale = p.vector && opc == 2 ? 4 : size;
        p.bytes = 1u << scale;
        if (w & (1u << 24)) {
            p.address = base + (uint64_t((w >> 10) & 0xFFF) << scale);
        } else if (w & (1u << 21)) {
            if (((w >> 10) & 3) != 2) return p;
            const unsigned rm = (w >> 16) & 31;
            uint64_t index = rm == 31 ? 0 : state.x[rm];
            switch ((w >> 13) & 7) {
                case 2: index = uint32_t(index); break;
                case 3: break;
                case 6: index = uint64_t(int64_t(int32_t(index))); break;
                case 7: break;
                default: return p;
            }
            p.address = base + (index << ((w & (1u << 12)) ? scale : 0));
        } else {
            const unsigned mode = (w >> 10) & 3;
            if (p.vector && mode == 2) return p;
            const int64_t offset = SignedImmediate((w >> 12) & 0x1FF, 9);
            p.baseAfter = base + offset;
            p.address = mode == 1 ? base : p.baseAfter;
            p.writeback = mode == 1 || mode == 3;
        }
    } else if ((w & 0x3A000000u) == 0x28000000u) {
        if (w & (1u << 22)) return p;
        if ((p.vector && size == 3) || (!p.vector && size != 0 && size != 2)) return p;
        p.pair = true;
        p.second = (w >> 10) & 31;
        p.bytes = p.vector ? (4u << size) : (size == 2 ? 8 : 4);
        const unsigned mode = (w >> 23) & 3;
        const int64_t offset = SignedImmediate((w >> 15) & 0x7F, 7) * p.bytes;
        p.baseAfter = base + offset;
        p.address = mode == 1 ? base : p.baseAfter;
        p.writeback = mode == 1 || mode == 3;
    } else {
        return p;
    }
    // Base/source overlap with writeback has constrained-unpredictable semantics.
    if (p.writeback && !p.vector && p.base != 31 &&
        (p.base == p.first || (p.pair && p.base == p.second))) return p;
    p.valid = true;
    return p;
}

bool ElfWritableAliases::Add(uintptr_t owner, uintptr_t guest, uintptr_t write, size_t size,
                            uintptr_t faultStart, size_t faultSize) {
    if (!owner || !guest || !write || guest == write || size == 0 ||
        size > std::numeric_limits<uintptr_t>::max() - guest ||
        size > std::numeric_limits<uintptr_t>::max() - write) return false;
    if (faultStart == 0 && faultSize == 0) { faultStart = guest; faultSize = size; }
    if (faultStart < guest || faultStart - guest >= size || faultSize == 0 ||
        faultSize > size - (faultStart - guest)) return false;
    unsigned n = count_.load(std::memory_order_relaxed);
    do {
        if (n >= kCapacity) return false;
    } while (!count_.compare_exchange_weak(n, n + 1, std::memory_order_relaxed));
    auto& entry = entries_[n];
    entry.owner = owner;
    entry.guest = guest;
    entry.write = write;
    entry.size = size;
    entry.faultStart = faultStart;
    entry.faultSize = faultSize;
    entry.active.store(true, std::memory_order_release);
    return true;
}

void ElfWritableAliases::Remove(uintptr_t owner) {
    const unsigned n = count_.load(std::memory_order_relaxed);
    for (unsigned i = 0; i < n; ++i) {
        auto& entry = entries_[i];
        if (entry.active.load(std::memory_order_acquire) && entry.owner == owner) {
            entry.active.store(false, std::memory_order_release);
        }
    }
}

void ElfWritableAliases::RemoveOverlapping(uintptr_t address, size_t size) {
    if (size == 0 || size > UINTPTR_MAX - address) return;
    const unsigned n = count_.load(std::memory_order_relaxed);
    for (unsigned i = 0; i < n; ++i) {
        auto& entry = entries_[i];
        if (entry.active.load(std::memory_order_acquire) &&
            address < entry.guest + entry.size && entry.guest < address + size) {
            entry.active.store(false, std::memory_order_release);
        }
    }
}

void* ElfWritableAliases::Find(uintptr_t address, size_t size) const {
    if (size == 0) return nullptr;
    const unsigned n = count_.load(std::memory_order_relaxed);
    for (unsigned i = 0; i < n; ++i) {
        const auto& entry = entries_[i];
        if (!entry.active.load(std::memory_order_acquire)) continue;
        if (address >= entry.guest && address - entry.guest < entry.size &&
            size <= entry.size - (address - entry.guest)) {
            return reinterpret_cast<void*>(entry.write + address - entry.guest);
        }
    }
    return nullptr;
}

bool ElfWritableAliases::ContainsFault(uintptr_t address) const {
    const unsigned n = count_.load(std::memory_order_relaxed);
    for (unsigned i = 0; i < n; ++i) {
        const auto& entry = entries_[i];
        if (entry.active.load(std::memory_order_acquire) && address >= entry.faultStart &&
            address - entry.faultStart < entry.faultSize) return true;
    }
    return false;
}

ElfWritableAliases& GuestElfWritableAliases() { return aliases; }

bool EmulateElfAliasStore(uint32_t instruction, uintptr_t fault,
                         Arm64StoreState& state, const ElfWritableAliases& aliases) {
    const auto plan = DecodeArm64Store(instruction, state);
    if (!plan.valid || !aliases.ContainsFault(fault)) return false;
    const size_t size = plan.bytes * (plan.pair ? 2 : 1);
    // A crossing transfer can fault on any byte in its access range.
    if (fault < plan.address || fault - plan.address >= size) return false;
    void* destination = aliases.Find(plan.address, size);
    if (!destination) return false;
    const uint64_t zero = 0;
    const void* first = plan.vector ? static_cast<const void*>(state.v[plan.first])
                       : plan.first == 31 ? &zero : &state.x[plan.first];
    if (plan.pair) {
        const void* second = plan.vector ? static_cast<const void*>(state.v[plan.second])
                            : plan.second == 31 ? &zero : &state.x[plan.second];
        StorePair(destination, first, second, plan.bytes, plan.vector);
    } else {
        StoreLane(destination, first, plan.bytes);
    }
    if (plan.writeback) state.x[plan.base] = plan.baseAfter;
    state.pc += 4;
    return true;
}
}  // namespace kudroid
