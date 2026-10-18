#include "kudroid/platform/MediaContainer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include <unistd.h>

namespace kudroid {
namespace media {

// ── byte sources ────────────────────────────────────────────────────────────

FdSource::FdSource(int fd, int64_t offset, int64_t length)
    : fd_(fd), offset_(offset < 0 ? 0 : offset), length_(length) {
    if (length_ <= 0) {
        const off_t end = ::lseek(fd_, 0, SEEK_END);
        length_ = end > offset_ ? static_cast<int64_t>(end) - offset_ : 0;
    }
}

int64_t FdSource::read(int64_t offset, void* dst, size_t n) {
    if (offset < 0 || offset > length_) return -1;
    const size_t want = static_cast<size_t>(std::min<int64_t>(static_cast<int64_t>(n), length_ - offset));
    size_t done = 0;
    auto* out = static_cast<uint8_t*>(dst);
    while (done < want) {
        const ssize_t r = ::pread(fd_, out + done, want - done,
                                  static_cast<off_t>(offset_ + offset + static_cast<int64_t>(done)));
        if (r < 0) return done > 0 ? static_cast<int64_t>(done) : -1;
        if (r == 0) break;
        done += static_cast<size_t>(r);
    }
    return static_cast<int64_t>(done);
}

int64_t MemorySource::read(int64_t offset, void* dst, size_t n) {
    if (offset < 0 || offset > static_cast<int64_t>(size_)) return -1;
    const size_t want = std::min(n, size_ - static_cast<size_t>(offset));
    std::memcpy(dst, data_ + offset, want);
    return static_cast<int64_t>(want);
}

namespace {

// Positional reads through a window: container walking issues thousands of tiny reads
// (element headers) and each one would otherwise be a syscall.
class Reader {
public:
    explicit Reader(ByteSource& source) : source_(source), size_(source.size()) {}

    int64_t size() const { return size_; }

    bool read(int64_t offset, void* dst, size_t n) {
        if (offset < 0 || n == 0) return n == 0;
        if (offset + static_cast<int64_t>(n) > size_) return false;
        if (n > kWindow / 2) {
            return source_.read(offset, dst, n) == static_cast<int64_t>(n);
        }
        if (offset < windowOffset_ ||
            offset + static_cast<int64_t>(n) > windowOffset_ + static_cast<int64_t>(window_.size())) {
            const size_t fill = static_cast<size_t>(std::min<int64_t>(kWindow, size_ - offset));
            window_.resize(fill);
            const int64_t got = source_.read(offset, window_.data(), fill);
            if (got < static_cast<int64_t>(n)) {
                window_.clear();
                windowOffset_ = -1;
                return false;
            }
            window_.resize(static_cast<size_t>(got));
            windowOffset_ = offset;
        }
        std::memcpy(dst, window_.data() + (offset - windowOffset_), n);
        return true;
    }

