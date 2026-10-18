// Exercise container extraction and real VP8/Vorbis buffer decoding.

#include "kudroid/platform/MediaShim.h"
#include "kudroid/platform/ShimDefs.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using Bytes = std::vector<uint8_t>;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                           \
        }                                                                           \
    } while (0)

// ── minimal EBML writer ─────────────────────────────────────────────────────

void PutId(Bytes& out, uint32_t id) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        if ((id >> shift) != 0 || shift == 0) out.push_back(static_cast<uint8_t>(id >> shift));
    }
}

Bytes Element(uint32_t id, const Bytes& body) {
    Bytes out;
    PutId(out, id);
    out.push_back(0x01);  // 8-byte size
    for (int shift = 48; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>(body.size() >> shift));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

Bytes UInt(uint32_t id, uint64_t value) {
    Bytes body;
    bool started = false;
    for (int shift = 56; shift >= 0; shift -= 8) {
        const uint8_t b = static_cast<uint8_t>(value >> shift);
        if (b != 0 || started || shift == 0) {
            body.push_back(b);
            started = true;
        }
    }
    return Element(id, body);
}

Bytes Str(uint32_t id, const std::string& value) { return Element(id, Bytes(value.begin(), value.end())); }

Bytes Float64(uint32_t id, double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, 8);
    Bytes body;
    for (int shift = 56; shift >= 0; shift -= 8) body.push_back(static_cast<uint8_t>(bits >> shift));
    return Element(id, body);
}

Bytes Cat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

Bytes SimpleBlock(int track, int relMs, bool key, size_t payload, uint8_t fill) {
    Bytes body;
    body.push_back(static_cast<uint8_t>(0x80 | track));
    body.push_back(static_cast<uint8_t>(relMs >> 8));
    body.push_back(static_cast<uint8_t>(relMs));
    body.push_back(key ? 0x80 : 0x00);
    body.insert(body.end(), payload, fill);
    return Element(0xA3, body);
}

constexpr int kVideoFrames = 60;   // 33 ms apart: about 2 s
constexpr int kAudioPackets = 100; // 20 ms apart: 2 s
constexpr int kVideoStepMs = 33;
constexpr int kAudioStepMs = 20;

Bytes BuildWebm() {
    Bytes ident(30, 0);
    ident[0] = 1;
    std::memcpy(&ident[1], "vorbis", 6);
    Bytes comment(10, 3);
    Bytes setup(40, 5);
    Bytes priv;
    priv.push_back(2);
    priv.push_back(static_cast<uint8_t>(ident.size()));
    priv.push_back(static_cast<uint8_t>(comment.size()));
    priv.insert(priv.end(), ident.begin(), ident.end());
    priv.insert(priv.end(), comment.begin(), comment.end());
    priv.insert(priv.end(), setup.begin(), setup.end());

    Bytes video = Element(0xAE, Cat({UInt(0xD7, 1), UInt(0x73C5, 11), UInt(0x83, 1), Str(0x86, "V_VP8"),
                                     Element(0xE0, Cat({UInt(0xB0, 320), UInt(0xBA, 240)}))}));
    Bytes audio = Element(0xAE, Cat({UInt(0xD7, 2), UInt(0x73C5, 12), UInt(0x83, 2), Str(0x86, "A_VORBIS"),
                                     Element(0x63A2, priv),
                                     Element(0xE1, Cat({Float64(0xB5, 48000.0), UInt(0x9F, 2)}))}));
    Bytes tracks = Element(0x1654AE6B, Cat({video, audio}));

    // Interleave by time, as a muxer does.
    Bytes cluster = UInt(0xE7, 0);
    int v = 0, a = 0;
    while (v < kVideoFrames || a < kAudioPackets) {
        const int vt = v < kVideoFrames ? v * kVideoStepMs : INT32_MAX;
        const int at = a < kAudioPackets ? a * kAudioStepMs : INT32_MAX;
        if (vt <= at) {
            const Bytes b = SimpleBlock(1, vt, v % 15 == 0, 200 + static_cast<size_t>(v), 0x11);
            cluster.insert(cluster.end(), b.begin(), b.end());
            ++v;
        } else {
            const Bytes b = SimpleBlock(2, at, true, 60, 0x22);
            cluster.insert(cluster.end(), b.begin(), b.end());
            ++a;
        }
    }
    Bytes segment = Element(0x18538067, Cat({tracks, Element(0x1F43B675, cluster)}));
    Bytes header = Element(0x1A45DFA3, Cat({UInt(0x4286, 1), Str(0x4282, "webm")}));
    return Cat({header, segment});
}

// ── symbol access ───────────────────────────────────────────────────────────

