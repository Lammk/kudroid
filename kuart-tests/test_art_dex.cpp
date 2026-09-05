// Host test: ART libdexfile parsing a synthetic DEX into class/method/bytecode.
// The DEX is generated in this file (no Android SDK on dev machines); layout per spec 035.
#include "dex/class_accessor-inl.h"
#include "dex/code_item_accessors-inl.h"
#include "dex/dex_file.h"
#include "dex/dex_file_loader.h"
#include "dex/dex_instruction.h"
#include "dex/standard_dex_file.h"

#include "base/leb128.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <zlib.h>

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

// DEX synthesizer.
// Little-endian; every offset is computed after section sizes are known.
class DexBuilder {
public:
    // Build a DEX holding class LHello extends Object with one static method
    // add(II)I whose body is `add-int v0, v1, v2; return v0`.
    std::vector<uint8_t> Build() {
        // string_ids must be ascending UTF-16 order (DEX spec).
        const char* kStrings[] = {
            "I",                   // 0
            "III",                 // 1 shorty of (II)I
            "LHello;",             // 2
            "Ljava/lang/Object;",  // 3
            "add",                 // 4
        };
        constexpr uint32_t kNumStrings = 5;
        // type_ids likewise ascending by descriptor_idx.
        const uint32_t kTypeToString[] = {0, 2, 3};  // I, LHello;, Object
        constexpr uint32_t kNumTypes = 3;
        constexpr uint16_t kTypeInt = 0;
        constexpr uint16_t kTypeHello = 1;
        constexpr uint16_t kTypeObject = 2;

        constexpr uint32_t kHeaderSize = 0x70;
        const uint32_t string_ids_off = kHeaderSize;
        const uint32_t type_ids_off = string_ids_off + kNumStrings * 4;
        const uint32_t proto_ids_off = type_ids_off + kNumTypes * 4;
        const uint32_t method_ids_off = proto_ids_off + 1 * 12;
        const uint32_t class_defs_off = method_ids_off + 1 * 8;
        const uint32_t data_off = class_defs_off + 1 * 32;

        // ── data section ──
        std::vector<uint8_t> data;
        const auto data_pos = [&]() { return data_off + static_cast<uint32_t>(data.size()); };
        const auto align4 = [&]() { while (data.size() % 4 != 0) data.push_back(0); };

        // string_data_item: LEB128(utf16_size) + MUTF-8 + '\0'
        uint32_t string_data_offs[kNumStrings];
        for (uint32_t i = 0; i < kNumStrings; ++i) {
            string_data_offs[i] = data_pos();
            const size_t len = std::strlen(kStrings[i]);
            art::EncodeUnsignedLeb128(&data, static_cast<uint32_t>(len));
            data.insert(data.end(), kStrings[i], kStrings[i] + len);
            data.push_back(0);
        }

        // TypeList for params (II)
        align4();
        const uint32_t param_type_list_off = data_pos();
        PutU32(&data, 2);
        PutU16(&data, kTypeInt);
        PutU16(&data, kTypeInt);

        // code_item for add(II)I
        align4();
        const uint32_t code_item_off = data_pos();
        PutU16(&data, 3);  // registers_size: v0 result, v1/v2 params
        PutU16(&data, 2);  // ins_size
        PutU16(&data, 0);  // outs_size
        PutU16(&data, 0);  // tries_size
        PutU32(&data, 0);  // debug_info_off
        PutU32(&data, 3);  // insns_size_in_code_units
        PutU16(&data, 0x0090);  // add-int vAA=v0 | op=0x90 (format 23x)
        PutU16(&data, 0x0201);  // BB=v1, CC=v2
        PutU16(&data, 0x000f);  // return vAA=v0 | op=0x0f

        // class_data_item: all LEB128
        const uint32_t class_data_off = data_pos();
        art::EncodeUnsignedLeb128(&data, 0);  // static_fields_size
        art::EncodeUnsignedLeb128(&data, 0);  // instance_fields_size
        art::EncodeUnsignedLeb128(&data, 1);  // direct_methods_size
        art::EncodeUnsignedLeb128(&data, 0);  // virtual_methods_size
        art::EncodeUnsignedLeb128(&data, 0);  // method_idx_diff (method 0)
        art::EncodeUnsignedLeb128(&data, 0x9);  // ACC_PUBLIC | ACC_STATIC
        art::EncodeUnsignedLeb128(&data, code_item_off);

        // map_list: required by spec; InitializeSectionsFromMapList reads it.
        align4();
        const uint32_t map_off = data_pos();
        struct MapEntry { uint16_t type; uint32_t size; uint32_t offset; };
        const MapEntry kMap[] = {
            {0x0000, 1, 0},                              // header_item
            {0x0001, kNumStrings, string_ids_off},       // string_id_item
            {0x0002, kNumTypes, type_ids_off},           // type_id_item
            {0x0003, 1, proto_ids_off},                  // proto_id_item
            {0x0005, 1, method_ids_off},                 // method_id_item
            {0x0006, 1, class_defs_off},                 // class_def_item
            {0x1001, 1, param_type_list_off},            // type_list
            {0x2001, kNumStrings, string_data_offs[0]},  // string_data_item
            {0x2000, 1, class_data_off},                 // class_data_item
            {0x2002, 1, code_item_off},                  // code_item
            {0x1000, 1, map_off},                        // map_list
        };
        const uint32_t map_count = sizeof(kMap) / sizeof(kMap[0]);
        PutU32(&data, map_count);
        for (const auto& m : kMap) {
            PutU16(&data, m.type);
            PutU16(&data, 0);
            PutU32(&data, m.size);
            PutU32(&data, m.offset);
        }

        const uint32_t data_size = static_cast<uint32_t>(data.size());
        const uint32_t file_size = data_off + data_size;

        // assemble file
        std::vector<uint8_t> out;
        out.reserve(file_size);

        // header
        const uint8_t kMagic[8] = {'d', 'e', 'x', '\n', '0', '3', '5', '\0'};
        out.insert(out.end(), kMagic, kMagic + 8);
        PutU32(&out, 0);  // checksum, filled in later
        out.resize(out.size() + 20, 0);  // SHA-1 signature, not verified
        PutU32(&out, file_size);
        PutU32(&out, kHeaderSize);
        PutU32(&out, 0x12345678);  // endian_tag little-endian
        PutU32(&out, 0);           // link_size
        PutU32(&out, 0);           // link_off
        PutU32(&out, map_off);
        PutU32(&out, kNumStrings);
        PutU32(&out, string_ids_off);
        PutU32(&out, kNumTypes);
        PutU32(&out, type_ids_off);
        PutU32(&out, 1);
        PutU32(&out, proto_ids_off);
        PutU32(&out, 0);  // field_ids_size
        PutU32(&out, 0);  // field_ids_off
        PutU32(&out, 1);
        PutU32(&out, method_ids_off);
        PutU32(&out, 1);
        PutU32(&out, class_defs_off);
        PutU32(&out, data_size);
        PutU32(&out, data_off);

        // string_ids
        for (uint32_t i = 0; i < kNumStrings; ++i) PutU32(&out, string_data_offs[i]);
        // type_ids
        for (uint32_t i = 0; i < kNumTypes; ++i) PutU32(&out, kTypeToString[i]);
        // proto_ids: shorty="III", return=I, params=(II)
        PutU32(&out, 1);
        PutU16(&out, kTypeInt);
        PutU16(&out, 0);
        PutU32(&out, param_type_list_off);
        // method_ids: LHello;.add proto0
        PutU16(&out, kTypeHello);
        PutU16(&out, 0);
        PutU32(&out, 4);
        // class_defs
        PutU16(&out, kTypeHello);
        PutU16(&out, 0);
        PutU32(&out, 0x1);  // ACC_PUBLIC
        PutU16(&out, kTypeObject);
        PutU16(&out, 0);
        PutU32(&out, 0);           // interfaces_off
        PutU32(&out, 0xFFFFFFFF);  // source_file_idx = NO_INDEX
        PutU32(&out, 0);           // annotations_off
        PutU32(&out, class_data_off);
        PutU32(&out, 0);  // static_values_off

        out.insert(out.end(), data.begin(), data.end());

        // checksum = adler32 of every byte after magic+checksum (offset 12 onward)
        const uint32_t adler = adler32(adler32(0L, nullptr, 0), out.data() + 12,
                                      static_cast<uInt>(out.size() - 12));
        std::memcpy(out.data() + 8, &adler, sizeof(adler));

        return out;
    }

private:
    static void PutU16(std::vector<uint8_t>* v, uint16_t x) {
        v->push_back(static_cast<uint8_t>(x & 0xFF));
        v->push_back(static_cast<uint8_t>(x >> 8));
    }
    static void PutU32(std::vector<uint8_t>* v, uint32_t x) {
        PutU16(v, static_cast<uint16_t>(x & 0xFFFF));
        PutU16(v, static_cast<uint16_t>(x >> 16));
    }
};

}  // namespace

