# 2026-08-11 — DEX→JAR Pipeline (Bước 2.2)

## Đã làm

### 1. `DexToJar` — DEX parser + JVM class writer + JAR writer
- **File mới**: `include/kudroid/DexToJar.h`, `src/DexToJar.cpp`
- Parse DEX: header, string_ids, type_ids, proto_ids, field_ids, method_ids, class_defs, class_data.
- Với mỗi class, tạo `.class` file JVM hợp lệ:
  - Constructor mặc định (`<init>()V` → `Object.<init>`)
  - Các method với signature đúng, **body rỗng** (trả default: `iconst_0`/`aconst_null`/`lconst_0`/...)
  - Fields với access flags
- Đóng gói thành JAR (ZIP, stored entries, CRC32, little-endian).

### 2. Tích hợp cache
- `DexCacheManager::translateAndCache()` — kiểm tra cache (hash + version), nếu miss thì dịch + lưu cache.
- C bridge `kudroid_translate_dex()` trong `kudroid_bridge.cpp` — gọi từ Swift, ghi JAR ra `translated_classes.jar`.

### 3. Test
- **File mới**: `tests/test_dex_to_jar.cpp` — tạo DEX tối thiểu bằng tay, chạy `DexToJar::convertBytes`, xác nhận ZIP magic + entry `.class`.
- **Kết quả**: PASS ✅
  - ZIP magic `50 4b 03 04` đúng
  - JAR chứa `com/example/Hello.class`
  - `javap -c` xác nhận class hợp lệ: constructor + `greet()` trả `String` với body rỗng

## File đã thay đổi
- `include/kudroid/DexToJar.h` (mới)
- `src/DexToJar.cpp` (mới)
- `src/DexCacheManager.cpp` + `.h` (thêm `translateAndCache`)
- `src/kudroid_bridge.cpp` (thêm `kudroid_translate_dex`)
- `ios-app/KuDroidShell/kudroid_bridge.h` (khai báo)
- `CMakeLists.txt` (thêm `DexToJar.cpp` + test target)
- `tests/test_dex_to_jar.cpp` (mới)

## Lưu ý
- Tool version = 1 (tăng khi sửa logic dịch).
- Chưa dịch opcode bên trong thân hàm — chỉ cấu trúc class + method (theo yêu cầu). Khi có issues GitHub mới phát triển tiếp.
- Test executable không gọi được `kudroid_translate_dex` (static lib kéo cả bridge → lỗi link). Test chỉ validate `DexToJar` trực tiếp. iOS app link OK.