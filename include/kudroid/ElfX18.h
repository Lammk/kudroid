#pragma once

// AOT x18 rewrite: Darwin reserves x18 as a platform register and does not
// preserve it (async delivery zeroes it), while Android NDK code uses it as a
// normal scratch register. Any guest x18 value live across a delivery dies,
// faulting later at an unrelated instruction (observed: mixer zero-fill loop).
//
// This pass renames every x18 reference inside known functions to a register
// that is unused in that function, so x18 is never observed live. It runs at
// load time on mapped RW memory, before protections are applied, for every
// guest .so. Generic across apps: no per-app offsets or behavior.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "kudroid/elf_loader.hpp"

namespace kudroid {

// Per-library rewrite statistics, logged once per .so.
struct X18Stats {
    std::uint64_t functions = 0;      // FDE ranges processed
    std::uint64_t rewritten = 0;      // functions with x18 renamed
    std::uint64_t sites = 0;          // x18 references renamed
    std::uint64_t skippedNoReg = 0;   // no free caller-saved register / spill+call
    std::uint64_t skippedEh = 0;      // personality (landing pads restore regs)
    std::uint64_t skippedUnknown = 0;  // undecodable instruction w/ possible x18
    std::uint64_t skippedRange = 0;   // bytes outside known executable ranges
};

// Rewrite x18 uses in EXEC segments to per-function free registers.
// base is the loader's mapping (pre-adjust: base[vaddr-minVaddr]); fileData
// locates .eh_frame via section headers. RW memory required.
X18Stats elf_x18_rewrite(const void* base, std::uint64_t minVaddr,
                         const std::vector<ElfLoader::Segment>& segments,
                         const std::uint8_t* fileData, std::size_t fileSize);

// Kill-switch: KUDROID_X18_REWRITE=0 disables the pass (logs once).
bool elf_x18_enabled();

}  // namespace kudroid
