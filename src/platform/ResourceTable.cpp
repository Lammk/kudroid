#include "kudroid/ResourceTable.h"
#include "kudroid/FrameworkResources.h"
#include "kudroid/platform/AssetShim.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kudroid {
namespace {

constexpr uint16_t kResTableType = 0x0002;
constexpr uint16_t kResStringPoolType = 0x0001;
constexpr uint16_t kResTablePackageType = 0x0200;
constexpr uint16_t kResTableTypeType = 0x0201;
constexpr uint8_t kTypeFlagSparse = 0x01;
constexpr uint16_t kEntryFlagComplex = 0x0001;
constexpr uint32_t kNoEntry = 0xffffffffu;
constexpr uint8_t kTypeReference = 0x01;
constexpr uint8_t kTypeString = 0x03;
constexpr uint8_t kTypeColorArgb8 = 0x1c;
constexpr uint8_t kTypeColorRgb8 = 0x1d;
constexpr uint8_t kTypeColorArgb4 = 0x1e;
constexpr uint8_t kTypeColorRgb4 = 0x1f;
constexpr size_t kMaxResourceTableBytes = 64u * 1024u * 1024u;

uint16_t U16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
uint32_t U32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
bool Range(size_t offset, size_t length, size_t total) {
    return offset <= total && length <= total - offset;
}
void AppendUtf8(uint32_t cp, std::string* out) {
    if (cp <= 0x7f) out->push_back(static_cast<char>(cp));
    else if (cp <= 0x7ff) {
        out->push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        out->push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out->push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}
bool ReadLength8(const uint8_t* data, size_t end, size_t* cursor, uint32_t* length) {
    if (*cursor >= end) return false;
    const uint8_t first = data[(*cursor)++];
    if ((first & 0x80) == 0) { *length = first; return true; }
    if (*cursor >= end) return false;
    *length = (static_cast<uint32_t>(first & 0x7f) << 8) | data[(*cursor)++];
    return true;
}
bool ReadLength16(const uint8_t* data, size_t end, size_t* cursor, uint32_t* length) {
    if (!Range(*cursor, 2, end)) return false;
    const uint16_t first = U16(data + *cursor);
    *cursor += 2;
    if ((first & 0x8000) == 0) { *length = first; return true; }
    if (!Range(*cursor, 2, end)) return false;
    *length = (static_cast<uint32_t>(first & 0x7fff) << 16) | U16(data + *cursor);
    *cursor += 2;
    return true;
}

bool ParseStringPool(const std::vector<uint8_t>& bytes, size_t base, size_t bound,
                     std::vector<std::string>* strings) {
    if (!Range(base, 8, bound) || U16(bytes.data() + base) != kResStringPoolType) return false;
    const uint16_t headerSize = U16(bytes.data() + base + 2);
    const uint32_t chunkSize = U32(bytes.data() + base + 4);
    if (headerSize < 28 || chunkSize < headerSize || !Range(base, chunkSize, bound)) return false;
    const size_t end = base + chunkSize;
    const uint32_t count = U32(bytes.data() + base + 8);
    const uint32_t flags = U32(bytes.data() + base + 16);
    const uint32_t stringsStart = U32(bytes.data() + base + 20);
    if (count > 1'000'000 || !Range(base + headerSize, static_cast<size_t>(count) * 4, end) ||
        stringsStart > chunkSize) return false;
    const bool utf8 = (flags & 0x100u) != 0;
    strings->clear();
    strings->reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t relative = U32(bytes.data() + base + headerSize + i * 4u);
        size_t cursor = base + stringsStart + relative;
        if (cursor >= end) return false;
        std::string value;
        if (utf8) {
            uint32_t utf16Length = 0, byteLength = 0;
            if (!ReadLength8(bytes.data(), end, &cursor, &utf16Length) ||
                !ReadLength8(bytes.data(), end, &cursor, &byteLength) ||
                !Range(cursor, static_cast<size_t>(byteLength) + 1, end) ||
                bytes[cursor + byteLength] != 0) return false;
            value.assign(reinterpret_cast<const char*>(bytes.data() + cursor), byteLength);
        } else {
            uint32_t length = 0;
            if (!ReadLength16(bytes.data(), end, &cursor, &length) ||
                length > (end - cursor) / 2 || !Range(cursor + length * 2u, 2, end) ||
                U16(bytes.data() + cursor + length * 2u) != 0) return false;
            for (uint32_t j = 0; j < length; ++j) {
                uint32_t cp = U16(bytes.data() + cursor + j * 2u);
                if (cp >= 0xd800 && cp <= 0xdbff && j + 1 < length) {
                    const uint32_t low = U16(bytes.data() + cursor + (j + 1) * 2u);
                    if (low >= 0xdc00 && low <= 0xdfff) {
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                        ++j;
                    }
                }
                AppendUtf8(cp, &value);
            }
        }
        strings->push_back(std::move(value));
    }
    return true;
}

std::string ReadPackageName(const uint8_t* data) {
    std::string name;
    for (size_t i = 0; i < 128; ++i) {
        const uint16_t ch = U16(data + i * 2);
        if (ch == 0) break;
        AppendUtf8(ch, &name);
    }
    return name;
}
char DecodeLocaleByte(uint8_t first, uint8_t second, bool region, int index) {
    if (first & 0x80) {
        const uint8_t base = region ? '0' : 'a';
        const uint8_t value = index == 0
                ? static_cast<uint8_t>(((second & 0x1f) << 2) | ((first & 0x60) >> 5))
                : static_cast<uint8_t>((second & 0xe0) >> 5);
        return static_cast<char>(base + value);
    }
    return static_cast<char>(index == 0 ? first : second);
}
std::string ReadLocale(const uint8_t* config, size_t size) {
    if (size < 12 || (config[8] == 0 && config[9] == 0)) return {};
    std::string locale;
    locale.push_back(DecodeLocaleByte(config[8], config[9], false, 0));
    locale.push_back(DecodeLocaleByte(config[8], config[9], false, 1));
    if (config[10] != 0 || config[11] != 0) {
        locale.push_back('_');
        locale.push_back(DecodeLocaleByte(config[10], config[11], true, 0));
        locale.push_back(DecodeLocaleByte(config[10], config[11], true, 1));
    }
    return locale;
}
std::string NormalizeLocale(std::string locale) {
    std::replace(locale.begin(), locale.end(), '-', '_');
    for (char& c : locale) {
        const unsigned char ch = static_cast<unsigned char>(c);
        if (c != '_') c = static_cast<char>(std::tolower(ch));
    }
    return locale;
}

struct ResourceValue { uint8_t type; uint32_t data; std::string locale; };
struct ParserAnalysis {
    ResourceTableAnalysis* result = nullptr;
    std::set<uint32_t> resourceIds;
    std::set<std::string> packages;
    std::map<uint8_t, uint64_t> valueTypeCounts;
    std::map<std::pair<std::string, std::string>, ResourceQualifierCount> qualifiers;
    std::string error;
    bool malformed = false;
};

const char* ValueTypeName(uint8_t type) {
    switch (type) {
        case 0x00: return "null";
        case 0x01: return "reference";
        case 0x02: return "attribute";
        case 0x03: return "string";
        case 0x04: return "float";
        case 0x05: return "dimension";
        case 0x06: return "fraction";
        case 0x07: return "dynamic_reference";
        case 0x08: return "dynamic_attribute";
        case 0x10: return "int_dec";
        case 0x11: return "int_hex";
        case 0x12: return "int_boolean";
        case 0x1c: return "int_color_argb8";
        case 0x1d: return "int_color_rgb8";
        case 0x1e: return "int_color_argb4";
        case 0x1f: return "int_color_rgb4";
        default: return nullptr;
    }
}

bool ValueTypeSupported(uint8_t type) {
    return type == kTypeString || type == kTypeReference ||
           (type >= kTypeColorArgb8 && type <= kTypeColorRgb4);
}

std::string HexByte(uint8_t value) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result = "unknown_0x00";
    result[result.size() - 2] = kHex[(value >> 4) & 0xf];
    result[result.size() - 1] = kHex[value & 0xf];
    return result;
}

