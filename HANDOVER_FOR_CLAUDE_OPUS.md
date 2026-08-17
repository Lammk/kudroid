# TÀI LIỆU CHUYỂN GIAO DỰ ÁN KUDROID CHO CLAUDE OPUS 5
> **Dự án**: KuDroid — Android Runtime Layer (ARM64 Translation & Native Execution Environment) trên iOS.
> **Trạng thái**: Đang hoàn thiện hệ thống Remote Debugger (KDB) và kiểm thử thực tế trên iPhone vật lý.

---

## 1. TỔNG QUAN VỀ KUDROID
KuDroid là một lớp tương thích cấp hệ thống (Compatibility Layer) cho phép chạy ứng dụng/game Android (`.apk` chứa mã nhị phân ARM64 Native `.so` và bytecode DEX) trực tiếp trên nền tảng iOS mà không cần jailbreak:
- **ELF Dynamic Linker/Loader**: Nạp file ELF `.so` (Android Bionic ARM64) trực tiếp vào không gian địa chỉ bộ nhớ iOS.
- **Bionic Shims**: Giả lập toàn bộ hệ thống libc, pthread, futex, ashmem, epoll, syscalls Linux (arm64).
- **Graphics Pipeline**: Ánh xạ OpenGL ES 2.0/3.0 và Vulkan sang `CAMetalLayer` thông qua ANGLE Metal & MoltenVK; hỗ trợ 2D CPU Canvas Blitting (`ANativeWindow_lock/unlockAndPost` $\rightarrow$ `CGImage` $\rightarrow$ `CALayer`).
- **JVM & JNI Runtime**: Tích hợp Avian JVM siêu nhẹ + pipeline AOT chuyển đổi DEX $\rightarrow$ JAR (`DexAotCache`).
- **Giao diện Shell**: Ứng dụng SwiftUI/UIKit (`ios-app/KuDroidShell/`) quản lý danh sách app, bộ cài APK, và bộ điều khiển container toàn màn hình (`NativeMetalViewController`).

---

## 2. NHỮNG CÔNG VIỆC VỪA HOÀN THÀNH GẦN ĐÂY
1. **Khắc phục lỗi Linker Mach-O (Commit `3a6b7a8`)**: Định nghĩa weak empty function body cho `kudroid_blit_canvas_to_layer` trên macOS Runner để test suite `test_shims` và `test_dex_to_jar` build & pass 100% (52 checks).
2. **Kích hoạt Real-Time Echo Logging (Commit `afb63e8`)**: Sửa `kudroid_run_apk` trong `src/kudroid_bridge.cpp` để bắn toàn bộ log khởi chạy ra `stderr` và `kudroid_android_log_message` thay vì chỉ lưu trong buffer.
3. **Sửa vòng đời Container trên iOS (Commit `dbbb5c1`)**: Giải quyết triệt để lỗi SwiftUI `UIViewControllerRepresentable` nuốt mất sự kiện `viewDidAppear`, thêm hàm `startAppIfNeeded()` đa điểm (`viewDidLoad`, `viewDidLayoutSubviews`, `updateUIViewController`, `viewDidAppear`) để đảm bảo app Android chắc chắn được kích hoạt khi bấm RUN.

---

## 3. Ý TƯỞNG & TÍNH NĂNG ĐỘT PHÁ MỚI: KDB (KuDroid Debug Bridge)
Người dùng đã đưa ra ý tưởng xây dựng một hệ thống **Remote Debug Bridge (tương tự ADB của Android)** kết nối giữa PC (Linux/macOS) và iPhone thông qua mạng nội bộ (WiFi/USB WebSocket Server).

### Quy tắc cốt lõi:
1. **Single Source of Truth**: Log xuất ra từ iPhone hay từ lệnh PC phải **giống nhau 100% từng byte**.
2. **Zero-Noise**: Chỉ stream log của KuDroid Core (loại bỏ hoàn toàn log rác của iOS hệ thống).
3. **Đồng bộ thời gian thực 2 chiều**:
   - Khi gõ lệnh `run <app_id>` trên PC, iPhone đang ở màn hình Launcher sẽ **tự động chuyển cảnh toàn màn hình (Foreground Transition)** mở ngay giao diện render đồ họa của app đó.
   - Khi chạy lệnh `debug`, server sẽ bật chế độ **"Thập Cẩm" All-In-One Stream** (cài app, nạp thư viện, JNI, EGL, crash dump). Khi nhấn `Ctrl + C`, CLI sẽ thoát stream và **tự động lưu toàn bộ phiên log vào file `./logs/debug_YYYY-MM-DD_HH-mm-ss.log` trên PC**.

### Bảng câu lệnh KDB CLI cần triển khai:
- `help`: Hiển thị hướng dẫn sử dụng và danh sách câu lệnh.
- `list`: Liệt kê các app Android đã cài trên iPhone (`/data/app`).
- `run <app_id>`: Mở app trực tiếp trên màn hình iPhone và bắt đầu stream log.
- `stop [app_id]`: Đóng app trên iPhone, quay về màn hình Launcher.
- `install <path_to_apk_on_pc>`: Bắn file APK từ PC sang iPhone và kích hoạt trích xuất native.
- `debug`: Chế độ nghe log tổng hợp thời gian thực, `Ctrl + C` để thoát và lưu log ra đĩa.
- `save <crash|log|snapshot>`: Kéo file log/crash dump từ iPhone về PC.
- `clear <cache <app_id>|all>`: Xóa bộ nhớ đệm / dalvik-cache.

---

## 4. NHIỆM VỤ TIẾP THEO CHO CLAUDE OPUS 5
1. **Triển khai Server KDB trên PC (`tools/kdb/`)**:
   - Viết Node.js WebSocket/HTTP Server (`server.js` + `cli.js`) trên port `8080`.
   - Thiết kế giao diện dòng lệnh tương tác (Readline / Inquirer), hỗ trợ màu sắc ANSI và tự động lưu log khi nhận tín hiệu `SIGINT` (`Ctrl + C`).
2. **Triển khai Remote Client trên iOS (`ios-app/KuDroidShell/`)**:
   - Viết `RemoteDebugClient.swift` (sử dụng `URLSessionWebSocketTask` native của iOS, có cơ chế auto-reconnect).
   - Thêm ô nhập IP Server (lưu vào `UserDefaults`) trong giao diện SwiftUI.
   - Nối đường ống log từ C++ (`logAndroidMessage` trong `src/abi/SyscallShim.cpp` / `src/kudroid_bridge.cpp`) sang `RemoteDebugClient` để phát tán log tức thời.
   - Xử lý lệnh `run <app_id>` từ WebSocket để set `session.activeGuestApp = app_id` trên Main Thread.
3. **Kiểm tra và xác thực**:
   - Đảm bảo `cmake --build build && ./build/test_shims` luôn pass trước khi commit.
   - Giữ nguyên cấu trúc code sạch sẽ, comment tiếng Việt rõ ràng.
