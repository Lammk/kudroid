# Fix fallback UI lần 2 — quét classes.jar tìm Activity thật

**Ngày:** 2026-08-22
**Build xác nhận:** `25d4704` (build stamp hoạt động đúng — nhìn log biết ngay bản IPA)

## Vấn đề

IPA `25d4704` đã chứa fix parse manifest, nhưng log vẫn hiện:

```
[kudroid_core] Target Activity: Minecraft.MainActivity    ← vẫn đoán!
```

→ Nghĩa là **parse AndroidManifest.xml của Minecraft thất bại im lặng** (khác
AXML tổng hợp trong test — manifest thật của Minecraft phức tạp hơn: nhiều
namespace, attribute không có rawValue, v.v.). Khi cả manifest lẫn app_info.json
đều rỗng, code rơi vào nhánh đoán theo tên folder → sai → ClassNotFoundException
→ fallback UI xám.

## Fix

### 1. Diagnostic logging (`kudroid_bridge.cpp`)

Parse manifest giờ in rõ kết quả:
```
[kudroid_core] Parsing AndroidManifest.xml (N bytes)...
[kudroid_core] Manifest parse: package='...' mainActivity='...'
```
→ Lần sau nhìn log là biết ngay parser đọc được gì, hết đoán mò.

### 2. Fallback mới — quét classes.jar (ƯU TIÊN 3)

Thêm `DexAotCache::list_app_classes(jar_path)`:
- Đọc central directory của classes.jar (AOT cache đã có sẵn)
- Lọc bỏ package hệ thống (android/, androidx/, java/, kotlin/, org/...)
- Bỏ inner class (`$`)
- Trả về danh sách class của app

Trong `kudroid_bridge.cpp`, chấm điểm chọn launcher:
- Chứa "Activity" trong tên: +100 điểm
- Package càng ngắn (gốc app): +điểm theo depth

Với Minecraft sẽ ra `com/mojang/minecraftpe/MainActivity` → đổi slash thành dot
→ `com.mojang.minecraftpe.MainActivity` — chính xác mà không cần manifest!

### 3. Thứ tự ưu tiên hoàn chỉnh khi tìm Target Activity

1. Parse `AndroidManifest.xml` đã giải nén (chính xác nhất)
2. `app_info.json` từ lúc cài đặt
3. **MỚI:** Quét `classes.jar` tìm class `*Activity` ở package gốc
4. Đoán theo tên folder (last resort)

## Xác nhận

- ✅ Build pass toàn bộ
- ✅ Test thủ công: `list_app_classes("/tmp/test_dex_to_jar.jar")` → tìm đúng `com/example/Hello`
- ✅ test_shims 52/52, test_manifest_parse PASSED, test_dex_to_jar PASS

## Files thay đổi

- `include/kudroid/DexAotCache.h` — khai báo `list_app_classes`
- `src/DexAotCache.cpp` — implement (dùng lại `collect_zip_entries` có sẵn)
- `src/kudroid_bridge.cpp` — diagnostic log + fallback quét jar + include `<algorithm>`