void AddValueType(ParserAnalysis* state, uint8_t type) {
    if (state == nullptr) return;
    ++state->valueTypeCounts[type];
}

std::string Decimal(uint32_t value) { return std::to_string(value); }

void AddQualifier(ParserAnalysis* state, const std::string& name, const std::string& value,
                  bool supported) {
    if (state == nullptr || value.empty()) return;
    const auto key = std::make_pair(name, value);
    auto [it, inserted] = state->qualifiers.emplace(
            key, ResourceQualifierCount{name, value, 0, supported});
    ++it->second.count;
    (void)inserted;
}

void MarkMalformed(ParserAnalysis* state, const char* reason) {
    if (state == nullptr) return;
    ++state->result->malformed_entries;
    state->malformed = true;
    if (state->error.empty()) state->error = reason;
}

bool ConfigHas(size_t size, size_t offset, size_t width) {
    return offset <= size && width <= size - offset;
}

uint16_t ConfigU16(const uint8_t* config, size_t size, size_t offset) {
    return ConfigHas(size, offset, 2) ? U16(config + offset) : 0;
}

std::string ConfigAscii(const uint8_t* config, size_t size, size_t offset, size_t width) {
    std::string value;
    if (!ConfigHas(size, offset, width)) return value;
    for (size_t i = 0; i < width && config[offset + i] != 0; ++i)
        value.push_back(static_cast<char>(config[offset + i]));
    return value;
}

