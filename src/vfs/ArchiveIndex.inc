// ── jar:/file:archive!/entry fallback: extract straight out of the archive ZIP ──
//
// The three loose-file candidates above assume every asset was extracted to disk.
// A Unity game that ships its whole content stream inside the APK (Addressables
// over Play Asset Delivery) has entries that exist in no loose directory: the URL
// then 404s and the engine boots to a splash with nothing to load — ULTRAKILL's
// GameBuildSettings.json was exactly this, and the 500 MB behind it never flowed.
// The archive is a ZIP that is already on disk, so the entry can be served from it.

constexpr uint64_t kJarEntryMaxUncompressed = 512ull * 1024 * 1024;  // 512 MB cap

uint64_t fnv1a64_str(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string url_percent_decode(const std::string& in) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size() && hex(in[i + 1]) >= 0 && hex(in[i + 2]) >= 0) {
            out.push_back(static_cast<char>((hex(in[i + 1]) << 4) | hex(in[i + 2])));
            i += 2;
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

// "jar:file:///a/b.apk!/x/y" -> {"/a/b.apk", "x/y"}; also "file:///a/b.apk!/x" and
// "jar:/a/b.apk!/x". Both empty when the shape is none of these.
std::pair<std::string, std::string> split_archive_url(const char* originalPath) {
    std::string p = originalPath;
    // Callers above prepend "/" (absolute-path normalization) before jar: URLs
    // reach here: "/jar:file://...". Strip it — a bare "/data/..." path never
    // starts with "/jar:" so plain paths are unaffected.
    while (p.size() > 4 && p[0] == '/' && p.rfind("/jar:", 0) == 0) p.erase(0, 1);
    if (p.rfind("jar:", 0) == 0) p.erase(0, 4);
    const size_t bang = p.find("!/");
    if (bang == std::string::npos) return {};
    std::string archive = url_percent_decode(p.substr(0, bang));
    std::string entry = url_percent_decode(p.substr(bang + 2));
    if (archive.rfind("file://", 0) == 0) archive.erase(0, 7);
    else if (archive.rfind("file:", 0) == 0) archive.erase(0, 5);
    while (!archive.empty() && archive.back() == '/') archive.pop_back();
    while (!entry.empty() && entry[0] == '/') entry.erase(0, 1);
    if (archive.empty() || entry.empty()) return {};
    return {archive, entry};
}

uint16_t zip_read16(const std::uint8_t* b, size_t o) {
    return static_cast<uint16_t>(b[o] | (b[o + 1] << 8));
}
uint32_t zip_read32(const std::uint8_t* b, size_t o) {
    return static_cast<uint32_t>(b[o]) | (static_cast<uint32_t>(b[o + 1]) << 8) |
           (static_cast<uint32_t>(b[o + 2]) << 16) | (static_cast<uint32_t>(b[o + 3]) << 24);
}

// Locate the End Of Central Directory record: its byte offset in the file, or npos.
size_t zip_find_eocd(std::FILE* f, long long file_size) {
    constexpr size_t kMaxComment = 65535;
    const size_t kScan =
        static_cast<size_t>(std::min<long long>(file_size, kMaxComment + 22));
    std::vector<uint8_t> tail(kScan);
    if (::fseeko(f, file_size - static_cast<long long>(kScan), SEEK_SET) != 0)
        return std::string::npos;
    if (std::fread(tail.data(), 1, kScan, f) != kScan) return std::string::npos;
    const size_t base = static_cast<size_t>(file_size - static_cast<long long>(kScan));
    // EOCD is at least 22 bytes and sits at the very end, comment notwithstanding.
    for (size_t i = kScan - 22;; --i) {
        if (tail[i] == 0x50 && tail[i + 1] == 0x4B && tail[i + 2] == 0x05 && tail[i + 3] == 0x06) {
            const uint16_t comment_len = zip_read16(tail.data(), i + 20);
            if (base + i + 22 + comment_len == static_cast<size_t>(file_size)) {
                return base + i;
            }
        }
        if (i == 0) break;
    }
    return std::string::npos;
}

// Extract `entry` from the ZIP at `archivePath` into `destPath`. Returns the
// uncompressed byte count, or 0 on any failure (missing entry, unsupported method,
// IO error). A hand-rolled reader over stdio, not minizip: the extractor loads whole
// archives into memory, which is right for install and wrong for serving a single
// entry out of a 900 MB APK on demand.
uint64_t zip_extract_entry(const std::string& archivePath, const std::string& entry,
                           const std::string& destPath) {
    std::FILE* f = std::fopen(archivePath.c_str(), "rb");
    if (f == nullptr) return 0;
    struct FileCloser {
        std::FILE* f;
        ~FileCloser() { std::fclose(f); }
    } closer{f};

    if (std::fseek(f, 0, SEEK_END) != 0) return 0;
    const long long size = ::ftello(f);
    if (size < 22) return 0;
    const size_t eocd = zip_find_eocd(f, size);
    if (eocd == std::string::npos) return 0;
    uint8_t e[22];
    if (::fseeko(f, static_cast<off_t>(eocd), SEEK_SET) != 0) return 0;
    if (std::fread(e, 1, 22, f) != 22) return 0;
    const uint16_t total_entries = zip_read16(e, 10);
    const uint32_t cd_offset = zip_read32(e, 16);
    // Zip64 uses 0xFFFF/0xFFFFFFFF sentinels pointing at a Zip64 EOCD locator
    // this reader does not parse: say so loudly instead of walking garbage as
    // a central directory and 404ing every entry in it.
    if (total_entries == 0xFFFF || cd_offset == 0xFFFFFFFFu) {
        ktraceLine("[KuDroidVFS] zip64 archive not supported: %s\n",
                     archivePath.c_str());
        return 0;
    }

    struct Hit {
        bool valid = false;
        uint16_t method = 0;
        uint32_t crc = 0;
        uint32_t csize = 0;
        uint32_t usize = 0;
        uint32_t local_off = 0;
    };
    auto walk = [&](bool fold_case) -> Hit {
        if (::fseeko(f, static_cast<off_t>(cd_offset), SEEK_SET) != 0) return {};
        for (uint16_t n = 0; n < total_entries; ++n) {
            uint8_t h[46];
            if (std::fread(h, 1, 46, f) != 46) return {};
            if (!(h[0] == 'P' && h[1] == 'K' && h[2] == 1 && h[3] == 2)) return {};
            const uint16_t name_len = zip_read16(h, 28);
            const uint16_t extra_len = zip_read16(h, 30);
            const uint16_t comment_len = zip_read16(h, 32);
            Hit hit;
            hit.method = zip_read16(h, 10);
            hit.crc = zip_read32(h, 16);  // central dir: crc32 at 16 (14 is mod date)
            hit.csize = zip_read32(h, 20);
            hit.usize = zip_read32(h, 24);
            hit.local_off = zip_read32(h, 42);
            std::string name(name_len, 0);
            if (name_len > 0 && std::fread(name.data(), 1, name_len, f) != name_len) return {};
            if (std::fseek(f, extra_len + comment_len, SEEK_CUR) != 0) return {};
            const bool eq = fold_case
                                ? name.size() == entry.size() &&
                                      std::equal(name.begin(), name.end(), entry.begin(),
                                                 [](char a, char b) {
                                                     return std::tolower(static_cast<unsigned char>(a)) ==
                                                            std::tolower(static_cast<unsigned char>(b));
                                                 })
                                : name == entry;
            if (eq) {
                hit.valid = true;
                return hit;
            }
        }
        return {};
    };
    Hit hit = walk(false);
    if (!hit.valid) hit = walk(true);  // ZIP writers disagree on case; Android's FS does not
    if (!hit.valid) return 0;
    if (hit.usize > kJarEntryMaxUncompressed) return 0;

    // Local header: sizes there can be zero when a data descriptor follows, so the
    // central-directory figures are the ones used; the local header only yields the
    // true start of the data.
    if (::fseeko(f, static_cast<off_t>(hit.local_off), SEEK_SET) != 0) return 0;
    uint8_t lh[30];
    if (std::fread(lh, 1, 30, f) != 30) return 0;
    if (!(lh[0] == 'P' && lh[1] == 'K' && lh[2] == 3 && lh[3] == 4)) return 0;
    const uint16_t l_nlen = zip_read16(lh, 26);
    const uint16_t l_elen = zip_read16(lh, 28);
    if (::fseeko(f, static_cast<off_t>(l_nlen) + static_cast<off_t>(l_elen), SEEK_CUR) != 0)
        return 0;
    const long long dataOff = ::ftello(f);
    if (dataOff < 0) return 0;
    // Cap the compressed input too: the usize cap below does not bound csize,
    // and a lying header would otherwise malloc first and fail later.
    if (hit.csize > 512ull * 1024 * 1024) return 0;

    // STORED input streams straight off the archive in 1MB chunks (no full
    // copy: a 500MB entry would cost 500MB of heap). DEFLATED still buffers
    // its input — bounded by the cap above — while output streams.
    std::vector<uint8_t> comp;
    if (hit.method == 8 && hit.csize > 0) {
        comp.resize(hit.csize);
        if (std::fread(comp.data(), 1, hit.csize, f) != hit.csize) return 0;
    }

    // Stream to a side file and rename over the destination only after the
    // bytes verify: a crash or kill mid-extract must never leave a partial
    // file that the next run serves as good. That was the "loads a few
    // segments then dies forever after" shape — one torn 108MB bundle cached
    // once, trusted on every later hit by mere existence. Chunked 1MB writes
    // also cap RAM (the old code held compressed+output fully: ~1GB for
    // data.unity3d). Resume is at file granularity and automatic: a torn run
    // leaves only the .part orphan, and the next open re-extracts from the
    // archive. Byte-level resume across runs would need a crash-safe offset
    // journal for a local-disk copy that takes seconds — not worth the
    // failure modes it adds.
    const std::string part = destPath + ".part";
    std::remove(part.c_str());
    std::FILE* out = std::fopen(part.c_str(), "wb");
    if (out == nullptr) return 0;
    bool ok = false;
    uint64_t written = 0;
    uint32_t crc = crc32(0L, Z_NULL, 0);
    constexpr size_t kChunk = 1 << 20;
    std::vector<uint8_t> chunk(kChunk);
    if (hit.method == 0) {
        ok = true;
        uint64_t remaining = hit.csize;
        long long pos = dataOff;
        while (ok && remaining > 0) {
            const size_t n = static_cast<size_t>(
                std::min<uint64_t>(kChunk, remaining));
            if (::fseeko(f, static_cast<off_t>(pos), SEEK_SET) != 0) {
                ok = false;
                break;
            }
            const size_t got = std::fread(chunk.data(), 1, n, f);
            if (got == 0) {
                ok = false;
                break;
            }
            if (std::fwrite(chunk.data(), 1, got, out) != got) {
                ok = false;
                break;
            }
            crc = crc32(crc, chunk.data(), static_cast<uInt>(got));
            pos += static_cast<long long>(got);
            remaining -= got;
            written += got;
        }
        // STORED entries whose central size runs past EOF pad as zeros.
        while (ok && written < hit.usize) {
            const size_t n = static_cast<size_t>(
                std::min<uint64_t>(kChunk, hit.usize - written));
            std::memset(chunk.data(), 0, n);
            if (std::fwrite(chunk.data(), 1, n, out) != n) {
                ok = false;
                break;
            }
            crc = crc32(crc, chunk.data(), static_cast<uInt>(n));
            written += n;
        }
    } else if (hit.method == 8) {
        z_stream zs = {};
        zs.next_in = comp.data();
        zs.avail_in = static_cast<uInt>(comp.size());
        if (inflateInit2(&zs, -MAX_WBITS) == Z_OK) {
            ok = true;
            int rc = Z_OK;
            while (ok && rc != Z_STREAM_END) {
                zs.next_out = chunk.data();
                zs.avail_out = static_cast<uInt>(chunk.size());
                rc = inflate(&zs, Z_NO_FLUSH);
                if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
                    ok = false;
                    break;
                }
                const size_t have = chunk.size() - zs.avail_out;
                if (have > 0) {
                    if (std::fwrite(chunk.data(), 1, have, out) != have) {
                        ok = false;
                        break;
                    }
                    crc = crc32(crc, chunk.data(), static_cast<uInt>(have));
                    written += have;
                }
                if (rc == Z_BUF_ERROR && zs.avail_in == 0) break;
            }
            inflateEnd(&zs);
            if (rc != Z_STREAM_END || written != hit.usize) ok = false;
        }
    } else {
        std::fclose(out);
        std::remove(part.c_str());
        return 0;  // bzip2/encrypted: unsupported
    }
    // verify, then commit: size AND central-directory CRC before the bytes
    // become servable. Rename is atomic; a crash anywhere above leaves at
    // most the .part orphan, never a trusted partial. A zero CRC in the
    // central directory means the writer never filled it (legal for streamed
    // archives): size+fstat is the floor, crc only when known.
    if (ok && (written != hit.usize || (hit.crc != 0 && crc != hit.crc))) ok = false;
    if (ok) {
        // A counter is not proof: ENOSPC or a torn page can leave fewer bytes
        // than counted. Flush, sync, then believe fstat — not the accumulator.
        if (std::fflush(out) != 0) ok = false;
#if !defined(_WIN32)
        if (ok && ::fsync(fileno(out)) != 0) ok = false;
#endif
        if (ok) {
            struct stat st;
            if (::fstat(fileno(out), &st) != 0 ||
                static_cast<uint64_t>(st.st_size) != written) {
                ok = false;
            }
        }
    }
    std::fclose(out);
    if (!ok) {
        std::remove(part.c_str());
        return 0;
    }
    std::error_code renameEc;
    std::filesystem::rename(part, destPath, renameEc);
    if (renameEc) {
        std::remove(part.c_str());
        return 0;
    }
    return written;
}

