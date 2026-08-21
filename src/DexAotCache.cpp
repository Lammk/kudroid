#include "kudroid/DexAotCache.h"
#include "kudroid/DexCacheManager.h"
#include "kudroid/DexToJar.h"
#include "kudroid/framework_jar_bytes.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <mutex>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace kudroid {
namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Tiện ích nhỏ.
// ─────────────────────────────────────────────────────────────────────────────

// Bọc path trong single-quote để chạy qua /bin/sh an toàn (path có thể chứa
// khoảng trắng / ký tự đặc biệt).
std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += '\'';
    return out;
}

bool read_small_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)), {});
    return true;
}

bool write_bytes_to_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    f.close();
    return static_cast<bool>(f);
}

bool write_small_file_atomic(const std::string& path, const std::string& data) {
    // Ghi nguyên tử: tmp + rename để không hỏng cache khi crash giữa chừng.
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        f.close();
        if (!f) {
            std::filesystem::remove(tmp);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp);
        return false;
    }
    return true;
}

// Thu thập classes.dex, classes2.dex, ... theo thứ tự tăng dần.
std::vector<std::string> collect_dex_files(const std::string& dir) {
    std::vector<std::string> dexes;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return dexes;
    for (unsigned n = 1;; ++n) {
        const std::string name =
            (n == 1) ? "classes.dex" : ("classes" + std::to_string(n) + ".dex");
        const std::string path = std::filesystem::path(dir) / name;
        if (!std::filesystem::is_regular_file(path, ec)) break; // hết chuỗi
        dexes.push_back(path);
    }
    return dexes;
}

// Serialize toàn bộ dịch cache: hai luồng (vd run_apk + load_apk) cùng MISS sẽ
// cùng chạy dex2jar ghi đè classes.jar → hỏng jar. Chỉ một luồng dịch tại một
// thời điểm; luồng sau sẽ gặp cache HIT.
static std::mutex g_translate_mtx;

// ─────────────────────────────────────────────────────────────────────────────
// Zip merge tối giản — ghép nhiều jar (zip) thành một mà KHÔNG giải nén lại:
// đọc central directory của từng jar, copy nguyên khối dữ liệu (raw deflate),
// ghi lại local header + central directory + EOCD với offset mới.
// ─────────────────────────────────────────────────────────────────────────────

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void wr16(std::ofstream& f, uint16_t v) { f.put(char(v & 0xff)); f.put(char((v >> 8) & 0xff)); }
void wr32(std::ofstream& f, uint32_t v) {
    f.put(char(v & 0xff)); f.put(char((v >> 8) & 0xff));
    f.put(char((v >> 16) & 0xff)); f.put(char((v >> 24) & 0xff));
}

struct ZipEntry {
    std::string name;
    uint16_t method;
    uint32_t crc;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint32_t localOffset;      // trong file nguồn
    size_t sourceIndex;        // file nguồn nào giữ dữ liệu raw
    uint32_t outLocalOffset;   // offset trong output (điền khi ghi)
    uint16_t versionNeeded;
    uint16_t externalAttrs;
    uint32_t internalAttrs;
};

