size_t vfs_fread(void* buf, size_t size, size_t count, FILE* stream) {
    const void* const caller = __builtin_return_address(0);
    const int sid = apk_stream_id(stream);
    const bool apk = (sid >= 0);
    const bool hotTrace = kudroid::log::trace_hot();
    const std::chrono::steady_clock::time_point t0 =
        apk ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

    size_t n = 0;
    if (apk) {
        const size_t total_want = size * count;
        if (total_want == 0 || size == 0) return 0;
        const int fd = g_apkStreamFd[sid].load(std::memory_order_relaxed);
        // Read and advance under the stream lock: two threads sharing this FILE*
        // must consume one cursor between them, never the same bytes twice.
        std::lock_guard<std::mutex> slock(g_apkStreamMtx[sid]);
        if (!apk_stream_live(sid, stream)) {
            // The slot no longer names this FILE*, so the read is unservable and
            // returns zero bytes -- the one read failure the SHORT trace cannot
            // report, because it returns before the trace block.
            if (g_apkStreamDeadLogged[sid].fetch_add(1, std::memory_order_relaxed) < 4) {
                ktraceLine("[KuDroidApkS] sid=%d DEAD read size=%zu%s\n", sid, size * count,
                           trace_caller_text(caller));
            }
            errno = EBADF;
            return 0;
        }
        const off_t cur_pos = g_apkStreamPos[sid].load(std::memory_order_relaxed);
        size_t total_read = 0;
        char* out_buf = static_cast<char*>(buf);
        if (fd >= 0) {
            while (total_read < total_want) {
                const ssize_t rd = ::pread(fd, out_buf + total_read, total_want - total_read,
                                          cur_pos + static_cast<off_t>(total_read));
                if (rd < 0) {
                    if (errno == EINTR) continue;
                    g_apkStreamErr[sid].store(1, std::memory_order_relaxed);
                    break;
                }
                if (rd == 0) {
                    g_apkStreamEof[sid].store(1, std::memory_order_relaxed);
                    break;
                }
                total_read += static_cast<size_t>(rd);
            }
        } else {
            // No descriptor behind the stream: an error, not a quiet zero a caller could
            // loop on forever waiting for EOF.
            g_apkStreamErr[sid].store(1, std::memory_order_relaxed);
            errno = EBADF;
        }
        g_apkStreamPos[sid].store(cur_pos + static_cast<off_t>(total_read), std::memory_order_relaxed);
        n = total_read / size;
    } else {
        n = std::fread(buf, size, count, stream);
    }

    if (apk) {
        const long long tookMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0)
                .count();
        uint64_t op = 0;
        if (sid >= 0) op = g_apkStreamOps[sid].fetch_add(1, std::memory_order_relaxed) + 1;
        // First ops of every stream: a clip load / bundle stream shows its
        // whole IO shape here (open -> seek -> header read -> data reads).
        // Per-volume counters and 16MB sampling cannot see this pattern.
        if (hotTrace && sid >= 0 && op <= 16 &&
            g_apkStreamTraceNo[sid].load(std::memory_order_relaxed) < kOpTracedStreams) {
            ktraceLine(
                         "[KuDroidApkS] sid=%d op=%llu fread want=%zu got=%zu pos=%ld\n",
                         sid, static_cast<unsigned long long>(op), size * count, n * size,
                         vfs_ftell(stream));
        } else if (audio_trace_take(sid)) {
            // Audio thread: its own sample of the same shape, past the instance cap.
            ktraceLine(
                         "[KuDroidApkS] sid=%d AUDIO op=%llu fread want=%zu got=%zu pos=%ld\n",
                         sid, static_cast<unsigned long long>(op), size * count, n * size,
                         vfs_ftell(stream));
        }
        // Short read: fewer elements than asked. On the archive stream that is
        // a truncated slice — the exact geometry that turns a valid FSB5 into
        // FMOD_ERR_FILE_BAD. Rare and decisive, so logged (capped per stream).
        if (n < count && sid >= 0 &&
            g_apkStreamShortLogged[sid].fetch_add(1, std::memory_order_relaxed) < 6) {
            ktraceLine(
                         "[KuDroidApkS] sid=%d SHORT want=%zu got=%zu pos=%ld eof=%d errno=%d\n",
                         sid, size * count, n * size, vfs_ftell(stream),
                         vfs_feof(stream) ? 1 : 0, errno);
        }
        // Slow read: the 90 s scene-load stall surfaced only as a gap between
        // volume milestones; naming the op that ate the wall-clock removes the
        // guesswork (locked VFS work, host FS pressure, guest-side spin).
        static std::atomic<int> s_slowLogged{0};
        if (tookMs >= 50 && s_slowLogged.load(std::memory_order_relaxed) < 24) {
            s_slowLogged.fetch_add(1, std::memory_order_relaxed);
            ktraceLine(
                         "[KuDroidApkS] sid=%d SLOW %lldms want=%zu got=%zu pos=%ld\n",
                         sid, tookMs, size * count, n * size, vfs_ftell(stream));
        }
    }
    if (n > 0 && apk) {
        static std::atomic<int> s_logged{0};
        static std::atomic<unsigned long long> s_bytes{0};
        static const auto s_start = std::chrono::steady_clock::now();
        const unsigned long long total =
            s_bytes.fetch_add(n * size) + n * size;
        const unsigned long long ms =
            static_cast<unsigned long long>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - s_start)
                    .count());
        // First 25 lines keep header detail; totals every 1MB after that so
        // later content reads (catalog, bundles) stay visible past the sniff.
        if (s_logged.load() < 25) {
            ++s_logged;
            KTRACE_HOT("[KuDroidApkF] fread bytes=%zu\n", n * size);
        } else if (total / (1024 * 1024) != (total - n * size) / (1024 * 1024)) {
            ktraceLine("[KuDroidApkF] fread total=%lluMB t=%llums\n",
                         total / (1024 * 1024), ms);
        }
        // Magic sniff: FMOD rejects every bank it is handed ("Error loading
        // file") while meshes from the same APK render. The bytes are read
        // from the buffer just filled — zero extra I/O — and ftell is
        // userspace. Starts past 4MB of flow so startup probing (EOCD,
        // central directory, catalog) does not consume the budget.
        //
        // Sampled every 16MB of flow, not "first 30": a fixed early budget was
        // all eaten by bundle streaming before the audio-clip loads began, so
        // the one window that mattered (Cannot load audio data at 17:31:27)
        // had no samples left. A per-flow rate keeps samples landing across
        // the whole session — audio included — at the same small log volume.
        // The offset is mapped back to its ZIP entry via the in-memory index,
        // so the line names the file the guest is actually reading, not just
        // four bytes at a mystery offset.
        static std::atomic<unsigned long long> s_nextSniff{4ULL * 1024 * 1024};
        static std::atomic<int> s_magicCount{0};
        if (hotTrace && total >= s_nextSniff.load(std::memory_order_relaxed) && s_magicCount.load() < 64 &&
            buf != nullptr && n * size >= 4) {
            s_magicCount.fetch_add(1, std::memory_order_relaxed);
            s_nextSniff.fetch_add(16ULL * 1024 * 1024, std::memory_order_relaxed);
            const long off = vfs_ftell(stream);
            const long start = off >= 0 ? off - static_cast<long>(n * size) : -1;
            const auto* b = static_cast<const unsigned char*>(buf);
            // Which entry does this offset belong to? The stream path is the
            // APK the tracker recorded; a miss (unindexed archive, offset past
            // the entries) falls back to the raw-bytes line alone.
            std::string entryName;
            {
                std::lock_guard<std::mutex> vlock(g_freadVolMtx);
                const auto pit = g_freadPaths.find(stream);
                if (pit != g_freadPaths.end()) {
                    const auto index = get_or_build_zip_index(pit->second);
                    if (index && start >= 0) {
                        std::string name;
                        ZipEntryMeta meta;
                        if (zip_entry_containing(index, start, name, meta)) {
                            entryName = name;
                        }
                    }
                }
            }
            ktraceLine(
                         "[KuDroidApkF] magic off=%ld n=%zu %02x%02x%02x%02x '%c%c%c%c'%s%s\n",
                         start, n * size, b[0], b[1], b[2], b[3],
                         b[0] >= 32 && b[0] < 127 ? b[0] : '.',
                         b[1] >= 32 && b[1] < 127 ? b[1] : '.',
                         b[2] >= 32 && b[2] < 127 ? b[2] : '.',
                         b[3] >= 32 && b[3] < 127 ? b[3] : '.',
                         entryName.empty() ? "" : " entry=", entryName.c_str());
        }
        // Account this read's bytes into any audio-blob window on the same
        // archive, whichever handle carried them.
        {
            std::lock_guard<std::mutex> vlock(g_freadVolMtx);
            const long pos = vfs_ftell(stream);
            const long start = pos >= 0 ? pos - static_cast<long>(n * size) : -1;
            const auto pit = g_freadPaths.find(stream);
            if (pit != g_freadPaths.end() && start >= 0) {
                auto wit = g_fsbWindows.find(pit->second);
                bool matched = false;
                if (wit != g_fsbWindows.end()) {
                    // The read is charged to the blob that OWNS those bytes, and a
                    // packed bank puts the next blob's header inside the previous
                    // blob's last block, so a range test alone charged those header
                    // bytes to the previous clip. The header bytes decide.
                    FsbWindow* w = fsb_window_at(wit->second, start, pos);
                    if (w != nullptr) {
                        matched = true;
                        // The FMOD parse sequence lives or dies between these reads,
                        // and an absent data-read is the fact that convicts the
                        // parser (it stopped asking) over the stream (it served
                        // wrong bytes). Budget per blob, not per process: a
                        // process-global budget is spent by the first clips of the
                        // bank and every later clip then loads unobserved.
                        if (hotTrace && fsb_window_log_read(*w)) {
                            ktraceLine(
                                         "[KuDroidFmod] in-blob read sid=%d off=%ld size=%zu pos=%ld "
                                         "blob=[%ld+%llu]%s\n",
                                         apk_stream_id(stream), start, n * size, pos, w->start,
                                         static_cast<unsigned long long>(w->expected),
                                         trace_caller_text(caller));
                        }
                        fsb_window_cover(*w, start, pos);
                    }
                    fsb_windows_report_locked(pit->second);
                }
                if (!matched) vfs_fsb_note_unmatched(pit->second.c_str(), n * size);
            }
        }
        // FSB5 probe: Unity hands an AudioClip's FSB slice to FMOD from the
        // resource it just read. If a read begins at an FSB5 header, record
        // the exact geometry so we can tell "FMOD was given good bytes and
        // still failed" from "Unity never streamed the FSB at all". The
        // served line names the containing ZIP entry plus its method and
        // size: a deflated slice served raw, or a slice whose entry span is
        // wrong, is instantly visible. Rate-limited: an FSB header appears
        // once per clip load.
        static std::atomic<int> s_fsbCount{0};
        // Archive streams only: the window map below is keyed by archive path,
        // so a hit on any other stream can never be accounted, and a
        // shader-cache or /proc read has no business being scanned for FSB5.
        if (apk && buf != nullptr && n * size >= 4) {
            const auto* base = static_cast<const unsigned char*>(buf);
            const size_t readBytes = n * size;
            const long pos = vfs_ftell(stream);
            const long readStart = pos >= 0 ? pos - static_cast<long>(readBytes) : -1;
            // Scan all complete headers with the host's vectorized byte search.
            std::vector<std::pair<size_t, FsbHeader>> hits;  // (offset in read, header)
            std::vector<const uint8_t*> hitPtr;
            size_t i = 0;
            while (readBytes - i >= 60 && hits.size() < kFsbChainMax) {
                const auto* candidate = static_cast<const unsigned char*>(
                        std::memchr(base + i, 'F', readBytes - i - 59));
                if (candidate == nullptr) break;
                const size_t hit = static_cast<size_t>(candidate - base);
                i = hit + 1;
                if (std::memcmp(candidate, "FSB5", 4) != 0) continue;
                if (readStart < 0) break;
                const FsbHeader h = fsb_parse_header(candidate);
                if (h.total < 60 || h.total > kFsbBlobMax) continue;
                hits.emplace_back(hit, h);
                hitPtr.push_back(candidate);
                // Header-shaped bytes inside the declared body are not new blobs.
                const size_t span = static_cast<size_t>(h.total);
                if (span >= readBytes - hit) break;
                i = hit + span;
            }
            if (!hits.empty()) {
                // Detection and window accounting run for every hit, so coverage
                // stays complete. Only the DESCRIPTION is budgeted: the zip
                // lookup, the 128-byte hex dump and the ~600-byte trace line are
                // per-hit work on the guest's synchronous read path, and stderr
                // here is unbuffered (one write syscall each). Left unbounded
                // this produced 1508 such lines in one 2.3s audio burst, which is
                // the stutter, not the diagnosis.
                const int seen = s_fsbCount.fetch_add(1, std::memory_order_relaxed);
                const long start = readStart + static_cast<long>(hits.front().first);
                const FsbHeader& h0 = hits.front().second;
                const size_t avail = readBytes - hits.front().first;
                // First 24 blobs by arrival, then only a blob whose header shape is
                // new (see fsb_shape_is_new): the description budget follows what
                // decides decodability instead of which blob was read first. The
                // shape is registered even when the blob is described anyway, so a
                // shape that first appeared in the first 24 is not seen as new later.
                char shape[96] = {};
                bool newShape = false;
                if (hotTrace) {
                    std::snprintf(shape, sizeof(shape), "v%u/m%u/shs%u/nts%u/num%u",
                                  h0.version, h0.mode, h0.shs, h0.nts, h0.numSamples);
                    newShape = fsb_shape_is_new(shape);
                }
                const bool describe = hotTrace && (seen < 24 || newShape);
                std::string entry;
                uint16_t method = 0xFFFF;
                uint32_t usize = 0;
                // Distance from the blob to its ZIP entry's bounds. A blob that
                // runs past the entry end is a slice FMOD can never read whole,
                // which is the one geometry that turns a valid FSB5 into
                // "Error loading file" without the bytes being wrong.
                long long entryOff = -1;
                if (describe && start >= 0) {
                    std::string archivePath;
                    {
                        std::lock_guard<std::mutex> vlock(g_freadVolMtx);
                        const auto pit = g_freadPaths.find(stream);
                        if (pit != g_freadPaths.end()) archivePath = pit->second;
                    }
                    if (!archivePath.empty()) {
                        const auto index = get_or_build_zip_index(archivePath);
                        std::string name;
                        ZipEntryMeta meta;
                        if (zip_entry_containing(index, start, name, meta)) {
                            entry = name;
                            method = meta.compressionMethod;
                            usize = meta.uncompressedSize;
                            entryOff = static_cast<long long>(start) -
                                       static_cast<long long>(meta.payloadOffset);
                        }
                    }
                }
                // Chain check from the same buffer: a valid second header at the
                // first blob's declared end proves the resource data itself is
                // well-formed and that the declared length is right.
                char nextDesc[96] = "n/a";
                if (describe && h0.total + 28 <= avail) {
                    const auto* g = hitPtr.front() + h0.total;
                    if (std::memcmp(g, "FSB5", 4) == 0) {
                        const FsbHeader hn = fsb_parse_header(g);
                        std::snprintf(nextDesc, sizeof(nextDesc),
                                      "FSB5@%llu num=%u dataSize=%u mode=%u",
                                      static_cast<unsigned long long>(h0.total),
                                      hn.numSamples, hn.dataSize, hn.mode);
                    } else {
                        std::snprintf(nextDesc, sizeof(nextDesc),
                                      "none@%llu (%02x%02x%02x%02x)",
                                      static_cast<unsigned long long>(h0.total),
                                      g[0], g[1], g[2], g[3]);
                    }
                }
                char hex[324] = {0};
                if (describe) {
                    // 128 bytes: FSB5 header (60) + sample header area, where the
                    // Vorbis setup chunk lives. 128 individual snprintf calls per
                    // hit is audit work, not per-read work.
                    const size_t hb = avail < 128 ? avail : 128;
                    size_t o = 0;
                    for (size_t i = 0; i < hb; ++i) {
                        o += static_cast<size_t>(
                            std::snprintf(hex + o, sizeof(hex) - o, "%02x", hitPtr.front()[i]));
                    }
                }
                // Sample header (first numSamples u64s after the 60-byte FSB5
                // v1 header): bit 0 = next-chunk flag, bits 1-4 = frequency,
                // bit 5 = channels-1, bits 6-33 = dataOffset/16, bits 34-63 =
                // sample count (python-fsb5 __init__.py). The chunk list
                // follows: u32 [next:1][size:24][type:7], VORBISDATA (11)
                // carries crc32 + unknown. Verifying crc membership and
                // chunk presence turns "valid header, FMOD fails" into a
                // parse-level fact.
                char sampleDesc[160] = "n/a";
                // 76 bytes reach: 60-byte header + u64 sample field + chunk
                // word + VORBISDATA crc. The old guard demanded blobTotal+28
                // served bytes — a streaming reader never satisfies that, so
                // every run logged sample=[n/a] no matter what it read.
                if (describe && h0.numSamples >= 1 && avail >= 76) {
                    const auto* sh = hitPtr.front() + 60;
                    auto rd64 = [&](size_t o) {
                        uint64_t v = 0;
                        for (int k = 7; k >= 0; --k) v = (v << 8) | sh[o + k];
                        return v;
                    };
                    const uint64_t raw0 = rd64(0);
                    const uint32_t freqIdx = static_cast<uint32_t>((raw0 >> 1) & 0xF);
                    static const uint32_t kFreqTable[10] = {0,  8000,  11000, 11025, 16000,
                                                            22050, 24000, 32000, 44100, 48000};
                    const uint32_t freq = freqIdx < 10 ? kFreqTable[freqIdx] : 0;
                    const uint32_t channels = static_cast<uint32_t>(((raw0 >> 5) & 0x1)) + 1;
                    const uint32_t dataOff = static_cast<uint32_t>((raw0 >> 6) & 0xFFFFFFF) * 16;
                    const uint32_t nSamples = static_cast<uint32_t>((raw0 >> 34) & 0x3FFFFFFF);
                    uint32_t chunkType = 0, chunkSize = 0, crc32 = 0;
                    if (h0.shs >= 12) {
                        const uint32_t cw = rd64(8) & 0xFFFFFFFF;
                        chunkSize = (cw >> 1) & 0x7FFFFF;
                        chunkType = (cw >> 25) & 0x7F;
                    }
                    if (chunkType == 11 && h0.shs >= 16) {
                        crc32 = fsb_rd32(sh + 12);
                    }
                    std::snprintf(sampleDesc, sizeof(sampleDesc),
                                  "freq=%u ch=%u dataOff=%u(%sdataSize) n=%u chunk1=%u/%u%s crc=%08X",
                                  freq, channels, dataOff,
                                  dataOff <= h0.dataSize ? "<=" : ">>",
                                  nSamples, chunkType, chunkSize,
                                  chunkType == 11 ? " VORBISDATA" : "", crc32);
                }
                if (describe) {
                    ktraceLine(
                                 "[KuDroidFmod] served FSB5 sid=%d at apk_off=%ld size=%zu hits=%zu seen=%d "
                                 "method=%u usize=%u entryOff=%lld entryRem=%lld "
                                 "ver=%u num=%u shs=%u nts=%u "
                                 "dataSize=%u mode=%u blobTotal=%llu next=[%s] hex=%s "
                                 "sample=[%s] shape=%s entry=%s%s\n",
                                 sid, start, avail, hits.size(), seen, method, usize, entryOff,
                                 entryOff >= 0
                                     ? static_cast<long long>(usize) - entryOff -
                                           static_cast<long long>(h0.total)
                                     : -1,
                                 h0.version, h0.numSamples, h0.shs, h0.nts,
                                 h0.dataSize, h0.mode, static_cast<unsigned long long>(h0.total),
                                 nextDesc, hex, sampleDesc, shape,
                                 entry.empty() ? "(none)" : entry.c_str(),
                                 trace_caller_text(caller));
                }
                // Decisive follow-up: when FMOD accepts the bank it streams
                // through THIS handle; when it rejects the slice it reads
                // little or nothing on it. Keyed by handle so unrelated
                // concurrent streams cannot mask the FSB stream's reads.
                {
                    std::lock_guard<std::mutex> vlock(g_freadVolMtx);
                    const auto pit2 = g_freadPaths.find(stream);
                    if (pit2 != g_freadPaths.end()) {
                        bool newBlob = false;
                        for (std::size_t k = 0; k < hits.size(); ++k) {
                            const long wstart = readStart + static_cast<long>(hits[k].first);
                            // A blob the guest is reading in THIS call is live by
                            // definition; retirement is by age, from fclose and the
                            // first-header path only, never from here.
                            const size_t bodyLo = hits[k].first + 60;
                            const size_t bodyEnd = std::min(
                                hits[k].first + static_cast<size_t>(hits[k].second.total), readBytes);
                            const bool bodyInThisRead = bodyEnd > bodyLo;
                            FsbWindow* known = fsb_window_at(g_fsbWindows[pit2->second],
                                                            wstart, wstart + 60);
                            if (known == nullptr || known->start != wstart) {
                                // The report can empty the list and drop the map
                                // entry, so the list is re-fetched after it.
                                fsb_windows_evict_locked(pit2->second, kFsbStaleMs);
                                newBlob = true;
                                auto& list = g_fsbWindows[pit2->second];
                                fsb_window_register(list, wstart, hits[k].second, hitPtr[k],
                                                    bodyInThisRead ? readStart + static_cast<long>(bodyLo) : 0,
                                                    bodyInThisRead ? readStart + static_cast<long>(bodyEnd) : 0);
                                fsb_windows_report_locked(pit2->second);
                            } else {
                                // An existing window may not have had its header
                                // captured (registered from a body hit in an older
                                // build): capture it now so the next read's header
                                // attribution works.
                                if (std::memcmp(known->hdr, hitPtr[k], 60) != 0) {
                                    std::memcpy(known->hdr, hitPtr[k], 60);
                                }
                                fsb_window_cover(*known, wstart, wstart + 60);
                                if (bodyInThisRead) {
                                    fsb_window_cover(*known, readStart + static_cast<long>(bodyLo),
                                                     readStart + static_cast<long>(bodyEnd));
                                }
                            }
                        }
                        if (newBlob) {
                            // Armed only for a blob seen for the first time.
                            // Re-arming on every hit let a retrying guest log the
                            // next eight reads of the same handle each time it
                            // asked again, which is a second unbounded log loop on
                            // the same path.
                            g_fsbFollowup[stream] = 8;
                        }
                    }
                }
            }
        }
        bool followLine = false;
        {
            std::lock_guard<std::mutex> vlock(g_freadVolMtx);
            const auto fit = g_fsbFollowup.find(stream);
            if (fit != g_fsbFollowup.end()) {
                if (fit->second > 0) {
                    --fit->second;
                    followLine = true;
                } else {
                    g_fsbFollowup.erase(fit);
                }
            }
        }
        if (hotTrace && followLine) {
            // req= names what the reader ASKED for, not just what arrived: a blob
            // reported SHORT is either a reader that stops asking (req is one small
            // chunk) or a stream that ends early (req is the whole blob, n is short).
            ktraceLine(
                         "[KuDroidFmod] post-FSB5 read req=%zu x %zu -> %zu bytes pos=%ld%s\n",
                         size, count, n, vfs_ftell(stream), trace_caller_text(caller));
        }
    }
    if (n > 0) {
        // Lock-free volume accounting (see above): resolve the path only on
        // stream change or fclose epoch bump, batch bytes/ops thread-locally.
        const uint64_t ep = g_freadEpoch.load(std::memory_order_relaxed);
        if (t_volbatch.stream != stream || t_volbatch.epoch != ep) {
            fread_vol_flush();
            t_volbatch.stream = stream;
            t_volbatch.epoch = ep;
            std::lock_guard<std::mutex> lock(g_freadVolMtx);
            const auto it = g_freadPaths.find(stream);
            t_volbatch.path = (it != g_freadPaths.end()) ? it->second : std::string();
        }
        if (!t_volbatch.path.empty()) {
            if (t_volbatch.path == "/dev/urandom" || t_volbatch.path == "/dev/random") {
                static std::atomic<int> entropyReads{0};
                if (entropyReads.fetch_add(1, std::memory_order_relaxed) < 16) {
                    const int savedErrno = errno;
                    ktraceLine("[KuDroidNet] entropy fread count=%zu size=%zu -> %zu\n", count, size, n);
                    errno = savedErrno;
                }
            }
            t_volbatch.bytes += n * size;
            ++t_volbatch.ops;
            if (t_volbatch.bytes >= 256ULL * 1024) fread_vol_flush();
        }
    }
    if (n * size >= 1048576) {
        static std::atomic<int> s_big{0};
        if (s_big.load() < 15) {
            ++s_big;
            ktraceLine("[KuDroidIO] big fread bytes=%zu\n", n * size);
        }
    }
    // Audio thread on a stream that is not the archive: FMOD also reads banks it
    // opened itself (extracted cache, loose .fsb/.bank). Traced so a failing
    // createSound keeps a complete picture whichever file it actually read from.
    if (!apk && audio_trace_take(-1)) {
        ktraceLine("[KuDroidApkS] sid=-1 AUDIO fread want=%zu got=%zu pos=%ld\n", size * count,
                     n * size, std::ftell(stream));
    }
    return n;
}

