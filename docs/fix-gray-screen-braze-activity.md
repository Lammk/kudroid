# Fix màn hình xám — Braze Activity + manifest TEXT (phần 2)

**Ngày:** 2026-08-22
**Build:** `b863020`
**Tiền đề:** xem `fix-fallback-ui-scan-jar.md` (phần 1)

## Kết quả thực tế trên máy (build b863020)

Pipeline chạy hết nhưng **launch nhầm Activity**:

```
[kudroid_core] Manifest parse: package='' mainActivity=''     ← AXML parser rỗng!
[kudroid_core] Found 500 non-system classes in classes.jar
[kudroid_core] Resolved from classes.jar: com.braze.push.NotificationTrampolineActivity
[ActivityThread] Activity launch complete! UI is live and rendered to Metal canvas.
```

→ **Màn hình xám là ĐÚNG hành vi** — `NotificationTrampolineActivity` là Activity
của **Braze push SDK**, nó không render gì cả, chỉ forward notification. Game
chưa hề được launch.

## Nguyên nhân

1. **Manifest là TEXT XML chứ không phải binary AXML**: APK repack bởi
   apktool/BANDISHARE giữ manifest dạng text (20420 bytes bắt đầu bằng `<`).
   Parser AXML trả rỗng im lặng với file text.
2. **Jar scan không phân biệt SDK vs game**: `com.braze.push.*Activity` thỏa
   điều kiện "chứa Activity" nên được chọn dù thuộc SDK bên thứ ba.

## Fix

### 1. `parse_manifest_text()` — parser XML text thuần

`APKExtractor.cpp/h`:
- Quét từng thẻ `<activity>`/`<activity-alias>`, gom attr `name=`/`android:name=`
- Theo dõi intent-filter con chứa action MAIN + category LAUNCHER
- Hỗ trợ namespace prefix (`:` trước attrName) và self-closing tag
- Bridge thử parser này khi AXML trả rỗng VÀ file bắt đầu bằng `<`

### 2. SDK filter trong jar scoring

`kudroid_bridge.cpp` — trừ -1000 điểm với Activity của SDK bên thứ ba:
braze, facebook, firebase, google, admob, unity3d, appsflyer, adjust, amplitude,
mixpanel, crashlytics, playfab, microsoft. Thêm +50 nếu package khớp pkgName
từ manifest/app_info.

### 3. Bug fix `extractXmlAttr`

Attr namespaced `android:name="..."` bị bỏ qua vì ký tự trước "name=" là 'd'
(không phải space/'<'). Cho phép thêm ':' làm prefix hợp lệ.

## Xác nhận

- ✅ Test thủ công manifest Minecraft-style TEXT:
  `package='com.mojang.minecraftpe' mainActivity='com.mojang.minecraftpe.MainActivity'` PASS
- ✅ test_shims 52/52, test_manifest_parse PASSED, test_dex_to_jar PASS

## Files thay đổi

- `include/kudroid/APKExtractor.h` — khai báo `parse_manifest_text`
- `src/APKExtractor.cpp` — implement text parser + fix namespaced attr
- `src/kudroid_bridge.cpp` — thử text parser khi AXML rỗng + SDK filter scoring

## Kỳ vọng lần chạy tới

```
[kudroid_core] Manifest is TEXT XML, trying text parser...
[kudroid_core] Manifest parse: package='com.mojang.minecraftpe' mainActivity='com.mojang.minecraftpe.MainActivity'
[kudroid_core] Target Activity: com.mojang.minecraftpe.MainActivity
[ActivityThread] Instantiating Activity: com.mojang.minecraftpe.MainActivity
```

→ Game thật được launch, màn hình không còn xám (nếu game render qua EGL/Metal
bình thường).
