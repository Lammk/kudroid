#include "kudroid/platform/MediaDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>
#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>
#include <vorbis/codec.h>

namespace kudroid::media {
namespace {

class Vp8Decoder final : public Decoder {
    vpx_codec_ctx_t codec_{};
    bool initialized_ = false;
public:
    bool init(int width, int height, std::string* error) {
        vpx_codec_dec_cfg_t config{};
        config.threads = std::max(1u, std::min(4u, std::thread::hardware_concurrency()));
        config.w = static_cast<unsigned>(std::max(0, width));
        config.h = static_cast<unsigned>(std::max(0, height));
        initialized_ = vpx_codec_dec_init(&codec_, vpx_codec_vp8_dx(), &config, 0) == VPX_CODEC_OK;
        if (!initialized_) *error = vpx_codec_error(&codec_);
        return initialized_;
    }
    ~Vp8Decoder() override { if (initialized_) vpx_codec_destroy(&codec_); }

    bool decode(const uint8_t* data, size_t size, int64_t ptsUs,
                std::vector<DecodedBuffer>* output, std::string* error) override {
        if (size == 0) return true;
        if (size > std::numeric_limits<unsigned>::max() ||
            vpx_codec_decode(&codec_, data, static_cast<unsigned>(size), nullptr, 0) != VPX_CODEC_OK) {
            *error = vpx_codec_error(&codec_);
            return false;
        }
        vpx_codec_iter_t iterator = nullptr;
        while (const auto* image = vpx_codec_get_frame(&codec_, &iterator)) {
            const size_t width = image->d_w, height = image->d_h;
            const size_t cw = (width + 1) / 2, ch = (height + 1) / 2;
            if (image->fmt != VPX_IMG_FMT_I420 || width == 0 || height == 0 ||
                width > 16384 || height > 16384 || width * height + 2 * cw * ch > (256u << 20)) {
                *error = "Unsupported decoded VP8 image layout";
                return false;
            }
            DecodedBuffer frame;
            frame.ptsUs = ptsUs;
            frame.width = static_cast<int>(width);
            frame.height = static_cast<int>(height);
            frame.bytes.resize(width * height + 2 * cw * ch);
            size_t offset = 0;
            for (int plane = 0; plane < 3; ++plane) {
                const size_t pw = plane == 0 ? width : cw, ph = plane == 0 ? height : ch;
                for (size_t row = 0; row < ph; ++row) {
                    std::memcpy(frame.bytes.data() + offset,
                        image->planes[plane] + static_cast<ptrdiff_t>(row) * image->stride[plane], pw);
                    offset += pw;
                }
            }
            output->push_back(std::move(frame));
        }
        return true;
    }
};

class VorbisDecoder final : public Decoder {
    vorbis_info info_{};
    vorbis_comment comments_{};
    vorbis_dsp_state dsp_{};
    vorbis_block block_{};
    bool dspReady_ = false, blockReady_ = false;
    int64_t packetNumber_ = 0;
    bool haveTime_ = false;
    int64_t basePtsUs_ = 0, emitted_ = 0;

