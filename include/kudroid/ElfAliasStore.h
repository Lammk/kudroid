#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace kudroid {

struct Arm64StoreState {
    uint64_t x[32]{};  // x[31] is SP for addressing.
    alignas(16) uint8_t v[32][16]{};
    uint64_t pc = 0;
};

struct Arm64StorePlan {
    uint64_t address = 0;
    uint64_t baseAfter = 0;
    unsigned base = 0, first = 0, second = 0, bytes = 0;
    bool valid = false, vector = false, pair = false, writeback = false;
};

Arm64StorePlan DecodeArm64Store(uint32_t instruction, const Arm64StoreState& state);

// Entries are immutable after publication and never reused by signal readers.
class ElfWritableAliases {
public:
    bool Add(uintptr_t owner, uintptr_t guest, uintptr_t write, size_t size,
             uintptr_t faultStart = 0, size_t faultSize = 0);
    void Remove(uintptr_t owner);
    void RemoveOverlapping(uintptr_t address, size_t size);
    void* Find(uintptr_t address, size_t size) const;
    bool ContainsFault(uintptr_t address) const;

private:
    struct Entry {
        std::atomic<bool> active{false};
        uintptr_t owner = 0, guest = 0, write = 0;
        uintptr_t faultStart = 0;
        size_t size = 0;
        size_t faultSize = 0;
    };
    static constexpr unsigned kCapacity = 4096;
    Entry entries_[kCapacity]{};
    std::atomic<unsigned> count_{0};
};

ElfWritableAliases& GuestElfWritableAliases();
bool EmulateElfAliasStore(uint32_t instruction, uintptr_t fault,
                         Arm64StoreState& state, const ElfWritableAliases& aliases);

}  // namespace kudroid
