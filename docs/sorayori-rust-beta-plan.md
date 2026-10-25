# Plan: chạy Sorayori (và tiến tới mọi APK) trên iOS — nhánh `rust-beta`

> Tài liệu kế hoạch nghiên cứu + triển khai. Mọi con số trong mục 3 và 4 là **đo trực tiếp**
> từ file `Sorayori-release.apk` (bản release đính kèm ngày 2026-10-25) và từ source của
> repo này ở `master` (`ba75515`). Chỗ nào là suy luận sẽ ghi rõ "suy luận".
>
> Đặt tên nhánh: Sorayori được gọi là "(java)" vì APK có `classes.dex`, nhưng khi mổ ra,
> phần Java/Kotlin chỉ là **vỏ sinh tự động của Tauri** (2,1 MB dex). Toàn bộ logic thật nằm
> trong `lib/arm64-v8a/libsorayori_lib.so` — **15 MB code Rust** (wry/tao/tauri runtime).
> Ngôn ngữ chính của mục tiêu này là Rust nên nhánh là `rust-beta`.

---

## 0. TL;DR

1. **KuDroid không bắt đầu từ số 0.** Repo đã có: KuART (interpreter DEX + JIT ARM64 đọc
   thẳng `.apk`), ELF loader cho `.so` Android, Bionic shim, cầu ANGLE→Metal,
   MoltenVK, shell iOS (Swift), entitlements JIT, và đang debug một game native
   (`com.min.n.*`, FMOD/Vulkan). Hướng kiến trúc "translation layer, không phải emulator"
   trong README là đúng và khớp với mọi tiền lệ thành công (Wine, libhybris, LiveContainer).
2. **Sorayori là ca dễ nhất trong các ca khó** — và là ca test lý tưởng cho nhánh Rust/WebView:
   - Chỉ **1 file `.so`**, chỉ **98 undefined symbol**, không link EGL/GLES/Vulkan trực tiếp
     (render qua WebView), không Binder, không đa tiến trình.
   - Dex mỏng: 2.160 class định nghĩa, trong đó lõi Tauri chỉ ~96 class
     (`app/tauri/*` 60 + `studio/sakayori/sorayori/*` 36).
   - Native cần đúng **24 JNI export** (đã liệt kê đủ ở mục 4.3) — bề mặt cầu nối hữu hạn, đếm được.
3. **Khoảng trống lớn nhất hiện tại nằm ở framework:** `framework/android/webkit/WebView.java`
   mới có **34 dòng (stub)**. Sorayori sống bằng WebView; game trước đây không cần nên chưa ai làm.
   Làm xong WebView→WKWebView là mở được cả họ app hybrid (Tauri/Cordova/Capacitor/WebView-app),
   không chỉ riêng Sorayori.
4. **Tường cứng duy nhất không lách bằng code được:** thực thi code native không được Apple ký
   (mã máy ARM64 trong ELF `.so`) trên iOS stock **bắt buộc** phải có đường JIT
   (debugger attach qua StikDebug/JitStreamer, hoặc TrollStore/CoreTrust, hoặc jailbreak).
   Không có JIT thì `.so` không chạy — chỉ còn đường diễn giải toàn bộ bằng CPU emulator
   (QEMU TCG threaded-interpreter như UTM SE) với giá hiệu năng rất đắt. Plan này lấy
   **JIT-là-bắt-buộc cho tầng native** làm tiền đề, đúng như README hiện tại ("please enable JIT").
5. Lộ trình tới "Sorayori chạy được" gồm 7 phase (mục 8), mỗi phase có tiêu chí đo được.
   Ước lượng trung thực: đây là dự án nhiều tuần–nhiều tháng cho 1 người, không phải vài ngày.
   Nhưng khác với mục tiêu "mọi APK", riêng Sorayori **khả thi cao** vì bề mặt phụ thuộc nhỏ bất thường.

---

## 1. Mục tiêu

### 1.1 Mục tiêu cuối (north star)

Chạy được **mọi loại APK mà phần cứng iPhone/iPad xử lý nổi**, theo nghĩa:

- Nhận file `.apk` (và về sau: `.xapk`/`.apks` split) làm đầu vào duy nhất.
- Không cần source gốc của app, không cần build lại app.
- App chạy trong tiến trình của KuDroid trên iOS, dùng CPU/GPU/mạng/âm thanh thật của máy,
  không phải stream từ máy khác, không phải giả lập toàn hệ thống (full-system emulation)
  ở chế độ mặc định.

### 1.2 Định nghĩa "chạy được" theo tầng (để khỏi cãi nhau về sau)

| Tầng | Tên | Tiêu chí |
|---|---|---|
| T0 | Load được | Parse APK, load dex + `.so`, resolve symbol, không crash ở loader |
| T1 | Boot được | `Application.onCreate` + Activity chính `onCreate/onResume` chạy xong |
| T2 | Vẽ được | Frame đầu tiên lên màn hình, đúng kích thước, có tương tác chạm |
| T3 | Dùng được | Luồng chức năng chính của app hoạt động (với Sorayori: WebView render `index.html`, IPC 2 chiều, geolocation trả tọa độ thật) |
| T4 | Sống được | Chạy ≥30 phút không crash, chịu được background/foreground, xoay máy, mạng chập chờn |
| T5 | Ngang bản Android | Đủ API surface để app không rơi vào nhánh "unsupported" nào trong phiên dùng bình thường |

Mục tiêu tạm thời của nhánh này: **Sorayori đạt T3, hướng tới T4.**

### 1.3 Ngoài phạm vi (nói thẳng từ đầu)

- **App Store.** Bất kỳ đường đi nào cần JIT/dynamic-codesigning đều không qua được App Review
  ở dạng hiện tại. Kênh phân phối thực tế: sideload (AltStore/SideStore) + JIT enabler,
  TrollStore trên máy/iOS hỗ trợ, hoặc jailbreak. (UTM SE lên được App Store chỉ vì nó
  *không* JIT — xem mục 6.4.)
- **Google Play Services nguyên bản** (GMS core là app hệ thống Android có chữ ký Google,
  giao tiếp Binder đa tiến trình). Chỉ có thể *thay thế từng API* (như microG làm trên Android),
  không "chạy" được.
- **DRM Widevine L1 / SafetyNet / Play Integrity.** Không có đường hợp lệ trên iOS. App nào
  chặn cứng bằng mấy thứ này thì xếp vào nhóm "không chạy được" (mục 7).
- **APK 32-bit (armeabi-v7a/x86) trên máy ARM64**: iOS không có chế độ chạy 32-bit kể từ iOS 11.
  `.so` 32-bit chỉ chạy qua CPU emulation. May là Sorayori có bản arm64 (xem mục 3).

---

## 2. Hiện trạng KuDroid (điểm xuất phát, đo từ `master`)

Những thứ **đã có** trong repo — plan này xây lên trên chúng, không làm lại:

| Thành phần | File/thư mục | Trạng thái suy từ source |
|---|---|---|
| KuART — DEX interpreter + JIT ARM64 | `src/kuart/` (Interpreter, JitCompiler, DexClassLinker, DexJniEnv, LibCore, LibNet, LibTls…) | Đang hoạt động, có test suite lớn ở `kuart-tests/` |
| Parser DEX lấy từ ART | `third_party/art_dex/` | Đã vendor |
| ELF loader cho `.so` Android | `src/elf_loader.cpp` (1.775 dòng), `src/elf_x18.cpp` | Đang hoạt động cho game native |
| Bionic shim / syscall shim | `src/BionicShim.cpp`, `src/abi/SyscallShim.cpp`, `src/abi/SocketAbi.cpp` | Có nền; độ phủ symbol cho ca game, chưa chắc đủ cho ca Rust (mục 5) |
| Bộ nhớ thực thi (RWX/JIT pages) | `src/ExecMemory.cpp` | Có |
| Graphics: ANGLE GLES3→Metal, MoltenVK | `cmake/`, `third_party/downloads/MoltenVK-ios-*.tar`, `src/platform/GraphicsShim.cpp` | Đang hoàn thiện (xem `docs/vulkan-compatibility-fix-plan.md`) |
| Audio OpenSL ES/AAudio→CoreAudio | `src/platform/AudioShim.cpp` | Có |
| Looper native | `src/platform/NativeLooper.cpp` | Có (liên quan trực tiếp tới `ALooper_*` ở mục 5) |
| Framework Java (AOSP adapted) | `framework/android/**` | Phủ rộng cho view/app/os; **webkit gần như trống** (mục 4.4) |
| Shell iOS | `ios-app/KuDroidShell/` (Swift: ContentView, bridge camera/media/network/sensor) | Có |
| Entitlements | `ios-app/entitlements.plist`: `get-task-allow`, `com.apple.security.cs.allow-jit`, `allow-unsigned-executable-memory`, `allow-dyld-environment-variables` | Đã khai đúng bộ tối thiểu cho JIT sideload |
| CI | `.github/workflows/build.yml` (Linux build KuART + test; iOS build) | Có |
| JIT trên iOS 26/27 | README nhắc gắn script `universal.js` trong StikDebug | Ghi nhận là biến số cần theo dõi từng bản iOS |