bool collect_zip_entries(const std::string& path, size_t sourceIndex,
                         std::vector<ZipEntry>& out, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "Cannot open jar: " + path; return false; }
    f.seekg(0, std::ios::end);
    const std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    if (size < 22) { if (error) *error = "Jar too small: " + path; return false; }

    const size_t tailLen = size < 65557 ? static_cast<size_t>(size) : 65557;
    std::vector<uint8_t> tail(tailLen);
    f.seekg(size - static_cast<std::streamsize>(tailLen));
    f.read(reinterpret_cast<char*>(tail.data()), static_cast<std::streamsize>(tailLen));
    size_t eocd = tailLen;
    for (size_t i = tailLen; i >= 22; --i) {
        if (rd32(tail.data() + (i - 22)) == 0x06054b50) { eocd = i - 22; break; }
    }
    if (eocd == tailLen) { if (error) *error = "EOCD not found: " + path; return false; }
    const uint16_t count = rd16(tail.data() + eocd + 10);
    const uint32_t central = rd32(tail.data() + eocd + 16);

    f.seekg(central);
    for (uint16_t i = 0; i < count; ++i) {
        uint8_t hdr[46];
        f.read(reinterpret_cast<char*>(hdr), 46);
        if (f.gcount() != 46 || rd32(hdr) != 0x02014b50) {
            if (error) *error = "Invalid central-directory entry in " + path;
            return false;
        }
        ZipEntry e;
        e.versionNeeded = rd16(hdr + 6);
        e.method = rd16(hdr + 10);
        e.crc = rd32(hdr + 16);
        e.compressedSize = rd32(hdr + 20);
        e.uncompressedSize = rd32(hdr + 24);
        const uint16_t nameLen = rd16(hdr + 28);
        const uint16_t extraLen = rd16(hdr + 30);
        const uint16_t commentLen = rd16(hdr + 32);
        e.localOffset = rd32(hdr + 42);
        e.externalAttrs = rd16(hdr + 38);
        e.internalAttrs = rd32(hdr + 36);
        e.sourceIndex = sourceIndex;
        std::string name(nameLen, '\0');
        f.read(&name[0], nameLen);
        f.seekg(static_cast<std::streamoff>(extraLen) + commentLen, std::ios::cur);
        e.name = std::move(name);
        // Bỏ entry thư mục và tên trùng (giữ bản đầu tiên).
        if (!e.name.empty() && e.name.back() != '/' &&
            std::find_if(out.begin(), out.end(),
                         [&](const ZipEntry& o) { return o.name == e.name; }) == out.end()) {
            out.push_back(std::move(e));
        }
    }
    return true;
}

bool merge_jars(const std::vector<std::string>& inputs, const std::string& output,
                std::string* error) {
    std::vector<ZipEntry> entries;
    for (size_t i = 0; i < inputs.size(); ++i) {
        if (!collect_zip_entries(inputs[i], i, entries, error)) return false;
    }
    if (entries.empty()) { if (error) *error = "No entries to merge"; return false; }

    // Mở sẵn các file nguồn để đọc raw data.
    std::vector<std::ifstream> sources;
    sources.reserve(inputs.size());
    for (const auto& in : inputs) {
        sources.emplace_back(in, std::ios::binary);
        if (!sources.back()) { if (error) *error = "Cannot reopen jar: " + in; return false; }
    }

    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out) { if (error) *error = "Cannot write output jar: " + output; return false; }

    // (1) Ghi local headers + raw data cho từng entry.
    const uint32_t kLocalMagic = 0x04034b50;
    const uint32_t kCentralMagic = 0x02014b50;
    const uint32_t kEocdMagic = 0x06054b50;
    for (auto& e : entries) {
        e.outLocalOffset = static_cast<uint32_t>(out.tellp());

        // Đọc local header nguồn để tính vị trí dữ liệu (localOffset + 30 + nameLen + extraLen).
        std::ifstream& src = sources[e.sourceIndex];
        src.seekg(e.localOffset);
        uint8_t lh[30];
        src.read(reinterpret_cast<char*>(lh), 30);
        if (src.gcount() != 30 || rd32(lh) != kLocalMagic) {
            if (error) *error = "Invalid local header for " + e.name;
            return false;
        }
        const uint16_t srcNameLen = rd16(lh + 26);
        const uint16_t srcExtraLen = rd16(lh + 28);
        src.seekg(static_cast<std::streamoff>(srcNameLen) + srcExtraLen, std::ios::cur);

        // Local header mới (flags=0 — ghi đầy đủ sizes, không cần data descriptor).
        wr32(out, kLocalMagic);
        wr16(out, 20);                     // version needed
        wr16(out, 0);                      // flags
        wr16(out, e.method);
        wr16(out, 0);                      // time
        wr16(out, 0);                      // date
        wr32(out, e.crc);
        wr32(out, e.compressedSize);
        wr32(out, e.uncompressedSize);
        wr16(out, static_cast<uint16_t>(e.name.size()));
        wr16(out, 0);                      // extra len
        out.write(e.name.data(), static_cast<std::streamsize>(e.name.size()));

        // Copy raw dữ liệu (stored hoặc deflate) — không giải nén lại.
        std::vector<uint8_t> raw(e.compressedSize);
        if (!raw.empty()) {
            src.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
            if (src.gcount() != static_cast<std::streamsize>(raw.size())) {
                if (error) *error = "Truncated entry data for " + e.name;
                return false;
            }
            out.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
        }
    }

    // (2) Central directory.
    const uint32_t centralStart = static_cast<uint32_t>(out.tellp());
    for (const auto& e : entries) {
        wr32(out, kCentralMagic);
        wr16(out, 20);                     // version made by
        wr16(out, e.versionNeeded);
        wr16(out, 0);                      // flags
        wr16(out, e.method);
        wr16(out, 0);                      // time
        wr16(out, 0);                      // date
        wr32(out, e.crc);
        wr32(out, e.compressedSize);
        wr32(out, e.uncompressedSize);
        wr16(out, static_cast<uint16_t>(e.name.size()));
        wr16(out, 0);                      // extra len
        wr16(out, 0);                      // comment len
        wr16(out, 0);                      // disk number
        wr16(out, 0);                      // internal attrs
        wr16(out, static_cast<uint16_t>(e.externalAttrs));
        wr32(out, e.outLocalOffset);
        out.write(e.name.data(), static_cast<std::streamsize>(e.name.size()));
    }
    const uint32_t centralEnd = static_cast<uint32_t>(out.tellp());

    // (3) EOCD.
    wr32(out, kEocdMagic);
    wr16(out, 0);                          // disk number
    wr16(out, 0);                          // central dir disk
    wr16(out, static_cast<uint16_t>(entries.size()));
    wr16(out, static_cast<uint16_t>(entries.size()));
    wr32(out, centralEnd - centralStart);
    wr32(out, centralStart);
    wr16(out, 0);                          // comment len
    out.close();
    return true;
}

} // namespace
} // namespace kudroid