    bool header(const std::vector<uint8_t>& bytes, bool first) {
        if (bytes.size() > static_cast<size_t>(std::numeric_limits<long>::max())) return false;
        ogg_packet packet{};
        packet.packet = const_cast<unsigned char*>(bytes.data());
        packet.bytes = static_cast<long>(bytes.size());
        packet.b_o_s = first;
        packet.packetno = packetNumber_++;
        return vorbis_synthesis_headerin(&info_, &comments_, &packet) == 0;
    }
public:
    VorbisDecoder() { vorbis_info_init(&info_); vorbis_comment_init(&comments_); }
    ~VorbisDecoder() override {
        if (blockReady_) vorbis_block_clear(&block_);
        if (dspReady_) vorbis_dsp_clear(&dsp_);
        vorbis_comment_clear(&comments_);
        vorbis_info_clear(&info_);
    }
    bool init(const std::vector<std::vector<uint8_t>>& csd, int* rate, int* channels,
              std::string* error) {
        // Android stores the identification and setup headers; comments are optional metadata.
        const std::vector<uint8_t> emptyComment = {3, 'v', 'o', 'r', 'b', 'i', 's',
            0, 0, 0, 0, 0, 0, 0, 0, 1};
        if (csd.size() < 2 || !header(csd[0], true) || !header(emptyComment, false) ||
            !header(csd[1], false) || info_.channels <= 0 || info_.channels > 2 ||
            info_.rate <= 0 || info_.rate > 384000) {
            *error = "Invalid Vorbis configuration headers";
            return false;
        }
        dspReady_ = vorbis_synthesis_init(&dsp_, &info_) == 0;
        if (dspReady_) blockReady_ = vorbis_block_init(&dsp_, &block_) == 0;
        if (!blockReady_) { *error = "Cannot initialize Vorbis synthesis"; return false; }
        *rate = static_cast<int>(info_.rate);
        *channels = info_.channels;
        return true;
    }
    bool decode(const uint8_t* data, size_t size, int64_t ptsUs,
                std::vector<DecodedBuffer>* output, std::string* error) override {
        if (size == 0) return true;
        if (size > static_cast<size_t>(std::numeric_limits<long>::max())) return false;
        ogg_packet packet{};
        packet.packet = const_cast<unsigned char*>(data);
        packet.bytes = static_cast<long>(size);
        packet.packetno = packetNumber_++;
        packet.granulepos = -1;
        if (vorbis_synthesis(&block_, &packet) != 0 || vorbis_synthesis_blockin(&dsp_, &block_) != 0) {
            *error = "Invalid Vorbis audio packet";
            return false;
        }
        if (!haveTime_) { basePtsUs_ = ptsUs; haveTime_ = true; }
        float** pcm = nullptr;
        int samples = 0;
        while ((samples = vorbis_synthesis_pcmout(&dsp_, &pcm)) > 0) {
            DecodedBuffer buffer;
            buffer.ptsUs = basePtsUs_ + emitted_ * 1000000 / info_.rate;
            buffer.bytes.resize(static_cast<size_t>(samples) * info_.channels * sizeof(int16_t));
            for (int sample = 0; sample < samples; ++sample) {
                for (int channel = 0; channel < info_.channels; ++channel) {
                    const float value = pcm[channel][sample];
                    const double scaled = std::isfinite(value) ? static_cast<double>(value) * 32768 : 0;
                    const int16_t result = static_cast<int16_t>(std::max(-32768.0, std::min(32767.0, std::nearbyint(scaled))));
                    const size_t offset = (static_cast<size_t>(sample) * info_.channels + channel) * 2;
                    buffer.bytes[offset] = static_cast<uint8_t>(result);
                    buffer.bytes[offset + 1] = static_cast<uint8_t>(static_cast<uint16_t>(result) >> 8);
                }
            }
            emitted_ += samples;
            vorbis_synthesis_read(&dsp_, samples);
            output->push_back(std::move(buffer));
        }
        return true;
    }
};
}

bool HasDecoder(const std::string& mime) {
    return mime == "video/x-vnd.on2.vp8" || mime == "audio/vorbis";
}

std::unique_ptr<Decoder> CreateDecoder(const std::string& mime,
    const std::vector<std::vector<uint8_t>>& csd, int width, int height,
    int* sampleRate, int* channels, std::string* error) {
    if (mime == "video/x-vnd.on2.vp8") {
        auto decoder = std::make_unique<Vp8Decoder>();
        if (decoder->init(width, height, error)) return decoder;
    } else if (mime == "audio/vorbis") {
        auto decoder = std::make_unique<VorbisDecoder>();
        if (decoder->init(csd, sampleRate, channels, error)) return decoder;
    } else {
        *error = "Unsupported media decoder";
    }
    return nullptr;
}
}