Bài học đã trả bằng log (từ `docs/root-cause-freeze.md`): app game treo không phải do render,
mà do **thiếu đúng 1 overload** `Activity.setContentView(View, ViewGroup.LayoutParams)` bị
auto-stub lặng lẽ. → Nguyên tắc cho nhánh này: **mọi auto-stub phải log thành tiếng và được
đếm**; danh sách stub chạm tới trong 1 phiên chạy là backlog ưu tiên số 1 (mục 8, Phase 0).

---

## 3. Mổ Sorayori — số liệu đo trực tiếp

Nguồn: `Sorayori-release.apk`, 28 MB (30.336.233 byte khi giải nén, 971 entry).

### 3.1 Nhận dạng

| Thuộc tính | Giá trị |
|---|---|
| Package | `studio.sakayori.sorayori` |
| Version | `0.1.0` (versionCode 1000) |
| Framework | **Tauri v2** (`assets/tauri.conf.json`, `$schema: schema.tauri.app/config/2`) |
| minSdk / targetSdk / compileSdk | 26 / 36 / 36 |
| ABI có sẵn | `arm64-v8a` ✅, `armeabi-v7a` |
| Permission | `INTERNET`, `ACCESS_COARSE_LOCATION`, `ACCESS_FINE_LOCATION` |
| Cửa sổ khai trong config | 390×844 (kích thước iPhone), `url: index.html` |
| CSP | `default-src 'self'; connect-src ipc: http://ipc.localhost; img-src 'self' asset: http://asset.localhost data:` |
| Plugin | `tauri_plugin_geolocation` (thấy trong `.so` + dex) |
| Frontend | **Nhúng trong `.so`** (Tauri embed `frontendDist` vào binary; trong APK không có thư mục assets web rời) |

### 3.2 Dex (phần "Java")

- `classes.dex` duy nhất, 2.137.644 byte, **3.119 type được tham chiếu, 2.160 class được định nghĩa**.
- Phân bố type theo package (top): `com/fasterxml/*` 260 (Jackson JSON — do Tauri plugin system dùng),
  `android/view` 120, `java/util` 110, `java/lang` 96, `com/google/*` 90 (Play Services location + Gson…),
  `android/graphics` 72, `app/tauri` 60, `org/chromium` 51 (suy luận: thành phần WebView/Chromium glue
  của AndroidX webkit), `android/content` 51, `androidx/lifecycle` 51, `android/widget` 50,
  `studio/sakayori` 36, `android/webkit` 22.
- Class lõi do Tauri sinh (định nghĩa ngay trong dex, tức KuART sẽ chạy chúng như code của app):
  `studio.sakayori.sorayori.MainActivity`, `TauriActivity`, `WryActivity`, `RustWebView`,
  `RustWebViewClient`, `RustWebChromeClient`, `Ipc`, `Rust` (class chứa native method),
  `app.tauri.plugin.PluginManager`, `app.tauri.plugin.Plugin`, `app.tauri.geolocation.GeolocationPlugin`…

Hệ quả quan trọng: **không cần viết lại vỏ Tauri.** Vỏ đã nằm trong APK dưới dạng dex —
KuART chỉ cần chạy nó, với điều kiện framework (`android.*`, `androidx.*`) và WebView đủ thật.

### 3.3 Native `.so` (phần Rust thật)

`lib/arm64-v8a/libsorayori_lib.so`: ELF64 AArch64, stripped section headers, 15.093.712 byte.

`DT_NEEDED` — chỉ 5 thư viện:

```
libandroid.so   libdl.so   liblog.so   libm.so   libc.so   (bionic)
```

Không có `libEGL`, `libGLESv3`, `libvulkan`, `libOpenSLES`, `libmediandk`, `libbinder`…
→ `.so` này **không tự vẽ, không tự phát tiếng**; nó nhờ Java WebView vẽ hộ qua wry.
Đây là khác biệt lớn nhất so với ca game (FMOD+Vulkan) mà KuDroid đang xử lý.

Chuỗi trong binary xác nhận stack: `tauri`, `tauri_runtime_wry`, `wry`, `tao`,
`tauri_plugin_geolocation`, `TAURI_INTERNALS__`, scheme `ipc.localhost` / `asset.localhost`.

### 3.4 Bề mặt import: đúng 98 undefined symbol

Nhóm theo nơi phải xử lý (chi tiết từng symbol ở Phụ lục A):

| Nhóm | Số lượng xấp xỉ | Đích xử lý |
|---|---|---|
| POSIX libc chuẩn, Darwin có tương đương trực tiếp | ~55 | BionicShim map sang libSystem (chú ý khác biệt ABI chi tiết: `stat` struct, `lseek64`, `strerror_r` bản GNU vs XSI…) |
| pthread | ~15 | Darwin pthread — gần tương thích, cần kiểm tra `pthread_cancel`/attr |
| Linux-only: `epoll_create/ctl/wait`, `eventfd` | 4 | **Phải emulate bằng kqueue** (mio của Rust gọi epoll; trên Darwin mio gốc dùng kqueue nhưng binary này đã biên cho Android nên nó *sẽ* gọi epoll) |
| Bionic internals: `__errno`, `__sF`, `__cxa_finalize`, `__register_atfork` (tên bị cắt trong readelf, cần dump đầy đủ), `__android_log_write` | ~5 | Tự cung cấp trong shim; `__sF` là mảng FILE của bionic — phải giả lập cẩn thận hoặc vá PLT sang hàm bọc |
| `libandroid`: `ALooper_*` (8), `AConfiguration_*` (2) | 10 | `NativeLooper.cpp` đã có nền cho ALooper; AConfiguration trả locale/orientation/density từ iOS |
| Nguy hiểm/cấm trên iOS: `fork`, `execvp`, `chroot`, `setuid/setgid/setgroups`, `prctl`, `syscall` (raw) | ~8 | Stub có kiểm soát: trả lỗi "sạch" (EPERM/ENOSYS) để Rust std rơi vào nhánh fallback thay vì crash; `syscall` raw phải audit từng số hiệu thực tế được gọi lúc chạy (telemetry đã có: `NativeCallTelemetry.cpp`) |
| Toán/libm + cấp phát | còn lại | Map thẳng |

98 symbol là con số **rất nhỏ**. Để so sánh (suy luận từ kinh nghiệm các ca tương tự):
một game Unity IL2CPP thường import vài trăm symbol cộng thêm EGL/Vulkan/AAudio/OpenSL.
Sorayori là ca native "sạch" nhất có thể gặp.

### 3.5 JNI — bề mặt cầu nối hữu hạn

`.so` export đúng **26 hàm `Java_*`** (Phụ lục B), trong đó:

- 24 hàm thuộc `studio.sakayori.sorayori.Rust`: toàn bộ vòng đời Activity
  (`onCreate/onResume/onPause/onStart/onStop/onDestroy/onLowMemory/onNewIntent/…`),
  vòng đời WebView (`onPageLoaded/onPageLoading/onEval/onWebviewDestroy/shouldOverride`),
  và kênh dữ liệu (`ipc`, `handleRequest`, `assetLoaderDomain`, `handleReceivedTitle`).
- 2 hàm thuộc `app.tauri.plugin.PluginManager`: `handlePluginResponse`, `sendChannelData`.

Chiều ngược lại (Rust gọi vào Java qua JNI `CallObjectMethod`…): wry/tao sẽ yêu cầu tạo
`WebView`, gắn `WebViewClient/WebChromeClient`, `loadUrl`, `evaluateJavascript`,
`postWebMessage`… Danh sách chính xác lấy bằng cách chạy thử + telemetry (Phase 1),
không đoán trước.

### 3.6 Luồng chạy dự kiến của Sorayori khi lên KuDroid

