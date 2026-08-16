#include "kudroid/APKExtractor.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <sys/stat.h>
#include <vector>
#include <zlib.h>

namespace kudroid {
namespace {
std::string gLastError;
void apkLog(const std::string& message) { std::fprintf(stderr, "[kudroid_apk] %s\n", message.c_str()); }

// Helpers over an in-memory buffer (extract_apk loads the whole file).
std::uint16_t read16(const std::vector<std::uint8_t>& d, std::size_t o) { return d[o] | (d[o + 1] << 8); }
std::uint32_t read32(const std::vector<std::uint8_t>& d, std::size_t o) {
    return d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24);
}
bool hasBytes(const std::vector<std::uint8_t>& d, std::size_t o, std::size_t n) { return o <= d.size() && n <= d.size() - o; }
bool inflateRaw(const std::uint8_t* input, std::size_t inputSize, std::vector<std::uint8_t>& output) {
    if (output.empty()) return true;

    // First try raw deflate (-MAX_WBITS = -15, standard for ZIP)
    z_stream stream = {};
    stream.next_in = const_cast<Bytef*>(input);
    stream.avail_in = static_cast<uInt>(inputSize);
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit2(&stream, -MAX_WBITS) == Z_OK) {
        const int result = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        if (result == Z_STREAM_END || stream.total_out == output.size()) return true;
    }

    // Fallback 1: Try auto-detect (32 + MAX_WBITS) for gzip/zlib header
    std::memset(&stream, 0, sizeof(stream));
    stream.next_in = const_cast<Bytef*>(input);
    stream.avail_in = static_cast<uInt>(inputSize);
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit2(&stream, 32 + MAX_WBITS) == Z_OK) {
        const int result = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        if (result == Z_STREAM_END || stream.total_out == output.size()) return true;
    }

    // Fallback 2: Try standard zlib (MAX_WBITS = 15)
    std::memset(&stream, 0, sizeof(stream));
    stream.next_in = const_cast<Bytef*>(input);
    stream.avail_in = static_cast<uInt>(inputSize);
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit2(&stream, MAX_WBITS) == Z_OK) {
        const int result = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        if (result == Z_STREAM_END || stream.total_out == output.size()) return true;
    }

    return false;
}

// Streaming helpers (is_bundle_container / extract_bundle read big containers
// entry-by-entry instead of loading the whole file into RAM — .xapk/.apks can
// contain multi-GB OBB files).
std::uint16_t read16b(const char* d, std::size_t o) {
    const auto* u = reinterpret_cast<const unsigned char*>(d);
    return static_cast<std::uint16_t>(u[o] | (u[o + 1] << 8));
}
std::uint32_t read32b(const char* d, std::size_t o) {
    const auto* u = reinterpret_cast<const unsigned char*>(d);
    return static_cast<std::uint32_t>(u[o] | (u[o + 1] << 8) | (u[o + 2] << 16) | (u[o + 3] << 24));
}
bool readExact(std::ifstream& f, void* buf, std::size_t n) {
    f.read(static_cast<char*>(buf), static_cast<std::streamsize>(n));
    return f.gcount() == static_cast<std::streamsize>(n);
}

struct ZipEntryInfo {
    std::string name;
    std::uint16_t compression;
    std::uint32_t compressedSize;
    std::uint32_t localOffset;
};

