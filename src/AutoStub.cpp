#include "kudroid/AutoStub.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <set>
#include <string>
#include <vector>
#include <zlib.h>

namespace kudroid {
namespace {

// ── Little-endian helpers (ZIP) ────────────────────────────────────────────
std::uint16_t rd16(const std::uint8_t* d, std::size_t o) {
    return static_cast<std::uint16_t>(d[o] | (d[o + 1] << 8));
}
std::uint32_t rd32(const std::uint8_t* d, std::size_t o) {
    return static_cast<std::uint32_t>(d[o]) | (static_cast<std::uint32_t>(d[o + 1]) << 8) |
           (static_cast<std::uint32_t>(d[o + 2]) << 16) |
           (static_cast<std::uint32_t>(d[o + 3]) << 24);
}
void le16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
}
void le32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    le16(v, static_cast<std::uint16_t>(x & 0xFFFF));
    le16(v, static_cast<std::uint16_t>(x >> 16));
}

// ── Big-endian helpers (class file) ───────────────────────────────────────
void be16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
}
void be32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    be16(v, static_cast<std::uint16_t>(x >> 16));
    be16(v, static_cast<std::uint16_t>(x & 0xFFFF));
}
void beUtf8(std::vector<std::uint8_t>& v, const char* s) {
    const std::size_t n = std::strlen(s);
    v.push_back(1);
    be16(v, static_cast<std::uint16_t>(n));
    v.insert(v.end(), s, s + n);
}

// ── ZIP reading ───────────────────────────────────────────────────────────
struct ZipEntryRef {
    std::string name;
    std::uint16_t method = 0;
    std::uint32_t compSize = 0;
    std::uint32_t localOffset = 0;
};

bool readCentralDirectory(const std::string& path, std::vector<ZipEntryRef>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    if (size < 22) { fclose(f); return false; }

    const std::size_t tailLen = size < 65557 ? static_cast<std::size_t>(size) : 65557;
    std::vector<std::uint8_t> tail(tailLen);
    fseek(f, static_cast<long>(size - static_cast<long>(tailLen)), SEEK_SET);
    if (fread(tail.data(), 1, tailLen, f) != tailLen) { fclose(f); return false; }

    std::size_t eocd = tailLen;
    for (std::size_t i = tailLen; i >= 22; --i) {
        if (rd32(tail.data(), i - 22) == 0x06054b50) { eocd = i - 22; break; }
    }
    if (eocd == tailLen) { fclose(f); return false; }
    const std::uint16_t count = rd16(tail.data(), eocd + 10);
    const std::uint32_t cdOffset = rd32(tail.data(), eocd + 16);
    if (cdOffset == 0 || cdOffset >= static_cast<std::uint32_t>(size)) { fclose(f); return false; }

    const std::size_t cdLen = static_cast<std::size_t>(size) - cdOffset;
    std::vector<std::uint8_t> cd(cdLen);
    fseek(f, static_cast<long>(cdOffset), SEEK_SET);
    if (fread(cd.data(), 1, cdLen, f) != cdLen) { fclose(f); return false; }
    fclose(f);

    std::size_t p = 0;
    for (std::uint16_t i = 0; i < count && p + 46 <= cd.size(); ++i) {
        if (rd32(cd.data(), p) != 0x02014b50) break;
        ZipEntryRef e;
        e.method = rd16(cd.data(), p + 10);
        e.compSize = rd32(cd.data(), p + 20);
        const std::uint16_t nameLen = rd16(cd.data(), p + 28);
        const std::uint16_t extraLen = rd16(cd.data(), p + 30);
        const std::uint16_t commentLen = rd16(cd.data(), p + 32);
        e.localOffset = rd32(cd.data(), p + 42);
        if (p + 46u + nameLen > cd.size()) break;
        e.name.assign(reinterpret_cast<const char*>(cd.data()) + p + 46, nameLen);
        out.push_back(std::move(e));
        p += 46u + nameLen + extraLen + commentLen;
    }
    return true;
}

