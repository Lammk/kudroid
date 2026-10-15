#include "kudroid/ResourceTable.h"
#include "kudroid/ResourceCompat.h"
#include "kudroid/platform/AssetShim.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <zlib.h>

#include <unistd.h>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* message) {
    ++g_checks;
    std::printf("%s %s\n", condition ? "  OK  " : "  FAIL", message);
    if (!condition) ++g_failures;
}

void U8(std::vector<uint8_t>* out, uint8_t value) { out->push_back(value); }
void U16(std::vector<uint8_t>* out, uint16_t value) {
    U8(out, static_cast<uint8_t>(value));
    U8(out, static_cast<uint8_t>(value >> 8));
}
void U32(std::vector<uint8_t>* out, uint32_t value) {
    U16(out, static_cast<uint16_t>(value));
    U16(out, static_cast<uint16_t>(value >> 16));
}
void Patch16(std::vector<uint8_t>* out, size_t at, uint16_t value) {
    (*out)[at] = static_cast<uint8_t>(value);
    (*out)[at + 1] = static_cast<uint8_t>(value >> 8);
}
void Patch32(std::vector<uint8_t>* out, size_t at, uint32_t value) {
    Patch16(out, at, static_cast<uint16_t>(value));
    Patch16(out, at + 2, static_cast<uint16_t>(value >> 16));
}

std::vector<uint8_t> StringPool(const std::vector<std::string>& strings) {
    std::vector<uint8_t> out;
    U16(&out, 0x0001);
    U16(&out, 28);
    U32(&out, 0);
    U32(&out, static_cast<uint32_t>(strings.size()));
    U32(&out, 0);
    U32(&out, 0x100);
    U32(&out, 0);
    U32(&out, 0);
    std::vector<uint32_t> offsets;
    std::vector<uint8_t> data;
    for (const std::string& value : strings) {
        offsets.push_back(static_cast<uint32_t>(data.size()));
        U8(&data, static_cast<uint8_t>(value.size()));
        U8(&data, static_cast<uint8_t>(value.size()));
        data.insert(data.end(), value.begin(), value.end());
        U8(&data, 0);
    }
    Patch32(&out, 20, static_cast<uint32_t>(28 + strings.size() * 4));
    for (uint32_t offset : offsets) U32(&out, offset);
    out.insert(out.end(), data.begin(), data.end());
    while ((out.size() & 3u) != 0) U8(&out, 0);
    Patch32(&out, 4, static_cast<uint32_t>(out.size()));
    return out;
}

std::vector<uint8_t> Config(size_t size = 4) {
    std::vector<uint8_t> config(size, 0);
    Patch32(&config, 0, static_cast<uint32_t>(size));
    return config;
}

void Put16(std::vector<uint8_t>* config, size_t at, uint16_t value) {
    if (at + 2 <= config->size()) Patch16(config, at, value);
}
void Put8(std::vector<uint8_t>* config, size_t at, uint8_t value) {
    if (at < config->size()) (*config)[at] = value;
}

struct Entry {
    uint16_t index = 0;
    uint32_t key_index = 0;
    bool complex = false;
    uint8_t value_type = 0;
    uint32_t value_data = 0;
    std::vector<std::tuple<uint32_t, uint8_t, uint32_t>> map_values;
};