// Đọc end-of-central-directory + toàn bộ central directory (chỉ metadata).
bool readZipEntries(std::ifstream& f, std::streamsize fileSize, std::vector<ZipEntryInfo>& entries) {
    entries.clear();
    const std::size_t tailLen = fileSize < 65557 ? static_cast<std::size_t>(fileSize) : 65557;
    std::vector<char> tail(tailLen);
    f.seekg(fileSize - static_cast<std::streamsize>(tailLen));
    if (!readExact(f, tail.data(), tailLen)) return false;

    std::size_t eocd = tailLen;
    for (std::size_t i = tailLen; i >= 22; --i) {
        if (read32b(tail.data(), i - 22) == 0x06054b50) { eocd = i - 22; break; }
    }
    if (eocd == tailLen) { gLastError = "ZIP end record not found"; return false; }
    const std::uint16_t entryCount = read16b(tail.data(), eocd + 10);
    const std::uint32_t centralOffset = read32b(tail.data(), eocd + 16);

    f.seekg(centralOffset);
    for (std::uint16_t index = 0; index < entryCount; ++index) {
        char hdr[46];
        if (!readExact(f, hdr, 46) || read32b(hdr, 0) != 0x02014b50) {
            gLastError = "Invalid ZIP central-directory entry";
            return false;
        }
        ZipEntryInfo info;
        info.compression = read16b(hdr, 10);
        info.compressedSize = read32b(hdr, 20);
        const std::uint16_t nameLen = read16b(hdr, 28);
        const std::uint16_t extraLen = read16b(hdr, 30);
        const std::uint16_t commentLen = read16b(hdr, 32);
        info.localOffset = read32b(hdr, 42);
        std::string name(nameLen, '\0');
        if (!readExact(f, &name[0], nameLen)) { gLastError = "Truncated ZIP entry name"; return false; }
        f.seekg(static_cast<std::streamoff>(extraLen) + commentLen, std::ios::cur);
        info.name = std::move(name);
        entries.push_back(std::move(info));
    }
    return true;
}

// Giải nén một entry (stored hoặc deflate) ra file — streaming, không nạp cả entry.
bool extractZipEntryToFile(std::ifstream& f, const ZipEntryInfo& e, const std::string& destPath) {
    f.seekg(e.localOffset);
    char lh[30];
    if (!readExact(f, lh, 30) || read32b(lh, 0) != 0x04034b50) return false;
    const std::uint16_t nameLen = read16b(lh, 26);
    const std::uint16_t extraLen = read16b(lh, 28);
    f.seekg(static_cast<std::streamoff>(nameLen) + extraLen, std::ios::cur);

    std::ofstream out(destPath, std::ios::binary);
    if (!out) return false;

    if (e.compression == 0) {
        std::size_t remaining = e.compressedSize;
        std::vector<char> buf(1 << 20);
        while (remaining > 0) {
            const std::size_t n = remaining < buf.size() ? remaining : buf.size();
            if (!readExact(f, buf.data(), n)) return false;
            out.write(buf.data(), static_cast<std::streamsize>(n));
            remaining -= n;
        }
        return true;
    }

    if (e.compression != 8) { gLastError = "Unsupported ZIP compression method"; return false; }

    z_stream stream = {};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return false;
    std::vector<char> in(1 << 16);
    std::vector<char> outBuf(1 << 16);
    std::size_t remaining = e.compressedSize;
    int ret = Z_OK;
    bool ok = true;
    while (ret != Z_STREAM_END) {
        if (stream.avail_in == 0) {
            if (remaining == 0) { ok = false; break; }
            const std::size_t n = remaining < in.size() ? remaining : in.size();
            if (!readExact(f, in.data(), n)) { ok = false; break; }
            remaining -= n;
            stream.next_in = reinterpret_cast<Bytef*>(in.data());
            stream.avail_in = static_cast<uInt>(n);
        }
        stream.next_out = reinterpret_cast<Bytef*>(outBuf.data());
        stream.avail_out = static_cast<uInt>(outBuf.size());
        ret = inflate(&stream, Z_NO_FLUSH);
        out.write(outBuf.data(), outBuf.size() - stream.avail_out);
        if (ret != Z_OK && ret != Z_STREAM_END) { ok = false; break; }
    }
    inflateEnd(&stream);
    return ok;
}

