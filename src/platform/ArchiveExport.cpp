#include "kudroid/APKExtractor.h"
#include <zlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
int failure(char** error, const std::string& message) {
    auto* text = static_cast<char*>(std::malloc(message.size() + 1));
    if (text) std::memcpy(text, message.c_str(), message.size() + 1);
    if (error) *error = text; else std::free(text);
    return -1;
}
void put(std::ostream& out, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) out.put(static_cast<char>(value >> (8 * i)));
}
struct Entry {
    fs::path path;
    std::string name, link;
    uint64_t size = 0, offset = 0;
    uint32_t crc = 0, mode = 0;
    bool directory = false, symlink = false;
};
constexpr uint64_t limit32 = 0xffffffffULL;
void archive(const fs::path& root, const fs::path& output) {
    const fs::path source = fs::canonical(root);
    const fs::path destination = fs::weakly_canonical(output);
    const fs::path relative = destination.lexically_relative(source);
    if (!relative.empty() && *relative.begin() != "..") {
        throw std::runtime_error("Archive output must be outside its source directory");
    }
    std::vector<Entry> entries;
    for (const auto& item : fs::recursive_directory_iterator(source)) {
        Entry e;
        e.path = item.path();
        e.name = e.path.lexically_relative(source).generic_string();
        struct stat st{};
        if (::lstat(e.path.c_str(), &st) != 0) throw std::runtime_error("Cannot read file metadata");
        e.mode = static_cast<uint32_t>(st.st_mode);
        e.directory = S_ISDIR(st.st_mode);
        e.symlink = S_ISLNK(st.st_mode);
        if (e.directory) e.name += '/';
        else if (e.symlink) {
            e.link = fs::read_symlink(e.path).generic_string();
            e.size = e.link.size();
        } else if (S_ISREG(st.st_mode)) e.size = fs::file_size(e.path);
        else throw std::runtime_error("Unsupported archive file type");
        if (e.name.empty() || e.name.size() > 65535) throw std::runtime_error("Invalid archive path");
        entries.push_back(std::move(e));
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot create IPA output");
    out.exceptions(std::ios::badbit | std::ios::failbit);
    std::array<char, 65536> buffer;
    bool zip64 = false;
    for (auto& e : entries) {
        e.offset = static_cast<uint64_t>(out.tellp());
        const bool big = e.size >= limit32;
        zip64 |= big || e.offset >= limit32;
        put(out, 0x04034b50, 4); put(out, big ? 45 : 20, 2);
        put(out, 0x0808, 2); put(out, 0, 2); put(out, 0, 2); put(out, 0x21, 2);
        put(out, 0, 4); put(out, big ? limit32 : 0, 4); put(out, big ? limit32 : 0, 4);
        put(out, e.name.size(), 2); put(out, big ? 20 : 0, 2);
        out.write(e.name.data(), e.name.size());
        if (big) { put(out, 1, 2); put(out, 16, 2); put(out, e.size, 8); put(out, e.size, 8); }
        uLong crc = crc32(0, Z_NULL, 0);
        if (e.symlink) {
            out.write(e.link.data(), e.link.size());
            crc = crc32(crc, reinterpret_cast<const Bytef*>(e.link.data()), e.link.size());
        } else if (!e.directory) {
            std::ifstream in(e.path, std::ios::binary);
            if (!in) throw std::runtime_error("Cannot read archive input: " + e.name);
            uint64_t remaining = e.size;
            while (remaining) {
                const auto count = static_cast<std::streamsize>(std::min<uint64_t>(remaining, buffer.size()));
                in.read(buffer.data(), count);
                if (in.gcount() != count) throw std::runtime_error("Archive input changed: " + e.name);
                out.write(buffer.data(), count);
                crc = crc32(crc, reinterpret_cast<const Bytef*>(buffer.data()), static_cast<uInt>(count));
                remaining -= count;
            }
        }
        e.crc = static_cast<uint32_t>(crc);
        put(out, 0x08074b50, 4); put(out, e.crc, 4);
        put(out, e.size, big ? 8 : 4); put(out, e.size, big ? 8 : 4);
    }
    const uint64_t centralOffset = static_cast<uint64_t>(out.tellp());
    for (const auto& e : entries) {
        const bool big = e.size >= limit32, far = e.offset >= limit32;
        const unsigned extra = (big ? 16 : 0) + (far ? 8 : 0);
        put(out, 0x02014b50, 4); put(out, (3 << 8) | 45, 2); put(out, (big || far) ? 45 : 20, 2);
        put(out, 0x0808, 2); put(out, 0, 2); put(out, 0, 2); put(out, 0x21, 2);
        put(out, e.crc, 4); put(out, big ? limit32 : e.size, 4); put(out, big ? limit32 : e.size, 4);
        put(out, e.name.size(), 2); put(out, extra ? extra + 4 : 0, 2);
        put(out, 0, 2); put(out, 0, 2); put(out, 0, 2);
        put(out, (e.mode << 16) | (e.directory ? 0x10 : 0), 4);
        put(out, far ? limit32 : e.offset, 4);
        out.write(e.name.data(), e.name.size());
        if (extra) {
            put(out, 1, 2); put(out, extra, 2);
            if (big) { put(out, e.size, 8); put(out, e.size, 8); }
            if (far) put(out, e.offset, 8);
        }
    }
    const uint64_t centralSize = static_cast<uint64_t>(out.tellp()) - centralOffset;
    zip64 |= entries.size() >= 65535 || centralOffset >= limit32 || centralSize >= limit32;
    if (zip64) {
        const uint64_t position = static_cast<uint64_t>(out.tellp());
        put(out, 0x06064b50, 4); put(out, 44, 8); put(out, (3 << 8) | 45, 2); put(out, 45, 2);
        put(out, 0, 4); put(out, 0, 4); put(out, entries.size(), 8); put(out, entries.size(), 8);
        put(out, centralSize, 8); put(out, centralOffset, 8);
        put(out, 0x07064b50, 4); put(out, 0, 4); put(out, position, 8); put(out, 1, 4);
    }
    put(out, 0x06054b50, 4); put(out, 0, 2); put(out, 0, 2);
    put(out, std::min<uint64_t>(entries.size(), 65535), 2);
    put(out, std::min<uint64_t>(entries.size(), 65535), 2);
    put(out, std::min(centralSize, limit32), 4); put(out, std::min(centralOffset, limit32), 4);
    put(out, 0, 2);
    out.close();
}
}

