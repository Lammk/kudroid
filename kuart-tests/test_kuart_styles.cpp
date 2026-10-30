// test_kuart_styles.cpp - styled attribute resolution against a real resources.arsc.
//
// The harness builds an application resource table with style bags, points the runtime at
// it, loads the framework and the Java test classes, and runs StyleTests. The Java side
// uses the same ids as the constants below.
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/platform/AssetShim.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
using kudroid::kuart::DexJniEnv;
using kudroid::kuart::DexMethod;
using kudroid::kuart::DexString;
using kudroid::kuart::DexValue;
using kudroid::kuart::Interpreter;

int g_checks = 0;
int g_failures = 0;

void U8(std::vector<uint8_t>* out, uint8_t value) { out->push_back(value); }
void U16(std::vector<uint8_t>* out, uint16_t value) {
    U8(out, static_cast<uint8_t>(value));
    U8(out, static_cast<uint8_t>(value >> 8));
}
void U32(std::vector<uint8_t>* out, uint32_t value) {
    U16(out, static_cast<uint16_t>(value));
    U16(out, static_cast<uint16_t>(value >> 16));
}
void Patch16(std::vector<uint8_t>* out, size_t offset, uint16_t value) {
    (*out)[offset] = static_cast<uint8_t>(value);
    (*out)[offset + 1] = static_cast<uint8_t>(value >> 8);
}
void Patch32(std::vector<uint8_t>* out, size_t offset, uint32_t value) {
    Patch16(out, offset, static_cast<uint16_t>(value));
    Patch16(out, offset + 2, static_cast<uint16_t>(value >> 16));
}

