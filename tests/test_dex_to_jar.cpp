// test_dex_to_jar.cpp — validates the DEX→JAR converter.
//
// Builds a minimal DEX in memory (one class with one method), runs it through
// DexToJar::convertBytes, and verifies the output is a valid ZIP (JAR) with a
// .class entry.
#include "kudroid/DexToJar.h"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace kudroid;

// Build a minimal DEX with one class "Lcom/example/Hello;" that has a method
// "greet" returning "Ljava/lang/String;".
static std::vector<uint8_t> buildMinimalDex() {
    std::vector<uint8_t> dex;
    // We'll build a DEX with:
    //   string_ids: ["com/example/Hello", "greet", "Ljava/lang/String;", "Lcom/example/Hello;", "Ljava/lang/Object;"]
    //   type_ids: [Lcom/example/Hello;, Ljava/lang/Object;, Ljava/lang/String;]
    //   proto_ids: [()->Ljava/lang/String;]
    //   method_ids: [class=Hello, proto=0, name=greet]
    //   class_defs: [class=Hello, super=Object, class_data with 1 method]

    // Layout offsets (computed as we append).
    // We'll place string_data at the end and reference via offsets.

    // Helper to append a string_data_item (uleb128 utf16_size + mutf8 + null).
    auto appendStringData = [&](const std::string& s) -> uint32_t {
        uint32_t off = static_cast<uint32_t>(dex.size());
        // utf16_size (approximate = byte length for ASCII).
        uint32_t size = static_cast<uint32_t>(s.size());
        // uleb128
        while (size >= 0x80) {
            dex.push_back((size & 0x7f) | 0x80);
            size >>= 7;
        }
        dex.push_back(size & 0x7f);
        dex.insert(dex.end(), s.begin(), s.end());
        dex.push_back(0);
        return off;
    };

    // We'll build the header last. First, reserve space for header (112 bytes).
    dex.resize(112, 0);

    // string_ids (5 entries) — offsets filled after string_data appended.
    uint32_t stringIdsOff = static_cast<uint32_t>(dex.size());
    std::vector<uint32_t> stringOffsets;
    std::vector<std::string> strings = {
        "com/example/Hello", "greet", "Ljava/lang/String;",
        "Lcom/example/Hello;", "Ljava/lang/Object;"
    };
    for (size_t i = 0; i < strings.size(); ++i) {
        dex.push_back(0); dex.push_back(0); dex.push_back(0); dex.push_back(0);
        stringOffsets.push_back(0);
    }

    // type_ids (3 entries): descriptor_idx into string_ids.
    uint32_t typeIdsOff = static_cast<uint32_t>(dex.size());
    // type 0 = Lcom/example/Hello; (string idx 3)
    // type 1 = Ljava/lang/Object; (string idx 4)
    // type 2 = Ljava/lang/String; (string idx 2)
    auto pushU32 = [&](uint32_t v) {
        dex.push_back(v & 0xff); dex.push_back((v >> 8) & 0xff);
        dex.push_back((v >> 16) & 0xff); dex.push_back((v >> 24) & 0xff);
    };
    pushU32(3); // type 0
    pushU32(4); // type 1
    pushU32(2); // type 2

    // proto_ids (1 entry): shorty_idx, return_type_idx, parameters_off.
    uint32_t protoIdsOff = static_cast<uint32_t>(dex.size());
    pushU32(2); // shorty_idx (Ljava/lang/String;)
    pushU32(2); // return_type_idx (Ljava/lang/String;)
    pushU32(0); // parameters_off (none)

    // field_ids (0 entries).
    uint32_t fieldIdsOff = static_cast<uint32_t>(dex.size());

    // method_ids (1 entry): class_idx, proto_idx, name_idx.
    uint32_t methodIdsOff = static_cast<uint32_t>(dex.size());
    dex.push_back(0); dex.push_back(0); // class_idx = 0
    dex.push_back(0); dex.push_back(0); // proto_idx = 0
    pushU32(1); // name_idx = "greet"

    // class_defs (1 entry): class_idx, access_flags, superclass_idx, interfaces_off,
    // source_file_idx, annotations_off, class_data_off, static_values_off.
    uint32_t classDefsOff = static_cast<uint32_t>(dex.size());
    pushU32(0); // class_idx = 0
    pushU32(0x0001); // access_flags = ACC_PUBLIC
    pushU32(1); // superclass_idx = 1 (Object)
    pushU32(0); // interfaces_off
    pushU32(0xffffffff); // source_file_idx
    pushU32(0); // annotations_off
    // class_data_off filled later
    uint32_t classDataOffPos = static_cast<uint32_t>(dex.size());
    pushU32(0); // placeholder
    pushU32(0); // static_values_off

    // class_data: static_fields=0, instance_fields=0, direct_methods=0, virtual_methods=1
    uint32_t classDataOff = static_cast<uint32_t>(dex.size());
    dex.push_back(0); // static_fields_size = 0
    dex.push_back(0); // instance_fields_size = 0
    dex.push_back(0); // direct_methods_size = 0
    dex.push_back(1); // virtual_methods_size = 1
    dex.push_back(0); // method_idx_diff = 0
    dex.push_back(0x0001); // access_flags = ACC_PUBLIC (uleb128)
    dex.push_back(0); // code_off = 0 (no code)

    // Patch class_data_off.
    dex[classDataOffPos] = classDataOff & 0xff;
    dex[classDataOffPos + 1] = (classDataOff >> 8) & 0xff;
    dex[classDataOffPos + 2] = (classDataOff >> 16) & 0xff;
    dex[classDataOffPos + 3] = (classDataOff >> 24) & 0xff;

    // Append string_data and patch string_ids offsets.
    for (size_t i = 0; i < strings.size(); ++i) {
        uint32_t off = appendStringData(strings[i]);
        uint32_t pos = stringIdsOff + i * 4;
        dex[pos] = off & 0xff;
        dex[pos + 1] = (off >> 8) & 0xff;
        dex[pos + 2] = (off >> 16) & 0xff;
        dex[pos + 3] = (off >> 24) & 0xff;
    }

    // Fill header.
    uint32_t fileSize = static_cast<uint32_t>(dex.size());
    uint8_t magic[8] = {0x64, 0x65, 0x78, 0x0a, 0x30, 0x33, 0x35, 0x00}; // "dex\n035\0"
    std::memcpy(dex.data(), magic, 8);
    // checksum (offset 8) — leave 0.
    // signature (offset 12) — leave 0.
    auto putU32 = [&](uint32_t off, uint32_t v) {
        dex[off] = v & 0xff;
        dex[off + 1] = (v >> 8) & 0xff;
        dex[off + 2] = (v >> 16) & 0xff;
        dex[off + 3] = (v >> 24) & 0xff;
    };
    putU32(32, fileSize);       // file_size
    putU32(36, 112);            // header_size
    putU32(40, 0x12345678);     // endian_tag (little-endian)
    putU32(56, 5);              // string_ids_size
    putU32(60, stringIdsOff);   // string_ids_off
    putU32(64, 3);              // type_ids_size
    putU32(68, typeIdsOff);     // type_ids_off
    putU32(72, 1);              // proto_ids_size
    putU32(76, protoIdsOff);    // proto_ids_off
    putU32(80, 0);              // field_ids_size
    putU32(84, fieldIdsOff);    // field_ids_off
    putU32(88, 1);              // method_ids_size
    putU32(92, methodIdsOff);   // method_ids_off
    putU32(96, 1);              // class_defs_size
    putU32(100, classDefsOff);  // class_defs_off
    putU32(104, 0);             // data_size
    putU32(108, 0);             // data_off

    return dex;
}

