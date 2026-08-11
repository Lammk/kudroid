# 2026-08-11 — Game Test Support (Teapot / Flappy Bird)

## Mục tiêu
Chuẩn bị KuDroid để chạy game pure-native (Teapot NDK sample, Flappy Bird clone) — test khởi động + render + input.

## Đã làm

### 1. Lifecycle callbacks (`src/kudroid_bridge.cpp`)
- `kudroid_run_apk` giờ gọi các lifecycle callbacks sau `ANativeActivity_onCreate`:
  - `onStart` → `onResume` → `onWindowFocusChanged` → `onNativeWindowCreated` → `onInputQueueCreated`
- Điều này giúp game bắt đầu render + nhận input (trước đây callbacks rỗng → game không render).

### 2. ALooper (`src/shims/SyscallShim.cpp`)
- Implement `ALooper` API (Android event loop) — game Teapot dùng để chạy event loop:
  - `ALooper_prepare`, `ALooper_forThread`, `ALooper_acquire`, `ALooper_release`
  - `ALooper_addFd`, `ALooper_removeFd`, `ALooper_wake`
  - `ALooper_pollOnce`, `ALooper_pollAll` (backed by `poll()`)
- Thêm vào symbol table.

### 3. ANativeWindow lock (`src/shims/GraphicsShim.cpp`)
- `ANativeWindow_lock` / `ANativeWindow_unlockAndPost` — trả dummy buffer (cho game dùng lock API).
- Thêm vào symbol table.

### 4. Input wake pipe (`src/shims/InputShim.cpp`)
- `BionicInputQueue` có wake pipe (pipe) để đánh thức looper khi có touch.
- `kudroid_inject_touch_event` ghi vào pipe sau khi push event.
- `AInputQueue_attachLooper` add pipe fd vào looper (qua `ALooper_addFd`).

## File đã thay đổi
- `src/kudroid_bridge.cpp`
- `src/shims/SyscallShim.cpp`
- `src/shims/GraphicsShim.cpp`
- `src/shims/InputShim.cpp`

## Kết quả
- Build Linux: sạch ✅
- Test `test_dex_to_jar`: PASS ✅

## Cần test trên device
- JIT (game cần JIT để chạy `.so`).
- OpenGL render qua ANGLE/MoltenVK.
- Touch input qua AInputQueue + ALooper.
- `g_metalLayer` phải được set từ Swift trước khi game render.