    bool u8(int64_t offset, uint8_t* v) { return read(offset, v, 1); }

private:
    static constexpr int64_t kWindow = 256 * 1024;
    ByteSource& source_;
    int64_t size_;
    std::vector<uint8_t> window_;
    int64_t windowOffset_ = -1;
};

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}
uint64_t be64(const uint8_t* p) { return (static_cast<uint64_t>(be32(p)) << 32) | be32(p + 4); }

const uint8_t kStartCode[4] = {0, 0, 0, 1};

void AppendStartCode(std::vector<uint8_t>* out, const uint8_t* nal, size_t n) {
    out->insert(out->end(), kStartCode, kStartCode + 4);
    out->insert(out->end(), nal, nal + n);
}

// ── codec-private parsing shared by both containers ─────────────────────────

// avcC -> csd-0 (SPS) and csd-1 (PPS) with start codes. Returns the NAL length size.
int ParseAvcC(const uint8_t* p, size_t n, std::vector<std::vector<uint8_t>>* csd) {
    if (n < 7 || p[0] != 1) return 0;
    const int nalLen = (p[4] & 3) + 1;
    size_t pos = 5;
    std::vector<uint8_t> sps, pps;
    const int numSps = p[pos++] & 0x1F;
    for (int i = 0; i < numSps; ++i) {
        if (pos + 2 > n) return nalLen;
        const size_t len = be16(p + pos);
        pos += 2;
        if (pos + len > n) return nalLen;
        AppendStartCode(&sps, p + pos, len);
        pos += len;
    }
    if (pos >= n) {
        csd->push_back(std::move(sps));
        return nalLen;
    }
    const int numPps = p[pos++];
    for (int i = 0; i < numPps; ++i) {
        if (pos + 2 > n) break;
        const size_t len = be16(p + pos);
        pos += 2;
        if (pos + len > n) break;
        AppendStartCode(&pps, p + pos, len);
        pos += len;
    }
    csd->push_back(std::move(sps));
    csd->push_back(std::move(pps));
    return nalLen;
}

// hvcC -> csd-0 holding every parameter set, start-code framed.
int ParseHvcC(const uint8_t* p, size_t n, std::vector<std::vector<uint8_t>>* csd) {
    if (n < 23) return 0;
    const int nalLen = (p[21] & 3) + 1;
    size_t pos = 22;
    const int arrays = p[pos++];
    std::vector<uint8_t> all;
    for (int a = 0; a < arrays; ++a) {
        if (pos + 3 > n) break;
        ++pos;  // array_completeness / NAL unit type
        const int count = be16(p + pos);
        pos += 2;
        for (int i = 0; i < count; ++i) {
            if (pos + 2 > n) break;
            const size_t len = be16(p + pos);
            pos += 2;
            if (pos + len > n) break;
            AppendStartCode(&all, p + pos, len);
            pos += len;
        }
    }
    csd->push_back(std::move(all));
    return nalLen;
}

// Matroska stores Vorbis as three Xiph-laced headers; MediaCodec wants the identification
// header and the setup header.
void ParseVorbisPrivate(const uint8_t* p, size_t n, std::vector<std::vector<uint8_t>>* csd) {
    if (n < 3 || p[0] != 2) return;
    size_t pos = 1;
    size_t sizes[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        size_t s = 0;
        while (pos < n && p[pos] == 255) {
            s += 255;
            ++pos;
        }
        if (pos >= n) return;
        s += p[pos++];
        sizes[i] = s;
    }
    if (pos + sizes[0] + sizes[1] > n) return;
    const size_t ident = pos;
    const size_t setup = pos + sizes[0] + sizes[1];
    csd->emplace_back(p + ident, p + ident + sizes[0]);
    csd->emplace_back(p + setup, p + n);
}

void SetOpusCsd(const uint8_t* p, size_t n, uint64_t codecDelayNs, uint64_t preRollNs,
                std::vector<std::vector<uint8_t>>* csd) {
    csd->emplace_back(p, p + n);
    std::vector<uint8_t> delay(8), preroll(8);
    for (int i = 0; i < 8; ++i) {
        delay[i] = static_cast<uint8_t>(codecDelayNs >> (8 * i));
        preroll[i] = static_cast<uint8_t>(preRollNs >> (8 * i));
    }
    csd->push_back(std::move(delay));
    csd->push_back(std::move(preroll));
}

void FinishTrack(Track* t) {
    uint32_t maxSize = 0;
    for (const Sample& s : t->samples) maxSize = std::max(maxSize, s.size);
    t->maxSampleSize = maxSize;
    if (t->kind == TrackKind::Audio) {
        for (Sample& s : t->samples) s.sync = true;
    }
    if (t->frameRate == 0 && t->kind == TrackKind::Video && t->samples.size() > 2) {
        // Median of the first few gaps survives a stray out-of-order timestamp.
        std::vector<int64_t> pts;
        for (size_t i = 0; i < t->samples.size() && i < 32; ++i) pts.push_back(t->samples[i].ptsUs);
        std::sort(pts.begin(), pts.end());
        std::vector<int64_t> gaps;
        for (size_t i = 1; i < pts.size(); ++i) {
            if (pts[i] > pts[i - 1]) gaps.push_back(pts[i] - pts[i - 1]);
        }
        if (!gaps.empty()) {
            std::sort(gaps.begin(), gaps.end());
            t->frameRate = static_cast<int>(std::llround(1e6 / static_cast<double>(gaps[gaps.size() / 2])));
        }
    }
    if (t->durationUs == 0 && !t->samples.empty()) {
        int64_t last = 0;
        for (const Sample& s : t->samples) last = std::max(last, s.ptsUs);
        t->durationUs = last;
    }
}

// ── Matroska / WebM ─────────────────────────────────────────────────────────

constexpr uint32_t kIdEbml = 0x1A45DFA3;
constexpr uint32_t kIdDocType = 0x4282;
constexpr uint32_t kIdSegment = 0x18538067;
constexpr uint32_t kIdSeekHead = 0x114D9B74;
constexpr uint32_t kIdInfo = 0x1549A966;
constexpr uint32_t kIdTimecodeScale = 0x2AD7B1;
constexpr uint32_t kIdDuration = 0x4489;
constexpr uint32_t kIdTracks = 0x1654AE6B;
constexpr uint32_t kIdTrackEntry = 0xAE;
constexpr uint32_t kIdTrackNumber = 0xD7;
constexpr uint32_t kIdTrackType = 0x83;
constexpr uint32_t kIdCodecId = 0x86;
constexpr uint32_t kIdCodecPrivate = 0x63A2;
constexpr uint32_t kIdCodecDelay = 0x56AA;
constexpr uint32_t kIdSeekPreRoll = 0x56BB;
constexpr uint32_t kIdDefaultDuration = 0x23E383;
constexpr uint32_t kIdLanguage = 0x22B59C;
constexpr uint32_t kIdVideo = 0xE0;
constexpr uint32_t kIdPixelWidth = 0xB0;
constexpr uint32_t kIdPixelHeight = 0xBA;
constexpr uint32_t kIdAudio = 0xE1;
constexpr uint32_t kIdSamplingFrequency = 0xB5;
constexpr uint32_t kIdChannels = 0x9F;
constexpr uint32_t kIdCluster = 0x1F43B675;
constexpr uint32_t kIdClusterTimecode = 0xE7;
constexpr uint32_t kIdSimpleBlock = 0xA3;
constexpr uint32_t kIdBlockGroup = 0xA0;
constexpr uint32_t kIdBlock = 0xA1;
constexpr uint32_t kIdReferenceBlock = 0xFB;
constexpr uint32_t kIdCues = 0x1C53BB6B;
constexpr uint32_t kIdTags = 0x1254C367;
constexpr uint32_t kIdChapters = 0x1043A770;
constexpr uint32_t kIdAttachments = 0x1941A469;

bool IsSegmentLevel(uint32_t id) {
    return id == kIdCluster || id == kIdCues || id == kIdTags || id == kIdSeekHead ||
           id == kIdInfo || id == kIdTracks || id == kIdChapters || id == kIdAttachments;
}

struct Element {
    uint32_t id = 0;
    int64_t dataStart = 0;
    int64_t size = -1;  // -1: unknown
};

// Element header at `pos`. EBML ids keep their length marker; sizes drop it.
bool ReadElement(Reader& r, int64_t pos, Element* e) {
    uint8_t first;
    if (!r.u8(pos, &first) || first == 0) return false;
    int idLen = 1;
    while (idLen <= 4 && !(first & (0x80 >> (idLen - 1)))) ++idLen;
    if (idLen > 4) return false;
    uint8_t idBytes[4];
    if (!r.read(pos, idBytes, idLen)) return false;
    uint32_t id = 0;
    for (int i = 0; i < idLen; ++i) id = (id << 8) | idBytes[i];
    uint8_t sizeFirst;
    if (!r.u8(pos + idLen, &sizeFirst) || sizeFirst == 0) return false;
    int sizeLen = 1;
    while (sizeLen <= 8 && !(sizeFirst & (0x80 >> (sizeLen - 1)))) ++sizeLen;
    if (sizeLen > 8) return false;
    uint8_t sizeBytes[8];
    if (!r.read(pos + idLen, sizeBytes, sizeLen)) return false;
    uint64_t size = sizeBytes[0] & (0xFFu >> sizeLen);
    bool allOnes = size == (0xFFu >> sizeLen);
    for (int i = 1; i < sizeLen; ++i) {
        size = (size << 8) | sizeBytes[i];
        if (sizeBytes[i] != 0xFF) allOnes = false;
    }
    e->id = id;
    e->dataStart = pos + idLen + sizeLen;
    e->size = allOnes ? -1 : static_cast<int64_t>(size);
    return true;
}

bool ReadUint(Reader& r, const Element& e, uint64_t* v) {
    if (e.size < 0 || e.size > 8) return false;
    uint8_t b[8];
    if (e.size > 0 && !r.read(e.dataStart, b, static_cast<size_t>(e.size))) return false;
    uint64_t out = 0;
    for (int64_t i = 0; i < e.size; ++i) out = (out << 8) | b[i];
    *v = out;
    return true;
}

bool ReadFloat(Reader& r, const Element& e, double* v) {
    if (e.size == 4) {
        uint8_t b[4];
        if (!r.read(e.dataStart, b, 4)) return false;
        const uint32_t u = be32(b);
        float f;
        std::memcpy(&f, &u, 4);
        *v = f;
        return true;
    }
    if (e.size == 8) {
        uint8_t b[8];
        if (!r.read(e.dataStart, b, 8)) return false;
        const uint64_t u = be64(b);
        double d;
        std::memcpy(&d, &u, 8);
        *v = d;
        return true;
    }
    return false;
}

bool ReadBytes(Reader& r, const Element& e, std::vector<uint8_t>* out) {
    if (e.size < 0 || e.size > (64 << 20)) return false;
    out->resize(static_cast<size_t>(e.size));
    return e.size == 0 || r.read(e.dataStart, out->data(), out->size());
}

struct MkvTrack {
    uint64_t number = 0;
    int type = 0;
    std::string codecId;
    std::vector<uint8_t> priv;
    uint64_t defaultDurationNs = 0;
    uint64_t codecDelayNs = 0;
    uint64_t seekPreRollNs = 0;
    std::string language = "und";
    int width = 0, height = 0, channels = 0;
    double sampleRate = 0;
};

class MkvParser {
public:
    MkvParser(Reader& r, Container* out) : r_(r), out_(out) {}