std::vector<uint8_t> TypeChunk(uint8_t type_id, const std::vector<Entry>& entries,
                               const std::vector<uint8_t>& config,
                               bool sparse = false) {
    const uint16_t header_size = static_cast<uint16_t>(20 + config.size());
    uint32_t table_count = static_cast<uint32_t>(entries.size());
    if (!sparse) {
        table_count = 0;
        for (const Entry& entry : entries) table_count = std::max<uint32_t>(table_count, entry.index + 1u);
    }
    const uint32_t table_bytes = table_count * (sparse ? 4u : 4u);
    const uint32_t entries_start = header_size + table_bytes;
    std::vector<uint32_t> entry_offsets;
    uint32_t current_offset = 0;
    for (const Entry& entry : entries) {
        entry_offsets.push_back(current_offset);
        current_offset += entry.complex
                ? static_cast<uint32_t>(16 + entry.map_values.size() * 12)
                : 16;
    }

    std::vector<uint8_t> out;
    U16(&out, 0x0201);
    U16(&out, header_size);
    U32(&out, 0);
    U8(&out, type_id);
    U8(&out, sparse ? 0x01 : 0x00);
    U16(&out, 0);
    U32(&out, table_count);
    U32(&out, entries_start);
    out.insert(out.end(), config.begin(), config.end());

    if (sparse) {
        for (size_t i = 0; i < entries.size(); ++i) {
            U16(&out, entries[i].index);
            U16(&out, static_cast<uint16_t>(entry_offsets[i] / 4));
        }
    } else {
        for (uint32_t index = 0; index < table_count; ++index) {
            auto found = std::find_if(entries.begin(), entries.end(), [index](const Entry& entry) {
                return entry.index == index;
            });
            if (found == entries.end()) {
                U32(&out, 0xffffffffu);
            } else {
                const size_t i = static_cast<size_t>(found - entries.begin());
                U32(&out, entry_offsets[i]);
            }
        }
    }

    for (const Entry& entry : entries) {
        U16(&out, entry.complex ? 16 : 8);
        U16(&out, entry.complex ? 1 : 0);
        U32(&out, entry.key_index);
        if (entry.complex) {
            U32(&out, 0);  // parent
            U32(&out, static_cast<uint32_t>(entry.map_values.size()));
            for (const auto& map : entry.map_values) {
                U32(&out, std::get<0>(map));
                U16(&out, 8);
                U8(&out, 0);
                U8(&out, std::get<1>(map));
                U32(&out, std::get<2>(map));
            }
        } else {
            U16(&out, 8);
            U8(&out, 0);
            U8(&out, entry.value_type);
            U32(&out, entry.value_data);
        }
    }
    Patch32(&out, 4, static_cast<uint32_t>(out.size()));
    return out;
}

std::vector<uint8_t> ResourceTable(const std::vector<std::vector<uint8_t>>& type_chunks) {
    const auto global_strings = StringPool({"Default", "English", "United States"});
    const auto types = StringPool({"string", "color", "dimen"});
    const auto keys = StringPool({
        "welcome", "welcome_alias", "integer", "boolean", "dimension", "fraction",
        "unknown", "bag", "qualified", "accent_argb8", "accent_rgb8", "accent_argb4",
        "accent_rgb4", "sparse_one", "sparse_four",
    });

    std::vector<uint8_t> package;
    U16(&package, 0x0200);
    U16(&package, 288);
    U32(&package, 0);
    U32(&package, 0x7f);
    const std::string package_name = "com.example.fixture";
    for (size_t i = 0; i < 128; ++i) U16(&package, i < package_name.size() ? package_name[i] : 0);
    const uint32_t type_offset = 288;
    const uint32_t key_offset = type_offset + static_cast<uint32_t>(types.size());
    U32(&package, type_offset);
    U32(&package, 0);
    U32(&package, key_offset);
    U32(&package, 0);
    U32(&package, 0);
    package.insert(package.end(), types.begin(), types.end());
    package.insert(package.end(), keys.begin(), keys.end());
    for (const auto& chunk : type_chunks) package.insert(package.end(), chunk.begin(), chunk.end());
    Patch32(&package, 4, static_cast<uint32_t>(package.size()));

    std::vector<uint8_t> table;
    U16(&table, 0x0002);
    U16(&table, 12);
    U32(&table, 0);
    U32(&table, 1);
    table.insert(table.end(), global_strings.begin(), global_strings.end());
    table.insert(table.end(), package.begin(), package.end());
    Patch32(&table, 4, static_cast<uint32_t>(table.size()));
    return table;
}