```
KuDroidShell (Swift) chọn Sorayori-release.apk
  → APKExtractor bóc classes.dex + lib/arm64-v8a/libsorayori_lib.so
  → elf_loader nạp .so, BionicShim/SyscallShim nối 98 symbol
  → KuART nạp dex, chạy app_process mô phỏng: Application → MainActivity (TauriActivity/WryActivity)
  → Java vỏ gọi JNI Rust.onCreate → tao tạo "cửa sổ" logic
  → wry yêu cầu 1 android.webkit.WebView thật
  → WebView (framework, mới làm) bắc cầu sang WKWebView (Swift/ObjC bridge)
  → Rust trả HTML/JS đã nhúng trong .so qua assetLoaderDomain/handleRequest (scheme asset.localhost)
  → WebView load index.html; JS gọi IPC → Ipc → Rust.ipc (ipc.localhost)
  → Lệnh geolocation: JS → plugin framework → GeolocationPlugin (dex)
      → LocationManager / Play Services (phải shim, mục 4.5) → CoreLocation của iOS
```

Mỗi mũi tên là một điểm có thể gãy — plan chia phase theo đúng các mũi tên này.

---

## 4. Bốn lớp bài toán của riêng Sorayori

### 4.1 Lớp ELF/Rust: nạp `libsorayori_lib.so`

Việc phải làm:

1. **Dump symbol đầy đủ, không bị cắt.** `readelf` trên binary stripped cho tên bị rút gọn
   (`AConfiguration_g[...]`, `pthread[...]`). Phase 0 phải trích toàn bộ `.dynstr` để có
   danh sách 98 symbol nguyên vẹn + danh sách export, lưu thành file máy đọc được
   (`tools/` đã có tiền lệ: `elf_func_lookup.py`).
2. **Đối chiếu 98 symbol với bảng symbol BionicShim/SyscallShim hiện có** → ra 3 giỏ:
   (a) đã có, (b) chưa có nhưng map thẳng được, (c) phải emulate/stub (mục 5).
3. **Kiểm tra đặc tính ELF**: `BIND_NOW` + RELA count 20.664 — loader phải xử lý toàn bộ
   relocation ngay khi nạp (không lazy). Đã nằm trong khả năng `elf_loader.cpp` (ca game nặng hơn),
   nhưng phải test với chính file này vì Rust dùng nhiều `R_AARCH64_RELATIVE`.
4. **TLS + `__cxa_atexit`/`__cxa_finalize` kiểu bionic**: Rust/C++ static trong `.so` đăng ký
   destructor qua cơ chế bionic; Darwin khác. Loader hiện tại đã sống sót qua ca game nên
   kỳ vọng tái dùng được, vẫn phải có test riêng (`kuart-tests` thêm ca nạp đúng file `.so` này
   ở chế độ headless trên macOS/Linux CI để bắt lỗi sớm, không cần máy iOS).
5. **x18**: Apple Silicon trên Darwin dành x18 (platform register); Android không.
   Repo đã có `src/elf_x18.cpp` riêng cho chuyện này — kiểm tra nó áp dụng cho code Rust
   (Rust/LLVM thường không dùng x18 làm scratch trên Android target, nhưng thư viện C trong
   `.so` thì có thể).

### 4.2 Lớp KuART/dex: chạy vỏ Tauri

- Vỏ Tauri là Kotlin/Java biên sẵn; KuART interpreter chạy được ngay về nguyên tắc,
  JIT của KuART tăng tốc sau. Điểm cần kiểm chứng: **coroutine Kotlin** (dex có
  `kotlinx_coroutines_android`), `invokedynamic`/method handles (đã có test
  `test_kuart_method_handles.cpp` ✅), reflection qua Jackson (260 type `com/fasterxml` —
  Jackson dùng reflection rất nặng; `DexReflect.cpp` phải chịu được).
- **R class & resources**: Tauri/WryActivity tra cứu layout/id qua `R.*` và `Resources`.
  `ResourceTable.cpp`/`ResourceCompat.cpp` đã có cho ca game; Sorayori dùng ít layout
  (chủ yếu 1 WebView toàn màn hình) nên rủi ro thấp hơn ca game nhiều.
- **AppCompat/Material**: dex tham chiếu `androidx.appcompat` 18 type, material —
  các class này nằm trong dex của app (app tự mang theo), KuART chạy chúng trên framework;
  thiếu method framework nào sẽ lộ qua auto-stub log (đây là lý do Phase 0 tồn tại).

### 4.3 Lớp WebView: bài toán trung tâm (việc lớn nhất của nhánh này)

Hiện trạng: `framework/android/webkit/` có 22 file nhưng `WebView.java` chỉ 34 dòng —
tức là **chưa có WebView thật**. Với Sorayori, WebView *là* màn hình, là UI, là tất cả.

Thiết kế đề xuất: implement `android.webkit.WebView` trong framework Java, mọi thao tác thật
uỷ thác qua JNI bridge xuống Swift/ObjC (`WKWebView`). Ánh xạ API tối thiểu cho Tauri/wry:

| Android WebView API (wry/Tauri dùng) | WKWebView tương ứng | Ghi chú lệch |
|---|---|---|
| `loadUrl(url)` / `loadDataWithBaseURL` | `load(URLRequest)` / `loadHTMLString(_:baseURL:)` | Scheme lạ (`asset.localhost`, `ipc.localhost`) phải chặn ở `WKURLSchemeHandler` hoặc navigation delegate |
| `WebViewClient.shouldOverrideUrlLoading` | `WKNavigationDelegate.decidePolicyFor` | Rust có native `Rust_shouldOverride` — vòng gọi: iOS quyết định → báo về Java → hỏi Rust |
| `WebViewClient.onPageStarted/onPageFinished` | `didStartProvisionalNavigation` / `didFinish` | Map sang `Rust_onPageLoading/onPageLoaded` |
| `evaluateJavascript(script, callback)` | `evaluateJavaScript` | Callback bất đồng bộ; phải giữ đúng thread (main looper) |
| `addJavascriptInterface(obj, name)` | `WKScriptMessageHandler` + `WKUserContentController` | **Lệch kiến trúc lớn nhất**: Android tiêm object Java callable từ JS; iOS chỉ có message 1 chiều + `evaluateJavaScript` chiều về. Tauri IPC thực ra đi qua `http://ipc.localhost` (theo CSP) và `Rust.handleRequest`, nên có thể **không cần** addJavascriptInterface cho luồng chính — phải xác minh bằng telemetry, nếu đúng thì nhẹ đi một nửa |
| `WebChromeClient.onGeolocationPermissionsShowPrompt` | `WKUIDelegate` (iOS tự hỏi quyền vị trí theo origin rất khác) | Dex có sẵn `RustWebChromeClient$onGeolocationPermissionsShowPrompt$1` — luồng này phải nối sang CoreLocation permission |
| `WebChromeClient.onShowFileChooser` | `UIDocumentPickerViewController` / `<input type=file>` trên WKWebView | Có sẵn callback trong dex (`onShowFileChooser$1`, showImageCapturePicker…) — làm ở Phase 5, không chặn T3 |
| Cookie (`CookieManager`) | `WKWebsiteDataStore.httpCookieStore` | Cần cho app đăng nhập; Sorayori hiện chưa rõ có cần — để Phase 5 kiểm chứng |
| `WebSettings` (JS enable, dom storage, UA…) | `WKWebViewConfiguration` | Phần lớn map 1-1; UA phải giả dạng Android WebView để frontend không vỡ layout? — câu hỏi mở, mặc định giữ UA iOS, chỉ đổi nếu frontend lỗi |

Nguyên tắc thread: WKWebView **chỉ** được chạm từ main thread iOS; KuART main looper của
guest phải được đồng nhất với main runloop của host (repo đã có tiền lệ ở ca game với
Surface/Choreographer — tái dùng cơ chế đó, đừng invent cơ chế thứ hai).

### 4.4 Lớp plugin & dịch vụ Android

1. **Geolocation plugin** (`app.tauri.geolocation.GeolocationPlugin` trong dex):
   Trên Android nó lấy vị trí qua `LocationManager` và/hoặc Fused Location Provider
   (Play Services — dex tham chiếu `com/google/*` 90 type, có `play-services-location.properties`).
   Trên KuDroid **không có Play Services**, nên:
   - Kiểm tra plugin có đường fallback `LocationManager` thuần không (đọc dex/strings, Phase 4).
   - Implement `android.location.LocationManager` trong framework bắc cầu **CoreLocation**
     (shell đã có `SensorBridge.swift` làm tiền lệ cho cảm biến).
   - Nếu plugin gọi chết vào Fused API: viết shim `com.google.android.gms.location.*`
     ở mức Java (class giả trong framework classpath) dịch sang LocationManager của mình.
     Đây là mẫu hình chung cho mọi plugin Play Services về sau (mục 7).
   - Quyền: Android runtime permission (`ACCESS_FINE_LOCATION`) phải map sang
     `CLLocationManager.requestWhenInUseAuthorization`; hộp thoại iOS hiện 1 lần cho cả KuDroid
     (giới hạn nền tảng: quyền là của host app, không phải của từng guest APK — ghi vào mục 6.5).
