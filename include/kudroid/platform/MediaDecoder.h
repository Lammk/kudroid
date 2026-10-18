#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kudroid::media {

struct DecodedBuffer {
    std::vector<uint8_t> bytes;
    int64_t ptsUs = 0;
    int width = 0, height = 0;
};

class Decoder {
public:
    virtual ~Decoder() = default;
    virtual bool decode(const uint8_t* data, size_t size, int64_t ptsUs,
                        std::vector<DecodedBuffer>* output, std::string* error) = 0;
};

bool HasDecoder(const std::string& mime);
std::unique_ptr<Decoder> CreateDecoder(const std::string& mime,
    const std::vector<std::vector<uint8_t>>& csd, int width, int height,
    int* sampleRate, int* channels, std::string* error);

}