    bool parse(std::string* error) {
        Element e;
        if (!ReadElement(r_, 0, &e) || e.id != kIdEbml || e.size < 0) {
            if (error) *error = "not an EBML file";
            return false;
        }
        std::string docType = "matroska";
        for (int64_t pos = e.dataStart; pos < e.dataStart + e.size;) {
            Element c;
            if (!ReadElement(r_, pos, &c) || c.size < 0) break;
            if (c.id == kIdDocType) {
                std::vector<uint8_t> v;
                if (ReadBytes(r_, c, &v)) docType.assign(v.begin(), v.end());
            }
            pos = c.dataStart + c.size;
        }
        out_->format = docType == "webm" ? "webm" : "matroska";
        int64_t pos = e.dataStart + e.size;
        const int64_t fileEnd = r_.size();
        while (pos < fileEnd) {
            Element top;
            if (!ReadElement(r_, pos, &top)) break;
            if (top.id == kIdSegment) {
                const int64_t end = top.size < 0 ? fileEnd : std::min(fileEnd, top.dataStart + top.size);
                parseSegment(top.dataStart, end);
                break;
            }
            if (top.size < 0) break;
            pos = top.dataStart + top.size;
        }
        if (tracks_.empty()) {
            if (error) *error = "no tracks";
            return false;
        }
        finish();
        return true;
    }

private:
    void parseSegment(int64_t pos, int64_t end) {
        while (pos < end) {
            Element e;
            if (!ReadElement(r_, pos, &e)) return;
            int64_t childEnd = e.size < 0 ? end : std::min(end, e.dataStart + e.size);
            switch (e.id) {
                case kIdInfo:
                    parseInfo(e.dataStart, childEnd);
                    break;
                case kIdTracks:
                    parseTracks(e.dataStart, childEnd);
                    break;
                case kIdCluster:
                    childEnd = parseCluster(e.dataStart, childEnd, e.size < 0);
                    break;
                default:
                    break;
            }
            if (childEnd <= pos) return;
            pos = childEnd;
        }
    }

    void parseInfo(int64_t pos, int64_t end) {
        while (pos < end) {
            Element e;
            if (!ReadElement(r_, pos, &e) || e.size < 0) return;
            if (e.id == kIdTimecodeScale) {
                uint64_t v;
                if (ReadUint(r_, e, &v) && v > 0) timecodeScaleNs_ = v;
            } else if (e.id == kIdDuration) {
                double d;
                if (ReadFloat(r_, e, &d)) durationTicks_ = d;
            }
            pos = e.dataStart + e.size;
        }
    }

    void parseTracks(int64_t pos, int64_t end) {
        while (pos < end) {
            Element e;
            if (!ReadElement(r_, pos, &e) || e.size < 0) return;
            if (e.id == kIdTrackEntry) parseTrackEntry(e.dataStart, e.dataStart + e.size);
            pos = e.dataStart + e.size;
        }
    }

