// Host-side test for the audio-thread IO trace.
//
// FMOD reads its banks from a thread it names itself ("FMOD nonblocking thread
// (0)"), and by the time audio loads the per-instance op gate on the archive
// stream has long been spent on preload traffic — so a failing createSound used
// to produce no IO trace of its own, at either layer (FILE* in
// VFSPathRemapper.cpp, fd in SyscallShim.cpp).
//
// The trace is keyed on the thread name, so this test pins exactly that: an
// audio-named thread is traced in both layers, every other thread is not, and
// the kernel-visible name is what the predicate reads. Two earlier attempts died
// on details this test exists to catch — the name must be set before the first
// IO (the predicate caches per thread) and glibc rejects names longer than 15
// bytes, which would leave the thread unnamed and silently untraced.

#include "kudroid/VFSPathRemapper.h"

#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::printf(condition ? "  OK   %s\n" : "  FAIL %s\n", what.c_str());
    if (!condition) ++g_failures;
}

// The FD-layer predicate lives in SyscallShim.cpp; the file layer exports its
// answer so both layers agree on what an audio thread is.
extern "C" bool kudroid_audio_thread(void);
extern "C" ssize_t bionic_read(int fd, void* buf, size_t count);

std::string g_capture;
int g_savedStderr = -1;