void AnalyzeConfig(const uint8_t* config, size_t size, ParserAnalysis* state) {
    if (state == nullptr) return;
    // Offsets follow android::ResTable_config in AOSP ResourceTypes.h. The size
    // passed here is the encoded struct size, so every field remains version-bounded.
    auto add16 = [&](const char* name, size_t offset) {
        const uint16_t value = ConfigU16(config, size, offset);
        if (value != 0) AddQualifier(state, name, Decimal(value), false);
    };
    auto add8 = [&](const char* name, size_t offset) {
        if (ConfigHas(size, offset, 1) && config[offset] != 0)
            AddQualifier(state, name, Decimal(config[offset]), false);
    };

    add16("mcc", 4);
    add16("mnc", 6);
    if (ConfigHas(size, 8, 2) && (config[8] != 0 || config[9] != 0)) {
        std::string language;
        language.push_back(DecodeLocaleByte(config[8], config[9], false, 0));
        language.push_back(DecodeLocaleByte(config[8], config[9], false, 1));
        AddQualifier(state, "locale.language", language, true);
    }
    if (ConfigHas(size, 10, 2) && (config[10] != 0 || config[11] != 0)) {
        std::string region;
        region.push_back(DecodeLocaleByte(config[10], config[11], true, 0));
        region.push_back(DecodeLocaleByte(config[10], config[11], true, 1));
        AddQualifier(state, "locale.region", region, true);
    }

    add8("orientation", 12);
    add8("touchscreen", 13);
    add16("density", 14);
    add8("keyboard", 16);
    add8("navigation", 17);
    if (ConfigHas(size, 18, 1)) {
        const uint8_t flags = config[18];
        if ((flags & 0x03) != 0) AddQualifier(state, "input.keyboardHidden", Decimal(flags & 0x03), false);
        if ((flags & 0x0c) != 0) AddQualifier(state, "input.navigationHidden", Decimal((flags & 0x0c) >> 2), false);
        if ((flags & 0xf0) != 0) AddQualifier(state, "input.unknownFlags", Decimal(flags & 0xf0), false);
    }
    add8("grammaticalGender", 19);
    add16("screenWidth", 20);
    add16("screenHeight", 22);
    add16("sdkVersion", 24);
    add16("minorVersion", 26);

    if (ConfigHas(size, 28, 1)) {
        const uint8_t layout = config[28];
        if ((layout & 0x0f) != 0) AddQualifier(state, "screenLayout.size", Decimal(layout & 0x0f), false);
        if ((layout & 0x30) != 0) AddQualifier(state, "screenLayout.long", Decimal((layout & 0x30) >> 4), false);
        if ((layout & 0xc0) != 0)
            AddQualifier(state, "screenLayout.layoutDirection", Decimal((layout & 0xc0) >> 6), false);
    }
    if (ConfigHas(size, 29, 1)) {
        const uint8_t mode = config[29];
        if ((mode & 0x0f) != 0) AddQualifier(state, "uiMode.type", Decimal(mode & 0x0f), false);
        if ((mode & 0x30) != 0) AddQualifier(state, "uiMode.night", Decimal((mode & 0x30) >> 4), false);
        if ((mode & 0xc0) != 0) AddQualifier(state, "uiMode.unknownBits", Decimal(mode & 0xc0), false);
    }
    add16("smallestScreenWidthDp", 30);
    add16("screenWidthDp", 32);
    add16("screenHeightDp", 34);

    const std::string script = ConfigAscii(config, size, 36, 4);
    if (!script.empty()) AddQualifier(state, "locale.script", script, false);
    const std::string variant = ConfigAscii(config, size, 40, 8);
    if (!variant.empty()) AddQualifier(state, "locale.variant", variant, false);
    if (ConfigHas(size, 48, 1)) {
        const uint8_t round = config[48] & 0x03;
        if (round != 0) AddQualifier(state, "screenLayout2.round", Decimal(round), false);
        if ((config[48] & 0xfc) != 0)
            AddQualifier(state, "screenLayout2.unknownBits", Decimal(config[48] & 0xfc), false);
    }
    if (ConfigHas(size, 49, 1)) {
        const uint8_t color_mode = config[49];
        if ((color_mode & 0x03) != 0)
            AddQualifier(state, "colorMode.wideColorGamut", Decimal(color_mode & 0x03), false);
        if ((color_mode & 0x0c) != 0)
            AddQualifier(state, "colorMode.hdr", Decimal((color_mode & 0x0c) >> 2), false);
        if ((color_mode & 0xf0) != 0)
            AddQualifier(state, "colorMode.unknownBits", Decimal(color_mode & 0xf0), false);
    }
    for (size_t offset = 50; offset < size && offset < 52; ++offset) {
        if (config[offset] != 0)
            AddQualifier(state, "unknownConfig.byte" + std::to_string(offset), Decimal(config[offset]), false);
    }
    const bool current_locale_config = size >= 61;
    if (ConfigHas(size, 52, 1) && config[52] != 0) {
        AddQualifier(state, current_locale_config ? "locale.scriptWasComputed" :
                                                    "locale.scriptWasProvided",
                     Decimal(config[52]), false);
    }
    if (current_locale_config) {
        const std::string numbering_system = ConfigAscii(config, size, 53, 8);
        if (!numbering_system.empty())
            AddQualifier(state, "locale.numberingSystem", numbering_system, false);
    }
    const size_t first_unknown = current_locale_config ? 61 : 53;
    for (size_t offset = first_unknown; offset < size; ++offset) {
        if (config[offset] != 0)
            AddQualifier(state, "unknownConfig.byte" + std::to_string(offset), Decimal(config[offset]), false);
    }
}

void FinalizeAnalysis(ParserAnalysis* state) {
    if (state == nullptr) return;
    state->result->packages.assign(state->packages.begin(), state->packages.end());
    state->result->resource_ids = state->resourceIds.size();
    state->result->value_types.clear();
    for (const auto& [type, count] : state->valueTypeCounts) {
        const char* known = ValueTypeName(type);
        state->result->value_types.push_back({type, known != nullptr ? known : HexByte(type),
                                               count, ValueTypeSupported(type)});
    }
    state->result->qualifiers.clear();
    for (const auto& [key, qualifier] : state->qualifiers) {
        (void)key;
        state->result->qualifiers.push_back(qualifier);
    }
}

struct ResourceBag {
    uint32_t parent = 0;
    bool selectable = true;
    std::string locale;
    uint16_t sdk = 0, smallest_width = 0, width = 0, height = 0;
    uint8_t ui_mode = 0, orientation = 0;
    std::map<uint32_t, ResourceScalar> attributes;
};

