#include "kudroid/APKExtractor.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <vector>
#include <zlib.h>

namespace kudroid {
namespace {
std::string gLastError;
void apkLog(const std::string& message) { std::fprintf(stderr, "[kudroid_apk] %s\n", message.c_str()); }
std::uint16_t read16(const std::vector<std::uint8_t>& d, std::size_t o) { return d[o] | (d[o + 1] << 8); }
std::uint32_t read32(const std::vector<std::uint8_t>& d, std::size_t o) {
    return d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24);
}
bool hasBytes(const std::vector<std::uint8_t>& d, std::size_t o, std::size_t n) { return o <= d.size() && n <= d.size() - o; }
bool inflateRaw(const std::uint8_t* input, std::size_t inputSize, std::vector<std::uint8_t>& output) {
    z_stream stream = {};
    stream.next_in = const_cast<Bytef*>(input);
    stream.avail_in = static_cast<uInt>(inputSize);
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return false;
    const int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    return result == Z_STREAM_END && stream.total_out == output.size();
}
} // namespace

const std::string& APKExtractor::lastError() { return gLastError; }

bool APKExtractor::extract_apk(const std::string& apkPath, const std::string& targetDirectory) {
    gLastError.clear();
    std::ifstream apk(apkPath, std::ios::binary);
    if (!apk) { gLastError = "Cannot open APK: " + apkPath; apkLog(gLastError); return false; }
    const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(apk)), {});
    if (data.size() < 22) { gLastError = "APK is too small to be a ZIP archive"; apkLog(gLastError); return false; }

    const std::size_t searchStart = data.size() > 65557 ? data.size() - 65557 : 0;
    std::size_t endRecord = data.size();
    for (std::size_t offset = data.size() - 22;; --offset) {
        if (read32(data, offset) == 0x06054b50) { endRecord = offset; break; }
        if (offset == searchStart) break;
    }
    if (endRecord == data.size()) { gLastError = "ZIP end record not found"; apkLog(gLastError); return false; }

    const std::uint16_t entryCount = read16(data, endRecord + 10);
    std::size_t centralOffset = read32(data, endRecord + 16);
    std::error_code error;
    std::filesystem::create_directories(targetDirectory, error);
    if (error) { gLastError = "Cannot create target directory: " + error.message(); apkLog(gLastError); return false; }

    bool found = false;
    for (std::uint16_t index = 0; index < entryCount; ++index) {
        if (!hasBytes(data, centralOffset, 46) || read32(data, centralOffset) != 0x02014b50) {
            gLastError = "Invalid ZIP central-directory entry"; apkLog(gLastError); return false;
        }
        const std::uint16_t compression = read16(data, centralOffset + 10);
        const std::uint32_t compressedSize = read32(data, centralOffset + 20);
        const std::uint32_t uncompressedSize = read32(data, centralOffset + 24);
        const std::uint16_t nameLength = read16(data, centralOffset + 28);
        const std::uint16_t extraLength = read16(data, centralOffset + 30);
        const std::uint16_t commentLength = read16(data, centralOffset + 32);
        const std::uint32_t localOffset = read32(data, centralOffset + 42);
        if (!hasBytes(data, centralOffset + 46, nameLength)) { gLastError = "Invalid ZIP entry name"; return false; }
        const std::string entry(reinterpret_cast<const char*>(data.data() + centralOffset + 46), nameLength);
        centralOffset += 46 + nameLength + extraLength + commentLength;
        
        bool shouldExtract = false;
        if (entry.rfind("lib/arm64-v8a/", 0) == 0 && entry.size() >= 3 && entry.compare(entry.size() - 3, 3, ".so") == 0) shouldExtract = true;
        else if (entry.size() >= 4 && entry.compare(entry.size() - 4, 4, ".dex") == 0) shouldExtract = true;
        else if (entry.rfind("assets/", 0) == 0) shouldExtract = true;
        else if (entry == "AndroidManifest.xml") shouldExtract = true;
        
        if (!shouldExtract) continue;
        if (entry.empty() || entry.back() == '/') continue; // bỏ qua thư mục
        
        found = true;
        if (!hasBytes(data, localOffset, 30) || read32(data, localOffset) != 0x04034b50) { gLastError = "Invalid local header: " + entry; return false; }
        const std::size_t contentOffset = localOffset + 30 + read16(data, localOffset + 26) + read16(data, localOffset + 28);
        if (!hasBytes(data, contentOffset, compressedSize)) { gLastError = "Truncated entry: " + entry; return false; }
        std::vector<std::uint8_t> output(uncompressedSize);
        if (compression == 0 && compressedSize == uncompressedSize) {
            std::memcpy(output.data(), data.data() + contentOffset, output.size());
        } else if (compression == 8) {
            if (!inflateRaw(data.data() + contentOffset, compressedSize, output)) { gLastError = "Deflate failed: " + entry; return false; }
        } else { gLastError = "Unsupported compression for: " + entry; return false; }
        
        const auto destination = std::filesystem::path(targetDirectory) / entry;
        std::filesystem::create_directories(destination.parent_path(), error);
        
        std::ofstream extracted(destination, std::ios::binary);
        extracted.write(reinterpret_cast<const char*>(output.data()), output.size());
        extracted.close();
        if (!extracted || ::chmod(destination.c_str(), 0755) != 0) { gLastError = "Cannot write/chmod: " + destination.string(); return false; }
        apkLog("Extracting: " + entry + " -> " + destination.string() + " (OK)");
    }
    if (!found) { gLastError = "No expected entries found in APK"; apkLog(gLastError); return false; }
    return true;
}
} // namespace kudroid
