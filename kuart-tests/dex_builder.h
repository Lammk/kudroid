// Build DEX 035 files from class/method/field spec (no javac+d8 on dev machines).
// Test-only. Id tables are collected, sorted per spec, then indexed, so callers
// may declare entities in any order.
#ifndef KUDROID_TESTS_DEX_BUILDER_H
#define KUDROID_TESTS_DEX_BUILDER_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <zlib.h>

namespace dexbuild {

struct FieldSpec {
    std::string name;
    std::string type;  // descriptor: "I", "Ljava/lang/String;"
    uint32_t access_flags = 0x1;  // ACC_PUBLIC
};

// A field_ids entry for a field that is NOT declared anywhere in this DEX.
//
// Real APKs are full of these: the app references android.view.inputmethod.
// EditorInfo.inputType, the field lives in the framework, and the app's DEX only
// carries the reference. Reproducing that shape is the only way to test what the
// interpreter does when a referenced field cannot be resolved — which is a case
// with no auto-stub fallback, unlike a missing method.
struct FieldRefSpec {
    std::string owner;  // declaring class descriptor
    std::string name;
    std::string type;
};

// A method_ids entry for a method that is NOT declared anywhere in this DEX.
//
// The method counterpart of FieldRefSpec, and needed for the same reason: an app's
// DEX references framework methods it does not declare. The interpreter's behaviour
// when such a reference will not resolve is only reachable through this shape — a
// method declared in the same DEX always resolves, so it cannot exercise the path.
struct MethodRefSpec {
    std::string owner;  // declaring class descriptor
    std::string name;
    std::string return_type = "V";
    std::vector<std::string> params;

    std::string Shorty() const;
};

struct CatchSpec {
    std::string type;  // exception type descriptor; empty = catch-all (finally)
    uint16_t handler_pc = 0;
};

struct TrySpec {
    uint16_t start_addr = 0;
    uint16_t insn_count = 0;
    std::vector<CatchSpec> handlers;
};

struct MethodSpec {
    std::string name;
    std::string return_type = "V";
    std::vector<std::string> params;
    uint32_t access_flags = 0x1;

    // Bytecode in 16-bit code units. Empty = abstract/native method.
    std::vector<uint16_t> code;
    uint16_t registers_size = 0;
    uint16_t ins_size = 0;
    uint16_t outs_size = 0;

    std::vector<TrySpec> tries;

    std::string Shorty() const {
        std::string s;
        s += ShortyChar(return_type);
        for (const std::string& p : params) s += ShortyChar(p);
        return s;
    }

    static char ShortyChar(const std::string& descriptor) {
        if (descriptor.empty()) return 'V';
        const char c = descriptor[0];
        // Every reference type (L.../[...] maps to 'L' in shorty.
        return (c == '[' || c == 'L') ? 'L' : c;
    }
};

inline std::string MethodRefSpec::Shorty() const {
    std::string s;
    s += MethodSpec::ShortyChar(return_type);
    for (const std::string& p : params) s += MethodSpec::ShortyChar(p);
    return s;
}

struct ClassSpec {
    std::string descriptor;
    std::string superclass = "Ljava/lang/Object;";
    std::vector<std::string> interfaces;
    uint32_t access_flags = 0x1;
    std::vector<FieldSpec> static_fields;
    std::vector<FieldSpec> instance_fields;
    std::vector<MethodSpec> direct_methods;
    std::vector<MethodSpec> virtual_methods;

    // Entities only referenced by bytecode (const-string "x", new-array [I)
    // appear in no field/method; declare them here so they enter string_ids/type_ids.
    std::vector<std::string> extra_strings;
    std::vector<std::string> extra_types;

    // Field references the bytecode uses without the field being declared here.
    std::vector<FieldRefSpec> extra_field_refs;

    // Method references the bytecode uses without the method being declared here.
    std::vector<MethodRefSpec> extra_method_refs;
};

class DexBuilder {
public:
    std::vector<uint8_t> Build(const std::vector<ClassSpec>& classes) {
        Collect(classes);
        return Emit(classes);
    }

