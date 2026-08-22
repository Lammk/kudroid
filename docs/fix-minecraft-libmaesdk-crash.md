# Fix crash SIGSEGV pc=0x0 khi load Minecraft PE (libmaesdk.so)

**Ngày:** 2026-08-22
**Build bị lỗi:** kudroid_core v0.2.0 Aug 22 2026 12:51:12 commit 3f922e2

## Triệu chứng

Chạy Minecraft PE 26.30 trên KuDroidShell → app crash ngay khi load thư viện:

```
signal = 11 (SIGSEGV)
fault_addr = 0x0
pc = 0x0                    ← nhảy tới địa chỉ NULL
lr = libmaesdk.so+0x3354cc  ← lời gọi đến từ init code của libmaesdk
x8 = 0x1337beefcafecafe     ← stack guard cookie của SyscallShim (TLS đúng)
x0 = string ptr, x1 = string ptr, x2 = 0x40   ← trông như strlcpy(dst, src, 64)
```

FP chain: `executeInit()` → chạy DT_INIT/init_array của **libmaesdk.so** → crash.
Log dừng ngay sau `bionic_dlsym: [getauxval] resolved via SyscallShim`.

## Nguyên nhân gốc

### Bug chính — `src/elf_loader.cpp` (relocation)

Weak undefined symbols (`STB_WEAK` + `SHN_UNDEF`) bị **bind cứng nullptr** mà
không hề thử resolve qua các lib đã load / shim:

```cpp
// CODE CŨ (SAI):
} else if (isWeakUndef) {
    address = nullptr;        // bỏ qua cả LibraryManager lẫn shim!
}
```

Trên Android thật, linker **vẫn bind** weak undef vào symbol mạnh nếu nó tồn tại
(vd `strlcpy`, `strlcat` có trong bionic libc). Chỉ khi không tìm thấy ở đâu cả
mới giữ slot = 0 (caller luôn có `cbz x8, skip` phía trước để xử lý null).

Hậu quả: GOT slot của `strlcpy` trong libmaesdk = 0 dù shim CÓ thể cung cấp →
init array gọi qua PLT → nhảy tới `pc=0x0` → SIGSEGV đúng như log.

Đây cũng là lý do **không có warning "Unresolved symbol" nào** trước crash —
nhánh warning bị skip cho weak undef.

### Bug phụ — `bionic_dlsym` với DUMMY_HANDLE

`bionic_dlopen("libc.so")` trả về handle giả `DUMMY_HANDLE` (libc không tồn tại
trên host). Nhưng khi guest gọi `dlsym(handle, "hàm")`, code cũ trả nullptr
ngay mà không hỏi các lib guest (.so Android) đã load qua LibraryManager —
trong khi trên Android thật `libc.so` là lib thật nên dlsym luôn resolve được.

## Các fix

### 1. `src/elf_loader.cpp` — weak undef giờ được resolve bình thường

Thứ tự resolve cho mọi symbol undef (kể cả weak):
1. `libraryManager_->resolveGlobalSymbol(name)` — tìm trong các .so guest đã load
2. `resolve_bionic_symbol(name)` — shim syscall/graphics/input/audio
3. Không thấy → slot = 0 (đúng hành vi linker), và vẫn im warning nếu là weak

### 2. `src/abi/SyscallShim.cpp` — dlsym(DUMMY_HANDLE) hỏi guest libs trước

Thêm hook toàn cục:

```cpp
extern "C" { void* (*kudroid_guest_symbol_lookup)(const char* name) = nullptr; }
```

Trong `bionic_dlsym`, nhánh `handle == DUMMY_HANDLE` giờ gọi hook trước khi trả
nullptr, kèm log `resolved via guest LibraryManager`.

### 3. `src/kudroid_bridge.cpp` — cài hook khi bắt đầu run APK

```cpp
kudroid_guest_symbol_lookup = [](const char* name) -> void* {
    return globalLibraryManager().resolveGlobalSymbol(name);
};
```

### 4. Bổ sung syscall/symbol còn thiếu vào bảng shim

Theo nguyên tắc: iOS có sẵn → map thẳng; iOS không có → giả lập; không quan
trọng → no-op.

| Symbol | Cách xử lý |
|---|---|
| `strlcpy`, `strlcat` | Map thẳng vào libc iOS (BSD origin, có sẵn) |
| `isatty`, `getpagesize` | Map thẳng |
| `tkill` | Giả lập = `tgkill(getpid(), tid, sig)` |
| `pthread_setname_np(pthread_t, const char*)` | Wrapper: thread hiện tại → iOS API; thread khác → no-op |
| `setprogname`/`getprogname` | Map thẳng trên Apple; Linux host dùng buffer tĩnh + `/proc/self/cmdline` |
| `mallopt` | No-op trả 1 (iOS malloc zone không tune được) |
| `malloc_info` | Trả -1/ENOSYS (Android thật cũng vậy) |

### 5. `CMakeLists.txt` — gỡ `src/gpu_test.cpp` (SAU ĐÓ HOÀN TÁC)

File này đã bị xóa ở commit f57c007 ("Delete") nhưng CMakeLists còn tham chiếu
→ build host fail. Đã gỡ dòng.

**Tuy nhiên** iOS CI build fail sau đó: `Undefined symbols: _kudroid_test_gpu`
— vì `ContentView.swift` (Debug tab) vẫn gọi `kudroid_test_gpu()` qua bridging
header. File bị xóa nhưng UI vẫn dùng → **khôi phục `src/gpu_test.cpp` từ git
history** (`git show f57c007^:src/gpu_test.cpp`) và thêm lại vào CMakeLists.
Kèm dọn warning `gl_get_error` unused — giờ được dùng để log `glGetError`
sau khi setup pipeline.

## Kết quả

- ✅ Build Linux host pass hoàn toàn
- ✅ `test_shims`: **52 checks, 0 failures**
- ✅ `test_dex_to_jar`: PASS
- ✅ iOS CI: fix linker error `_kudroid_test_gpu` (khôi phục gpu_test.cpp)
- ⏳ Cần build iOS + test lại Minecraft PE trên máy thật để xác nhận hết crash

## Files thay đổi

- `src/elf_loader.cpp` — fix weak undef resolution
- `src/abi/SyscallShim.cpp` — hook guest lookup + 9 symbol mới
- `src/kudroid_bridge.cpp` — cài hook + extern declaration
- `CMakeLists.txt` — thêm lại `src/gpu_test.cpp` (bị xóa nhầm khi Swift UI vẫn dùng)
- `src/gpu_test.cpp` — khôi phục từ git history + dùng `gl_get_error` trong log