bool extractEntry(FILE* f, const ZipEntryRef& e, std::vector<std::uint8_t>& out) {
    std::uint8_t lh[30];
    if (fseek(f, static_cast<long>(e.localOffset), SEEK_SET) != 0) return false;
    if (fread(lh, 1, 30, f) != 30 || rd32(lh, 0) != 0x04034b50) return false;
    const std::uint16_t lNameLen = rd16(lh, 26);
    const std::uint16_t lExtraLen = rd16(lh, 28);
    if (fseek(f, static_cast<long>(e.localOffset) + 30 + lNameLen + lExtraLen, SEEK_SET) != 0)
        return false;

    std::vector<std::uint8_t> comp(e.compSize);
    if (e.compSize > 0 && fread(comp.data(), 1, e.compSize, f) != e.compSize) return false;

    if (e.method == 0) { out = std::move(comp); return true; }
    if (e.method != 8) return false;

    z_stream s = {};
    if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return false;
    s.next_in = comp.data();
    s.avail_in = static_cast<uInt>(comp.size());
    out.clear();
    std::uint8_t buf[16384];
    int rc = Z_OK;
    while (rc == Z_OK) {
        s.next_out = buf;
        s.avail_out = sizeof(buf);
        rc = inflate(&s, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) break;
        out.insert(out.end(), buf, buf + (sizeof(buf) - s.avail_out));
    }
    inflateEnd(&s);
    return rc == Z_STREAM_END;
}

// ── Class file parsing: constant pool + hierarchy ─────────────────────────
class BeReader {
public:
    BeReader(const std::uint8_t* d, std::size_t n) : d_(d), n_(n) {}
    bool u1(std::uint8_t& v) { if (p_ + 1 > n_) return false; v = d_[p_++]; return true; }
    bool u2(std::uint16_t& v) {
        std::uint8_t a, b;
        if (!u1(a) || !u1(b)) return false;
        v = static_cast<std::uint16_t>((a << 8) | b);
        return true;
    }
    bool u4(std::uint32_t& v) {
        std::uint16_t a, b;
        if (!u2(a) || !u2(b)) return false;
        v = (static_cast<std::uint32_t>(a) << 16) | b;
        return true;
    }
    bool skip(std::size_t k) { if (p_ + k > n_) return false; p_ += k; return true; }
    std::size_t pos() const { return p_; }
    std::size_t size() const { return n_; }
    const std::uint8_t* data() const { return d_; }

private:
    const std::uint8_t* d_;
    std::size_t n_;
    std::size_t p_ = 0;
};

bool isAndroidName(const std::string& slash) {
    return slash.compare(0, 8, "android/") == 0 || slash.compare(0, 9, "androidx/") == 0;
}

std::string toDotted(std::string s) {
    for (char& c : s) if (c == '/') c = '.';
    return s;
}

/**
 * Đọc một .class của app và ghi nhận:
 *  - allRefs:   MỌI class android/* xuất hiện trong constant pool
 *  - asIface:   class android/* xuất hiện trong bảng `interfaces` (phải là interface)
 *  - asSuper:   class android/* là super_class (phải là class thường)
 */
