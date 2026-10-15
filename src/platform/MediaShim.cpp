#include "kudroid/platform/MediaShim.h"
#include "kudroid/platform/ShimDefs.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace kudroid {
namespace {

// NDK media_status_t: 0 is AMEDIA_OK. Dequeue calls return an index or -1
// (TRY_AGAIN_LATER), which the universal dummy already returned by accident —
// the difference here is valid handles and written out-params instead of nulls.
constexpr int kMediaOk = 0;
constexpr int kDequeueTryAgain = -1;

// ── ATrace (android/trace.h) ────────────────────────────────────────────────
// Tracing is a host-side no-op; isEnabled false so guests skip trace payloads.

extern "C" void bionic_ATrace_beginSection(const char* /*sectionName*/) {}
extern "C" void bionic_ATrace_endSection() {}
extern "C" bool bionic_ATrace_isEnabled() { return false; }
extern "C" void bionic_ATrace_setCounter(const char* /*counterName*/, int64_t /*counterValue*/) {}

// ── AMEDIAFORMAT_KEY_* (NdkMediaFormat.h data symbols) ──────────────────────
// Exported `const char*` constants, NOT functions: bind the address of our copy
// so the guest reads a valid C string (same precedent as stderr/stdout).
// Values are the NDK's documented key strings.

#define MEDIA_KEY(name, value)                       \
    static const char* kMediaKey_##name = value;     \
    static_assert(true, "")

MEDIA_KEY(AAC_PROFILE, "aac-profile");
MEDIA_KEY(BIT_RATE, "bitrate");
MEDIA_KEY(CHANNEL_COUNT, "channel-count");
MEDIA_KEY(CHANNEL_MASK, "channel-mask");
MEDIA_KEY(COLOR_FORMAT, "color-format");
MEDIA_KEY(COLOR_RANGE, "color-range");
MEDIA_KEY(COLOR_STANDARD, "color-standard");
MEDIA_KEY(DURATION, "duration-us");
MEDIA_KEY(FLAC_COMPRESSION_LEVEL, "flac-compression-level");
MEDIA_KEY(FRAME_RATE, "frame-rate");
MEDIA_KEY(HEIGHT, "height");
MEDIA_KEY(I_FRAME_INTERVAL, "i-frame-interval");
MEDIA_KEY(IS_ADTS, "is-adts");
MEDIA_KEY(IS_AUTOSELECT, "is-autoselect");
MEDIA_KEY(IS_DEFAULT, "is-default");
MEDIA_KEY(IS_FORCED_SUBTITLE, "is-forced-subtitle");
MEDIA_KEY(LANGUAGE, "language");
MEDIA_KEY(MAX_HEIGHT, "max-height");
MEDIA_KEY(MAX_INPUT_SIZE, "max-input-size");
MEDIA_KEY(MAX_WIDTH, "max-width");
MEDIA_KEY(MIME, "mime");
MEDIA_KEY(PUSH_BLANK_BUFFERS_ON_STOP, "push-blank-buffers-on-stop");
MEDIA_KEY(REPEAT_PREVIOUS_FRAME_AFTER, "repeat-previous-frame-after");
MEDIA_KEY(ROTATION, "rotation-degrees");
MEDIA_KEY(SAMPLE_RATE, "sample-rate");
MEDIA_KEY(SLICE_HEIGHT, "slice-height");
MEDIA_KEY(STRIDE, "stride");
MEDIA_KEY(WIDTH, "width");

// ── AMediaFormat (working key-value store) ──────────────────────────────────

struct GuestMediaFormat {
    std::map<std::string, int32_t> i32;
    std::map<std::string, int64_t> i64;
    std::map<std::string, float> flt;
    std::map<std::string, std::string> str;
    std::map<std::string, std::vector<uint8_t>> buf;
    std::string cached_string;
};

extern "C" GuestMediaFormat* bionic_AMediaFormat_new() { return new GuestMediaFormat(); }
extern "C" int bionic_AMediaFormat_delete(GuestMediaFormat* format) {
    delete format;
    return kMediaOk;
}
extern "C" void bionic_AMediaFormat_setInt32(GuestMediaFormat* format, const char* name, int32_t value) {
    if (format != nullptr && name != nullptr) format->i32[name] = value;
}
extern "C" void bionic_AMediaFormat_setInt64(GuestMediaFormat* format, const char* name, int64_t value) {
    if (format != nullptr && name != nullptr) format->i64[name] = value;
}
extern "C" void bionic_AMediaFormat_setFloat(GuestMediaFormat* format, const char* name, float value) {
    if (format != nullptr && name != nullptr) format->flt[name] = value;
}
extern "C" void bionic_AMediaFormat_setString(GuestMediaFormat* format, const char* name, const char* value) {
    if (format != nullptr && name != nullptr) format->str[name] = value != nullptr ? value : "";
}
extern "C" void bionic_AMediaFormat_setBuffer(GuestMediaFormat* format, const char* name,
                                              const void* data, size_t size) {
    if (format == nullptr || name == nullptr) return;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    format->buf[name] = std::vector<uint8_t>(bytes, bytes + (data != nullptr ? size : 0));
}
extern "C" bool bionic_AMediaFormat_getInt32(GuestMediaFormat* format, const char* name, int32_t* out) {
    if (format == nullptr || name == nullptr || out == nullptr) return false;
    auto it = format->i32.find(name);
    if (it == format->i32.end()) return false;
    *out = it->second;
    return true;
}
extern "C" bool bionic_AMediaFormat_getInt64(GuestMediaFormat* format, const char* name, int64_t* out) {
    if (format == nullptr || name == nullptr || out == nullptr) return false;
    auto it = format->i64.find(name);
    if (it == format->i64.end()) return false;
    *out = it->second;
    return true;
}
extern "C" bool bionic_AMediaFormat_getFloat(GuestMediaFormat* format, const char* name, float* out) {
    if (format == nullptr || name == nullptr || out == nullptr) return false;
    auto it = format->flt.find(name);
    if (it == format->flt.end()) return false;
    *out = it->second;
    return true;
}
extern "C" bool bionic_AMediaFormat_getString(GuestMediaFormat* format, const char* name, const char** out) {
    if (format == nullptr || name == nullptr || out == nullptr) return false;
    auto it = format->str.find(name);
    if (it == format->str.end()) return false;
    *out = it->second.c_str();
    return true;
}
extern "C" bool bionic_AMediaFormat_getBuffer(GuestMediaFormat* format, const char* name,
                                              void** data, size_t* size) {
    if (format == nullptr || name == nullptr || data == nullptr || size == nullptr) return false;
    auto it = format->buf.find(name);
    if (it == format->buf.end()) return false;
    *data = it->second.data();
    *size = it->second.size();
    return true;
}
extern "C" const char* bionic_AMediaFormat_toString(GuestMediaFormat* format) {
    if (format == nullptr) return "";
    std::string out = "{";
    bool first = true;
    auto emit = [&](const char* key, const std::string& value) {
        if (!first) out += ", ";
        first = false;
        out += key;
        out += "=";
        out += value;
    };
    for (const auto& kv : format->i32) emit(kv.first.c_str(), std::to_string(kv.second));
    for (const auto& kv : format->i64) emit(kv.first.c_str(), std::to_string(kv.second));
    for (const auto& kv : format->str) emit(kv.first.c_str(), kv.second);
    out += "}";
    format->cached_string = out;
    return format->cached_string.c_str();
}

// ── AMediaDataSource (callback holder; reads never served) ──────────────────

struct GuestMediaDataSource {
    void* userdata = nullptr;
    void* readAt = nullptr;
    void* getSize = nullptr;
    void* close = nullptr;
};

extern "C" GuestMediaDataSource* bionic_AMediaDataSource_new() { return new GuestMediaDataSource(); }
extern "C" int bionic_AMediaDataSource_delete(GuestMediaDataSource* source) {
    delete source;
    return kMediaOk;
}
extern "C" void bionic_AMediaDataSource_setUserdata(GuestMediaDataSource* source, void* userdata) {
    if (source != nullptr) source->userdata = userdata;
}
extern "C" void bionic_AMediaDataSource_setReadAt(GuestMediaDataSource* source, void* readAt) {
    if (source != nullptr) source->readAt = readAt;
}
extern "C" void bionic_AMediaDataSource_setGetSize(GuestMediaDataSource* source, void* getSize) {
    if (source != nullptr) source->getSize = getSize;
}
extern "C" void bionic_AMediaDataSource_setClose(GuestMediaDataSource* source, void* close) {
    if (source != nullptr) source->close = close;
}

// ── AMediaExtractor (honest-empty: zero tracks, EOF everywhere) ─────────────
// No container parsing exists on this side, so any source selects nothing.
// Getters return the NDK's empty/error sentinels; the player fails cleanly
// instead of dereferencing the dummy's null handle.

struct GuestMediaExtractor {
    int fd = -1;
};

extern "C" GuestMediaExtractor* bionic_AMediaExtractor_new() { return new GuestMediaExtractor(); }
extern "C" int bionic_AMediaExtractor_delete(GuestMediaExtractor* extractor) {
    delete extractor;
    return kMediaOk;
}
extern "C" int bionic_AMediaExtractor_setDataSourceFd(GuestMediaExtractor* extractor, int fd,
                                                      int64_t /*offset*/, int64_t /*length*/) {
    if (extractor == nullptr) return -1;
    extractor->fd = fd;
    return kMediaOk;
}
extern "C" int bionic_AMediaExtractor_setDataSource(GuestMediaExtractor* extractor, const char* /*path*/) {
    if (extractor == nullptr) return -1;
    return kMediaOk;
}
extern "C" int bionic_AMediaExtractor_setDataSourceCustom(GuestMediaExtractor* extractor,
                                                          GuestMediaDataSource* /*source*/) {
    if (extractor == nullptr) return -1;
    return kMediaOk;
}
extern "C" size_t bionic_AMediaExtractor_getTrackCount(GuestMediaExtractor* /*extractor*/) { return 0; }
extern "C" GuestMediaFormat* bionic_AMediaExtractor_getTrackFormat(GuestMediaExtractor* /*extractor*/,
                                                                   size_t /*idx*/) {
    return nullptr;
}
extern "C" int bionic_AMediaExtractor_selectTrack(GuestMediaExtractor* extractor, size_t /*idx*/) {
    // Zero tracks exist, so any select is an error: succeeding would let the
    // guest drive a codec and spin on TRY_AGAIN forever instead of taking its
    // unsupported path.
    (void)extractor;
    return -1;
}
extern "C" int bionic_AMediaExtractor_unselectTrack(GuestMediaExtractor* extractor, size_t /*idx*/) {
    return extractor != nullptr ? kMediaOk : -1;
}
extern "C" bool bionic_AMediaExtractor_advance(GuestMediaExtractor* /*extractor*/) { return false; }
extern "C" int bionic_AMediaExtractor_readSampleData(GuestMediaExtractor* /*extractor*/,
                                                     uint8_t* /*buffer*/, size_t /*capacity*/) {
    return -1;
}
extern "C" int bionic_AMediaExtractor_getSampleTrackIndex(GuestMediaExtractor* /*extractor*/) { return -1; }
extern "C" int64_t bionic_AMediaExtractor_getSampleTime(GuestMediaExtractor* /*extractor*/) { return -1; }
extern "C" uint32_t bionic_AMediaExtractor_getSampleFlags(GuestMediaExtractor* /*extractor*/) { return 0; }
extern "C" int bionic_AMediaExtractor_seekTo(GuestMediaExtractor* extractor, int64_t /*timeUs*/,
                                             uint32_t /*mode*/) {
    return extractor != nullptr ? kMediaOk : -1;
}

// ── AMediaCodec (valid handles, TRY_AGAIN forever) ──────────────────────────
// With zero extractor tracks the codec is never driven; if one is, dequeue
// reports no buffers rather than crashing, and configure/start/stop succeed.

struct GuestMediaCodec {
    std::string name;
    GuestMediaFormat* cachedFormat = nullptr;
};

extern "C" GuestMediaCodec* bionic_AMediaCodec_createDecoderByType(const char* mime) {
    auto* codec = new GuestMediaCodec();
    if (mime != nullptr) codec->name = mime;
    return codec;
}
extern "C" GuestMediaCodec* bionic_AMediaCodec_createEncoderByType(const char* mime) {
    auto* codec = new GuestMediaCodec();
    if (mime != nullptr) codec->name = mime;
    return codec;
}
extern "C" GuestMediaCodec* bionic_AMediaCodec_createCodecByName(const char* name) {
    auto* codec = new GuestMediaCodec();
    if (name != nullptr) codec->name = name;
    return codec;
}
extern "C" int bionic_AMediaCodec_delete(GuestMediaCodec* codec) {
    if (codec != nullptr) delete codec->cachedFormat;
    delete codec;
    return kMediaOk;
}
extern "C" int bionic_AMediaCodec_configure(GuestMediaCodec* codec, const GuestMediaFormat* /*format*/,
                                            void* /*surface*/, void* /*crypto*/, uint32_t /*flags*/) {
    return codec != nullptr ? kMediaOk : -1;
}
extern "C" int bionic_AMediaCodec_start(GuestMediaCodec* codec) {
    return codec != nullptr ? kMediaOk : -1;
}
extern "C" int bionic_AMediaCodec_stop(GuestMediaCodec* codec) {
    return codec != nullptr ? kMediaOk : -1;
}
extern "C" int bionic_AMediaCodec_flush(GuestMediaCodec* codec) {
    return codec != nullptr ? kMediaOk : -1;
}
extern "C" int bionic_AMediaCodec_queueInputBuffer(GuestMediaCodec* codec, size_t /*idx*/, int64_t /*offset*/,
                                                   size_t /*size*/, uint64_t /*time*/,
                                                   uint32_t /*flags*/) {
    return codec != nullptr ? kMediaOk : -1;
}
extern "C" int bionic_AMediaCodec_dequeueInputBuffer(GuestMediaCodec* /*codec*/,
                                                     int64_t /*timeoutUs*/) {
    return kDequeueTryAgain;
}
extern "C" int bionic_AMediaCodec_dequeueOutputBuffer(GuestMediaCodec* /*codec*/, void* /*info*/,
                                                      int64_t /*timeoutUs*/) {
    return kDequeueTryAgain;
}
extern "C" int bionic_AMediaCodec_releaseOutputBuffer(GuestMediaCodec* codec, size_t /*idx*/,
                                                      bool /*render*/) {
    return codec != nullptr ? kMediaOk : -1;
}
extern "C" int bionic_AMediaCodec_releaseOutputBufferAtTime(GuestMediaCodec* codec, size_t /*idx*/,
                                                            int64_t /*timestampNs*/) {
    return codec != nullptr ? kMediaOk : -1;
}
extern "C" uint8_t* bionic_AMediaCodec_getInputBuffer(GuestMediaCodec* /*codec*/, size_t /*idx*/,
                                                      size_t* outSize) {
    if (outSize != nullptr) *outSize = 0;
    return nullptr;
}
extern "C" uint8_t* bionic_AMediaCodec_getOutputBuffer(GuestMediaCodec* /*codec*/, size_t /*idx*/,
                                                       size_t* outSize) {
    if (outSize != nullptr) *outSize = 0;
    return nullptr;
}
extern "C" GuestMediaFormat* bionic_AMediaCodec_getOutputFormat(GuestMediaCodec* codec) {
    if (codec == nullptr) return nullptr;
    // One owned format per codec, not per call: per-frame pollers would leak
    // a format every frame otherwise.
    if (codec->cachedFormat == nullptr) codec->cachedFormat = new GuestMediaFormat();
    return codec->cachedFormat;
}

#define MEDIA_FN(name) {#name, reinterpret_cast<void*>(&bionic_##name)}
#define MEDIA_KEY_ENTRY(name) {"AMEDIAFORMAT_KEY_" #name, reinterpret_cast<void*>(&kMediaKey_##name)}

static const SymbolEntry kMediaSymbols[] = {
    MEDIA_FN(ATrace_beginSection),
    MEDIA_FN(ATrace_endSection),
    MEDIA_FN(ATrace_isEnabled),
    MEDIA_FN(ATrace_setCounter),
    MEDIA_KEY_ENTRY(AAC_PROFILE),
    MEDIA_KEY_ENTRY(BIT_RATE),
    MEDIA_KEY_ENTRY(CHANNEL_COUNT),
    MEDIA_KEY_ENTRY(CHANNEL_MASK),
    MEDIA_KEY_ENTRY(COLOR_FORMAT),
    MEDIA_KEY_ENTRY(COLOR_RANGE),
    MEDIA_KEY_ENTRY(COLOR_STANDARD),
    MEDIA_KEY_ENTRY(DURATION),
    MEDIA_KEY_ENTRY(FLAC_COMPRESSION_LEVEL),
    MEDIA_KEY_ENTRY(FRAME_RATE),
    MEDIA_KEY_ENTRY(HEIGHT),
    MEDIA_KEY_ENTRY(I_FRAME_INTERVAL),
    MEDIA_KEY_ENTRY(IS_ADTS),
    MEDIA_KEY_ENTRY(IS_AUTOSELECT),
    MEDIA_KEY_ENTRY(IS_DEFAULT),
    MEDIA_KEY_ENTRY(IS_FORCED_SUBTITLE),
    MEDIA_KEY_ENTRY(LANGUAGE),
    MEDIA_KEY_ENTRY(MAX_HEIGHT),
    MEDIA_KEY_ENTRY(MAX_INPUT_SIZE),
    MEDIA_KEY_ENTRY(MAX_WIDTH),
    MEDIA_KEY_ENTRY(MIME),
    MEDIA_KEY_ENTRY(PUSH_BLANK_BUFFERS_ON_STOP),
    MEDIA_KEY_ENTRY(REPEAT_PREVIOUS_FRAME_AFTER),
    MEDIA_KEY_ENTRY(ROTATION),
    MEDIA_KEY_ENTRY(SAMPLE_RATE),
    MEDIA_KEY_ENTRY(SLICE_HEIGHT),
    MEDIA_KEY_ENTRY(STRIDE),
    MEDIA_KEY_ENTRY(WIDTH),
    MEDIA_FN(AMediaFormat_new),
    MEDIA_FN(AMediaFormat_delete),
    MEDIA_FN(AMediaFormat_toString),
    MEDIA_FN(AMediaFormat_getInt32),
    MEDIA_FN(AMediaFormat_getInt64),
    MEDIA_FN(AMediaFormat_getFloat),
    MEDIA_FN(AMediaFormat_getBuffer),
    MEDIA_FN(AMediaFormat_getString),
    MEDIA_FN(AMediaFormat_setInt32),
    MEDIA_FN(AMediaFormat_setInt64),
    MEDIA_FN(AMediaFormat_setFloat),
    MEDIA_FN(AMediaFormat_setString),
    MEDIA_FN(AMediaFormat_setBuffer),
    MEDIA_FN(AMediaExtractor_new),
    MEDIA_FN(AMediaExtractor_delete),
    MEDIA_FN(AMediaExtractor_setDataSourceFd),
    MEDIA_FN(AMediaExtractor_setDataSource),
    MEDIA_FN(AMediaExtractor_setDataSourceCustom),
    MEDIA_FN(AMediaExtractor_getTrackCount),
    MEDIA_FN(AMediaExtractor_getTrackFormat),
    MEDIA_FN(AMediaExtractor_selectTrack),
    MEDIA_FN(AMediaExtractor_unselectTrack),
    MEDIA_FN(AMediaExtractor_seekTo),
    MEDIA_FN(AMediaExtractor_readSampleData),
    MEDIA_FN(AMediaExtractor_getSampleTrackIndex),
    MEDIA_FN(AMediaExtractor_getSampleTime),
    MEDIA_FN(AMediaExtractor_getSampleFlags),
    MEDIA_FN(AMediaExtractor_advance),
    MEDIA_FN(AMediaDataSource_new),
    MEDIA_FN(AMediaDataSource_delete),
    MEDIA_FN(AMediaDataSource_setUserdata),
    MEDIA_FN(AMediaDataSource_setReadAt),
    MEDIA_FN(AMediaDataSource_setGetSize),
    MEDIA_FN(AMediaDataSource_setClose),
    MEDIA_FN(AMediaCodec_createDecoderByType),
    MEDIA_FN(AMediaCodec_createEncoderByType),
    MEDIA_FN(AMediaCodec_createCodecByName),
    MEDIA_FN(AMediaCodec_configure),
    MEDIA_FN(AMediaCodec_start),
    MEDIA_FN(AMediaCodec_stop),
    MEDIA_FN(AMediaCodec_flush),
    MEDIA_FN(AMediaCodec_delete),
    MEDIA_FN(AMediaCodec_queueInputBuffer),
    MEDIA_FN(AMediaCodec_dequeueInputBuffer),
    MEDIA_FN(AMediaCodec_dequeueOutputBuffer),
    MEDIA_FN(AMediaCodec_getInputBuffer),
    MEDIA_FN(AMediaCodec_getOutputBuffer),
    MEDIA_FN(AMediaCodec_getOutputFormat),
    MEDIA_FN(AMediaCodec_releaseOutputBuffer),
    MEDIA_FN(AMediaCodec_releaseOutputBufferAtTime),
};

}  // namespace

const SymbolEntry* get_media_symbols(size_t* count) {
    if (count != nullptr) *count = sizeof(kMediaSymbols) / sizeof(SymbolEntry);
    return kMediaSymbols;
}

}  // namespace kudroid
