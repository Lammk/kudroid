# 2026-08-11 — Fix CI Build Failures

## Vấn đề & Fix

### 1. Avian build fail: `invalid source release: 1.`
- **Nguyên nhân**: Avian's classpath build dùng `javac -source 1`, bị bỏ trong JDK 9+. Workflow dùng JDK 11.
- **Fix**: Đổi `build-avian.yml` sang **JDK 8** (temurin 8) — Avian cần JDK 8 để build bootimage/classpath.

### 2. `minijvm-rt-jar` artifact not found
- **Nguyên nhân**: `build.yml` vẫn download artifact `minijvm-rt-jar` (miniJVM) đã bị xóa khi chuyển sang Avian.
- **Fix**: Xóa bước download `minijvm-rt-jar` khỏi `ios-build` job.

### 3. CMake cache path mismatch
- **Nguyên nhân**: `build/`, `build-ios/`, `build-test/`, `framework/build/` bị commit vào git với path máy local (`/home/kuzei/...`). CI dùng path khác → CMakeCache.txt mismatch.
- **Fix**:
  - Tạo `.gitignore` loại bỏ các thư mục build.
  - `git rm -r --cached` xóa build/, build-ios/, build-test/, tests/build/, framework/build/ khỏi git tracking.
  - Thêm bước `rm -rf build build-ios build-test` trước khi configure trong cả 3 job (linux, macos, ios).

### 4. Test .so không còn trong repo
- **Nguyên nhân**: `tests/*.so` bị xóa khỏi git (chúng được build trong CI).
- **Fix**: `ios-build` job thêm `needs: [build-test-so-arm64, build-bionic-test-so-arm64]`, download artifact, copy từ artifact thay vì repo.

## File đã thay đổi
- `.github/workflows/build-avian.yml` (JDK 8)
- `.github/workflows/build.yml` (xóa minijvm, thêm needs + download test .so, clean cache)
- `.gitignore` (mới — loại bỏ build dirs)

## Lưu ý còn lại
- `xcodebuild -project build-ios/KuDroid.xcodeproj` có thể fail vì CMake dùng generator Makefiles (không tạo .xcodeproj). Cần kiểm tra — có thể phải dùng `-G Xcode` hoặc script `generate-xcode.sh` (SwiftPM).