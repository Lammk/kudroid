#pragma once

#include <cstdint>
#include <cstddef>

namespace kudroid {

// Looks up FMOD Vorbis codebook entry for the given CRC32.
// Returns pointer to (&entry + 0x10) if found, nullptr otherwise.
const void* kudroid_fmod_vorbis_lookup(uint32_t crc);

// Arms the FMOD Vorbis fallback trap parameters.
void kudroid_arm_fmod_vorbis_trap(uintptr_t patchAddr, uintptr_t targetFound, uintptr_t targetAfter);

// Handles the BRK #0x464d trap when FMOD cannot find a Vorbis CRC in its builtin table.
bool bionic_handle_fmod_vorbis_trap(void* ucontext);

} // namespace kudroid