// A game resolves assets one file at a time, so the naive path here was fopen+EOCD+
// central-directory walk per stat and per listing — over 1,100 scans of the same APK on
// one cold start. One in-memory index per archive turns every later query into a hash
// lookup and lets zip_list_dir_entries stop re-reading the directory at all.
struct ZipEntryMeta {
    uint64_t payloadOffset = 0;
    uint32_t uncompressedSize = 0;
    uint32_t compressedSize = 0;
    uint32_t crc32 = 0;
    uint16_t compressionMethod = 0;
};
struct ZipArchiveIndex {
    // Exact entry names. Names that differ only by case are distinct entries
    // (shortened resource paths rely on it), so every one is kept.
    std::unordered_map<std::string, ZipEntryMeta> entries;
    // Case-folded fallback for lookups with no exact match; first occurrence wins.
    std::unordered_map<std::string, ZipEntryMeta> folded;
    // Directory prefix ("assets/shaders/") -> immediate child names, exactly the slice
    // zip_list_dir_entries serves. Built alongside the entry map so listings are free.
    std::unordered_map<std::string, std::vector<std::string>> dirChildren;
};

std::string zip_index_key(std::string_view entry) {
    std::string key(entry);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return key;
}

// Most-specific entry whose on-disk span contains `start`.
//
// The span is PHYSICAL (payloadOffset + compressedSize): a deflated entry
// occupies csize bytes in the archive, and spanning it by usize swallowed the
// entries that follow it — audio blobs were reported as global-metadata.dat
// because that deflated entry's uncompressed size reached past them. Among
// overlapping candidates (duplicate names in a repacked archive) the smallest
// span is the one whose bytes the read actually lands in.
static bool zip_entry_containing(const std::shared_ptr<const ZipArchiveIndex>& index,
                                 long long start, std::string& outName,
                                 ZipEntryMeta& outMeta) {
    if (!index) return false;
    bool found = false;
    long long bestSpan = 0;
    for (const auto& [name, meta] : index->entries) {
        const long long span = static_cast<long long>(
            meta.compressedSize != 0 ? meta.compressedSize : meta.uncompressedSize);
        if (span <= 0) continue;
        const long long eOff = static_cast<long long>(meta.payloadOffset);
        if (start >= eOff && start < eOff + span) {
            if (!found || span < bestSpan) {
                found = true;
                bestSpan = span;
                outName = name;
                outMeta = meta;
            }
        }
    }
    return found;
}