    // Getters below are valid only AFTER Build(): bytecode uses sorted-table indices.
    uint32_t StringIndexOf(const std::string& s) const { return string_idx_.at(s); }
    uint32_t TypeIndexOf(const std::string& descriptor) const {
        return type_idx_.at(descriptor);
    }
    uint32_t FieldIndexOf(const std::string& class_desc, const FieldSpec& f) const {
        return field_idx_.at(FieldKey{type_idx_.at(class_desc), type_idx_.at(f.type),
                                     string_idx_.at(f.name)});
    }
    // Index of a reference to a field declared elsewhere (see FieldRefSpec).
    uint32_t FieldRefIndexOf(const FieldRefSpec& f) const {
        return field_idx_.at(FieldKey{type_idx_.at(f.owner), type_idx_.at(f.type),
                                     string_idx_.at(f.name)});
    }
    uint32_t MethodIndexOf(const std::string& class_desc, const MethodSpec& m) const {
        ProtoKey key;
        key.return_type_idx = type_idx_.at(m.return_type);
        for (const std::string& p : m.params) key.param_type_idx.push_back(type_idx_.at(p));
        key.shorty = m.Shorty();
        return method_idx_.at(MethodKey{type_idx_.at(class_desc), proto_idx_.at(key),
                                       string_idx_.at(m.name)});
    }
    // Index of a reference to a method declared elsewhere (see MethodRefSpec).
    uint32_t MethodRefIndexOf(const MethodRefSpec& m) const {
        ProtoKey key;
        key.return_type_idx = type_idx_.at(m.return_type);
        for (const std::string& p : m.params) key.param_type_idx.push_back(type_idx_.at(p));
        key.shorty = m.Shorty();
        return method_idx_.at(MethodKey{type_idx_.at(m.owner), proto_idx_.at(key),
                                       string_idx_.at(m.name)});
    }

private:
    struct ProtoKey {
        uint32_t return_type_idx;
        std::vector<uint32_t> param_type_idx;
        std::string shorty;

        bool operator<(const ProtoKey& o) const {
            if (return_type_idx != o.return_type_idx) return return_type_idx < o.return_type_idx;
            return param_type_idx < o.param_type_idx;
        }
    };
    struct FieldKey {
        uint32_t class_idx, type_idx, name_idx;
        bool operator<(const FieldKey& o) const {
            if (class_idx != o.class_idx) return class_idx < o.class_idx;
            if (name_idx != o.name_idx) return name_idx < o.name_idx;
            return type_idx < o.type_idx;
        }
    };
    struct MethodKey {
        uint32_t class_idx, proto_idx, name_idx;
        bool operator<(const MethodKey& o) const {
            if (class_idx != o.class_idx) return class_idx < o.class_idx;
            if (name_idx != o.name_idx) return name_idx < o.name_idx;
            return proto_idx < o.proto_idx;
        }
    };

    // Phase 1: collect
    void Collect(const std::vector<ClassSpec>& classes) {
        // Strings and types first; proto/field/method use their indices.
        for (const ClassSpec& c : classes) {
            AddType(c.descriptor);
            if (!c.superclass.empty()) AddType(c.superclass);
            for (const std::string& i : c.interfaces) AddType(i);
            for (const std::string& s : c.extra_strings) AddString(s);
            for (const std::string& t : c.extra_types) AddType(t);
            for (const FieldRefSpec& f : c.extra_field_refs) {
                AddType(f.owner);
                AddString(f.name);
                AddType(f.type);
            }
            for (const MethodRefSpec& m : c.extra_method_refs) {
                AddType(m.owner);
                AddString(m.name);
                AddString(m.Shorty());
                AddType(m.return_type);
                for (const std::string& p : m.params) AddType(p);
            }

            for (const auto* list : {&c.static_fields, &c.instance_fields}) {
                for (const FieldSpec& f : *list) {
                    AddString(f.name);
                    AddType(f.type);
                }
            }
            for (const auto* list : {&c.direct_methods, &c.virtual_methods}) {
                for (const MethodSpec& m : *list) {
                    AddString(m.name);
                    AddString(m.Shorty());
                    AddType(m.return_type);
                    for (const std::string& p : m.params) AddType(p);
                    for (const TrySpec& t : m.tries) {
                        for (const CatchSpec& h : t.handlers) {
                            if (!h.type.empty()) AddType(h.type);
                        }
                    }
                }
            }
        }

        // Freeze string/type indices (already sorted via std::set).
        uint32_t idx = 0;
        for (const std::string& s : string_set_) string_idx_[s] = idx++;
        idx = 0;
        for (const std::string& t : type_set_) type_idx_[t] = idx++;

        // Build proto/field/method now; type_idx_ is frozen.
        for (const ClassSpec& c : classes) {
            const uint32_t class_type = type_idx_[c.descriptor];
            for (const FieldSpec& f : c.static_fields) AddFieldKey(class_type, f);
            for (const FieldSpec& f : c.instance_fields) AddFieldKey(class_type, f);
            // References to fields owned by another class, which this DEX does not
            // declare. The owner index is that other class, not class_type.
            for (const FieldRefSpec& f : c.extra_field_refs) {
                field_set_.insert(FieldKey{type_idx_[f.owner], type_idx_[f.type],
                                           string_idx_[f.name]});
            }
            for (const MethodSpec& m : c.direct_methods) AddMethodKey(class_type, m);
            for (const MethodSpec& m : c.virtual_methods) AddMethodKey(class_type, m);
            // References to methods owned by another class, which this DEX does not
            // declare. The owner index is that other class, not class_type.
            for (const MethodRefSpec& m : c.extra_method_refs) {
                ProtoKey key;
                key.return_type_idx = type_idx_[m.return_type];
                for (const std::string& p : m.params) {
                    key.param_type_idx.push_back(type_idx_[p]);
                }
                key.shorty = m.Shorty();
                proto_set_.insert(key);
                pending_methods_.push_back({type_idx_[m.owner], key, string_idx_[m.name]});
            }
        }

        idx = 0;
        for (const ProtoKey& p : proto_set_) proto_idx_[p] = idx++;
        idx = 0;
        for (const FieldKey& f : field_set_) field_idx_[f] = idx++;
        idx = 0;
        for (const MethodKey& m : method_set_) method_idx_[m] = idx++;
    }

