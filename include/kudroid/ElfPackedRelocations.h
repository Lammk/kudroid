#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace kudroid {

struct PackedRela {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
};

bool decodeAndroidPackedRela(const uint8_t* data, size_t size,
                            const std::function<bool(const PackedRela&)>& apply,
                            std::string& error);

} // namespace kudroid
