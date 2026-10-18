#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kudroid {
namespace media {

// Where container bytes come from: a file descriptor window, an in-memory buffer, or a
// guest-supplied read callback. Reads are positional so one source serves any number of
// cursors.
class ByteSource {
public:
    virtual ~ByteSource() = default;
    // Total readable bytes.
    virtual int64_t size() = 0;
    // Bytes read at `offset` (short only at the end), or -1 on error.
    virtual int64_t read(int64_t offset, void* dst, size_t n) = 0;
};

struct Sample {
    int64_t offset = 0;  // position in the source
    uint32_t size = 0;
    int64_t ptsUs = 0;
    bool sync = false;
};

enum class TrackKind { Other, Video, Audio };

struct Track {
    TrackKind kind = TrackKind::Other;
    std::string mime;      // Android MIME type, e.g. "video/x-vnd.on2.vp8"
    std::string language;  // ISO 639-2, "und" when unknown
    int width = 0;
    int height = 0;
    int channels = 0;
    int sampleRate = 0;
    int frameRate = 0;     // frames per second, 0 when it cannot be told
    int64_t durationUs = 0;
    // Codec-specific data as Android's extractors hand it over: parameter sets with Annex B
    // start codes for AVC/HEVC, the Vorbis identification and setup headers, the Opus
    // header plus its delay/pre-roll, the AAC AudioSpecificConfig.
    std::vector<std::vector<uint8_t>> csd;
    // Width of the length prefix on AVC/HEVC samples (0 for codecs without one). Samples
    // are rewritten to Annex B when they are read.
    int nalLengthSize = 0;
    std::vector<Sample> samples;  // in file order
    uint32_t maxSampleSize = 0;
};

struct Container {
    std::string format;  // "webm", "matroska" or "mp4"
    int64_t durationUs = 0;
    std::vector<Track> tracks;
};

// Reads the track list and the full sample index of a WebM/Matroska or MP4 file.
bool ParseContainer(ByteSource& source, Container* out, std::string* error);

// Copies sample `sampleIndex` of `track` into `dst` as a decoder consumes it: AVC/HEVC
// length prefixes become start codes. Returns the byte count, -1 when `capacity` is too
// small or the read fails.
int64_t ReadSample(ByteSource& source, const Track& track, size_t sampleIndex, uint8_t* dst,
                   size_t capacity);

// Size a sample occupies once ReadSample has rewritten it.
size_t ConvertedSampleSize(ByteSource& source, const Track& track, size_t sampleIndex);

// ByteSource over a descriptor window [offset, offset + length). A negative or zero length
// extends to the end of the file.
class FdSource : public ByteSource {
public:
    FdSource(int fd, int64_t offset, int64_t length);
    int64_t size() override { return length_; }
    int64_t read(int64_t offset, void* dst, size_t n) override;

private:
    int fd_;
    int64_t offset_;
    int64_t length_;
};

// ByteSource over memory the caller keeps alive.
class MemorySource : public ByteSource {
public:
    MemorySource(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    int64_t size() override { return static_cast<int64_t>(size_); }
    int64_t read(int64_t offset, void* dst, size_t n) override;

private:
    const uint8_t* data_;
    size_t size_;
};

}  // namespace media
}  // namespace kudroid
