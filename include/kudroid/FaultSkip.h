#pragma once

// AArch64 fault-instruction skip: pure decode half, platform-typeless so host
// unit tests drive it (see fault_decode_skip). The signal-context wrapper
// lives in kudroid_bridge.cpp behind the Apple/arm64 gate; this file has no
// platform dependencies and compiles everywhere.
#include <cstdint>

namespace kudroid {

// What skipping a faulting word means. effAddr must equal si_addr or the
// caller refuses (a decode that does not explain the fault is a mis-decode).
struct FaultSkipPlan {
    bool skippable = false;
    bool isLoad = false;   // false = store/PRFM: no register is touched
    bool isPair = false;   // LDP: rt2 is a second destination
    unsigned rt = 31;
    unsigned rt2 = 31;
    uint64_t effAddr = 0;
};

// Decode one AArch64 word for the skip rules documented in kudroid_bridge.cpp:
// plain integer loads/stores (unsigned/unscaled immediate, register offset,
// literal, signed-offset pairs) and PRFM. Everything else — writeback forms,
// exclusives, LSE atomics, SIMD/FP lanes, branches — reports unskippable.
// baseVal/rmVal are the values the addressing mode reads (SP for Rn==31,
// 0 for XZR); the caller supplies them from the signal context.
FaultSkipPlan fault_decode_skip(uint32_t w, uint64_t pc, uint64_t baseVal,
                                uint64_t rmVal);

}  // namespace kudroid