int kudroid_test_fsb_window_count(const char* archivePath) {
    if (!archivePath) return 0;
    std::lock_guard<std::mutex> lock(g_freadVolMtx);
    const auto it = g_fsbWindows.find(archivePath);
    return it == g_fsbWindows.end() ? 0 : static_cast<int>(it->second.size());
}

long long kudroid_test_fsb_first_uncovered(const char* archivePath, int nth) {
    if (!archivePath || nth < 0) return -1;
    std::lock_guard<std::mutex> lock(g_freadVolMtx);
    const auto it = g_fsbWindows.find(archivePath);
    if (it == g_fsbWindows.end()) return -1;
    const auto& list = it->second;
    if (static_cast<std::size_t>(nth) >= list.size()) return -1;
    const FsbWindow& w = list[static_cast<std::size_t>(nth)];
    const uint64_t blocks = (w.expected + kFsbBlock - 1) / kFsbBlock;
    for (uint64_t b = 0; b < blocks; ++b) {
        const uint64_t word = (static_cast<std::size_t>(b >> 6) < w.covered.size())
                                  ? w.covered[b >> 6]
                                  : 0;
        if (!(word & (1ull << (b & 63)))) {
            return static_cast<long long>(w.start) + static_cast<long long>(b * kFsbBlock);
        }
    }
    return -1;
}

