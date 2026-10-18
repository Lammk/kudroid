#include "kudroid/platform/MediaShim.h"
#include "kudroid/platform/ShimDefs.h"

#include "kudroid/platform/MediaContainer.h"
#include "kudroid/platform/MediaDecoder.h"
#include "kudroid/platform/SurfaceTextureHost.h"
#include <cctype>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

extern "C" void kudroid_looper_wake_main(void);
extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);
// Id of the SurfaceTexture behind a native window, 0 for a screen window.
extern "C" int kudroid_native_window_surface_texture_id(void* window);

namespace kudroid {
namespace {

// NDK media_status_t: 0 is AMEDIA_OK. Dequeue calls return an index or -1
// (TRY_AGAIN_LATER), which the universal dummy already returned by accident —
// the difference here is valid handles and written out-params instead of nulls.
constexpr int kMediaOk = 0;
constexpr int kDequeueTryAgain = -1;

// ── Logging ─────────────────────────────────────────────────────────────────
// Lifecycle events and the first few buffers of each stream, so a player that stops making
// progress shows where. Bounded: a clip streams thousands of buffers.

std::atomic<int> g_mediaLogBudget{300};

void MediaLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void MediaLog(const char* fmt, ...) {
    if (g_mediaLogBudget.fetch_sub(1, std::memory_order_relaxed) <= 0) return;
    char line[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    kudroid_android_log_message(4, "KuDroidMedia", line);
}

// ── Frame announcements (codec thread -> main Looper) ───────────────────────

std::mutex g_frameMutex;
std::deque<std::pair<int, int64_t>> g_frames;

constexpr size_t kMaxQueuedFrames = 64;

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

// ── AMediaDataSource (guest read callbacks) ─────────────────────────────────

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

// NDK callback shapes: ssize_t readAt(userdata, off64_t offset, void* buffer, size_t size)
// and ssize_t getSize(userdata).
using DataSourceReadAt = int64_t (*)(void*, int64_t, void*, size_t);
using DataSourceGetSize = int64_t (*)(void*);

// Byte source over the guest callbacks. The guest object may be deleted right after the
// extractor is set up, so the callbacks and userdata are copied.
class CallbackSource : public media::ByteSource {
public:
    explicit CallbackSource(const GuestMediaDataSource& source)
        : userdata_(source.userdata),
          readAt_(reinterpret_cast<DataSourceReadAt>(source.readAt)),
          getSize_(reinterpret_cast<DataSourceGetSize>(source.getSize)) {
        size_ = getSize_ != nullptr ? getSize_(userdata_) : -1;
    }
    int64_t size() override { return size_ > 0 ? size_ : 0; }
    int64_t read(int64_t offset, void* dst, size_t n) override {
        if (readAt_ == nullptr || offset < 0 || offset > size_) return -1;
        const size_t want = static_cast<size_t>(std::min<int64_t>(static_cast<int64_t>(n), size_ - offset));
        size_t done = 0;
        auto* out = static_cast<uint8_t*>(dst);
        while (done < want) {
            const int64_t r = readAt_(userdata_, offset + static_cast<int64_t>(done), out + done, want - done);
            if (r < 0) return done > 0 ? static_cast<int64_t>(done) : -1;
            if (r == 0) break;
            done += static_cast<size_t>(r);
        }
        return static_cast<int64_t>(done);
    }

private:
    void* userdata_;
    DataSourceReadAt readAt_;
    DataSourceGetSize getSize_;
    int64_t size_ = -1;
};

// ── AMediaExtractor ─────────────────────────────────────────────────────────

constexpr int kMediaErrorMalformed = -10001;
constexpr int kMediaErrorUnsupported = -10002;
constexpr int kMediaErrorInvalidParameter = -10004;
constexpr uint32_t kSampleFlagSync = 1;

struct GuestMediaExtractor {
    std::unique_ptr<media::ByteSource> source;
    int ownedFd = -1;
    media::Container container;
    bool opened = false;
    int64_t baseUs = 0;  // earliest timestamp in the file, mapped to 0
    std::vector<bool> selected;
    std::vector<size_t> next;  // per track: index of the next sample to deliver

    ~GuestMediaExtractor() {
        source.reset();
        if (ownedFd >= 0) ::close(ownedFd);
    }

    int open(std::unique_ptr<media::ByteSource> src) {
        source = std::move(src);
        opened = false;
        container = media::Container();
        std::string error;
        if (!media::ParseContainer(*source, &container, &error)) {
            MediaLog("extractor: unreadable container (%s)", error.c_str());
            return error.empty() || error.find("unsupported") == std::string::npos
                       ? kMediaErrorMalformed
                       : kMediaErrorUnsupported;
        }
        baseUs = INT64_MAX;
        for (const auto& track : container.tracks) {
            for (const auto& sample : track.samples) baseUs = std::min(baseUs, sample.ptsUs);
        }
        if (baseUs == INT64_MAX) baseUs = 0;
        selected.assign(container.tracks.size(), false);
        next.assign(container.tracks.size(), 0);
        opened = true;
        MediaLog("extractor: %s, %zu tracks, base=%lldus", container.format.c_str(),
                 container.tracks.size(), static_cast<long long>(baseUs));
        for (size_t t = 0; t < container.tracks.size(); ++t) {
            const auto& track = container.tracks[t];
            MediaLog("extractor: track %zu %s %dx%d %dHz x%d, %zu samples", t, track.mime.c_str(),
                     track.width, track.height, track.sampleRate, track.channels,
                     track.samples.size());
        }
        return kMediaOk;
    }

    int64_t virtualTime(int64_t realUs) const { return realUs; }

    // Track holding the next sample in file order among the selected ones, -1 at the end.
    int current() const {
        int best = -1;
        int64_t bestOffset = 0;
        for (size_t t = 0; t < container.tracks.size(); ++t) {
            if (!selected[t] || next[t] >= container.tracks[t].samples.size()) continue;
            const int64_t offset = container.tracks[t].samples[next[t]].offset;
            if (best < 0 || offset < bestOffset) {
                best = static_cast<int>(t);
                bestOffset = offset;
            }
        }
        return best;
    }
};

extern "C" GuestMediaExtractor* bionic_AMediaExtractor_new() { return new GuestMediaExtractor(); }
extern "C" int bionic_AMediaExtractor_delete(GuestMediaExtractor* extractor) {
    delete extractor;
    return kMediaOk;
}
extern "C" int bionic_AMediaExtractor_setDataSourceFd(GuestMediaExtractor* extractor, int fd,
                                                      int64_t offset, int64_t length) {
    if (extractor == nullptr || fd < 0) return kMediaErrorInvalidParameter;
    // The caller may close its descriptor once this returns.
    const int mine = ::dup(fd);
    if (mine < 0) return kMediaErrorInvalidParameter;
    if (extractor->ownedFd >= 0) ::close(extractor->ownedFd);
    extractor->ownedFd = mine;
    return extractor->open(std::make_unique<media::FdSource>(mine, offset, length));
}
extern "C" int bionic_AMediaExtractor_setDataSource(GuestMediaExtractor* extractor, const char* path) {
    if (extractor == nullptr || path == nullptr) return kMediaErrorInvalidParameter;
    const int fd = ::open(path, O_RDONLY);
    if (fd < 0) return kMediaErrorInvalidParameter;
    if (extractor->ownedFd >= 0) ::close(extractor->ownedFd);
    extractor->ownedFd = fd;
    return extractor->open(std::make_unique<media::FdSource>(fd, 0, 0));
}
extern "C" int bionic_AMediaExtractor_setDataSourceCustom(GuestMediaExtractor* extractor,
                                                          GuestMediaDataSource* source) {
    if (extractor == nullptr || source == nullptr || source->readAt == nullptr) {
        return kMediaErrorInvalidParameter;
    }
    return extractor->open(std::make_unique<CallbackSource>(*source));
}
extern "C" size_t bionic_AMediaExtractor_getTrackCount(GuestMediaExtractor* extractor) {
    return extractor != nullptr && extractor->opened ? extractor->container.tracks.size() : 0;
}

void FillTrackFormat(const media::Track& track, int64_t durationUs, GuestMediaFormat* format) {
    format->str[kMediaKey_MIME] = track.mime;
    if (!track.language.empty()) format->str[kMediaKey_LANGUAGE] = track.language;
    format->i64[kMediaKey_DURATION] = durationUs;
    if (track.kind == media::TrackKind::Video) {
        format->i32[kMediaKey_WIDTH] = track.width;
        format->i32[kMediaKey_HEIGHT] = track.height;
        if (track.frameRate > 0) format->i32[kMediaKey_FRAME_RATE] = track.frameRate;
    } else if (track.kind == media::TrackKind::Audio) {
        format->i32[kMediaKey_CHANNEL_COUNT] = track.channels;
        format->i32[kMediaKey_SAMPLE_RATE] = track.sampleRate;
    }
    if (track.maxSampleSize > 0) {
        format->i32[kMediaKey_MAX_INPUT_SIZE] = static_cast<int32_t>(track.maxSampleSize);
    }
    for (size_t i = 0; i < track.csd.size(); ++i) {
        format->buf["csd-" + std::to_string(i)] = track.csd[i];
    }
}

extern "C" GuestMediaFormat* bionic_AMediaExtractor_getTrackFormat(GuestMediaExtractor* extractor,
                                                                   size_t idx) {
    if (extractor == nullptr || !extractor->opened || idx >= extractor->container.tracks.size()) {
        return nullptr;
    }
    const media::Track& track = extractor->container.tracks[idx];
    auto* format = new GuestMediaFormat();
    FillTrackFormat(track, track.durationUs > 0 ? track.durationUs : extractor->container.durationUs,
                    format);
    return format;
}
extern "C" int bionic_AMediaExtractor_selectTrack(GuestMediaExtractor* extractor, size_t idx) {
    if (extractor == nullptr || !extractor->opened || idx >= extractor->selected.size()) {
        return kMediaErrorInvalidParameter;
    }
    extractor->selected[idx] = true;
    MediaLog("extractor: select track %zu", idx);
    return kMediaOk;
}
extern "C" int bionic_AMediaExtractor_unselectTrack(GuestMediaExtractor* extractor, size_t idx) {
    if (extractor == nullptr || !extractor->opened || idx >= extractor->selected.size()) {
        return kMediaErrorInvalidParameter;
    }
    extractor->selected[idx] = false;
    return kMediaOk;
}
extern "C" bool bionic_AMediaExtractor_advance(GuestMediaExtractor* extractor) {
    if (extractor == nullptr || !extractor->opened) return false;
    const int cur = extractor->current();
    if (cur < 0) return false;
    ++extractor->next[static_cast<size_t>(cur)];
    return extractor->current() >= 0;
}
extern "C" ssize_t bionic_AMediaExtractor_readSampleData(GuestMediaExtractor* extractor, uint8_t* buffer,
                                                     size_t capacity) {
    if (extractor == nullptr || !extractor->opened || buffer == nullptr) return -1;
    const int cur = extractor->current();
    if (cur < 0) return -1;
    const auto& track = extractor->container.tracks[static_cast<size_t>(cur)];
    const int64_t n = media::ReadSample(*extractor->source, track,
                                        extractor->next[static_cast<size_t>(cur)], buffer, capacity);
    return n < 0 ? -1 : static_cast<ssize_t>(n);
}
extern "C" int bionic_AMediaExtractor_getSampleTrackIndex(GuestMediaExtractor* extractor) {
    if (extractor == nullptr || !extractor->opened) return -1;
    return extractor->current();
}
extern "C" int64_t bionic_AMediaExtractor_getSampleTime(GuestMediaExtractor* extractor) {
    if (extractor == nullptr || !extractor->opened) return -1;
    const int cur = extractor->current();
    if (cur < 0) return -1;
    const auto& samples = extractor->container.tracks[static_cast<size_t>(cur)].samples;
    return extractor->virtualTime(samples[extractor->next[static_cast<size_t>(cur)]].ptsUs);
}
extern "C" uint32_t bionic_AMediaExtractor_getSampleFlags(GuestMediaExtractor* extractor) {
    if (extractor == nullptr || !extractor->opened) return 0;
    const int cur = extractor->current();
    if (cur < 0) return 0;
    const auto& samples = extractor->container.tracks[static_cast<size_t>(cur)].samples;
    return samples[extractor->next[static_cast<size_t>(cur)]].sync ? kSampleFlagSync : 0;
}
extern "C" int bionic_AMediaExtractor_seekTo(GuestMediaExtractor* extractor, int64_t timeUs,
                                             uint32_t mode) {
    if (extractor == nullptr || !extractor->opened) return kMediaErrorInvalidParameter;
    const int64_t target = (timeUs < 0 ? 0 : timeUs);
    for (size_t t = 0; t < extractor->container.tracks.size(); ++t) {
        const auto& samples = extractor->container.tracks[t].samples;
        // Sync samples only: the last one at or before the target, the first one at or
        // after it, or whichever is nearer (SEEK_PREVIOUS_SYNC / NEXT_SYNC / CLOSEST_SYNC).
        int64_t before = -1;
        int64_t after = -1;
        for (size_t i = 0; i < samples.size(); ++i) {
            if (!samples[i].sync) continue;
            if (samples[i].ptsUs <= target) {
                before = static_cast<int64_t>(i);
            } else if (after < 0) {
                after = static_cast<int64_t>(i);
                break;
            }
        }
        int64_t pick;
        if (mode == 1) {
            pick = after >= 0 ? after : static_cast<int64_t>(samples.size());
        } else if (mode == 2 && before >= 0 && after >= 0) {
            pick = (target - samples[static_cast<size_t>(before)].ptsUs <=
                    samples[static_cast<size_t>(after)].ptsUs - target)
                       ? before
                       : after;
        } else {
            pick = before >= 0 ? before : 0;
        }
        extractor->next[t] = static_cast<size_t>(pick);
    }
    return kMediaOk;
}

// ── AMediaCodec: CPU-readable VP8 and Vorbis output ────────────────────────
constexpr ssize_t kInfoOutputFormatChanged = -2;
constexpr uint32_t kBufferFlagKeyFrame = 1;
constexpr uint32_t kBufferFlagCodecConfig = 2;
constexpr uint32_t kBufferFlagEndOfStream = 4;
constexpr uint32_t kConfigureFlagEncode = 1;
constexpr size_t kInputSlots = 8;
constexpr size_t kOutputSlots = 8;

struct GuestBufferInfo {
    int32_t offset;
    int32_t size;
    int64_t presentationTimeUs;
    uint32_t flags;
};
static_assert(sizeof(GuestBufferInfo) == 24, "AMediaCodecBufferInfo ABI");

struct PendingOutput {
    int64_t ptsUs = 0;
    uint32_t flags = 0;
    int width = 0, height = 0;
    std::vector<uint8_t> data;
};
struct OutputSlot {
    bool busy = false;
    int64_t ptsUs = 0;
    int32_t size = 0;
    int width = 0, height = 0;
    std::vector<uint8_t> data;
};
struct InputSlot {
    bool busy = false;
    std::vector<uint8_t> data;
};
struct GuestMediaCodec {
    std::string mime;
    bool video = false, audio = false;
    bool configured = false, started = false;
    bool inputEos = false, outputEos = false;
    int error = 0;
    int width = 0, height = 0, sampleRate = 48000, channels = 2;
    size_t inputSize = 0;
    int64_t lastPtsUs = 0;
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<InputSlot> inputs;
    std::vector<OutputSlot> outputs;
    std::deque<PendingOutput> pending;
    std::vector<std::vector<uint8_t>> csd;
    std::unique_ptr<media::Decoder> decoder;
    bool formatAnnounced = false;
    int queuedCount = 0, outputCount = 0;
    int surfaceTexture = 0;
};

void SetCodecKind(GuestMediaCodec* codec, const std::string& mime) {
    codec->mime = mime;
    codec->video = mime.rfind("video/", 0) == 0;
    codec->audio = mime.rfind("audio/", 0) == 0;
}
extern "C" GuestMediaCodec* bionic_AMediaCodec_createDecoderByType(const char* mime) {
    if (!mime || !media::HasDecoder(mime)) return nullptr;
    auto* codec = new GuestMediaCodec();
    SetCodecKind(codec, mime);
    return codec;
}
extern "C" GuestMediaCodec* bionic_AMediaCodec_createEncoderByType(const char*) { return nullptr; }
extern "C" GuestMediaCodec* bionic_AMediaCodec_createCodecByName(const char* name) {
    if (!name) return nullptr;
    std::string value(name);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    if (value.find("encoder") != std::string::npos) return nullptr;
    if (value.find("vp8") != std::string::npos) return bionic_AMediaCodec_createDecoderByType("video/x-vnd.on2.vp8");
    if (value.find("vorbis") != std::string::npos) return bionic_AMediaCodec_createDecoderByType("audio/vorbis");
    return nullptr;
}
extern "C" int bionic_AMediaCodec_delete(GuestMediaCodec* codec) { delete codec; return kMediaOk; }

void ResetCodecStream(GuestMediaCodec* codec) {
    codec->pending.clear();
    for (auto& slot : codec->inputs) slot.busy = false;
    for (auto& slot : codec->outputs) { slot.busy = false; slot.data.clear(); }
    codec->formatAnnounced = false;
    codec->inputEos = codec->outputEos = false;
    codec->error = 0;
    codec->lastPtsUs = 0;
    codec->queuedCount = codec->outputCount = 0;
}

int OpenCodecDecoder(GuestMediaCodec* codec) {
    std::string error;
    codec->decoder = media::CreateDecoder(codec->mime, codec->csd, codec->width, codec->height,
                                          &codec->sampleRate, &codec->channels, &error);
    if (!codec->decoder) {
        MediaLog("codec %p: initialization failed: %s", static_cast<void*>(codec), error.c_str());
        return kMediaErrorUnsupported;
    }
    return kMediaOk;
}

extern "C" int bionic_AMediaCodec_configure(GuestMediaCodec* codec, const GuestMediaFormat* format,
                                            void* surface, void* crypto, uint32_t flags) {
    if (!codec || !format) return kMediaErrorInvalidParameter;
    std::lock_guard<std::mutex> lock(codec->mutex);
    if (codec->started) return kMediaErrorInvalidParameter;
    codec->configured = false;
    if (crypto || (flags & kConfigureFlagEncode)) return kMediaErrorUnsupported;
    codec->surfaceTexture = surface ? kudroid_native_window_surface_texture_id(surface) : 0;
    if (surface && (!codec->video || !surface_texture::exists(codec->surfaceTexture))) {
        MediaLog("codec %p: unsupported output surface", static_cast<void*>(codec));
        return kMediaErrorUnsupported;
    }
    auto mime = format->str.find(kMediaKey_MIME);
    if (mime != format->str.end()) SetCodecKind(codec, mime->second);
    if (!media::HasDecoder(codec->mime)) return kMediaErrorUnsupported;
    auto geti = [&](const char* key, int fallback) {
        const auto it = format->i32.find(key);
        return it != format->i32.end() ? it->second : fallback;
    };
    codec->width = geti(kMediaKey_WIDTH, 0);
    codec->height = geti(kMediaKey_HEIGHT, 0);
    codec->sampleRate = geti(kMediaKey_SAMPLE_RATE, 48000);
    codec->channels = geti(kMediaKey_CHANNEL_COUNT, 2);
    const int maxInput = geti(kMediaKey_MAX_INPUT_SIZE, 0);
    if (maxInput < 0 || maxInput > (32 << 20) || codec->width < 0 || codec->height < 0 ||
        codec->width > 16384 || codec->height > 16384) return kMediaErrorInvalidParameter;
    codec->inputSize = static_cast<size_t>(maxInput);
    codec->csd.clear();
    for (unsigned index = 0; ; ++index) {
        const auto it = format->buf.find("csd-" + std::to_string(index));
        if (it == format->buf.end()) break;
        codec->csd.push_back(it->second);
    }
    const int result = OpenCodecDecoder(codec);
    if (result != kMediaOk) return result;
    codec->inputs.clear();
    codec->outputs.clear();
    codec->configured = true;
    ResetCodecStream(codec);
    MediaLog("codec %p: configure %s %dx%d %dHz x%d, %s output", static_cast<void*>(codec),
             codec->mime.c_str(), codec->width, codec->height, codec->sampleRate, codec->channels,
             codec->surfaceTexture ? "SurfaceTexture" : "CPU");
    return kMediaOk;
}
extern "C" int bionic_AMediaCodec_start(GuestMediaCodec* codec) {
    if (!codec) return kMediaErrorInvalidParameter;
    std::lock_guard<std::mutex> lock(codec->mutex);
    if (!codec->configured || codec->started) return kMediaErrorInvalidParameter;
    if (!codec->decoder && OpenCodecDecoder(codec) != kMediaOk) return kMediaErrorUnsupported;
    const size_t size = std::max<size_t>(codec->inputSize + 4096, codec->video ? (1u << 20) : (1u << 16));
    codec->inputs.resize(kInputSlots);
    for (auto& slot : codec->inputs) slot.data.resize(size);
    codec->outputs.resize(kOutputSlots);
    ResetCodecStream(codec);
    codec->started = true;
    MediaLog("codec %p: start (%zu input buffers of %zu bytes)", static_cast<void*>(codec), kInputSlots, size);
    return kMediaOk;
}
extern "C" int bionic_AMediaCodec_stop(GuestMediaCodec* codec) {
    if (!codec) return kMediaErrorInvalidParameter;
    {
        std::lock_guard<std::mutex> lock(codec->mutex);
        MediaLog("codec %p: stop after %d inputs, %d outputs", static_cast<void*>(codec), codec->queuedCount, codec->outputCount);
        codec->started = false;
        codec->configured = false;
        ResetCodecStream(codec);
        codec->decoder.reset();
    }
    codec->changed.notify_all();
    return kMediaOk;
}
extern "C" int bionic_AMediaCodec_flush(GuestMediaCodec* codec) {
    if (!codec) return kMediaErrorInvalidParameter;
    int result;
    {
        std::lock_guard<std::mutex> lock(codec->mutex);
        if (!codec->started) return kMediaErrorInvalidParameter;
        ResetCodecStream(codec);
        result = OpenCodecDecoder(codec);
        codec->error = result;
    }
    codec->changed.notify_all();
    return result;
}

template <typename Ready>
bool WaitFor(GuestMediaCodec* codec, std::unique_lock<std::mutex>& lock, int64_t timeoutUs, Ready ready) {
    if (ready()) return true;
    if (timeoutUs == 0) return false;
    auto done = [&] { return ready() || !codec->started || codec->error; };
    if (timeoutUs < 0) codec->changed.wait(lock, done);
    else codec->changed.wait_for(lock, std::chrono::microseconds(timeoutUs), done);
    return ready();
}

extern "C" int bionic_AMediaCodec_queueInputBuffer(GuestMediaCodec* codec, size_t idx, int64_t offset,
                                                   size_t size, uint64_t time, uint32_t flags) {
    if (!codec) return kMediaErrorInvalidParameter;
    int result = kMediaOk;
    {
        std::lock_guard<std::mutex> lock(codec->mutex);
        if (!codec->started || codec->inputEos || idx >= codec->inputs.size() || !codec->inputs[idx].busy ||
            offset < 0 || static_cast<uint64_t>(offset) > codec->inputs[idx].data.size() ||
            size > codec->inputs[idx].data.size() - static_cast<size_t>(offset)) return kMediaErrorInvalidParameter;
        codec->inputs[idx].busy = false;
        const int64_t pts = static_cast<int64_t>(time);
        if (++codec->queuedCount <= 3 || (flags & kBufferFlagEndOfStream)) {
            MediaLog("codec %p: input #%d pts=%lld size=%zu flags=%u", static_cast<void*>(codec),
                     codec->queuedCount, static_cast<long long>(pts), size, flags);
        }
        if (!(flags & kBufferFlagCodecConfig) && (size > 0 || (flags & kBufferFlagEndOfStream))) {
            std::vector<media::DecodedBuffer> decoded;
            std::string error;
            const uint8_t* data = size ? codec->inputs[idx].data.data() + offset : nullptr;
            if (!codec->decoder->decode(data, size, pts, &decoded, &error)) {
                codec->error = result = kMediaErrorMalformed;
                MediaLog("codec %p: decode failed: %s", static_cast<void*>(codec), error.c_str());
            } else {
                for (auto& frame : decoded) {
                    PendingOutput out;
                    out.ptsUs = frame.ptsUs;
                    out.flags = flags & kBufferFlagKeyFrame;
                    out.width = frame.width; out.height = frame.height;
                    out.data = std::move(frame.bytes);
                    codec->lastPtsUs = std::max(codec->lastPtsUs, out.ptsUs);
                    codec->pending.push_back(std::move(out));
                }
            }
        }
        if ((flags & kBufferFlagEndOfStream) && result == kMediaOk) {
            codec->inputEos = true;
            PendingOutput eos;
            eos.ptsUs = std::max(pts, codec->lastPtsUs);
            eos.flags = kBufferFlagEndOfStream;
            codec->pending.push_back(std::move(eos));
        }
    }
    codec->changed.notify_all();
    return result;
}
extern "C" ssize_t bionic_AMediaCodec_dequeueInputBuffer(GuestMediaCodec* codec, int64_t timeoutUs) {
    if (!codec) return kMediaErrorInvalidParameter;
    std::unique_lock<std::mutex> lock(codec->mutex);
    if (codec->inputEos) return kDequeueTryAgain;
    auto freeSlot = [&]() -> ssize_t {
        if (!codec->started || codec->error || codec->pending.size() >= kOutputSlots) return -1;
        for (size_t i = 0; i < codec->inputs.size(); ++i) if (!codec->inputs[i].busy) return static_cast<ssize_t>(i);
        return -1;
    };
    if (!WaitFor(codec, lock, timeoutUs, [&] { return freeSlot() >= 0; })) return codec->error ? codec->error : kDequeueTryAgain;
    const ssize_t slot = freeSlot();
    codec->inputs[static_cast<size_t>(slot)].busy = true;
    return slot;
}
extern "C" ssize_t bionic_AMediaCodec_dequeueOutputBuffer(GuestMediaCodec* codec, void* info, int64_t timeoutUs) {
    if (!codec) return kMediaErrorInvalidParameter;
    std::unique_lock<std::mutex> lock(codec->mutex);
    auto available = [&]() {
        if (!codec->started || codec->error || codec->pending.empty()) return false;
        return std::any_of(codec->outputs.begin(), codec->outputs.end(), [](const OutputSlot& slot) { return !slot.busy; });
    };
    if (!WaitFor(codec, lock, timeoutUs, available)) return codec->error ? codec->error : kDequeueTryAgain;
    const auto& front = codec->pending.front();
    if (codec->video && front.width && (front.width != codec->width || front.height != codec->height)) {
        codec->width = front.width; codec->height = front.height;
        codec->formatAnnounced = false;
    }
    if (!codec->formatAnnounced) {
        codec->formatAnnounced = true;
        MediaLog("codec %p: output format changed", static_cast<void*>(codec));
        return kInfoOutputFormatChanged;
    }
    size_t slot = 0;
    while (codec->outputs[slot].busy) ++slot;
    PendingOutput out = std::move(codec->pending.front());
    codec->pending.pop_front();
    OutputSlot& target = codec->outputs[slot];
    target.busy = true;
    target.ptsUs = out.ptsUs;
    target.size = static_cast<int32_t>(out.data.size());
    target.width = out.width; target.height = out.height;
    target.data = std::move(out.data);
    if (target.data.empty()) target.data.resize(1);
    if (info) *static_cast<GuestBufferInfo*>(info) = {0, target.size, out.ptsUs, out.flags};
    if (out.flags & kBufferFlagEndOfStream) codec->outputEos = true;
    if (++codec->outputCount <= 3 || (out.flags & kBufferFlagEndOfStream)) {
        MediaLog("codec %p: output #%d slot=%zu pts=%lld size=%d flags=%u", static_cast<void*>(codec),
                 codec->outputCount, slot, static_cast<long long>(out.ptsUs), target.size, out.flags);
    }
    codec->changed.notify_all();
    return static_cast<ssize_t>(slot);
}
extern "C" int bionic_AMediaCodec_releaseOutputBuffer(GuestMediaCodec* codec, size_t idx, bool render) {
    if (!codec) return kMediaErrorInvalidParameter;
    {
        std::lock_guard<std::mutex> lock(codec->mutex);
        if (idx >= codec->outputs.size() || !codec->outputs[idx].busy) return kMediaErrorInvalidParameter;
        auto& output = codec->outputs[idx];
        if (render && codec->surfaceTexture && output.width > 0 && output.height > 0) {
            if (output.ptsUs < 0 || output.ptsUs > INT64_MAX / 1000 ||
                !surface_texture::post(codec->surfaceTexture, std::move(output.data),
                                       output.width, output.height, output.ptsUs * 1000)) {
                output.busy = false;
                codec->changed.notify_all();
                return kMediaErrorInvalidParameter;
            }
        }
        codec->outputs[idx].busy = false;
    }
    codec->changed.notify_all();
    return kMediaOk;
}
extern "C" int bionic_AMediaCodec_releaseOutputBufferAtTime(GuestMediaCodec* codec, size_t idx, int64_t) {
    return bionic_AMediaCodec_releaseOutputBuffer(codec, idx, true);
}
extern "C" uint8_t* bionic_AMediaCodec_getInputBuffer(GuestMediaCodec* codec, size_t idx, size_t* outSize) {
    if (outSize) *outSize = 0;
    if (!codec) return nullptr;
    std::lock_guard<std::mutex> lock(codec->mutex);
    if (idx >= codec->inputs.size() || !codec->inputs[idx].busy) return nullptr;
    if (outSize) *outSize = codec->inputs[idx].data.size();
    return codec->inputs[idx].data.data();
}
extern "C" uint8_t* bionic_AMediaCodec_getOutputBuffer(GuestMediaCodec* codec, size_t idx, size_t* outSize) {
    if (outSize) *outSize = 0;
    if (!codec) return nullptr;
    std::lock_guard<std::mutex> lock(codec->mutex);
    if (codec->surfaceTexture) return nullptr;
    if (idx >= codec->outputs.size() || !codec->outputs[idx].busy) return nullptr;
    if (outSize) *outSize = codec->outputs[idx].data.size();
    return codec->outputs[idx].data.data();
}
extern "C" GuestMediaFormat* bionic_AMediaCodec_getOutputFormat(GuestMediaCodec* codec) {
    if (!codec) return nullptr;
    std::lock_guard<std::mutex> lock(codec->mutex);
    auto* format = new GuestMediaFormat();
    if (codec->video) {
        format->str[kMediaKey_MIME] = "video/raw";
        format->i32[kMediaKey_WIDTH] = codec->width;
        format->i32[kMediaKey_HEIGHT] = codec->height;
        format->i32[kMediaKey_COLOR_FORMAT] = codec->surfaceTexture ? 0x7f000789 : 19;
        format->i32["stride"] = codec->width;
        format->i32["slice-height"] = codec->height;
        format->i32["crop-left"] = format->i32["crop-top"] = 0;
        format->i32["crop-right"] = codec->width - 1;
        format->i32["crop-bottom"] = codec->height - 1;
    } else {
        format->str[kMediaKey_MIME] = "audio/raw";
        format->i32[kMediaKey_SAMPLE_RATE] = codec->sampleRate;
        format->i32[kMediaKey_CHANNEL_COUNT] = codec->channels;
        format->i32["pcm-encoding"] = 2;
    }
    return format;
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

}  // namespace kudroid

extern "C" void kudroid_media_frame_post(int textureId, long long timestampNs) {
    {
        std::lock_guard<std::mutex> lock(kudroid::g_frameMutex);
        // A consumer that stopped draining must not grow the queue without bound.
        if (kudroid::g_frames.size() >= kudroid::kMaxQueuedFrames) kudroid::g_frames.pop_front();
        kudroid::g_frames.emplace_back(textureId, timestampNs);
    }
    kudroid_looper_wake_main();
}

extern "C" int kudroid_media_frame_take(int* textureId, long long* timestampNs) {
    std::lock_guard<std::mutex> lock(kudroid::g_frameMutex);
    if (kudroid::g_frames.empty()) return 0;
    *textureId = kudroid::g_frames.front().first;
    *timestampNs = kudroid::g_frames.front().second;
    kudroid::g_frames.pop_front();
    return 1;
}

namespace kudroid {

const SymbolEntry* get_media_symbols(size_t* count) {
    if (count != nullptr) *count = sizeof(kMediaSymbols) / sizeof(SymbolEntry);
    return kMediaSymbols;
}

}  // namespace kudroid
