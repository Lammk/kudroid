# 2026-08-08 — Fix Execution Crash, JIT Detection & Persistent Logging

## Trạng thái hiện tại
- ELF parse / mmap / relocate: ✅ OK
- Tìm symbol `kudroid_add` trong `.dynsym`: ✅ Đã fix (không còn OOB/segfault khi lookup)
- iOS build (CI): ✅ Đã fix lỗi link `___clear_cache`
- Thực thi native trên ARM64 (LiveContainer + JIT): 🔄 Chờ test IPA mới (commit `fbb1106`)
- JIT status hiển thị trong app: ✅ Bật/tắt phản ánh đúng
- Log ra folder app (success + crash): ✅ Verify trên Linux

## Các bug đã sửa (theo thứ tự)

### 1. Symbol lookup segfault — `getSymbolAddress`
- **Nguyên nhân**: số entry `.dynsym` tính bằng `filesize / sizeof(Elf64Sym)`
  → đọc vượt cuối file buffer 552 bytes → segfault.
- **Fix**: giới hạn số entry bằng khoảng cách tới `STRTAB` (`.dynstr` luôn nằm
  ngay sau `.dynsym`), fallback về cuối buffer.
- File: `src/elf_loader.cpp`

### 2. Stale `.so` trong tmp — Swift
- **Nguyên nhân**: `runExecutionTest()` chỉ copy `.so` nếu chưa tồn tại;
  LiveContainer giữ tmp qua các lần update app → dùng `.so` cũ.
- **Fix**: luôn ghi đè `tmp/test_lib.so`.
- File: `ios-app/KuDroidShell/ContentView.swift`

### 3. Crash không log khi execute (ARM64 I-cache)
- **Nguyên nhân**: ARM64 có I-cache/D-cache tách biệt; code mới copy nằm ở
  D-cache còn I-cache giữ byte cũ → CPU chạy rác → crash bằng signal.
- **Fix**: flush I-cache sau khi map code.

### 4. Lỗi link iOS — `___clear_cache`
- **Nguyên nhân**: `__builtin___clear_cache` hạ xuống `___clear_cache`, không
  có trong iOS runtime → `Undefined symbols for architecture arm64`.
- **Fix**: dùng `sys_icache_invalidate()` (`<libkern/OSCacheControl.h>`) trên
  `__APPLE__`, giữ `__builtin___clear_cache` cho Linux.
- File: `src/elf_loader.cpp`

### 5. JIT status báo sai (luôn Enabled)
- **Nguyên nhân**: probe `mmap` RWX luôn thành công dù JIT tắt → false Enabled.
- **Fix**: chỉ dựa vào `csops(CS_DEBUGGED)`, phản ánh đúng trạng thái JIT.
- File: `src/kudroid_bridge.cpp`

### 6. Crash khi thực thi (hardened runtime RWX)
- **Nguyên nhân**: iOS cấm page RWX thường.
- **Fix**: dùng `MAP_JIT` + `pthread_jit_write_protect_np()` toggle khi ghi
  code, fallback RWX nếu `MAP_JIT` bị từ chối. Thêm chặn: nếu JIT Disabled thì
  `kudroid_execution_test` báo lỗi rõ ràng thay vì crash.
- File: `src/elf_loader.cpp`, `src/kudroid_bridge.cpp`

## Tính năng mới

### JIT status indicator
- `kudroid_jit_status()` → "JIT: Enabled" / "JIT: Disabled"
- Hiển thị dưới tiêu đề app (xanh + tia sét / đỏ + tia sét gạch), cập nhật lúc `onAppear`.

### Persistent logging ra folder app
- `kudroid_set_log_dir(dir)` — app truyền thư mục Documents lúc khởi động.
- Log success `.txt`:
  - Self-Test → `kudroid_selftest.txt`
  - Load .so → `kudroid_load.txt`
  - Execution Test → `kudroid_exec.txt`
- Log crash: signal handler (SIGILL/BUS/SEGV/TRAP/ABRT) flush buffer ra
  `kudroid_crash.log` bằng `write()` async-signal-safe → crash bằng signal
  không còn mất log.

## File đã thay đổi
- `src/elf_loader.cpp`
- `src/kudroid_bridge.cpp`
- `ios-app/KuDroidShell/kudroid_bridge.h`
- `ios-app/KuDroidShell/ContentView.swift`

## Commits liên quan
- `cef3876`: fix bound .dynsym iteration by strtab offset; refresh tmp .so
- `4659c15`: flush I-cache after mapping code (ARM64)
- `5e87dae`: sys_icache_invalidate on Apple + JIT status indicator
- `fbb1106`: persist logs + crash dumps; MAP_JIT + accurate JIT check

## Verify (Linux harness)
- Native x86 `.so`: `EXECUTION SUCCESS! kudroid_add(40, 20) = 60`
- File log `.txt` ghi đúng khi success.
- ARM64 `.so` (crash bằng SIGSEGV trên host x86): `kudroid_crash.log` ghi
  `signal = 11` + toàn bộ log tới thời điểm crash.

## Bước tiếp theo
1. Chờ CI run `31247718806` xong.
2. Tải artifact `kudroid-ios-unsigned`, cài IPA vào LiveContainer (bật JIT) trên iPhone 11.
3. Kiểm tra header hiện `JIT: Enabled`.
4. Bấm "Execution Test" → kỳ vọng `EXECUTION SUCCESS! kudroid_add(40, 20) = 60`.
5. Đọc `kudroid_exec.txt` / `kudroid_crash.log` trong thư mục Documents của app.
6. (Tùy chọn) Thêm `UIFileSharingEnabled` + `LSSupportsOpeningDocumentsInPlace`
   vào Info.plist để xem log qua app Files của iOS.
