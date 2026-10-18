#pragma once

#include <cstddef>

extern "C" {
// Frames a media codec rendered to a SurfaceTexture, queued by the codec thread and
// taken by the main Looper, which tells the Java texture. Returns 0 when none are left.
void kudroid_media_frame_post(int textureId, long long timestampNs);
int kudroid_media_frame_take(int* textureId, long long* timestampNs);
}

namespace kudroid {

struct SymbolEntry;

// NDK media extraction and CPU buffer decoding for VP8/Vorbis.
const SymbolEntry* get_media_symbols(size_t* count);

} // namespace kudroid