    void parseTrackEntry(int64_t pos, int64_t end) {
        MkvTrack t;
        while (pos < end) {
            Element e;
            if (!ReadElement(r_, pos, &e) || e.size < 0) return;
            uint64_t u;
            switch (e.id) {
                case kIdTrackNumber: if (ReadUint(r_, e, &u)) t.number = u; break;
                case kIdTrackType: if (ReadUint(r_, e, &u)) t.type = static_cast<int>(u); break;
                case kIdCodecId: {
                    std::vector<uint8_t> v;
                    if (ReadBytes(r_, e, &v)) t.codecId.assign(v.begin(), v.end());
                    while (!t.codecId.empty() && t.codecId.back() == '\0') t.codecId.pop_back();
                    break;
                }
                case kIdCodecPrivate: ReadBytes(r_, e, &t.priv); break;
                case kIdDefaultDuration: if (ReadUint(r_, e, &u)) t.defaultDurationNs = u; break;
                case kIdCodecDelay: if (ReadUint(r_, e, &u)) t.codecDelayNs = u; break;
                case kIdSeekPreRoll: if (ReadUint(r_, e, &u)) t.seekPreRollNs = u; break;
                case kIdLanguage: {
                    std::vector<uint8_t> v;
                    if (ReadBytes(r_, e, &v) && !v.empty()) t.language.assign(v.begin(), v.end());
                    break;
                }
                case kIdVideo:
                    for (int64_t p = e.dataStart; p < e.dataStart + e.size;) {
                        Element c;
                        if (!ReadElement(r_, p, &c) || c.size < 0) break;
                        if (c.id == kIdPixelWidth && ReadUint(r_, c, &u)) t.width = static_cast<int>(u);
                        if (c.id == kIdPixelHeight && ReadUint(r_, c, &u)) t.height = static_cast<int>(u);
                        p = c.dataStart + c.size;
                    }
                    break;
                case kIdAudio:
                    for (int64_t p = e.dataStart; p < e.dataStart + e.size;) {
                        Element c;
                        if (!ReadElement(r_, p, &c) || c.size < 0) break;
                        double d;
                        if (c.id == kIdSamplingFrequency && ReadFloat(r_, c, &d)) t.sampleRate = d;
                        if (c.id == kIdChannels && ReadUint(r_, c, &u)) t.channels = static_cast<int>(u);
                        p = c.dataStart + c.size;
                    }
                    break;
                default:
                    break;
            }
            pos = e.dataStart + e.size;
        }
        if (t.number == 0 || (t.type != 1 && t.type != 2)) return;
        Track track;
        track.kind = t.type == 1 ? TrackKind::Video : TrackKind::Audio;
        track.language = t.language;
        track.width = t.width;
        track.height = t.height;
        track.channels = t.channels;
        track.sampleRate = static_cast<int>(std::llround(t.sampleRate));
        if (t.defaultDurationNs > 0) {
            track.frameRate = static_cast<int>(std::llround(1e9 / static_cast<double>(t.defaultDurationNs)));
        }
        const std::string& c = t.codecId;
        const uint8_t* priv = t.priv.data();
        const size_t privN = t.priv.size();
        if (c == "V_VP8") track.mime = "video/x-vnd.on2.vp8";
        else if (c == "V_VP9") track.mime = "video/x-vnd.on2.vp9";
        else if (c == "V_AV1") track.mime = "video/av01";
        else if (c == "V_MPEG4/ISO/AVC") {
            track.mime = "video/avc";
            track.nalLengthSize = ParseAvcC(priv, privN, &track.csd);
        } else if (c == "V_MPEGH/ISO/HEVC") {
            track.mime = "video/hevc";
            track.nalLengthSize = ParseHvcC(priv, privN, &track.csd);
        } else if (c == "A_VORBIS") {
            track.mime = "audio/vorbis";
            ParseVorbisPrivate(priv, privN, &track.csd);
        } else if (c == "A_OPUS") {
            track.mime = "audio/opus";
            SetOpusCsd(priv, privN, t.codecDelayNs, t.seekPreRollNs, &track.csd);
        } else if (c.compare(0, 5, "A_AAC") == 0) {
            track.mime = "audio/mp4a-latm";
            if (privN > 0) track.csd.emplace_back(priv, priv + privN);
        } else if (c == "A_MPEG/L3") track.mime = "audio/mpeg";
        else if (c == "A_FLAC") {
            track.mime = "audio/flac";
            if (privN > 0) track.csd.emplace_back(priv, priv + privN);
        } else if (c == "A_AC3") track.mime = "audio/ac3";
        else track.mime = (track.kind == TrackKind::Video ? "video/" : "audio/") + c;
        numberToIndex_[t.number] = tracks_.size();
        tracks_.push_back(std::move(track));
    }

    // Returns where the cluster ends (the next element when its size was unknown).
    int64_t parseCluster(int64_t pos, int64_t end, bool unknownSize) {
        int64_t clusterTc = 0;
        while (pos < end) {
            Element e;
            if (!ReadElement(r_, pos, &e)) return end;
            if (unknownSize && IsSegmentLevel(e.id)) return pos;
            if (e.size < 0) return end;
            const int64_t next = e.dataStart + e.size;
            if (next > end && !unknownSize) return end;
            uint64_t u;
            switch (e.id) {
                case kIdClusterTimecode:
                    if (ReadUint(r_, e, &u)) clusterTc = static_cast<int64_t>(u);
                    break;
                case kIdSimpleBlock:
                    addBlock(e.dataStart, e.size, clusterTc, /*groupSync=*/-1);
                    break;
                case kIdBlockGroup: {
                    int64_t blockStart = 0, blockSize = -1;
                    bool hasReference = false;
                    for (int64_t p = e.dataStart; p < next;) {
                        Element c;
                        if (!ReadElement(r_, p, &c) || c.size < 0) break;
                        if (c.id == kIdBlock) { blockStart = c.dataStart; blockSize = c.size; }
                        if (c.id == kIdReferenceBlock) hasReference = true;
                        p = c.dataStart + c.size;
                    }
                    if (blockSize >= 0) addBlock(blockStart, blockSize, clusterTc, hasReference ? 0 : 1);
                    break;
                }
                default:
                    break;
            }
            pos = next;
        }
        return end;
    }