bool analyzeClass(const std::vector<std::uint8_t>& bytes,
                  std::set<std::string>& allRefs,
                  std::set<std::string>& asIface,
                  std::set<std::string>& asSuper) {
    BeReader r(bytes.data(), bytes.size());
    std::uint32_t magic;
    std::uint16_t minor, major, cpCount;
    if (!r.u4(magic) || magic != 0xCAFEBABE) return false;
    if (!r.u2(minor) || !r.u2(major) || !r.u2(cpCount) || cpCount == 0) return false;

    struct Entry {
        std::uint8_t tag = 0;
        std::string utf8;
        std::uint16_t nameIdx = 0;
    };
    std::vector<Entry> pool(cpCount);
    for (std::uint16_t i = 1; i < cpCount; ++i) {
        std::uint8_t tag;
        if (!r.u1(tag)) return false;
        pool[i].tag = tag;
        switch (tag) {
            case 1: { // CONSTANT_Utf8
                std::uint16_t len;
                if (!r.u2(len)) return false;
                if (len > 0) {
                    if (r.pos() + len > r.size()) return false;
                    pool[i].utf8.assign(reinterpret_cast<const char*>(r.data() + r.pos()), len);
                    if (!r.skip(len)) return false;
                }
                break;
            }
            case 7:  // Class
            case 8:  // String
            case 16: // MethodType
            case 19: // Module
            case 20: // Package
                if (!r.u2(pool[i].nameIdx)) return false;
                break;
            case 15: // MethodHandle: u1 kind + u2 ref
                if (!r.skip(3)) return false;
                break;
            case 3:  // Integer
            case 4:  // Float
                if (!r.skip(4)) return false;
                break;
            case 5:  // Long  — chiếm 2 slot
            case 6:  // Double
                if (!r.skip(8)) return false;
                ++i;
                break;
            default: // Fieldref/Methodref/InterfaceMethodref/NameAndType/InvokeDynamic...
                if (!r.skip(4)) return false;
                break;
        }
    }

    auto classNameAt = [&](std::uint16_t idx) -> std::string {
        if (idx == 0 || idx >= pool.size() || pool[idx].tag != 7) return std::string();
        const std::uint16_t ni = pool[idx].nameIdx;
        if (ni == 0 || ni >= pool.size() || pool[ni].tag != 1) return std::string();
        return pool[ni].utf8;
    };

    // Mọi Class entry trong pool → allRefs.
    for (std::uint16_t i = 1; i < cpCount; ++i) {
        if (pool[i].tag != 7) continue;
        std::string n = classNameAt(i);
        // Bỏ array descriptor ("[Landroid/...;") — JVM không cần stub cho array.
        if (n.empty() || n[0] == '[') continue;
        if (isAndroidName(n)) allRefs.insert(toDotted(n));
    }

    std::uint16_t accessFlags, thisClass, superClass, ifaceCount;
    if (!r.u2(accessFlags) || !r.u2(thisClass) || !r.u2(superClass)) return true;
    std::string sup = classNameAt(superClass);
    if (!sup.empty() && isAndroidName(sup)) asSuper.insert(toDotted(sup));

    if (!r.u2(ifaceCount)) return true;
    for (std::uint16_t i = 0; i < ifaceCount; ++i) {
        std::uint16_t idx;
        if (!r.u2(idx)) break;
        std::string n = classNameAt(idx);
        if (!n.empty() && isAndroidName(n)) asIface.insert(toDotted(n));
    }
    return true;
}

// ── Stub .class builder ───────────────────────────────────────────────────
/**
 * Sinh .class binary tối thiểu, đủ để JVM resolve:
 *   class:     public class <Name> extends Object { public <Name>() {} }
 *   interface: public interface <Name> {}
 *
 * Constant pool cố định (index 1..11):
 *   1 Utf8 <name>      2 Class #1
 *   3 Utf8 java/lang/Object   4 Class #3
 *   5 Utf8 <init>      6 Utf8 ()V     7 Utf8 Code
 *   8 NameAndType #5#6 9 Methodref #4#8
 *   10 Utf8 SourceFile 11 Utf8 AutoStub
 */