    void AddString(const std::string& s) { string_set_.insert(s); }

    void AddType(const std::string& descriptor) {
        if (descriptor.empty()) return;
        string_set_.insert(descriptor);
        type_set_.insert(descriptor);
    }

    ProtoKey MakeProtoKey(const MethodSpec& m) {
        ProtoKey key;
        key.return_type_idx = type_idx_[m.return_type];
        for (const std::string& p : m.params) key.param_type_idx.push_back(type_idx_[p]);
        key.shorty = m.Shorty();
        return key;
    }

    void AddFieldKey(uint32_t class_type, const FieldSpec& f) {
        field_set_.insert(FieldKey{class_type, type_idx_[f.type], string_idx_[f.name]});
    }

    void AddMethodKey(uint32_t class_type, const MethodSpec& m) {
        const ProtoKey key = MakeProtoKey(m);
        proto_set_.insert(key);
        pending_methods_.push_back({class_type, key, string_idx_[m.name]});
    }

    // ── Phase 2: emit file ──
    std::vector<uint8_t> Emit(const std::vector<ClassSpec>& classes) {
        // proto_idx_ is frozen, so method_set_ can now be built.
        for (const auto& pm : pending_methods_) {
            method_set_.insert(MethodKey{pm.class_idx, proto_idx_[pm.proto], pm.name_idx});
        }
        uint32_t idx = 0;
        method_idx_.clear();
        for (const MethodKey& m : method_set_) method_idx_[m] = idx++;

        const uint32_t num_strings = static_cast<uint32_t>(string_set_.size());
        const uint32_t num_types = static_cast<uint32_t>(type_set_.size());
        const uint32_t num_protos = static_cast<uint32_t>(proto_set_.size());
        const uint32_t num_fields = static_cast<uint32_t>(field_set_.size());
        const uint32_t num_methods = static_cast<uint32_t>(method_set_.size());
        const uint32_t num_classes = static_cast<uint32_t>(classes.size());

        constexpr uint32_t kHeaderSize = 0x70;
        const uint32_t string_ids_off = kHeaderSize;
        const uint32_t type_ids_off = string_ids_off + num_strings * 4;
        const uint32_t proto_ids_off = type_ids_off + num_types * 4;
        const uint32_t field_ids_off = proto_ids_off + num_protos * 12;
        const uint32_t method_ids_off = field_ids_off + num_fields * 8;
        const uint32_t class_defs_off = method_ids_off + num_methods * 8;
        const uint32_t data_off = class_defs_off + num_classes * 32;

        std::vector<uint8_t> data;
        const auto pos = [&] { return data_off + static_cast<uint32_t>(data.size()); };
        const auto align = [&](size_t a) {
            while (data.size() % a != 0) data.push_back(0);
        };

        // string_data_item
        std::vector<uint32_t> string_data_offs(num_strings);
        for (const std::string& s : string_set_) {
            string_data_offs[string_idx_[s]] = pos();
            Leb128(&data, static_cast<uint32_t>(s.size()));
            data.insert(data.end(), s.begin(), s.end());
            data.push_back(0);
        }

        // type_list for protos with params and for class interfaces
        std::map<std::vector<uint32_t>, uint32_t> type_list_offs;
        const auto emit_type_list = [&](const std::vector<uint32_t>& types) -> uint32_t {
            if (types.empty()) return 0;
            auto it = type_list_offs.find(types);
            if (it != type_list_offs.end()) return it->second;
            align(4);
            const uint32_t off = pos();
            PutU32(&data, static_cast<uint32_t>(types.size()));
            for (uint32_t t : types) PutU16(&data, static_cast<uint16_t>(t));
            type_list_offs[types] = off;
            return off;
        };
        std::map<ProtoKey, uint32_t> proto_params_off;
        for (const ProtoKey& p : proto_set_) {
            proto_params_off[p] = emit_type_list(p.param_type_idx);
        }
        std::vector<uint32_t> interfaces_off(num_classes, 0);
        for (size_t ci = 0; ci < classes.size(); ++ci) {
            std::vector<uint32_t> iface_types;
            for (const std::string& i : classes[ci].interfaces) {
                iface_types.push_back(type_idx_[i]);
            }
            interfaces_off[ci] = emit_type_list(iface_types);
        }

        // code_item first; class_data_item needs their offsets.
        std::map<const MethodSpec*, uint32_t> code_offs;
        for (const ClassSpec& c : classes) {
            for (const auto* list : {&c.direct_methods, &c.virtual_methods}) {
                for (const MethodSpec& m : *list) {
                    if (m.code.empty()) continue;
                    align(4);
                    code_offs[&m] = pos();
                    PutU16(&data, m.registers_size);
                    PutU16(&data, m.ins_size);
                    PutU16(&data, m.outs_size);
                    PutU16(&data, static_cast<uint16_t>(m.tries.size()));
                    PutU32(&data, 0);  // debug_info_off
                    PutU32(&data, static_cast<uint32_t>(m.code.size()));
                    for (uint16_t u : m.code) PutU16(&data, u);
                    // Padding only when try_items exist: they need 4-byte
                    // alignment but insns may be an odd number of code units.
                    if (!m.tries.empty() && m.code.size() % 2 != 0) PutU16(&data, 0);

                    if (m.tries.empty()) continue;

                    // Handler offsets are relative to the handler list after try_items,
                    // so build the list in a temp buffer first, then write try_items.
                    std::vector<uint8_t> handler_list;
                    Leb128(&handler_list, static_cast<uint32_t>(m.tries.size()));
                    std::vector<uint32_t> handler_offs;
                    for (const TrySpec& t : m.tries) {
                        handler_offs.push_back(static_cast<uint32_t>(handler_list.size()));
                        size_t typed = 0;
                        bool catch_all = false;
                        uint32_t catch_all_pc = 0;
                        for (const CatchSpec& h : t.handlers) {
                            if (h.type.empty()) {
                                catch_all = true;
                                catch_all_pc = h.handler_pc;
                            } else {
                                ++typed;
                            }
                        }
                        // size > 0: typed handlers only. size <= 0: |size|
                        // typed handlers then catch_all_addr.
                        SLeb128(&handler_list,
                                catch_all ? -static_cast<int32_t>(typed)
                                          : static_cast<int32_t>(typed));
                        for (const CatchSpec& h : t.handlers) {
                            if (h.type.empty()) continue;
                            Leb128(&handler_list, type_idx_.at(h.type));
                            Leb128(&handler_list, h.handler_pc);
                        }
                        if (catch_all) Leb128(&handler_list, catch_all_pc);
                    }

                    for (size_t ti = 0; ti < m.tries.size(); ++ti) {
                        PutU32(&data, m.tries[ti].start_addr);
                        PutU16(&data, m.tries[ti].insn_count);
                        PutU16(&data, static_cast<uint16_t>(handler_offs[ti]));
                    }
                    data.insert(data.end(), handler_list.begin(), handler_list.end());
                }
            }
        }

        // class_data_item
        std::vector<uint32_t> class_data_offs(num_classes, 0);
        for (size_t ci = 0; ci < classes.size(); ++ci) {
            const ClassSpec& c = classes[ci];
            if (c.static_fields.empty() && c.instance_fields.empty() &&
                c.direct_methods.empty() && c.virtual_methods.empty()) {
                continue;
            }
            class_data_offs[ci] = pos();
            const uint32_t class_type = type_idx_[c.descriptor];

            Leb128(&data, static_cast<uint32_t>(c.static_fields.size()));
            Leb128(&data, static_cast<uint32_t>(c.instance_fields.size()));
            Leb128(&data, static_cast<uint32_t>(c.direct_methods.size()));
            Leb128(&data, static_cast<uint32_t>(c.virtual_methods.size()));

            // class_data_item stores index diffs, so entries must be ascending.
            const auto emit_fields = [&](const std::vector<FieldSpec>& fields) {
                std::vector<std::pair<uint32_t, const FieldSpec*>> sorted;
                for (const FieldSpec& f : fields) {
                    const FieldKey key{class_type, type_idx_[f.type], string_idx_[f.name]};
                    sorted.emplace_back(field_idx_[key], &f);
                }
                std::sort(sorted.begin(), sorted.end(),
                          [](const auto& a, const auto& b) { return a.first < b.first; });
                uint32_t prev = 0;
                for (const auto& [fi, f] : sorted) {
                    Leb128(&data, fi - prev);
                    prev = fi;
                    Leb128(&data, f->access_flags);
                }
            };
            const auto emit_methods = [&](const std::vector<MethodSpec>& methods) {
                std::vector<std::pair<uint32_t, const MethodSpec*>> sorted;
                for (const MethodSpec& m : methods) {
                    const MethodKey key{class_type, proto_idx_[MakeProtoKey(m)],
                                        string_idx_[m.name]};
                    sorted.emplace_back(method_idx_[key], &m);
                }
                std::sort(sorted.begin(), sorted.end(),
                          [](const auto& a, const auto& b) { return a.first < b.first; });
                uint32_t prev = 0;
                for (const auto& [mi, m] : sorted) {
                    Leb128(&data, mi - prev);
                    prev = mi;
                    Leb128(&data, m->access_flags);
                    auto it = code_offs.find(m);
                    Leb128(&data, it != code_offs.end() ? it->second : 0);
                }
            };
            emit_fields(c.static_fields);
            emit_fields(c.instance_fields);
            emit_methods(c.direct_methods);
            emit_methods(c.virtual_methods);
        }

        // map_list
        align(4);
        const uint32_t map_off = pos();
        struct MapEntry { uint16_t type; uint32_t size; uint32_t offset; };
        std::vector<MapEntry> map_entries = {
            {0x0000, 1, 0},
            {0x0001, num_strings, string_ids_off},
            {0x0002, num_types, type_ids_off},
        };
        if (num_protos > 0) map_entries.push_back({0x0003, num_protos, proto_ids_off});
        if (num_fields > 0) map_entries.push_back({0x0004, num_fields, field_ids_off});
        if (num_methods > 0) map_entries.push_back({0x0005, num_methods, method_ids_off});
        map_entries.push_back({0x0006, num_classes, class_defs_off});
        map_entries.push_back({0x1000, 1, map_off});
        PutU32(&data, static_cast<uint32_t>(map_entries.size()));
        for (const MapEntry& m : map_entries) {
            PutU16(&data, m.type);
            PutU16(&data, 0);
            PutU32(&data, m.size);
            PutU32(&data, m.offset);
        }

        const uint32_t data_size = static_cast<uint32_t>(data.size());
        const uint32_t file_size = data_off + data_size;

        std::vector<uint8_t> out;
        out.reserve(file_size);
        const uint8_t kMagic[8] = {'d', 'e', 'x', '\n', '0', '3', '5', '\0'};
        out.insert(out.end(), kMagic, kMagic + 8);
        PutU32(&out, 0);                  // checksum, filled in later
        out.resize(out.size() + 20, 0);   // SHA-1 signature, not verified
        PutU32(&out, file_size);
        PutU32(&out, kHeaderSize);
        PutU32(&out, 0x12345678);
        PutU32(&out, 0);
        PutU32(&out, 0);
        PutU32(&out, map_off);
        PutU32(&out, num_strings);
        PutU32(&out, num_strings > 0 ? string_ids_off : 0);
        PutU32(&out, num_types);
        PutU32(&out, num_types > 0 ? type_ids_off : 0);
        PutU32(&out, num_protos);
        PutU32(&out, num_protos > 0 ? proto_ids_off : 0);
        PutU32(&out, num_fields);
        PutU32(&out, num_fields > 0 ? field_ids_off : 0);
        PutU32(&out, num_methods);
        PutU32(&out, num_methods > 0 ? method_ids_off : 0);
        PutU32(&out, num_classes);
        PutU32(&out, num_classes > 0 ? class_defs_off : 0);
        PutU32(&out, data_size);
        PutU32(&out, data_off);

        for (uint32_t i = 0; i < num_strings; ++i) PutU32(&out, string_data_offs[i]);

        std::vector<uint32_t> type_descriptor_idx(num_types);
        for (const std::string& t : type_set_) type_descriptor_idx[type_idx_[t]] = string_idx_[t];
        for (uint32_t i = 0; i < num_types; ++i) PutU32(&out, type_descriptor_idx[i]);

        for (const ProtoKey& p : proto_set_) {
            PutU32(&out, string_idx_[p.shorty]);
            PutU16(&out, static_cast<uint16_t>(p.return_type_idx));
            PutU16(&out, 0);
            PutU32(&out, proto_params_off[p]);
        }
        for (const FieldKey& f : field_set_) {
            PutU16(&out, static_cast<uint16_t>(f.class_idx));
            PutU16(&out, static_cast<uint16_t>(f.type_idx));
            PutU32(&out, f.name_idx);
        }
        for (const MethodKey& m : method_set_) {
            PutU16(&out, static_cast<uint16_t>(m.class_idx));
            PutU16(&out, static_cast<uint16_t>(m.proto_idx));
            PutU32(&out, m.name_idx);
        }

        for (size_t ci = 0; ci < classes.size(); ++ci) {
            const ClassSpec& c = classes[ci];
            PutU16(&out, static_cast<uint16_t>(type_idx_[c.descriptor]));
            PutU16(&out, 0);
            PutU32(&out, c.access_flags);
            if (c.superclass.empty()) {
                PutU16(&out, 0xFFFF);  // NO_INDEX
            } else {
                PutU16(&out, static_cast<uint16_t>(type_idx_[c.superclass]));
            }
            PutU16(&out, 0);
            PutU32(&out, interfaces_off[ci]);
            PutU32(&out, 0xFFFFFFFF);  // source_file_idx = NO_INDEX
            PutU32(&out, 0);           // annotations_off
            PutU32(&out, class_data_offs[ci]);
            PutU32(&out, 0);           // static_values_off
        }

        out.insert(out.end(), data.begin(), data.end());

        const uint32_t adler = adler32(adler32(0L, nullptr, 0), out.data() + 12,
                                      static_cast<uInt>(out.size() - 12));
        std::memcpy(out.data() + 8, &adler, sizeof(adler));
        return out;
    }

