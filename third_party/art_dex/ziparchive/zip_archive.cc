#include "ziparchive/zip_archive.h"

#include <string.h>

#include <unordered_map>
#include <vector>

#include <zlib.h>

namespace {

constexpr uint32_t kEocdSignature = 0x06054b50;
constexpr uint32_t kCdSignature = 0x02014b50;
constexpr uint32_t kLocalSignature = 0x04034b50;

constexpr int32_t kOk = 0;
constexpr int32_t kInvalidOffset = -1;
constexpr int32_t kEntryNotFound = -2;
constexpr int32_t kInflateError = -3;
constexpr int32_t kIoError = -4;

uint16_t Rd16(const uint8_t* d, size_t o) {
    return static_cast<uint16_t>(d[o] | (d[o + 1] << 8));
}

uint32_t Rd32(const uint8_t* d, size_t o) {
    return static_cast<uint32_t>(d[o]) | (static_cast<uint32_t>(d[o + 1]) << 8) |
           (static_cast<uint32_t>(d[o + 2]) << 16) | (static_cast<uint32_t>(d[o + 3]) << 24);
}

}  // namespace

struct ZipArchive {
    const uint8_t* base = nullptr;
    size_t size = 0;
    std::unordered_map<std::string, ZipEntry> entries;
};

int32_t OpenArchiveFromMemory(void* address, size_t length, const char* /*debug_name*/,
                             ZipArchiveHandle* handle) {
    *handle = nullptr;
    if (address == nullptr || length < 22) return kInvalidOffset;

    const uint8_t* d = static_cast<const uint8_t*>(address);

    // EOCD nằm ở cuối file, có thể có comment tối đa 64KB phía sau.
    size_t eocd = length;
    const size_t scan_from = length > 65557 ? length - 65557 : 0;
    for (size_t i = length - 22 + 1; i-- > scan_from;) {
        if (Rd32(d, i) == kEocdSignature) {
            eocd = i;
            break;
        }
    }
    if (eocd == length) return kInvalidOffset;

    const uint16_t entry_count = Rd16(d, eocd + 10);
    const uint32_t cd_size = Rd32(d, eocd + 12);
    const uint32_t cd_offset = Rd32(d, eocd + 16);
    if (static_cast<size_t>(cd_offset) + cd_size > length) return kInvalidOffset;

    auto* archive = new ZipArchive();
    archive->base = d;
    archive->size = length;

    size_t p = cd_offset;
    for (uint16_t i = 0; i < entry_count && p + 46 <= length; ++i) {
        if (Rd32(d, p) != kCdSignature) break;
        ZipEntry e;
        e.method = Rd16(d, p + 10);
        e.crc32 = Rd32(d, p + 16);
        e.compressed_length = Rd32(d, p + 20);
        e.uncompressed_length = Rd32(d, p + 24);
        const uint16_t name_len = Rd16(d, p + 28);
        const uint16_t extra_len = Rd16(d, p + 30);
        const uint16_t comment_len = Rd16(d, p + 32);
        e.offset = Rd32(d, p + 42);
        if (p + 46u + name_len > length) break;
        std::string name(reinterpret_cast<const char*>(d) + p + 46, name_len);
        archive->entries.emplace(std::move(name), e);
        p += 46u + name_len + extra_len + comment_len;
    }

    if (archive->entries.empty()) {
        delete archive;
        return kInvalidOffset;
    }

    *handle = archive;
    return kOk;
}

int32_t FindEntry(ZipArchiveHandle handle, const ZipString& entry_name, ZipEntry* data) {
    if (handle == nullptr || entry_name.name == nullptr || data == nullptr) return kIoError;
    auto it = handle->entries.find(entry_name.name);
    if (it == handle->entries.end()) return kEntryNotFound;
    *data = it->second;
    return kOk;
}

int32_t ExtractToMemory(ZipArchiveHandle handle, ZipEntry* entry, uint8_t* begin, size_t size) {
    if (handle == nullptr || entry == nullptr || begin == nullptr) return kIoError;
    if (size < entry->uncompressed_length) return kIoError;

    const uint8_t* d = handle->base;
    const size_t n = handle->size;
    if (entry->offset + 30u > n || Rd32(d, entry->offset) != kLocalSignature) {
        return kInvalidOffset;
    }
    // Tên và extra field của local header có thể khác central directory.
    const size_t data_at = entry->offset + 30u + Rd16(d, entry->offset + 26) +
                           Rd16(d, entry->offset + 28);
    if (data_at + entry->compressed_length > n) return kInvalidOffset;

    if (entry->method == 0) {
        memcpy(begin, d + data_at, entry->uncompressed_length);
        return kOk;
    }

    z_stream s = {};
    if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return kInflateError;
    s.next_in = const_cast<Bytef*>(d + data_at);
    s.avail_in = static_cast<uInt>(entry->compressed_length);
    s.next_out = begin;
    s.avail_out = static_cast<uInt>(entry->uncompressed_length);
    const int rc = inflate(&s, Z_FINISH);
    inflateEnd(&s);
    return rc == Z_STREAM_END ? kOk : kInflateError;
}

void CloseArchive(ZipArchiveHandle handle) { delete handle; }

const char* ErrorCodeString(int32_t error_code) {
    switch (error_code) {
        case kOk: return "Success";
        case kInvalidOffset: return "Invalid offset or malformed archive";
        case kEntryNotFound: return "Entry not found";
        case kInflateError: return "Inflate error";
        case kIoError: return "I/O error";
        default: return "Unknown error";
    }
}
