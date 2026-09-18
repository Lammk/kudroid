#pragma once

// AArch64 fault-instruction skip: pure decode half, platform-typeless so host
// unit tests drive it (see fault_decode_skip). The signal-context wrapper
// lives in kudroid_bridge.cpp behind the Apple/arm64 gate; this file has no
// platform dependencies and compiles everywhere.
#include <cstdint>

namespace kudroid {

// What skipping a faulting word means. effAddr must equal si_addr or the
// caller refuses (a decode that does not explain the fault is a mis-decode),
// except inside the first page where a faulting multi-register SIMD access
// may report the page instead of the byte.
struct FaultSkipPlan {
    bool skippable = false;
    bool isLoad = false;   // false = store/PRFM: no register is touched
    bool isPair = false;   // LDP: rt2 is a second destination
    bool isVector = false;  // SIMD/FP: rt names __ns.__v, not __x
    unsigned rt = 31;
    unsigned rt2 = 31;
    unsigned rtCount = 1;  // vector structure loads: consecutive dests rt..rt+rtCount-1
    uint64_t effAddr = 0;
};

// True when `nextWord` is a register branch — BR/BLR/RET and every
// pointer-authenticated form (BRAA/BRAB/BLRAA/BLRAB/RETAA/RETAB) — whose target
// register is `reg`. A skip that fabricates a load result into `reg` must not
// resume on such an instruction: the fabricated value becomes the callee
// address, so the thread jumps to it instead of reporting the real fault.
bool fault_skip_branches_through(uint32_t nextWord, unsigned reg);

// Decode one AArch64 word for the skip rules documented in kudroid_bridge.cpp:
// plain integer loads/stores (unsigned/unscaled immediate, register offset,
// literal, signed-offset pairs), SIMD/FP single transfers (same addressing
// modes; Rt names a vector register), SIMD whole-register structure transfers
// (LD1/ST1 x1-4, LD2/ST2, LD3/ST3, LD4/ST4, LD1R; no-offset only, consecutive
// dests in rtCount), and PRFM. Everything else — writeback forms, exclusives,
// LSE atomics, SIMD lane-indexed and pair forms, branches — reports unskippable.
// baseVal/rmVal are the values the addressing mode reads (SP for Rn==31,
// 0 for XZR); the caller supplies them from the signal context.
FaultSkipPlan fault_decode_skip(uint32_t w, uint64_t pc, uint64_t baseVal,
                                uint64_t rmVal);

}  // namespace kudroid