struct ResourceTable {
    std::vector<std::string> globalStrings;
    std::unordered_map<uint32_t, std::vector<ResourceValue>> values;
    std::unordered_map<std::string, uint32_t> identifiers;
    std::string defaultPackage;
    std::unordered_map<uint32_t, std::vector<ResourceBag>> bags;
    // id -> "package:type/entry", built from `identifiers` on first use.
    mutable std::once_flag reverseOnce;
    mutable std::unordered_map<uint32_t, std::string> reverseNames;
    bool parse(const std::vector<uint8_t>& bytes, ParserAnalysis* analysis = nullptr);
    const ResourceValue* select(uint32_t id, const std::string& locale) const;
    bool getString(uint32_t id, const std::string& locale, std::string* out, unsigned depth = 0) const;
    bool getColor(uint32_t id, const std::string& locale, uint32_t* out, unsigned depth = 0) const;
};

bool ParseTypeChunk(const std::vector<uint8_t>& bytes, size_t base, size_t packageEnd,
                    uint32_t packageId, uint32_t typeIdOffset,
                    const std::vector<std::string>& typeNames,
                    const std::vector<std::string>& keyNames,
                    const std::vector<std::string>& globalStrings,
                    const std::string& packageName, ResourceTable* table,
                    ParserAnalysis* analysis) {
    auto fail = [analysis](const char* reason) {
        MarkMalformed(analysis, reason);
        return false;
    };
    if (!Range(base, 24, packageEnd) || U16(bytes.data() + base) != kResTableTypeType)
        return fail("invalid resource type chunk header");
    const uint16_t headerSize = U16(bytes.data() + base + 2);
    const uint32_t chunkSize = U32(bytes.data() + base + 4);
    if (headerSize < 24 || chunkSize < headerSize || !Range(base, chunkSize, packageEnd))
        return fail("resource type chunk is out of bounds");
    const size_t chunkEnd = base + chunkSize;
    const uint8_t rawTypeId = bytes[base + 8];
    const uint8_t flags = bytes[base + 9];
    const uint32_t entryCount = U32(bytes.data() + base + 12);
    const uint32_t entriesStart = U32(bytes.data() + base + 16);
    const uint32_t configSize = U32(bytes.data() + base + 20);
    if (entryCount > 1'000'000 ||
        (analysis != nullptr && configSize < sizeof(uint32_t)) ||
        configSize > static_cast<uint32_t>(headerSize - 20) || entriesStart < headerSize ||
        entriesStart > chunkSize) return fail("invalid resource type chunk offsets or counts");
    const uint64_t typeIdWide = static_cast<uint64_t>(rawTypeId) + typeIdOffset;
    if (rawTypeId == 0 || typeIdWide > 255) return fail("invalid resource type identifier");
    const size_t entryTableStart = base + headerSize;
    const size_t entryTableBytes = static_cast<size_t>(entryCount) * 4;
    if (!Range(entryTableStart, entryTableBytes, base + entriesStart))
        return fail("resource entry offset table is out of bounds");
    const uint32_t typeNameIndex = rawTypeId - 1;
    if (typeNameIndex >= typeNames.size()) return fail("resource type name index is invalid");
    const std::string locale = ReadLocale(bytes.data() + base + 20, configSize);
    AnalyzeConfig(bytes.data() + base + 20, configSize, analysis);

    for (uint32_t i = 0; i < entryCount; ++i) {
        uint32_t entryIndex = i;
        uint32_t offset = kNoEntry;
        if ((flags & kTypeFlagSparse) != 0) {
            const uint8_t* pair = bytes.data() + entryTableStart + i * 4u;
            entryIndex = U16(pair);
            offset = static_cast<uint32_t>(U16(pair + 2)) * 4u;
        } else {
            offset = U32(bytes.data() + entryTableStart + i * 4u);
            if (offset == kNoEntry) continue;
        }
        if (entryIndex > 0xffff || (offset & 3u) != 0 ||
            offset > chunkSize - entriesStart ||
            !Range(base + entriesStart + offset, 8, chunkEnd)) {
            if (analysis != nullptr) MarkMalformed(analysis, "resource entry offset is invalid");
            continue;
        }
        const size_t entry = base + entriesStart + offset;
        const uint16_t entrySize = U16(bytes.data() + entry);
        const uint16_t entryFlags = U16(bytes.data() + entry + 2);
        const uint32_t keyIndex = U32(bytes.data() + entry + 4);
        if (entrySize < 8 || !Range(entry, entrySize, chunkEnd) || keyIndex >= keyNames.size()) {
            if (analysis != nullptr) MarkMalformed(analysis, "resource entry header is invalid");
            continue;
        }
        const uint32_t typeId = static_cast<uint32_t>(typeIdWide);
        const uint32_t id = ((packageId & 0xffu) << 24) | (typeId << 16) | entryIndex;
        if (analysis != nullptr && analysis->resourceIds.insert(id).second) {
            ++analysis->result->resource_type_counts[typeNames[typeNameIndex]];
        }

        if ((entryFlags & kEntryFlagComplex) != 0) {
            if (entrySize < 16) {
                MarkMalformed(analysis, "complex resource entry header is truncated");
                continue;
            }
            const uint32_t mapCount = U32(bytes.data() + entry + 12);
            const size_t mapStart = entry + entrySize;
            if (mapCount > 1'000'000 || mapCount > (chunkEnd - mapStart) / 12u) {
                MarkMalformed(analysis, "complex resource map array is out of bounds");
                continue;
            }
            if (analysis != nullptr) {
                ++analysis->result->complex_entries;
                ++analysis->result->skipped_entries;
            }
            ResourceBag bag;
            bag.parent = U32(bytes.data() + entry + 8);
            bag.locale = locale;
            const uint8_t* config = bytes.data() + base + 20;
            for (size_t at = 4; at < configSize; ++at) {
                const bool supported = (at >= 8 && at <= 12) || (at >= 24 && at <= 25)
                        || (at >= 29 && at <= 35);
                if (!supported && config[at] != 0) bag.selectable = false;
            }
            if (ConfigHas(configSize, 29, 1) && (config[29] & 0xc0) != 0) bag.selectable = false;
            bag.sdk = ConfigU16(config, configSize, 24);
            bag.smallest_width = ConfigU16(config, configSize, 30);
            bag.width = ConfigU16(config, configSize, 32);
            bag.height = ConfigU16(config, configSize, 34);
            if (ConfigHas(configSize, 29, 1)) bag.ui_mode = config[29];
            if (ConfigHas(configSize, 12, 1)) bag.orientation = config[12];
            bool valid = true;
            for (uint32_t mapIndex = 0; mapIndex < mapCount; ++mapIndex) {
                const size_t mapOffset = mapStart + static_cast<size_t>(mapIndex) * 12u;
                const uint8_t* value = bytes.data() + mapOffset + 4u;
                if (U16(value) != 8) {
                    MarkMalformed(analysis, "complex resource map value is truncated");
                    valid = false;
                    continue;
                }
                AddValueType(analysis, value[3]);
                if (analysis != nullptr) ++analysis->result->complex_map_values;
                ResourceScalar scalar;
                scalar.type = value[3];
                scalar.data = U32(value + 4);
                if (scalar.type == kTypeString) {
                    if (scalar.data >= globalStrings.size()) {
                        MarkMalformed(analysis, "complex resource string value index is out of range");
                        valid = false;
                        continue;
                    }
                    scalar.text = globalStrings[scalar.data];
                }
                bag.attributes[U32(bytes.data() + mapOffset)] = std::move(scalar);
            }
            if (valid && typeNames[typeNameIndex] == "style") {
                table->bags[id].push_back(std::move(bag));
                const std::string suffix = ":" + typeNames[typeNameIndex] + "/" + keyNames[keyIndex];
                table->identifiers[packageName + suffix] = id;
                table->identifiers["*" + suffix] = id;
            }
            continue;
        }
        if (!Range(entry + entrySize, 8, chunkEnd)) {
            if (analysis != nullptr) MarkMalformed(analysis, "simple resource value is out of bounds");
            continue;
        }
        const uint8_t* value = bytes.data() + entry + entrySize;
        const uint16_t valueSize = U16(value);
        if (valueSize < 8 || !Range(entry + entrySize, valueSize, chunkEnd)) {
            if (analysis != nullptr) MarkMalformed(analysis, "simple resource value has an invalid size");
            continue;
        }
        const uint8_t dataType = value[3];
        const uint32_t data = U32(value + 4);
        if (analysis != nullptr) {
            ++analysis->result->simple_entries;
            AddValueType(analysis, dataType);
        }
        if (dataType == kTypeString && data >= globalStrings.size()) {
            if (analysis != nullptr) {
                ++analysis->result->skipped_entries;
                MarkMalformed(analysis, "resource string value index is out of range");
            }
            continue;
        }
        table->values[id].push_back(ResourceValue{dataType, data, locale});
        const std::string suffix = ":" + typeNames[typeNameIndex] + "/" + keyNames[keyIndex];
        table->identifiers[packageName + suffix] = id;
        table->identifiers["*" + suffix] = id;
    }
    return true;
}