std::vector<uint8_t> ZipEntry(const std::string& name, const std::vector<uint8_t>& payload,
                              uint16_t method = 0) {
    const uint32_t crc = static_cast<uint32_t>(crc32(0L, payload.data(),
                                                     static_cast<uInt>(payload.size())));
    std::vector<uint8_t> compressed = payload;
    if (method == 8) {
        z_stream stream{};
        if (deflateInit2(&stream, Z_BEST_SPEED, Z_DEFLATED, -MAX_WBITS, 8,
                         Z_DEFAULT_STRATEGY) != Z_OK) return {};
        compressed.resize(static_cast<size_t>(deflateBound(&stream,
                                                           static_cast<uLong>(payload.size()))));
        stream.next_in = const_cast<Bytef*>(payload.data());
        stream.avail_in = static_cast<uInt>(payload.size());
        stream.next_out = compressed.data();
        stream.avail_out = static_cast<uInt>(compressed.size());
        const int status = deflate(&stream, Z_FINISH);
        const uLong compressed_length = stream.total_out;
        deflateEnd(&stream);
        if (status != Z_STREAM_END) return {};
        compressed.resize(static_cast<size_t>(compressed_length));
    }
    const uint32_t compressed_size = static_cast<uint32_t>(compressed.size());
    const uint32_t uncompressed_size = static_cast<uint32_t>(payload.size());
    std::vector<uint8_t> zip;
    U32(&zip, 0x04034b50); U16(&zip, 20); U16(&zip, 0); U16(&zip, method);
    U16(&zip, 0); U16(&zip, 0); U32(&zip, crc); U32(&zip, compressed_size); U32(&zip, uncompressed_size);
    U16(&zip, static_cast<uint16_t>(name.size())); U16(&zip, 0);
    zip.insert(zip.end(), name.begin(), name.end());
    zip.insert(zip.end(), compressed.begin(), compressed.end());
    const uint32_t central_offset = static_cast<uint32_t>(zip.size());
    U32(&zip, 0x02014b50); U16(&zip, 20); U16(&zip, 20); U16(&zip, 0); U16(&zip, method);
    U16(&zip, 0); U16(&zip, 0); U32(&zip, crc); U32(&zip, compressed_size); U32(&zip, uncompressed_size);
    U16(&zip, static_cast<uint16_t>(name.size())); U16(&zip, 0); U16(&zip, 0);
    U16(&zip, 0); U16(&zip, 0); U32(&zip, 0); U32(&zip, 0);
    zip.insert(zip.end(), name.begin(), name.end());
    const uint32_t central_size = static_cast<uint32_t>(zip.size()) - central_offset;
    U32(&zip, 0x06054b50); U16(&zip, 0); U16(&zip, 0); U16(&zip, 1); U16(&zip, 1);
    U32(&zip, central_size); U32(&zip, central_offset); U16(&zip, 0);
    return zip;
}

bool WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct Fixture {
    std::vector<uint8_t> valid;
    std::vector<uint8_t> malformed;
    std::vector<uint8_t> malformed_chunk;
};

