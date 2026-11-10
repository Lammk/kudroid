#include "kudroid/VFSPathRemapper.h"
#include "kudroid/cacert_data.h"
#include "kudroid/DeviceProfile.h"
#include "kudroid/Log.h"
#include "kudroid/platform/AssetShim.h"
#include "kudroid/platform/CpuInfo.h"
#include "kudroid/platform/MemoryInfo.h"

#include <cerrno>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <memory>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sstream>
#include <unistd.h>
#include <vector>
#include <zlib.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "kudroid/kudroid_bridge.h"

// Defined in SyscallShim.cpp. Declared here rather than through a header because the
// remapper otherwise has no reason to depend on the syscall layer; it needs this only to
// record what device figures the pseudo-files were written from.
extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);
extern "C" const char* kudroid_trace_stamp(void);

// One diagnostic line: boot-relative time and the guest thread name in front of the
// caller's text, so the VFS/audio stream can be placed between the [KuDroidBoot] phase
// marks and beside the Unity side of the log. A single fprintf keeps the line whole —
// two writes would let another thread interleave into the middle of it. The formats
// already end in '\n'.
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
static void ktraceLine(const char* fmt, ...) {
    const int savedErrno = errno;
    char body[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "%s%s", kudroid_trace_stamp(), body);
    errno = savedErrno;
}

#define KTRACE_HOT(...) do { if (::kudroid::log::trace_hot()) ktraceLine(__VA_ARGS__); } while (0)

// Declared in elf_loader.cpp; spelled C so the name cannot be captured by a namespace.
extern "C" bool kudroid_lookup_guest_module(void* addr, char* out, std::size_t outSize);

// " from=libunity.so+0x1a2b40" for the guest frame that called into the file layer.
//
// The guest's fopen/fread/open/lseek are bound straight to the functions below (see the
// symbol table in SyscallShim), so __builtin_return_address(0) *there* is the guest's own
// return address — the code that asked for the file. Without it a file trace says what was
// read but never who read it, and for audio that is the whole question: Unity's resource
// reader and FMOD's own loader look byte-for-byte identical in the trace. The loader
// already knows the module ranges, so this is a lookup, not a per-app guess.
static const char* trace_caller_text(const void* ra) {
    if (ra == nullptr) return "";
    static thread_local char text[192];
    char full[384];
    full[0] = '\0';
    if (kudroid_lookup_guest_module(const_cast<void*>(ra), full, sizeof(full))) {
        const char* slash = std::strrchr(full, '/');
        std::snprintf(text, sizeof(text), " from=%s", slash != nullptr ? slash + 1 : full);
    } else {
        std::snprintf(text, sizeof(text), " from=host:%p", ra);
    }
    return text;
}

#include "vfs/RootAndIdentity.inc"
#include "vfs/PseudoFiles.inc"
#include "vfs/ArchiveIndex.inc"
#include "vfs/PathMapping.inc"
#include "vfs/StreamTracking.inc"
#include "vfs/StreamIO.inc"
#include "vfs/FileOperations.inc"
