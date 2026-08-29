// Test parse_manifest: reads LAUNCHER activity from binary AndroidManifest.xml.
// Build synthesized AXML manually (string pool + start/end element chunks).
#include "kudroid/APKExtractor.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using kudroid::ActivityEntry;
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

// UTF-16LE string pool (UTF-8 flag = 0), the simplest style.
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
    // stringsStart must point AFTER the offset table, not right after the header.
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
    put32(out, 20, stringsStart);                // stringsStart (absolute in chunk)
    put32(out, 24, 0);                           // stylesStart
    std::memcpy(out.data() + stringsStart, blob.data(), blob.size());
    for (std::size_t i = 0; i < strings.size(); ++i) {
        put32(out, 28 + i * 4, offsets[i]);
    }
    return out;
}

// Start element chunk: name + attrCount attrs (each attr: ns, name, rawValue,
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
    // attributeStart counts from the beginning of ResXMLTree_attrExt (chunk+16), NOT from the beginning
    // chunk — attrs is located at byte 36 so the correct value according to the spec is 20.
    put16(c, 24, 20);
    put16(c, 26, attrSize);
    put16(c, 28, static_cast<std::uint16_t>(nameRawPairs.size()));
    std::size_t a = 36;
    for (const auto& [attrName, rawVal] : nameRawPairs) {
        put32(c, a, 0xFFFFFFFF);       // ns
        put32(c, a + 4, attrName);     // name
        put32(c, a + 8, rawVal);       // rawValue index (or 0xFFFFFFFF)
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
    // 5 name, 6 com.example.game.SplashActivity (NOT launcher),
    // 7 intent-filter, 8 action, 9 android.intent.action.MAIN,
    // 10 category, 11 android.intent.category.LAUNCHER,
    // 12 com.example.game.MainActivity (real LAUNCHER),
    // 13 appComponentFactory, 14 its value, 15 com.example.game.GameApp,
    // 16 meta-data, 17 value, 18 android.app.lib_name, 19 libgame,
    // 20 com.example.app.KEY, 21 app-level-value
    const std::vector<std::u16string> strings = {
        u"manifest", u"package", u"com.example.game", u"application",
        u"activity", u"name", u"com.example.game.SplashActivity",
        u"intent-filter", u"action", u"android.intent.action.MAIN",
        u"category", u"android.intent.category.LAUNCHER",
        u"com.example.game.MainActivity",
        u"appComponentFactory", u"androidx.core.app.CoreComponentFactory",
        u"com.example.game.GameApp",
        u"meta-data", u"value", u"android.app.lib_name", u"libgame",
        u"com.example.app.KEY", u"app-level-value",
    };

    std::vector<std::uint8_t> doc;
    // Header file: [type=0x0003 u16][headerSize=8 u16][fileSize u32 fill in later].
    // parseAxml checks read32(data,0) == 0x00080003 (type | headerSize<<16).
    doc.resize(8, 0);
    put16(doc, 0, 0x0003);
    put16(doc, 2, 8);

    const auto pool = buildStringPool(strings);
    put32(doc, 4, 8 + pool.size()); // first chunk offset — will fix the end
    doc = appendChunk(doc, pool);

    // <manifest package="com.example.game">
    doc = appendChunk(doc, startElement(0, {{1, 2}}));
    //   <application name="...GameApp" appComponentFactory="androidx...CoreComponentFactory">
    doc = appendChunk(doc, startElement(3, {{5, 15}, {13, 14}}));
    //     <meta-data name="com.example.app.KEY" value="app-level-value" />
    doc = appendChunk(doc, startElement(16, {{5, 20}, {17, 21}}));
    doc = appendChunk(doc, endElement(16));
    //   <activity name="...SplashActivity"> </activity> (first, NO launcher)
    doc = appendChunk(doc, startElement(4, {{5, 6}}));
    doc = appendChunk(doc, endElement(4));
    //   <activity name="...MainActivity">
    doc = appendChunk(doc, startElement(4, {{5, 12}}));
    //     <meta-data name="android.app.lib_name" value="libgame" />
    //     Nested inside <activity>, which is where AGDK's GameActivity looks for it.
    doc = appendChunk(doc, startElement(16, {{5, 18}, {17, 19}}));
    doc = appendChunk(doc, endElement(16));
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
    // </application>
    doc = appendChunk(doc, endElement(3));
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
    check(info.appClass == "com.example.game.GameApp", "appClass", info.appClass);
    // Android instantiates the factory before any component, so its <clinit> runs
    // before all app code. Losing it here means that initialisation never happens
    // and whatever the app sets up there is empty when it is later read.
    check(info.appComponentFactory == "androidx.core.app.CoreComponentFactory",
          "appComponentFactory", info.appComponentFactory);

    // Every declared activity is recorded, not just the launcher. This is what lets
    // the runtime try real manifest entries instead of inventing names such as
    // "<pkg>.Main" — those could only match by coincidence and otherwise produced a
    // string of ClassNotFoundException for classes no app ever declared.
    check(info.activities.size() == 2, "activities recorded",
          std::to_string(info.activities.size()));
    if (info.activities.size() == 2) {
        // Declaration order preserved: Splash was declared first.
        check(info.activities[0].name == "com.example.game.SplashActivity",
              "activities[0] name", info.activities[0].name);
        check(!info.activities[0].isLauncher, "activities[0] is not LAUNCHER",
              info.activities[0].isLauncher ? "true" : "false");
        check(info.activities[1].name == "com.example.game.MainActivity",
              "activities[1] name", info.activities[1].name);
        check(info.activities[1].isLauncher, "activities[1] is LAUNCHER",
              info.activities[1].isLauncher ? "true" : "false");
        // An activity carrying an intent-filter is implicitly exported.
        check(info.activities[1].isExported, "activities[1] exported (has intent-filter)",
              info.activities[1].isExported ? "true" : "false");
    }

    // launchOrder() puts launcher activities first so the runtime tries the entry
    // point the Android launcher would use, then the remaining declared activities.
    const std::vector<std::string> order = info.launchOrder();
    check(order.size() == 2, "launchOrder size", std::to_string(order.size()));
    if (order.size() == 2) {
        check(order[0] == "com.example.game.MainActivity", "launchOrder[0] is the LAUNCHER",
              order[0]);
        check(order[1] == "com.example.game.SplashActivity", "launchOrder[1] is the rest",
              order[1]);
    }

    // ── <meta-data> ──
    //
    // This is how a component is told things the framework has to look up for it, and
    // it is not optional decoration. AGDK's GameActivity reads
    //
    //   getActivityInfo(getIntent().getComponent(), GET_META_DATA)
    //       .metaData.getString("android.app.lib_name")
    //
    // to learn which .so holds its renderer. Parsing the manifest without meta-data
    // means the activity never finds its native library, so the surface it creates
    // stays blank — a failure that looks like a graphics bug and is not one.
    {
        // Scoped to the activity that declared it. Two activities in one app can
        // legitimately carry different values for the same key, so a global bag would
        // hand out the wrong one.
        const ActivityEntry* main = nullptr;
        const ActivityEntry* splash = nullptr;
        for (const ActivityEntry& a : info.activities) {
            if (a.name == "com.example.game.MainActivity") main = &a;
            if (a.name == "com.example.game.SplashActivity") splash = &a;
        }

        check(main != nullptr && main->metaData.size() == 1,
              "MainActivity meta-data count",
              main != nullptr ? std::to_string(main->metaData.size()) : "no activity");
        if (main != nullptr && main->metaData.size() == 1) {
            check(main->metaData[0].name == "android.app.lib_name",
                  "MainActivity meta-data key", main->metaData[0].name);
            check(main->metaData[0].value == "libgame",
                  "MainActivity meta-data value", main->metaData[0].value);
        }

        // The <meta-data> under <application> must NOT leak into an activity that
        // declared none of its own at parse time; that merge is a lookup-time
        // fallback, not a parse-time one.
        check(splash != nullptr && splash->metaData.empty(),
              "SplashActivity declared no meta-data of its own",
              splash != nullptr ? std::to_string(splash->metaData.size()) : "no activity");

        check(info.applicationMetaData.size() == 1, "<application> meta-data count",
              std::to_string(info.applicationMetaData.size()));
        if (info.applicationMetaData.size() == 1) {
            check(info.applicationMetaData[0].name == "com.example.app.KEY",
                  "<application> meta-data key", info.applicationMetaData[0].name);
            check(info.applicationMetaData[0].value == "app-level-value",
                  "<application> meta-data value", info.applicationMetaData[0].value);
        }

        // metaDataFor() is the lookup callers use: the activity's own entries when it
        // has them, the application's otherwise. The fallback matches how apps are
        // written rather than how Android stores it — a library documenting a manifest
        // key rarely says which element it belongs under, so across real APKs the same
        // key turns up in either place.
        const auto mainMeta = info.metaDataFor("com.example.game.MainActivity");
        check(mainMeta.size() == 1 && mainMeta[0].name == "android.app.lib_name",
              "metaDataFor(MainActivity) returns its own entries",
              mainMeta.empty() ? "empty" : mainMeta[0].name);

        const auto splashMeta = info.metaDataFor("com.example.game.SplashActivity");
        check(splashMeta.size() == 1 && splashMeta[0].name == "com.example.app.KEY",
              "metaDataFor(SplashActivity) falls back to <application>",
              splashMeta.empty() ? "empty" : splashMeta[0].name);

        const auto unknownMeta = info.metaDataFor("com.example.game.NotDeclared");
        check(unknownMeta.size() == 1 && unknownMeta[0].name == "com.example.app.KEY",
              "metaDataFor(unknown) falls back to <application>",
              unknownMeta.empty() ? "empty" : unknownMeta[0].name);
    }

    if (failures == 0) {
        std::printf("=== parse_manifest test PASSED ===\n");
        return 0;
    }
    std::printf("=== parse_manifest test FAILED (%d) ===\n", failures);
    return 1;
}