bool ResourceTable::parse(const std::vector<uint8_t>& bytes, ParserAnalysis* analysis) {
    if (bytes.size() < 12 || U16(bytes.data()) != kResTableType) {
        MarkMalformed(analysis, "resource table header is invalid");
        return false;
    }
    const uint16_t headerSize = U16(bytes.data() + 2);
    const uint32_t tableSize = U32(bytes.data() + 4);
    if (headerSize < 12 || tableSize > bytes.size() || tableSize < headerSize) {
        MarkMalformed(analysis, "resource table size is invalid");
        return false;
    }
    const size_t end = tableSize;
    const uint32_t declaredPackageCount = U32(bytes.data() + 8);
    uint32_t actualPackageCount = 0;
    size_t cursor = headerSize;
    while (cursor < end) {
        if (!Range(cursor, 8, end)) {
            MarkMalformed(analysis, "resource table child header is truncated");
            return false;
        }
        const uint16_t type = U16(bytes.data() + cursor);
        const uint16_t childHeader = U16(bytes.data() + cursor + 2);
        const uint32_t childSize = U32(bytes.data() + cursor + 4);
        if (childHeader < 8 || childSize < childHeader || !Range(cursor, childSize, end)) {
            MarkMalformed(analysis, "resource table child chunk is out of bounds");
            return false;
        }
        if (type == kResStringPoolType) {
            if (!ParseStringPool(bytes, cursor, end, &globalStrings)) {
                MarkMalformed(analysis, "resource table string pool is malformed");
                return false;
            }
        } else if (type == kResTablePackageType) {
            ++actualPackageCount;
            if (childHeader < 284) {
                MarkMalformed(analysis, "resource package header is truncated");
                return false;
            }
            const size_t packageEnd = cursor + childSize;
            const uint32_t packageId = U32(bytes.data() + cursor + 8);
            const std::string packageName = ReadPackageName(bytes.data() + cursor + 12);
            if (analysis != nullptr) analysis->packages.insert(packageName);
            const uint32_t typeOffset = U32(bytes.data() + cursor + 268);
            const uint32_t keyOffset = U32(bytes.data() + cursor + 276);
            const uint32_t typeIdOffset = childHeader >= 288 ? U32(bytes.data() + cursor + 284) : 0;
            if (defaultPackage.empty()) defaultPackage = packageName;
            if (!Range(cursor + typeOffset, 8, packageEnd) ||
                !Range(cursor + keyOffset, 8, packageEnd)) {
                MarkMalformed(analysis, "resource package string-pool offset is invalid");
                return false;
            }
            std::vector<std::string> typeNames, keyNames;
            if (!ParseStringPool(bytes, cursor + typeOffset, packageEnd, &typeNames) ||
                !ParseStringPool(bytes, cursor + keyOffset, packageEnd, &keyNames)) {
                MarkMalformed(analysis, "resource package string pool is malformed");
                return false;
            }
            size_t child = cursor + childHeader;
            while (child < packageEnd) {
                if (!Range(child, 8, packageEnd)) {
                    MarkMalformed(analysis, "resource package child header is truncated");
                    return false;
                }
                const uint16_t subType = U16(bytes.data() + child);
                const uint16_t subHeader = U16(bytes.data() + child + 2);
                const uint32_t subSize = U32(bytes.data() + child + 4);
                if (subHeader < 8 || subSize < subHeader || !Range(child, subSize, packageEnd)) {
                    MarkMalformed(analysis, "resource package child chunk is out of bounds");
                    return false;
                }
                if (subType == kResTableTypeType &&
                    !ParseTypeChunk(bytes, child, packageEnd, packageId, typeIdOffset,
                                    typeNames, keyNames, globalStrings, packageName, this, analysis)) return false;
                child += subSize;
            }
            if (child != packageEnd) {
                MarkMalformed(analysis, "resource package child chunks are misaligned");
                return false;
            }
        }
        cursor += childSize;
    }
    if (analysis != nullptr) {
        if (actualPackageCount != declaredPackageCount)
            MarkMalformed(analysis, "resource table package count does not match its chunks");
        return cursor == end && !analysis->malformed;
    }
    return cursor == end && !globalStrings.empty() && !values.empty();
}