// Lookups try the exact name first, then the case-folded one; dirChildren keys are
// lower-cased prefixes, values keep original case.
ZipArchiveIndex build_zip_index(std::FILE* f) {
    ZipArchiveIndex index;
    if (std::fseek(f, 0, SEEK_END) != 0) return index;
    const long long size = ::ftello(f);
    if (size < 22) return index;
    const size_t eocd = zip_find_eocd(f, size);
    if (eocd == std::string::npos) return index;
    uint8_t e[22];
    if (::fseeko(f, static_cast<off_t>(eocd), SEEK_SET) != 0) return index;
    if (std::fread(e, 1, 22, f) != 22) return index;
    const uint16_t total_entries = zip_read16(e, 10);
    const uint32_t cd_offset = zip_read32(e, 16);
    if (total_entries == 0xFFFF || cd_offset == 0xFFFFFFFFu) {
        ktraceLine("[KuDroidVFS] zip64 archive not supported (index build)\n");
        return index;
    }
    index.entries.reserve(total_entries * 2 + 1);
    index.folded.reserve(total_entries * 2 + 1);

    if (::fseeko(f, static_cast<off_t>(cd_offset), SEEK_SET) != 0) return index;
    for (uint16_t n = 0; n < total_entries; ++n) {
        uint8_t h[46];
        if (std::fread(h, 1, 46, f) != 46) break;
        if (!(h[0] == 'P' && h[1] == 'K' && h[2] == 1 && h[3] == 2)) break;
        const uint16_t name_len = zip_read16(h, 28);
        const uint16_t extra_len = zip_read16(h, 30);
        const uint16_t comment_len = zip_read16(h, 32);
        std::string name(name_len, 0);
        if (name_len > 0 && std::fread(name.data(), 1, name_len, f) != name_len) break;
        if (std::fseek(f, extra_len + comment_len, SEEK_CUR) != 0) break;

        const bool is_dir = !name.empty() && name.back() == '/';
        ZipEntryMeta meta;
        meta.compressionMethod = zip_read16(h, 10);
        meta.compressedSize = zip_read32(h, 20);
        meta.uncompressedSize = zip_read32(h, 24);
        meta.crc32 = zip_read32(h, 16);
        const uint32_t local_off = zip_read32(h, 42);

        // The central directory does not say where the data starts — the local header
        // does, and its name/extra lengths are allowed to differ from the CD's. Resolving
        // the payload offset here keeps every later stat a pure memory lookup.
        if (!is_dir) {
            const off_t saved = ::ftello(f);
            if (::fseeko(f, static_cast<off_t>(local_off), SEEK_SET) != 0) continue;
            uint8_t lh[30];
            if (std::fread(lh, 1, 30, f) != 30) continue;
            if (!(lh[0] == 'P' && lh[1] == 'K' && lh[2] == 3 && lh[3] == 4)) continue;
            const uint16_t l_nlen = zip_read16(lh, 26);
            const uint16_t l_elen = zip_read16(lh, 28);
            meta.payloadOffset = static_cast<uint64_t>(local_off) + 30 + l_nlen + l_elen;
            ::fseeko(f, saved, SEEK_SET);
        }

        // First occurrence wins, matching the old sequential walk's first match.
        index.entries.try_emplace(name, meta);
        index.folded.try_emplace(zip_index_key(name), meta);
        if (is_dir) {
            std::string dir = name.substr(0, name.size() - 1);
            if (!dir.empty()) {
                const size_t slash = dir.rfind('/');
                const std::string parent = slash == std::string::npos ? "" : dir.substr(0, slash + 1);
                index.dirChildren[zip_index_key(parent)].push_back(dir.substr(slash + 1));
            }
        } else if (const size_t slash = name.rfind('/'); slash != std::string::npos) {
            index.dirChildren[zip_index_key(name.substr(0, slash + 1))].push_back(
                name.substr(slash + 1));
        }
    }
    return index;
}