void CaptureStart() {
    std::fflush(stderr);
    g_savedStderr = ::dup(2);
    const int fd = ::open(g_capture.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ::dup2(fd, 2);
    ::close(fd);
}

std::string CaptureStop() {
    std::fflush(stderr);
    ::dup2(g_savedStderr, 2);
    ::close(g_savedStderr);
    g_savedStderr = -1;
    std::ifstream in(g_capture);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The file layer's trace goes through ktraceLine, which stamps every line with
// "[t=<ms> th=<thread name>] ", so a captured line can be attributed to its
// writer — that stamp is what proves which thread the ops came from.
bool HasLineWith(const std::string& text, const std::string& needle,
                 const std::string& threadName) {
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        const size_t lineStart = text.rfind('\n', pos);
        const size_t from = lineStart == std::string::npos ? 0 : lineStart + 1;
        const size_t lineEnd = text.find('\n', pos);
        const std::string line = text.substr(from, lineEnd - from);
        if (line.find(threadName) != std::string::npos) return true;
        pos += needle.size();
    }
    return false;
}

int SetThreadName(const char* name) {
#if defined(__APPLE__)
    return pthread_setname_np(name);
#else
    return pthread_setname_np(pthread_self(), name);
#endif
}

// Guest path for the VFS layer and the host path it resolves to for the fd layer.
const char* kGuestPath = "/data/data/com.kudroid.test/files/audio_probe.bin";
std::string g_hostPath;

// Both IO layers on the calling thread: the FILE* path and the fd path.
void DoIo() {
    FILE* f = kudroid::vfs_fopen(kGuestPath, "rb");
    if (f != nullptr) {
        char buf[64];
        kudroid::vfs_fread(buf, 1, sizeof(buf), f);
        kudroid::vfs_fseek(f, 0, SEEK_SET);
        kudroid::vfs_fread(buf, 1, 16, f);
        kudroid::vfs_fclose(f);
    }
    const int fd = ::open(g_hostPath.c_str(), O_RDONLY);
    if (fd >= 0) {
        char buf[64];
        bionic_read(fd, buf, sizeof(buf));
        ::close(fd);
    }
}

void* AudioThreadMain(void*) {
    SetThreadName("FMOD-io");
    DoIo();
    return nullptr;
}

void* PlainThreadMain(void*) {
    DoIo();
    return nullptr;
}

// --- One FILE*, one cursor ------------------------------------------------
//
// Unity hands FMOD the loader's own FILE* for an FSB slice and both threads
// read it, so the archive stream's cursor is shared state: whoever reads next
// continues where the previous read stopped. A per-thread cursor satisfies each
// thread locally and still hands FMOD bytes from the wrong offset, which is how
// a full-length read turns into "Error loading file". These checks pin the two
// properties that fix has to keep: the position is visible across threads, and
// two threads reading at once consume the file once, not twice.

const char* kApkGuestPath = "/data/app/com.kudroid.test/base.apk";
constexpr size_t kApkSize = 4096;
constexpr size_t kChunk = 64;

// File content: an LCG's high bits, so no 64-byte window repeats anywhere in
// the file. That is what makes the offset a chunk was served from recoverable
// from its bytes -- a per-thread cursor re-reads the same window, and a shared
// one never does.
std::vector<unsigned char> g_apkBytes;

void BuildApkBytes() {
    g_apkBytes.assign(kApkSize, 0);
    uint32_t s = 0x12345678u;
    for (size_t i = 0; i < kApkSize; ++i) {
        s = s * 1103515245u + 12345u;
        g_apkBytes[i] = static_cast<unsigned char>((s >> 16) & 0xFF);
    }
}

unsigned char ApkByte(size_t i) { return i < g_apkBytes.size() ? g_apkBytes[i] : 0; }

// How many 64-byte windows in the file are unique; 1 means an offset is
// recoverable from the bytes alone.
size_t UniqueWindows() {
    std::vector<std::string> seen;
    size_t dup = 0;
    for (size_t off = 0; off + kChunk <= kApkSize; ++off) {
        const std::string w(reinterpret_cast<const char*>(&g_apkBytes[off]), kChunk);
        if (std::find(seen.begin(), seen.end(), w) != seen.end()) ++dup;
        else seen.push_back(w);
    }
    return (kApkSize - kChunk + 1) - dup;
}

struct ReadRecord {
    unsigned char data[kChunk] = {};
    size_t n = 0;
};

std::vector<ReadRecord> g_records[2];
FILE* g_sharedStream = nullptr;
std::atomic<int> g_recordsBad{0};

// Start gate for the reader threads.
//
// pthread barriers are an optional part of POSIX and macOS does not implement
// them -- there is no pthread_barrier_t in its libc, which is where the macOS
// arm64 build of this test failed. A participant count plus a flag is the
// portable equivalent, and the counter is what makes the opener wait for every
// thread instead of releasing the stragglers early.
std::atomic<int> g_gateArrived{0};
std::atomic<bool> g_gateOpen{false};

void GateArm() {
    g_gateArrived.store(0, std::memory_order_relaxed);
    g_gateOpen.store(false, std::memory_order_relaxed);
}

void GateWait() {
    g_gateArrived.fetch_add(1, std::memory_order_acq_rel);
    while (!g_gateOpen.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

// The opener's side of the barrier: arrive, wait for everyone else, then release.
// It must not call GateWait() -- only this side sets the flag, so a participant
// that waits for it deadlocks against itself.
void GateRelease(int participants) {
    g_gateArrived.fetch_add(1, std::memory_order_acq_rel);
    while (g_gateArrived.load(std::memory_order_acquire) < participants) {
        std::this_thread::yield();
    }
    g_gateOpen.store(true, std::memory_order_release);
}

// One thread's share of the stream: plain sequential reads, no seeks, so every
// read depends entirely on the cursor the other thread last advanced. The
// argument names the slot whose records this thread appends to (null = slot 0).
void* SharedCursorThread(void* arg) {
    const int slot = arg == nullptr ? 0 : *static_cast<const int*>(arg);
    GateWait();
    for (int i = 0; i < 8; ++i) {
        ReadRecord rec;
        rec.n = kudroid::vfs_fread(rec.data, 1, kChunk, g_sharedStream);
        if (rec.n != kChunk) ++g_recordsBad;
        g_records[slot].push_back(rec);
    }
    return nullptr;
}

// The offset a chunk was read from cannot be sampled with a separate ftello:
// between that call and the read the other thread is free to advance the
// cursor. Recover it from the bytes instead -- with unique windows, exactly one
// offset matches, and that is the offset the stream actually served.
long MatchedOffset(const ReadRecord& rec) {
    long found = -1;
    for (size_t off = 0; off + kChunk <= kApkSize; ++off) {
        size_t b = 0;
        while (b < kChunk && rec.data[b] == g_apkBytes[off + b]) ++b;
        if (b != kChunk) continue;
        if (found >= 0) return -1;  // ambiguous: cannot attribute this chunk
        found = static_cast<long>(off);
    }
    return found;
}

unsigned char g_oneRead[kChunk] = {};
size_t g_oneReadN = 0;

void* OneReadThread(void*) {
    GateWait();
    g_oneReadN = kudroid::vfs_fread(g_oneRead, 1, kChunk, g_sharedStream);
    return nullptr;
}

// --- FSB blob coverage windows ---------------------------------------------
//
// The coverage windows answer "did the engine read the clip it was handed", so
// they have to survive their own creation. They did not: the serve path held a
// reference into the window map, then called the reporter, which drops the map
// entry when the list empties -- and the first header read of every archive
// empties it. The push after that wrote into freed heap and left the archive
// with no window at all, so no in-blob read and no COMPLETE/SHORT verdict could
// ever be logged. Detection had the matching gap: a read beginning with 'F' was
// the only shape recognised, while Unity streams clips in reads far larger than
// one blob, so a clip starting inside a read was never seen at all.

const char* kFsbApkGuestPath = "/data/app/com.kudroid.test/fsbprobe.apk";
constexpr size_t kFsbBlobSize = 60 + 32 + 0 + 4096;  // header + sample headers + data
constexpr size_t kFsbBlobs = 3;
constexpr size_t kFsbPrefix = 700;                    // filler before the first blob

// FSB5 v1: magic, version, numSamples, sampleHeadersSize, nameTableSize,
// dataSize, mode. blobTotal = 60 + shs + nts + dataSize, which is what the
// probe and the window both derive.
void PutFsbHeader(unsigned char* p, uint32_t shs, uint32_t dataSize) {
    std::memset(p, 0, 60);
    std::memcpy(p, "FSB5", 4);
    auto put32 = [&](size_t o, uint32_t v) {
        p[o] = static_cast<unsigned char>(v);
        p[o + 1] = static_cast<unsigned char>(v >> 8);
        p[o + 2] = static_cast<unsigned char>(v >> 16);
        p[o + 3] = static_cast<unsigned char>(v >> 24);
    };
    put32(4, 1);        // version
    put32(8, 1);        // numSamples
    put32(12, shs);     // sampleHeadersSize
    put32(16, 0);       // nameTableSize
    put32(20, dataSize);
    put32(24, 15);      // mode: Vorbis
}

}  // namespace

int main() {
    std::printf("=== audio-thread IO trace test ===\n");

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() /
                                      ("kudroid_audio_trace_" + std::to_string(::getpid()));
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    g_capture = (tmp / "captured.log").string();
    kudroid::VFSPathRemapper::getInstance().setDocumentsDirectory((tmp / "Documents").string());
    g_hostPath = kudroid::VFSPathRemapper::getInstance().remap(kGuestPath);
    Check(!g_hostPath.empty(), "probe path resolves inside the VFS");

    {
        FILE* f = kudroid::vfs_fopen(kGuestPath, "w");
        Check(f != nullptr, "probe file created");
        if (f != nullptr) {
            std::fputs("0123456789abcdefghijklmnopqrstuvwxyz", f);
            kudroid::vfs_fclose(f);
        }
    }

    // The test binary's own thread is not an audio thread.
    Check(!kudroid_audio_thread(), "main thread is not an audio thread");

    // Non-audio thread: identical IO, no AUDIO lines.
    {
        CaptureStart();
        pthread_t t;
        pthread_create(&t, nullptr, PlainThreadMain, nullptr);
        pthread_join(t, nullptr);
        const std::string out = CaptureStop();
        Check(out.find("AUDIO ") == std::string::npos,
              "non-audio thread produces no AUDIO lines");
    }

    // Audio-named thread: every op traced, and only from that thread.
    {
        CaptureStart();
        pthread_t t;
        pthread_create(&t, nullptr, AudioThreadMain, nullptr);
        pthread_join(t, nullptr);
        const std::string out = CaptureStop();
        const std::string who = "th=FMOD-io]";
        Check(HasLineWith(out, "AUDIO fopen(", who), "audio open traced");
        Check(HasLineWith(out, "AUDIO fread", who), "audio read (FILE*) traced");
        Check(HasLineWith(out, "AUDIO seek", who), "audio seek (FILE*) traced");
        Check(HasLineWith(out, "[KuDroidFd] AUDIO read", who), "audio read (fd) traced");
    }

    // Nothing outside the audio thread was tagged.
    {
        CaptureStart();
        pthread_t t;
        pthread_create(&t, nullptr, PlainThreadMain, nullptr);
        pthread_join(t, nullptr);
        const std::string out = CaptureStop();
        Check(out.find("th=FMOD-io]") == std::string::npos,
              "no line is attributed to the audio thread when it is idle");
    }

    // A tracked archive stream, as the audio path opens it.
    BuildApkBytes();
    Check(UniqueWindows() == kApkSize - kChunk + 1,
          "every 64-byte window of the probe file is unique, so a served chunk names its offset");
    const std::string apkHost = kudroid::VFSPathRemapper::getInstance().remap(kApkGuestPath);
    Check(!apkHost.empty(), "apk guest path resolves inside the VFS");
    {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(apkHost).parent_path(), ec);
        FILE* w = std::fopen(apkHost.c_str(), "wb");
        Check(w != nullptr, "apk probe file created");
        if (w != nullptr) {
            std::fwrite(g_apkBytes.data(), 1, g_apkBytes.size(), w);
            std::fclose(w);
        }
    }

    // A read on one thread must be visible as the stream's position to the next
    // thread, and a relative seek there must build on it.
    g_sharedStream = kudroid::vfs_fopen(kApkGuestPath, "rb");
    Check(g_sharedStream != nullptr, "apk stream opened for the shared-cursor checks");
    if (g_sharedStream != nullptr) {
        GateArm();
        pthread_t one;
        pthread_create(&one, nullptr, OneReadThread, nullptr);
        GateRelease(2);
        pthread_join(one, nullptr);
        Check(g_oneReadN == kChunk, "other thread's read returned a full chunk");
        Check(kudroid::vfs_ftello(g_sharedStream) == static_cast<off_t>(kChunk),
              "another thread sees the stream position the read left");
        bool contentOk = true;
        for (size_t b = 0; b < g_oneReadN; ++b) {
            if (g_oneRead[b] != ApkByte(b)) contentOk = false;
        }
        Check(contentOk, "bytes read by the other thread match the file");
        Check(kudroid::vfs_fseeko(g_sharedStream, 16, SEEK_CUR) == 0,
              "relative seek accepted on the shared cursor");
        Check(kudroid::vfs_ftello(g_sharedStream) == static_cast<off_t>(kChunk + 16),
              "relative seek continues from the shared cursor");
        unsigned char tail[kChunk] = {};
        Check(kudroid::vfs_fread(tail, 1, kChunk, g_sharedStream) == kChunk,
              "read after the relative seek returns a full chunk");
        bool tailOk = true;
        for (size_t b = 0; b < kChunk; ++b) {
            if (tail[b] != ApkByte(kChunk + 16 + b)) tailOk = false;
        }
        Check(tailOk, "bytes after the relative seek match the file");

        // A thread that already looked at the position must keep seeing it move.
        // This is the failure the fix exists for: a cursor cached per thread goes
        // stale the moment another thread touches the stream, and the thread that
        // picks the slice up next resumes from an offset nobody is at.
        const off_t beforeThird = kudroid::vfs_ftello(g_sharedStream);
        GateArm();
        pthread_t third;
        pthread_create(&third, nullptr, OneReadThread, nullptr);
        GateRelease(2);
        pthread_join(third, nullptr);
        Check(kudroid::vfs_ftello(g_sharedStream) == beforeThird + static_cast<off_t>(kChunk),
              "position tracks a read made by another thread after this one looked");

        // Concurrent implicit reads must tile the stream, never overlap it.
        g_records[0].clear();
        g_records[1].clear();
        g_recordsBad = 0;
        kudroid::vfs_fseeko(g_sharedStream, 0, SEEK_SET);
        GateArm();
        pthread_t ta, tb;
        const int secondSlot = 1;
        pthread_create(&ta, nullptr, SharedCursorThread, nullptr);
        pthread_create(&tb, nullptr, SharedCursorThread,
                       const_cast<int*>(&secondSlot));
        GateRelease(3);
        pthread_join(ta, nullptr);
        pthread_join(tb, nullptr);
        Check(g_recordsBad == 0, "concurrent reads each returned a full chunk");
        std::vector<long> offsets;
        for (int slot = 0; slot < 2; ++slot) {
            for (const auto& rec : g_records[slot]) offsets.push_back(MatchedOffset(rec));
        }
        std::sort(offsets.begin(), offsets.end());
        bool tiled = offsets.size() == 16;
        for (size_t i = 0; i < offsets.size() && tiled; ++i) {
            if (offsets[i] != static_cast<long>(i * kChunk)) tiled = false;
        }
        Check(tiled, "two threads consumed the stream once, in order, without overlap");
        Check(kudroid::vfs_ftello(g_sharedStream) == static_cast<off_t>(16 * kChunk),
              "stream position accounts for every byte both threads read");
        kudroid::vfs_fclose(g_sharedStream);
        g_sharedStream = nullptr;
    }

    // FSB coverage windows: a blob chain that does not start at the beginning of
    // a read must still be tracked, covered and verdicted.
    {
        const std::string fsbHost = kudroid::VFSPathRemapper::getInstance().remap(kFsbApkGuestPath);
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(fsbHost).parent_path(), ec);
        std::vector<unsigned char> image(kFsbPrefix + kFsbBlobs * kFsbBlobSize, 0xAB);
        for (size_t i = 0; i < kFsbBlobs; ++i) {
            PutFsbHeader(&image[kFsbPrefix + i * kFsbBlobSize], 32, 4096);
        }
        FILE* w = std::fopen(fsbHost.c_str(), "wb");
        Check(w != nullptr, "fsb probe file created");
        if (w != nullptr) {
            std::fwrite(image.data(), 1, image.size(), w);
            std::fclose(w);
        }

        // One read covering the whole chain, the way Unity streams a .resource:
        // the first blob starts mid-buffer and the other two follow it. A second
        // pass then re-reads it, which is what exercises the accounting path --
        // the windows are created after the first read's own accounting has
        // already run, so only a later read can be charged to them.
        CaptureStart();
        FILE* f = kudroid::vfs_fopen(kFsbApkGuestPath, "rb");
        Check(f != nullptr, "fsb probe stream opened");
        if (f != nullptr) {
            std::vector<unsigned char> got(image.size());
            Check(kudroid::vfs_fread(got.data(), 1, got.size(), f) == got.size(),
                  "whole-chain read returned every byte");
            Check(kudroid::vfs_fseeko(f, 0, SEEK_SET) == 0, "re-read seeks to the chain start");
            Check(kudroid::vfs_fread(got.data(), 1, got.size(), f) == got.size(),
                  "second whole-chain read returned every byte");
            kudroid::vfs_fclose(f);
        }
        const std::string out = CaptureStop();
        Check(out.find("served FSB5") != std::string::npos,
              "a blob starting inside a read is recognised");
        Check(out.find("in-blob read") != std::string::npos,
              "reads inside a tracked blob are accounted");
        // Every blob start in the chain must get a verdict. Compared as a set of
        // offsets, not a line count: a re-read legitimately re-covers a blob, and
        // the point is coverage, not how often it is said.
        std::vector<long> verdicts;
        for (size_t at = out.find("blob off="); at != std::string::npos;) {
            verdicts.push_back(std::strtol(out.c_str() + at + 9, nullptr, 10));
            at = out.find("blob off=", at + 9);
        }
        std::sort(verdicts.begin(), verdicts.end());
        verdicts.erase(std::unique(verdicts.begin(), verdicts.end()), verdicts.end());
        bool allVerdicted = true;
        for (size_t i = 0; i < kFsbBlobs; ++i) {
            const long want = static_cast<long>(kFsbPrefix + i * kFsbBlobSize);
            if (!std::binary_search(verdicts.begin(), verdicts.end(), want)) allVerdicted = false;
        }
        Check(allVerdicted,
              "every blob in the chain is tracked and verdicted COMPLETE");

        // ...and exactly once each. A guest that cannot play a clip re-reads the
        // same blob for as long as it retries; announcing it again on every pass
        // turns that retry loop into an unbounded log loop on the guest's
        // synchronous read path (observed: one 576-byte blob announced 262 times
        // in a single run, two unbuffered stderr lines each).
        std::size_t announced = 0;
        for (size_t at = out.find("COMPLETE"); at != std::string::npos;
             at = out.find("COMPLETE", at + 1)) {
            // ktraceLine stamps every line with "[t=<ms> th=<name>] ", so the
            // verdict is found by substring, not by line offset.
            const size_t lineStart = out.rfind('\n', at);
            const std::string line =
                out.substr(lineStart == std::string::npos ? 0 : lineStart + 1,
                           (lineStart == std::string::npos ? at : at - lineStart - 1));
            if (line.find("blob off=") != std::string::npos) ++announced;
        }
        Check(announced == verdicts.size(),
              "a re-read of the same blob does not announce it again (" +
                  std::to_string(announced) + " lines for " +
                  std::to_string(verdicts.size()) + " blobs)");
    }

    std::filesystem::remove_all(tmp);

    if (g_failures == 0) {
        std::printf("=== audio-thread IO trace test PASSED ===\n");
        return 0;
    }
    std::printf("=== %d failure(s) ===\n", g_failures);
    return 1;
}
