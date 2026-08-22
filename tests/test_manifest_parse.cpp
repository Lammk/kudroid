// Test parse_manifest: đọc LAUNCHER activity từ binary AndroidManifest.xml.
// Build AXML tổng hợp bằng tay (string pool + start/end element chunks).
#include "kudroid/APKExtractor.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using kudroid::APKExtractor;
using kudroid::ManifestInfo;

namespace {

void put16(std::vector<std::uint8_t>& v, std::size_t off, std::uint16_t x) {
    v[off] = static_cast<std::uint8_t>(x & 0xFF);
    v[off + 1] = static_cast<std::uint8_t>(x >> 8);
}
void put32(std::vector<std::uint8_t>& v, std::size_t off, std::uint32_t x) {
    put16(v, off, static_cast<std::uint16_t>(x & 0xFFFF));
    put16(v, off + 2, static_cast<std::uint16_t>(x >> 16));
}

// UTF-16LE string pool (UTF-8 flag = 0), style đơn giản nhất.
std::vector<std::uint8_t> buildStringPool(const std::vector<std::u16string>& strings) {
    std::vector<std::uint32_t> offsets(strings.size());
    std::vector<std::uint8_t> blob;
    for (const auto& s : strings) {
        offsets[&s - strings.data()] = static_cast<std::uint32_t>(blob.size());
        const std::uint16_t len = static_cast<std::uint16_t>(s.size());
        blob.push_back(static_cast<std::uint8_t>(len & 0xFF));
        blob.push_back(static_cast<std::uint8_t>(len >> 8));
        for (char16_t c : s) {
            blob.push_back(static_cast<std::uint8_t>(c & 0xFF));
            blob.push_back(static_cast<std::uint8_t>(c >> 8));
        }
        blob.push_back(0); blob.push_back(0); // NUL terminator
    }
    while (blob.size() % 4 != 0) blob.push_back(0);

    // Layout pool: [header 28B][offset table stringCount*4][string blob].
    // stringsStart phải trỏ tới SAU offset table, không phải ngay sau header.
    constexpr std::size_t headerSize = 28;
    const std::uint32_t stringsStart =
        static_cast<std::uint32_t>(headerSize + strings.size() * 4);
    std::vector<std::uint8_t> out(stringsStart + blob.size(), 0);
    put16(out, 0, 0x0001);                       // RES_STRING_POOL_TYPE
    put16(out, 2, 28);                           // header size
    put32(out, 4, static_cast<std::uint32_t>(out.size())); // total size
    put32(out, 8, static_cast<std::uint32_t>(strings.size()));
    put32(out, 12, 0);                           // styleCount
    put32(out, 16, 0);                           // flags (UTF-16)
    put32(out, 20, stringsStart);                // stringsStart (tuyệt đối trong chunk)
    put32(out, 24, 0);                           // stylesStart
    std::memcpy(out.data() + stringsStart, blob.data(), blob.size());
    for (std::size_t i = 0; i < strings.size(); ++i) {
        put32(out, 28 + i * 4, offsets[i]);
    }
    return out;
}

// Start element chunk: name + attrCount attrs (mỗi attr: ns, name, rawValue,
// typed value{size,res0,dataType,data}).
std::vector<std::uint8_t> startElement(std::uint32_t nameIdx,
                                       const std::vector<std::pair<std::uint32_t, std::uint32_t>>& nameRawPairs,
                                       std::uint32_t line = 1) {
    const std::uint16_t attrSize = 20;
    const std::size_t size = 36 + nameRawPairs.size() * attrSize;
    std::vector<std::uint8_t> c(size, 0);
    put16(c, 0, 0x0102);   // RES_XML_START_ELEMENT_TYPE
    put16(c, 2, 16);       // header size
    put32(c, 4, static_cast<std::uint32_t>(size));
    put32(c, 8, line);
    put32(c, 12, 0xFFFFFFFF); // comment
    put32(c, 16, 0xFFFFFFFF); // ns
    put32(c, 20, nameIdx);
    put16(c, 24, 36);      // attribute start
    put16(c, 26, attrSize);
    put16(c, 28, static_cast<std::uint16_t>(nameRawPairs.size()));
    std::size_t a = 36;
    for (const auto& [attrName, rawVal] : nameRawPairs) {
        put32(c, a, 0xFFFFFFFF);       // ns
        put32(c, a + 4, attrName);     // name
        put32(c, a + 8, rawVal);       // rawValue index (hoặc 0xFFFFFFFF)
        put16(c, a + 12, 8);           // typed value size
        c[a + 14] = 0;                 // res0
        c[a + 15] = 0x03;              // dataType STRING
        put32(c, a + 16, rawVal);      // data = string index
        a += attrSize;
    }
    return c;
}

std::vector<std::uint8_t> endElement(std::uint32_t nameIdx, std::uint32_t line = 1) {
    std::vector<std::uint8_t> c(24, 0);
    put16(c, 0, 0x0103);   // RES_XML_END_ELEMENT_TYPE
    put16(c, 2, 16);
    put32(c, 4, 24);
    put32(c, 8, line);
    put32(c, 12, 0xFFFFFFFF);
    put32(c, 16, 0xFFFFFFFF);
    put32(c, 20, nameIdx);
    return c;
}

std::vector<std::uint8_t> appendChunk(std::vector<std::uint8_t> doc, const std::vector<std::uint8_t>& chunk) {
    doc.insert(doc.end(), chunk.begin(), chunk.end());
    return doc;
}

} // namespace