std::vector<std::uint8_t> buildStubClass(const std::string& slashName, bool isInterface) {
    std::vector<std::uint8_t> cp;
    // #1 Utf8 name
    cp.push_back(1);
    be16(cp, static_cast<std::uint16_t>(slashName.size()));
    cp.insert(cp.end(), slashName.begin(), slashName.end());
    // #2 Class → #1
    cp.push_back(7); be16(cp, 1);
    // #3 Utf8 java/lang/Object ; #4 Class → #3
    beUtf8(cp, "java/lang/Object");
    cp.push_back(7); be16(cp, 3);
    // #5 <init> ; #6 ()V ; #7 Code
    beUtf8(cp, "<init>");
    beUtf8(cp, "()V");
    beUtf8(cp, "Code");
    // #8 NameAndType #5 #6
    cp.push_back(12); be16(cp, 5); be16(cp, 6);
    // #9 Methodref #4 #8
    cp.push_back(10); be16(cp, 4); be16(cp, 8);
    // #10 SourceFile ; #11 AutoStub
    beUtf8(cp, "SourceFile");
    beUtf8(cp, "AutoStub");

    std::vector<std::uint8_t> out;
    be32(out, 0xCAFEBABE);
    be16(out, 0);   // minor_version
    be16(out, 52);  // major_version = Java 8
    be16(out, 12);  // constant_pool_count = last_index(11) + 1
    out.insert(out.end(), cp.begin(), cp.end());

    // ACC_PUBLIC|ACC_INTERFACE|ACC_ABSTRACT hoặc ACC_PUBLIC|ACC_SUPER
    be16(out, isInterface ? 0x0601 : 0x0021);
    be16(out, 2); // this_class  → #2
    be16(out, 4); // super_class → Object (interface cũng vậy, đúng spec)
    be16(out, 0); // interfaces_count
    be16(out, 0); // fields_count

    if (isInterface) {
        be16(out, 0); // methods_count
    } else {
        be16(out, 1);      // methods_count
        be16(out, 0x0001); // ACC_PUBLIC
        be16(out, 5);      // name_index <init>
        be16(out, 6);      // descriptor_index ()V
        be16(out, 1);      // attributes_count
        be16(out, 7);      // attribute_name_index = Code
        // Code attr body: max_stack(2)+max_locals(2)+code_length(4)+code(5)
        //                 +exception_table_length(2)+attributes_count(2) = 17
        be32(out, 17);
        be16(out, 1); // max_stack
        be16(out, 1); // max_locals
        be32(out, 5); // code_length
        out.push_back(0x2A);       // aload_0
        out.push_back(0xB7);       // invokespecial
        be16(out, 9);              //   → Methodref #9 (Object.<init>)
        out.push_back(0xB1);       // return
        be16(out, 0); // exception_table_length
        be16(out, 0); // attributes_count
    }

    be16(out, 1);  // class attributes_count = SourceFile
    be16(out, 10); // attribute_name_index
    be32(out, 2);  // attribute_length
    be16(out, 11); // sourcefile_index

    return out;
}

// ── Stub JAR writer (store, không nén) ────────────────────────────────────
void appendZipEntry(std::vector<std::uint8_t>& zip, std::vector<std::uint8_t>& central,
                    const std::string& name, const std::vector<std::uint8_t>& data) {
    const std::uint32_t crc = static_cast<std::uint32_t>(
        crc32(crc32(0L, Z_NULL, 0), data.data(), static_cast<uInt>(data.size())));
    const std::uint32_t localOff = static_cast<std::uint32_t>(zip.size());
    const std::uint32_t sz = static_cast<std::uint32_t>(data.size());

    le32(zip, 0x04034b50);
    le16(zip, 20); le16(zip, 0); le16(zip, 0); // ver / flags / method=store
    le16(zip, 0);  le16(zip, 0);               // time / date
    le32(zip, crc); le32(zip, sz); le32(zip, sz);
    le16(zip, static_cast<std::uint16_t>(name.size()));
    le16(zip, 0);
    zip.insert(zip.end(), name.begin(), name.end());
    zip.insert(zip.end(), data.begin(), data.end());

    le32(central, 0x02014b50);
    le16(central, 20); le16(central, 20);
    le16(central, 0);  le16(central, 0);
    le16(central, 0);  le16(central, 0);
    le32(central, crc); le32(central, sz); le32(central, sz);
    le16(central, static_cast<std::uint16_t>(name.size()));
    le16(central, 0); le16(central, 0); le16(central, 0); le16(central, 0);
    le32(central, 0);
    le32(central, localOff);
    central.insert(central.end(), name.begin(), name.end());
}

} // namespace