Fixture BuildFixture() {
    std::vector<Entry> strings = {
        {0, 0, false, 0x03, 0, {}},
        {1, 1, false, 0x01, 0x7f010000, {}},
        {2, 2, false, 0x10, 42, {}},
        {3, 3, false, 0x12, 1, {}},
        {4, 4, false, 0x05, 0x00010001, {}},
        {5, 5, false, 0x06, 0x00010001, {}},
        {6, 6, false, 0x77, 0, {}},
        {7, 7, true, 0, 0, {{0x01010000, 0x03, 0}, {0x01010001, 0x05, 0x00010001}}},
        {8, 8, false, 0x03, 0, {}},
    };
    std::vector<std::vector<uint8_t>> chunks;
    chunks.push_back(TypeChunk(1, strings, Config()));

    std::vector<uint8_t> en = Config(12);
    Put8(&en, 8, 'e'); Put8(&en, 9, 'n');
    chunks.push_back(TypeChunk(1, {{0, 0, false, 0x03, 1, {}}}, en));
    std::vector<uint8_t> en_us = en;
    Put8(&en_us, 10, 'U'); Put8(&en_us, 11, 'S');
    chunks.push_back(TypeChunk(1, {{0, 0, false, 0x03, 2, {}}}, en_us));

    auto add_qualifier = [&chunks](size_t config_size, size_t offset, uint32_t value,
                                   size_t width, const char* locale = nullptr) {
        std::vector<uint8_t> config = Config(config_size);
        if (locale != nullptr) {
            Put8(&config, 8, static_cast<uint8_t>(locale[0]));
            Put8(&config, 9, static_cast<uint8_t>(locale[1]));
        }
        if (width == 1) Put8(&config, offset, static_cast<uint8_t>(value));
        else Put16(&config, offset, static_cast<uint16_t>(value));
        chunks.push_back(TypeChunk(1, {{8, 8, false, 0x03, 0, {}}}, config));
    };
    add_qualifier(8, 4, 310, 2);                     // mcc
    add_qualifier(8, 6, 260, 2);                     // mnc
    add_qualifier(16, 12, 1, 1);                     // orientation
    add_qualifier(14, 13, 3, 1);                     // touchscreen
    add_qualifier(16, 14, 320, 2);                   // density
    add_qualifier(17, 16, 2, 1);                     // keyboard
    add_qualifier(18, 17, 1, 1);                     // navigation
    add_qualifier(19, 18, 5, 1);                     // hidden keyboard/navigation flags
    add_qualifier(20, 19, 1, 1);                     // grammatical gender
    add_qualifier(24, 20, 1080, 2);                  // pixel width
    add_qualifier(24, 22, 1920, 2);                  // pixel height
    add_qualifier(28, 24, 35, 2);                    // SDK version
    add_qualifier(28, 26, 1, 2);                     // minor version
    add_qualifier(32, 28, 2, 1);                     // screen layout
    add_qualifier(30, 28, 0xa2, 1);                  // screen layout long and direction bits
    add_qualifier(30, 29, 3, 1);                     // UI mode type
    add_qualifier(32, 29, 0x20, 1);                  // UI night mode
    add_qualifier(36, 30, 600, 2);                   // smallest width dp
    add_qualifier(40, 32, 411, 2);                   // screen width dp
    add_qualifier(40, 34, 891, 2);                   // screen height dp
    add_qualifier(40, 36, 0, 4, "en");               // locale script bytes set below
    chunks.back()[20 + 36] = 'L';
    chunks.back()[20 + 37] = 'a';
    chunks.back()[20 + 38] = 't';
    chunks.back()[20 + 39] = 'n';
    add_qualifier(48, 40, 0, 8, "en");               // locale variant bytes set below
    chunks.back()[20 + 40] = 'P';
    chunks.back()[20 + 41] = 'O';
    chunks.back()[20 + 42] = 'S';
    chunks.back()[20 + 43] = 'I';
    add_qualifier(52, 48, 1, 1);                     // round-screen bit
    add_qualifier(52, 49, 1, 1);                     // wide color gamut
    add_qualifier(52, 49, 4, 1);                     // HDR mode
    add_qualifier(56, 53, 1, 1);                     // future config byte

    std::vector<uint8_t> modern_locale = Config(64);
    Put8(&modern_locale, 8, 'e');
    Put8(&modern_locale, 9, 'n');
    Put8(&modern_locale, 52, 1);                     // script was computed
    Put8(&modern_locale, 53, 'l');
    Put8(&modern_locale, 54, 'a');
    Put8(&modern_locale, 55, 't');
    Put8(&modern_locale, 56, 'n');
    chunks.push_back(TypeChunk(1, {{8, 8, false, 0x03, 0, {}}}, modern_locale));

    chunks.push_back(TypeChunk(2, {
        {0, 9, false, 0x1c, 0xff336699, {}},
        {1, 10, false, 0x1d, 0x00336699, {}},
        {2, 11, false, 0x1e, 0x0000f8a2, {}},
        {3, 12, false, 0x1f, 0x00000f8a, {}},
    }, Config()));
    chunks.push_back(TypeChunk(3, {
        {1, 13, false, 0x05, 0x00010001, {}},
        {4, 14, false, 0x06, 0x00010001, {}},
    }, Config(), true));

    Fixture fixture;
    fixture.valid = ResourceTable(chunks);
    fixture.malformed = fixture.valid;
    // Corrupt the first type chunk's entry offset so the table remains bounded but the entry
    // cannot be visited as a valid ResTable_entry.
    const size_t package_start = 12 + static_cast<size_t>(fixture.valid[16]) +
            (static_cast<size_t>(fixture.valid[17]) << 8) +
            (static_cast<size_t>(fixture.valid[18]) << 16) +
            (static_cast<size_t>(fixture.valid[19]) << 24);
    const size_t first_type = package_start + 288 + StringPool({"string", "color", "dimen"}).size() +
            StringPool({"welcome", "welcome_alias", "integer", "boolean", "dimension", "fraction",
                        "unknown", "bag", "qualified", "accent_argb8", "accent_rgb8", "accent_argb4",
                        "accent_rgb4", "sparse_one", "sparse_four"}).size();
    Patch32(&fixture.malformed, first_type + 24, 0xfffffffcu);
    fixture.malformed_chunk = fixture.valid;
    Patch32(&fixture.malformed_chunk, 4,
            static_cast<uint32_t>(fixture.malformed_chunk.size() + 1));
    return fixture;
}

