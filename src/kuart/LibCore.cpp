#include "kudroid/platform/VectorRasterizer.h"
#include "kudroid/platform/PixelColorFilter.h"
#include <fstream>
#include "kudroid/kuart/LibCore.h"
#include "kudroid/platform/FileObserverHost.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#if defined(KUDROID_HAVE_SQLITE3)
#include <sqlite3.h>
#endif
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <mutex>
#include <new>
#include <random>
#include <set>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <memory>
#include <pthread.h>
#endif

#include "kudroid/kudroid_bridge.h"
#include "kudroid/VFSPathRemapper.h"
#include "kudroid/kuart/DexClass.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexHeap.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/kuart/VmLock.h"
#include "kudroid/platform/ThermalShim.h"
#include "kudroid/platform/MemoryInfo.h"
#include "kudroid/platform/AssetShim.h"
#include "kudroid/ResourceTable.h"
#include "kudroid/platform/CpuInfo.h"
#include "kudroid/platform/AudioShim.h"
#include "kudroid/platform/AudioCapture.h"
#include "kudroid/platform/CameraShim.h"
#include "kudroid/platform/MediaPlayerHost.h"
#include "kudroid/platform/SurfaceTextureHost.h"
#include "kudroid/platform/SensorHost.h"
#include "kudroid/platform/NetworkState.h"
#include "kudroid/platform/FramePacer.h"
#include "kudroid/platform/GuestClock.h"
#include "kudroid/platform/JavaCanvasRenderer.h"
#include "kudroid/platform/GraphicsShim.h"
#include "kudroid/KuArtRuntime.h"

// Native side of MessageQueue's AOSP-style wake primitive and Looper-touch
// drain (implemented in platform/InputShim.cpp and KuArtRuntime.cpp).
extern "C" void kudroid_console_init_nonblocking(void);
extern "C" int64_t kudroid_looper_init(void);
extern "C" void kudroid_looper_poll(int64_t ptr, int64_t timeout_millis);
extern "C" void kudroid_looper_wake(int64_t ptr);
extern "C" void kudroid_looper_set_main(int64_t ptr);
extern "C" int kudroid_looper_is_main(int64_t ptr);
extern "C" void kudroid_looper_destroy(int64_t ptr);
extern "C" float g_metalLayerDensity;