2. **Path/App plugin mặc định của Tauri** (`PathPlugin`, `AppPlugin` trong dex): đường dẫn
   Android (`getFilesDir`, `getCacheDir`, external storage…) map qua `VFSPathRemapper.cpp`
   (3.569 dòng — thành phần này đã rất dày) vào sandbox của KuDroid, mỗi APK một thư mục riêng.
3. **Mạng**: `.so` Rust tự mở socket (98 symbol có đủ `socket/connect/send/recv/getaddrinfo` +
   TLS do Rust lo — `LibTls` của KuART là cho phía Java). Mạng phía Java (`java.net`, lỗi
   `URL$1 not HttpsURLConnection` từng gặp ở ca game) là backlog chung của framework, Sorayori
   có thể chạm qua Jackson/plugin — theo dõi qua log, không chặn T3 nếu IPC nội bộ chạy.

### 4.5 Những thứ Sorayori *không* cần (may mắn phải ghi ra)

Không SurfaceView/game loop, không Choreographer-frame-callback cho render (WKWebView tự render),
không OpenGL/Vulkan/Metal trực tiếp, không AAudio/OpenSL, không MediaCodec, không Binder IPC,
không đa Activity phức tạp (1 cửa sổ `main`), không split APK. Mỗi mục "không cần" ở đây là
một hệ thống của KuDroid bản đầy đủ được phép để sau.

---

## 5. Chiến lược theo nhóm symbol (chi tiết mục 3.4)

### 5.1 `epoll`/`eventfd` → kqueue (việc khó nhất trong 98 symbol)

- Rust `mio`/`tokio` trên target Android dùng epoll + eventfd làm cơ chế thức giấc (waker).
  Darwin không có cả hai. Không có đường vòng: phải emulate.
- Tiền lệ: **libepoll-shim** (đã chạy trên FreeBSD/macOS cho nhiều dự án) — port/đối chiếu
  semantics: `EPOLLET` (edge-triggered) trên kqueue là `EV_CLEAR`; `EPOLLONESHOT` là `EV_ONESHOT`;
  eventfd là pipe nội bộ có bộ đếm kiểu semaphore.
- Đặt shim này trong `src/abi/` cạnh `SyscallShim.cpp` (nơi đã nhắc tới epoll), không rải vào BionicShim.
- Test bắt buộc: vòng mio tối thiểu (1 listener + 1 waker) chạy trong `kuart-tests` trước khi
  nạp cả `.so`, vì lỗi epoll-shim biểu hiện thành treo ngẫu nhiên rất khó debug về sau.

### 5.2 Bionic internals

- `__errno`: bionic trả con trỏ tới errno TLS qua hàm `__errno()`; Darwin dùng `__error()`.
  Cung cấp hàm tương đương trong shim trỏ vào errno của thread guest (chú ý thread do Rust tạo
  qua pthread — errno phải là TLS thật của Darwin, không phải biến toàn cục).
- `__sF`: mảng 3 `FILE` chuẩn của bionic (stdin/stdout/stderr). Struct `FILE` của bionic
  **khác layout** Darwin. Nếu `.so` chỉ lấy địa chỉ để truyền qua `fprintf/fwrite` của chính
  bionic-thì-phải-cung-cấp… nhưng KuDroid không nạp bionic thật; các hàm stdio được map sang
  Darwin. Hướng an toàn: cấp vùng nhớ giả có kích thước đúng bionic `FILE`, và vá các hàm
  stdio trong shim để nhận diện 3 địa chỉ đặc biệt này rồi dịch sang `__stdinp/__stdoutp/__stderrp`.
  (Suy luận: khả năng cao `__sF` đến từ code C/C++ tĩnh trong `.so` — Rust thuần không dùng `__sF`.)
- `__cxa_finalize`/`__cxa_atexit`: đăng ký destructor — loader tự quản lý danh sách, chạy khi unload.
- `__android_log_write`: nối vào log hệ thống của KuDroid (đã có hạ tầng log/breadcrumb) + `os_log`.

### 5.3 Nhóm bị cấm trên iOS

`fork/execvp/chroot/setuid/setgid/setgroups/setsid/prctl/syscall`:

- iOS sandbox: tiến trình con mới gần như không tồn tại (không spawn được binary khác;
  `fork` thuần không-exec bị hạn chế mạnh và không được dựa vào).
- Rust std/program chỉ *link* các symbol này vì chúng nằm trong `std::process`/`std::os` —
  phần lớn app không gọi thật lúc chạy. Chiến lược: **stub trả -1/EPERM kèm log telemetry 1 lần**,
  để nếu app thật sự gọi, ta biết ngay từ log thay vì crash khó hiểu.
- `syscall()` raw: bọc lại, ghi số hiệu + đối chiếu bảng cho phép (getrandom, futex…);
  số hiệu lạ → ENOSYS + log. Đây cũng là van an toàn bảo mật cho mục tiêu "mọi APK".

### 5.4 `libandroid` còn lại

- `AConfiguration_*`: Tauri/tao hỏi orientation/locale/density. Trả từ `UIScreen`/`NSLocale`
  qua bridge; density Android (dpi bucket) map từ scale factor iOS (1x/2x/3x → 160/320/480).
- `ALooper_*`: `NativeLooper.cpp` đã implement cho ca game; kiểm tra đủ 8 hàm + hành vi
  `ALooper_pollAll/pollOnce` với timeout -1 (block vô hạn) dưới runloop iOS.

---

## 6. Nền tảng iOS: ràng buộc pháp lý-kỹ thuật & các tiền lệ phải đọc

Phần này là kết quả research tài liệu Apple developer, tài liệu mã nguồn mở của Apple
(XNU/AMFI/dyld được Apple công bố tại opensource.apple.com — "leak" thực tế đáng tin nhất
chính là source chính thức này), AOSP, và các dự án đi trước.

### 6.1 Code signing & JIT — bức tường trung tâm

- iOS yêu cầu mọi trang nhớ thực thi phải thuộc code đã ký (AMFI kiểm tra `CS_VALID`).
  Ngoại lệ duy nhất cho app thường: tiến trình có entitlement `dynamic-codesigning` /
  `com.apple.security.cs.allow-jit` (Apple giữ cho Safari/WebKit và cấp rất chọn lọc),
  **hoặc** tiến trình đang bị debugger attach (`get-task-allow` + debugserver) thì kernel
  nới kiểm tra cho phép `MAP_JIT`/trang thực thi không ký — đây là cơ chế StikDebug/
  JitStreamer/AltStore dùng: attach debugger → app được bật JIT → debugger rời đi, JIT còn
  hiệu lực tới khi app chết.
- KuDroid đã khai sẵn trong `entitlements.plist`: `get-task-allow`,
  `com.apple.security.cs.allow-jit`, `allow-unsigned-executable-memory`. Bộ này chỉ có tác dụng
  khi **ký bằng chứng chỉ dev/sideload**; bản App Store sẽ bị tước/bị từ chối.
- Hệ quả kiến trúc: KuDroid có 2 chế độ chạy phải thiết kế song song từ đầu:
  - **Chế độ JIT** (mặc định): KuART-JIT cho dex + ELF loader chạy `.so` native tốc độ thật.
  - **Chế độ không-JIT** (dự phòng): KuART interpreter cho dex thuần; `.so` **không chạy được**
    trừ khi dùng CPU threaded-interpreter (mục 6.4). UI phải nói rõ với người dùng APK nào
    sẽ không chạy ở chế độ này, thay vì crash.

### 6.2 Tiền lệ 1 — PojavLauncher iOS (Java trên iOS, đã chạy thật nhiều năm)

- Nhúng **OpenJDK (mobile port)** vào app iOS, chạy Minecraft Java nguyên bản; JIT bật qua
  AltStore/TrollStore/JitStreamer/StikDebug; bản TrollStore thì mọi caveat JIT biến mất.
- Bài học cho KuDroid: (1) cộng đồng chấp nhận nghi thức "bật JIT trước mỗi phiên" —
  UX của KuDroid nên tích hợp sẵn luồng StikDebug/JitStreamer thay vì bắt người dùng tự mò;
  (2) giới hạn RAM tiến trình (jetsam) là kẻ giết app thật sự, không phải CPU — cần theo dõi
  footprint như watchdog hiện tại của repo đang làm; (3) OpenJDK-mobile chứng minh một runtime
  Java-classfile đầy đủ sống được trong sandbox iOS — là phương án B cho tầng Java nếu một ngày
  KuART cần chạy `.jar`/class thay vì dex (ngoài phạm vi nhánh này).