bool endsWithCi(const std::string& s, const char* suffix) {
    const std::size_t sl = std::strlen(suffix);
    if (s.size() < sl) return false;
    for (std::size_t i = 0; i < sl; ++i) {
        if (std::tolower(static_cast<unsigned char>(s[s.size() - sl + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i]))) return false;
    }
    return true;
}
// lowercase toàn bộ (dùng để lọc split theo tên).
std::string toLower(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

struct ManifestInfo {
    std::string packageName;
    std::string versionName;
    std::string versionCode;
    std::string appLabel;
};

static int iconPriority(const std::string& name) {
    if (!endsWithCi(name, ".png") && !endsWithCi(name, ".webp") && !endsWithCi(name, ".jpg") && !endsWithCi(name, ".jpeg")) return -1;
    if (endsWithCi(name, ".9.png")) return -1; // 9-patch border format is not an app icon

    const std::string lower = toLower(name);
    int score = 0;

    // Ưu tiên độ phân giải
    if (lower.find("xxxhdpi") != std::string::npos) score += 60;
    else if (lower.find("xxhdpi") != std::string::npos) score += 50;
    else if (lower.find("xhdpi") != std::string::npos) score += 40;
    else if (lower.find("hdpi") != std::string::npos) score += 30;
    else if (lower.find("mdpi") != std::string::npos) score += 20;
    else if (lower.find("ldpi") != std::string::npos) score += 10;
    else if (lower.find("nodpi") != std::string::npos) score += 35;
    else if (lower.find("drawable") != std::string::npos || lower.find("mipmap") != std::string::npos) score += 15;

    // Ưu tiên theo tên icon chuẩn của Android & Unity
    if (lower.find("ic_launcher_foreground") != std::string::npos) score += 85;
    else if (lower.find("ic_launcher") != std::string::npos) score += 100;
    else if (lower.find("app_icon") != std::string::npos || lower.find("appicon") != std::string::npos) score += 95;
    else if (lower.find("icon") != std::string::npos) score += 75;
    else if (lower.find("logo") != std::string::npos) score += 65;
    else if (lower.find("banner") != std::string::npos) score += 40;
    else if (lower.find("splash") != std::string::npos) score += 30;
    else if (lower.rfind("res/drawable", 0) == 0 || lower.rfind("res/mipmap", 0) == 0) score += 10;

    return score > 0 ? score : -1;
}

static std::vector<std::string> parseStringPool(const std::vector<std::uint8_t>& data, std::size_t poolOffset) {
    std::vector<std::string> stringPool;
    if (poolOffset + 28 > data.size() || read16(data, poolOffset) != 0x0001) return stringPool;

    const std::uint32_t stringCount = read32(data, poolOffset + 8);
    const std::uint32_t flags = read32(data, poolOffset + 16);
    const std::uint32_t stringsStart = poolOffset + read32(data, poolOffset + 20);
    const bool isUtf8 = (flags & (1 << 8)) != 0;

    stringPool.reserve(stringCount);
    const std::size_t offsetsBase = poolOffset + 28;
    for (std::uint32_t i = 0; i < stringCount; ++i) {
        if (offsetsBase + i * 4 + 4 > data.size()) break;
        const std::uint32_t strOffset = stringsStart + read32(data, offsetsBase + i * 4);
        if (strOffset >= data.size()) { stringPool.push_back(""); continue; }

        if (isUtf8) {
            std::size_t cur = strOffset;
            if (cur >= data.size()) { stringPool.push_back(""); continue; }
            if (data[cur] & 0x80) cur += 2; else cur += 1;
            if (cur >= data.size()) { stringPool.push_back(""); continue; }
            std::size_t byteLen = 0;
            if (data[cur] & 0x80) {
                if (cur + 1 >= data.size()) { stringPool.push_back(""); continue; }
                byteLen = ((data[cur] & 0x7F) << 8) | data[cur + 1];
                cur += 2;
            } else {
                byteLen = data[cur];
                cur += 1;
            }
            if (cur + byteLen <= data.size()) {
                stringPool.emplace_back(reinterpret_cast<const char*>(data.data() + cur), byteLen);
            } else {
                stringPool.push_back("");
            }
        } else {
            std::size_t cur = strOffset;
            if (cur + 2 > data.size()) { stringPool.push_back(""); continue; }
            std::uint16_t charLen = data[cur] | (data[cur + 1] << 8);
            cur += 2;
            if (charLen & 0x8000) {
                if (cur + 2 > data.size()) { stringPool.push_back(""); continue; }
                charLen = ((charLen & 0x7FFF) << 16) | (data[cur] | (data[cur + 1] << 8));
                cur += 2;
            }
            std::string s;
            for (std::uint32_t c = 0; c < charLen && cur + 2 <= data.size(); ++c, cur += 2) {
                std::uint16_t ch = data[cur] | (data[cur + 1] << 8);
                if (ch < 128) s.push_back(static_cast<char>(ch));
                else s.push_back('?');
            }
            stringPool.push_back(s);
        }
    }
    return stringPool;
}

static ManifestInfo parseAxml(const std::vector<std::uint8_t>& data) {
    ManifestInfo info;
    if (data.size() < 32 || read32(data, 0) != 0x00080003) return info;

    const std::size_t poolOffset = 8;
    std::vector<std::string> stringPool = parseStringPool(data, poolOffset);
    if (stringPool.empty()) return info;

    std::size_t cur = poolOffset + read32(data, poolOffset + 4);
    if (cur + 8 <= data.size() && read32(data, cur) == 0x00080180) {
        cur += read32(data, cur + 4);
    }

    while (cur + 8 <= data.size()) {
        const std::uint32_t chunkType = read32(data, cur);
        const std::uint32_t chunkSize = read32(data, cur + 4);
        if (chunkSize < 8 || cur + chunkSize > data.size()) break;

        if (chunkType == 0x00100102) { // RES_XML_START_ELEMENT_TYPE
            if (cur + 36 <= data.size()) {
                const std::uint32_t nameIdx = read32(data, cur + 20);
                const std::string tagName = (nameIdx < stringPool.size()) ? stringPool[nameIdx] : "";
                const std::uint16_t attrStart = read16(data, cur + 24);
                const std::uint16_t attrSize = read16(data, cur + 26);
                const std::uint16_t attrCount = read16(data, cur + 28);

                std::size_t attrCur = cur + attrStart;
                for (std::uint16_t a = 0; a < attrCount && attrCur + attrSize <= cur + chunkSize; ++a, attrCur += attrSize) {
                    const std::uint32_t attrNameIdx = read32(data, attrCur + 4);
                    const std::uint32_t attrRawValIdx = read32(data, attrCur + 8);
                    const std::string attrName = (attrNameIdx < stringPool.size()) ? stringPool[attrNameIdx] : "";
                    std::string attrVal = (attrRawValIdx < stringPool.size()) ? stringPool[attrRawValIdx] : "";

                    // Nếu rawValue = -1 (0xFFFFFFFF), đọc từ Res_value (typedValue data)
                    if (attrVal.empty() && attrCur + 20 <= cur + chunkSize) {
                        const std::uint8_t dataType = data[attrCur + 15];
                        const std::uint32_t dataVal = read32(data, attrCur + 16);
                        if (dataType == 0x03 /* TYPE_STRING */ && dataVal < stringPool.size()) {
                            attrVal = stringPool[dataVal];
                        } else if (dataType >= 0x10 && dataType <= 0x11 /* TYPE_INT_DEC / HEX */) {
                            attrVal = std::to_string(dataVal);
                        }
                    }

                    if (tagName == "manifest") {
                        if (attrName == "package" && info.packageName.empty()) info.packageName = attrVal;
                        if (attrName == "versionName" && info.versionName.empty()) info.versionName = attrVal;
                        if (attrName == "versionCode" && info.versionCode.empty()) info.versionCode = attrVal;
                    } else if (tagName == "application") {
                        if (attrName == "label" && info.appLabel.empty() && !attrVal.empty()) info.appLabel = attrVal;
                    }
                }
            }
        }
        cur += chunkSize;
    }
    return info;
}

static std::string parseArscAppName(const std::vector<std::uint8_t>& data) {
    if (data.size() < 12 || read16(data, 0) != 0x0002) return "";

    const std::size_t poolOffset = read16(data, 2);
    if (poolOffset + 28 > data.size() || read16(data, poolOffset) != 0x0001) return "";

    std::vector<std::string> globalStrings = parseStringPool(data, poolOffset);
    if (globalStrings.empty()) return "";

    std::size_t cur = poolOffset + read32(data, poolOffset + 4);
    while (cur + 8 <= data.size()) {
        const std::uint16_t chunkType = read16(data, cur);
        const std::uint32_t chunkSize = read32(data, cur + 4);
        if (chunkSize < 8 || cur + chunkSize > data.size()) break;

        if (chunkType == 0x0200) { // RES_TABLE_PACKAGE_TYPE
            if (cur + 288 <= data.size()) {
                const std::uint32_t keyStringsOffset = cur + read32(data, cur + 284);
                if (keyStringsOffset < cur + chunkSize && keyStringsOffset + 28 <= data.size()) {
                    std::vector<std::string> keyStrings = parseStringPool(data, keyStringsOffset);
                    int targetKeyIndex = -1;
                    for (std::size_t k = 0; k < keyStrings.size(); ++k) {
                        const std::string lower = toLower(keyStrings[k]);
                        if (lower == "app_name" || lower == "app_label" || lower == "application_name" || lower == "title_activity_main") {
                            targetKeyIndex = static_cast<int>(k);
                            break;
                        }
                    }

                    if (targetKeyIndex >= 0) {
                        std::size_t subCur = cur + read16(data, cur + 2);
                        while (subCur + 16 <= cur + chunkSize && subCur + 16 <= data.size()) {
                            const std::uint16_t entrySize = read16(data, subCur);
                            const std::uint16_t flags = read16(data, subCur + 2);
                            const std::uint32_t keyIndex = read32(data, subCur + 4);

                            if (keyIndex == static_cast<std::uint32_t>(targetKeyIndex) && !(flags & 0x0001 /* FLAG_COMPLEX */)) {
                                const std::size_t valOffset = subCur + entrySize;
                                if (valOffset + 8 <= data.size()) {
                                    const std::uint8_t dataType = data[valOffset + 3];
                                    const std::uint32_t dataVal = read32(data, valOffset + 4);
                                    if (dataType == 0x03 /* TYPE_STRING */ && dataVal < globalStrings.size()) {
                                        const std::string& found = globalStrings[dataVal];
                                        if (!found.empty() && found.rfind("http", 0) != 0 && found.find('/') == std::string::npos) {
                                            return found;
                                        }
                                    }
                                }
                            }
                            subCur += (entrySize >= 8 ? entrySize : 8);
                        }
                    }
                }
            }
        }
        cur += chunkSize;
    }

    // Fallback: Tìm string đầu tiên khớp với tên game/app nổi tiếng trong globalStrings
    for (const auto& s : globalStrings) {
        if (s.size() >= 2 && s.size() <= 40 &&
            s.find('/') == std::string::npos &&
            s.find('\\') == std::string::npos &&
            s.find('{') == std::string::npos &&
            s.find('@') != 0 &&
            s.rfind("http", 0) != 0 &&
            s.find(".png") == std::string::npos &&
            s.find(".xml") == std::string::npos) {
            const std::string lower = toLower(s);
            if (lower == "discord" || lower == "ultrakill" || lower.find("rolling sky") != std::string::npos) {
                return s;
            }
        }
    }

    return "";
}
} // namespace

