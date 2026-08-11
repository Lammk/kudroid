#include "kudroid/DexCacheManager.h"
#include "kudroid/DexToJar.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace kudroid {

// ─────────────────────────────────────────────────────────────────────────────
// Minimal SHA-256 implementation (no external dependency).
// ─────────────────────────────────────────────────────────────────────────────

namespace {

const uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

struct Sha256Ctx {
    uint32_t h[8];
    uint64_t len;
    uint8_t buf[64];
    size_t bufLen;
};

void sha256_init(Sha256Ctx& ctx) {
    ctx.h[0] = 0x6a09e667;
    ctx.h[1] = 0xbb67ae85;
    ctx.h[2] = 0x3c6ef372;
    ctx.h[3] = 0xa54ff53a;
    ctx.h[4] = 0x510e527f;
    ctx.h[5] = 0x9b05688c;
    ctx.h[6] = 0x1f83d9ab;
    ctx.h[7] = 0x5be0cd19;
    ctx.len = 0;
    ctx.bufLen = 0;
}

void sha256_block(Sha256Ctx& ctx, const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) |
               (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx.h[0], b = ctx.h[1], c = ctx.h[2], d = ctx.h[3];
    uint32_t e = ctx.h[4], f = ctx.h[5], g = ctx.h[6], h = ctx.h[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + S1 + ch + kSha256K[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    ctx.h[0] += a; ctx.h[1] += b; ctx.h[2] += c; ctx.h[3] += d;
    ctx.h[4] += e; ctx.h[5] += f; ctx.h[6] += g; ctx.h[7] += h;
}

void sha256_update(Sha256Ctx& ctx, const uint8_t* data, size_t len) {
    ctx.len += len;
    while (len > 0) {
        size_t take = 64 - ctx.bufLen;
        if (take > len) take = len;
        std::memcpy(ctx.buf + ctx.bufLen, data, take);
        ctx.bufLen += take;
        data += take;
        len -= take;
        if (ctx.bufLen == 64) {
            sha256_block(ctx, ctx.buf);
            ctx.bufLen = 0;
        }
    }
}

void sha256_final(Sha256Ctx& ctx, uint8_t out[32]) {
    uint64_t bitLen = ctx.len * 8;
    uint8_t pad = 0x80;
    sha256_update(ctx, &pad, 1);
    uint8_t zero = 0;
    while (ctx.bufLen != 56) {
        sha256_update(ctx, &zero, 1);
    }
    uint8_t lenBytes[8];
    for (int i = 0; i < 8; ++i) {
        lenBytes[i] = uint8_t(bitLen >> (56 - i * 8));
    }
    sha256_update(ctx, lenBytes, 8);
    for (int i = 0; i < 8; ++i) {
        out[i * 4]     = uint8_t(ctx.h[i] >> 24);
        out[i * 4 + 1] = uint8_t(ctx.h[i] >> 16);
        out[i * 4 + 2] = uint8_t(ctx.h[i] >> 8);
        out[i * 4 + 3] = uint8_t(ctx.h[i]);
    }
}

std::string toHex(const uint8_t* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 0x0f];
    }
    return out;
}

} // namespace

std::string DexCacheManager::sha256(const uint8_t* data, size_t len) {
    Sha256Ctx ctx;
    sha256_init(ctx);
    sha256_update(ctx, data, len);
    uint8_t digest[32];
    sha256_final(ctx, digest);
    return toHex(digest, 32);
}

std::string DexCacheManager::sha256File(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    Sha256Ctx ctx;
    sha256_init(ctx);
    char buffer[65536];
    while (file) {
        file.read(buffer, sizeof(buffer));
        std::streamsize n = file.gcount();
        if (n > 0) {
            sha256_update(ctx, reinterpret_cast<const uint8_t*>(buffer),
                          static_cast<size_t>(n));
        }
    }
    uint8_t digest[32];
    sha256_final(ctx, digest);
    return toHex(digest, 32);
}

// ─────────────────────────────────────────────────────────────────────────────
// CacheManager
// ─────────────────────────────────────────────────────────────────────────────

DexCacheManager& DexCacheManager::getInstance() {
    static DexCacheManager instance;
    return instance;
}