void* Sym(const char* name) {
    size_t count = 0;
    const kudroid::SymbolEntry* table = kudroid::get_media_symbols(&count);
    for (size_t i = 0; i < count; ++i) {
        if (std::strcmp(table[i].name, name) == 0) return table[i].address;
    }
    std::fprintf(stderr, "missing symbol %s\n", name);
    ++g_failures;
    return nullptr;
}

template <typename Fn>
Fn Fnc(const char* name) { return reinterpret_cast<Fn>(Sym(name)); }

struct BufferInfo {
    int32_t offset;
    int32_t size;
    int64_t presentationTimeUs;
    uint32_t flags;
};

using NewFn = void* (*)();
using SetFdFn = int (*)(void*, int, int64_t, int64_t);
using CountFn = size_t (*)(void*);
using FormatFn = void* (*)(void*, size_t);
using SelectFn = int (*)(void*, size_t);
using GetI32Fn = bool (*)(void*, const char*, int32_t*);
using GetI64Fn = bool (*)(void*, const char*, int64_t*);
using GetStrFn = bool (*)(void*, const char*, const char**);
using GetBufFn = bool (*)(void*, const char*, void**, size_t*);
using ReadFn = ssize_t (*)(void*, uint8_t*, size_t);
using TrackIdxFn = int (*)(void*);
using TimeFn = int64_t (*)(void*);
using FlagsFn = uint32_t (*)(void*);
using AdvanceFn = bool (*)(void*);
using SeekFn = int (*)(void*, int64_t, uint32_t);
using DeleteFn = int (*)(void*);
using CodecNewFn = void* (*)(const char*);
using ConfigureFn = int (*)(void*, const void*, void*, void*, uint32_t);
using CodecOpFn = int (*)(void*);
using DequeueInFn = ssize_t (*)(void*, int64_t);
using InBufFn = uint8_t* (*)(void*, size_t, size_t*);
using QueueFn = int (*)(void*, size_t, int64_t, size_t, uint64_t, uint32_t);
using DequeueOutFn = ssize_t (*)(void*, void*, int64_t);
using ReleaseFn = int (*)(void*, size_t, bool);
using OutFormatFn = void* (*)(void*);

}  // namespace