const ResourceValue* ResourceTable::select(uint32_t id, const std::string& requested) const {
    const auto found = values.find(id);
    if (found == values.end() || found->second.empty()) return nullptr;
    const std::string wanted = NormalizeLocale(requested);
    const ResourceValue* exact = nullptr;
    const ResourceValue* language = nullptr;
    const ResourceValue* fallback = nullptr;
    const ResourceValue* first = nullptr;
    for (const ResourceValue& value : found->second) {
        if (first == nullptr) first = &value;
        const std::string candidate = NormalizeLocale(value.locale);
        if (candidate.empty()) fallback = &value;
        if (!wanted.empty() && candidate == wanted) exact = &value;
        if (!wanted.empty() && candidate.size() >= 2 && candidate.compare(0, 2, wanted, 0, 2) == 0 &&
            language == nullptr) language = &value;
    }
    if (exact != nullptr) return exact;
    if (language != nullptr) return language;
    return fallback != nullptr ? fallback : first;
}

bool ResourceTable::getString(uint32_t id, const std::string& locale, std::string* out,
                              unsigned depth) const {
    if (out == nullptr || depth >= 32) return false;
    const ResourceValue* value = select(id, locale);
    if (value == nullptr) return false;
    if (value->type == kTypeReference) return getString(value->data, locale, out, depth + 1);
    if (value->type != kTypeString || value->data >= globalStrings.size()) return false;
    *out = globalStrings[value->data];
    return true;
}
bool ResourceTable::getColor(uint32_t id, const std::string& locale, uint32_t* out,
                             unsigned depth) const {
    if (out == nullptr || depth >= 32) return false;
    const ResourceValue* value = select(id, locale);
    if (value == nullptr) return false;
    if (value->type == kTypeReference) return getColor(value->data, locale, out, depth + 1);
    if (value->type < kTypeColorArgb8 || value->type > kTypeColorRgb4) return false;
    if (value->type == kTypeColorArgb8) {
        *out = value->data;
    } else if (value->type == kTypeColorRgb8) {
        *out = value->data | 0xff000000u;
    } else if (value->type == kTypeColorArgb4) {
        const uint32_t a = (value->data >> 12) & 0xf;
        const uint32_t r = (value->data >> 8) & 0xf;
        const uint32_t g = (value->data >> 4) & 0xf;
        const uint32_t b = value->data & 0xf;
        *out = ((a * 17) << 24) | ((r * 17) << 16) | ((g * 17) << 8) | (b * 17);
    } else {
        const uint32_t r = (value->data >> 8) & 0xf;
        const uint32_t g = (value->data >> 4) & 0xf;
        const uint32_t b = value->data & 0xf;
        *out = 0xff000000u | ((r * 17) << 16) | ((g * 17) << 8) | (b * 17);
    }
    return true;
}

