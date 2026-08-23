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

// ── ZIP: little-endian ────────────────────────────────────────────────────
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

// ── Class file: big-endian ────────────────────────────────────────────────
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

bool isAndroidName(const std::string& slash) {
    return slash.compare(0, 8, "android/") == 0 || slash.compare(0, 9, "androidx/") == 0;
}
std::string toDotted(std::string s) {
    for (char& c : s) if (c == '/') c = '.';
    return s;
}
bool endsWithClass(const std::string& n) {
    return n.size() >= 7 && n.compare(n.size() - 6, 6, ".class") == 0;
}

// ── Jar layout ───────────────────────────────────────────────────────────
struct JarEntry {
    std::string name;
    std::uint16_t method = 0;
    std::uint32_t compSize = 0;
    std::uint32_t localOffset = 0;
};

struct JarLayout {
    std::vector<std::uint8_t> bytes;      // toàn bộ nội dung file
    std::vector<JarEntry> entries;
    std::size_t cdOffset = 0;             // đầu central directory
    std::size_t cdSize = 0;               // tổng byte của các CD record
    std::uint16_t entryCount = 0;
};

bool readJar(const std::string& path, JarLayout& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    if (size < 22) { fclose(f); return false; }
    fseek(f, 0, SEEK_SET);
    out.bytes.resize(static_cast<std::size_t>(size));
    const bool readOk = fread(out.bytes.data(), 1, out.bytes.size(), f) == out.bytes.size();
    fclose(f);
    if (!readOk) return false;

    const std::uint8_t* d = out.bytes.data();
    const std::size_t n = out.bytes.size();

    std::size_t eocd = n;
    const std::size_t scanFrom = n > 65557 ? n - 65557 : 0;
    for (std::size_t i = n - 22 + 1; i-- > scanFrom;) {
        if (rd32(d, i) == 0x06054b50) { eocd = i; break; }
    }
    if (eocd == n) return false;

    out.entryCount = rd16(d, eocd + 10);
    out.cdSize = rd32(d, eocd + 12);
    out.cdOffset = rd32(d, eocd + 16);
    if (out.cdOffset + out.cdSize > n) return false;

    std::size_t p = out.cdOffset;
    for (std::uint16_t i = 0; i < out.entryCount && p + 46 <= n; ++i) {
        if (rd32(d, p) != 0x02014b50) break;
        JarEntry e;
        e.method = rd16(d, p + 10);
        e.compSize = rd32(d, p + 20);
        const std::uint16_t nameLen = rd16(d, p + 28);
        const std::uint16_t extraLen = rd16(d, p + 30);
        const std::uint16_t commentLen = rd16(d, p + 32);
        e.localOffset = rd32(d, p + 42);
        if (p + 46u + nameLen > n) break;
        e.name.assign(reinterpret_cast<const char*>(d) + p + 46, nameLen);
        out.entries.push_back(std::move(e));
        p += 46u + nameLen + extraLen + commentLen;
    }
    return !out.entries.empty();
}

bool inflateEntry(const JarLayout& jar, const JarEntry& e, std::vector<std::uint8_t>& out) {
    const std::uint8_t* d = jar.bytes.data();
    const std::size_t n = jar.bytes.size();
    if (e.localOffset + 30 > n || rd32(d, e.localOffset) != 0x04034b50) return false;
    const std::size_t dataAt = e.localOffset + 30u + rd16(d, e.localOffset + 26) +
                               rd16(d, e.localOffset + 28);
    if (dataAt + e.compSize > n) return false;

    if (e.method == 0) {
        out.assign(d + dataAt, d + dataAt + e.compSize);
        return true;
    }
    if (e.method != 8) return false;

    z_stream s = {};
    if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return false;
    s.next_in = const_cast<Bytef*>(d + dataAt);
    s.avail_in = static_cast<uInt>(e.compSize);
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

// ── Class file: thu thập tham chiếu android/* ────────────────────────────
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

// Tên class trong descriptor phải là identifier dạng path: "a/b/C", "a/b/C$D".
bool looksLikeClassPath(const std::string& n) {
    if (n.empty() || n.find('/') == std::string::npos) return false;
    for (const char c : n) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '$';
        if (!ok) return false;
    }
    return true;
}

