namespace {
thread_local std::string gLastError;
void apkLog(const std::string& message) { std::fprintf(stderr, "[kudroid_apk] %s\n", message.c_str()); }

// Helpers over an in-memory buffer (extract_apk loads the whole file).
std::uint16_t read16(const std::vector<std::uint8_t>& d, std::size_t o) { return d[o] | (d[o + 1] << 8); }
std::uint32_t read32(const std::vector<std::uint8_t>& d, std::size_t o) {
    return d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24);
}
[[maybe_unused]] bool hasBytes(const std::vector<std::uint8_t>& d, std::size_t o, std::size_t n) { return o <= d.size() && n <= d.size() - o; }
bool inflateRaw(const std::uint8_t* input, std::size_t inputSize, std::vector<std::uint8_t>& output) {
    if (output.empty()) return true;

    // First try raw deflate (-MAX_WBITS = -15, standard for ZIP)
    z_stream stream = {};
    stream.next_in = const_cast<Bytef*>(input);
    stream.avail_in = static_cast<uInt>(inputSize);
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit2(&stream, -MAX_WBITS) == Z_OK) {
        const int result = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        if (result == Z_STREAM_END || stream.total_out == output.size()) return true;
    }

    // Fallback 1: Try auto-detect (32 + MAX_WBITS) for gzip/zlib header
    std::memset(&stream, 0, sizeof(stream));
    stream.next_in = const_cast<Bytef*>(input);
    stream.avail_in = static_cast<uInt>(inputSize);
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit2(&stream, 32 + MAX_WBITS) == Z_OK) {
        const int result = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        if (result == Z_STREAM_END || stream.total_out == output.size()) return true;
    }

    // Fallback 2: Try standard zlib (MAX_WBITS = 15)
    std::memset(&stream, 0, sizeof(stream));
    stream.next_in = const_cast<Bytef*>(input);
    stream.avail_in = static_cast<uInt>(inputSize);
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit2(&stream, MAX_WBITS) == Z_OK) {
        const int result = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        if (result == Z_STREAM_END || stream.total_out == output.size()) return true;
    }

    return false;
}

// Streaming helpers (is_bundle_container / extract_bundle read big containers
// entry-by-entry instead of loading the whole file into RAM — .xapk/.apks can
// contain multi-GB OBB files).
std::uint16_t read16b(const char* d, std::size_t o) {
    const auto* u = reinterpret_cast<const unsigned char*>(d);
    return static_cast<std::uint16_t>(u[o] | (u[o + 1] << 8));
}
std::uint32_t read32b(const char* d, std::size_t o) {
    const auto* u = reinterpret_cast<const unsigned char*>(d);
    return static_cast<std::uint32_t>(u[o] | (u[o + 1] << 8) | (u[o + 2] << 16) | (u[o + 3] << 24));
}
bool readExact(std::ifstream& f, void* buf, std::size_t n) {
    f.read(static_cast<char*>(buf), static_cast<std::streamsize>(n));
    return f.gcount() == static_cast<std::streamsize>(n);
}

struct ZipEntryInfo {
    std::string name;
    std::uint16_t compression;
    std::uint32_t compressedSize;
    std::uint32_t uncompressedSize;
    std::uint32_t localOffset;
};

// Read end-of-central-directory + entire central directory (metadata only).
bool readZipEntries(std::ifstream& f, std::streamsize fileSize, std::vector<ZipEntryInfo>& entries) {
    entries.clear();
    const std::size_t tailLen = fileSize < 65557 ? static_cast<std::size_t>(fileSize) : 65557;
    std::vector<char> tail(tailLen);
    f.seekg(fileSize - static_cast<std::streamsize>(tailLen));
    if (!readExact(f, tail.data(), tailLen)) return false;

    std::size_t eocd = tailLen;
    for (std::size_t i = tailLen; i >= 22; --i) {
        if (read32b(tail.data(), i - 22) == 0x06054b50) { eocd = i - 22; break; }
    }
    if (eocd == tailLen) { gLastError = "ZIP end record not found"; return false; }
    const std::uint16_t entryCount = read16b(tail.data(), eocd + 10);
    const std::uint32_t centralOffset = read32b(tail.data(), eocd + 16);
    const std::uint16_t commentLen = read16b(tail.data(), eocd + 20);
    // Not Zip64 (sentinels point at a Zip64 EOCD locator this reader does not
    // parse), and the EOCD must actually sit at the end: without the length
    // check a false 0x06054b50 inside compressed data mis-parses the tail.
    // Not a size cap (APKs are ~1GB); Zip64 is the explicit error.
    if (entryCount == 0xFFFF || centralOffset == 0xFFFFFFFFu) {
        gLastError = "Zip64 archive not supported";
        return false;
    }
    const std::uint64_t eocdAbs =
        static_cast<std::uint64_t>(fileSize - static_cast<std::streamsize>(tailLen)) + eocd;
    if (eocdAbs + 22 + commentLen != static_cast<std::uint64_t>(fileSize)) {
        gLastError = "ZIP end record not at end of file";
        return false;
    }

    f.seekg(centralOffset);
    for (std::uint16_t index = 0; index < entryCount; ++index) {
        char hdr[46];
        if (!readExact(f, hdr, 46) || read32b(hdr, 0) != 0x02014b50) {
            gLastError = "Invalid ZIP central-directory entry";
            return false;
        }
        ZipEntryInfo info;
        info.compression = read16b(hdr, 10);
        info.compressedSize = read32b(hdr, 20);
        info.uncompressedSize = read32b(hdr, 24);
        const std::uint16_t nameLen = read16b(hdr, 28);
        const std::uint16_t extraLen = read16b(hdr, 30);
        const std::uint16_t commentLen = read16b(hdr, 32);
        info.localOffset = read32b(hdr, 42);
        std::string name(nameLen, '\0');
        if (!readExact(f, &name[0], nameLen)) { gLastError = "Truncated ZIP entry name"; return false; }
        f.seekg(static_cast<std::streamoff>(extraLen) + commentLen, std::ios::cur);
        info.name = std::move(name);
        entries.push_back(std::move(info));
    }
    return true;
}

