# 2026-08-11 — Fix CI Build (Round 3)

## Vấn đề & Fix

### 1. Workflow invalid: `ios-build` depends on unknown job `build-avian-ios`
- **Nguyên nhân**: `build-avian-ios` nằm trong `build-avian.yml` (file riêng), nhưng `ios-build` trong `build.yml` không thể phụ thuộc job từ file khác.
- **Fix**: Di chuyển job `build-avian-ios` vào `build.yml` (cùng file với `ios-build`).

### 2. Avian build fail: `classpath-jar.o` architecture 'unknown'
- **Nguyên nhân**: `binaryToObject` tool tạo object với architecture 'unknown' trên iOS/macho → link fail.
- **Fix**: Patch makefile Avian để dùng `ld -r -sectcreate __TEXT __kudroid_classpath` thay vì `binaryToObject`.
- Patch `boot.cpp` để dùng `getsectdata("__TEXT", "__kudroid_classpath", &s)` (Apple API) thay vì symbol `_binary_classpath_jar_start/_end` (mà `ld -r -sectcreate` không tạo).

## File đã thay đổi
- `.github/workflows/build.yml` (thêm job build-avian-ios, patch makefile + boot.cpp)
- `.github/workflows/build-avian.yml` (patch makefile + boot.cpp)

## Lưu ý
- `ld -r -sectcreate` + `getsectdata` là cách chuẩn cho macOS/iOS để embed binary data.
- Section name `__kudroid_classpath` riêng biệt để tránh merge với section `__const` khác.