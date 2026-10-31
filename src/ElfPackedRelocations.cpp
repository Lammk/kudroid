#include "kudroid/ElfPackedRelocations.h"

#include <cstring>
#include <limits>

namespace kudroid {
namespace {

class SlebReader {
public:
    SlebReader(const uint8_t* data, size_t size) : current_(data), end_(data + size) {}

    bool read(int64_t& result) {
        uint64_t bits = 0;
        for (unsigned shift = 0; shift < 64; shift += 7) {
            if (current_ == end_) return false;
            const uint8_t byte = *current_++;
            if (shift == 63 && byte != 0 && byte != 127) return false;
            bits |= uint64_t(byte & 127) << shift;
            if (!(byte & 128)) {
                if (shift < 57 && (byte & 64)) bits |= ~uint64_t(0) << (shift + 7);
                std::memcpy(&result, &bits, sizeof(result));
                return true;
            }
        }
        return false;
    }

private:
    const uint8_t* current_;
    const uint8_t* end_;
};

bool addSigned(int64_t& value, int64_t delta) {
    if ((delta > 0 && value > INT64_MAX - delta) ||
        (delta < 0 && value < INT64_MIN - delta)) return false;
    value += delta;
    return true;
}

bool addOffset(uint64_t& value, int64_t delta) {
    if (delta < 0) {
        const uint64_t magnitude = uint64_t(-(delta + 1)) + 1;
        if (value < magnitude) return false;
        value -= magnitude;
    } else {
        if (value > UINT64_MAX - uint64_t(delta)) return false;
        value += uint64_t(delta);
    }
    return true;
}

} // namespace

bool decodeAndroidPackedRela(const uint8_t* data, size_t size,
                            const std::function<bool(const PackedRela&)>& apply,
                            std::string& error) {
    auto invalid = [&] { error = "Invalid Android packed RELA stream"; return false; };
    if (!data || size < 4 || std::memcmp(data, "APS2", 4) != 0) return invalid();
    SlebReader reader(data + 4, size - 4);
    int64_t count, initialOffset;
    if (!reader.read(count) || count < 0 || !reader.read(initialOffset) || initialOffset < 0 ||
        uint64_t(count) > std::numeric_limits<size_t>::max() / sizeof(PackedRela)) return invalid();
    PackedRela reloc{uint64_t(initialOffset), 0, 0};
    int64_t done = 0;
    while (done < count) {
        int64_t groupSize, flags, offsetDelta = 0, info, addendDelta;
        if (!reader.read(groupSize) || groupSize <= 0 || groupSize > count - done ||
            !reader.read(flags) || flags < 0 || (flags & ~15)) return invalid();
        if ((flags & 2) && !reader.read(offsetDelta)) return invalid();
        if (flags & 1) {
            if (!reader.read(info)) return invalid();
            reloc.info = uint64_t(info);
        }
        if ((flags & 12) == 12) {
            if (!reader.read(addendDelta) || !addSigned(reloc.addend, addendDelta)) return invalid();
        } else if (!(flags & 8)) {
            reloc.addend = 0;
        }
        for (int64_t index = 0; index < groupSize; ++index) {
            if (!(flags & 2) && !reader.read(offsetDelta)) return invalid();
            if (!addOffset(reloc.offset, offsetDelta)) return invalid();
            if (!(flags & 1)) {
                if (!reader.read(info)) return invalid();
                reloc.info = uint64_t(info);
            }
            if ((flags & 12) == 8 &&
                (!reader.read(addendDelta) || !addSigned(reloc.addend, addendDelta))) return invalid();
            if (!apply(reloc)) return false;
        }
        done += groupSize;
    }
    return true;
}

} // namespace kudroid
