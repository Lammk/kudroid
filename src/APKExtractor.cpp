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
#include <sstream>
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

// Read end-of-central-directory + entire central directory (metadata only).
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

// Decompress an entry (stored or deflate) to a file — streaming, do not load the entire entry.
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
// entire lowercase (used to filter split by name).
std::string toLower(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

// ManifestInfo is now a public struct declared in APKExtractor.h — parseAxml
// fill it in, kudroid_bridge calls parse_manifest() directly on the resolved AXML
// compressed to get the correct LAUNCHER activity (instead of guessing by folder name).

static int iconPriority(const std::string& name) {
    if (!endsWithCi(name, ".png") && !endsWithCi(name, ".webp") && !endsWithCi(name, ".jpg") && !endsWithCi(name, ".jpeg")) return -1;
    if (endsWithCi(name, ".9.png")) return -1; // 9-patch border format is not an app icon

    const std::string lower = toLower(name);
    int score = 0;

    // Prioritize resolution
    if (lower.find("xxxhdpi") != std::string::npos) score += 60;
    else if (lower.find("xxhdpi") != std::string::npos) score += 50;
    else if (lower.find("xhdpi") != std::string::npos) score += 40;
    else if (lower.find("hdpi") != std::string::npos) score += 30;
    else if (lower.find("mdpi") != std::string::npos) score += 20;
    else if (lower.find("ldpi") != std::string::npos) score += 10;
    else if (lower.find("nodpi") != std::string::npos) score += 35;
    else if (lower.find("drawable") != std::string::npos || lower.find("mipmap") != std::string::npos) score += 15;

    // Priority is given to standard icon names of Android & Unity
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
    // Diagnostics: The actual manifest of some APK repacks has been parsed and is empty
    // no clue — log enough for remote debugging via logs/.
    if (data.size() < 32 || read32(data, 0) != 0x00080003) {
        char magic[64];
        std::snprintf(magic, sizeof(magic),
                      "[kudroid_axml] reject: size=%zu magic=0x%02x%02x%02x%02x (expect 0x00080003)",
                      data.size(),
                      data.size() > 3 ? data[3] : 0, data.size() > 2 ? data[2] : 0,
                      data.size() > 1 ? data[1] : 0, data.size() > 0 ? data[0] : 0);
        apkLog(magic);
        return info;
    }

    const std::size_t poolOffset = 8;
    std::vector<std::string> stringPool = parseStringPool(data, poolOffset);
    if (stringPool.empty()) {
        apkLog("[kudroid_axml] string pool EMPTY (type=" +
               std::to_string(read16(data, poolOffset)) + ")");
        return info;
    }

    std::size_t cur = poolOffset + read32(data, poolOffset + 4);
    if (cur + 8 <= data.size() && read32(data, cur) == 0x00080180) {
        cur += read32(data, cur + 4);
    }

    // State finds LAUNCHER activity: manifest contains MANY <activity>, only
    // activity (or activity-alias) has <intent-filter> with
    // <action android:name="android.intent.action.MAIN"> +
    // <category android:name="android.intent.category.LAUNCHER"> is
    // entry point that the Android launcher opens. Get the first activity as before
    // This is WRONG (e.g. Minecraft: the first activity is not a launcher).
    // IMPORTANT: state must be OUTSIDE the while loop — AXML splits into multiple
    // chunk (one chunk per tag), declared in loop will reset each chunk.
    static const char* kActionMain = "android.intent.action.MAIN";
    static const char* kCategoryLauncher = "android.intent.category.LAUNCHER";
    std::string currentActivity;   // name of the <activity>/<activity-alias> being browsed
    std::string aliasTarget;       // targetActivity of <activity-alias>
    bool inIntentFilter = false;
    bool sawActionMain = false;
    bool sawCategoryLauncher = false;
    bool sawAnyIntentFilter = false;
    bool currentExported = false;
    bool currentExportedSet = false;
    std::string firstActivityName; // fallback when there is no intent-filter

    // Record every activity, not just the launcher. Callers can then try real
    // manifest entries in order instead of guessing names from the package.
    auto commitActivity = [&](const std::string& name, bool isAlias) {
        if (name.empty()) return;
        for (ActivityEntry& e : info.activities) {
            if (e.name != name) continue;
            // Same activity seen again (multiple intent-filter blocks): merge.
            e.isLauncher = e.isLauncher || (sawActionMain && sawCategoryLauncher);
            e.isExported = e.isExported || currentExported || sawAnyIntentFilter;
            return;
        }
        ActivityEntry e;
        e.name = name;
        e.isLauncher = sawActionMain && sawCategoryLauncher;
        // An activity with an intent-filter is implicitly exported unless it says
        // otherwise, which is how the launcher can start it.
        e.isExported = currentExportedSet ? currentExported : sawAnyIntentFilter;
        e.isAlias = isAlias;
        info.activities.push_back(e);
    };

    auto commitLauncher = [&](const std::string& name) {
        if (info.mainActivity.empty() && !name.empty() &&
            sawActionMain && sawCategoryLauncher) {
            info.mainActivity = name;
        }
    };

    while (cur + 8 <= data.size()) {
        const std::uint32_t chunkType = read32(data, cur);
        const std::uint32_t chunkSize = read32(data, cur + 4);
        if (chunkSize < 8 || cur + chunkSize > data.size()) break;

        if (chunkType == 0x00100102) { // RES_XML_START_ELEMENT_TYPE
            if (cur + 36 <= data.size()) {
                const std::uint32_t nameIdx = read32(data, cur + 20);
                const std::string tagName = (nameIdx < stringPool.size()) ? stringPool[nameIdx] : "";
                // Spec ResXMLTree_attrExt: attributeStart is the offset FROM START
                // struct attrExt (chunk+16), not from the beginning of the chunk. aapt writes
                // attrStart=20 → attrs starts at chunk+36. Old code used
                // cur+attrStart → reads 16 bytes off every real manifest
                // (synthetic test matches the old code so the password is fake).
                const std::uint16_t attrStart = read16(data, cur + 24);
                const std::uint16_t attrSize = read16(data, cur + 26);
                const std::uint16_t attrCount = read16(data, cur + 28);
                const std::size_t attrsBase = cur + 16 + attrStart;

                const bool isActivityTag =
                    tagName == "activity" || tagName == "activity-alias";
                if (isActivityTag) {
                    // Start a new activity — reset the state of the previous activity.
                    currentActivity.clear();
                    aliasTarget.clear();
                    sawActionMain = false;
                    sawCategoryLauncher = false;
                    sawAnyIntentFilter = false;
                    currentExported = false;
                    currentExportedSet = false;
                    inIntentFilter = false;
                } else if (tagName == "intent-filter") {
                    inIntentFilter = true;
                    sawAnyIntentFilter = true;
                }

                std::size_t attrCur = attrsBase;
                for (std::uint16_t a = 0; a < attrCount && attrCur + attrSize <= cur + chunkSize; ++a, attrCur += attrSize) {
                    const std::uint32_t attrNameIdx = read32(data, attrCur + 4);
                    const std::uint32_t attrRawValIdx = read32(data, attrCur + 8);
                    const std::string attrName = (attrNameIdx < stringPool.size()) ? stringPool[attrNameIdx] : "";
                    std::string attrVal = (attrRawValIdx < stringPool.size()) ? stringPool[attrRawValIdx] : "";

                    // If rawValue = -1 (0xFFFFFFFF), read from Res_value (typedValue data)
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
                        // android:name on <application> is the custom Application
                        // subclass; Android instantiates it before any Activity.
                        if (attrName == "name" && info.appClass.empty() && !attrVal.empty()) info.appClass = attrVal;
                    } else if (isActivityTag) {
                        if (attrName == "name" && currentActivity.empty() && !attrVal.empty()) {
                            currentActivity = attrVal;
                            if (firstActivityName.empty()) firstActivityName = attrVal;
                        } else if (tagName == "activity-alias" && attrName == "targetActivity" && !attrVal.empty()) {
                            aliasTarget = attrVal;
                        } else if (attrName == "exported") {
                            currentExportedSet = true;
                            // Boolean attributes arrive as "true"/"false" or as an int 0/1.
                            currentExported = (attrVal == "true" || attrVal == "1");
                        }
                    } else if (tagName == "action") {
                        if (inIntentFilter && attrName == "name" && attrVal == kActionMain) {
                            sawActionMain = true;
                        }
                    } else if (tagName == "category") {
                        if (inIntentFilter && attrName == "name" && attrVal == kCategoryLauncher) {
                            sawCategoryLauncher = true;
                        }
                    }
                }

                // Activity-alias points to the real activity via targetActivity —
                // Android launcher opens TARGET, not alias.
                if (tagName == "activity-alias") {
                    commitLauncher(!aliasTarget.empty() ? aliasTarget : currentActivity);
                }
                // Self-closing <activity .../> never produces an end element, so
                // record it here too; commitActivity dedupes.
                if (isActivityTag && !currentActivity.empty()) {
                    commitActivity(!aliasTarget.empty() ? aliasTarget : currentActivity,
                                   tagName == "activity-alias");
                }
            }
        } else if (chunkType == 0x00100103) { // RES_XML_END_ELEMENT_TYPE
            if (cur + 24 <= data.size()) {
                const std::uint32_t nameIdx = read32(data, cur + 20);
                const std::string tagName = (nameIdx < stringPool.size()) ? stringPool[nameIdx] : "";
                if (tagName == "intent-filter") {
                    inIntentFilter = false;
                } else if (tagName == "activity" || tagName == "activity-alias") {
                    // </activity>: if intent-filter MAIN+LAUNCHER appears between
                    // Apparently this is the launcher activity.
                    commitLauncher(currentActivity);
                    commitActivity(!aliasTarget.empty() ? aliasTarget : currentActivity,
                                   tagName == "activity-alias");
                    currentActivity.clear();
                    aliasTarget.clear();
                    sawActionMain = false;
                    sawCategoryLauncher = false;
                    sawAnyIntentFilter = false;
                    currentExported = false;
                    currentExportedSet = false;
                    inIntentFilter = false;
                }
            }
        }
        cur += chunkSize;
    }

    // Fallback: no intent-filter found (non-standard/obfuscated manifest)
    // → use the first activity as the old behavior, better than empty.
    if (info.mainActivity.empty() && !firstActivityName.empty()) {
        info.mainActivity = firstActivityName;
    }

    // Manifests may write ".MainActivity" or even a bare "MainActivity" relative to
    // the package; expand both so callers always hold a resolvable class name.
    auto qualify = [&](std::string& name) {
        if (name.empty() || info.packageName.empty()) return;
        if (name.front() == '.') {
            name = info.packageName + name;
        } else if (name.find('.') == std::string::npos) {
            name = info.packageName + "." + name;
        }
    };
    qualify(info.mainActivity);
    qualify(info.appClass);
    for (ActivityEntry& e : info.activities) qualify(e.name);

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

    // Fallback: Find the first string that matches a famous game/app name in globalStrings
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
            if (lower.find("minecraft") != std::string::npos) {
                return "Minecraft";
            }
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

    // 1. Separate the package name if there is one (for example "com.discord" or "com.hammerandchisel.discord")
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
    if (lower.find("minecraft") != std::string::npos || lower.find("mojang") != std::string::npos) return "Minecraft";
    if (lower.find("ultrakill") != std::string::npos) return "ULTRAKILL";
    if (lower.find("discord") != std::string::npos) return "Discord";
    if (lower.find("rolling") != std::string::npos && lower.find("sky") != std::string::npos) return "Rolling Sky";
    if (lower.find("triangle") != std::string::npos) return "Triangle Test";

    // 2. Separate common junk prefixes/suffixes in mod/port APK file names
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
            wLower == "jakitomzed" || wLower == "bandishare" || wLower == "apkpure" ||
            wLower == "moddroid" || wLower == "an1" || wLower == "apkmirror") {
            continue;
        }
        // Ignore pure numeric version e.g. "5.5.8" or "v202"
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