namespace kudroid {

std::string DexAotCache::dex2jar_command() {
    const char* env = std::getenv("KUDROID_DEX2JAR");
    if (env && *env) return env;
    return "d2j-dex2jar.sh";
}

std::string DexAotCache::translate_dex_if_needed(const std::string& apk_extracted_path,
                                                 const std::string& cache_dir,
                                                 std::string* error) {
    std::lock_guard<std::mutex> lock(g_translate_mtx);
    const auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return std::string();
    };

    // (1) Thu thập + băm classes*.dex.
    const std::vector<std::string> dexes = collect_dex_files(apk_extracted_path);
    if (dexes.empty()) {
        return fail("[kudroid_aot] No classes*.dex found in " + apk_extracted_path);
    }
    const std::string hash = DexCacheManager::sha256Files(dexes) + "_v10_system_arraycopy";
    if (hash.empty()) {
        return fail("[kudroid_aot] Cannot compute SHA-256 of dex files");
    }

    const std::string jarPath = (std::filesystem::path(cache_dir) / "classes.jar").string();
    const std::string hashPath = (std::filesystem::path(cache_dir) / "cache.hash").string();

    // (2) Cache hit?
    {
        std::error_code ec;
        if (std::filesystem::is_regular_file(jarPath, ec) &&
            std::filesystem::is_regular_file(hashPath, ec)) {
            std::string stored;
            if (read_small_file(hashPath, stored)) {
                // Bỏ whitespace đầu/cuối.
                while (!stored.empty() && (stored.back() == '\n' || stored.back() == '\r' || stored.back() == ' '))
                    stored.pop_back();
                if (stored == hash) {
                    return jarPath; // MATCH — dùng thẳng cache
                }
            }
        }
    }

    // (3) MISMATCH / chưa có → dịch lại.
    std::error_code ec;
    std::filesystem::create_directories(cache_dir, ec);
    if (ec) return fail("[kudroid_aot] Cannot create cache dir: " + cache_dir);

    bool built = false;

