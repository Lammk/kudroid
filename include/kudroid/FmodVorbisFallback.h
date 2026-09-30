#pragma once

#include <cstdint>
#include <cstddef>

namespace kudroid {
class ElfLoader;

// Installs FMOD Vorbis codebook fallback if module contains FSB5 Vorbis decoder.
void install_fmod_vorbis_fallback(ElfLoader* loader);

} // namespace kudroid