int vfs_fclose(FILE* stream) {
    // fclose(NULL) is undefined behaviour — both host libcs trap on it — so the guard
    // has to cover the call as well as the tracking below it. Returning EOF keeps the
    // guard's promise instead of defeating it: the old code tested for null and then
    // called fclose(NULL) anyway.
    if (stream == nullptr) {
        errno = EINVAL;
        return EOF;
    }
    // Flush this thread's batch for the stream before the map entry goes
    // away, then bump the epoch so other threads' stale FILE* caches
    // re-resolve instead of attributing to a recycled address.
    if (t_volbatch.stream == stream) fread_vol_flush();
    g_freadEpoch.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_apkStreamsMtx);
        for (int i = 0; i < kTrackedStreams; ++i) {
            if (g_apkStreams[i].load(std::memory_order_relaxed) ==
                reinterpret_cast<uintptr_t>(stream)) {
                const uint64_t ops =
                    g_apkStreamOps[i].load(std::memory_order_relaxed);
                if (g_apkStreamTraceNo[i].load(std::memory_order_relaxed) < kOpTracedStreams) {
                    ktraceLine("[KuDroidApkS] sid=%d close after %llu ops\n", i,
                               static_cast<unsigned long long>(ops));
                } else if (audio_thread()) {
                    ktraceLine("[KuDroidApkS] sid=%d AUDIO close after %llu ops\n", i,
                               static_cast<unsigned long long>(ops));
                }
                // Retire the slot under the stream lock, so a reader that is
                // already inside an op finishes against this record and a reader
                // that arrives afterwards fails the apk_stream_live() check
                // instead of writing into whatever file reuses the slot.
                {
                    std::lock_guard<std::mutex> slock(g_apkStreamMtx[i]);
                    g_apkStreamFd[i].store(-1, std::memory_order_relaxed);
                    g_apkStreamSize[i].store(0, std::memory_order_relaxed);
                    g_apkStreamPos[i].store(0, std::memory_order_relaxed);
                    g_apkStreamEof[i].store(0, std::memory_order_relaxed);
                    g_apkStreamErr[i].store(0, std::memory_order_relaxed);
                    g_apkStreams[i].store(0, std::memory_order_release);
                }
                break;
            }
        }
    }
    {
        std::lock_guard<std::mutex> vlock(g_freadVolMtx);
        const auto pit = g_freadPaths.find(stream);
        if (pit != g_freadPaths.end()) {
            // The guest closed this handle; blobs it stopped reading are retired
            // here rather than from the read path it is blocked in.
            fsb_windows_evict_locked(pit->second, kFsbStaleMs);
            // The verdict for the whole bank, once the handle that walked it is
            // gone. One line per close on an archive that has blobs, answering
            // "which clip did the guest never finish reading" — the question the
            // per-blob lines can only answer for the blobs that got a trace.
            static std::atomic<int> s_dump{0};
            if (!g_fsbWindows[pit->second].empty() &&
                s_dump.fetch_add(1, std::memory_order_relaxed) < 8) {
                // vfs_fsb_stats takes g_freadVolMtx, which is already held here.
                const FsbArchiveStats bs = fsb_archive_stats_locked(pit->second);
                ktraceLine("[KuDroidFmod] bank windows=%d complete=%d unread=%d "
                           "first_unread_off=%lld\n",
                           bs.windows, bs.complete, bs.unread, bs.first_unread_off);
            }
        }
        g_freadPaths.erase(stream);
    }
    return std::fclose(stream);
}