// Build once per archive, guarded; a failed build is cached as an empty index so a
// broken file is re-probed at stat rate, not re-parsed at stat rate.
// Read-mostly: shared_mutex lets concurrent statters proceed in parallel; only
// the one-time build per archive takes the write lock. Indices are shared_ptr:
// a raw interior pointer would dangle when another archive's try_emplace rehashes
// the map while a first thread still reads its index.
std::shared_ptr<const ZipArchiveIndex> get_or_build_zip_index(const std::string& archivePath) {
    struct Cache {
        std::shared_mutex mtx;
        std::unordered_map<std::string, std::shared_ptr<const ZipArchiveIndex>> byArchive;
    };
    static Cache cache;
    {
        std::shared_lock<std::shared_mutex> lock(cache.mtx);
        const auto it = cache.byArchive.find(archivePath);
        if (it != cache.byArchive.end()) return it->second;
    }
    std::unique_lock<std::shared_mutex> lock(cache.mtx);
    auto [it, inserted] = cache.byArchive.try_emplace(archivePath);
    if (inserted) {
        auto built = std::make_shared<ZipArchiveIndex>();
        std::FILE* f = std::fopen(archivePath.c_str(), "rb");
        if (f != nullptr) {
            *built = build_zip_index(f);
            std::fclose(f);
        }
        it->second = std::move(built);
    }
    return it->second;
}