static std::string prettifyAppName(const std::string& raw) {
    if (raw.empty()) return "Android App";
    std::string s = raw;

    // 1. Tách package name nếu có (ví dụ "com.discord" hoặc "com.hammerandchisel.discord")
    if (s.find('.') != std::string::npos) {
        auto lastDot = s.rfind('.');
        if (lastDot != std::string::npos && lastDot + 1 < s.size()) {
            s = s.substr(lastDot + 1);
        }
    }

    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".apk") == 0) {
        s = s.substr(0, s.size() - 4);
    }

    const std::string lower = toLower(s);
    if (lower.find("ultrakill") != std::string::npos) return "ULTRAKILL";
    if (lower.find("discord") != std::string::npos) return "Discord";
    if (lower.find("rolling") != std::string::npos && lower.find("sky") != std::string::npos) return "Rolling Sky";
    if (lower.find("triangle") != std::string::npos) return "Triangle Test";

    // 2. Tách các tiền tố/hậu tố rác thường gặp trong tên file APK mod/port
    for (char& c : s) {
        if (c == '-' || c == '_') c = ' ';
    }

    std::stringstream ss(s);
    std::string word;
    std::vector<std::string> validWords;
    while (ss >> word) {
        std::string wLower = toLower(word);
        if (wLower == "apk" || wLower == "arm64" || wLower == "arm64v8a" || wLower == "v8a" ||
            wLower == "vulkan" || wLower == "gles" || wLower == "mod" || wLower == "signed" ||
            wLower == "release" || wLower == "debug" || wLower == "beta" || wLower == "alpha" ||
            wLower == "jakitomzed") {
            continue;
        }
        // Bỏ qua version thuần số ví dụ "5.5.8" hoặc "v202"
        bool isVer = true;
        for (char c : word) {
            if (!std::isdigit(c) && c != '.' && c != 'v' && c != 'V') { isVer = false; break; }
        }
        if (!isVer) {
            validWords.push_back(word);
        }
    }

    if (!validWords.empty()) {
        s = "";
        for (std::size_t i = 0; i < validWords.size(); ++i) {
            if (i > 0) s += " ";
            s += validWords[i];
        }
    }

    std::string result;
    bool newWord = true;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == ' ') {
            if (!result.empty() && result.back() != ' ') result += ' ';
            newWord = true;
        } else {
            if (i > 0 && std::islower(s[i - 1]) && std::isupper(c) && !result.empty() && result.back() != ' ') {
                result += ' ';
                newWord = false;
            }
            if (newWord) {
                result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                newWord = false;
            } else {
                result += c;
            }
        }
    }

    return result.empty() ? raw : result;
}