    // groupSync: -1 read the keyframe flag from the block, else the group's answer.
    void addBlock(int64_t pos, int64_t size, int64_t clusterTc, int groupSync) {
        uint8_t hdr[16];
        const size_t n = static_cast<size_t>(std::min<int64_t>(size, sizeof(hdr)));
        if (n < 4 || !r_.read(pos, hdr, n)) return;
        // Track number: an EBML variable-length integer.
        int tnLen = 1;
        while (tnLen <= 8 && !(hdr[0] & (0x80 >> (tnLen - 1)))) ++tnLen;
        if (tnLen > 3 || static_cast<size_t>(tnLen) + 3 > n) return;
        uint64_t number = hdr[0] & (0xFFu >> tnLen);
        for (int i = 1; i < tnLen; ++i) number = (number << 8) | hdr[i];
        const auto it = numberToIndex_.find(number);
        if (it == numberToIndex_.end()) return;
        const int16_t rel = static_cast<int16_t>(be16(hdr + tnLen));
        const uint8_t flags = hdr[tnLen + 2];
        const int64_t headerLen = tnLen + 3;
        const int64_t ptsUs = (clusterTc + rel) * static_cast<int64_t>(timecodeScaleNs_) / 1000;
        const bool sync = groupSync >= 0 ? groupSync != 0 : (flags & 0x80) != 0;
        Track& track = tracks_[it->second];
        const int lacing = (flags >> 1) & 3;
        const int64_t payload = pos + headerLen;
        const int64_t payloadSize = size - headerLen;
        if (payloadSize < 0) return;
        if (lacing == 0) {
            push(track, payload, payloadSize, ptsUs, sync);
            return;
        }
        uint8_t countByte;
        if (!r_.u8(payload, &countByte)) return;
        const int frames = countByte + 1;
        int64_t cursor = payload + 1;
        std::vector<int64_t> sizes(static_cast<size_t>(frames), 0);
        if (lacing == 1) {  // Xiph
            for (int i = 0; i < frames - 1; ++i) {
                int64_t s = 0;
                uint8_t b;
                do {
                    if (!r_.u8(cursor++, &b)) return;
                    s += b;
                } while (b == 255);
                sizes[static_cast<size_t>(i)] = s;
            }
        } else if (lacing == 3) {  // EBML
            auto readVint = [&](int64_t* value, bool isSigned) {
                uint8_t first;
                if (!r_.u8(cursor, &first) || first == 0) return false;
                int len = 1;
                while (len <= 8 && !(first & (0x80 >> (len - 1)))) ++len;
                uint8_t raw[8];
                if (len > 8 || !r_.read(cursor, raw, static_cast<size_t>(len))) return false;
                int64_t v = raw[0] & (0xFFu >> len);
                for (int i = 1; i < len; ++i) v = (v << 8) | raw[i];
                if (isSigned) v -= (int64_t(1) << (7 * len - 1)) - 1;
                cursor += len;
                *value = v;
                return true;
            };
            int64_t prev = 0;
            if (!readVint(&prev, false)) return;
            sizes[0] = prev;
            for (int i = 1; i < frames - 1; ++i) {
                int64_t delta;
                if (!readVint(&delta, true)) return;
                prev += delta;
                sizes[static_cast<size_t>(i)] = prev;
            }
        }
        const int64_t dataStart = cursor;
        const int64_t total = payload + payloadSize - dataStart;
        if (lacing == 2) {
            const int64_t each = total / frames;
            for (auto& s : sizes) s = each;
        } else {
            int64_t used = 0;
            for (int i = 0; i < frames - 1; ++i) used += sizes[static_cast<size_t>(i)];
            sizes[static_cast<size_t>(frames - 1)] = total - used;
        }
        int64_t offset = dataStart;
        for (int i = 0; i < frames; ++i) {
            const int64_t s = sizes[static_cast<size_t>(i)];
            if (s < 0 || offset + s > payload + payloadSize) return;
            push(track, offset, s, ptsUs, sync && i == 0);
            offset += s;
        }
    }

    void push(Track& track, int64_t offset, int64_t size, int64_t ptsUs, bool sync) {
        if (size <= 0 || size > 0xFFFFFFFFll) return;
        Sample s;
        s.offset = offset;
        s.size = static_cast<uint32_t>(size);
        s.ptsUs = ptsUs;
        s.sync = sync;
        track.samples.push_back(s);
    }

    void finish() {
        out_->durationUs = static_cast<int64_t>(durationTicks_ * static_cast<double>(timecodeScaleNs_) / 1000.0);
        for (Track& t : tracks_) {
            if (t.durationUs == 0) t.durationUs = out_->durationUs;
            FinishTrack(&t);
        }
        if (out_->durationUs == 0) {
            for (const Track& t : tracks_) out_->durationUs = std::max(out_->durationUs, t.durationUs);
        }
        out_->tracks = std::move(tracks_);
    }

    Reader& r_;
    Container* out_;
    uint64_t timecodeScaleNs_ = 1000000;
    double durationTicks_ = 0;
    std::vector<Track> tracks_;
    std::map<uint64_t, size_t> numberToIndex_;
};

// ── ISO base media (MP4 / MOV) ──────────────────────────────────────────────

struct Box {
    uint32_t type = 0;
    size_t dataStart = 0;
    size_t end = 0;
};

constexpr uint32_t Fourcc(const char (&s)[5]) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(s[0])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(s[3]));
}

// Box at `pos` inside an in-memory region [0, n).
bool NextBox(const uint8_t* d, size_t n, size_t pos, Box* b) {
    if (pos + 8 > n) return false;
    uint64_t size = be32(d + pos);
    b->type = be32(d + pos + 4);
    size_t header = 8;
    if (size == 1) {
        if (pos + 16 > n) return false;
        size = be64(d + pos + 8);
        header = 16;
    } else if (size == 0) {
        size = n - pos;
    }
    if (size < header || pos + size > n) return false;
    b->dataStart = pos + header;
    b->end = static_cast<size_t>(pos + size);
    return true;
}

bool FindBox(const uint8_t* d, size_t start, size_t end, uint32_t type, Box* out) {
    for (size_t pos = start; pos < end;) {
        Box b;
        if (!NextBox(d, end, pos, &b)) return false;
        if (b.type == type) {
            *out = b;
            return true;
        }
        pos = b.end;
    }
    return false;
}

struct Mp4Tables {
    std::vector<uint32_t> sampleSizes;
    std::vector<std::pair<uint32_t, uint32_t>> stts;   // count, delta
    std::vector<std::pair<uint32_t, int32_t>> ctts;    // count, offset
    struct Stsc { uint32_t firstChunk, samplesPerChunk; };
    std::vector<Stsc> stsc;
    std::vector<uint64_t> chunkOffsets;
    std::vector<uint32_t> syncSamples;  // 1-based; empty: every sample is sync
    bool hasStss = false;
};

