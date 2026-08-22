# Fix ClassNotFoundException — đọc đúng LAUNCHER Activity từ AndroidManifest

**Ngày:** 2026-08-22
**Bối cảnh:** Sau khi fix crash SIGSEGV (xem `fix-minecraft-libmaesdk-crash.md`),
Minecraft PE không còn crash nhưng rơi vào **fallback UI** thay vì chạy game.

## Triệu chứng

Log mới cho thấy toàn bộ 8 thư viện native load thành công, Avian JVM khởi động
OK, nhưng launch Activity thất bại:

```
[kudroid_core] Target Activity: Minecraft.MainActivity        ← ĐOÁN SAI!
[ActivityThread] Launching target Activity immediately: Minecraft.MainActivity
Candidate 'Minecraft.MainActivity' failed: java.lang.ClassNotFoundException: Minecraft/MainActivity
... (7 candidates đều fail)
[ActivityThread] Could not resolve Activity class, launching Fallback KuDroid UI...
```

Package thật của Minecraft PE là `com.mojang.minecraftpe`, launcher activity là
`com.mojang.minecraftpe.MainActivity` — không phải `Minecraft.MainActivity`.

## Nguyên nhân

1. **`parseAxml` lấy activity ĐẦU TIÊN** trong manifest chứ không phải activity
   có `<intent-filter>` với `action.MAIN` + `category.LAUNCHER`. Minecraft có
   hàng chục `<activity>`, activity đầu tiên không phải entry point.
2. Khi parse thất bại/kết quả không dùng được, `kudroid_bridge.cpp` fallback
   **đoán tên** theo tên folder (`<folder>.MainActivity`) — gần như luôn sai.

## Các fix

### 1. `src/APKExtractor.cpp` — parseAxml nhận diện LAUNCHER activity thật

- Theo dõi state qua các chunk AXML: `currentActivity`, `inIntentFilter`,
  `sawActionMain`, `sawCategoryLauncher`.
- Chỉ commit `mainActivity` khi gặp đủ cặp `android.intent.action.MAIN` +
  `android.intent.category.LAUNCHER` trong intent-filter của activity đó.
- Hỗ trợ `<activity-alias targetActivity="...">` (launcher mở TARGET).
- Fallback: nếu manifest không có intent-filter nào → dùng activity đầu tiên.
- **Bug tự phát hiện:** state ban đầu bị khai báo BÊN TRONG vòng while → bị
  reset mỗi chunk (AXML = mỗi tag một chunk). Đã đưa ra ngoài loop.

### 2. `include/kudroid/APKExtractor.h` + `parse_manifest()`

- Struct `ManifestInfo` chuyển thành public (namespace-level).
- Thêm `APKExtractor::parse_manifest(data, size)` để parse AXML đã giải nén.

### 3. `src/kudroid_bridge.cpp` — thứ tự ưu tiên mới khi tìm Target Activity

1. Parse trực tiếp `AndroidManifest.xml` đã giải nén trong appDir (nguồn chính xác)
2. `app_info.json` do extractor ghi lúc cài đặt
3. Đoán theo package/tên folder (chỉ khi cả hai nguồn trên thất bại)

### 4. `tests/test_manifest_parse.cpp` — unit test mới

Build AXML tổng hợp bằng tay (string pool UTF-16 + start/end element chunks)
với 2 activity: SplashActivity (đầu tiên, KHÔNG launcher) và MainActivity (có
intent-filter MAIN+LAUNCHER). Test xác nhận parser chọn đúng MainActivity.

Bug trong test builder cũng đã sửa: `stringsStart` phải = header(28) + offset
table (`count*4`), không phải 28 — blob đè lên offset table khiến string rỗng.

## Kết quả

- ✅ `test_manifest_parse`: PASSED (packageName + LAUNCHER activity đúng)
- ✅ `test_shims`: 52/52 checks OK
- ✅ `test_dex_to_jar`: PASS
- ⏳ Cần build iOS + test Minecraft PE lần nữa: giờ sẽ thấy
  `Target Activity: com.mojang.minecraftpe.MainActivity` và game vào thẳng UI

## Files thay đổi

- `src/APKExtractor.cpp` — LAUNCHER detection + fix state scope
- `include/kudroid/APKExtractor.h` — public ManifestInfo + parse_manifest()
- `src/kudroid_bridge.cpp` — ưu tiên parse manifest trước khi đoán
- `tests/test_manifest_parse.cpp` — test mới
- `CMakeLists.txt` — thêm test_manifest_parse