struct ClassRefs {
    std::set<std::string> all;      // mọi android/* được tham chiếu
    std::set<std::string> asIface;  // xuất hiện trong bảng `interfaces`
    std::set<std::string> asSuper;  // xuất hiện ở super_class
};

bool collectRefs(const std::vector<std::uint8_t>& bytes, ClassRefs& refs) {
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
            case 1: { // Utf8
                std::uint16_t len;
                if (!r.u2(len)) return false;
                if (len > 0) {
                    if (r.pos() + len > r.size()) return false;
                    pool[i].utf8.assign(reinterpret_cast<const char*>(r.data() + r.pos()), len);
                    if (!r.skip(len)) return false;
                }
                break;
            }
            case 7: case 8: case 16: case 19: case 20: // Class/String/MT/Module/Pkg
                if (!r.u2(pool[i].nameIdx)) return false;
                break;
            case 15: // MethodHandle
                if (!r.skip(3)) return false;
                break;
            case 3: case 4: // Integer/Float
                if (!r.skip(4)) return false;
                break;
            case 5: case 6: // Long/Double chiếm 2 slot
                if (!r.skip(8)) return false;
                ++i;
                break;
            default: // Fieldref/Methodref/NameAndType/InvokeDynamic...
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

    for (std::uint16_t i = 1; i < cpCount; ++i) {
        if (pool[i].tag == 7) {
            const std::string n = classNameAt(i);
            if (!n.empty() && n[0] != '[' && isAndroidName(n)) refs.all.insert(toDotted(n));
            continue;
        }
        // Class chỉ dùng qua signature KHÔNG có CONSTANT_Class riêng — tên nằm
        // trong descriptor ("Landroid/graphics/PorterDuffColorFilter;").
        if (pool[i].tag != 1) continue;
        const std::string& u = pool[i].utf8;
        for (std::size_t pos = 0; (pos = u.find('L', pos)) != std::string::npos;) {
            const std::size_t semi = u.find(';', pos);
            if (semi == std::string::npos) break;
            const std::string n = u.substr(pos + 1, semi - pos - 1);
            pos = semi + 1;
            if (looksLikeClassPath(n) && isAndroidName(n)) refs.all.insert(toDotted(n));
        }
    }

    std::uint16_t accessFlags, thisClass, superClass, ifaceCount;
    if (!r.u2(accessFlags) || !r.u2(thisClass) || !r.u2(superClass)) return true;
    const std::string sup = classNameAt(superClass);
    if (!sup.empty() && isAndroidName(sup)) refs.asSuper.insert(toDotted(sup));

    if (!r.u2(ifaceCount)) return true;
    for (std::uint16_t i = 0; i < ifaceCount; ++i) {
        std::uint16_t idx;
        if (!r.u2(idx)) break;
        const std::string n = classNameAt(idx);
        if (!n.empty() && isAndroidName(n)) refs.asIface.insert(toDotted(n));
    }
    return true;
}

