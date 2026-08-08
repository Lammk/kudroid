# 2026-08-08 — Push to GitHub & Trigger CI

## Đã làm gì
Push toàn bộ thay đổi lên GitHub và trigger GitHub Actions CI/CD.

## Làm như thế nào
1. `git add -A` — stage tất cả thay đổi
2. `git commit -m "feat: add getSymbolAddress, testExecution, execution test button + docs"`
3. `git push origin master` — push lên nhánh master (nhánh develop chưa tồn tại)

## File đã thay đổi
- `include/kudroid/elf_loader.hpp` — thêm getSymbolAddress, testExecution
- `src/elf_loader.cpp` — implement getSymbolAddress + testExecution
- `src/kudroid_bridge.cpp` — implement kudroid_execution_test
- `ios-app/KuDroidShell/kudroid_bridge.h` — khai báo kudroid_execution_test
- `ios-app/KuDroidShell/ContentView.swift` — thêm nút Execution Test
- `docs/2026-08-08-execution-test-button.md` — docs mới
- `tests/libtest_lib.so` — prebuilt ARM64 test lib (từ artifact)

## Workflow CI (`.github/workflows/build.yml`)
4 jobs được trigger:
- **build-linux** (ubuntu-latest): cmake build
- **build-macos-arm64** (macos-14): cmake build ARM64
- **ios-build** (macos-14): build iOS static lib + compile Swift app + đóng gói IPA unsigned
- **build-test-so-arm64** (ubuntu-latest): cross-compile test_lib.so ARM64

## Kết quả
- Push thành công lên https://github.com/Lammk/kudroid (commit cc7fbde)
- Actions tự động chạy: https://github.com/Lammk/kudroid/actions