// Length of an MPEG-4 descriptor's size field (variable, 7 bits per byte).
bool ReadDescriptorSize(const uint8_t* d, size_t n, size_t* pos, size_t* size) {
    size_t value = 0;
    for (int i = 0; i < 4; ++i) {
        if (*pos >= n) return false;
        const uint8_t b = d[(*pos)++];
        value = (value << 7) | (b & 0x7F);
        if (!(b & 0x80)) {
            *size = value;
            return true;
        }
    }
    return false;
}

void ParseEsds(const uint8_t* d, size_t n, Track* track) {
    if (n < 4) return;
    size_t pos = 4;  // version + flags
    int objectType = 0;
    while (pos < n) {
        const uint8_t tag = d[pos++];
        size_t size;
        if (!ReadDescriptorSize(d, n, &pos, &size)) return;
        if (tag == 0x03) {
            pos += 3;  // ES_ID + flags (stream dependence / URL / OCR bits ignored)
        } else if (tag == 0x04) {
            if (pos + 13 > n) return;
            objectType = d[pos];
            pos += 13;
        } else if (tag == 0x05) {
            if (pos + size > n) return;
            track->csd.emplace_back(d + pos, d + pos + size);
            pos += size;
        } else {
            pos += size;
        }
    }
    if (objectType == 0x6B || objectType == 0x69) track->mime = "audio/mpeg";
    else if (objectType != 0) track->mime = "audio/mp4a-latm";
}

