#include "kudroid/ResourceCompat.h"
#include "kudroid/ResourceTable.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <zlib.h>

namespace kudroid {
namespace {

constexpr uint64_t kMaxScanBytes = 64ull * 1024ull * 1024ull;
constexpr size_t kReadChunkBytes = 16u * 1024u;
constexpr uint32_t kZipEocdSignature = 0x06054b50;
constexpr uint32_t kZipCentralSignature = 0x02014b50;
constexpr uint32_t kZipLocalSignature = 0x04034b50;

struct ScanResult {
    std::string path;
    bool ok = false;
    std::string error;
    ResourceTableAnalysis analysis;
};

bool IsApkPath(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension == ".apk";
}

bool ReadRawTable(const std::string& path, std::vector<uint8_t>* bytes, std::string* error) {
    std::error_code ec;
    const uint64_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        *error = "cannot stat input: " + ec.message();
        return false;
    }
    if (size == 0) {
        *error = "input file is empty";
        return false;
    }
    if (size > kMaxScanBytes) {
        *error = "input exceeds the 64 MiB scan limit";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        *error = "cannot open input for reading";
        return false;
    }
    bytes->resize(static_cast<size_t>(size));
    input.read(reinterpret_cast<char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    if (!input || static_cast<uint64_t>(input.gcount()) != size) {
        bytes->clear();
        *error = "input ended before its declared file size";
        return false;
    }
    return true;
}

uint16_t ReadU16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t ReadU32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

bool ReadAt(std::ifstream* input, uint64_t file_size, uint64_t offset,
            void* output, size_t length) {
    if (offset > file_size || length > file_size - offset ||
        offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) return false;
    input->clear();
    input->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!*input) return false;
    input->read(static_cast<char*>(output), static_cast<std::streamsize>(length));
    return *input && static_cast<size_t>(input->gcount()) == length;
}

bool ReadApkTable(const std::string& path, std::vector<uint8_t>* bytes, std::string* error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        *error = "cannot open APK for reading";
        return false;
    }
    const std::streamoff end_pos = input.tellg();
    if (end_pos < 22) {
        *error = "APK is too short to contain a ZIP end record";
        return false;
    }
    const uint64_t file_size = static_cast<uint64_t>(end_pos);
    const size_t tail_size = static_cast<size_t>(std::min<uint64_t>(file_size, 22u + 65535u));
    std::vector<uint8_t> tail(tail_size);
    if (!ReadAt(&input, file_size, file_size - tail_size, tail.data(), tail.size())) {
        *error = "cannot read APK ZIP end record";
        return false;
    }

    bool found_eocd = false;
    uint64_t eocd_offset = 0;
    uint32_t central_size = 0;
    uint32_t central_offset = 0;
    uint16_t entry_count = 0;
    for (size_t cursor = tail.size() - 22 + 1; cursor-- > 0;) {
        const uint8_t* record = tail.data() + cursor;
        if (ReadU32(record) != kZipEocdSignature) continue;
        const uint16_t comment_size = ReadU16(record + 20);
        if (cursor + 22u + comment_size > tail.size()) continue;
        if (ReadU16(record + 4) != 0 || ReadU16(record + 6) != 0 ||
            ReadU16(record + 8) != ReadU16(record + 10)) continue;
        central_size = ReadU32(record + 12);
        central_offset = ReadU32(record + 16);
        entry_count = ReadU16(record + 10);
        eocd_offset = file_size - tail_size + cursor;
        found_eocd = true;
        break;
    }
    if (!found_eocd) {
        *error = "APK has no valid single-disk ZIP end record";
        return false;
    }
    if (central_size == 0xffffffffu || central_offset == 0xffffffffu ||
        central_size > kMaxScanBytes || central_offset > eocd_offset ||
        central_size > eocd_offset - central_offset ||
        static_cast<uint64_t>(central_offset) + central_size > file_size) {
        *error = "APK ZIP central directory is invalid or exceeds the scan limit";
        return false;
    }

