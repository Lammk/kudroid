#pragma once

#include <cstddef>

namespace kudroid {

struct SymbolEntry;

// NDK media stack (libmediandk / libandroid tracing): ATrace, AMediaFormat,
// AMediaDataSource, AMediaExtractor, AMediaCodec.
//
// Games reach this through Unity's VideoPlayer (cutscenes, animated menus).
// Previously every one of these resolved to the universal dummy (returned 0),
// so AMediaExtractor_new "succeeded" with a null handle and the first real call
// dereferenced it. This provides honest semantics instead: a working
// AMediaFormat key-value store, valid opaque handles, and an extractor that
// reports zero tracks, so playback fails cleanly (error callback) instead of
// crashing. No decoding is attempted — that needs a platform media pipeline.
const SymbolEntry* get_media_symbols(size_t* count);

} // namespace kudroid