bool BuildMp4Track(const uint8_t* d, const Box& trak, int64_t movieTimescale, int64_t movieDurationUs,
                   Track* track) {
    (void)movieTimescale;
    Box mdia, mdhd, hdlr, minf, stbl, stsd, tkhd;
    if (!FindBox(d, trak.dataStart, trak.end, Fourcc("mdia"), &mdia)) return false;
    if (!FindBox(d, mdia.dataStart, mdia.end, Fourcc("mdhd"), &mdhd)) return false;
    if (!FindBox(d, mdia.dataStart, mdia.end, Fourcc("hdlr"), &hdlr)) return false;
    if (!FindBox(d, mdia.dataStart, mdia.end, Fourcc("minf"), &minf)) return false;
    if (!FindBox(d, minf.dataStart, minf.end, Fourcc("stbl"), &stbl)) return false;
    if (!FindBox(d, stbl.dataStart, stbl.end, Fourcc("stsd"), &stsd)) return false;

    // Handler type.
    if (hdlr.dataStart + 12 > hdlr.end) return false;
    const uint32_t handler = be32(d + hdlr.dataStart + 8);
    if (handler == Fourcc("vide")) track->kind = TrackKind::Video;
    else if (handler == Fourcc("soun")) track->kind = TrackKind::Audio;
    else return false;

    // Media timescale and duration.
    size_t p = mdhd.dataStart;
    const int version = d[p];
    uint32_t timescale;
    uint64_t duration;
    if (version == 1) {
        if (p + 32 > mdhd.end) return false;
        timescale = be32(d + p + 20);
        duration = be64(d + p + 24);
        p += 32;
    } else {
        if (p + 20 > mdhd.end) return false;
        timescale = be32(d + p + 12);
        duration = be32(d + p + 16);
        p += 20;
    }
    if (timescale == 0) return false;
    track->durationUs = static_cast<int64_t>(duration * 1000000ull / timescale);
    if (p + 2 <= mdhd.end) {
        const uint16_t lang = be16(d + p);
        char code[4] = {static_cast<char>(((lang >> 10) & 31) + 0x60), static_cast<char>(((lang >> 5) & 31) + 0x60),
                        static_cast<char>((lang & 31) + 0x60), 0};
        track->language = lang == 0 ? "und" : code;
    } else {
        track->language = "und";
    }
    if (track->durationUs == 0) track->durationUs = movieDurationUs;

    // Display size from tkhd (16.16 fixed point at the end).
    if (FindBox(d, trak.dataStart, trak.end, Fourcc("tkhd"), &tkhd)) {
        const size_t sizePos = tkhd.end - 8;
        if (sizePos >= tkhd.dataStart) {
            track->width = static_cast<int>(be32(d + sizePos) >> 16);
            track->height = static_cast<int>(be32(d + sizePos + 4) >> 16);
        }
    }

    // Sample description (first entry).
    if (stsd.dataStart + 8 > stsd.end) return false;
    Box entry;
    if (!NextBox(d, stsd.end, stsd.dataStart + 8, &entry)) return false;
    const uint32_t fmt = entry.type;
    if (track->kind == TrackKind::Video) {
        if (entry.dataStart + 78 > entry.end) return false;
        const int w = be16(d + entry.dataStart + 24);
        const int h = be16(d + entry.dataStart + 26);
        if (w > 0 && h > 0) {
            track->width = w;
            track->height = h;
        }
        for (size_t cp = entry.dataStart + 78; cp < entry.end;) {
            Box c;
            if (!NextBox(d, entry.end, cp, &c)) break;
            if (c.type == Fourcc("avcC")) track->nalLengthSize = ParseAvcC(d + c.dataStart, c.end - c.dataStart, &track->csd);
            if (c.type == Fourcc("hvcC")) track->nalLengthSize = ParseHvcC(d + c.dataStart, c.end - c.dataStart, &track->csd);
            cp = c.end;
        }
        if (fmt == Fourcc("avc1") || fmt == Fourcc("avc3")) track->mime = "video/avc";
        else if (fmt == Fourcc("hvc1") || fmt == Fourcc("hev1")) track->mime = "video/hevc";
        else if (fmt == Fourcc("vp09")) track->mime = "video/x-vnd.on2.vp9";
        else if (fmt == Fourcc("av01")) track->mime = "video/av01";
        else if (fmt == Fourcc("mp4v")) track->mime = "video/mp4v-es";
        else if (fmt == Fourcc("s263")) track->mime = "video/3gpp";
        else return false;
    } else {
        if (entry.dataStart + 28 > entry.end) return false;
        track->channels = be16(d + entry.dataStart + 16);
        track->sampleRate = static_cast<int>(be32(d + entry.dataStart + 24) >> 16);
        for (size_t cp = entry.dataStart + 28; cp < entry.end;) {
            Box c;
            if (!NextBox(d, entry.end, cp, &c)) break;
            if (c.type == Fourcc("esds")) ParseEsds(d + c.dataStart, c.end - c.dataStart, track);
            cp = c.end;
        }
        if (fmt == Fourcc("mp4a")) {
            if (track->mime.empty()) track->mime = "audio/mp4a-latm";
        } else if (fmt == Fourcc(".mp3")) track->mime = "audio/mpeg";
        else if (fmt == Fourcc("samr")) track->mime = "audio/3gpp";
        else if (fmt == Fourcc("sawb")) track->mime = "audio/amr-wb";
        else if (fmt == Fourcc("ac-3")) track->mime = "audio/ac3";
        else return false;
    }

    // Sample tables.
    Mp4Tables t;
    for (size_t sp = stbl.dataStart; sp < stbl.end;) {
        Box b;
        if (!NextBox(d, stbl.end, sp, &b)) break;
        const uint8_t* body = d + b.dataStart;
        const size_t len = b.end - b.dataStart;
        if (b.type == Fourcc("stts") && len >= 8) {
            const uint32_t cnt = be32(body + 4);
            for (uint32_t i = 0; i < cnt && 8 + (i + 1) * 8 <= len; ++i)
                t.stts.emplace_back(be32(body + 8 + i * 8), be32(body + 12 + i * 8));
        } else if (b.type == Fourcc("ctts") && len >= 8) {
            const uint32_t cnt = be32(body + 4);
            for (uint32_t i = 0; i < cnt && 8 + (i + 1) * 8 <= len; ++i)
                t.ctts.emplace_back(be32(body + 8 + i * 8), static_cast<int32_t>(be32(body + 12 + i * 8)));
        } else if (b.type == Fourcc("stsc") && len >= 8) {
            const uint32_t cnt = be32(body + 4);
            for (uint32_t i = 0; i < cnt && 8 + (i + 1) * 12 <= len; ++i)
                t.stsc.push_back({be32(body + 8 + i * 12), be32(body + 12 + i * 12)});
        } else if (b.type == Fourcc("stsz") && len >= 12) {
            const uint32_t fixed = be32(body + 4);
            const uint32_t cnt = be32(body + 8);
            t.sampleSizes.resize(cnt);
            for (uint32_t i = 0; i < cnt; ++i) {
                if (fixed != 0) t.sampleSizes[i] = fixed;
                else if (12 + (i + 1) * 4 <= len) t.sampleSizes[i] = be32(body + 12 + i * 4);
            }
        } else if (b.type == Fourcc("stco") && len >= 8) {
            const uint32_t cnt = be32(body + 4);
            for (uint32_t i = 0; i < cnt && 8 + (i + 1) * 4 <= len; ++i)
                t.chunkOffsets.push_back(be32(body + 8 + i * 4));
        } else if (b.type == Fourcc("co64") && len >= 8) {
            const uint32_t cnt = be32(body + 4);
            for (uint32_t i = 0; i < cnt && 8 + (i + 1) * 8 <= len; ++i)
                t.chunkOffsets.push_back(be64(body + 8 + i * 8));
        } else if (b.type == Fourcc("stss") && len >= 8) {
            t.hasStss = true;
            const uint32_t cnt = be32(body + 4);
            for (uint32_t i = 0; i < cnt && 8 + (i + 1) * 4 <= len; ++i)
                t.syncSamples.push_back(be32(body + 8 + i * 4));
        }
        sp = b.end;
    }
    if (t.sampleSizes.empty() || t.chunkOffsets.empty() || t.stsc.empty()) return false;

    const size_t count = t.sampleSizes.size();
    track->samples.resize(count);
    // Offsets: walk chunks, spreading samples per the sample-to-chunk runs.
    size_t sample = 0;
    for (size_t chunk = 0; chunk < t.chunkOffsets.size() && sample < count; ++chunk) {
        size_t run = 0;
        while (run + 1 < t.stsc.size() && t.stsc[run + 1].firstChunk <= chunk + 1) ++run;
        uint64_t offset = t.chunkOffsets[chunk];
        for (uint32_t k = 0; k < t.stsc[run].samplesPerChunk && sample < count; ++k, ++sample) {
            track->samples[sample].offset = static_cast<int64_t>(offset);
            track->samples[sample].size = t.sampleSizes[sample];
            offset += t.sampleSizes[sample];
        }
    }
    // Timestamps: decode time from stts, composition offset from ctts.
    {
        int64_t dts = 0;
        size_t i = 0;
        for (const auto& run : t.stts) {
            for (uint32_t k = 0; k < run.first && i < count; ++k, ++i) {
                track->samples[i].ptsUs = dts;  // ticks for now
                dts += run.second;
            }
        }
        i = 0;
        for (const auto& run : t.ctts) {
            for (uint32_t k = 0; k < run.first && i < count; ++k, ++i) track->samples[i].ptsUs += run.second;
        }
        for (Sample& s : track->samples) s.ptsUs = s.ptsUs * 1000000ll / static_cast<int64_t>(timescale);
    }
    if (t.hasStss) {
        for (uint32_t s : t.syncSamples) {
            if (s >= 1 && s <= count) track->samples[s - 1].sync = true;
        }
    } else {
        for (Sample& s : track->samples) s.sync = true;
    }
    if (track->kind == TrackKind::Video && track->durationUs > 0 && count > 0) {
        track->frameRate = static_cast<int>(std::llround(static_cast<double>(count) * 1e6 /
                                                         static_cast<double>(track->durationUs)));
    }
    return true;
}