std::vector<uint8_t> StringPool(const std::vector<std::string>& strings) {
    std::vector<uint8_t> out;
    U16(&out, 0x0001);
    U16(&out, 28);
    U32(&out, 0);
    U32(&out, static_cast<uint32_t>(strings.size()));
    U32(&out, 0);
    U32(&out, 0x00000100);  // UTF-8
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

struct Item { uint32_t attr; uint8_t type; uint32_t data; };
struct Style { uint32_t keyIndex; uint32_t parent; std::vector<Item> items; };

// A type chunk of style bags (complex entries).
std::vector<uint8_t> StyleTypeChunk(uint8_t typeId, const std::vector<Style>& styles) {
    const uint32_t configSize = 4;
    const uint16_t headerSize = static_cast<uint16_t>(20 + configSize);
    std::vector<uint8_t> out;
    U16(&out, 0x0201);
    U16(&out, headerSize);
    U32(&out, 0);
    U8(&out, typeId);
    U8(&out, 0);
    U16(&out, 0);
    U32(&out, static_cast<uint32_t>(styles.size()));
    U32(&out, 0);  // entriesStart, patched below
    U32(&out, configSize);
    std::vector<uint8_t> entries;
    std::vector<uint32_t> offsets;
    for (const Style& style : styles) {
        offsets.push_back(static_cast<uint32_t>(entries.size()));
        U16(&entries, 16);  // ResTable_map_entry header size
        U16(&entries, 0x0001);  // FLAG_COMPLEX
        U32(&entries, style.keyIndex);
        U32(&entries, style.parent);
        U32(&entries, static_cast<uint32_t>(style.items.size()));
        for (const Item& item : style.items) {
            U32(&entries, item.attr);
            U16(&entries, 8);
            U8(&entries, 0);
            U8(&entries, item.type);
            U32(&entries, item.data);
        }
    }
    for (uint32_t offset : offsets) U32(&out, offset);
    Patch32(&out, 16, static_cast<uint32_t>(headerSize + styles.size() * 4));
    out.insert(out.end(), entries.begin(), entries.end());
    Patch32(&out, 4, static_cast<uint32_t>(out.size()));
    return out;
}

std::vector<uint8_t> SimpleTypeChunk(uint8_t typeId, const std::vector<uint32_t>& keyIndices,
                                     const std::vector<std::pair<uint8_t, uint32_t>>& values) {
    const uint32_t configSize = 4;
    const uint16_t headerSize = static_cast<uint16_t>(20 + configSize);
    std::vector<uint8_t> out;
    U16(&out, 0x0201);
    U16(&out, headerSize);
    U32(&out, 0);
    U8(&out, typeId);
    U8(&out, 0);
    U16(&out, 0);
    U32(&out, static_cast<uint32_t>(values.size()));
    U32(&out, static_cast<uint32_t>(headerSize + values.size() * 4));
    U32(&out, configSize);
    for (uint32_t i = 0; i < values.size(); ++i) U32(&out, i * 16);
    for (uint32_t i = 0; i < values.size(); ++i) {
        U16(&out, 8);
        U16(&out, 0);
        U32(&out, keyIndices[i]);
        U16(&out, 8);
        U8(&out, 0);
        U8(&out, values[i].first);
        U32(&out, values[i].second);
    }
    Patch32(&out, 4, static_cast<uint32_t>(out.size()));
    return out;
}

constexpr uint8_t kRef = 0x01, kAttr = 0x02, kDimension = 0x05, kIntDec = 0x10, kColor = 0x1c;
constexpr uint32_t kMinWidth = 0x0101013f, kMinHeight = 0x01010140, kPaddingLeft = 0x010100d6,
                   kPaddingTop = 0x010100d7, kPaddingRight = 0x010100d8, kVisibility = 0x010100dc,
                   kBackground = 0x010100d4;
constexpr uint32_t kDefStyleAttr = 0x7f040001, kPadAttr = 0x7f040002;
constexpr uint32_t kStyleTheme = 0x7f010000, kStyleDef = 0x7f010001, kStyleXml = 0x7f010002,
                   kStyleRes = 0x7f010003, kStyleChild = 0x7f010004, kColorAccent = 0x7f020000;

uint32_t Px(uint32_t value) { return value << 8; }  // TYPE_DIMENSION, px unit

std::vector<uint8_t> Variant(uint32_t index, uint8_t type, uint32_t data,
                             uint16_t sw = 0, uint8_t night = 0, uint8_t orientation = 0,
                             uint16_t sdk = 0, uint16_t density = 0) {
    std::vector<uint32_t> keys(index + 1, 6);
    std::vector<std::pair<uint8_t, uint32_t>> values(index + 1, {type, data});
    auto chunk = SimpleTypeChunk(3, keys, values);
    chunk.insert(chunk.begin() + 24, 32, 0);
    Patch16(&chunk, 2, 56);
    Patch32(&chunk, 4, static_cast<uint32_t>(chunk.size()));
    Patch32(&chunk, 16, 56 + (index + 1) * 4);
    Patch32(&chunk, 20, 36);
    chunk[32] = orientation;
    Patch16(&chunk, 34, density);
    Patch16(&chunk, 44, sdk);
    chunk[49] = night;
    Patch16(&chunk, 50, sw);
    for (uint32_t i = 0; i < index; ++i) Patch32(&chunk, 56 + i * 4, 0xffffffff);
    return chunk;
}

std::vector<uint8_t> ResourceTable() {
    const std::vector<uint8_t> globalStrings = StringPool({"unused", "day", "night", "res/day.xml", "res/night.xml"});
    const std::vector<uint8_t> typeStrings = StringPool({"style", "color", "qualified"});
    const std::vector<uint8_t> keyStrings =
            StringPool({"Theme", "DefStyle", "XmlStyle", "ResStyle", "Child", "accent", "variant"});
    const std::vector<uint8_t> styles = StyleTypeChunk(1, {
        {0, 0, {{kDefStyleAttr, kRef, kStyleDef}, {kMinHeight, kDimension, Px(33)},
                {kPaddingTop, kDimension, Px(7)}, {kPadAttr, kDimension, Px(11)}}},
        {1, 0, {{kMinWidth, kDimension, Px(20)}, {kMinHeight, kDimension, Px(22)},
                {kPaddingLeft, kDimension, Px(5)}, {kBackground, kRef, kColorAccent}}},
        {2, 0, {{kMinWidth, kDimension, Px(30)}, {kVisibility, kIntDec, 4},
                {kPaddingRight, kAttr, kPadAttr}}},
        {3, 0, {{kPaddingLeft, kDimension, Px(9)}, {kMinWidth, kDimension, Px(50)}}},
        {4, kStyleDef, {{kMinWidth, kDimension, Px(21)}}},
    });
    const std::vector<uint8_t> colors = SimpleTypeChunk(2, {5}, {{kColor, 0xff336699}});
    auto nightStyle = StyleTypeChunk(1, {{0, 0, {{kPaddingTop, kDimension, Px(17)}}}});
    nightStyle.insert(nightStyle.begin() + 24, 32, 0);
    Patch16(&nightStyle, 2, 56);
    Patch32(&nightStyle, 4, static_cast<uint32_t>(nightStyle.size()));
    Patch32(&nightStyle, 16, 60);
    Patch32(&nightStyle, 20, 36);
    nightStyle[49] = 0x20;

    std::vector<uint8_t> package;
    U16(&package, 0x0200);
    U16(&package, 288);
    U32(&package, 0);
    U32(&package, 0x7f);
    const char* name = "com.example.styles";
    for (size_t i = 0; i < 128; ++i) U16(&package, i < 18 ? static_cast<uint8_t>(name[i]) : 0);
    const uint32_t typeStringsOffset = 288;
    const uint32_t keyStringsOffset = typeStringsOffset + static_cast<uint32_t>(typeStrings.size());
    U32(&package, typeStringsOffset);
    U32(&package, 0);
    U32(&package, keyStringsOffset);
    U32(&package, 0);
    U32(&package, 0);
    package.insert(package.end(), typeStrings.begin(), typeStrings.end());
    package.insert(package.end(), keyStrings.begin(), keyStrings.end());
    package.insert(package.end(), styles.begin(), styles.end());
    package.insert(package.end(), colors.begin(), colors.end());
    package.insert(package.end(), nightStyle.begin(), nightStyle.end());
    const std::vector<std::vector<uint8_t>> variants = {
        Variant(0, 0x12, 0), Variant(0, 0x12, 1, 600),
        Variant(1, kColor, 0xffeeeeee), Variant(1, kColor, 0xff111111, 0, 0x20),
        Variant(2, kIntDec, 1), Variant(2, kIntDec, 2, 0, 0, 2),
        Variant(3, kIntDec, 28), Variant(3, kIntDec, 99, 0, 0, 0, 99),
        Variant(4, kRef, 0x7f030001),
        Variant(5, kIntDec, 160, 0, 0, 0, 0, 160),
        Variant(5, kIntDec, 480, 0, 0, 0, 0, 480),
        Variant(6, 3, 1), Variant(6, 3, 2, 0, 0x20),
        Variant(7, 3, 3), Variant(7, 3, 4, 0, 0x20),
        Variant(8, kIntDec, 160, 0, 0, 0, 0, 160),
        Variant(8, kIntDec, 240, 0, 0, 0, 0, 240),
        Variant(9, kIntDec, 480, 0, 0, 0, 0, 480),
        Variant(9, kIntDec, 65534, 0, 0, 0, 0, 65534)
    };
    for (const auto& variant : variants) package.insert(package.end(), variant.begin(), variant.end());
    Patch32(&package, 4, static_cast<uint32_t>(package.size()));

    std::vector<uint8_t> table;
    U16(&table, 0x0002);
    U16(&table, 12);
    U32(&table, 0);
    U32(&table, 1);
    table.insert(table.end(), globalStrings.begin(), globalStrings.end());
    table.insert(table.end(), package.begin(), package.end());
    Patch32(&table, 4, static_cast<uint32_t>(table.size()));
    return table;
}

std::vector<uint8_t> ReadFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::printf("=== KuART styled attribute resolution ===\n");
    const auto appDir = std::filesystem::temp_directory_path() / "kuart-styles-test";
    std::filesystem::remove_all(appDir);
    std::filesystem::create_directories(appDir / "assets");
    const std::vector<uint8_t> table = ResourceTable();
    {
        std::ofstream out(appDir / "resources.arsc", std::ios::binary);
        out.write(reinterpret_cast<const char*>(table.data()), static_cast<std::streamsize>(table.size()));
    }
    const std::string assetsDir = (appDir / "assets").string();
    kudroid_set_assets_dir(assetsDir.c_str());

    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex", &error)) {
        std::printf("  FAIL AddDexFile(framework.dex): %s\n", error.c_str());
        return 1;
    }
    const std::vector<uint8_t> testDex = ReadFile(KUART_JAVA_TEST_DEX);
    if (testDex.empty() || !linker.AddDexFile(testDex.data(), testDex.size(), "tests.dex", &error)) {
        std::printf("  FAIL AddDexFile(tests.dex): %s\n", error.c_str());
        return 1;
    }
    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    interp.set_instruction_limit(8000ull * 1000ull * 1000ull);

    DexClass* klass = linker.FindClass("LStyleTests;");
    DexMethod* method = klass != nullptr ? klass->FindDirectMethod("run", "()Ljava/lang/String;") : nullptr;
    if (klass == nullptr || !interp.EnsureInitialized(klass) || method == nullptr) {
        std::printf("  FAIL StyleTests.run not found\n");
        return 1;
    }
    const DexValue v = interp.Execute(method, nullptr, 0);
    if (interp.HasPendingException()) {
        std::printf("  FAIL StyleTests threw %s\n", interp.DescribePendingException().c_str());
        return 1;
    }
    std::istringstream lines(v.l != nullptr ? static_cast<DexString*>(v.l)->utf8 : "");
    std::string line;
    while (std::getline(lines, line)) {
        ++g_checks;
        if (line.rfind("FAIL", 0) == 0) ++g_failures;
        std::printf("  %s\n", line.c_str());
    }
    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 && g_checks > 0 ? 0 : 1;
}