std::mutex g_tableMutex;
std::string g_loadedAssetsDir;
std::shared_ptr<ResourceTable> g_table;
bool g_loadAttempted = false;

std::shared_ptr<ResourceTable> LoadTable() {
    const std::string assetsDir = kudroid_get_assets_dir_cpp();
    std::lock_guard<std::mutex> lock(g_tableMutex);
    if (g_loadedAssetsDir != assetsDir) {
        g_loadedAssetsDir = assetsDir;
        g_table.reset();
        g_loadAttempted = false;
    }
    if (g_loadAttempted) return g_table;
    g_loadAttempted = true;
    char* path = nullptr;
    int64_t offset = 0, length = 0;
    if (kudroid_package_resolve_bytes("resources.arsc", &path, &offset, &length) <= 0 ||
        path == nullptr || length <= 0 || static_cast<uint64_t>(length) > kMaxResourceTableBytes) {
        std::free(path);
        return nullptr;
    }
    std::ifstream in(path, std::ios::binary);
    std::free(path);
    if (!in) return nullptr;
    in.seekg(offset, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in || static_cast<size_t>(in.gcount()) != bytes.size()) return nullptr;
    auto table = std::make_shared<ResourceTable>();
    if (!table->parse(bytes)) return nullptr;
    g_table = std::move(table);
    return g_table;
}

}  // namespace

bool resource_analyze_bytes(const uint8_t* bytes, size_t size,
                            ResourceTableAnalysis* analysis, std::string* error) {
    if (error != nullptr) error->clear();
    if (analysis == nullptr) {
        if (error != nullptr) *error = "analysis output is null";
        return false;
    }
    *analysis = ResourceTableAnalysis{};
    if (bytes == nullptr || size == 0) {
        if (error != nullptr) *error = "resource table input is empty";
        return false;
    }
    if (size > kMaxResourceTableBytes) {
        if (error != nullptr) *error = "resource table exceeds the 64 MiB scan limit";
        return false;
    }

    std::vector<uint8_t> owned(bytes, bytes + size);
    ResourceTable table;
    ParserAnalysis state;
    state.result = analysis;
    const bool parsed = table.parse(owned, &state);
    FinalizeAnalysis(&state);
    if (!parsed) {
        if (error != nullptr) {
            *error = state.error.empty() ? "resource table is malformed" : state.error;
        }
        return false;
    }
    return true;
}

bool resource_get_pool_string(uint32_t index, std::string* value) {
    const auto table = LoadTable();
    if (!table || value == nullptr || index >= table->globalStrings.size()) return false;
    *value = table->globalStrings[index];
    return true;
}

// A reference may cross tables (an app color aliasing @android:color/white), so each hop
// picks the table that owns the id. Framework ids need no application table at all.
bool resource_get_value(uint32_t id, const std::string& locale, bool resolve,
                        ResourceScalar* value) {
    if (value == nullptr) return false;
    const auto table = LoadTable();
    std::set<uint32_t> seen;
    while (seen.insert(id).second && seen.size() <= 64) {
        uint8_t type = 0;
        uint32_t data = 0;
        std::string text;
        if (framework_resource_is_id(id)) {
            if (framework_resource_string(id, &text)) type = kTypeString;
            else if (!framework_resource_scalar(id, &type, &data)) return false;
        } else {
            if (!table) return false;
            const ResourceValue* scalar = table->select(id, locale);
            if (scalar == nullptr) return false;
            type = scalar->type;
            data = scalar->data;
            if (type == kTypeString && data < table->globalStrings.size())
                text = table->globalStrings[data];
        }
        if (resolve && type == kTypeReference && data != 0) {
            id = data;
            continue;
        }
        *value = ResourceScalar{};
        value->type = type;
        value->data = data;
        value->resource_id = id;
        value->text = std::move(text);
        return true;
    }
    return false;
}

bool resource_get_style(uint32_t id, const ResourceStyleConfig& config,
                        std::map<uint32_t, ResourceScalar>* attributes) {
    const auto table = LoadTable();
    if (!table || attributes == nullptr) return false;
    std::vector<const ResourceBag*> chain;
    std::set<uint32_t> seen;
    const std::string wanted = NormalizeLocale(config.locale);
    while (id != 0) {
        if (!seen.insert(id).second || seen.size() > 64) return false;
        const auto found = table->bags.find(id);
        if (found == table->bags.end()) {
            const ResourceValue* alias = table->select(id, config.locale);
            if (alias != nullptr && alias->type == kTypeReference) { id = alias->data; continue; }
            // Framework parents can be absent from an application resource table.
            if (!chain.empty() && (id >> 24) == 1) break;
            return false;
        }
        const ResourceBag* best = nullptr;
        std::vector<int> best_rank;
        for (const ResourceBag& bag : found->second) {
            if (!bag.selectable) continue;
            const std::string locale = NormalizeLocale(bag.locale);
            int language = 0;
            if (!locale.empty()) {
                if (locale == wanted) language = 2;
                else if (locale.size() == 2 && wanted.size() >= 2 && wanted.compare(0, 2, locale) == 0) language = 1;
                else continue;
            }
            if (bag.sdk > config.sdk || bag.smallest_width > std::min(config.width_dp, config.height_dp)
                    || bag.width > config.width_dp || bag.height > config.height_dp
                    || (bag.orientation != 0 && bag.orientation != config.orientation)
                    || ((bag.ui_mode & 0x0f) != 0 && (bag.ui_mode & 0x0f) != (config.ui_mode & 0x0f))
                    || ((bag.ui_mode & 0x30) != 0 && (bag.ui_mode & 0x30) != (config.ui_mode & 0x30))) continue;
            std::vector<int> rank{language, bag.smallest_width, bag.width, bag.height,
                                  bag.orientation != 0, (bag.ui_mode & 0x0f) != 0,
                                  (bag.ui_mode & 0x30) != 0, bag.sdk};
            if (best == nullptr || rank > best_rank) { best = &bag; best_rank = std::move(rank); }
        }
        if (best == nullptr) return false;
        chain.push_back(best);
        id = best->parent;
    }
    if (chain.empty()) return false;
    attributes->clear();
    for (auto bag = chain.rbegin(); bag != chain.rend(); ++bag) {
        for (const auto& attribute : (*bag)->attributes) (*attributes)[attribute.first] = attribute.second;
    }
    return true;
}