bool ParseMp4(Reader& r, Container* out, std::string* error) {
    // Locate moov among the top-level boxes without reading mdat.
    int64_t pos = 0;
    int64_t moovStart = -1, moovSize = 0;
    const int64_t fileEnd = r.size();
    while (pos + 8 <= fileEnd) {
        uint8_t h[16];
        if (!r.read(pos, h, 8)) break;
        uint64_t size = be32(h);
        const uint32_t type = be32(h + 4);
        int64_t header = 8;
        if (size == 1) {
            if (!r.read(pos, h, 16)) break;
            size = be64(h + 8);
            header = 16;
        } else if (size == 0) {
            size = static_cast<uint64_t>(fileEnd - pos);
        }
        if (size < static_cast<uint64_t>(header)) break;
        if (type == Fourcc("moov")) {
            moovStart = pos + header;
            moovSize = static_cast<int64_t>(size) - header;
            break;
        }
        pos += static_cast<int64_t>(size);
    }
    if (moovStart < 0 || moovSize <= 0 || moovSize > (256ll << 20) || moovStart + moovSize > fileEnd) {
        if (error) *error = "no moov box";
        return false;
    }
    std::vector<uint8_t> moov(static_cast<size_t>(moovSize));
    if (!r.read(moovStart, moov.data(), moov.size())) {
        if (error) *error = "short moov";
        return false;
    }
    const uint8_t* d = moov.data();
    int64_t movieTimescale = 1000, movieDurationUs = 0;
    Box mvhd;
    if (FindBox(d, 0, moov.size(), Fourcc("mvhd"), &mvhd)) {
        const size_t p = mvhd.dataStart;
        if (d[p] == 1 && p + 32 <= mvhd.end) {
            movieTimescale = be32(d + p + 20);
            movieDurationUs = static_cast<int64_t>(be64(d + p + 24) * 1000000ull / std::max<int64_t>(1, movieTimescale));
        } else if (p + 20 <= mvhd.end) {
            movieTimescale = be32(d + p + 12);
            movieDurationUs = static_cast<int64_t>(be32(d + p + 16) * 1000000ull / std::max<int64_t>(1, movieTimescale));
        }
    }
    for (size_t p = 0; p < moov.size();) {
        Box b;
        if (!NextBox(d, moov.size(), p, &b)) break;
        if (b.type == Fourcc("trak")) {
            Track t;
            if (BuildMp4Track(d, b, movieTimescale, movieDurationUs, &t)) {
                FinishTrack(&t);
                out->tracks.push_back(std::move(t));
            }
        }
        p = b.end;
    }
    if (out->tracks.empty()) {
        if (error) *error = "no playable tracks";
        return false;
    }
    out->format = "mp4";
    out->durationUs = movieDurationUs;
    for (const Track& t : out->tracks) out->durationUs = std::max(out->durationUs, t.durationUs);
    return true;
}

}  // namespace

bool ParseContainer(ByteSource& source, Container* out, std::string* error) {
    if (out == nullptr) return false;
    *out = Container();
    Reader reader(source);
    uint8_t head[12];
    if (reader.size() < 12 || !reader.read(0, head, sizeof(head))) {
        if (error) *error = "file too small";
        return false;
    }
    if (be32(head) == kIdEbml) {
        MkvParser parser(reader, out);
        return parser.parse(error);
    }
    if (std::memcmp(head + 4, "ftyp", 4) == 0 || std::memcmp(head + 4, "moov", 4) == 0 ||
        std::memcmp(head + 4, "mdat", 4) == 0 || std::memcmp(head + 4, "free", 4) == 0 ||
        std::memcmp(head + 4, "wide", 4) == 0) {
        return ParseMp4(reader, out, error);
    }
    if (error) *error = "unrecognised container";
    return false;
}

namespace {

size_t ConvertedSize(const uint8_t* raw, size_t n, int nalLen) {
    if (nalLen == 4) return n;
    size_t pos = 0, out = 0;
    while (pos + static_cast<size_t>(nalLen) <= n) {
        size_t len = 0;
        for (int i = 0; i < nalLen; ++i) len = (len << 8) | raw[pos + i];
        pos += static_cast<size_t>(nalLen);
        if (len > n - pos) len = n - pos;
        out += 4 + len;
        pos += len;
    }
    return out;
}

}  // namespace

size_t ConvertedSampleSize(ByteSource& source, const Track& track, size_t sampleIndex) {
    if (sampleIndex >= track.samples.size()) return 0;
    const Sample& s = track.samples[sampleIndex];
    if (track.nalLengthSize == 0 || track.nalLengthSize == 4) return s.size;
    std::vector<uint8_t> raw(s.size);
    if (source.read(s.offset, raw.data(), raw.size()) != static_cast<int64_t>(raw.size())) return s.size;
    return ConvertedSize(raw.data(), raw.size(), track.nalLengthSize);
}

int64_t ReadSample(ByteSource& source, const Track& track, size_t sampleIndex, uint8_t* dst,
                   size_t capacity) {
    if (sampleIndex >= track.samples.size()) return -1;
    const Sample& s = track.samples[sampleIndex];
    const int nalLen = track.nalLengthSize;
    if (nalLen == 0) {
        if (capacity < s.size) return -1;
        return source.read(s.offset, dst, s.size) == static_cast<int64_t>(s.size)
                   ? static_cast<int64_t>(s.size)
                   : -1;
    }
    std::vector<uint8_t> raw(s.size);
    if (source.read(s.offset, raw.data(), raw.size()) != static_cast<int64_t>(raw.size())) return -1;
    const size_t need = ConvertedSize(raw.data(), raw.size(), nalLen);
    if (capacity < need) return -1;
    size_t pos = 0, out = 0;
    while (pos + static_cast<size_t>(nalLen) <= raw.size()) {
        size_t len = 0;
        for (int i = 0; i < nalLen; ++i) len = (len << 8) | raw[pos + i];
        pos += static_cast<size_t>(nalLen);
        if (len > raw.size() - pos) len = raw.size() - pos;
        std::memcpy(dst + out, kStartCode, 4);
        std::memcpy(dst + out + 4, raw.data() + pos, len);
        out += 4 + len;
        pos += len;
    }
    return static_cast<int64_t>(out);
}

}  // namespace media
}  // namespace kudroid