bool extractZipEntryToMemory(std::ifstream& f, const ZipEntryInfo& e, std::vector<std::uint8_t>& output) {
    // Bounded before allocating: a lying header otherwise turns into a 4GB
    // vector (bad_alloc/OOM) during install. Manifests/icons/dex here are KBs.
    constexpr std::uint32_t kMaxMemoryEntry = 512u * 1024 * 1024;
    if (e.uncompressedSize > kMaxMemoryEntry || e.compressedSize > kMaxMemoryEntry) return false;
    f.seekg(e.localOffset);
    char lh[30];
    if (!readExact(f, lh, 30) || read32b(lh, 0) != 0x04034b50) return false;
    const std::uint16_t nameLen = read16b(lh, 26);
    const std::uint16_t extraLen = read16b(lh, 28);
    f.seekg(static_cast<std::streamoff>(nameLen) + extraLen, std::ios::cur);

    if (e.compression == 0) {
        output.resize(e.compressedSize);
        return readExact(f, output.data(), e.compressedSize);
    }
    if (e.compression != 8) return false;

    output.resize(e.uncompressedSize);
    std::vector<std::uint8_t> in(e.compressedSize);
    if (!readExact(f, in.data(), e.compressedSize)) return false;

    return inflateRaw(in.data(), in.size(), output);
}

// Decompress an entry (stored or deflate) to a file — streaming, do not load the entire entry.
bool extractZipEntryToFile(std::ifstream& f, const ZipEntryInfo& e, const std::string& destPath,
                           uint64_t maxOutput = UINT64_MAX) {
    if (e.uncompressedSize > maxOutput || (e.compression == 0 && e.compressedSize > maxOutput)) return false;
    f.clear();
    f.seekg(e.localOffset);
    char lh[30];
    if (!readExact(f, lh, 30) || read32b(lh, 0) != 0x04034b50) return false;
    const std::uint16_t nameLen = read16b(lh, 26);
    const std::uint16_t extraLen = read16b(lh, 28);
    f.seekg(static_cast<std::streamoff>(nameLen) + extraLen, std::ios::cur);

    std::ofstream out(destPath, std::ios::binary);
    if (!out) return false;

    if (e.compression == 0) {
        std::size_t remaining = e.compressedSize;
        std::vector<char> buf(1 << 20);
        while (remaining > 0) {
            const std::size_t n = remaining < buf.size() ? remaining : buf.size();
            if (!readExact(f, buf.data(), n)) return false;
            out.write(buf.data(), static_cast<std::streamsize>(n));
            remaining -= n;
        }
        return out.good();
    }

    if (e.compression != 8) { gLastError = "Unsupported ZIP compression method"; return false; }

    z_stream stream = {};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return false;
    std::vector<char> in(1 << 20);
    std::vector<char> outBuf(1 << 20);
    std::size_t remaining = e.compressedSize;
    int ret = Z_OK;
    bool ok = true;
    uint64_t written = 0;
    while (ret != Z_STREAM_END) {
        if (stream.avail_in == 0) {
            if (remaining == 0) { ok = false; break; }
            const std::size_t n = remaining < in.size() ? remaining : in.size();
            if (!readExact(f, in.data(), n)) { ok = false; break; }
            remaining -= n;
            stream.next_in = reinterpret_cast<Bytef*>(in.data());
            stream.avail_in = static_cast<uInt>(n);
        }
        stream.next_out = reinterpret_cast<Bytef*>(outBuf.data());
        stream.avail_out = static_cast<uInt>(outBuf.size());
        ret = inflate(&stream, Z_NO_FLUSH);
        const size_t produced = outBuf.size() - stream.avail_out;
        if (produced > maxOutput - written) { ok = false; break; }
        out.write(outBuf.data(), produced);
        written += produced;
        if (!out) { ok = false; break; }
        if (ret != Z_OK && ret != Z_STREAM_END) { ok = false; break; }
    }
    inflateEnd(&stream);
    return ok && (maxOutput == UINT64_MAX || written == e.uncompressedSize);
}

