# 2026-08-08 — Execution Test Debug & Fix

## Trạng thái hiện tại (2026-08-08 14:30 ICT)
- ELF parse, mmap, relocate: ✅ OK
- Execution test: ❌ `Symbol 'kudroid_add' not found in .dynsym` (trên IPA cũ)
- Fix `vaddrToOffset`: ✅ Đã push lên GitHub
- CI build mới: 🔄 Đang chạy (commit `f921c7a`)
- Chờ tải IPA mới để test lại

## Đã làm

### 1. Phát hiện bug trong `getSymbolAddress`
- `DT_SYMTAB` và `DT_STRTAB` trong ELF chứa **virtual address**, không phải file offset
- Code cũ dùng trực tiếp `dynamic[i].d_val` làm offset → đọc sai vị trí `.dynsym`
- **Fix**: Thêm hàm `vaddrToOffset()` convert virtual address → file offset qua PT_LOAD segments
- File: `src/elf_loader.cpp`

### 2. Xác nhận file `.so` có symbol
- `tests/test_lib_arm64.so`: ELF 64-bit ARM aarch64, 67272 bytes
- `readelf -s` xác nhận: `kudroid_add` tại offset `0x5c8`, GLOBAL, FUNC
- File đã được commit vào repo từ commit `896476b`

### 3. CI/CD
- `ios-build` copy `tests/test_lib_arm64.so` → `KuDroidShell.app/test_lib.so`
- Đóng gói vào `KuDroidShell.ipa`

## File đã thay đổi
- `src/elf_loader.cpp`: Thêm `vaddrToOffset`, sửa `getSymbolAddress`
- `.github/workflows/build.yml`: Điều chỉnh flow build `.so`
- `.gitignore`: Đã có `!tests/*.so`

## Commits liên quan
- `bbf2aad`: fix: vaddr-to-offset conversion in getSymbolAddress
- `80c234f`: chore: sync local changes

## Bước tiếp theo
1. Đợi CI build xong commit `80c234f`
2. Tải artifact `kudroid-ios-unsigned` mới nhất
3. Cài IPA lên iPhone
4. Bấm "Execution Test"
5. Kết quả mong đợi: `EXECUTION SUCCESS! kudroid_add(40, 20) = 60`