// ── Sinh .class tối thiểu ────────────────────────────────────────────────
// Constant pool cố định:
//   1 Utf8 <name>    2 Class #1
//   3 Utf8 Object    4 Class #3
//   5 Utf8 <init>    6 Utf8 ()V     7 Utf8 Code
//   8 NameAndType #5#6               9 Methodref #4#8
//  10 Utf8 SourceFile              11 Utf8 AutoStub
std::vector<std::uint8_t> buildStubClass(const std::string& slashName, bool isInterface) {
    std::vector<std::uint8_t> cp;
    cp.push_back(1);
    be16(cp, static_cast<std::uint16_t>(slashName.size()));
    cp.insert(cp.end(), slashName.begin(), slashName.end());
    cp.push_back(7); be16(cp, 1);
    beUtf8(cp, "java/lang/Object");
    cp.push_back(7); be16(cp, 3);
    beUtf8(cp, "<init>");
    beUtf8(cp, "()V");
    beUtf8(cp, "Code");
    cp.push_back(12); be16(cp, 5); be16(cp, 6);
    cp.push_back(10); be16(cp, 4); be16(cp, 8);
    beUtf8(cp, "SourceFile");
    beUtf8(cp, "AutoStub");

    std::vector<std::uint8_t> out;
    be32(out, 0xCAFEBABE);
    be16(out, 0);
    be16(out, 52);  // Java 8
    be16(out, 12);  // cp_count = 11 + 1
    out.insert(out.end(), cp.begin(), cp.end());

    be16(out, isInterface ? 0x0601 : 0x0021); // PUBLIC|INTERFACE|ABSTRACT / PUBLIC|SUPER
    be16(out, 2); // this_class
    be16(out, 4); // super_class = Object (interface cũng vậy, đúng spec)
    be16(out, 0); // interfaces_count
    be16(out, 0); // fields_count

    if (isInterface) {
        be16(out, 0);
    } else {
        be16(out, 1);      // methods_count
        be16(out, 0x0001); // ACC_PUBLIC
        be16(out, 5);      // <init>
        be16(out, 6);      // ()V
        be16(out, 1);      // attributes_count
        be16(out, 7);      // Code
        be32(out, 17);     // 2+2+4+5+2+2
        be16(out, 1);      // max_stack
        be16(out, 1);      // max_locals
        be32(out, 5);      // code_length
        out.push_back(0x2A);       // aload_0
        out.push_back(0xB7);       // invokespecial
        be16(out, 9);              //   Object.<init>
        out.push_back(0xB1);       // return
        be16(out, 0);      // exception_table_length
        be16(out, 0);      // attributes_count
    }

    be16(out, 1);  // class attributes = SourceFile
    be16(out, 10);
    be32(out, 2);
    be16(out, 11);
    return out;
}

// ── Ghi entry mới vào jar (method = store) ──────────────────────────────
void emitStoredEntry(std::vector<std::uint8_t>& local, std::vector<std::uint8_t>& central,
                     const std::string& name, const std::vector<std::uint8_t>& data,
                     std::uint32_t localOffsetInFile) {
    const std::uint32_t crc = static_cast<std::uint32_t>(
        crc32(crc32(0L, Z_NULL, 0), data.data(), static_cast<uInt>(data.size())));
    const std::uint32_t sz = static_cast<std::uint32_t>(data.size());

    le32(local, 0x04034b50);
    le16(local, 20); le16(local, 0); le16(local, 0); // version / flags / store
    le16(local, 0);  le16(local, 0);                 // time / date
    le32(local, crc); le32(local, sz); le32(local, sz);
    le16(local, static_cast<std::uint16_t>(name.size()));
    le16(local, 0);
    local.insert(local.end(), name.begin(), name.end());
    local.insert(local.end(), data.begin(), data.end());

    le32(central, 0x02014b50);
    le16(central, 20); le16(central, 20);
    le16(central, 0);  le16(central, 0);
    le16(central, 0);  le16(central, 0);
    le32(central, crc); le32(central, sz); le32(central, sz);
    le16(central, static_cast<std::uint16_t>(name.size()));
    le16(central, 0); le16(central, 0); le16(central, 0); le16(central, 0);
    le32(central, 0);
    le32(central, localOffsetInFile);
    central.insert(central.end(), name.begin(), name.end());
}

} // namespace