### 6.3 Tiền lệ 2 — LiveContainer (chạy app khách trong 1 host đã ký)

- LiveContainer (của Duy Tran Khanh) chạy IPA chưa cài đặt bên trong 1 app host: vá header
  Mach-O của guest (`MH_EXECUTE`→`MH_DYLIB`, sửa `__PAGEZERO`), hook dyld/`NSBundle.mainBundle`,
  `dlopen` executable của guest rồi nhảy vào entry point; có JIT thì bỏ qua codesign hoàn toàn,
  không JIT thì dùng ZSign ký lại bằng cert của host (JIT-less mode).
- Bài học: (1) mô hình "1 host ký sẵn + N guest" là đúng đắn và KuDroid đang đi đúng mô hình đó;
  (2) giới hạn phải chấp nhận giống hệt: entitlement của guest không được áp, quyền (permission)
  là của host, guest không có sandbox riêng với nhau, app extension/push từ xa không hoạt động —
  ghi các giới hạn này vào tài liệu người dùng của KuDroid ngay từ đầu;
  (3) khác biệt căn bản: guest của LiveContainer là Mach-O iOS; guest của KuDroid là dex+ELF
  Android nên không dùng được đường "ký lại" (ELF không có chữ ký Mach-O để ký) — KuDroid
  **phụ thuộc JIT hơn** LiveContainer. Đây là rủi ro nền tảng số 1 (mục 9).

### 6.4 Tiền lệ 3 — UTM SE / iSH (đường không-JIT duy nhất đã chứng minh)

- UTM SE được App Store duyệt (2024, sau khi bị từ chối rồi được duyệt lại) nhờ dùng
  **QEMU TCG ở chế độ threaded interpreter** (TCTI) thay vì JIT — chậm ("SE = Slow Edition")
  nhưng hợp lệ hoàn toàn. iSH dùng kỹ thuật tương tự cho user-mode Linux x86.
- Ý nghĩa với KuDroid: đây là **Plan C** cho tầng native khi không có JIT — nhúng một
  threaded-interpreter ARM64 (QEMU user-mode hoặc tự viết tối giản) để *diễn giải* mã máy
  trong `.so`. Giá phải trả: chậm 10–50× (suy luận theo các benchmark TCG-interpreter công khai),
  và phải mô phỏng cả MMU phần mềm nếu code chạm địa chỉ lạ. Chỉ đáng làm sau khi đường JIT
  hoàn thiện, và chỉ cho `.so` nhẹ (như của Sorayori — Rust logic + WebView vẽ hộ thì phần native
  chậm đi vẫn có thể chấp nhận được; game thì không).

### 6.5 Các ràng buộc nền tảng khác phải thiết kế quanh

- **Quyền riêng tư (permission) là của host**: vị trí/mic/camera/photos — iOS cấp cho KuDroid,
  không cấp cho từng APK. Phải tự xây lớp "quyền ảo" trong KuDroid (hỏi người dùng theo từng APK,
  ghi nhớ lựa chọn, chặn ở shim) để 1 APK không lén dùng quyền APK khác đã xin.
- **Jetsam/RAM**: iOS giết tiến trình theo footprint (mức cụ thể thay đổi theo máy, vài trăm MB
  tới vài GB ở máy mới). Watchdog hiện có của repo (đang log ~296 MB cho ca game) là đúng hướng;
  cần thêm chiến lược giảm áp (giải phóng cache dex, nén asset) cho máy 3–4 GB RAM.
- **Không có `fork/exec`**: mọi APK giả định đa tiến trình (`:remote` process, WebView process
  tách riêng như Chromium trên Android) đều phải gộp về 1 tiến trình. Sorayori không dính;
  app dùng `android:process` sẽ cần phân tích riêng.
- **Background**: iOS treo app nền rất nhanh; Android Service/ForegroundService không có tương
  đương 1-1. Ngoài phạm vi Sorayori; ghi nhận cho mục tiêu tổng.
- **Keychain/sandbox file**: mỗi guest 1 thư mục dữ liệu riêng dưới sandbox của KuDroid
  (`VFSPathRemapper` đảm nhiệm); Keystore của Android → Keychain iOS chỉ cho host identity.

### 6.6 Nguồn tài liệu research (đọc theo thứ tự này)

1. Apple Developer Documentation: `WKWebView`/`WKURLSchemeHandler`, Core Location,
   entitlements (`com.apple.security.cs.*`), App Review Guidelines mục 2.5.2 (quy tắc về
   tải & thực thi code — lý do phân phối ngoài App Store).
2. Apple Open Source (opensource.apple.com): XNU — `amfi`, `cs_blobs`, Mach VM `MAP_JIT`;
   dyld — cách loader kiểm tra chữ ký trang. Đây là "tài liệu leak" hợp pháp và chính xác nhất.
3. AOSP source: `frameworks/base` (đối chiếu hành vi `WebView`, `LocationManager`, `Activity`),
   bionic `libc` headers (định nghĩa đúng `__sF`, `__errno`, ALooper/AConfiguration trong NDK).
4. Tauri v2 source (`tauri-apps/tauri`): `crates/tauri-runtime-wry`, `crates/tauri` mobile —
   đọc để biết chính xác wry gọi Java những gì, thay vì đoán từ strings.
5. Wry/Tao source: vòng đời `WryActivity`, cách tao dùng `ALooper` cho event loop.
6. StikDebug (github.com/StikDebug/StikDebug) + thư viện `idevice`: giao thức attach debugger
   bật JIT trên iOS 17.4+ (pairing file, loopback VPN, DDI mount) — và ghi chú iOS 26/TXM +
   script `universal.js` mà README repo đã nhắc.
7. LiveContainer (github.com/LiveContainer/LiveContainer) + bài "Restoring Dyld Memory Loading"
   của xpn: kỹ thuật nạp code khách trong host.
8. libhybris (github.com/libhybris/libhybris): 10+ năm kinh nghiệm nối bionic↔glibc —
   đọc phần xử lý `__sF`, TLS, `dlopen` hai thế giới; bài học của nó áp dụng nguyên vẹn cho
   bionic↔Darwin, chỉ khác đích.
9. libepoll-shim: semantics epoll trên kqueue (mục 5.1).
10. UTM SE / QEMU TCTI + iSH: cho Plan C không-JIT (mục 6.4).
11. PojavLauncher iOS + OpenJDK mobile (Gluon): vận hành JIT thực tế & giới hạn RAM.
12. `docs/` ngay trong repo này: `root-cause-freeze.md` (bài học auto-stub) và
    `vulkan-compatibility-fix-plan.md` (mẫu cấu trúc cho một plan kỹ thuật trong dự án).

---

## 7. Phân loại APK tổng quát — ma trận khả thi cho mục tiêu cuối

Sorayori chỉ là 1 ô trong bảng này. Xếp hạng dựa trên: có `.so` không, `.so` cần gì,
app cần dịch vụ Android nào. Đây là bản đồ để sau Sorayori biết đánh đâu tiếp.

| # | Loại APK | Ví dụ cấu trúc | Đường chạy trên KuDroid | Độ khó | Ghi chú |
|---|---|---|---|---|---|
| 1 | Thuần Java/Kotlin, API phổ thông | Chỉ dex, không `.so` | KuART interpreter/JIT + framework | ★☆☆ | Nhóm nên phủ đầu tiên sau Sorayori; không cần JIT cho `.so` (vẫn cần nếu KuART-JIT) |
| 2 | **Hybrid WebView (Tauri/Cordova/Capacitor/app WebView thuần)** | Dex mỏng + (Tauri: `.so` Rust) + web asset | **Nhánh này.** WebView→WKWebView + ELF nếu có | ★★☆ | Sorayori thuộc nhóm này với biến thể khó nhất của nhóm (có `.so` Rust) — làm xong là các biến thể nhẹ hơn tự mở |
| 3 | Flutter | `libflutter.so` + `libapp.so` (Dart AOT snapshot) | Nạp cả 2 `.so`; Dart snapshot là mã máy + định dạng riêng của Flutter engine | ★★★★ | Engine Flutter tự vẽ qua Skia/Impeller → cần cầu Surface/GLES thật; khả thi nhưng là dự án riêng |
| 4 | React Native | Hermes bytecode (`.so` Hermes/JSC) hoặc bridge cũ | Hermes là VM bytecode riêng — nạp `libhermes.so` hoặc diễn giải bytecode | ★★★ | Tương tự Flutter, nhẹ hơn về đồ hoạ (UI qua View Android thật) |
| 5 | Game native (Unity IL2CPP/Mono, Unreal, custom engine + FMOD…) | `.so` lớn + GLES/Vulkan + AAudio/OpenSL | Đường KuDroid đang làm (ANGLE/MoltenVK) | ★★★★ | Ca `com.min.n.*` hiện tại thuộc nhóm này |
| 6 | App phụ thuộc Play Services nặng (Maps, FCM, ML Kit…) | dex + GMS API | Shim từng API kiểu microG, ưu tiên theo nhu cầu thật | ★★★★★ | Không bao giờ "đủ 100%"; làm theo nhu cầu từng app |
| 7 | App đa tiến trình / Binder nặng / System service | `android:process`, AIDL khắp nơi | Gộp tiến trình + giả lập Binder nội bộ | ★★★★★ | Binder driver không tồn tại trên iOS; phải mô phỏng ở mức thư viện |
| 8 | App chống giả lập/chống root/Play Integrity cứng, DRM L1 | — | **Không chạy** | ✖ | Khai báo thua sớm, đừng đốt thời gian |