bool endsWithCi(const std::string& s, const char* suffix) {
    const std::size_t sl = std::strlen(suffix);
    if (s.size() < sl) return false;
    for (std::size_t i = 0; i < sl; ++i) {
        if (std::tolower(static_cast<unsigned char>(s[s.size() - sl + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i]))) return false;
    }
    return true;
}

// Zip-Slip guard: entry names are attacker-controlled. Reject absolute paths,
// ".." components and NUL bytes, then verify the joined destination still
// sits under the target directory (symlink- and case-insensitive filesystems
// can otherwise still escape the prefix check).
bool zip_dest_within(const std::filesystem::path& targetDir,
                     const std::filesystem::path& dest) {
    std::error_code ec;
    if (dest.string().find('\0') != std::string::npos) return false;
    for (const auto& part : dest) {
        if (part == "..") return false;
    }
    const auto normTarget = targetDir.lexically_normal();
    const auto normDest = dest.lexically_normal();
    const std::string t = normTarget.string();
    const std::string d = normDest.string();
    return d.size() > t.size() && d.compare(0, t.size(), t) == 0 &&
           (d[t.size()] == '/' || t.back() == '/');
}
// entire lowercase (used to filter split by name).
std::string toLower(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

// ManifestInfo is now a public struct declared in APKExtractor.h — parseAxml
// fill it in, kudroid_bridge calls parse_manifest() directly on the resolved AXML
// compressed to get the correct LAUNCHER activity (instead of guessing by folder name).

static int iconPriority(const std::string& name) {
    if (!endsWithCi(name, ".png") && !endsWithCi(name, ".webp") && !endsWithCi(name, ".jpg") && !endsWithCi(name, ".jpeg")) return -1;
    if (endsWithCi(name, ".9.png")) return -1; // 9-patch border format is not an app icon

    const std::string lower = toLower(name);
    int score = 0;

    // Prioritize resolution
    if (lower.find("xxxhdpi") != std::string::npos) score += 60;
    else if (lower.find("xxhdpi") != std::string::npos) score += 50;
    else if (lower.find("xhdpi") != std::string::npos) score += 40;
    else if (lower.find("hdpi") != std::string::npos) score += 30;
    else if (lower.find("mdpi") != std::string::npos) score += 20;
    else if (lower.find("ldpi") != std::string::npos) score += 10;
    else if (lower.find("nodpi") != std::string::npos) score += 35;
    else if (lower.find("drawable") != std::string::npos || lower.find("mipmap") != std::string::npos) score += 15;

    // Priority is given to standard icon names of Android & Unity
    if (lower.find("ic_launcher_foreground") != std::string::npos) score += 85;
    else if (lower.find("ic_launcher") != std::string::npos) score += 100;
    else if (lower.find("app_icon") != std::string::npos || lower.find("appicon") != std::string::npos) score += 95;
    else if (lower.find("icon") != std::string::npos) score += 75;
    else if (lower.find("logo") != std::string::npos) score += 65;
    else if (lower.find("banner") != std::string::npos) score += 40;
    else if (lower.find("splash") != std::string::npos) score += 30;
    else if (lower.rfind("res/drawable", 0) == 0 || lower.rfind("res/mipmap", 0) == 0) score += 10;

    return score > 0 ? score : -1;
}

static std::vector<std::string> parseStringPool(const std::vector<std::uint8_t>& data,
                                                std::size_t poolOffset);

static bool isBitmapName(const std::string& name) {
    if (endsWithCi(name, ".9.png")) return false;
    return endsWithCi(name, ".png") || endsWithCi(name, ".webp") ||
           endsWithCi(name, ".jpg") || endsWithCi(name, ".jpeg");
}

// Adaptive icons live in a binary XML that names the foreground/background
// drawables. Their resource ids lead back to the raster files, which may have
// been renamed by resource shrinking.
static void collectAdaptiveIconRefs(const std::vector<std::uint8_t>& data,
                                    std::vector<std::uint32_t>* ids) {
    if (ids == nullptr || data.size() < 8 || read16(data, 0) != 0x0003) return;
    std::size_t cur = read16(data, 2);
    std::vector<std::uint32_t> foreground;
    std::vector<std::uint32_t> background;
    std::vector<std::string> pool;
    bool havePool = false;
    while (cur + 8 <= data.size()) {
        const std::uint32_t type = read32(data, cur);
        const std::uint32_t size = read32(data, cur + 4);
        if (size < 8 || cur + size > data.size()) break;
        if (type == 0x0001) {
            pool = parseStringPool(data, cur);
            havePool = !pool.empty();
        } else if (type == 0x0102 && havePool && cur + 36 <= data.size()) {
            const std::uint32_t nameIdx = read32(data, cur + 20);
            const std::string tag = nameIdx < pool.size() ? pool[nameIdx] : "";
            if (tag == "foreground" || tag == "background") {
                const std::uint16_t attrStart = read16(data, cur + 24);
                const std::uint16_t attrSize = read16(data, cur + 26);
                const std::uint16_t attrCount = read16(data, cur + 28);
                std::size_t attr = cur + 16 + attrStart;
                if (attrSize >= 20) {
                    for (std::uint16_t i = 0; i < attrCount && attr + attrSize <= cur + size;
                         ++i, attr += attrSize) {
                        const std::uint32_t attrNameIdx = read32(data, attr + 4);
                        const std::string attrName =
                            attrNameIdx < pool.size() ? pool[attrNameIdx] : "";
                        if (attrName != "drawable" && attrName != "src") continue;
                        const std::uint8_t attrType = data[attr + 15];
                        const std::uint32_t attrData = read32(data, attr + 16);
                        if (attrType == 0x01 && attrData != 0) {
                            (tag == "foreground" ? foreground : background).push_back(attrData);
                        }
                    }
                }
            }
        }
        cur += size;
    }
    ids->insert(ids->end(), foreground.begin(), foreground.end());
    ids->insert(ids->end(), background.begin(), background.end());
}

static void appendUnique(std::vector<std::string>* values, const std::string& value) {
    if (values == nullptr || value.empty()) return;
    if (std::find(values->begin(), values->end(), value) == values->end()) {
        values->push_back(value);
    }
}

static bool isBitmapData(const std::vector<std::uint8_t>& data) {
    if (data.size() >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
        data[3] == 'G') return true;
    if (data.size() >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff) return true;
    return data.size() >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' &&
           data[3] == 'F' && data[8] == 'W' && data[9] == 'E' && data[10] == 'B' &&
           data[11] == 'P';
}

struct IconPick {
    const ZipEntryInfo* entry = nullptr;
    int score = -1;
};

// Referenced files win over filename guesses, then recognizable icon names, then
// any bitmap in the archive.
static IconPick pickIcon(const std::vector<ZipEntryInfo>& entries,
                         const std::vector<std::string>& preferred) {
    IconPick pick;
    for (const auto& entry : entries) {
        if (!isBitmapName(entry.name)) continue;
        const int rank = iconPriority(entry.name);
        if (rank < 0) continue;
        const bool referenced =
            std::find(preferred.begin(), preferred.end(), entry.name) != preferred.end();
        const std::string lower = toLower(entry.name);
        const bool recognizable = lower.find("launcher") != std::string::npos ||
                lower.find("icon") != std::string::npos ||
                lower.find("logo") != std::string::npos;
        int score = rank;
        if (referenced) score += 100000;
        else if (recognizable) score += 10000;
        if (score > pick.score) pick = {&entry, score};
    }
    return pick;
}

template <typename Reader>
static std::uint32_t resolveIconPaths(const std::string& iconRef,
                                      const std::vector<std::uint8_t>& arsc,
                                      const std::vector<ZipEntryInfo>& entries,
                                      Reader&& readEntry,
                                      std::vector<std::string>* paths) {
    if (paths == nullptr || arsc.empty() || iconRef.empty()) return 0;
    std::uint32_t iconId = 0;
    resource_resolve_file_paths(arsc, iconRef, paths, &iconId);
    if (iconId == 0) return 0;

    std::vector<std::uint32_t> refs;
    for (const auto& path : *paths) {
        if (!endsWithCi(path, ".xml")) continue;
        for (const auto& entry : entries) {
            if (entry.name != path || entry.uncompressedSize > 1024u * 1024u) continue;
            std::vector<std::uint8_t> xml;
            if (readEntry(entry, &xml)) collectAdaptiveIconRefs(xml, &refs);
            break;
        }
    }
    for (std::uint32_t ref : refs) {
        std::vector<std::string> nested;
        if (!resource_id_file_paths(arsc, ref, &nested)) continue;
        for (const auto& path : nested) appendUnique(paths, path);
    }
    return iconId;
}