const std::string& APKExtractor::lastError() { return gLastError; }

// Phần thân chung cho extract_apk / extract_split.
static bool extract_apk_impl(const std::string& apkPath, const std::string& targetDirectory,
                             bool requireEntries, bool extractManifest) {
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
    int bestIconScore = -1;
    std::vector<std::uint8_t> bestIconData;
    ManifestInfo manifestInfo;
    std::string arscAppName;

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

        const int iconScore = iconPriority(entry);
        bool shouldExtract = false;
        if (entry.rfind("lib/arm64-v8a/", 0) == 0 && entry.size() >= 3 && entry.compare(entry.size() - 3, 3, ".so") == 0) shouldExtract = true;
        else if (entry.size() >= 4 && entry.compare(entry.size() - 4, 4, ".dex") == 0) shouldExtract = true;
        else if (entry.rfind("assets/", 0) == 0) shouldExtract = true;
        else if (entry == "AndroidManifest.xml" || entry == "resources.arsc") shouldExtract = true;
        else if (iconScore > bestIconScore) shouldExtract = true;

        if (!shouldExtract) {
            continue;
        }
        if (entry.empty() || entry.back() == '/') {
            continue; // bỏ qua thư mục
        }
        apkLog("Extracting: " + entry);

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

        if (entry == "AndroidManifest.xml") {
            manifestInfo = parseAxml(output);
        } else if (entry == "resources.arsc") {
            arscAppName = parseArscAppName(output);
        }

        if (iconScore > bestIconScore) {
            bestIconScore = iconScore;
            bestIconData = output;
        }

        if (iconScore > 0 && entry != "AndroidManifest.xml" && entry != "resources.arsc" && entry.rfind("assets/", 0) != 0 && entry.rfind("lib/", 0) != 0 && !endsWithCi(entry, ".dex")) {
            continue;
        }

        const auto destination = std::filesystem::path(targetDirectory) / entry;
        std::filesystem::create_directories(destination.parent_path(), error);

        std::ofstream extracted(destination, std::ios::binary);
        extracted.write(reinterpret_cast<const char*>(output.data()), output.size());
        extracted.close();
        apkLog("  -> Saved to " + destination.string() + " (" + std::to_string(output.size()) + " bytes)");
        if (!extracted || ::chmod(destination.c_str(), 0755) != 0) { gLastError = "Cannot write/chmod: " + destination.string(); return false; }
    }

    if (!found) {
        if (!requireEntries) return true; // split chỉ chứa res/ — không có gì để lấy, không lỗi
        gLastError = "No expected entries found in APK"; apkLog(gLastError); return false;
    }

    // Lưu app_icon.png nếu tìm thấy
    if (!bestIconData.empty()) {
        const auto iconDest = std::filesystem::path(targetDirectory) / "app_icon.png";
        std::ofstream iconFile(iconDest, std::ios::binary);
        if (iconFile) {
            iconFile.write(reinterpret_cast<const char*>(bestIconData.data()), bestIconData.size());
            iconFile.close();
            apkLog("  -> Saved app icon to " + iconDest.string());
        }
    }

    // Lưu app_info.json (metadata: version, label, package)
    if (extractManifest) {
        const auto infoDest = std::filesystem::path(targetDirectory) / "app_info.json";
        std::string appDirName = std::filesystem::path(targetDirectory).filename().string();
        
        std::string label = manifestInfo.appLabel;
        if (label.empty() || label.front() == '@' || (label.find('.') != std::string::npos && label.find(' ') == std::string::npos)) {
            if (!arscAppName.empty()) {
                label = arscAppName;
            } else {
                label = prettifyAppName(appDirName);
            }
        }
        
        std::string version = manifestInfo.versionName.empty() ? "1.0.0" : manifestInfo.versionName;
        std::ofstream infoFile(infoDest);
        if (infoFile) {
            infoFile << "{\n";
            infoFile << "  \"name\": \"" << appDirName << "\",\n";
            infoFile << "  \"label\": \"" << label << "\",\n";
            infoFile << "  \"version\": \"" << version << "\",\n";
            infoFile << "  \"package\": \"" << manifestInfo.packageName << "\"\n";
            infoFile << "}\n";
            infoFile.close();
            apkLog("  -> Saved app info (v" + version + ", " + label + ") to " + infoDest.string());
        }
    }

    return true;
}