    bool found_resource = false;
    uint16_t compression_method = 0;
    uint16_t general_flags = 0;
    uint32_t expected_crc = 0;
    uint32_t compressed_size = 0;
    uint32_t uncompressed_size = 0;
    uint32_t local_header_offset = 0;
    uint64_t central_cursor = central_offset;
    const uint64_t central_end = static_cast<uint64_t>(central_offset) + central_size;
    std::array<uint8_t, 46> central_record{};
    for (uint16_t entry = 0; entry < entry_count; ++entry) {
        if (central_cursor > central_end || central_end - central_cursor < central_record.size() ||
            !ReadAt(&input, file_size, central_cursor, central_record.data(), central_record.size()) ||
            ReadU32(central_record.data()) != kZipCentralSignature) {
            *error = "APK ZIP central directory entry is malformed";
            return false;
        }
        const uint16_t name_size = ReadU16(central_record.data() + 28);
        const uint16_t extra_size = ReadU16(central_record.data() + 30);
        const uint16_t comment_size = ReadU16(central_record.data() + 32);
        const uint64_t full_size = central_record.size() + name_size + extra_size + comment_size;
        if (full_size > central_end - central_cursor) {
            *error = "APK ZIP central directory entry extends past its bounds";
            return false;
        }
        if (ReadU16(central_record.data() + 34) != 0) {
            *error = "multi-disk APK ZIP entries are unsupported";
            return false;
        }
        std::string name(name_size, '\0');
        if (name_size != 0 && !ReadAt(&input, file_size, central_cursor + central_record.size(),
                                      name.data(), name.size())) {
            *error = "cannot read APK ZIP entry name";
            return false;
        }
        if (name == "resources.arsc") {
            if (found_resource) {
                *error = "APK has duplicate root resources.arsc entries";
                return false;
            }
            found_resource = true;
            general_flags = ReadU16(central_record.data() + 8);
            compression_method = ReadU16(central_record.data() + 10);
            expected_crc = ReadU32(central_record.data() + 16);
            compressed_size = ReadU32(central_record.data() + 20);
            uncompressed_size = ReadU32(central_record.data() + 24);
            local_header_offset = ReadU32(central_record.data() + 42);
        }
        central_cursor += full_size;
    }
    if (!found_resource) {
        *error = "APK has no exact root resources.arsc entry";
        return false;
    }
    if ((general_flags & 0x0001) != 0) {
        *error = "APK resources.arsc entry is encrypted";
        return false;
    }
    if (uncompressed_size == 0 || uncompressed_size > kMaxScanBytes ||
        compressed_size > kMaxScanBytes) {
        *error = uncompressed_size == 0
                ? "APK resources.arsc entry is empty"
                : "APK resources.arsc exceeds the 64 MiB scan limit";
        return false;
    }