void DexCacheManager::setCacheDirectory(const std::string& dir) {
    cacheDir_ = dir;
    std::error_code ec;
    std::filesystem::create_directories(cacheDir_, ec);
}

std::string DexCacheManager::cacheBasePath(const std::string& dexPath,
                                           int version) const {
    std::string hash = sha256File(dexPath);
    if (hash.empty()) return {};
    return cacheDir_ + "/" + hash + "_v" + std::to_string(version);
}

bool DexCacheManager::hasValidCache(const std::string& dexPath,
                                    int toolVersion) {
    if (cacheDir_.empty()) return false;
    std::string base = cacheBasePath(dexPath, toolVersion);
    if (base.empty()) return false;

    // Both the .bin data file AND the .meta.json must exist.
    std::string binPath = base + ".bin";
    std::string metaPath = base + ".meta.json";
    return std::filesystem::exists(binPath) && std::filesystem::exists(metaPath);
}

bool DexCacheManager::loadCache(const std::string& dexPath, int toolVersion,
                                std::vector<uint8_t>& out) {
    if (!hasValidCache(dexPath, toolVersion)) return false;
    std::string binPath = cacheBasePath(dexPath, toolVersion) + ".bin";

    std::ifstream file(binPath, std::ios::binary | std::ios::ate);
    if (!file) return false;
    std::streamsize size = file.tellg();
    if (size <= 0) return false;
    file.seekg(0);
    out.resize(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(out.data()), size);
    return !!file;
}

bool DexCacheManager::saveCache(const std::string& dexPath, int toolVersion,
                                const std::vector<uint8_t>& data) {
    if (cacheDir_.empty()) return false;
    std::string base = cacheBasePath(dexPath, toolVersion);
    if (base.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(cacheDir_, ec);

    // Atomic write: write to a temp file, then rename.
    std::string tmpBin = base + ".bin.tmp";
    {
        std::ofstream out(tmpBin, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        out.close();
        if (!out) return false;
    }
    std::filesystem::rename(tmpBin, base + ".bin", ec);
    if (ec) return false;

    // Write metadata.
    std::string hash = sha256File(dexPath);
    std::string meta = "{\n"
        "  \"dex_hash\": \"" + hash + "\",\n"
        "  \"version\": " + std::to_string(toolVersion) + ",\n"
        "  \"tool\": \"kudroid-dex-v" + std::to_string(toolVersion) + "\"\n"
        "}\n";
    std::string tmpMeta = base + ".meta.json.tmp";
    {
        std::ofstream out(tmpMeta, std::ios::trunc);
        if (!out) return false;
        out << meta;
        out.close();
        if (!out) return false;
    }
    std::filesystem::rename(tmpMeta, base + ".meta.json", ec);
    return !ec;
}

void DexCacheManager::clearCacheForDex(const std::string& dexPath) {
    if (cacheDir_.empty()) return;
    std::string hash = sha256File(dexPath);
    if (hash.empty()) return;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(cacheDir_, ec)) {
        std::string name = entry.path().filename().string();
        if (name.rfind(hash + "_v", 0) == 0) {
            std::filesystem::remove_all(entry.path(), ec);
        }
    }
}

void DexCacheManager::clearCache() {
    if (cacheDir_.empty()) return;
    std::error_code ec;
    std::filesystem::remove_all(cacheDir_, ec);
    std::filesystem::create_directories(cacheDir_, ec);
}

bool DexCacheManager::translateAndCache(const std::string& dexPath, int toolVersion,
                                        std::vector<uint8_t>& outJar,
                                        std::string* error) {
    // 1. Try the cache first.
    if (hasValidCache(dexPath, toolVersion)) {
        if (loadCache(dexPath, toolVersion, outJar)) {
            return true;
        }
        // Cache exists but failed to load — fall through and re-translate.
    }

    // 2. Translate the DEX to a JAR.
    if (!DexToJar::convert(dexPath, outJar, error)) {
        return false;
    }

    // 3. Save to cache (best-effort; failure to cache is not fatal).
    if (!cacheDir_.empty()) {
        saveCache(dexPath, toolVersion, outJar);
    }

    return true;
}

} // namespace kudroid