bool zip_stat_entry(const std::string& archivePath, const std::string& entry,
                    uint64_t* outOffset, uint64_t* outSize, uint16_t* outMethod) {
    const auto index = get_or_build_zip_index(archivePath);
    if (!index) return false;
    const ZipEntryMeta* meta = nullptr;
    if (const auto it = index->entries.find(entry); it != index->entries.end()) {
        meta = &it->second;
    } else if (const auto fit = index->folded.find(zip_index_key(entry));
               fit != index->folded.end()) {
        meta = &fit->second;
    }
    if (meta == nullptr) return false;
    if (outOffset) *outOffset = meta->payloadOffset;
    if (outSize) *outSize = meta->uncompressedSize;
    if (outMethod) *outMethod = meta->compressionMethod;
    return true;
}

std::vector<std::string> zip_list_dir_entries(const std::string& archivePath,
                                              const std::string& dirPrefix) {
    std::vector<std::string> result;
    const auto index = get_or_build_zip_index(archivePath);
    if (!index) return result;

    std::string prefix = dirPrefix;
    while (!prefix.empty() && prefix[0] == '/') prefix.erase(0, 1);
    if (!prefix.empty() && prefix.back() != '/') prefix.push_back('/');
    const auto it = index->dirChildren.find(zip_index_key(prefix));
    if (it != index->dirChildren.end()) result = it->second;
    return result;
}