    std::array<uint8_t, 30> local_record{};
    if (!ReadAt(&input, file_size, local_header_offset, local_record.data(), local_record.size()) ||
        ReadU32(local_record.data()) != kZipLocalSignature) {
        *error = "APK resources.arsc local header is invalid";
        return false;
    }
    const uint16_t local_name_size = ReadU16(local_record.data() + 26);
    const uint16_t local_extra_size = ReadU16(local_record.data() + 28);
    if (ReadU16(local_record.data() + 6) != general_flags ||
        ReadU16(local_record.data() + 8) != compression_method) {
        *error = "APK resources.arsc local header disagrees with its ZIP directory entry";
        return false;
    }
    std::string local_name(local_name_size, '\0');
    if ((local_name_size != 0 && !ReadAt(&input, file_size,
             static_cast<uint64_t>(local_header_offset) + local_record.size(),
             local_name.data(), local_name.size())) || local_name != "resources.arsc") {
        *error = "APK resources.arsc local name does not match the root entry";
        return false;
    }
    const uint64_t data_offset = static_cast<uint64_t>(local_header_offset) +
            local_record.size() + local_name_size + local_extra_size;
    if (data_offset > central_offset || compressed_size > central_offset - data_offset) {
        *error = "APK resources.arsc bytes overlap the ZIP central directory";
        return false;
    }
    std::vector<uint8_t> compressed(compressed_size);
    if (!ReadAt(&input, file_size, data_offset, compressed.data(), compressed.size())) {
        *error = "APK resources.arsc compressed bytes are out of bounds";
        return false;
    }
    bytes->assign(uncompressed_size, 0);
    if (compression_method == 0) {
        if (compressed_size != uncompressed_size) {
            *error = "stored APK resources.arsc has inconsistent sizes";
            bytes->clear();
            return false;
        }
        *bytes = std::move(compressed);
    } else if (compression_method == 8) {
        z_stream stream{};
        stream.next_in = compressed.data();
        stream.avail_in = static_cast<uInt>(compressed.size());
        stream.next_out = bytes->data();
        stream.avail_out = static_cast<uInt>(bytes->size());
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            *error = "cannot initialize ZIP deflate reader";
            bytes->clear();
            return false;
        }
        const int inflate_status = inflate(&stream, Z_FINISH);
        const uLong total_out = stream.total_out;
        const uLong total_in = stream.total_in;
        inflateEnd(&stream);
        if (inflate_status != Z_STREAM_END || total_out != uncompressed_size ||
            total_in != compressed_size) {
            *error = "APK resources.arsc deflate stream is malformed";
            bytes->clear();
            return false;
        }
    } else {
        *error = "APK resources.arsc uses an unsupported ZIP compression method";
        bytes->clear();
        return false;
    }
    const uint32_t actual_crc = static_cast<uint32_t>(crc32(0L, bytes->data(),
                                                           static_cast<uInt>(bytes->size())));
    if (actual_crc != expected_crc) {
        *error = "APK resources.arsc failed its ZIP CRC check";
        bytes->clear();
        return false;
    }
    return true;
}

ScanResult ScanPath(const std::string& path) {
    ScanResult result;
    result.path = path;
    std::vector<uint8_t> bytes;
    bool read = false;
    if (IsApkPath(path)) {
        read = ReadApkTable(path, &bytes, &result.error);
    } else {
        read = ReadRawTable(path, &bytes, &result.error);
    }
    if (!read) return result;
    result.ok = resource_analyze_bytes(bytes.data(), bytes.size(), &result.analysis, &result.error);
    return result;
}

std::string JsonString(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                        << static_cast<unsigned>(c) << std::dec << std::setfill(' ');
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    out << '"';
    return out.str();
}

std::string CodeHex(uint8_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(value);
    return out.str();
}

void WriteJsonReport(const std::vector<ScanResult>& results, std::string* output) {
    std::ostringstream out;
    out << "{\"schemaVersion\":1,\"inputs\":[";
    for (size_t i = 0; i < results.size(); ++i) {
        if (i != 0) out << ',';
        const ScanResult& result = results[i];
        const ResourceTableAnalysis& analysis = result.analysis;
        out << "{\"path\":" << JsonString(result.path)
            << ",\"status\":" << JsonString(result.ok ? "ok" : "error")
            << ",\"packages\":[";
        for (size_t j = 0; j < analysis.packages.size(); ++j) {
            if (j != 0) out << ',';
            out << JsonString(analysis.packages[j]);
        }
        out << "],\"resourceIds\":" << analysis.resource_ids << ",\"resourceTypes\":[";
        size_t index = 0;
        for (const auto& [name, count] : analysis.resource_type_counts) {
            if (index++ != 0) out << ',';
            out << "{\"name\":" << JsonString(name) << ",\"count\":" << count << '}';
        }
        out << "],\"valueTypes\":[";
        for (size_t j = 0; j < analysis.value_types.size(); ++j) {
            if (j != 0) out << ',';
            const auto& type = analysis.value_types[j];
            out << "{\"code\":" << JsonString(CodeHex(type.data_type))
                << ",\"name\":" << JsonString(type.name)
                << ",\"count\":" << type.count
                << ",\"supported\":" << (type.supported ? "true" : "false") << '}';
        }
        out << "],\"qualifiers\":[";
        for (size_t j = 0; j < analysis.qualifiers.size(); ++j) {
            if (j != 0) out << ',';
            const auto& qualifier = analysis.qualifiers[j];
            out << "{\"name\":" << JsonString(qualifier.name)
                << ",\"value\":" << JsonString(qualifier.value)
                << ",\"count\":" << qualifier.count
                << ",\"supported\":" << (qualifier.supported ? "true" : "false") << '}';
        }
        out << "],\"simpleEntries\":" << analysis.simple_entries
            << ",\"complexEntries\":" << analysis.complex_entries
            << ",\"complexMapValues\":" << analysis.complex_map_values
            << ",\"malformedEntries\":" << analysis.malformed_entries
            << ",\"skippedEntries\":" << analysis.skipped_entries
            << ",\"error\":" << (result.error.empty() ? "null" : JsonString(result.error)) << '}';
    }
    out << "]}\n";
    *output = out.str();
}