Thứ tự chinh phục đề xuất sau nhánh này: 1 → 2 (đang làm) → 5 (đang dở ở master) → 4 → 3 → 6/7 theo nhu cầu.

---

## 8. Lộ trình triển khai (7 phase)

Mỗi phase kết thúc bằng một **tiêu chí đo được trên máy thật hoặc CI**, không tính "cảm giác chạy được".
Người dùng test mục tiêu: iPhone chạy iOS 17.4+ có SideStore + StikDebug (đường JIT chuẩn hiện nay).

### Phase 0 — Đo đạc & dựng dàn giáo (không sửa hành vi)

- [ ] Trích xuất đầy đủ từ APK: danh sách symbol import/export nguyên vẹn (không bị `readelf` cắt),
      danh sách class/method dex tham chiếu vào `android/**` + `androidx/**`.
- [ ] Chạy `scripts/audit_framework_gaps.py` (mở rộng cho đầu vào dex nếu cần) với Sorayori →
      danh sách method framework còn thiếu/stub mà Sorayori *thật sự chạm tới*. Đây là backlog chính thức.
- [ ] Bật chế độ "stub kêu thành tiếng": mọi auto-stub bị gọi đều log kèm stack; cuối phiên in bảng tổng kết.
- [ ] Thêm fixture vào `tests/fixtures`: thông tin giải phẫu APK (không commit file APK 28 MB;
      commit bản trích xuất: manifest text, symbol list, class list — repo đã có tiền lệ `tools/elf_func_lookup.py`).
- [ ] Viết test nạp `.so` headless: `elf_loader` nạp `libsorayori_lib.so`, resolve đủ symbol hoặc
      in đúng danh sách symbol thiếu. Chạy trong CI Linux/macOS, chưa cần iOS.
- **Tiêu chí qua phase:** danh sách "symbol thiếu" và "method thiếu" đều là file cụ thể trong repo,
  đếm được, diff được giữa các lần chạy.

### Phase 1 — ELF/Rust sống dậy (T0 cho native)

- [ ] Xử lý giỏ symbol (c) ở mục 5: epoll/eventfd trên kqueue, bionic internals, stub an toàn nhóm bị cấm.
- [ ] Nạp `.so`, chạy `JNI_OnLoad`/init array, gọi thử 1 export vô hại qua test harness
      (ví dụ export chỉ log) với JNI env giả lập của KuART.
- [ ] Telemetry: ghi mọi `syscall()` raw + mọi lần chạm stub cấm, kèm số hiệu.
- **Tiêu chí:** `.so` nạp xong, init xong, không crash trong 60 giây idle dưới harness;
  bảng telemetry cho thấy không có syscall lạ chưa phân loại.

### Phase 2 — Vỏ Tauri boot (T1)

- [ ] KuART chạy dex Sorayori: `Application` → `MainActivity.onCreate` → `Rust_onCreate` (JNI thật).
- [ ] Tao/wry event loop chạy trên `ALooper` shim (kiểm tra `pollOnce(-1)` không treo cứng runloop host).
- [ ] VFS: đường dẫn Tauri `PathPlugin` trỏ đúng thư mục riêng của Sorayori.
- **Tiêu chí:** log có đủ chuỗi `onCreate → onStart → onResume` từ cả phía Java lẫn phía Rust;
  process sống, main looper phản hồi ping test.

### Phase 3 — WebView thật (T2) ⭐ phase nặng nhất

- [ ] Implement `android.webkit.WebView` + `WebViewClient` + `WebChromeClient` + `WebSettings`
      trong framework, bridge xuống `WKWebView` ở shell Swift (thêm `WebViewBridge.swift`
      cạnh các bridge sẵn có: Camera/Media/Network/Sensor).
- [ ] Đăng ký scheme handler cho `asset.localhost`/`ipc.localhost` nối vào
      `Rust_handleRequest`/`Rust_ipc`/`Rust_assetLoaderDomain`.
- [ ] Ánh xạ sự kiện trang: loading/loaded/title → `Rust_onPageLoading/onPageLoaded/handleReceivedTitle`;
      điều hướng ngoài → `Rust_shouldOverride`.
- [ ] `evaluateJavascript` 2 chiều đúng thread, đúng thứ tự so với vòng đời trang.
- **Tiêu chí (T2→T3 một phần):** màn hình iPhone hiện đúng giao diện `index.html` của Sorayori
  (so ảnh chụp với bản Android), chạm/scroll hoạt động, JS trong trang gọi IPC và nhận trả lời.

### Phase 4 — Geolocation + plugin framework (T3 đầy đủ)

- [ ] Xác định đường lấy vị trí plugin chọn khi không có Play Services (đọc dex, không đoán).
- [ ] `android.location.LocationManager` → CoreLocation bridge; hoặc shim GMS-location nếu plugin bắt buộc.
- [ ] Quyền ảo theo-APK cho vị trí (mục 6.5): lần đầu hỏi trong UI KuDroid, lưu lựa chọn.
- [ ] Các lệnh Tauri plugin còn lại mà frontend Sorayori gọi (liệt kê từ log IPC ở Phase 3) —
      implement đúng từng lệnh, lệnh lạ → trả lỗi có cấu trúc thay vì treo.
- **Tiêu chí (đạt T3):** trong Sorayori, tính năng vị trí trả về đúng tọa độ GPS thật của máy;
  toàn bộ luồng chức năng chính đi hết không gặp "unsupported".

### Phase 5 — Độ bền (T4)

- [ ] Background/foreground (`onPause/onResume` ↔ `UIApplication` lifecycle), khoá/mở màn hình,
      cuộc gọi đến, xoay máy (AConfiguration thay đổi), bàn phím iOS ↔ Android IME events.
- [ ] File chooser + camera capture từ WebChromeClient (dex đã có sẵn các callback này).
- [ ] Cookie/localStorage của WKWebView bền qua các phiên; dữ liệu app không mất khi KuDroid bị giết.
- [ ] Soak test 30 phút + test máy RAM thấp; theo dõi jetsam footprint bằng watchdog sẵn có.
- **Tiêu chí (đạt T4):** checklist trên pass hết trên máy thật, soak 30 phút không crash.

### Phase 6 — Đóng gói trải nghiệm JIT

- [ ] KuDroid tự phát hiện "chưa có JIT" khi khởi động → hướng dẫn/nút mở StikDebug hoặc
      JitStreamer ngay trong shell, thay vì để `.so` crash khó hiểu (bài học UX từ Pojav).
- [ ] Chế độ không-JIT: vẫn cho mở APK thuần-dex (interpreter), APK có `.so` thì báo rõ lý do.
- [ ] Kiểm chứng lại trên bản iOS mới nhất lúc đó (cơ chế JIT thay đổi theo bản iOS — iOS 26/TXM
      là ví dụ gần nhất) và cập nhật tài liệu này.
- **Tiêu chí:** người dùng mới, chỉ đọc hướng dẫn trong app, chạy được Sorayori trong ≤10 phút.

### Phase 7 (tuỳ chọn, nghiên cứu) — Plan C: native không-JIT

- [ ] Đánh giá QEMU user-mode TCTI hoặc threaded-interpreter ARM64 tối giản để diễn giải `.so`.
- [ ] Chỉ nhắm `.so` nhẹ kiểu Sorayori; đo benchmark trung thực trước khi hứa hẹn.
- **Tiêu chí:** có bản demo + con số hiệu năng; nếu chậm >50× cho ca Sorayori thì đóng hướng này,
  ghi kết luận vào docs như repo đã làm với `root-cause-freeze.md`.

---