off_t vfs_ftello(FILE* stream) {
    if (!stream) {
        errno = EBADF;
        return -1;
    }
    const int sid = apk_stream_id(stream);
    if (sid >= 0) {
        return g_apkStreamPos[sid].load(std::memory_order_relaxed);
    }
    return ::ftello(stream);
}

long vfs_ftell(FILE* stream) {
    return static_cast<long>(vfs_ftello(stream));
}

int vfs_fseeko(FILE* stream, off_t offset, int whence) {
    if (!stream) {
        errno = EBADF;
        return -1;
    }
    const void* const caller = __builtin_return_address(0);
    const int sid = apk_stream_id(stream);
    const bool apk = (sid >= 0);
    const off_t before = vfs_ftello(stream);
    int rc = 0;

    if (apk) {
        std::lock_guard<std::mutex> slock(g_apkStreamMtx[sid]);
        if (!apk_stream_live(sid, stream)) {
            errno = EBADF;
            return -1;
        }
        off_t base = 0;
        if (whence == SEEK_SET) {
            base = 0;
        } else if (whence == SEEK_CUR) {
            // Relative to the stream's one cursor, not this thread's last guess:
            // the thread that gets the stream next must land where the thread that
            // had it left off.
            base = g_apkStreamPos[sid].load(std::memory_order_relaxed);
        } else if (whence == SEEK_END) {
            base = g_apkStreamSize[sid].load(std::memory_order_relaxed);
        } else {
            errno = EINVAL;
            return -1;
        }
        if ((offset > 0 && base > std::numeric_limits<off_t>::max() - offset)
                || (offset < 0 && base < std::numeric_limits<off_t>::min() - offset)) {
            errno = EOVERFLOW;
            return -1;
        }
        const off_t new_pos = base + offset;
        if (new_pos < 0) {
            errno = EINVAL;
            return -1;
        }
        g_apkStreamPos[sid].store(new_pos, std::memory_order_relaxed);
        g_apkStreamEof[sid].store(0, std::memory_order_relaxed);
        rc = 0;
    } else {
        rc = ::fseeko(stream, offset, whence);
    }

    if (rc == 0 && apk && kudroid::log::trace_hot()) {
        if (sid >= 0) {
            const uint64_t op = g_apkStreamOps[sid].fetch_add(1, std::memory_order_relaxed) + 1;
            if (op <= 16 &&
                g_apkStreamTraceNo[sid].load(std::memory_order_relaxed) < kOpTracedStreams) {
                ktraceLine(
                             "[KuDroidApkS] sid=%d op=%llu seek %lld whence=%d -> %lld\n",
                             sid, static_cast<unsigned long long>(op), static_cast<long long>(offset), whence,
                             static_cast<long long>(vfs_ftello(stream)));
            } else if (audio_trace_take(sid)) {
                ktraceLine(
                             "[KuDroidApkS] sid=%d AUDIO op=%llu seek %lld whence=%d -> %lld\n",
                             sid, static_cast<unsigned long long>(op), static_cast<long long>(offset), whence,
                             static_cast<long long>(vfs_ftello(stream)));
            }
        }
        static std::atomic<int> s_logged{0};
        if (s_logged.load() < 40) {
            ++s_logged;
            ktraceLine("[KuDroidApkF] fseek offset=%lld whence=%d\n", static_cast<long long>(offset),
                         whence);
        }
        {
            std::lock_guard<std::mutex> vlock(g_freadVolMtx);
            const auto pit = g_freadPaths.find(stream);
            if (pit != g_freadPaths.end()) {
                auto wit = g_fsbWindows.find(pit->second);
                if (wit != g_fsbWindows.end()) {
                    // A seek inside a blob is that blob's read; a seek into the gap
                    // between two packed blobs is the next blob's header. A "+64K
                    // from any blob" test also matched every seek in the gap and
                    // named the wrong clip.
                    const long lo = before < offset ? before : offset;
                    const long hi = before < offset ? offset : before;
                    FsbWindow* w = fsb_window_at(wit->second, lo, hi);
                    if (w == nullptr && lo != hi) w = fsb_window_at(wit->second, hi, lo + 1);
                    if (w != nullptr) {
                        if (fsb_window_log_seek(*w)) {
                            ktraceLine(
                                         "[KuDroidFmod] in-blob seek sid=%d %lld -> %lld whence=%d "
                                         "blob=[%ld+%llu]%s\n",
                                         sid, static_cast<long long>(before), static_cast<long long>(offset), whence, w->start,
                                         static_cast<unsigned long long>(w->expected),
                                         trace_caller_text(caller));
                        }
                    }
                }
            }
        }
        static std::atomic<int> s_endLogged{0};
        if (whence == SEEK_END && s_endLogged.load() < 16) {
            ++s_endLogged;
            ktraceLine("[KuDroidApkF] fseek END on apk stream -> %lld\n",
                         static_cast<long long>(vfs_ftello(stream)));
        }
    } else if (!apk) {
        if (audio_trace_take(sid)) {
            ktraceLine(
                         "[KuDroidApkS] sid=%d AUDIO seek %lld whence=%d from=%lld -> %lld rc=%d\n",
                         sid, static_cast<long long>(offset), whence, static_cast<long long>(before),
                         static_cast<long long>(vfs_ftello(stream)), rc);
        }
    }
    return rc;
}