int main() {
    // String pool indices:
    // 0 manifest, 1 package, 2 com.example.game, 3 application, 4 activity,
    // 5 name, 6 com.example.game.SplashActivity (KHÔNG phải launcher),
    // 7 intent-filter, 8 action, 9 android.intent.action.MAIN,
    // 10 category, 11 android.intent.category.LAUNCHER,
    // 12 com.example.game.MainActivity (LAUNCHER thật)
    const std::vector<std::u16string> strings = {
        u"manifest", u"package", u"com.example.game", u"application",
        u"activity", u"name", u"com.example.game.SplashActivity",
        u"intent-filter", u"action", u"android.intent.action.MAIN",
        u"category", u"android.intent.category.LAUNCHER",
        u"com.example.game.MainActivity",
    };

    std::vector<std::uint8_t> doc;
    // File header: [type=0x0003 u16][headerSize=8 u16][fileSize u32 điền sau].
    // parseAxml kiểm tra read32(data,0) == 0x00080003 (type | headerSize<<16).
    doc.resize(8, 0);
    put16(doc, 0, 0x0003);
    put16(doc, 2, 8);

    const auto pool = buildStringPool(strings);
    put32(doc, 4, 8 + pool.size()); // first chunk offset — sẽ fix lại cuối
    doc = appendChunk(doc, pool);

    // <manifest package="com.example.game">
    doc = appendChunk(doc, startElement(0, {{1, 2}}));
    //   <activity name="...SplashActivity"> </activity>  (đầu tiên, KHÔNG launcher)
    doc = appendChunk(doc, startElement(4, {{5, 6}}));
    doc = appendChunk(doc, endElement(4));
    //   <activity name="...MainActivity">
    doc = appendChunk(doc, startElement(4, {{5, 12}}));
    //     <intent-filter>
    doc = appendChunk(doc, startElement(7, {}));
    //       <action name="android.intent.action.MAIN">
    doc = appendChunk(doc, startElement(8, {{5, 9}}));
    doc = appendChunk(doc, endElement(8));
    //       <category name="android.intent.category.LAUNCHER">
    doc = appendChunk(doc, startElement(10, {{5, 11}}));
    doc = appendChunk(doc, endElement(10));
    //     </intent-filter>
    doc = appendChunk(doc, endElement(7));
    //   </activity>
    doc = appendChunk(doc, endElement(4));
    // </manifest>
    doc = appendChunk(doc, endElement(0));

    put32(doc, 4, static_cast<std::uint32_t>(doc.size())); // file size

    const ManifestInfo info = APKExtractor::parse_manifest(doc.data(), doc.size());

    int failures = 0;
    auto check = [&](bool ok, const char* what, const std::string& got) {
        if (ok) {
            std::printf("OK: %s = %s\n", what, got.c_str());
        } else {
            std::printf("FAIL: %s expected different, got '%s'\n", what, got.c_str());
            ++failures;
        }
    };
    check(info.packageName == "com.example.game", "packageName", info.packageName);
    check(info.mainActivity == "com.example.game.MainActivity", "mainActivity (LAUNCHER)", info.mainActivity);

    if (failures == 0) {
        std::printf("=== parse_manifest test PASSED ===\n");
        return 0;
    }
    std::printf("=== parse_manifest test FAILED (%d) ===\n", failures);
    return 1;
}