    // (3a) Ưu tiên dex2jar (tool chuẩn của dex2jar project) qua system():
    //   single dex → dịch thẳng; nhiều dex → dịch từng cái rồi merge.
    //   GHI ATOMIC: dex2jar xuất ra <jar>.tmp rồi rename — nếu process crash
    //   giữa chừng, classes.jar ở đường dẫn cuối không bao giờ bị nửa-dở
    //   (rename là atomic trên POSIX); cache.hash chỉ ghi sau khi rename xong.
    //
    //   std::system() bị Apple đánh dấu UNAVAILABLE trên iOS SDK (app không
    //   được fork/exec) — nhánh này chỉ biên dịch trên desktop/macOS.
#if !defined(__APPLE__) || !TARGET_OS_IPHONE
    const std::string d2j = dex2jar_command();
    if (dexes.size() == 1) {
        const std::string tmpJar = jarPath + ".tmp";
        const std::string cmd = d2j + " --force -o " + shell_quote(tmpJar) + " " + shell_quote(dexes[0]);
        const int rc = std::system(cmd.c_str());
        built = (rc == 0) && std::filesystem::is_regular_file(tmpJar, ec);
        if (built) {
            std::filesystem::rename(tmpJar, jarPath, ec);
            built = !ec;
            if (!built && error) {
                *error = "[kudroid_aot] Cannot rename dex2jar output to " + jarPath;
            }
        } else if (error) {
            *error = "[kudroid_aot] dex2jar failed (rc=" + std::to_string(rc) + "): " + cmd;
        }
    } else {
        std::vector<std::string> tmpJars;
        bool ok = true;
        for (size_t i = 0; i < dexes.size() && ok; ++i) {
            const std::string tmp = (std::filesystem::path(cache_dir) /
                                     (".tmp_" + std::to_string(i) + ".jar")).string();
            const std::string cmd = d2j + " --force -o " + shell_quote(tmp) + " " + shell_quote(dexes[i]);
            const int rc = std::system(cmd.c_str());
            if (rc != 0 || !std::filesystem::is_regular_file(tmp, ec)) {
                if (error) {
                    *error = "[kudroid_aot] dex2jar failed for " + dexes[i] +
                             " (rc=" + std::to_string(rc) + ")";
                }
                ok = false;
            } else {
                tmpJars.push_back(tmp);
            }
        }
        if (ok) {
            std::string mergeErr;
            const std::string mergedTmp = jarPath + ".tmp";
            ok = merge_jars(tmpJars, mergedTmp, &mergeErr);
            if (ok) {
                std::filesystem::rename(mergedTmp, jarPath, ec);
                ok = !ec;
                if (!ok && error) {
                    *error = "[kudroid_aot] Cannot rename merged jar to " + jarPath;
                }
            } else if (error) {
                *error = "[kudroid_aot] " + mergeErr;
            }
        }
        for (const auto& tmp : tmpJars) std::filesystem::remove(tmp, ec);
        built = ok;
    }
#endif // !defined(__APPLE__) || !TARGET_OS_IPHONE

    // (3b) Fallback nội bộ: d2j-dex2jar.sh cần JRE ngoài — trên iOS không có
    // (và std::system không compile được), dùng trình dịch DEX→JAR nhúng
    // (DexToJar) để không chặn khởi động JVM.
    if (!built) {
        std::vector<std::string> tmpJars;
        bool ok = true;
        for (size_t i = 0; i < dexes.size() && ok; ++i) {
            const std::string tmp = (std::filesystem::path(cache_dir) /
                                     (".tmp_" + std::to_string(i) + ".jar")).string();
            std::vector<uint8_t> jarBytes;
            std::string convertErr;
            if (!DexToJar::convert(dexes[i], jarBytes, &convertErr) ||
                !write_bytes_to_file(tmp, jarBytes)) {
                if (error) {
                    *error = "[kudroid_aot] built-in DEX→JAR failed for " + dexes[i] +
                             ": " + convertErr;
                }
                ok = false;
            } else {
                tmpJars.push_back(tmp);
            }
        }

        // Gộp framework.jar nhúng sẵn (chứa android.* + androidx.* stubs) vào classes.jar của app
        const std::string fwTmp = (std::filesystem::path(cache_dir) / ".tmp_framework.jar").string();
        if (write_bytes_to_file(fwTmp, std::vector<uint8_t>(g_framework_jar_bytes, g_framework_jar_bytes + g_framework_jar_size))) {
            tmpJars.push_back(fwTmp);
        }

        if (ok) {
            std::string mergeErr;
            ok = merge_jars(tmpJars, jarPath, &mergeErr);
            if (!ok && error) *error = "[kudroid_aot] " + mergeErr;
        }
        for (const auto& tmp : tmpJars) std::filesystem::remove(tmp, ec);
        built = ok;
    }

    if (!built) {
        // error đã được set bởi nhánh thất bại tương ứng
        return std::string();
    }

    // (4) Ghi hash mới (nguyên tử).
    if (!write_small_file_atomic(hashPath, hash + "\n")) {
        return fail("[kudroid_aot] Cannot write cache hash: " + hashPath);
    }
    return jarPath;
}

} // namespace kudroid