## 9. Rủi ro & câu hỏi mở

| # | Rủi ro/câu hỏi | Mức | Cách giảm thiểu |
|---|---|---|---|
| R1 | Apple thay đổi cơ chế JIT/debugger ở bản iOS mới (đã xảy ra với iOS 26/TXM) làm đường JIT gãy tạm thời | Cao | Theo dõi StikDebug/idevice sát; giữ chế độ interpreter cho dex; không hứa "chạy mọi iOS" |
| R2 | `__sF`/stdio bionic layout khác Darwin gây lỗi bộ nhớ khó truy | Trung | Vá ở tầng hàm (mục 5.2), test stdio riêng trước khi nạp cả `.so` |
| R3 | Jackson reflection + Kotlin coroutine dùng tính năng KuART chưa phủ (method handle, invokedynamic lạ) | Trung | Phase 0 audit + log stub; bổ sung theo nhu cầu đo được, không làm trước |
| R4 | WKWebView khác Android WebView ở hành vi (UA, viewport, scheme, file access) làm frontend vỡ mà không lỗi log | Trung | Đối chiếu ảnh chụp 2 nền tảng ở Phase 3; có công tắc UA giả Android dự phòng |
| R5 | Plugin geolocation bắt chết Play Services, không fallback LocationManager | Trung | Đọc dex ở Phase 4 trước khi code; shim GMS-location mức Java là đường đã dự trù |
| R6 | Jetsam giết tiến trình khi `.so` Rust + WKWebView + KuART cộng dồn RAM | Trung | Watchdog footprint; giới hạn cache; test máy RAM thấp ở Phase 5 |
| R7 | Frontend Sorayori gọi thêm plugin Tauri khác (fs, dialog, shell…) ngoài geolocation | Thấp–Trung | Log IPC Phase 3 liệt kê đầy đủ; implement theo nhu cầu thật |
| R8 | Vấn đề pháp lý/phân phối: APK của bên thứ ba, tài sản trong APK | Thấp (với Sorayori: app của chính chủ dự án) | KuDroid chỉ là runtime; người dùng tự chịu trách nhiệm về APK họ nạp — ghi rõ trong README |

Câu hỏi cần trả lời bằng thực nghiệm, chưa trả lời trong plan này (cố ý):
1. `.so` có gọi `syscall()` raw với số hiệu nào lúc chạy thật? (Phase 1 telemetry)
2. Tauri IPC có dùng `addJavascriptInterface` không hay thuần `ipc.localhost`? (Phase 3 telemetry)
3. `AConfiguration` được hỏi những trường nào — có phụ thuộc density bucket để chọn asset không?
4. KuART JIT và ELF loader có tranh chấp vùng nhớ thực thi nào khi chạy đồng thời không?

---

## 10. Kiểm thử & CI

- Mở rộng `kuart-tests/` theo mẫu sẵn có: thêm `test_sorayori_elf_load.cpp` (Phase 0/1),
  `test_epoll_shim.cpp` (mục 5.1), `test_webkit_bridge.cpp` (Phase 3, mock phía Swift bằng stub C).
- CI Linux hiện tại (`.github/workflows/build.yml`, job `build-kuart`) chạy được các test thuần C++
  — mọi thứ ở Phase 0/1 phải xanh ở CI trước khi đụng máy iOS.
- Test máy thật: giữ 1 thiết bị "chuẩn" (ghi rõ model + bản iOS trong mỗi báo cáo lỗi),
  tái dùng bộ log sẵn có (`logs/stderr.log`, breadcrumb, `scripts/analyze_logs.sh`) như ca game.
- Quy tắc báo cáo (rút từ `root-cause-freeze.md`): kết luận phải tách "đo được" khỏi "suy luận",
  kèm mức độ chắc chắn. Plan này cũng tuân theo quy tắc đó.

## 11. Phạm vi nhánh `rust-beta`

- Nhánh thí nghiệm cho **họ APK lõi Rust/WebView**, mục tiêu đầu tiên là Sorayori.
- Chỉ thêm: tài liệu này, fixture giải phẫu APK, test mới, các shim/phần framework mà
  Phase 0 chứng minh là cần (webkit, location, epoll…).
- Không refactor kiến trúc đang dở ở `master` (ca game/FMOD/Vulkan); nếu phải sửa file dùng chung
  (BionicShim, SyscallShim, elf_loader), giữ thay đổi tối thiểu và ghi lý do vào commit.
- Tôn trọng quy định của repo: `master` không nhận PR trực tiếp — nhánh này tồn tại để
  thí nghiệm và báo cáo, việc nhập về `master` do chủ repo quyết định.

---

## Phụ lục A — 98 undefined symbol của `libsorayori_lib.so` (đo trực tiếp)

Trích từ `.dynsym` (tên dài bị `readelf` mặc định cắt — Phase 0 sẽ dump nguyên vẹn từ `.dynstr`):

```
AConfiguration_getCountry (suy luận hoàn thiện tên)  AConfiguration_new
ALooper_acquire  ALooper_addFd  ALooper_forThread  ALooper_pollAll
ALooper_pollOnce  ALooper_prepare  ALooper_release  ALooper_wake
__android_log_write  __cxa_atexit  __cxa_finalize (suy luận)  __errno
__register_atfork (suy luận)  __sF
_exit abort bind calloc chdir chroot clock_gettime (suy luận) clock_nanosleep (suy luận)
close connect dl_iterate_phdr (suy luận) dlsym dup2 environ
epoll_create (suy luận) epoll_ctl epoll_wait eventfd
execvp exit fcntl fflush fork fprintf free freeaddrinfo fstat fwrite
gai_strerror getaddrinfo getauxval getcwd getenv getpeername getrandom
getsockname getsockopt gettid lseek64 malloc memcmp memcpy memmove memset
mkdir mmap munmap open pipe pipe2 poll posix_madvise (suy luận) pow prctl
pthread_* (gia đình ~15 hàm: create/join/mutex/cond/rwlock/key/once…)
read readlink realloc realpath recv sched_getaffinity (suy luận) sched_yield
send setgid setgroups setpgid setsid setsockopt setuid shutdown sigaction
socket stat strerror_r strlen syscall sysconf waitpid write writev
```

`DT_NEEDED`: `libandroid.so`, `libdl.so`, `liblog.so`, `libm.so`, `libc.so` (bionic).

## Phụ lục B — 26 JNI export của `.so` (đo trực tiếp, đầy đủ)

```
Java_app_tauri_plugin_PluginManager_handlePluginResponse
Java_app_tauri_plugin_PluginManager_sendChannelData
Java_studio_sakayori_sorayori_RustWebChromeClient_onGeolocationPermissionRequestNative
Java_studio_sakayori_sorayori_RustWebChromeClient_onPermissionRequestNative
Java_studio_sakayori_sorayori_Rust_assetLoaderDomain
Java_studio_sakayori_sorayori_Rust_handleReceivedTitle
Java_studio_sakayori_sorayori_Rust_handleRequest
Java_studio_sakayori_sorayori_Rust_ipc
Java_studio_sakayori_sorayori_Rust_onCreate
Java_studio_sakayori_sorayori_Rust_onDestroy
Java_studio_sakayori_sorayori_Rust_onEval
Java_studio_sakayori_sorayori_Rust_onFirstActivityCreate
Java_studio_sakayori_sorayori_Rust_onFirstActivityCreateWry
Java_studio_sakayori_sorayori_Rust_onLowMemory
Java_studio_sakayori_sorayori_Rust_onNewIntent
Java_studio_sakayori_sorayori_Rust_onPageLoaded
Java_studio_sakayori_sorayori_Rust_onPageLoading
Java_studio_sakayori_sorayori_Rust_onPause
Java_studio_sakayori_sorayori_Rust_onResume
Java_studio_sakayori_sorayori_Rust_onStart
Java_studio_sakayori_sorayori_Rust_onStop
Java_studio_sakayori_sorayori_Rust_onWebviewDestroy
Java_studio_sakayori_sorayori_Rust_onWindowFocusChanged
Java_studio_sakayori_sorayori_Rust_shouldOverride
```

(24 hàm `Rust_*`/`RustWebChromeClient_*` + 2 hàm `PluginManager_*`.)

## Phụ lục C — Giải phẫu nhanh Sorayori (tóm tắt đo được)

- APK 28 MB; 971 entry; `classes.dex` 2,1 MB (3.119 type tham chiếu / 2.160 class định nghĩa);
  `lib/arm64-v8a/libsorayori_lib.so` 15,1 MB; `lib/armeabi-v7a/` 10,9 MB (không dùng trên iOS).