// Generic body for extract_apk / extract_split.
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
            continue; // skip folder
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
        if (!requireEntries) return true; // split contains only res/ — nothing to get, no error
        gLastError = "No expected entries found in APK"; apkLog(gLastError); return false;
    }

    // Save app_icon.png if found
    if (!bestIconData.empty()) {
        const auto iconDest = std::filesystem::path(targetDirectory) / "app_icon.png";
        std::ofstream iconFile(iconDest, std::ios::binary);
        if (iconFile) {
            iconFile.write(reinterpret_cast<const char*>(bestIconData.data()), bestIconData.size());
            iconFile.close();
            apkLog("  -> Saved app icon to " + iconDest.string());
        }
    }

    // Save app_info.json (metadata: version, label, package)
    if (extractManifest) {
        const auto infoDest = std::filesystem::path(targetDirectory) / "app_info.json";
        std::string appDirName = std::filesystem::path(targetDirectory).filename().string();
        
        std::string label = manifestInfo.appLabel;
        if (label.empty() || label.front() == '@' || (label.find('.') != std::string::npos && label.find(' ') == std::string::npos)) {
            if (!arscAppName.empty()) {
                label = arscAppName;
            } else if (manifestInfo.packageName.find("minecraft") != std::string::npos || manifestInfo.packageName.find("mojang") != std::string::npos) {
                label = "Minecraft";
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
            infoFile << "  \"package\": \"" << manifestInfo.packageName << "\",\n";
            infoFile << "  \"main_activity\": \"" << manifestInfo.mainActivity << "\"\n";
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
        // Only considered a container if the .apk is at the top-level or below splits/ —
        // Avoid mistakenly normal APK containing .apk file in assets/.
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

    // Select related APKs: base + arm64-v8a + other non-ABI config.
    // Remove other ABI split (armeabi/x86/mips) to avoid wasting useless I/O.
    std::vector<ZipEntryInfo> apks;
    for (const auto& e : allEntries) {
        if (!endsWithCi(e.name, ".apk")) continue;
        const std::string lower = toLower(e.name);
        if (lower.find("armeabi") != std::string::npos || lower.find("x86") != std::string::npos ||
            lower.find("mips") != std::string::npos) continue;
        apks.push_back(e);
    }
    if (apks.empty()) { gLastError = "Bundle container has no usable APK splits"; apkLog(gLastError); return false; }

    // Process the base finally so that the actual (base's) AndroidManifest.xml overwrites the manifest
    // wrapper of the split.
    auto isBaseApk = [](const ZipEntryInfo& e) {
        const std::string lower = toLower(e.name);
        return lower.find("base") != std::string::npos || lower.find("config") == std::string::npos;
    };
    std::stable_sort(apks.begin(), apks.end(), [&](const ZipEntryInfo& a, const ZipEntryInfo& b) {
        return !isBaseApk(a) && isBaseApk(b); // non-base first, base last
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
    // .xapk (APKPure) usually contains "Android/obb/<pkg>/<file>.obb". The game reads them
    // qua /sdcard/Android/obb/<pkg>/<file> — VFSPathRemapper map /sdcard/ →
    // <androidRoot>/sdcard/. So extract to <androidRoot>/sdcard/Android/obb/.
    // targetDirectory = <androidRoot>/data/app/<appName> → androidRoot 3 levels above.
    const auto androidRoot = std::filesystem::path(targetDirectory).parent_path().parent_path().parent_path();
    const std::string appName = std::filesystem::path(targetDirectory).filename().string();
    const auto obbRoot = androidRoot / "sdcard/Android/obb";

    for (const auto& e : allEntries) {
        if (!endsWithCi(e.name, ".obb")) continue;
        // Get the package from the entry path (".../Android/obb/<pkg>/file.obb" or
        // ".../obb/<pkg>/file.obb" — search anywhere in the path for some tools
        // zip with prefix directory); If not, fallback to app name.
        std::string pkg = appName;
        std::string rel = e.name;
        auto pos = rel.find("Android/obb/");
        if (pos != std::string::npos) {
            rel = rel.substr(pos + 12); // cut to "<pkg>/file.obb"
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

    // Clean up temp (even if there are errors).
    std::filesystem::remove_all(splitDir, error);
    if (!ok) return false;
    apkLog("Bundle merged into " + targetDirectory);
    return true;
}

ManifestInfo APKExtractor::parse_manifest(const std::uint8_t* data, std::size_t size) {
    if (!data || size == 0) return ManifestInfo{};
    return parseAxml(std::vector<std::uint8_t>(data, data + size));
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_manifest_text — AndroidManifest.xml plain TEXT. APK repack by
// apktool/mod tools (BANDISHARE etc.) usually contain manifest text instead
// binary AXML; parseAxml returns empty on them. Heuristic: scan each card
// <activity ...> / <activity-alias ...>, collect attribute name=, track
// The child intent-filter contains action MAIN + category LAUNCHER.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

std::string extractXmlAttr(const std::string& tag, const char* attrName) {
    // Find attrName="value" or attrName='value' in the tag string. Attr can
    // has namespace (android:name=) — accepts both forms: " name=" and ":name=".
    const std::string needle = std::string(attrName) + "=";
    std::size_t pos = 0;
    while ((pos = tag.find(needle, pos)) != std::string::npos) {
        // The character before attrName must be a space, '<' or ':' (namespace prefix).
        if (pos > 0) {
            const char prev = tag[pos - 1];
            if (!std::isspace(static_cast<unsigned char>(prev)) &&
                prev != '<' && prev != ':') {
                pos += needle.size();
                continue;
            }
        }
        pos += needle.size();
        if (pos >= tag.size()) break;
        const char quote = tag[pos];
        if (quote != '"' && quote != '\'') continue;
        const std::size_t end = tag.find(quote, pos + 1);
        if (end == std::string::npos) break;
        return tag.substr(pos + 1, end - pos - 1);
    }
    return "";
}

} // namespace

ManifestInfo APKExtractor::parse_manifest_text(const char* data, std::size_t size) {
    ManifestInfo info;
    if (!data || size == 0) return info;
    const std::string xml(data, size);

    // package="..." on the <manifest> tag.
    {
        const std::size_t mpos = xml.find("<manifest");
        if (mpos != std::string::npos) {
            const std::size_t close = xml.find('>', mpos);
            if (close != std::string::npos) {
                info.packageName = extractXmlAttr(xml.substr(mpos, close - mpos), "package");
            }
        }
    }

    static const char* kActionMain = "android.intent.action.MAIN";
    static const char* kCategoryLauncher = "android.intent.category.LAUNCHER";

    std::size_t cur = 0;
    std::string currentActivity;
    std::string aliasTarget;
    bool sawActionMain = false;
    bool sawCategoryLauncher = false;
    bool sawAnyIntentFilter = false;
    bool inIntentFilter = false;
    std::string currentExported;

    auto commit = [&](const std::string& name) {
        if (info.mainActivity.empty() && !name.empty() && sawActionMain && sawCategoryLauncher) {
            info.mainActivity = name;
        }
    };

    // Same as the AXML path: keep every declared activity so callers can walk real
    // manifest entries rather than guessing names.
    auto commitActivity = [&](const std::string& name, bool isAlias) {
        if (name.empty()) return;
        for (ActivityEntry& e : info.activities) {
            if (e.name != name) continue;
            e.isLauncher = e.isLauncher || (sawActionMain && sawCategoryLauncher);
            e.isExported = e.isExported || (currentExported == "true") || sawAnyIntentFilter;
            return;
        }
        ActivityEntry e;
        e.name = name;
        e.isLauncher = sawActionMain && sawCategoryLauncher;
        e.isExported = currentExported.empty() ? sawAnyIntentFilter : (currentExported == "true");
        e.isAlias = isAlias;
        info.activities.push_back(e);
    };

    while ((cur = xml.find('<', cur)) != std::string::npos) {
        const std::size_t end = xml.find('>', cur);
        if (end == std::string::npos) break;
        const std::string tag = xml.substr(cur, end - cur + 1);
        const bool isClose = tag.size() > 1 && tag[1] == '/';
        const bool isSelfClose = tag.size() > 1 && tag[tag.size() - 2] == '/';

        // Card name: remove '<' or '</' and take the first token.
        std::string tagName;
        {
            std::size_t s = 1;
            while (s < tag.size() && (tag[s] == '/' || std::isspace(static_cast<unsigned char>(tag[s])))) ++s;
            std::size_t e = s;
            while (e < tag.size() && !std::isspace(static_cast<unsigned char>(tag[e])) && tag[e] != '/' && tag[e] != '>') ++e;
            tagName = tag.substr(s, e - s);
        }

        if (tagName == "activity" || tagName == "activity-alias") {
            if (!isClose) {
                currentActivity = extractXmlAttr(tag, "name");
                aliasTarget = extractXmlAttr(tag, "targetActivity");
                currentExported = extractXmlAttr(tag, "exported");
                sawActionMain = false;
                sawCategoryLauncher = false;
                sawAnyIntentFilter = false;
                inIntentFilter = false;
                if (isSelfClose) {
                    // <activity ... /> has no closing tag, so record it now.
                    commit(!aliasTarget.empty() ? aliasTarget : currentActivity);
                    commitActivity(!aliasTarget.empty() ? aliasTarget : currentActivity,
                                   tagName == "activity-alias");
                    currentActivity.clear();
                    aliasTarget.clear();
                    currentExported.clear();
                }
            } else {
                commit(currentActivity);
                commitActivity(!aliasTarget.empty() ? aliasTarget : currentActivity,
                               tagName == "activity-alias");
                currentActivity.clear();
                aliasTarget.clear();
                currentExported.clear();
                sawActionMain = false;
                sawCategoryLauncher = false;
                sawAnyIntentFilter = false;
                inIntentFilter = false;
            }
        } else if (tagName == "intent-filter") {
            inIntentFilter = !isClose;
            if (!isClose) sawAnyIntentFilter = true;
        } else if (tagName == "application") {
            if (!isClose && info.appClass.empty()) {
                info.appClass = extractXmlAttr(tag, "name");
            }
        } else if (tagName == "action") {
            if (inIntentFilter && extractXmlAttr(tag, "name") == kActionMain) sawActionMain = true;
        } else if (tagName == "category") {
            if (inIntentFilter && extractXmlAttr(tag, "name") == kCategoryLauncher) sawCategoryLauncher = true;
        }

        cur = end + 1;
    }

    auto qualify = [&](std::string& name) {
        if (name.empty() || info.packageName.empty()) return;
        if (name.front() == '.') {
            name = info.packageName + name;
        } else if (name.find('.') == std::string::npos) {
            name = info.packageName + "." + name;
        }
    };
    qualify(info.mainActivity);
    qualify(info.appClass);
    for (ActivityEntry& e : info.activities) qualify(e.name);

    return info;
}

std::string APKExtractor::get_package_name(const std::string& apkPath) {
    std::ifstream f(apkPath, std::ios::binary);
    if (f) {
        f.seekg(0, std::ios::end);
        const auto size = f.tellg();
        f.seekg(0, std::ios::beg);
        if (size >= 22) {
            std::vector<ZipEntryInfo> allEntries;
            if (readZipEntries(f, size, allEntries)) {
                for (const auto& e : allEntries) {
                    if (e.name == "AndroidManifest.xml") {
                        f.seekg(e.localOffset);
                        char lhdr[30];
                        if (readExact(f, lhdr, 30) && read32b(lhdr, 0) == 0x04034b50) {
                            const std::uint16_t nameLen = read16b(lhdr, 26);
                            const std::uint16_t extraLen = read16b(lhdr, 28);
                            f.seekg(e.localOffset + 30 + nameLen + extraLen);
                            std::vector<std::uint8_t> comp(e.compressedSize);
                            if (readExact(f, comp.data(), e.compressedSize)) {
                                std::vector<std::uint8_t> uncomp;
                                if (e.compression == 0) {
                                    uncomp = std::move(comp);
                                } else if (e.compression == 8) {
                                    uncomp.resize(e.compressedSize * 4 + 4096);
                                    z_stream stream = {};
                                    stream.next_in = comp.data();
                                    stream.avail_in = static_cast<uInt>(comp.size());
                                    stream.next_out = uncomp.data();
                                    stream.avail_out = static_cast<uInt>(uncomp.size());
                                    if (inflateInit2(&stream, -MAX_WBITS) == Z_OK) {
                                        inflate(&stream, Z_FINISH);
                                        uncomp.resize(stream.total_out);
                                        inflateEnd(&stream);
                                    }
                                }
                                if (!uncomp.empty()) {
                                    ManifestInfo info = parseAxml(uncomp);
                                    if (!info.packageName.empty()) {
                                        return info.packageName;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Fallback: Automatically filter out version _1.0.10 if available
    std::string stem = std::filesystem::path(apkPath).stem().string();
    auto idx = stem.find('_');
    if (idx != std::string::npos) {
        std::string part = stem.substr(0, idx);
        if (part.find('.') != std::string::npos) return part;
    }
    return stem;
}

} // namespace kudroid