int AutoStub::append_missing_stubs(const std::string& jarPath) {
    JarLayout jar;
    if (!readJar(jarPath, jar)) {
        std::fprintf(stderr, "[kudroid_autostub] cannot read jar: %s\n", jarPath.c_str());
        return 0;
    }

    ClassRefs refs;
    std::set<std::string> present;
    std::size_t scanned = 0;
    for (const auto& e : jar.entries) {
        if (!endsWithClass(e.name)) continue;
        if (isAndroidName(e.name)) {
            present.insert(toDotted(e.name.substr(0, e.name.size() - 6)));
        }
        // Phải parse CẢ class android/* — chính chúng tham chiếu những class
        // framework khác còn thiếu.
        std::vector<std::uint8_t> bytes;
        if (!inflateEntry(jar, e, bytes)) continue;
        if (collectRefs(bytes, refs)) ++scanned;
    }

    std::vector<std::string> missing;
    std::set_difference(refs.all.begin(), refs.all.end(), present.begin(), present.end(),
                        std::back_inserter(missing));

    std::fprintf(stderr,
                 "[kudroid_autostub] %s: scanned %zu classes, %zu android refs, "
                 "%zu present, %zu missing\n",
                 jarPath.c_str(), scanned, refs.all.size(), present.size(), missing.size());
    if (missing.empty()) return 0;

    // Local entries cũ nằm ở [0, cdOffset) và giữ nguyên vị trí → localOffset
    // trong các CD record cũ vẫn đúng, chỉ cần nối thêm ở sau.
    std::vector<std::uint8_t> newLocal, newCentral;
    int added = 0;
    for (const auto& dotted : missing) {
        std::string slash = dotted;
        for (char& c : slash) if (c == '.') c = '/';
        // Sai loại (class vs interface) gây IncompatibleClassChangeError.
        const bool isInterface = refs.asIface.count(dotted) > 0 &&
                                 refs.asSuper.count(dotted) == 0;
        const std::uint32_t offsetInFile =
            static_cast<std::uint32_t>(jar.cdOffset + newLocal.size());
        emitStoredEntry(newLocal, newCentral, slash + ".class",
                        buildStubClass(slash, isInterface), offsetInFile);
        if (added < 15) {
            std::fprintf(stderr, "[kudroid_autostub]   + %s%s\n", dotted.c_str(),
                         isInterface ? " (interface)" : "");
        }
        ++added;
    }

    const std::uint32_t newCdOffset =
        static_cast<std::uint32_t>(jar.cdOffset + newLocal.size());
    const std::uint32_t newCdSize =
        static_cast<std::uint32_t>(jar.cdSize + newCentral.size());
    const std::uint16_t newCount =
        static_cast<std::uint16_t>(jar.entryCount + added);

    const std::string tmpPath = jarPath + ".autostub.tmp";
    FILE* out = fopen(tmpPath.c_str(), "wb");
    if (!out) {
        std::fprintf(stderr, "[kudroid_autostub] cannot write %s\n", tmpPath.c_str());
        return 0;
    }
    bool ok = fwrite(jar.bytes.data(), 1, jar.cdOffset, out) == jar.cdOffset;
    ok = ok && fwrite(newLocal.data(), 1, newLocal.size(), out) == newLocal.size();
    ok = ok && fwrite(jar.bytes.data() + jar.cdOffset, 1, jar.cdSize, out) == jar.cdSize;
    ok = ok && fwrite(newCentral.data(), 1, newCentral.size(), out) == newCentral.size();

    std::vector<std::uint8_t> eocd;
    le32(eocd, 0x06054b50);
    le16(eocd, 0); le16(eocd, 0);
    le16(eocd, newCount);
    le16(eocd, newCount);
    le32(eocd, newCdSize);
    le32(eocd, newCdOffset);
    le16(eocd, 0);
    ok = ok && fwrite(eocd.data(), 1, eocd.size(), out) == eocd.size();
    fclose(out);

    if (!ok) {
        std::remove(tmpPath.c_str());
        std::fprintf(stderr, "[kudroid_autostub] write failed, jar untouched\n");
        return 0;
    }
    if (std::rename(tmpPath.c_str(), jarPath.c_str()) != 0) {
        std::remove(tmpPath.c_str());
        std::fprintf(stderr, "[kudroid_autostub] rename failed, jar untouched\n");
        return 0;
    }

    std::fprintf(stderr, "[kudroid_autostub] appended %d stubs into %s\n",
                 added, jarPath.c_str());
    return added;
}

} // namespace kudroid