namespace kudroid {
namespace kuart {

#include "libcore/StringsAndObjects.inc"
#include "libcore/Reflection.inc"
#include "libcore/JavaLang.inc"
#include "libcore/FileAndSql.inc"
#include "libcore/AndroidGraphics.inc"
#include "libcore/AndroidServices.inc"
#include "libcore/MemoryAndResources.inc"

size_t LibCoreDiscardFrameCallbacks(Interpreter* interp) {
    if (interp == nullptr) return 0;
    VmLockGuard guard;
    std::lock_guard<std::mutex> lock(g_java_frame_mutex);
    size_t removed = 0;
    for (size_t i = g_java_frame_contexts.size(); i-- > 0;) {
        JavaFrameCallback* ctx = g_java_frame_contexts[i];
        if (ctx->interp != interp) continue;
        g_java_frame_contexts.erase(g_java_frame_contexts.begin() + static_cast<long>(i));
        if (interp->jni_env() != nullptr) {
            interp->jni_env()->DeleteGlobalRef(reinterpret_cast<jobject>(ctx->callback));
            interp->jni_env()->DeleteGlobalRef(reinterpret_cast<jobject>(ctx->choreographer));
        }
        delete ctx;
        ++removed;
    }
    return removed;
}

bool LibCoreInvoke(Interpreter* interp, const DexMethod* method, const DexValue* args,
                   size_t num_args, DexValue* result) {
    if (method == nullptr || method->declaring_class == nullptr) return false;
    const char* desc = method->declaring_class->descriptor;
    const char* name = method->name;

    if (std::strcmp(desc, "Ljava/lang/Object;") == 0) return Invoke_java_lang_Object(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/Class;") == 0) return Invoke_java_lang_Class(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/System;") == 0) return Invoke_java_lang_System(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Landroid/os/SystemClock;") == 0) {
        if (std::strcmp(name, "uptimeMillis") == 0) {
            *result = DexValue::Long(guest_monotonic_ns() / 1000000ll);
            return true;
        }
        if (std::strcmp(name, "currentThreadTimeMillis") == 0) {
            *result = DexValue::Long(guest_clock_ns(3) / 1000000ll);
            return true;
        }
        if (std::strcmp(name, "elapsedRealtime") == 0 ||
            std::strcmp(name, "elapsedRealtimeNanos") == 0) {
            const int64_t ns = guest_clock_ns(7);
            *result = DexValue::Long(std::strcmp(name, "elapsedRealtime") == 0
                                        ? ns / 1000000ll : ns);
            return true;
        }
    }
    if (std::strcmp(desc, "Ljava/lang/Math;") == 0) return Invoke_java_lang_Math(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/Float;") == 0) return Invoke_java_lang_Float(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/Double;") == 0) return Invoke_java_lang_Double(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/String;") == 0) return Invoke_java_lang_String(interp, method, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/Thread;") == 0) return Invoke_java_lang_Thread(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/Throwable;") == 0) return Invoke_java_lang_Throwable(interp, name, args, result);
    if (std::strcmp(desc, "Ljava/util/concurrent/locks/LockSupport;") == 0) {
        return Invoke_java_util_concurrent_locks_LockSupport(interp, name, args, result);
    }
    if (std::strcmp(desc, "Ljava/lang/Runtime;") == 0) {
        // Runtime owns both loadLibrary/load and the heap figures; memory first so
        // totalMemory/freeMemory report the device rather than a constant.
        if (Invoke_kudroid_memory(interp, name, args, num_args, result)) return true;
        return Invoke_java_lang_Runtime(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Ljava/lang/reflect/Method;") == 0) return Invoke_java_lang_reflect_Method(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/reflect/Constructor;") == 0) return Invoke_java_lang_reflect_Constructor(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/reflect/Field;") == 0) return Invoke_java_lang_reflect_Field(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/reflect/Array;") == 0) return Invoke_java_lang_reflect_Array(interp, method, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/reflect/Proxy;") == 0) return Invoke_java_lang_reflect_Proxy(interp, name, args, num_args, result);
    // Direct handles arrive as Direct; route both here.
    if (std::strcmp(desc, "Ljava/lang/invoke/MethodHandle;") == 0 ||
        std::strcmp(desc, "Ljava/lang/invoke/MethodHandle$Direct;") == 0) {
        return Invoke_java_lang_invoke_MethodHandle(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Ljava/lang/invoke/MethodHandles;") == 0) {
        return Invoke_java_lang_invoke_MethodHandles(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Ljava/lang/invoke/MethodHandles$Lookup;") == 0) {
        return Invoke_java_lang_invoke_Lookup(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Landroid/os/FileObserver;") == 0) {
        if (std::strcmp(name, "nativeStart") == 0) {
            const uint32_t mask = static_cast<uint32_t>(args[1].i);
            const std::string path = VFSPathRemapper::getInstance().remap(GetStringUtf8(args[0]));
            if ((mask & 0xfff & ~supported_file_watch_events()) != 0)
                std::fprintf(stderr, "[KuART][FileObserver] unsupported kernel event mask=0x%x\n",
                             mask & 0xfff & ~supported_file_watch_events());
            *result = DexValue::Long(static_cast<int64_t>(start_file_watch(path, mask)));
            return true;
        }
        if (std::strcmp(name, "nativeStop") == 0) {
            stop_file_watch(static_cast<uint64_t>(args[0].j));
            return true;
        }
        if (std::strcmp(name, "nativePoll") == 0) {
            FileWatchEvent event;
            bool received;
            {
                VmLockRelease release;
                received = poll_file_watch(static_cast<uint64_t>(args[0].j), &event);
            }
            auto* array = reinterpret_cast<DexArray*>(args[1].l);
            if (array != nullptr && array->length > 0)
                array->Set<int32_t>(0, received ? static_cast<int32_t>(event.mask) : 0);
            *result = DexValue::Ref(received && !event.name.empty()
                ? interp->linker()->NewString(event.name.c_str()) : nullptr);
            return true;
        }
    }
    if (std::strcmp(desc, "Ljava/io/File;") == 0) return Invoke_java_io_File(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/io/FileInputStream;") == 0) return Invoke_java_io_FileInputStream(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/io/FileOutputStream;") == 0) return Invoke_java_io_FileOutputStream(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/io/RandomAccessFile;") == 0) return Invoke_java_io_RandomAccessFile(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Landroid/database/sqlite/SQLiteNative;") == 0) {
        return Invoke_android_database_sqlite_SQLiteNative(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Ljava/io/PrintStream;") == 0) return Invoke_java_io_PrintStream(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/util/TimeZone;") == 0) return Invoke_java_util_TimeZone(interp, name, args, num_args, result);
    // Direct buffers need a real backing store for native access.
    if (std::strcmp(desc, "Ljava/nio/DirectByteBuffer;") == 0) {
        return Invoke_java_nio_DirectByteBuffer(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Landroid/content/res/AssetManager;") == 0) {
        return Invoke_android_content_res_AssetManager(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Landroid/content/res/Resources;") == 0) {
        return Invoke_android_content_res_Resources(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Landroid/view/Display;") == 0) {
        return Invoke_android_view_Display(name, result);
    }
    // The native method lives on the outer Settings class (inner classes call
    // through it). Hook only that descriptor so later branches still run.
    if (std::strcmp(desc, "Landroid/provider/Settings;") == 0) {
        if (Invoke_android_provider_Settings(interp, name, args, num_args, result))
            return true;
    }
    if (std::strcmp(desc, "Lsun/misc/Unsafe;") == 0) return Invoke_sun_misc_Unsafe(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Llibcore/io/Memory;") == 0)
        return Invoke_libcore_io_Memory(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ldalvik/system/BaseDexClassLoader;") == 0) {
        if (std::strcmp(name, "findResourceBacking") == 0) {
            return Invoke_android_content_res_AssetManager(interp,
                    "nativeResolvePackageResource", args, num_args, result);
        }
        return Invoke_dalvik_system_BaseDexClassLoader(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Ldalvik/system/InMemoryDexClassLoader;") == 0)
        return Invoke_dalvik_system_InMemoryDexClassLoader(interp, name, args, num_args, result);

    if (std::strcmp(desc, "Landroid/util/Log;") == 0) return Invoke_android_util_Log(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Landroid/graphics/drawable/VectorDrawable;") == 0)
        return Invoke_VectorDrawable(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Landroid/graphics/Canvas;") == 0) return Invoke_android_graphics_Canvas(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Landroid/graphics/BitmapFactory;") == 0) return Invoke_android_graphics_BitmapFactory(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Landroid/app/Activity;") == 0) return Invoke_android_app_Activity(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Landroid/app/ActivityThread;") == 0) {
        return Invoke_android_app_ActivityThread(interp, name, args, num_args, result);
    }
    // Choreographer shares one frame source with NDK entry points.
    if (std::strcmp(desc, "Landroid/view/Choreographer;") == 0) {
        return Invoke_android_view_Choreographer(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Landroid/os/Vibrator;") == 0) return Invoke_android_os_Vibrator(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Landroid/os/PowerManager;") == 0) return Invoke_android_os_PowerManager(name, result);
    // MessageQueue owns the native Looper wake slot; touch wakes it and the
    // Looper thread drains input itself.
    if (std::strcmp(desc, "Landroid/os/MessageQueue;") == 0) {
        return Invoke_android_os_MessageQueue(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Landroid/content/pm/RuntimePermissions;") == 0) {
        return Invoke_android_content_pm_RuntimePermissions(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Landroid/media/AudioRecord;") == 0) {
        return Invoke_android_media_AudioRecord(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Landroid/hardware/Camera;") == 0) {
        return Invoke_android_hardware_Camera(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Landroid/graphics/SurfaceTexture;") == 0) {
        if (std::strcmp(name, "nativeCreate") == 0 && num_args == 2) {
            kudroid_surface_texture_create(args[0].i, static_cast<unsigned>(args[1].i));
        } else if (std::strcmp(name, "nativeRelease") == 0 && num_args == 1) {
            kudroid_surface_texture_release(args[0].i);
        } else if (std::strcmp(name, "nativeAttach") == 0 && num_args == 2) {
            kudroid_surface_texture_attach(args[0].i, static_cast<unsigned>(args[1].i));
        } else if (std::strcmp(name, "nativeDetach") == 0 && num_args == 1) {
            kudroid_surface_texture_detach(args[0].i);
        } else if (std::strcmp(name, "nativeUpdate") == 0 && num_args == 1) {
            VmLockRelease unlocked;
            *result = DexValue::Long(kudroid_surface_texture_update(args[0].i));
        } else return false;
        return true;
    }
    if (std::strcmp(desc, "Landroid/graphics/Bitmap;") == 0) {
        return Invoke_android_graphics_Bitmap(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Landroid/net/ConnectivityManager;") == 0) {
        return Invoke_android_net_ConnectivityManager(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Llibcore/io/KuNet;") == 0) {
        return Invoke_libcore_io_KuNet(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Llibcore/io/KuTls;") == 0) {
        return Invoke_libcore_io_KuTls(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Llibcore/io/KuZlib;") == 0) {
        return Invoke_libcore_io_KuZlib(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Llibcore/io/KuCrypto;") == 0) {
        return Invoke_libcore_io_KuCrypto(interp, name, args, num_args, result);
    }
    // AudioTrack is the Java audio path used by FMOD.
    if (std::strcmp(desc, "Landroid/media/MediaPlayer;") == 0) {
        return Invoke_android_media_MediaPlayer(name, args, num_args, result);
    }
    if (std::strcmp(desc, "Landroid/hardware/SensorManager;") == 0) {
        if (std::strcmp(name, "nativeAvailable") == 0 && num_args == 1) {
            *result = DexValue::Int(kudroid_sensor_available(args[0].i));
            return true;
        }
        if (std::strcmp(name, "nativeConfigure") == 0 && num_args == 2) {
            VmLockRelease unlocked;
            kudroid_sensor_java_configure(args[0].i, args[1].i);
            return true;
        }
        if (std::strcmp(name, "nativeRead") == 0 && num_args == 2) {
            auto* output = reinterpret_cast<DexArray*>(args[1].l);
            *result = DexValue::Long(output ? kudroid_sensor_read(args[0].i,
                reinterpret_cast<float*>(output->Data()), output->length) : 0);
            return true;
        }
        return false;
    }
    if (std::strcmp(desc, "Landroid/media/AudioTrack;") == 0) {
        return Invoke_android_media_AudioTrack(interp, name, args, num_args, result);
    }
    // Memory figures come from the host, not constants.
    if (std::strcmp(desc, "Landroid/app/ActivityManager;") == 0 ||
        std::strcmp(desc, "Landroid/os/Debug;") == 0) {
        return Invoke_kudroid_memory(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Landroid/view/Surface;") == 0) {
        if (std::strcmp(name, "nativeSetFrame") == 0 && num_args == 5) {
            VmLockRelease unlocked;
            *result = DexValue::Long(kudroid_surface_set_frame(args[0].j, args[1].i,
                args[2].i, args[3].i, args[4].i));
            return true;
        }
        if (std::strcmp(name, "nativeRelease") == 0 && num_args == 1) {
            VmLockRelease unlocked;
            bionic_ANativeWindow_release(reinterpret_cast<void*>(args[0].j));
            return true;
        }
        return false;
    }
    if (std::strcmp(desc, "Landroid/view/Window;") == 0 ||
        std::strcmp(desc, "Landroid/view/View;") == 0 ||
        std::strcmp(desc, "Landroid/os/PowerManager$WakeLock;") == 0) {
        if (std::strcmp(desc, "Landroid/view/Window;") == 0 && std::strcmp(name, "nativeGetSafeAreaInsets") == 0) {
            auto* edges = num_args > 0 ? reinterpret_cast<DexArray*>(args[0].l) : nullptr;
            if (edges != nullptr && edges->length >= 4)
                kudroid_get_safe_area_insets(reinterpret_cast<int*>(edges->Data()));
            return true;
        }
        return Invoke_keep_screen_on(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Landroid/view/inputmethod/InputMethodManager;") == 0) {
        return Invoke_android_view_inputmethod_InputMethodManager(interp, name, args,
                                                                 num_args, result);
    }
    if (std::strcmp(desc, "Landroid/opengl/GLSurfaceView;") == 0) {
        return Invoke_android_opengl_GLSurfaceView(interp, name, args, num_args, result);
    }

    return false;
}

bool LibCoreHasMethod(const DexMethod* method) {
    if (method == nullptr || method->declaring_class == nullptr) return false;
    const char* desc = method->declaring_class->descriptor;
    if (desc == nullptr) return false;

    return (std::strncmp(desc, "Ljava/", 6) == 0 ||
            std::strcmp(desc, "Lsun/misc/Unsafe;") == 0 ||
            std::strcmp(desc, "Ldalvik/system/BaseDexClassLoader;") == 0 ||
            std::strcmp(desc, "Ldalvik/system/InMemoryDexClassLoader;") == 0 ||
            std::strcmp(desc, "Landroid/util/Log;") == 0 ||
            std::strcmp(desc, "Landroid/graphics/Canvas;") == 0 ||
            std::strcmp(desc, "Landroid/graphics/drawable/VectorDrawable;") == 0 ||
            std::strcmp(desc, "Landroid/app/Activity;") == 0 ||
            std::strcmp(desc, "Landroid/app/ActivityManager;") == 0 ||
            std::strcmp(desc, "Landroid/os/Debug;") == 0 ||
            std::strcmp(desc, "Landroid/os/Vibrator;") == 0 ||
            std::strcmp(desc, "Landroid/os/PowerManager;") == 0 ||
            std::strcmp(desc, "Landroid/os/MessageQueue;") == 0 ||
            std::strcmp(desc, "Landroid/content/res/Resources;") == 0 ||
            std::strcmp(desc, "Landroid/view/Display;") == 0 ||
            std::strcmp(desc, "Landroid/media/AudioTrack;") == 0 ||
            std::strcmp(desc, "Landroid/content/pm/RuntimePermissions;") == 0 ||
            std::strcmp(desc, "Landroid/media/AudioRecord;") == 0 ||
            std::strcmp(desc, "Landroid/hardware/Camera;") == 0 ||
            std::strcmp(desc, "Landroid/graphics/Bitmap;") == 0 ||
            std::strcmp(desc, "Landroid/net/ConnectivityManager;") == 0 ||
            std::strcmp(desc, "Llibcore/io/KuNet;") == 0 ||
            std::strcmp(desc, "Llibcore/io/Memory;") == 0 ||
            std::strcmp(desc, "Llibcore/io/KuTls;") == 0 ||
            std::strcmp(desc, "Llibcore/io/KuZlib;") == 0 ||
            std::strcmp(desc, "Llibcore/io/KuCrypto;") == 0 ||
            std::strcmp(desc, "Landroid/view/Window;") == 0 ||
            std::strcmp(desc, "Landroid/view/Surface;") == 0 ||
            std::strcmp(desc, "Landroid/view/View;") == 0 ||
            std::strcmp(desc, "Landroid/view/inputmethod/InputMethodManager;") == 0 ||
            std::strcmp(desc, "Landroid/opengl/GLSurfaceView;") == 0 ||
            std::strcmp(desc, "Landroid/os/PowerManager$WakeLock;") == 0);
}

// Bridges for the interpreter's proxy dispatch.

DexObject* LibCoreGetRefField(DexObject* obj, const char* name, const char* type) {
    return GetRefField(obj, name, type);
}

DexObject* LibCoreNewMethodObject(DexClassLinker* linker, DexMethod* m) {
    return NewMethodObject(linker, m);
}

DexArray* LibCoreNewRefArray(DexClassLinker* linker, const char* array_descriptor,
                             const std::vector<DexObject*>& items) {
    return NewRefArray(linker, array_descriptor, items);
}

std::vector<std::string> LibCoreSplitParams(const char* signature) {
    return SplitParams(signature);
}

DexObject* LibCoreBoxValue(Interpreter* interp, const char* descriptor, const DexValue& v) {
    return BoxValue(interp, descriptor, v);
}

bool LibCoreUnboxValue(Interpreter* interp, const char* descriptor, DexObject* obj,
                       DexValue* out) {
    return UnboxValue(interp, descriptor, obj, out);
}

}  // namespace kuart
}  // namespace kudroid
