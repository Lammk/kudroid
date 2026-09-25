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
pthread_barrier_t g_start;
FILE* g_sharedStream = nullptr;
std::atomic<int> g_recordsBad{0};

// One thread's share of the stream: plain sequential reads, no seeks, so every
// read depends entirely on the cursor the other thread last advanced. The
// argument names the slot whose records this thread appends to (null = slot 0).
void* SharedCursorThread(void* arg) {
    const int slot = arg == nullptr ? 0 : *static_cast<const int*>(arg);
    pthread_barrier_wait(&g_start);
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
    pthread_barrier_wait(&g_start);
    g_oneReadN = kudroid::vfs_fread(g_oneRead, 1, kChunk, g_sharedStream);
    return nullptr;
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
        pthread_barrier_init(&g_start, nullptr, 2);
        pthread_t one;
        pthread_create(&one, nullptr, OneReadThread, nullptr);
        pthread_barrier_wait(&g_start);
        pthread_join(one, nullptr);
        pthread_barrier_destroy(&g_start);
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
        pthread_barrier_init(&g_start, nullptr, 2);
        pthread_t third;
        pthread_create(&third, nullptr, OneReadThread, nullptr);
        pthread_barrier_wait(&g_start);
        pthread_join(third, nullptr);
        pthread_barrier_destroy(&g_start);
        Check(kudroid::vfs_ftello(g_sharedStream) == beforeThird + static_cast<off_t>(kChunk),
              "position tracks a read made by another thread after this one looked");

        // Concurrent implicit reads must tile the stream, never overlap it.
        g_records[0].clear();
        g_records[1].clear();
        g_recordsBad = 0;
        kudroid::vfs_fseeko(g_sharedStream, 0, SEEK_SET);
        pthread_barrier_init(&g_start, nullptr, 3);
        pthread_t ta, tb;
        const int secondSlot = 1;
        pthread_create(&ta, nullptr, SharedCursorThread, nullptr);
        pthread_create(&tb, nullptr, SharedCursorThread,
                       const_cast<int*>(&secondSlot));
        pthread_barrier_wait(&g_start);
        pthread_join(ta, nullptr);
        pthread_join(tb, nullptr);
        pthread_barrier_destroy(&g_start);
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

    std::filesystem::remove_all(tmp);

    if (g_failures == 0) {
        std::printf("=== audio-thread IO trace test PASSED ===\n");
        return 0;
    }
    std::printf("=== %d failure(s) ===\n", g_failures);
    return 1;
}
