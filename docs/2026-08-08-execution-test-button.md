# 2026-08-08 — Add Execution Test Button

## Đã làm gì
Thêm nút "Execution Test" vào SwiftUI `ContentView` và hàm helper `runExecutionTest()`.

## Làm như thế nào

### 1. Thêm nút "Execution Test" vào UI (`ContentView.swift`)
- Thêm `HStack` mới với `Button("Execution Test")` sau hàng "Load Bundled .so" + "Self-Test"
- Nút có style `.borderedProminent`, màu cam (`.tint(.orange)`)
- Gọi `runExecutionTest()` khi bấm

### 2. Thêm hàm `runExecutionTest()`
- Copy `test_lib.so` từ bundle ra tmp nếu chưa tồn tại
- Gọi `kudroid_execution_test(tmpURL.path)` từ C++ bridge
- Trả về log string (giải phóng bộ nhớ C string sau khi dùng)

### 3. Bridge C++ đã có sẵn
- `kudroid_bridge.h` đã khai báo `kudroid_execution_test`
- `kudroid_bridge.cpp` đã implement flow: parse → map → relocate → testExecution (gọi `kudroid_add(40, 20)`)

## File đã thay đổi
- `ios-app/KuDroidShell/ContentView.swift`: thêm nút + hàm

## Kết quả
- UI có 3 hàng nút: (Load + Self-Test) | (Execution Test) | (Copy Full Log)
- Nút Execution Test gọi được C++ bridge để test thực thi hàm native từ .so