- Manifest: package `studio.sakayori.sorayori`, 0.1.0, minSdk 26, targetSdk 36,
  quyền INTERNET + ACCESS_COARSE/FINE_LOCATION.
- `assets/tauri.conf.json`: Tauri v2, cửa sổ 390×844, `index.html`,
  CSP chỉ cho `ipc:`/`http://ipc.localhost` + `asset:`/`http://asset.localhost`,
  iOS minimumSystemVersion khai sẵn 14.0 (cấu hình mặc định của Tauri, không phải dấu hiệu
  đã từng build cho iOS).
- Dex định nghĩa sẵn toàn bộ vỏ: `studio.sakayori.sorayori.{MainActivity, TauriActivity,
  WryActivity, RustWebView, RustWebViewClient, RustWebChromeClient, Ipc, Rust}`,
  `app.tauri.*` (plugin framework + geolocation), Jackson, AndroidX, Play Services location.

---

## 12. Bổ sung 2026-10-25 — đính chính scope + những gì nhánh này ĐÃ code

### 12.1 Đính chính scope (theo yêu cầu của chủ dự án)

Mục tiêu của nhánh **không phải** "chạy riêng Sorayori". Mục tiêu là **KuDroid hỗ trợ cả
một lớp ngôn ngữ/công nghệ**: app hybrid mà lõi nằm trong WebView và/hoặc `.so` Rust —
Tauri, Cordova, Capacitor, app WebView thuần, và mọi app dùng `android.webkit` +
`android.location` theo cách thông thường. Sorayori chỉ là **ca tham chiếu** để đo bề mặt
API (mục 3), không có dòng code nào trong nhánh này hardcode cho riêng nó: không tên
package, không scheme riêng, không handle riêng.

Cũng đính chính 2 điểm trong chính plan này (đọc lại source `master` kỹ hơn khi bắt tay code):
- **epoll/eventfd/ALooper/AConfiguration/__android_log_write/__register_atfork ĐÃ có sẵn**
  trong `master` (`SyscallShim.cpp` ~9.000 dòng, bảng 834 symbol; `AssetShim.cpp` cho
  AConfiguration). epoll trên Apple đã được dịch sang kqueue (kể cả EPOLLET/ONESHOT),
  eventfd giả lập bằng loopback socket. Mục 5 của plan viết khi chưa đọc hết source —
  phần việc "phải viết epoll shim" là **thừa**; bề mặt ELF cho hồ sơ `.so` Rust về cơ bản
  đã đủ, phần POSIX còn lại resolve qua host dlsym của Darwin.
- `__sF`: `BionicShim.cpp` đã có comment giải thích — Darwin cũng export `__sF` và resolver
  bind thẳng sang host, không cần giả lập layout như mục 5.2 lo ngại.

### 12.2 Đã code trong nhánh này

| Lớp | File | Nội dung |
|---|---|---|
| Host bridge C++ | `include/kudroid/platform/WebViewHost.h`, `src/platform/WebViewHost.cpp` | Registry theo handle, hàng đợi sự kiện host→guest, nav-state đồng bộ, waiter chặn request (begin/await/respond + body hai chiều), vtable host theo mẫu `KuDroidSensorHost` |
| Host bridge C++ | `include/kudroid/platform/LocationHost.h`, `src/platform/LocationHost.cpp` | Subscription theo handle, hàng đợi fix, last-known toàn cục, vtable CoreLocation |
| Runtime | `src/kuart/LibCore.cpp` | `Invoke_android_webkit_WebView` (22 native), `Invoke_android_location_LocationManager` (6 native), route + `LibCoreHasMethod` |
| Framework Java | `framework/android/webkit/WebView.java` | Viết lại toàn bộ: load/eval/JS-interface (reflection + `@JavascriptInterface`), poll sự kiện trên main Looper, dispatch client, interception qua `shouldInterceptRequest` (đọc body request, trả status/mime/body), settings đẩy xuống host |
| Framework Java | `WebViewClient.java`, `WebChromeClient.java`, `WebSettings.java`, `WebResourceRequest.java`, `WebResourceError.java` | Bổ sung các overload/API mà hybrid runtime override: `shouldInterceptRequest`, `shouldOverrideUrlLoading(WebResourceRequest)`, `onGeolocationPermissionsShowPrompt`, `onPermissionRequest`, `onShowFileChooser`, JsResult/JsPromptResult… |
| Framework Java | `PermissionRequest.java`, `GeolocationPermissions.java`, `JsResult.java`, `JsPromptResult.java` (mới) | Class còn thiếu mà Tauri/WebChromeClient tham chiếu |
| Framework Java | `CookieManager.java` | Từ no-op thành kho cookie in-memory thật (set/get theo host) |
| Framework Java | `framework/android/location/LocationManager.java` | Viết lại: subscription thật qua LocationHost, `getCurrentLocation`, `requestSingleUpdate`, poll+deliver đúng Looper của listener. **Bỏ tọa độ giả** 37.4220,-122.0841 mà bản stub cũ trả — bản cũ nói dối mọi app dùng nó |
| Shell iOS | `ios-app/KuDroidShell/WebViewBridge.swift` | WKWebView theo handle: delegate đầy đủ, fetch/XHR shim ở document-start cho host `ipc.localhost`/`asset.localhost`, navigation chính bị chặn qua guest, scheme ngoài → mở app ngoài, geo/media permission |
| Shell iOS | `ios-app/KuDroidShell/LocationBridge.swift` | CLLocationManager: 1 nguồn cho mọi subscription, lọc minTime, one-shot, trạng thái authorization → `services_enabled` |
| Shell iOS | `ContentView.swift`, `kudroid_bridge.h` | Đăng ký 2 bridge cạnh SensorBridge; include header mới |
| Build/test | `CMakeLists.txt`, `tests/test_hybrid_hosts.cpp` | Thêm 2 source vào `kudroid_core`; test host cho event queue/intercept round-trip/pass/timeout/location |

### 12.3 Kiểm chứng đã làm (không cần thiết bị)

- `tests/test_hybrid_hosts.cpp`: build + chạy xanh toàn bộ (event queue thứ tự, intercept
  round-trip kèm body POST và trường hợp buffer nhỏ phải lấy 2 lần, pass, timeout, location encode).
- Hai file host mới compile bằng chính build system của dự án (CMake object target của `kudroid_core`).
- `LibCore.cpp`: qua `g++ -fsyntax-only` với include của dự án.
- Framework Java (webkit + location + toàn bộ dependency kéo theo): `javac --release 8` sạch.
- **Chưa kiểm chứng được trong môi trường này** (nói thẳng): Swift chưa qua swiftc (môi trường
  không có Xcode); link trọn `kudroid_core` ở VM này gãy ở **libvpx** (third-party C build bằng
  cờ của clang trong khi VM chỉ có gcc) — lỗi có từ trước, không liên quan nhánh này, CI của repo
  dùng toolchain đúng sẽ là nơi xác nhận cuối. Chạy thật trên máy iOS vẫn là bước của chủ dự án.

### 12.4 Khoảng trống còn lại của lớp hybrid (việc tiếp theo, theo thứ tự)

1. `shouldOverrideUrlLoading` cho link thường: hiện shell tự quyết (scheme ngoài → mở ngoài);
   chưa hỏi guest Java một cách đồng bộ vì WKNavigationDelegate không chờ được guest main thread
   mà khôngเสี่ยง deadlock. Cần cơ chế quyết định bất đồng bộ + cache quyết định theo URL.
2. `loadUrl(url, headers)`: chưa có kênh header xuống host — headers đang bị bỏ qua.
3. `addJavascriptInterface`: bản bridge là message 1 chiều, **không có giá trị trả về đồng bộ**
   như Android. Runtime nào cần sync-return phải đi qua interception (như Tauri/Cordova hiện đại).
4. Scheme tùy biến do guest đăng ký động (vd `myapp://`): WKURLSchemeHandler phải khai tại lúc
   tạo WKWebView; cần API cho guest khai scheme trước khi create + tạo lại view khi scheme thay đổi.
5. File chooser: host iOS xử lý `<input type=file>` nguyên bản; `onShowFileChooser` của guest
   mới được thông báo, chưa nhận được file chọn. Cần cầu UIDocumentPicker khi guest yêu cầu sở hữu luồng.
6. Geolocation per-origin: quyết định của `GeolocationPermissions.Callback` phía guest đang là
   advisory — quyền thật do CoreLocation của host quyết (đúng mô hình quyền-của-host ở mục 6.5).
7. Cookie: CookieManager phía Java là in-memory; cookie của chính WKWebView do
   WKWebsiteDataStore quản — hai kho chưa đồng bộ hai chiều.