int main() {
    std::vector<uint8_t> dex = buildMinimalDex();
    std::printf("Built minimal DEX: %zu bytes\n", dex.size());

    std::vector<uint8_t> jar;
    std::string error;
    if (!DexToJar::convertBytes(dex, jar, &error)) {
        std::printf("FAIL: convertBytes error: %s\n", error.c_str());
        return 1;
    }

    std::printf("JAR output: %zu bytes\n", jar.size());

    // Debug: print first 8 bytes.
    std::printf("First 8 bytes: ");
    for (size_t i = 0; i < jar.size() && i < 8; ++i) {
        std::printf("%02x ", jar[i]);
    }
    std::printf("\n");

    // Verify ZIP magic.
    if (jar.size() < 4 || jar[0] != 'P' || jar[1] != 'K') {
        std::printf("FAIL: output is not a ZIP (bad magic)\n");
        return 1;
    }
    std::printf("OK: ZIP magic verified\n");

    // Verify it contains "com/example/Hello.class".
    std::string jarStr(jar.begin(), jar.end());
    if (jarStr.find("com/example/Hello.class") == std::string::npos) {
        std::printf("FAIL: JAR does not contain com/example/Hello.class\n");
        return 1;
    }
    std::printf("OK: JAR contains com/example/Hello.class\n");

    // Write the JAR to a file for external validation (unzip/javap).
    FILE* f = std::fopen("/tmp/test_dex_to_jar.jar", "wb");
    if (f) {
        std::fwrite(jar.data(), 1, jar.size(), f);
        std::fclose(f);
        std::printf("Wrote /tmp/test_dex_to_jar.jar for validation\n");
    }

    std::printf("PASS: DexToJar test succeeded\n");
    return 0;
}
