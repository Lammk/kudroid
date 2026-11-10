#include "kudroid/BionicShim.h"
#include "kudroid/kudroid_bridge.h"
#include "kudroid/abi/SyscallShim.h"
#include "kudroid/abi/Sendfile.h"
#include "kudroid/abi/TimerDeadline.h"
#include "kudroid/abi/SocketAbi.h"
#include "kudroid/abi/GuestVarargs.h"
#include "kudroid/abi/BlockingWaitRegistry.h"
#include "kudroid/abi/GuestSignals.h"
#include "kudroid/debug/FrameWalk.h"
#include "kudroid/elf_loader.hpp"
#include "kudroid/DeviceProfile.h"
#include "kudroid/ExecMemory.h"
#include "kudroid/ElfAliasStore.h"
#include "kudroid/Log.h"
#include "kudroid/platform/BundledFramework.h"
#include "kudroid/platform/GraphicsShim.h"
#include "kudroid/platform/GuestClock.h"
#include "kudroid/platform/InputShim.h"
#include "kudroid/platform/AudioShim.h"
#include "kudroid/platform/MediaShim.h"
#include "kudroid/platform/CpuInfo.h"
#include "kudroid/platform/MemoryInfo.h"
#include "kudroid/VFSPathRemapper.h"
#include <filesystem>

#include <cmath>
#include <ctime>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sched.h>
#include <search.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <syslog.h>
#include "kudroid/platform/FramePacer.h"
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#if defined(__linux__)
#include <sys/inotify.h>
#include <sys/signalfd.h>
#endif
#include <unwind.h>
#include <cxxabi.h>
#include <chrono>
#include <algorithm>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
#if defined(__APPLE__)
#include <resolv.h>
#endif
#include <condition_variable>
#include <limits.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/vm_region.h>
#include <malloc/malloc.h>
#include <libkern/OSCacheControl.h>
#include "kudroid/MachVmDecls.h"
#else
#include <malloc.h>
#endif
#if defined(__APPLE__)
// Direct declaration of libobjc autorelease pool functions to avoid header search path
// issues on certain macOS SDK runners while remaining ABI-compatible.
extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void* pool);
#endif

extern "C" void __gxx_personality_v0();

// Mirror log lines to crash buffer in kudroid_bridge.cpp for pre-crash diagnostics.
extern "C" void kudroid_append_crash_log(const char* text, size_t len);

// Guest symbol lookup hook: queries loaded .so modules via LibraryManager.
// Allows dummy handles (e.g. dlopen('libc.so')) to resolve exported symbols.
extern "C" {
void* (*kudroid_guest_symbol_lookup)(const char* name) = nullptr;

// Per-library handles for guest .so files LibraryManager already mapped.
//
// dlopen of a guest library used to fall through to DUMMY_HANDLE, which makes
// dlsym search every loaded library at once. That is wrong in the way that is
// hardest to see: two guest libraries exporting the same name resolve to whichever
// sorts first, so a caller that deliberately opened one gets the other's function.
// AGDK's GameActivity is the caller that matters — it dlopens the .so named by
// android.app.lib_name and then dlsyms its entry points out of that handle
// specifically.
//
// `open` returns an opaque handle or null when the name is not a guest library;
// `symbol` validates the handle against its own registry, so it is safe to call
// with any pointer and must be consulted BEFORE ::dlsym, which would dereference a
// handle it does not own.
void* (*kudroid_guest_library_open)(const char* filename) = nullptr;
void* (*kudroid_guest_library_symbol)(void* handle, const char* symbol) = nullptr;

// True when `handle` came from kudroid_guest_library_open. Needed because
// ::dlclose on it would hand the host loader a pointer it never issued.
int (*kudroid_guest_library_owns)(void* handle) = nullptr;
}

// Store guest abort message (android_set_abort_message) for crash reporting.
extern "C" void kudroid_store_abort_message(const char* msg);

// JNI & JVM Runtime declarations
extern "C" void* kudroid_jni_get_javavm(void);
extern "C" int JNI_GetCreatedJavaVMs(void** vmBuf, size_t bufLen, size_t* nVMs);
extern "C" int JNI_CreateJavaVM(void** p_vm, void** p_env, void* vm_args);

// Orientation, Haptic & Sensor Bridge declarations
extern "C" void kudroid_vibrate(int intensity);
extern "C" void kudroid_set_requested_orientation(int orientation);
extern "C" int kudroid_get_requested_orientation(void);
extern "C" void kudroid_inject_sensor_event(int sensorType, float x, float y, float z);

extern "C" int kudroid_check_permission(const char* packageName, const char* permissionName);
extern "C" void kudroid_set_group_permission(const char* packageName, const char* groupKey, int granted);
extern "C" int kudroid_is_group_granted(const char* packageName, const char* groupKey);
extern "C" void kudroid_grant_all_permissions(const char* packageName);
extern "C" const char* kudroid_get_app_permissions_json(const char* packageName);
extern "C" void kudroid_set_app_permissions_json(const char* packageName, const char* jsonStr);

// For Bionic pthread emulation
#include <cstdarg>
#include <semaphore.h>
#ifdef __APPLE__
#include <sys/event.h>
#include <dispatch/dispatch.h>
#else
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#endif

#ifndef EPOLLIN
#define EPOLLIN 0x001
#define EPOLLPRI 0x002
#define EPOLLOUT 0x004
#define EPOLLERR 0x008
#define EPOLLHUP 0x010
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3
#endif

// POLLERR/POLLHUP/POLLNVAL are bionic-specific names; define them if the host
// poll.h does not (e.g. glibc uses POLL_ERR/POLL_HUP).
#ifndef POLLERR
#define POLLERR 0x001
#endif
#ifndef POLLHUP
#define POLLHUP 0x002
#endif
#ifndef POLLNVAL
#define POLLNVAL 0x020
#endif

struct android_epoll_event {
    uint32_t events;
    uint64_t data;
};
static_assert(sizeof(android_epoll_event) == 16, "android_epoll_event size must match LP64");
static_assert(offsetof(android_epoll_event, data) == 8, "android_epoll_event data offset must match LP64");
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <pthread.h>
#include <ifaddrs.h>
#if !defined(__APPLE__)
#include <sys/syscall.h>
#endif
#include <unordered_map>
#include <map>
#include <set>
#include <mutex>
#include <shared_mutex>
#include <optional>
#include <atomic>
#include <vector>
#include <string>
#include <array>
#include <memory>
#include <new>
#include <dlfcn.h>
#include <thread>

extern const char* g_kudroid_log_dir_ptr;

#include "syscall/Logging.inc"
#include "syscall/Pthread.inc"
#include "syscall/FileMetadata.inc"
#include "syscall/Memory.inc"
#include "syscall/SignalsAndGuards.inc"
#include "syscall/DirectSyscalls.inc"
#include "syscall/DescriptorIO.inc"
#include "syscall/Sockets.inc"
#include "syscall/NetworkAndSignals.inc"
#include "syscall/Libc.inc"
#include "syscall/DescriptorsAndLoader.inc"
#include "syscall/EventPoll.inc"
#include "syscall/ThreadLocalStorage.inc"
#include "syscall/PropertiesAndStdio.inc"
#include "syscall/LinuxExtensions.inc"
#include "syscall/LooperAndSemaphore.inc"
#include "syscall/FortifyAndAllocator.inc"
#include "syscall/Symbols.inc"