// Cache home for extracted jar entries. Lives under android_root/data/cache: inside
// the VFS root, writable, and already a remapped prefix a guest cannot see.
std::string extract_jar_entry_to_cache(const std::string& archivePath,
                                       const std::string& entryName,
                                       const std::string& androidRoot) {
    std::error_code ec;
    if (!std::filesystem::exists(archivePath, ec) ||
        !std::filesystem::is_regular_file(archivePath, ec)) {
        return {};
    }
    // Serialise per entry, not globally: the same URL can be resolved from
    // several threads during startup (double-extracting it is wasted IO), but
    // a single mutex around a 500MB inflate stalls every other asset behind
    // it. Striped by entry hash, with double-checked existence inside.
    static std::mutex s_jarStripes[16];
    std::mutex& stripe =
        s_jarStripes[fnv1a64_str(archivePath + "\x01" + entryName) % 16];
    std::lock_guard<std::mutex> lock(stripe);

    const std::string dir = androidRoot + "/data/cache/jar_entries";
    std::filesystem::create_directories(dir, ec);
    if (ec) return {};

    // One cache file per (archive identity, entry) pair. Identity folds in the
    // archive's size+mtime — not just its path — so an updated APK with a
    // same-size entry cannot serve stale bytes forever (the old file simply
    // stops being addressed; it is not deleted, which keeps the extractor
    // lock-free on the cleanup path).
    struct stat archiveSt;
    uint64_t archiveIdHi = 0, archiveIdLo = 0;
    if (::stat(archivePath.c_str(), &archiveSt) == 0) {
        archiveIdHi = static_cast<uint64_t>(archiveSt.st_size);
#if defined(__APPLE__)
        archiveIdLo = static_cast<uint64_t>(archiveSt.st_mtimespec.tv_sec);
#else
        archiveIdLo = static_cast<uint64_t>(archiveSt.st_mtime);
#endif
    }
    std::string base = entryName;
    const size_t slash = base.find_last_of('/');
    if (slash != std::string::npos) base = base.substr(slash + 1);
    if (base.size() > 64) base.resize(64);
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx",
                  static_cast<unsigned long long>(fnv1a64_str(
                      archivePath + "\x01" + std::to_string(archiveIdHi) + "\x01" +
                      std::to_string(archiveIdLo) + "\x01" + entryName)));
    const std::string dest = dir + "/" + hex + "_" + base;

    if (std::filesystem::exists(dest, ec) && std::filesystem::is_regular_file(dest, ec)) {
        // Trust but verify: a torn cache file (killed mid-extract by an older
        // build, before atomic commit) would otherwise be served forever. Size
        // is the cheap check against the central directory; a mismatch deletes
        // and re-extracts below. Content CRC was verified at commit time, and
        // only the atomic rename makes a file servable, so no full reread here.
        uint64_t payloadOff = 0, payloadSize = 0;
        uint16_t method = 0;
        std::error_code sizeEc;
        const uint64_t cachedSize = std::filesystem::file_size(dest, sizeEc);
        if (!sizeEc && zip_stat_entry(archivePath, entryName, &payloadOff, &payloadSize, &method) &&
            cachedSize == payloadSize) {
            return dest;
        }
        std::filesystem::remove(dest, ec);
    }
    const uint64_t n = zip_extract_entry(archivePath, entryName, dest);
    if (n == 0) {
        // Leave no zero-byte marker: a stale cache file would be served forever after.
        std::error_code rm;
        std::filesystem::remove(dest, rm);
        return {};
    }
    return dest;
}