// Error text, when present, is malloc-owned.
extern "C" int kudroid_export_extract(const char* apk, const char* target, char** error) {
    if (error) *error = nullptr;
    try {
        if (!apk || !target || !*apk || !*target) return failure(error, "Missing export path");
        const bool ok = kudroid::APKExtractor::is_bundle_container(apk)
            ? kudroid::APKExtractor::extract_bundle(apk, target,
                (fs::path(target) / "SeedSdcard/Android/obb").string())
            : kudroid::APKExtractor::extract_apk(apk, target);
        if (!ok) return failure(error, kudroid::APKExtractor::lastError());
        return 0;
    } catch (const std::exception& e) { return failure(error, e.what()); }
}
extern "C" int kudroid_preview_apk(const char* apk, const char* target, const char* locale, char** error) {
    if (error) *error = nullptr;
    try {
        if (!apk || !target || !*apk || !*target) return failure(error, "Missing preview path");
        fs::create_directories(target);
        kudroid::PackagePreview preview;
        if (!kudroid::APKExtractor::preview_package(apk, target, locale ? locale : "", &preview))
            return failure(error, kudroid::APKExtractor::lastError());
        if (preview.label.size() > 4096) return failure(error, "Application label is too long");
        std::ofstream identity(fs::path(target) / "package.txt", std::ios::binary);
        identity << preview.packageName;
        if (!identity) return failure(error, "Could not save package identity");
        std::ofstream label(fs::path(target) / "label.txt", std::ios::binary);
        label << preview.label;
        if (!label) return failure(error, "Could not save application label");
        if (!preview.icon.empty()) {
            std::ofstream icon(fs::path(target) / "app_icon.png", std::ios::binary);
            icon.write(reinterpret_cast<const char*>(preview.icon.data()), preview.icon.size());
            if (!icon) return failure(error, "Could not save application icon");
        }
        return 0;
    } catch (const std::exception& e) { return failure(error, e.what()); }
}

extern "C" int kudroid_export_archive(const char* directory, const char* output, char** error) {
    if (error) *error = nullptr;
    if (!directory || !output || !*directory || !*output) return failure(error, "Missing archive path");
    std::string temporary;
    try {
        std::string pattern = std::string(output) + ".XXXXXX";
        std::vector<char> path(pattern.begin(), pattern.end());
        path.push_back('\0');
        const int fd = ::mkstemp(path.data());
        if (fd < 0) return failure(error, "Cannot create temporary IPA");
        ::close(fd);
        temporary = path.data();
        archive(directory, temporary);
        fs::rename(temporary, output);
        return 0;
    } catch (const std::exception& e) {
        if (!temporary.empty()) { std::error_code ignored; fs::remove(temporary, ignored); }
        return failure(error, e.what());
    }
}
