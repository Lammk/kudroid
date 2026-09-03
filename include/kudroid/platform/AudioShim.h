#pragma once
#include "ShimDefs.h"

#include <cstdint>

namespace kudroid {
const SymbolEntry* get_audio_symbols(size_t* count);
}

// ── android.media.AudioTrack, for KuART ──────────────────────────────────────
//
// AudioTrack is the JAVA audio path, and it is the one FMOD uses: Unity games drive it
// from Java rather than calling OpenSL ES or AAudio from native code. So unlike every
// other symbol in this file, these are not reached by a guest relocation — KuART's
// LibCore calls them when the interpreter hits AudioTrack's native methods.
//
// They are declared here, rather than left as extern declarations at the LibCore call
// site, so the signatures are checked by the compiler in both places. A mismatched
// signature between a native binding and its implementation is a class of bug that
// otherwise only shows up as corrupt audio parameters at runtime.
//
// All of them share AudioPlayer and the host audio queue with the OpenSL ES and AAudio
// paths: one output, one callback, one frame counter. `track` is the handle returned by
// _create; 0 is never valid. Errors follow AudioTrack's own constants (-1 ERROR,
// -3 ERROR_INVALID_OPERATION, -6 ERROR_DEAD_OBJECT) so the Java side can pass them
// straight through.
extern "C" {

// Minimum workable buffer in bytes, or 0 if the parameters make no sense.
//
// `encoding` < 0 is a second query rather than a format: it asks for the device's
// preferred output rate, which is what AudioTrack.getNativeOutputSampleRate needs. One
// native method serves both so AudioTrack needs no extra binding for it.
int32_t bionic_kudroid_audiotrack_min_buffer_size(int32_t sampleRateInHz,
                                                  int32_t channelCount,
                                                  int32_t encoding);

// Open a player. Returns a handle, or 0 — which is what makes AudioTrack report
// STATE_UNINITIALIZED rather than claiming success and going silent.
int64_t bionic_kudroid_audiotrack_create(int32_t sampleRateInHz, int32_t channelCount,
                                         int32_t encoding, int32_t bufferSizeInBytes);

// Submit PCM. Returns the byte count accepted, or a negative AudioTrack error.
int32_t bionic_kudroid_audiotrack_write(int64_t track, const void* data, int32_t sizeInBytes);

int32_t bionic_kudroid_audiotrack_play(int64_t track);
int32_t bionic_kudroid_audiotrack_pause(int64_t track);
int32_t bionic_kudroid_audiotrack_stop(int64_t track);
int32_t bionic_kudroid_audiotrack_flush(int64_t track);
int32_t bionic_kudroid_audiotrack_release(int64_t track);
int32_t bionic_kudroid_audiotrack_set_volume(int64_t track, float volume);

// Frames the device has consumed, wrapping at 32 bits as the platform's does. FMOD sizes
// each write from the difference between successive reads, so this must keep climbing
// while audio plays — a frozen value reads as a dead device and it stops writing.
int32_t bionic_kudroid_audiotrack_head_position(int64_t track);

// Frames queued but not yet played. Used for smoothing, not for correctness.
int32_t bionic_kudroid_audiotrack_latency_frames(int64_t track);

}  // extern "C"