const kudroid::ResourceValueTypeCount* FindValueType(const kudroid::ResourceTableAnalysis& analysis,
                                                      uint8_t code) {
    for (const auto& item : analysis.value_types) if (item.data_type == code) return &item;
    return nullptr;
}

const kudroid::ResourceQualifierCount* FindQualifier(const kudroid::ResourceTableAnalysis& analysis,
                                                      const std::string& name) {
    for (const auto& item : analysis.qualifiers) if (item.name == name) return &item;
    return nullptr;
}

}  // namespace

int main() {
    const Fixture fixture = BuildFixture();
    kudroid::ResourceTableAnalysis analysis;
    std::string error;
    Check(kudroid::resource_analyze_bytes(fixture.valid.data(), fixture.valid.size(), &analysis, &error),
          "parse the generated resource compatibility corpus");
    Check(analysis.packages == std::vector<std::string>{"com.example.fixture"},
          "report the package name deterministically");
    Check(analysis.resource_ids == 15 && analysis.resource_type_counts.at("string") == 9 &&
              analysis.resource_type_counts.at("color") == 4 &&
              analysis.resource_type_counts.at("dimen") == 2,
          "count unique resource IDs by Android type across configurations");
    Check(analysis.simple_entries == 43 && analysis.complex_entries == 1 &&
              analysis.complex_map_values == 2 && analysis.skipped_entries == 1 &&
              analysis.malformed_entries == 0,
          "count simple, complex, map, skipped, and malformed entries exactly");

    const std::array<uint8_t, 11> supported{{0x01, 0x03, 0x1c, 0x1d, 0x1e, 0x1f,
                                             0x05, 0x06, 0x10, 0x12, 0x77}};
    for (uint8_t code : supported) {
        Check(FindValueType(analysis, code) != nullptr,
              "report every generated Res_value.dataType code");
    }
    const std::array<std::tuple<uint8_t, uint64_t, bool>, 11> value_expectations{{
        {0x01, 1, true}, {0x03, 32, true},
        {0x1c, 1, true}, {0x1d, 1, true}, {0x1e, 1, true}, {0x1f, 1, true},
        {0x05, 3, false}, {0x06, 2, false}, {0x10, 1, false}, {0x12, 1, false}, {0x77, 1, false},
    }};
    for (const auto& [code, expected_count, expected_supported] : value_expectations) {
        const auto* found = FindValueType(analysis, code);
        Check(found != nullptr && found->count == expected_count &&
                  found->supported == expected_supported,
              expected_supported ? "count/classify a runtime-supported value type" :
                                   "count/classify an unsupported/unknown value type");
    }

    const std::vector<std::pair<std::string, bool>> qualifier_expectations = {
        {"locale.language", true}, {"locale.region", true},
        {"locale.script", false}, {"locale.variant", false}, {"mcc", false}, {"mnc", false},
        {"grammaticalGender", false},
        {"orientation", false}, {"touchscreen", false}, {"density", false},
        {"keyboard", false}, {"navigation", false}, {"input.keyboardHidden", false},
        {"input.navigationHidden", false}, {"screenWidth", false}, {"screenHeight", false},
        {"sdkVersion", false}, {"minorVersion", false}, {"screenLayout.size", false},
        {"screenLayout.long", false}, {"screenLayout.layoutDirection", false},
        {"uiMode.type", false}, {"uiMode.night", false},
        {"smallestScreenWidthDp", false}, {"screenWidthDp", false},
        {"screenHeightDp", false}, {"screenLayout2.round", false},
        {"colorMode.wideColorGamut", false}, {"colorMode.hdr", false},
        {"locale.scriptWasComputed", false}, {"locale.numberingSystem", false},
        {"unknownConfig.byte53", false},
    };
    for (const auto& [name, expected_supported] : qualifier_expectations) {
        const auto* found = FindQualifier(analysis, name);
        const std::string label = "classify qualifier " + name +
                (expected_supported ? " as supported" : " as unsupported");
        Check(found != nullptr && found->supported == expected_supported, label.c_str());
    }

    kudroid::ResourceTableAnalysis malformed_analysis;
    error.clear();
    Check(!kudroid::resource_analyze_bytes(fixture.malformed.data(), fixture.malformed.size(),
                                            &malformed_analysis, &error) && !error.empty() &&
              malformed_analysis.malformed_entries > 0,
          "reject malformed entry offsets with a diagnostic");
    Check(!kudroid::resource_analyze_bytes(fixture.malformed_chunk.data(),
                                            fixture.malformed_chunk.size(),
                                            &malformed_analysis, &error) && !error.empty(),
          "reject a malformed root chunk size before reading child data");

    const auto temp = std::filesystem::temp_directory_path() /
            ("kuart-resource-compat-" + std::to_string(static_cast<long long>(getpid())));
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp / "assets");
    {
        std::ofstream out(temp / "resources.arsc", std::ios::binary);
        out.write(reinterpret_cast<const char*>(fixture.valid.data()),
                  static_cast<std::streamsize>(fixture.valid.size()));
    }
    const std::string assets_dir = (temp / "assets").string();
    kudroid_set_assets_dir(assets_dir.c_str());
    kudroid::reset_resource_table_cache();
    uint32_t id = 0;
    std::string text;
    Check(kudroid::resource_get_identifier("welcome", "string", "com.example.fixture", &id) &&
              kudroid::resource_get_string(id, "", &text) && text == "Default" &&
              kudroid::resource_get_string(id, "en", &text) && text == "English" &&
              kudroid::resource_get_string(id, "en_US", &text) && text == "United States",
          "preserve existing default, language, and language-region lookup behavior");
    uint32_t alias = 0;
    Check(kudroid::resource_get_identifier("welcome_alias", "string", "com.example.fixture", &alias) &&
              kudroid::resource_get_string(alias, "en_US", &text) && text == "United States",
          "preserve existing reference resolution through localized resources");
    uint32_t color = 0;
    Check(kudroid::resource_get_color(0x7f020000, "", &color) && color == 0xff336699 &&
              kudroid::resource_get_color(0x7f020001, "", &color) && color == 0xff336699 &&
              kudroid::resource_get_color(0x7f020002, "", &color) && color == 0xff88aa22 &&
              kudroid::resource_get_color(0x7f020003, "", &color) && color == 0xffff88aa,
          "preserve all four existing color encodings in runtime lookup");
    uint32_t unsupported = 0;
    Check(kudroid::resource_get_identifier("integer", "string", "com.example.fixture", &unsupported) &&
              !kudroid::resource_get_string(unsupported, "", &text) &&
              kudroid::resource_get_identifier("dimension", "string", "com.example.fixture", &unsupported) &&
              !kudroid::resource_get_string(unsupported, "", &text) &&
              !kudroid::resource_get_color(unsupported, "", &color),
          "audit support reporting without changing unsupported runtime lookups");

    std::printf("-- local scanner CLI facade --\n");
    const auto quoted_raw = temp / "resource\"fixture.arsc";
    const auto apk_path = temp / "local-fixture.apk";
    const auto deflated_apk_path = temp / "deflated-fixture.apk";
    const auto nested_apk_path = temp / "nested-fixture.apk";
    const auto malformed_path = temp / "malformed.arsc";
    Check(WriteFile(quoted_raw, fixture.valid) &&
              WriteFile(apk_path, ZipEntry("resources.arsc", fixture.valid)) &&
              WriteFile(deflated_apk_path, ZipEntry("resources.arsc", fixture.valid, 8)) &&
              WriteFile(nested_apk_path, ZipEntry("assets/resources.arsc", fixture.valid)) &&
              WriteFile(malformed_path, fixture.malformed),
          "write generated raw and APK scanner inputs");
    const auto raw_before = ReadFile(quoted_raw);
    const auto apk_before = ReadFile(apk_path);

    std::string report;
    std::string cli_error;
    Check(kudroid::resource_compat_cli({quoted_raw.string()}, &report, &cli_error) == 0 &&
              report.find("Status: ok") != std::string::npos &&
              report.find("int_dec") != std::string::npos &&
              report.find("uiMode.night") != std::string::npos,
          "scan a raw ARSC and report unsupported values and qualifiers successfully");
    std::string json_one;
    std::string json_two;
    Check(kudroid::resource_compat_cli({"--json", quoted_raw.string()}, &json_one, &cli_error) == 0 &&
              kudroid::resource_compat_cli({"--json", quoted_raw.string()}, &json_two, &cli_error) == 0 &&
              json_one == json_two && json_one.find("\"schemaVersion\":1") != std::string::npos &&
              json_one.find("resource\\\"fixture.arsc") != std::string::npos,
          "emit deterministic schema-versioned JSON with escaped paths");
    Check(ReadFile(quoted_raw) == raw_before, "leave raw scanner input bytes unchanged");

    Check(kudroid::resource_compat_cli({apk_path.string()}, &report, &cli_error) == 0 &&
              report.find("Status: ok") != std::string::npos &&
              report.find("com.example.fixture") != std::string::npos,
          "scan the exact root resources.arsc entry from a generated APK");
    Check(kudroid::resource_compat_cli({deflated_apk_path.string()}, &report, &cli_error) == 0 &&
              report.find("Status: ok") != std::string::npos,
          "scan a deflated root resources.arsc entry with bounded decompression");
    Check(kudroid::resource_compat_cli({nested_apk_path.string()}, &report, &cli_error) == 1 &&
              report.find("no exact root resources.arsc") != std::string::npos,
          "reject nested APK resources.arsc without extracting an arbitrary path");
    Check(ReadFile(apk_path) == apk_before, "leave APK scanner input bytes unchanged");
    Check(kudroid::resource_compat_cli({"--json", malformed_path.string()}, &report, &cli_error) == 1 &&
              report.find("\"status\":\"error\"") != std::string::npos &&
              report.find("resource entry offset is invalid") != std::string::npos,
          "report malformed ARSC as an input error while retaining partial analysis");
    Check(kudroid::resource_compat_cli({(temp / "missing.arsc").string()}, &report, &cli_error) == 1 &&
              report.find("cannot stat input") != std::string::npos,
          "report missing local inputs as scan errors");
    Check(kudroid::resource_compat_cli({"--unknown"}, &report, &cli_error) == 2 &&
              cli_error.find("unknown option") != std::string::npos &&
              kudroid::resource_compat_cli({}, &report, &cli_error) == 2,
          "reject unknown options and empty input lists with usage errors");
    Check(kudroid::resource_compat_cli({quoted_raw.string(), malformed_path.string()}, &report,
                                        &cli_error) == 1 &&
              report.find("Status: ok") != std::string::npos &&
              report.find("Status: error") != std::string::npos,
          "continue scanning later inputs after an earlier per-file failure");

    std::filesystem::remove_all(temp);

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