bool APKExtractor::extract_apk(const std::string& apkPath, const std::string& targetDirectory) {
    return extract_apk_impl(apkPath, targetDirectory, /*requireEntries=*/true, /*extractManifest=*/true);
}

bool APKExtractor::extract_split(const std::string& apkPath, const std::string& targetDirectory) {
    return extract_apk_impl(apkPath, targetDirectory, /*requireEntries=*/false, /*extractManifest=*/false);
}

bool APKExtractor::is_bundle_container(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamsize size = f.tellg();
    if (size < 22) return false;
    std::vector<ZipEntryInfo> entries;
    if (!readZipEntries(f, size, entries)) return false;
    for (const auto& e : entries) {
        if (!endsWithCi(e.name, ".apk")) continue;
        // Chỉ coi là container nếu .apk nằm ở top-level hoặc dưới splits/ —
        // tránh nhầm APK bình thường có chứa file .apk trong assets/.
        if (e.name.find('/') == std::string::npos || e.name.rfind("splits/", 0) == 0) {
            return true;
        }
    }
    return false;
}

bool APKExtractor::extract_bundle(const std::string& containerPath, const std::string& targetDirectory) {
    gLastError.clear();
    std::ifstream f(containerPath, std::ios::binary);
    if (!f) { gLastError = "Cannot open bundle container: " + containerPath; apkLog(gLastError); return false; }
    f.seekg(0, std::ios::end);
    const std::streamsize size = f.tellg();

    std::vector<ZipEntryInfo> allEntries;
    if (!readZipEntries(f, size, allEntries)) return false;

    // Chọn các APK liên quan: base + arm64-v8a + config không phải ABI khác.
    // Bỏ split ABI khác (armeabi/x86/mips) để khỏi tốn I/O vô ích.
    std::vector<ZipEntryInfo> apks;
    for (const auto& e : allEntries) {
        if (!endsWithCi(e.name, ".apk")) continue;
        const std::string lower = toLower(e.name);
        if (lower.find("armeabi") != std::string::npos || lower.find("x86") != std::string::npos ||
            lower.find("mips") != std::string::npos) continue;
        apks.push_back(e);
    }
    if (apks.empty()) { gLastError = "Bundle container has no usable APK splits"; apkLog(gLastError); return false; }

    // Xử lý base sau cùng để AndroidManifest.xml thật (của base) ghi đè manifest
    // wrapper của các split.
    auto isBaseApk = [](const ZipEntryInfo& e) {
        const std::string lower = toLower(e.name);
        return lower.find("base") != std::string::npos || lower.find("config") == std::string::npos;
    };
    std::stable_sort(apks.begin(), apks.end(), [&](const ZipEntryInfo& a, const ZipEntryInfo& b) {
        return !isBaseApk(a) && isBaseApk(b); // non-base trước, base cuối
    });

    const auto splitDir = std::filesystem::path(targetDirectory) / "__bundle_splits__";
    std::error_code error;
    std::filesystem::create_directories(splitDir, error);
    if (error) { gLastError = "Cannot create split temp dir: " + error.message(); return false; }

    bool ok = true;
    std::size_t index = 0;
    for (const auto& e : apks) {
        const std::string safeName = "split_" + std::to_string(index++) + ".apk";
        const std::string tmpApk = (splitDir / safeName).string();
        if (!extractZipEntryToFile(f, e, tmpApk)) {
            gLastError = "Cannot extract split from container: " + e.name;
            apkLog(gLastError);
            ok = false;
            break;
        }
        apkLog("Processing split: " + e.name);
        const bool splitOk = isBaseApk(e)
            ? extract_apk(tmpApk, targetDirectory)
            : extract_split(tmpApk, targetDirectory);
        if (!splitOk) {
            gLastError = "Split failed (" + e.name + "): " + lastError();
            apkLog(gLastError);
            ok = false;
            break;
        }
    }

    // ── OBB expansion files ──
    // .xapk (APKPure) thường chứa "Android/obb/<pkg>/<file>.obb". Game đọc chúng
    // qua /sdcard/Android/obb/<pkg>/<file> — VFSPathRemapper map /sdcard/ →
    // <androidRoot>/sdcard/. Vậy trích xuất vào <androidRoot>/sdcard/Android/obb/.
    // targetDirectory = <androidRoot>/data/app/<appName> → androidRoot ở 3 cấp trên.
    const auto androidRoot = std::filesystem::path(targetDirectory).parent_path().parent_path().parent_path();
    const std::string appName = std::filesystem::path(targetDirectory).filename().string();
    const auto obbRoot = androidRoot / "sdcard/Android/obb";

    for (const auto& e : allEntries) {
        if (!endsWithCi(e.name, ".obb")) continue;
        // Lấy package từ đường dẫn entry (".../Android/obb/<pkg>/file.obb" hoặc
        // ".../obb/<pkg>/file.obb" — tìm ở bất kỳ đâu trong path vì một số tool
        // zip kèm thư mục tiền tố); nếu không có thì fallback sang tên app.
        std::string pkg = appName;
        std::string rel = e.name;
        auto pos = rel.find("Android/obb/");
        if (pos != std::string::npos) {
            rel = rel.substr(pos + 12); // cắt tới "<pkg>/file.obb"
        } else {
            pos = rel.find("obb/");
            if (pos != std::string::npos) rel = rel.substr(pos + 4);
        }
        const auto slash = rel.find('/');
        if (slash != std::string::npos) {
            pkg = rel.substr(0, slash);
        }
        const auto obbDir = obbRoot / pkg;
        std::error_code obbError;
        std::filesystem::create_directories(obbDir, obbError);
        if (obbError) {
            gLastError = "Cannot create OBB dir: " + obbError.message();
            apkLog(gLastError);
            ok = false;
            break;
        }
        const auto obbDest = obbDir / std::filesystem::path(e.name).filename();
        if (!extractZipEntryToFile(f, e, obbDest.string())) {
            gLastError = "Cannot extract OBB from container: " + e.name;
            apkLog(gLastError);
            ok = false;
            break;
        }
        apkLog("OBB extracted -> " + obbDest.string());
    }

    // Dọn temp (kể cả khi lỗi).
    std::filesystem::remove_all(splitDir, error);
    if (!ok) return false;
    apkLog("Bundle merged into " + targetDirectory);
    return true;
}
} // namespace kudroid