void WriteTextReport(const std::vector<ScanResult>& results, std::string* output) {
    std::ostringstream out;
    for (size_t i = 0; i < results.size(); ++i) {
        if (i != 0) out << '\n';
        const ScanResult& result = results[i];
        const ResourceTableAnalysis& analysis = result.analysis;
        out << "Input: " << result.path << '\n'
            << "Status: " << (result.ok ? "ok" : "error") << '\n'
            << "Packages: ";
        if (analysis.packages.empty()) out << "(none)";
        for (size_t j = 0; j < analysis.packages.size(); ++j) {
            if (j != 0) out << ", ";
            out << analysis.packages[j];
        }
        out << '\n' << "Resource IDs: " << analysis.resource_ids << '\n'
            << "Resource types:\n";
        for (const auto& [name, count] : analysis.resource_type_counts)
            out << "  " << name << ": " << count << '\n';
        out << "Value types:\n";
        for (const auto& type : analysis.value_types)
            out << "  " << CodeHex(type.data_type) << ' ' << type.name << ": " << type.count
                << " (" << (type.supported ? "supported" : "unsupported") << ")\n";
        out << "Qualifiers:\n";
        for (const auto& qualifier : analysis.qualifiers)
            out << "  " << qualifier.name << '=' << qualifier.value << ": " << qualifier.count
                << " (" << (qualifier.supported ? "supported" : "unsupported") << ")\n";
        out << "Entries: simple=" << analysis.simple_entries
            << " complex=" << analysis.complex_entries
            << " complexMapValues=" << analysis.complex_map_values
            << " malformed=" << analysis.malformed_entries
            << " skipped=" << analysis.skipped_entries << '\n';
        if (!result.error.empty()) out << "Error: " << result.error << '\n';
    }
    *output = out.str();
}

const char* Usage() {
    return "Usage: kuart_resource_compat [--json] <file.apk|resources.arsc> [more inputs...]\n"
           "       kuart_resource_compat --help\n"
           "Scans local resource tables without modifying input files.\n";
}

}  // namespace

int resource_compat_cli(const std::vector<std::string>& args,
                        std::string* output, std::string* error) {
    if (output == nullptr || error == nullptr) return 2;
    output->clear();
    error->clear();

    if (args.size() == 1 && args[0] == "--help") {
        *output = Usage();
        return 0;
    }
    bool json = false;
    std::vector<std::string> paths;
    for (const std::string& arg : args) {
        if (arg == "--json") {
            json = true;
        } else if (!arg.empty() && arg[0] == '-') {
            *error = std::string("unknown option: ") + arg + "\n" + Usage();
            return 2;
        } else {
            paths.push_back(arg);
        }
    }
    if (paths.empty()) {
        *error = std::string("no input paths supplied\n") + Usage();
        return 2;
    }

    std::vector<ScanResult> results;
    results.reserve(paths.size());
    bool all_ok = true;
    for (const std::string& path : paths) {
        results.push_back(ScanPath(path));
        all_ok = all_ok && results.back().ok;
    }
    if (json) WriteJsonReport(results, output);
    else WriteTextReport(results, output);
    if (!all_ok) {
        *error = "one or more resource inputs could not be scanned";
        return 1;
    }
    return 0;
}

}  // namespace kudroid