    static void PutU16(std::vector<uint8_t>* v, uint16_t x) {
        v->push_back(static_cast<uint8_t>(x & 0xFF));
        v->push_back(static_cast<uint8_t>(x >> 8));
    }
    static void PutU32(std::vector<uint8_t>* v, uint32_t x) {
        PutU16(v, static_cast<uint16_t>(x & 0xFFFF));
        PutU16(v, static_cast<uint16_t>(x >> 16));
    }
    static void Leb128(std::vector<uint8_t>* v, uint32_t value) {
        do {
            uint8_t byte = value & 0x7F;
            value >>= 7;
            if (value != 0) byte |= 0x80;
            v->push_back(byte);
        } while (value != 0);
    }
    static void SLeb128(std::vector<uint8_t>* v, int32_t value) {
        bool more = true;
        while (more) {
            uint8_t byte = value & 0x7F;
            value >>= 7;  // arithmetic shift: -1 stays -1
            // The byte's sign bit must match the rest, else emit another byte.
            if ((value == 0 && (byte & 0x40) == 0) || (value == -1 && (byte & 0x40) != 0)) {
                more = false;
            } else {
                byte |= 0x80;
            }
            v->push_back(byte);
        }
    }

    struct PendingMethod {
        uint32_t class_idx;
        ProtoKey proto;
        uint32_t name_idx;
    };

    std::set<std::string> string_set_;
    std::set<std::string> type_set_;
    std::set<ProtoKey> proto_set_;
    std::set<FieldKey> field_set_;
    std::set<MethodKey> method_set_;
    std::vector<PendingMethod> pending_methods_;

    std::map<std::string, uint32_t> string_idx_;
    std::map<std::string, uint32_t> type_idx_;
    std::map<ProtoKey, uint32_t> proto_idx_;
    std::map<FieldKey, uint32_t> field_idx_;
    std::map<MethodKey, uint32_t> method_idx_;
};

}  // namespace dexbuild

#endif  // KUDROID_TESTS_DEX_BUILDER_H
