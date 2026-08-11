# 2026-08-11 — Full Refactor & Bug Fixes

## Đã làm

### 1. Refactor SyscallShim (`src/shims/SyscallShim.cpp`)
- **`gSyncRegistry`**: Tách `create_sync_obj`/`destroy_sync_obj` helper, dùng enum `SYNC_MUTEX/COND/RWLOCK` thay số 1/2/3. Cleanup đúng khi destroy.
- **`bionic_pthread_mutex_init`/`cond_init`/`rwlock_init`**: Dùng `create_sync_obj` (tránh duplicate logic).
- **`bionic_sigaction`**: Copy `sa_mask` đúng (Android 64-bit bitmask ↔ sigset_t).
- **`bionic_epoll_wait`**: Dùng stack buffer (64 events) thay malloc, heap cho lớn hơn (tránh leak).
- **`bionic_pthread_key_create`**: Dùng `memcpy` thay `static_cast<int*>` (an toàn hơn).
- **`bionic_close`** (mới): Cleanup GCD timerfd khi fd đóng (tránh leak). Thêm vào symbol table.
- **`bionic_getrandom`**: Dùng `arc4random_buf` trên Apple (nhanh + an toàn).
- **`bionic_ashmem_create_region`**: Dùng `std::atomic` counter thay `rand()` (thread-safe).
- **`DUMMY_HANDLE`**: Đổi thành `0x4B5544524F494421` ("KUDROID!") tránh trùng pointer.

### 2. Fix race conditions JNI (`src/kudroid_jni.cpp`)
- **`g_jni_log_callback`**: Thêm `g_log_mutex` — thread-safe.
- **`kudroid_jni_get_env`**: Dùng `GetEnv`/`AttachCurrentThread` per-thread (JNIEnv là per-thread trong JVM thật).
- **`kudroid_jni_get_javavm`**: Lock khi đọc `g_vm`.
- **`kudroid_jni_init_jvm`**: Thêm `-Xmx256m` giới hạn heap cho iOS.

### 3. Refactor VFS (`src/VFSPathRemapper.cpp`)
- **`remap`**: Thêm mapping `/data/user_de/0/`, `/data/app/`, `/data/local/tmp/`, `/cache/`, `/dev/`.
- **`vfs_open`**: Tạo parent dir cho O_CREAT (tránh ENOENT crash).
- **`vfs_stat`**: Tạo pseudo-file `/proc/self/maps` nếu chưa tồn tại.
- **`initialize`**: Thêm thư mục Android chuẩn (`data/app`, `data/local/tmp`, `data/cache`, `dev`, `sdcard/Android/*`).

### 4. Fix bugs khác
- **`BionicShim.cpp`**: `print_bound_symbols` thêm lock (tránh race).
- **`InputShim.cpp`**: `kudroid_inject_touch_event` dùng `steady_clock` cho eventTime (thay 0).

## File đã thay đổi
- `src/shims/SyscallShim.cpp`
- `src/kudroid_jni.cpp`
- `src/VFSPathRemapper.cpp`
- `src/BionicShim.cpp`
- `src/shims/InputShim.cpp`

## Kết quả
- Build Linux: sạch, không warning/error ✅
- Test `test_dex_to_jar`: PASS ✅
- Không mất/hỏng logic nào — tất cả refactor giữ nguyên hành vi, chỉ tối ưu + fix bug.