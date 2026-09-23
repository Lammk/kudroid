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

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

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

    std::filesystem::remove_all(tmp);

    if (g_failures == 0) {
        std::printf("=== audio-thread IO trace test PASSED ===\n");
        return 0;
    }
    std::printf("=== %d failure(s) ===\n", g_failures);
    return 1;
}