int AutoStub::build_stub_jar(const std::string& appJarPath,
                             const std::string& outStubJarPath) {
    std::vector<ZipEntryRef> entries;
    if (!readCentralDirectory(appJarPath, entries)) {
        std::fprintf(stderr, "[kudroid_autostub] cannot read jar: %s\n", appJarPath.c_str());
        return 0;
    }

    FILE* f = fopen(appJarPath.c_str(), "rb");
    if (!f) return 0;

    std::set<std::string> referenced, asIface, asSuper, present;
    std::size_t scanned = 0;
    for (const auto& e : entries) {
        if (e.name.size() < 7 || e.name.compare(e.name.size() - 6, 6, ".class") != 0) continue;
        if (isAndroidName(e.name)) {
            // Ghi nhận là CÓ SẴN, nhưng VẪN PHẢI PARSE tiếp bên dưới:
            // chính các class framework (Paint, Canvas...) là nơi tham chiếu
            // những class khác còn thiếu (PorterDuffColorFilter...). Bỏ qua
            // parse ở đây = mất toàn bộ chuỗi tham chiếu từ framework.
            present.insert(toDotted(e.name.substr(0, e.name.size() - 6)));
        }
        std::vector<std::uint8_t> bytes;
        if (!extractEntry(f, e, bytes)) continue;
        analyzeClass(bytes, referenced, asIface, asSuper);
        ++scanned;
    }
    fclose(f);

    std::vector<std::string> missing;
    std::set_difference(referenced.begin(), referenced.end(), present.begin(), present.end(),
                        std::back_inserter(missing));

    std::fprintf(stderr,
                 "[kudroid_autostub] scanned %zu app classes: %zu android refs, "
                 "%zu present, %zu MISSING\n",
                 scanned, referenced.size(), present.size(), missing.size());
    if (missing.empty()) return 0;

    std::vector<std::uint8_t> zip, central;
    int count = 0;
    for (const auto& dotted : missing) {
        std::string slash = dotted;
        for (char& c : slash) if (c == '.') c = '/';
        // Nếu app `implements` nó → phải sinh interface, ngược lại sinh class.
        // Sai loại sẽ gây IncompatibleClassChangeError nên phải phân biệt.
        const bool isInterface = asIface.count(dotted) > 0 && asSuper.count(dotted) == 0;
        appendZipEntry(zip, central, slash + ".class", buildStubClass(slash, isInterface));
        if (count < 15) {
            std::fprintf(stderr, "[kudroid_autostub]   + %s%s\n", dotted.c_str(),
                         isInterface ? " (interface)" : "");
        }
        ++count;
    }

    const std::uint32_t cdOffset = static_cast<std::uint32_t>(zip.size());
    zip.insert(zip.end(), central.begin(), central.end());
    const std::uint32_t cdSize = static_cast<std::uint32_t>(central.size());
    le32(zip, 0x06054b50);
    le16(zip, 0); le16(zip, 0);
    le16(zip, static_cast<std::uint16_t>(count));
    le16(zip, static_cast<std::uint16_t>(count));
    le32(zip, cdSize);
    le32(zip, cdOffset);
    le16(zip, 0); // comment length

    FILE* o = fopen(outStubJarPath.c_str(), "wb");
    if (!o) {
        std::fprintf(stderr, "[kudroid_autostub] cannot write %s\n", outStubJarPath.c_str());
        return 0;
    }
    fwrite(zip.data(), 1, zip.size(), o);
    fclose(o);

    std::fprintf(stderr, "[kudroid_autostub] wrote %d stubs -> %s (%zu bytes)\n",
                 count, outStubJarPath.c_str(), zip.size());
    return count;
}

} // namespace kudroid