int main() {
std::printf("=== art_dex: parse DEX b ng libdexfile c a ART ===\n");

    DexBuilder builder;
    const std::vector<uint8_t> dex_bytes = builder.Build();
    std::printf("DEX synthetic: %zu bytes\n", dex_bytes.size());

    // (1) Open DEX
    const art::DexFileLoader loader;
    std::string error_msg;
    std::unique_ptr<const art::DexFile> dex_file = loader.Open(
        dex_bytes.data(), dex_bytes.size(), "test.dex",
        /*location_checksum=*/0, /*oat_dex_file=*/nullptr,
        /*verify=*/false, /*verify_checksum=*/false, &error_msg);

    Check(dex_file != nullptr,
dex_file != nullptr ? "m  DEX succeeded" : "m  DEX: " + error_msg);
    if (dex_file == nullptr) {
        std::printf("=== art_dex test FAILED ===\n");
        return 1;
    }

    // (2) Header
    Check(dex_file->NumStringIds() == 5, "NumStringIds == 5");
    Check(dex_file->NumTypeIds() == 3, "NumTypeIds == 3");
    Check(dex_file->NumMethodIds() == 1, "NumMethodIds == 1");
    Check(dex_file->NumClassDefs() == 1, "NumClassDefs == 1");

    // (3) Class descriptor: what AutoStub/DexAotCache had to translate via JAR;
    // now read straight from DEX.
    const art::dex::ClassDef& class_def = dex_file->GetClassDef(0);
    const char* descriptor = dex_file->GetClassDescriptor(class_def);
    Check(std::strcmp(descriptor, "LHello;") == 0,
std::string("class descriptor == LHello; (th c t : ") + descriptor + ")");

    const char* super_descriptor =
        dex_file->StringByTypeIdx(class_def.superclass_idx_);
    Check(std::strcmp(super_descriptor, "Ljava/lang/Object;") == 0,
std::string("superclass == Ljava/lang/Object; (th c t : ") + super_descriptor + ")");

    // (4) Iterate methods via ClassAccessor
    art::ClassAccessor accessor(*dex_file, class_def);
    Check(accessor.NumMethods() == 1, "NumMethods == 1");

    int methods_seen = 0;
    bool found_add = false;
    bool bytecode_ok = false;
    for (const art::ClassAccessor::Method& method : accessor.GetMethods()) {
        ++methods_seen;
        const char* name = dex_file->GetMethodName(dex_file->GetMethodId(method.GetIndex()));
        const art::Signature sig = dex_file->GetMethodSignature(
            dex_file->GetMethodId(method.GetIndex()));
        const std::string sig_str = sig.ToString();
        std::printf("  method: %s%s access=0x%x static=%d\n", name, sig_str.c_str(),
                    method.GetAccessFlags(), method.IsStaticOrDirect() ? 1 : 0);

        if (std::strcmp(name, "add") == 0 && sig_str == "(II)I") {
            found_add = true;
        }

        // (5) Read bytecode: the interpreter goes through this exact API.
        art::CodeItemDataAccessor code(*dex_file, method.GetCodeItem());
        Check(code.RegistersSize() == 3, "registers_size == 3");
        Check(code.InsSize() == 2, "ins_size == 2");
        Check(code.InsnsSizeInCodeUnits() == 3, "insns 3 code units");

        std::vector<art::Instruction::Code> opcodes;
        for (const art::DexInstructionPcPair& pair : code) {
            const art::Instruction& inst = pair.Inst();
            std::printf("    %04x: %s\n", static_cast<unsigned int>(pair.DexPc()), inst.Name());
            opcodes.push_back(inst.Opcode());
        }
        bytecode_ok = opcodes.size() == 2 &&
                      opcodes[0] == art::Instruction::ADD_INT &&
                      opcodes[1] == art::Instruction::RETURN;
    }

Check(methods_seen == 1, "duy t  c  ng 1 method");
Check(found_add, "t m th y add(II)I");
Check(bytecode_ok, "bytecode gi i m   ng: add-int r i return");

    // (6) FindClassDef by descriptor, as the class loader uses it.
    const art::dex::TypeId* type_id = dex_file->FindTypeId("LHello;");
Check(type_id != nullptr, "FindTypeId(LHello;) kh c null");
    if (type_id != nullptr) {
        const art::dex::TypeIndex type_idx = dex_file->GetIndexForTypeId(*type_id);
        const art::dex::ClassDef* found = dex_file->FindClassDef(type_idx);
Check(found == &class_def, "FindClassDef tr  v   ng ClassDef");
    }

    if (g_failures == 0) {
        std::printf("=== art_dex test PASSED ===\n");
        return 0;
    }
std::printf("=== art_dex test FAILED (%d error) ===\n", g_failures);
    return 1;
}