int vfs_fseek(FILE* stream, long offset, int whence) {
    return vfs_fseeko(stream, offset, whence);
}

int vfs_feof(FILE* stream) {
    if (!stream) return 0;
    const int sid = apk_stream_id(stream);
    if (sid >= 0) {
        std::lock_guard<std::mutex> slock(g_apkStreamMtx[sid]);
        return g_apkStreamEof[sid].load(std::memory_order_relaxed) ? 1 : 0;
    }
    return std::feof(stream);
}

int vfs_ferror(FILE* stream) {
    if (!stream) return 0;
    const int sid = apk_stream_id(stream);
    if (sid >= 0) {
        return g_apkStreamErr[sid].load(std::memory_order_relaxed) ? 1 : 0;
    }
    return std::ferror(stream);
}

void vfs_clearerr(FILE* stream) {
    if (!stream) return;
    const int sid = apk_stream_id(stream);
    if (sid >= 0) {
        std::lock_guard<std::mutex> slock(g_apkStreamMtx[sid]);
        g_apkStreamEof[sid].store(0, std::memory_order_relaxed);
        g_apkStreamErr[sid].store(0, std::memory_order_relaxed);
        return;
    }
    std::clearerr(stream);
}

void vfs_rewind(FILE* stream) {
    if (!stream) return;
    const int sid = apk_stream_id(stream);
    if (sid >= 0) {
        vfs_fseeko(stream, 0, SEEK_SET);
        vfs_clearerr(stream);
        return;
    }
    std::rewind(stream);
}

