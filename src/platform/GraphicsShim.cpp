#include "kudroid/platform/GraphicsShim.h"
#include "kudroid/platform/EglCapabilities.h"
#include "kudroid/platform/GlesBackendProbe.h"
#include "kudroid/platform/Es31Compat.h"
#include "kudroid/VFSPathRemapper.h"
#include "kudroid/kudroid_bridge.h"
#include "kudroid/platform/BundledFramework.h"
#include "kudroid/platform/FramePacer.h"
#include "kudroid/platform/GuestClock.h"
#include "kudroid/platform/ThermalShim.h"
#include "kudroid/platform/SurfaceTextureHost.h"
#include "kudroid/Log.h"
#include <jni.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

#if defined(__APPLE__)
#include <pthread.h>
#include <dispatch/dispatch.h>
#include <objc/runtime.h>
#include <objc/message.h>
extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void* pool);
// Thread-local autorelease pool for ANGLE Metal.
// ANGLE internally creates @autoreleased ObjC objects (MTLCommandBuffer,
// MTLRenderPipelineState, MTLLibrary, etc.) during GL calls such as
// glCreateShader, glCompileShader, glDrawArrays, etc. Without a thread-local
// autorelease pool, these objects have no pool to drain into, causing SIGABRT.
// We push a pool when eglMakeCurrent binds a context to the thread, and
// drain+re-push on every eglSwapBuffers to keep memory bounded.
static thread_local void* tls_autorelease_pool = nullptr;
#endif

// This comes from kudroid_bridge.cpp
extern void* g_metalLayer;
extern int g_metalLayerWidth;
extern int g_metalLayerHeight;
extern float g_metalLayerDensity;

// Frame-liveness hook (defined in kudroid_bridge.cpp): stamps the last
// successful present so the watchdog can tell a dead frame loop from a busy
// one even when no tracked I/O is flowing.
extern "C" void kudroid_note_frame_presented(void);

// Log directory owned by the bridge; weak so the shim also links on its own.
extern const char* g_kudroid_log_dir_ptr __attribute__((weak));

#include "graphics/NativeWindows.inc"
#include "graphics/ProcedureLookup.inc"
#include "graphics/Es31Shim.inc"
#include "graphics/EglDisplay.inc"
#include "graphics/Vulkan.inc"
#include "graphics/EglSurface.inc"
#include "graphics/GlesForwarding.inc"
#include "graphics/GlesStackAbi.inc"
#include "graphics/Symbols.inc"
