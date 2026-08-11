#include "kudroid/DexToJar.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <map>

namespace kudroid {

// ─────────────────────────────────────────────────────────────────────────────
// cấu trúc phân tích dex
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// phần đầu dex (little-endian).
struct DexHeader {
    uint8_t  magic[8];
    uint32_t checksum;
    uint8_t  signature[20];
    uint32_t fileSize;
    uint32_t headerSize;
    uint32_t endianTag;
    uint32_t linkSize;
    uint32_t linkOff;
    uint32_t mapOff;
    uint32_t stringIdsSize;
    uint32_t stringIdsOff;
    uint32_t typeIdsSize;
    uint32_t typeIdsOff;
    uint32_t protoIdsSize;
    uint32_t protoIdsOff;
    uint32_t fieldIdsSize;
    uint32_t fieldIdsOff;
    uint32_t methodIdsSize;
    uint32_t methodIdsOff;
    uint32_t classDefsSize;
    uint32_t classDefsOff;
    uint32_t dataSize;
    uint32_t dataOff;
};

// string_id_item: uint32 string_data_off
// type_id_item: uint32 descriptor_idx
// proto_id_item: uint32 shorty_idx, uint32 return_type_idx, uint32 parameters_off
// field_id_item: uint16 class_idx, uint16 type_idx, uint32 name_idx
// method_id_item: uint16 class_idx, uint16 proto_idx, uint32 name_idx
// class_def_item: uint32 class_idx, uint32 access_flags, uint32 superclass_idx,
//                 uint32 interfaces_off, uint32 source_file_idx, uint32 annotations_off,
//                 uint32 class_data_off, uint32 static_values_off

inline uint16_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
inline uint32_t rd32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

// đọc giá trị uleb128.
inline uint32_t readUleb128(const uint8_t*& p) {
    uint32_t result = 0;
    int shift = 0;
    for (;;) {
        uint8_t byte = *p++;
        result |= (uint32_t)(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) break;
        shift += 7;
    }
    return result;
}

// đọc chuỗi mutf-8 từ string_data_item.
std::string readMutf8(const uint8_t* p) {
    // bỏ qua uleb128 utf16_size.
    readUleb128(p);
    std::string result;
    while (*p != 0) {
        uint8_t b0 = *p++;
        if (b0 < 0x80) {
            result += (char)b0;
        } else if ((b0 & 0xE0) == 0xC0) {
            uint8_t b1 = *p++;
            result += (char)(((b0 & 0x1F) << 6) | (b1 & 0x3F));
        } else if ((b0 & 0xF0) == 0xE0) {
            uint8_t b1 = *p++;
            uint8_t b2 = *p++;
            result += (char)(((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F));
        } else {
            // 4-byte (cặp thay thế) — xấp xỉ.
            uint8_t b1 = *p++;
            uint8_t b2 = *p++;
            uint8_t b3 = *p++;
            (void)b1; (void)b2; (void)b3;
            result += '?';
        }
    }
    return result;
}

// chuyển đổi mô tả kiểu dex (ví dụ "lcom/foo/bar;") sang tên nội bộ jvm
// (ví dụ "com/foo/bar").
std::string typeToInternal(const std::string& desc) {
    if (!desc.empty() && desc[0] == 'L' && desc.back() == ';') {
        return desc.substr(1, desc.size() - 2);
    }
    return desc; // kiểu nguyên thuỷ và mảng giữ nguyên
}

// chuyển đổi chữ ký phương thức dex (proto) sang mô tả jvm.
// dex proto return_type_idx + parameters_off (type_list) → mô tả jvm.
std::string buildMethodDescriptor(const std::vector<std::string>& paramTypes,
                                  const std::string& returnType) {
    std::string desc = "(";
    for (const auto& pt : paramTypes) desc += pt;
    desc += ")";
    desc += returnType;
    return desc;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// bộ phân tích dex
// ─────────────────────────────────────────────────────────────────────────────

bool DexToJar::parseDex(const std::vector<uint8_t>& dex,
                        std::vector<ClassInfo>& classes, std::string* error) {
    if (dex.size() < sizeof(DexHeader)) {
        if (error) *error = "DEX too small";
        return false;
    }
    const uint8_t* base = dex.data();
    const DexHeader* hdr = reinterpret_cast<const DexHeader*>(base);

    // xác thực magic.
    if (memcmp(hdr->magic, "dex\n", 4) != 0) {
        if (error) *error = "Not a DEX file (bad magic)";
        return false;
    }

    // hàm hỗ trợ đọc chuỗi theo chỉ mục.
    auto getString = [&](uint32_t idx) -> std::string {
        if (idx >= hdr->stringIdsSize) return "";
        uint32_t off = rd32(base + hdr->stringIdsOff + idx * 4);
        if (off >= dex.size()) return "";
        return readMutf8(base + off);
    };

    // hàm hỗ trợ đọc mô tả kiểu theo chỉ mục.
    auto getType = [&](uint32_t idx) -> std::string {
        if (idx >= hdr->typeIdsSize) return "";
        uint32_t descIdx = rd32(base + hdr->typeIdsOff + idx * 4);
        return getString(descIdx);
    };

    // phân tích proto_ids → {returntype, paramtypes}.
    struct ProtoInfo {
        std::string returnType;
        std::vector<std::string> params;
    };
    std::vector<ProtoInfo> protos;
    protos.reserve(hdr->protoIdsSize);
    for (uint32_t i = 0; i < hdr->protoIdsSize; ++i) {
        const uint8_t* p = base + hdr->protoIdsOff + i * 12;
        uint32_t returnTypeIdx = rd32(p + 4);
        uint32_t paramsOff = rd32(p + 8);
        ProtoInfo pi;
        pi.returnType = getType(returnTypeIdx);
        if (paramsOff != 0 && paramsOff < dex.size()) {
            uint32_t size = rd32(base + paramsOff);
            for (uint32_t j = 0; j < size; ++j) {
                uint32_t typeIdx = rd16(base + paramsOff + 4 + j * 2);
                pi.params.push_back(getType(typeIdx));
            }
        }
        protos.push_back(pi);
    }

    // phân tích method_ids → {classidx, protoidx, name}.
    struct MethodId {
        uint32_t classIdx;
        uint32_t protoIdx;
        std::string name;
    };
    std::vector<MethodId> methodIds;
    methodIds.reserve(hdr->methodIdsSize);
    for (uint32_t i = 0; i < hdr->methodIdsSize; ++i) {
        const uint8_t* p = base + hdr->methodIdsOff + i * 8;
        MethodId m;
        m.classIdx = rd16(p);
        m.protoIdx = rd16(p + 2);
        m.name = getString(rd32(p + 4));
        methodIds.push_back(m);
    }

    // phân tích field_ids → {classidx, typeidx, name}.
    struct FieldId {
        uint32_t classIdx;
        std::string type;
        std::string name;
    };
    std::vector<FieldId> fieldIds;
    fieldIds.reserve(hdr->fieldIdsSize);
    for (uint32_t i = 0; i < hdr->fieldIdsSize; ++i) {
        const uint8_t* p = base + hdr->fieldIdsOff + i * 8;
        FieldId f;
        f.classIdx = rd16(p);
        f.type = getType(rd16(p + 2));
        f.name = getString(rd32(p + 4));
        fieldIds.push_back(f);
    }

    // phân tích class_defs.
    classes.reserve(hdr->classDefsSize);
    for (uint32_t i = 0; i < hdr->classDefsSize; ++i) {
        const uint8_t* p = base + hdr->classDefsOff + i * 32;
        uint32_t classIdx = rd32(p);
        uint32_t accessFlags = rd32(p + 4);
        uint32_t superclassIdx = rd32(p + 8);
        uint32_t interfacesOff = rd32(p + 12);
        uint32_t classDataOff = rd32(p + 24);

        ClassInfo ci;
        ci.name = typeToInternal(getType(classIdx));
        ci.superName = (superclassIdx == 0xffffffff)
                           ? "java/lang/Object"
                           : typeToInternal(getType(superclassIdx));
        ci.accessFlags = accessFlags;

        // giao diện.
        if (interfacesOff != 0 && interfacesOff < dex.size()) {
            uint32_t size = rd32(base + interfacesOff);
            for (uint32_t j = 0; j < size; ++j) {
                uint32_t typeIdx = rd16(base + interfacesOff + 4 + j * 2);
                ci.interfaces.push_back(typeToInternal(getType(typeIdx)));
            }
        }

        // dữ liệu lớp: phương thức + trường.
        if (classDataOff != 0 && classDataOff < dex.size()) {
            const uint8_t* cd = base + classDataOff;
            uint32_t staticFieldsSize = readUleb128(cd);
            uint32_t instanceFieldsSize = readUleb128(cd);
            uint32_t directMethodsSize = readUleb128(cd);
            uint32_t virtualMethodsSize = readUleb128(cd);

            // trường (tĩnh + thể hiện).
            uint32_t fieldIdx = 0;
            for (uint32_t j = 0; j < staticFieldsSize + instanceFieldsSize; ++j) {
                fieldIdx += readUleb128(cd);
                uint32_t fieldAccess = readUleb128(cd);
                (void)fieldAccess;
                if (fieldIdx < fieldIds.size()) {
                    ci.fields.push_back({fieldIds[fieldIdx].name, fieldIds[fieldIdx].type});
                }
            }

            // phương thức (trực tiếp + ảo).
            uint32_t methodIdx = 0;
            for (uint32_t j = 0; j < directMethodsSize + virtualMethodsSize; ++j) {
                methodIdx += readUleb128(cd);
                uint32_t methodAccess = readUleb128(cd);
                (void)methodAccess;
                uint32_t codeOff = readUleb128(cd);
                (void)codeOff;
                if (methodIdx < methodIds.size()) {
                    const MethodId& m = methodIds[methodIdx];
                    const ProtoInfo& proto = protos[m.protoIdx];
                    std::string desc = buildMethodDescriptor(proto.params, proto.returnType);
                    ci.methods.push_back({m.name, desc});
                }
            }
        }

        classes.push_back(ci);
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// trình ghi tệp .class jvm
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// trình ghi bộ đệm byte đơn giản.
class ByteWriter {
public:
    std::vector<uint8_t> data;

    void u1(uint8_t v) { data.push_back(v); }
    void u2(uint16_t v) { data.push_back(v >> 8); data.push_back(v & 0xff); }
    void u4(uint32_t v) {
        data.push_back((v >> 24) & 0xff);
        data.push_back((v >> 16) & 0xff);
        data.push_back((v >> 8) & 0xff);
        data.push_back(v & 0xff);
    }
    // các biến thể little-endian (cho định dạng zip/jar).
    void u2le(uint16_t v) { data.push_back(v & 0xff); data.push_back(v >> 8); }
    void u4le(uint32_t v) {
        data.push_back(v & 0xff);
        data.push_back((v >> 8) & 0xff);
        data.push_back((v >> 16) & 0xff);
        data.push_back((v >> 24) & 0xff);
    }
    void bytes(const uint8_t* p, size_t n) { data.insert(data.end(), p, p + n); }
};

// bộ xây dựng nhóm hằng số.
class ConstantPool {
public:
    // trả về chỉ mục nhóm hằng số (bắt đầu từ 1).
    uint16_t addUtf8(const std::string& s) {
        auto it = utf8Map.find(s);
        if (it != utf8Map.end()) return it->second;
        uint16_t idx = static_cast<uint16_t>(entries.size() + 1);
        entries.push_back({1, s}); // CONSTANT_Utf8
        utf8Map[s] = idx;
        return idx;
    }
    uint16_t addClass(const std::string& name) {
        uint16_t nameIdx = addUtf8(name);
        auto key = std::string("C:") + name;
        auto it = classMap.find(key);
        if (it != classMap.end()) return it->second;
        uint16_t idx = static_cast<uint16_t>(entries.size() + 1);
        entries.push_back({7, "", nameIdx}); // CONSTANT_Class
        classMap[key] = idx;
        return idx;
    }
    uint16_t addNameAndType(const std::string& name, const std::string& desc) {
        uint16_t nameIdx = addUtf8(name);
        uint16_t descIdx = addUtf8(desc);
        auto key = std::string("NT:") + name + ":" + desc;
        auto it = ntMap.find(key);
        if (it != ntMap.end()) return it->second;
        uint16_t idx = static_cast<uint16_t>(entries.size() + 1);
        entries.push_back({12, "", nameIdx, descIdx}); // CONSTANT_NameAndType
        ntMap[key] = idx;
        return idx;
    }
    uint16_t addMethodref(const std::string& cls, const std::string& name, const std::string& desc) {
        uint16_t classIdx = addClass(cls);
        uint16_t ntIdx = addNameAndType(name, desc);
        auto key = std::string("M:") + cls + ":" + name + ":" + desc;
        auto it = methodrefMap.find(key);
        if (it != methodrefMap.end()) return it->second;
        uint16_t idx = static_cast<uint16_t>(entries.size() + 1);
        entries.push_back({10, "", classIdx, ntIdx}); // CONSTANT_Methodref
        methodrefMap[key] = idx;
        return idx;
    }
    uint16_t addFieldref(const std::string& cls, const std::string& name, const std::string& desc) {
        uint16_t classIdx = addClass(cls);
        uint16_t ntIdx = addNameAndType(name, desc);
        auto key = std::string("F:") + cls + ":" + name + ":" + desc;
        auto it = fieldrefMap.find(key);
        if (it != fieldrefMap.end()) return it->second;
        uint16_t idx = static_cast<uint16_t>(entries.size() + 1);
        entries.push_back({9, "", classIdx, ntIdx}); // CONSTANT_Fieldref
        fieldrefMap[key] = idx;
        return idx;
    }

    // ghi nhóm hằng số vào tệp lớp.
    void write(ByteWriter& w) {
        w.u2(static_cast<uint16_t>(entries.size() + 1)); // count = entries + 1
        for (const auto& e : entries) {
            w.u1(e.tag);
            switch (e.tag) {
                case 1: { // Utf8
                    w.u2(static_cast<uint16_t>(e.str.size()));
                    w.bytes(reinterpret_cast<const uint8_t*>(e.str.data()), e.str.size());
                    break;
                }
                case 7: // Class
                case 8: // String
                    w.u2(e.a);
                    break;
                case 9: // Fieldref
                case 10: // Methodref
                case 11: // InterfaceMethodref
                case 12: // NameAndType
                    w.u2(e.a);
                    w.u2(e.b);
                    break;
                default:
                    break;
            }
        }
    }

private:
    struct Entry {
        uint8_t tag;
        std::string str;
        uint16_t a = 0;
        uint16_t b = 0;
    };
    std::vector<Entry> entries;
    std::map<std::string, uint16_t> utf8Map;
    std::map<std::string, uint16_t> classMap;
    std::map<std::string, uint16_t> ntMap;
    std::map<std::string, uint16_t> methodrefMap;
    std::map<std::string, uint16_t> fieldrefMap;
};

// xác định lệnh trả về mặc định cho mô tả phương thức.
// trả về mã byte cho "return default" (iconst_0/aconst_null/lconst_0/v.v.).
// cũng trả về số khe ngăn xếp được đẩy.
void emitDefaultReturn(ByteWriter& code, const std::string& desc, uint16_t& maxStack) {
    // desc = "(params)returnType"
    size_t close = desc.rfind(')');
    std::string ret = (close != std::string::npos) ? desc.substr(close + 1) : "V";
    if (ret == "V") {
        code.u1(0xB1); // return
        maxStack = std::max<uint16_t>(maxStack, 0);
    } else if (ret == "Z" || ret == "B" || ret == "C" || ret == "S" || ret == "I") {
        code.u1(0x03); // iconst_0
        code.u1(0xAC); // ireturn
        maxStack = std::max<uint16_t>(maxStack, 1);
    } else if (ret == "J") {
        code.u1(0x09); // lconst_0
        code.u1(0xAD); // lreturn
        maxStack = std::max<uint16_t>(maxStack, 2);
    } else if (ret == "F") {
        code.u1(0x0B); // fconst_0
        code.u1(0xAE); // freturn
        maxStack = std::max<uint16_t>(maxStack, 1);
    } else if (ret == "D") {
        code.u1(0x0E); // dconst_0
        code.u1(0xAF); // dreturn
        maxStack = std::max<uint16_t>(maxStack, 2);
    } else {
        // đối tượng/mảng → aconst_null + areturn
        code.u1(0x01); // aconst_null
        code.u1(0xB0); // areturn
        maxStack = std::max<uint16_t>(maxStack, 1);
    }
}

// xây dựng tệp .class cho lớp giả.
std::vector<uint8_t> buildClassFile(const DexToJar::ClassInfo& ci) {
    ByteWriter w;
    ConstantPool cp;

    // magic + phiên bản (java 8 = 52.0).
    w.u4(0xCAFEBABE);
    w.u2(0); // minor
    w.u2(52); // major

    // this_class, super_class.
    uint16_t thisClassIdx = cp.addClass(ci.name);
    uint16_t superClassIdx = cp.addClass(ci.superName);

    // giao diện.
    std::vector<uint16_t> interfaceIdx;
    for (const auto& iface : ci.interfaces) {
        interfaceIdx.push_back(cp.addClass(iface));
    }

    // trường.
    struct FieldEntry {
        uint16_t nameIdx;
        uint16_t descIdx;
        uint16_t access;
    };
    std::vector<FieldEntry> fields;
    for (const auto& f : ci.fields) {
        FieldEntry fe;
        fe.nameIdx = cp.addUtf8(f.first);
        fe.descIdx = cp.addUtf8(f.second);
        fe.access = 0x0001; // ACC_PUBLIC
        fields.push_back(fe);
    }

    // phương thức: hàm tạo mặc định + tất cả các phương thức được khai báo.
    struct MethodEntry {
        uint16_t nameIdx;
        uint16_t descIdx;
        uint16_t access;
        std::vector<uint8_t> code;
        uint16_t maxStack;
        uint16_t maxLocals;
    };
    std::vector<MethodEntry> methods;

    // hàm tạo mặc định: <init>()v → aload_0; invokespecial object.<init>; return.
    {
        MethodEntry me;
        me.nameIdx = cp.addUtf8("<init>");
        me.descIdx = cp.addUtf8("()V");
        me.access = 0x0001; // ACC_PUBLIC
        ByteWriter code;
        code.u1(0x2A); // aload_0
        uint16_t objInit = cp.addMethodref("java/lang/Object", "<init>", "()V");
        code.u1(0xB7); // invokespecial
        code.u2(objInit);
        code.u1(0xB1); // return
        me.code = code.data;
        me.maxStack = 1;
        me.maxLocals = 1;
        methods.push_back(me);
    }

    // các phương thức được khai báo (phần thân trống).
    for (const auto& m : ci.methods) {
        // bỏ qua <clinit> (khởi tạo tĩnh) — chúng ta không phát ra nó.
        if (m.first == "<clinit>") continue;

        MethodEntry me;
        me.nameIdx = cp.addUtf8(m.first);
        me.descIdx = cp.addUtf8(m.second);
        me.access = 0x0001; // ACC_PUBLIC
        ByteWriter code;
        uint16_t maxStack = 0;
        emitDefaultReturn(code, m.second, maxStack);
        me.code = code.data;
        me.maxStack = maxStack;
        // maxlocals = 1 (this) + params.
        size_t close = m.second.rfind(')');
        std::string params = (close != std::string::npos) ? m.second.substr(1, close - 1) : "";
        uint16_t locals = 1;
        for (size_t i = 0; i < params.size(); ++i) {
            char c = params[i];
            if (c == 'J' || c == 'D') {
                locals += 2;
            } else if (c == 'L') {
                locals += 1;
                while (i < params.size() && params[i] != ';') i++;
            } else if (c == '[') {
                locals += 1;
                while (i < params.size() && (params[i] == '[')) i++;
                if (i < params.size() && params[i] == 'L') {
                    while (i < params.size() && params[i] != ';') i++;
                }
            } else {
                locals += 1;
            }
        }
        me.maxLocals = locals;
        methods.push_back(me);
    }

    // ghi nhóm hằng số.
    uint16_t codeNameIdx = cp.addUtf8("Code");
    cp.write(w);

    // cờ truy cập (acc_public | acc_super).
    w.u2(0x0021);
    w.u2(thisClassIdx);
    w.u2(superClassIdx);

    // giao diện.
    w.u2(static_cast<uint16_t>(interfaceIdx.size()));
    for (uint16_t idx : interfaceIdx) w.u2(idx);

    // trường.
    w.u2(static_cast<uint16_t>(fields.size()));
    for (const auto& f : fields) {
        w.u2(f.access);
        w.u2(f.nameIdx);
        w.u2(f.descIdx);
        w.u2(0); // no attributes
    }

    // phương thức.
    w.u2(static_cast<uint16_t>(methods.size()));
    for (const auto& m : methods) {
        w.u2(m.access);
        w.u2(m.nameIdx);
        w.u2(m.descIdx);
        w.u2(1); // một thuộc tính: code
        // thuộc tính code: name_index, length, max_stack, max_locals,
        // code_length, code, exception_table_length, attributes_count.
        uint32_t codeLen = static_cast<uint32_t>(m.code.size());
        uint32_t attrLen = 2 + 2 + 4 + codeLen + 2 + 2; // max_stack+max_locals+code_length+code+exc+attrs
        w.u2(codeNameIdx);
        w.u4(attrLen);
        w.u2(m.maxStack);
        w.u2(m.maxLocals);
        w.u4(codeLen);
        w.bytes(m.code.data(), m.code.size());
        w.u2(0); // độ dài bảng ngoại lệ
        w.u2(0); // số lượng thuộc tính
    }

    // thuộc tính lớp.
    w.u2(0);

    return w.data;
}

// ─────────────────────────────────────────────────────────────────────────────
// trình ghi zip (jar) — tối giản, không nén (các mục được lưu).
// ─────────────────────────────────────────────────────────────────────────────

uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            uint32_t mask = (crc & 1) ? 0xEDB88320 : 0;
            crc = (crc >> 1) ^ mask;
        }
    }
    return ~crc;
}

void writeZipEntry(ByteWriter& out, const std::string& name, const std::vector<uint8_t>& data) {
    // phần đầu tệp cục bộ (little-endian).
    out.u4le(0x04034b50);
    out.u2le(20); // phiên bản cần thiết
    out.u2le(0);  // cờ
    out.u2le(0);  // phương pháp nén (được lưu)
    out.u2le(0);  // thời gian sửa
    out.u2le(0);  // ngày sửa
    out.u4le(crc32(data.data(), data.size()));
    out.u4le(static_cast<uint32_t>(data.size())); // kích thước nén
    out.u4le(static_cast<uint32_t>(data.size())); // kích thước không nén
    out.u2le(static_cast<uint16_t>(name.size()));
    out.u2le(0); // độ dài thêm
    out.bytes(reinterpret_cast<const uint8_t*>(name.data()), name.size());
    out.bytes(data.data(), data.size());
}

void writeCentralDir(ByteWriter& out, const std::string& name, const std::vector<uint8_t>& data,
                     uint32_t localOffset) {
    out.u4le(0x02014b50);
    out.u2le(20); // phiên bản tạo bởi
    out.u2le(20); // phiên bản cần thiết
    out.u2le(0);  // cờ
    out.u2le(0);  // phương pháp
    out.u2le(0);  // thời gian sửa
    out.u2le(0);  // ngày sửa
    out.u4le(crc32(data.data(), data.size()));
    out.u4le(static_cast<uint32_t>(data.size()));
    out.u4le(static_cast<uint32_t>(data.size()));
    out.u2le(static_cast<uint16_t>(name.size()));
    out.u2le(0); // thêm
    out.u2le(0); // bình luận
    out.u2le(0); // số đĩa
    out.u2le(0); // thuộc tính nội bộ
    out.u4le(0); // thuộc tính bên ngoài
    out.u4le(localOffset);
    out.bytes(reinterpret_cast<const uint8_t*>(name.data()), name.size());
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// bộ xây dựng jar
// ─────────────────────────────────────────────────────────────────────────────

bool DexToJar::buildJar(const std::vector<ClassInfo>& classes,
                        std::vector<uint8_t>& outJar, std::string* error) {
    (void)error;
    ByteWriter jar;
    std::vector<uint32_t> centralOffsets;
    std::vector<std::string> entryNames;
    std::vector<std::vector<uint8_t>> entryData;

    // xây dựng từng tệp lớp.
    for (const auto& ci : classes) {
        std::vector<uint8_t> classFile = buildClassFile(ci);
        std::string entryName = ci.name + ".class";
        entryNames.push_back(entryName);
        entryData.push_back(classFile);
    }

    // ghi tiêu đề cục bộ + dữ liệu.
    for (size_t i = 0; i < entryNames.size(); ++i) {
        centralOffsets.push_back(static_cast<uint32_t>(jar.data.size()));
        writeZipEntry(jar, entryNames[i], entryData[i]);
    }

    // ghi thư mục trung tâm.
    uint32_t centralStart = static_cast<uint32_t>(jar.data.size());
    for (size_t i = 0; i < entryNames.size(); ++i) {
        writeCentralDir(jar, entryNames[i], entryData[i], centralOffsets[i]);
    }
    uint32_t centralSize = static_cast<uint32_t>(jar.data.size()) - centralStart;

    // kết thúc thư mục trung tâm.
    jar.u4le(0x06054b50);
    jar.u2le(0); // số đĩa
    jar.u2le(0); // đĩa thư mục trung tâm
    jar.u2le(static_cast<uint16_t>(entryNames.size()));
    jar.u2le(static_cast<uint16_t>(entryNames.size()));
    jar.u4le(centralSize);
    jar.u4le(centralStart);
    jar.u2le(0); // độ dài bình luận

    outJar = jar.data;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// api công khai
// ─────────────────────────────────────────────────────────────────────────────

bool DexToJar::convert(const std::string& dexPath, std::vector<uint8_t>& outJar,
                       std::string* error) {
    std::vector<uint8_t> dexBytes;
    FILE* f = std::fopen(dexPath.c_str(), "rb");
    if (!f) {
        if (error) *error = "Cannot open DEX file: " + dexPath;
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(f);
        if (error) *error = "Empty DEX file";
        return false;
    }
    dexBytes.resize(static_cast<size_t>(size));
    size_t read = std::fread(dexBytes.data(), 1, dexBytes.size(), f);
    std::fclose(f);
    if (read != dexBytes.size()) {
        if (error) *error = "Failed to read DEX file";
        return false;
    }
    return convertBytes(dexBytes, outJar, error);
}

bool DexToJar::convertBytes(const std::vector<uint8_t>& dexBytes,
                            std::vector<uint8_t>& outJar, std::string* error) {
    std::vector<ClassInfo> classes;
    if (!parseDex(dexBytes, classes, error)) {
        return false;
    }
    if (classes.empty()) {
        if (error) *error = "No classes found in DEX";
        return false;
    }
    return buildJar(classes, outJar, error);
}

} // namespace kudroid
