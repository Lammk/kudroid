#pragma once

#include <cstdint>
#include <cstddef>

namespace kudroid {

// Looks up FMOD Vorbis codebook entry for the given CRC32.
// Returns pointer to (&entry + 0x10) if found, nullptr otherwise.
const void* kudroid_fmod_vorbis_lookup(uint32_t crc);

// Arms the FMOD Vorbis fallback trap. patchAddr is the BRK site, targetFound the
// decoder's "setup found" continuation, resumeAfter where execution continues when
// the fallback has no entry, origInst the instruction the BRK replaced (emulated on
// a miss) and probe true for the lookup loop head, which only reports the asked CRC.
// Returns false when the fixed trap-site table is full; callers must not patch
// a BRK that the signal handler cannot identify.
bool kudroid_arm_fmod_vorbis_trap(uintptr_t patchAddr, uintptr_t targetFound,
                                  uintptr_t resumeAfter, uint32_t origInst, bool probe);

// Handles the BRK #0x464d trap when FMOD cannot find a Vorbis CRC in its builtin table.
bool bionic_handle_fmod_vorbis_trap(void* ucontext);

} // namespace kudroid
