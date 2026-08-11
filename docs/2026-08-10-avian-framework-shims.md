# 2026-08-10 — Avian JVM Switch + Android Framework + Shims

## Tổng quan
Đợt thay đổi lớn: chuyển từ miniJVM sang **Avian JVM**, thêm **Android framework** (Java), và nâng cấp các shim (input, syscall, audio).

## Đã làm

### 1. Avian JVM switch (thay miniJVM)
- **`src/kudroid_jni.cpp`**: Viết lại hoàn toàn dùng Avian `JNI_CreateJavaVM`. Bỏ `MiniJVM*`, `jvm_create`, `jvm_init`. Giữ API `kudroid_jni_init_jvm/destroy/get_javavm/get_env/set_log_callback`.
- **`src/kudroid_jni_impl.inc`**: Xóa (230 JNI stub không còn cần — Avian có JNI chuẩn).
- **`scripts/gen_jni_bridge.py`**: Xóa (không còn dùng).
- **`CMakeLists.txt`**: Bỏ include `third_party/jvm/miniJVM`, thêm Avian include + link `libavian.a` (nếu có) + embed boot jar.
- **`.github/workflows/build-avian.yml`**: Workflow mới build Avian iOS trên GitHub Actions.
- **`.gitmodules`**: Avian là submodule `ReadyTalk/avian`.

### 2. Android framework (Java) — `framework/`
- **76 file Java** compile thành `framework.jar` (75K).
- **Class thật**: `android.util.Log`, `android.os.Handler/Looper/MessageQueue/Message/Bundle`, `android.app.Activity/Application/Dialog/AlertDialog`, `android.content.Context/ContextWrapper/Intent/SharedPreferences`, `android.view.View/ViewGroup/MotionEvent/Window`, `android.widget.TextView/Button/LinearLayout/Toast`, `android.graphics.*`.
- **Class stub**: `TelephonyManager`, `BluetoothAdapter`, `NotificationManager`, `LocationManager`, `WifiManager`, `SensorManager`, `AudioManager`, `Vibrator`, `PowerManager`, `ConnectivityManager`, `Settings`, `ClipboardManager`, `InputMethodManager`.
- **`framework/build.sh`**: Compile .java → .class → JAR.
- **`framework/README.md`**: Hướng dẫn.

### 3. DEX cache system
- **`src/DexCacheManager.cpp` + `include/kudroid/DexCacheManager.h`**: Cache DEX→JAR theo SHA-256 hash + tool version. Atomic write (tmp + rename). "1 trong 2 sai thì rebuild".

### 4. Input shim — `src/shims/InputShim.cpp`
- Thêm **AInputQueue** với FIFO + mutex.
- Thêm `AInputQueue_getEvent/preDispatchEvent/finishEvent/attachLooper/detachLooper/hasEvents`.
- Thêm `AMotionEvent_getPointerCount/getPointerId/getRawX/getRawY/getDownTime/getEventTime`.
- Giữ sensor stubs.

### 5. Syscall hardening — `src/shims/SyscallShim.cpp`
- **futex**: thêm `FUTEX_WAIT_BITSET`, `FUTEX_WAKE_BITSET`, `FUTEX_REQUEUE`, `FUTEX_CMP_REQUEUE`.
- Thêm syscall mới: `getcpu`, `sched_getaffinity`, `sched_setaffinity`, `inotify_init1/add_watch/rm_watch`, `signalfd`, `eventfd2`, `prlimit64`, `statx`.

### 6. Audio shim — `src/shims/AudioShim.cpp` (mới)
- **OpenSL ES stubs**: `slCreateEngine`, `slObjectDestroy/Realize/GetInterface`, `slEngineCreateAudioPlayer/CreateOutputMix`, `slAndroidSimpleBufferQueue*`, `slPlay*`, `slVolume*`.
- **AAudio stubs**: `AAudio_createStreamBuilder`, `AAudioStreamBuilder_*`, `AAudioStream_*`.
- Thêm `get_audio_symbols` vào `bionic_dlsym` + `resolve_bionic_symbol`.

### 7. Graphics fixes
- **`ios-app/entitlements.plist`**: JIT entitlement cho sideload (`get-task-allow`, `allow-jit`, `allow-unsigned-executable-memory`).
- **`.github/workflows/build.yml`**: Embed entitlements vào app bundle.

### 8. Trick nạp Java — `src/kudroid_bridge.cpp`
- `kudroid_run_apk`: init Avian JVM trước khi gọi `JNI_OnLoad` + `ANativeActivity_onCreate`.

### 9. Release
- **`LICENSE`**: MIT.
- **`readme.md`**: Thêm hướng dẫn build + architecture.
- **`framework/README.md`**: Hướng dẫn framework.

## File đã thay đổi
- `src/kudroid_jni.cpp` (viết lại)
- `src/kudroid_bridge.cpp`
- `src/BionicShim.cpp`
- `src/shims/SyscallShim.cpp`
- `src/shims/InputShim.cpp`
- `src/shims/AudioShim.cpp` (mới)
- `src/DexCacheManager.cpp` (mới)
- `include/kudroid/DexCacheManager.h` (mới)
- `include/kudroid/shims/AudioShim.h` (mới)
- `CMakeLists.txt`
- `.github/workflows/build.yml`
- `.github/workflows/build-avian.yml` (mới)
- `ios-app/entitlements.plist` (mới)
- `framework/**` (mới, 76 file Java + build.sh + README)
- `LICENSE` (mới)
- `readme.md`
- Xóa: `src/kudroid_jni_impl.inc`, `scripts/gen_jni_bridge.py`

## Kết quả
- Build Linux: ✅ sạch (không warning/error)
- Framework Java: ✅ compile 76 file → framework.jar
- Avian: chưa build (cần GitHub Actions hoặc `make platform=ios`)

## Cần test trên device
- Avian JVM chạy trên iOS device (Bước 0.2 spike)
- OpenGL/Vulkan qua ANGLE/MoltenVK
- AInputQueue nhận touch event