int main() {
    const Bytes webm = BuildWebm();
    char path[] = "/tmp/kudroid_media_testXXXXXX";
    const int fd = mkstemp(path);
    CHECK(fd >= 0);
    CHECK(write(fd, webm.data(), webm.size()) == static_cast<ssize_t>(webm.size()));

    auto extNew = Fnc<NewFn>("AMediaExtractor_new");
    auto setFd = Fnc<SetFdFn>("AMediaExtractor_setDataSourceFd");
    auto count = Fnc<CountFn>("AMediaExtractor_getTrackCount");
    auto trackFormat = Fnc<FormatFn>("AMediaExtractor_getTrackFormat");
    auto select = Fnc<SelectFn>("AMediaExtractor_selectTrack");
    auto getI32 = Fnc<GetI32Fn>("AMediaFormat_getInt32");
    auto getI64 = Fnc<GetI64Fn>("AMediaFormat_getInt64");
    auto getStr = Fnc<GetStrFn>("AMediaFormat_getString");
    auto getBuf = Fnc<GetBufFn>("AMediaFormat_getBuffer");
    auto fmtDelete = Fnc<DeleteFn>("AMediaFormat_delete");
    auto readSample = Fnc<ReadFn>("AMediaExtractor_readSampleData");
    auto sampleTrack = Fnc<TrackIdxFn>("AMediaExtractor_getSampleTrackIndex");
    auto sampleTime = Fnc<TimeFn>("AMediaExtractor_getSampleTime");
    auto sampleFlags = Fnc<FlagsFn>("AMediaExtractor_getSampleFlags");
    auto advance = Fnc<AdvanceFn>("AMediaExtractor_advance");
    auto seekTo = Fnc<SeekFn>("AMediaExtractor_seekTo");
    auto extDelete = Fnc<DeleteFn>("AMediaExtractor_delete");

    void* ex = extNew();
    CHECK(setFd(ex, fd, 0, static_cast<int64_t>(webm.size())) == 0);
    close(fd);  // the extractor keeps its own descriptor
    CHECK(count(ex) == 2);

    void* vf = trackFormat(ex, 0);
    const char* mime = nullptr;
    int32_t w = 0, h = 0;
    CHECK(vf != nullptr && getStr(vf, "mime", &mime) && std::strcmp(mime, "video/x-vnd.on2.vp8") == 0);
    CHECK(getI32(vf, "width", &w) && w == 320);
    CHECK(getI32(vf, "height", &h) && h == 240);
    int64_t duration = 0;
    CHECK(getI64(vf, "duration-us", &duration) && duration > 0);
    fmtDelete(vf);

    void* af = trackFormat(ex, 1);
    int32_t rate = 0, channels = 0;
    CHECK(getStr(af, "mime", &mime) && std::strcmp(mime, "audio/vorbis") == 0);
    CHECK(getI32(af, "sample-rate", &rate) && rate == 48000);
    CHECK(getI32(af, "channel-count", &channels) && channels == 2);
    void* csd = nullptr;
    size_t csdSize = 0;
    CHECK(getBuf(af, "csd-0", &csd, &csdSize) && csdSize == 30);
    CHECK(getBuf(af, "csd-1", &csd, &csdSize) && csdSize == 40);
    fmtDelete(af);
    CHECK(trackFormat(ex, 5) == nullptr);

    // Walk every sample the way a player does.
    CHECK(select(ex, 0) == 0 && select(ex, 1) == 0);
    CHECK(select(ex, 9) != 0);
    std::vector<uint8_t> buf(1 << 16);
    int videoSeen = 0, audioSeen = 0;
    int64_t lastVideo = -1, lastAudio = -1, endTime = 0;
    bool firstKey = false;
    do {
        const int track = sampleTrack(ex);
        const int n = readSample(ex, buf.data(), buf.size());
        const int64_t t = sampleTime(ex);
        CHECK(n > 0 && t >= 0);
        if (track == 0) {
            if (videoSeen == 0) firstKey = (sampleFlags(ex) & 1) != 0;
            CHECK(n == 200 + videoSeen);
            CHECK(t >= lastVideo);
            lastVideo = t;
            ++videoSeen;
        } else {
            CHECK(n == 60);
            CHECK(t >= lastAudio);
            lastAudio = t;
            ++audioSeen;
        }
        endTime = std::max(endTime, t);
    } while (advance(ex));
    CHECK(firstKey);
    CHECK(videoSeen == kVideoFrames);
    CHECK(audioSeen == kAudioPackets);
    CHECK(sampleTrack(ex) == -1 && readSample(ex, buf.data(), buf.size()) == -1);
    CHECK(endTime == 1980000);

    // Seek timestamps use the container clock without compression.
    CHECK(seekTo(ex, 100000, 0) == 0);
    CHECK(sampleTrack(ex) >= 0);
    CHECK(sampleTime(ex) <= 100000);
    CHECK(seekTo(ex, 0, 0) == 0);
    CHECK(sampleTime(ex) == 0);

    // ── codec: video ────────────────────────────────────────────────────────
    auto codecNew = Fnc<CodecNewFn>("AMediaCodec_createDecoderByType");
    auto configure = Fnc<ConfigureFn>("AMediaCodec_configure");
    auto start = Fnc<CodecOpFn>("AMediaCodec_start");
    auto stop = Fnc<CodecOpFn>("AMediaCodec_stop");
    auto dequeueIn = Fnc<DequeueInFn>("AMediaCodec_dequeueInputBuffer");
    auto inBuf = Fnc<InBufFn>("AMediaCodec_getInputBuffer");
    auto queue = Fnc<QueueFn>("AMediaCodec_queueInputBuffer");
    auto dequeueOut = Fnc<DequeueOutFn>("AMediaCodec_dequeueOutputBuffer");
    auto release = Fnc<ReleaseFn>("AMediaCodec_releaseOutputBuffer");
    auto outFormat = Fnc<OutFormatFn>("AMediaCodec_getOutputFormat");
    auto outBuf = Fnc<InBufFn>("AMediaCodec_getOutputBuffer");
    auto codecDelete = Fnc<DeleteFn>("AMediaCodec_delete");

    const int fixtureFd = open(KUDROID_TEST_MEDIA_FILE, O_RDONLY);
    CHECK(fixtureFd >= 0);
    if (fixtureFd < 0) return 1;
    for (size_t track = 0; track < 2; ++track) {
        void* fixture = extNew();
        CHECK(setFd(fixture, fixtureFd, 0, 0) == 0);
        CHECK(count(fixture) == 2 && select(fixture, track) == 0);
        void* format = trackFormat(fixture, track);
        CHECK(format != nullptr && getStr(format, "mime", &mime));
        void* codec = codecNew(mime);
        CHECK(codec != nullptr);
        CHECK(configure(codec, nullptr, nullptr, nullptr, 0) != 0);
        CHECK(configure(codec, format, nullptr, nullptr, 0) == 0);
        fmtDelete(format);
        CHECK(start(codec) == 0);
        CHECK(dequeueOut(codec, nullptr, 20000) == -1);

        bool inputEos = false, outputEos = false, sawFormat = false;
        bool nonzeroPcm = false;
        int delivered = 0;
        int64_t pcmFrames = 0, firstPts = -1, lastPts = -1;
        for (int iteration = 0; iteration < 500 && !outputEos; ++iteration) {
            const ssize_t input = inputEos ? -1 : dequeueIn(codec, 0);
            if (input >= 0) {
                size_t capacity = 0;
                uint8_t* bytes = inBuf(codec, static_cast<size_t>(input), &capacity);
                CHECK(bytes != nullptr);
                const bool last = sampleTrack(fixture) < 0;
                const ssize_t size = last ? 0 : readSample(fixture, bytes, capacity);
                CHECK(size >= 0);
                if (size < 0) break;
                const int64_t pts = last ? lastPts + 1 : sampleTime(fixture);
                CHECK(pts >= 0);
                CHECK(queue(codec, static_cast<size_t>(input), 0, static_cast<size_t>(size),
                            static_cast<uint64_t>(pts), last ? 4u : sampleFlags(fixture)) == 0);
                if (last) inputEos = true;
                else advance(fixture);
            }
            BufferInfo info{};
            ssize_t output;
            while ((output = dequeueOut(codec, &info, 0)) != -1) {
                if (output == -2) {
                    CHECK(!sawFormat);
                    sawFormat = true;
                    void* decodedFormat = outFormat(codec);
                    int32_t first = 0, second = 0;
                    if (track == 0) {
                        int32_t color = 0;
                        CHECK(getI32(decodedFormat, "width", &first) && first == 16);
                        CHECK(getI32(decodedFormat, "height", &second) && second == 16);
                        CHECK(getI32(decodedFormat, "color-format", &color) && color == 19);
                    } else {
                        CHECK(getI32(decodedFormat, "sample-rate", &first) && first == 48000);
                        CHECK(getI32(decodedFormat, "channel-count", &second) && second == 2);
                    }
                    fmtDelete(decodedFormat);
                    continue;
                }
                CHECK(output >= 0 && sawFormat);
                if (output < 0) break;
                if (info.size > 0) {
                    size_t capacity = 0;
                    const uint8_t* bytes = outBuf(codec, static_cast<size_t>(output), &capacity);
                    CHECK(bytes != nullptr && capacity == static_cast<size_t>(info.size));
                    CHECK(info.presentationTimeUs > lastPts);
                    if (firstPts < 0) firstPts = info.presentationTimeUs;
                    lastPts = info.presentationTimeUs;
                    if (track == 0) {
                        CHECK(capacity == 16 * 16 * 3 / 2);
                        if (bytes && capacity == 384) {
                            CHECK(std::abs(int(bytes[0]) - 81) <= 2);
                            CHECK(std::abs(int(bytes[256]) - 90) <= 2);
                            CHECK(std::abs(int(bytes[320]) - 240) <= 2);
                        }
                    } else {
                        CHECK(capacity % 4 == 0);
                        pcmFrames += static_cast<int64_t>(capacity / 4);
                        for (size_t i = 0; bytes && i < capacity; ++i) nonzeroPcm |= bytes[i] != 0;
                    }
                    ++delivered;
                }
                if (info.flags & 4) outputEos = true;
                CHECK(release(codec, static_cast<size_t>(output), false) == 0);
                CHECK(release(codec, static_cast<size_t>(output), false) != 0);
            }
        }
        CHECK(inputEos && outputEos && sawFormat);
        if (track == 0) {
            CHECK(delivered == 10);
            CHECK(firstPts >= 0 && lastPts >= 360000 && lastPts < 410000);
        } else {
            CHECK(nonzeroPcm && delivered > 0);
            CHECK(pcmFrames >= 19200 - 2048 && pcmFrames <= 19200 + 2048);
            CHECK(firstPts == 0);
        }
        CHECK(stop(codec) == 0);
        CHECK(dequeueIn(codec, 0) == -1);
        codecDelete(codec);
        extDelete(fixture);
    }
    close(fixtureFd);

    // Frame queue.
    int id = 0;
    long long ts = 0;
    CHECK(kudroid_media_frame_take(&id, &ts) == 0);
    kudroid_media_frame_post(7, 123456);
    CHECK(kudroid_media_frame_take(&id, &ts) == 1 && id == 7 && ts == 123456);
    CHECK(kudroid_media_frame_take(&id, &ts) == 0);

    // Garbage is rejected, not trusted.
    char junk[] = "/tmp/kudroid_media_junkXXXXXX";
    const int jfd = mkstemp(junk);
    const char text[] = "this is not a media container at all, just text padding padding padding";
    CHECK(write(jfd, text, sizeof(text)) == static_cast<ssize_t>(sizeof(text)));
    void* bad = extNew();
    CHECK(setFd(bad, jfd, 0, 0) != 0);
    CHECK(count(bad) == 0);
    close(jfd);
    extDelete(bad);

    extDelete(ex);
    unlink(path);
    unlink(junk);
    if (g_failures == 0) std::printf("test_media: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