// Resources.getIdentifier: the name is "[package:][type/]entry"; defType and defPackage
// fill in whatever the name leaves out. "android" selects the framework table.
bool resource_get_identifier(const std::string& name, const std::string& type,
                             const std::string& package, uint32_t* id) {
    if (id == nullptr || name.empty()) return false;
    std::string entry = name, wantedType = type, wantedPackage = package;
    const size_t slash = entry.find('/');
    const size_t colon = entry.find(':');
    if (slash != std::string::npos) {
        std::string head = entry.substr(0, slash);
        entry = entry.substr(slash + 1);
        const size_t headColon = head.find(':');
        if (headColon != std::string::npos) {
            wantedPackage = head.substr(0, headColon);
            wantedType = head.substr(headColon + 1);
        } else {
            wantedType = head;
        }
    } else if (colon != std::string::npos) {
        wantedPackage = entry.substr(0, colon);
        entry = entry.substr(colon + 1);
    }
    if (entry.empty() || wantedType.empty()) return false;
    if (wantedPackage == "android") return framework_resource_find(wantedType, entry, id);
    const auto table = LoadTable();
    if (!table) return false;
    const std::string packageName = wantedPackage.empty() ? table->defaultPackage : wantedPackage;
    const auto found = table->identifiers.find(packageName + ":" + wantedType + "/" + entry);
    if (found == table->identifiers.end()) return false;
    *id = found->second;
    return true;
}

bool resource_get_name(uint32_t id, std::string* package, std::string* type,
                       std::string* entry) {
    std::string typeName, entryName;
    if (framework_resource_is_id(id)) {
        if (!framework_resource_name(id, &typeName, &entryName)) return false;
        if (package != nullptr) *package = "android";
    } else {
        const auto table = LoadTable();
        if (!table) return false;
        std::call_once(table->reverseOnce, [&table] {
            for (const auto& named : table->identifiers) {
                // "*:type/name" is the package-less alias of the entry; skip it.
                if (named.first.compare(0, 2, "*:") == 0) continue;
                table->reverseNames.emplace(named.second, named.first);
            }
        });
        const auto found = table->reverseNames.find(id);
        if (found == table->reverseNames.end()) return false;
        const std::string& full = found->second;  // package:type/entry
        const size_t colon = full.find(':');
        const size_t slash = full.find('/');
        if (colon == std::string::npos || slash == std::string::npos || slash < colon) return false;
        if (package != nullptr) *package = full.substr(0, colon);
        typeName = full.substr(colon + 1, slash - colon - 1);
        entryName = full.substr(slash + 1);
    }
    if (type != nullptr) *type = typeName;
    if (entry != nullptr) *entry = entryName;
    return true;
}

bool resource_get_string(uint32_t id, const std::string& locale, std::string* value) {
    ResourceScalar scalar;
    if (value == nullptr || !resource_get_value(id, locale, true, &scalar) ||
        scalar.type != kTypeString) return false;
    *value = scalar.text;
    return true;
}

bool resource_get_color(uint32_t id, const std::string& locale, uint32_t* value) {
    ResourceScalar scalar;
    if (value == nullptr || !resource_get_value(id, locale, true, &scalar)) return false;
    if (scalar.type == kTypeColorArgb8) {
        *value = scalar.data;
    } else if (scalar.type == kTypeColorRgb8) {
        *value = scalar.data | 0xff000000u;
    } else if (scalar.type == kTypeColorArgb4) {
        const uint32_t a = (scalar.data >> 12) & 0xf, r = (scalar.data >> 8) & 0xf;
        const uint32_t g = (scalar.data >> 4) & 0xf, b = scalar.data & 0xf;
        *value = ((a * 17) << 24) | ((r * 17) << 16) | ((g * 17) << 8) | (b * 17);
    } else if (scalar.type == kTypeColorRgb4) {
        const uint32_t r = (scalar.data >> 8) & 0xf, g = (scalar.data >> 4) & 0xf;
        const uint32_t b = scalar.data & 0xf;
        *value = 0xff000000u | ((r * 17) << 16) | ((g * 17) << 8) | (b * 17);
    } else {
        return false;
    }
    return true;
}
void reset_resource_table_cache() {
    std::lock_guard<std::mutex> lock(g_tableMutex);
    g_loadedAssetsDir.clear();
    g_table.reset();
    g_loadAttempted = false;
}

}  // namespace kudroid
