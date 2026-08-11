# 2026-08-11 — Fix CI Build (Round 2)

## Vấn đề & Fix

### 1. Avian build fail: `Could not find satisfied version for SemVer '8'`
- **Nguyên nhân**: Temurin không còn cung cấp JDK 8 cho macOS ARM64.
- **Fix**: Đổi `build-avian.yml` sang distribution **`zulu`** (có JDK 8 cho macOS ARM64) + `actions/setup-java@v5`.

### 2. iOS build fail: `Swift language not supported by "Unix Makefiles" generator`
- **Nguyên nhân**: CMake dùng generator Makefiles, không hỗ trợ Swift (KuDroidShell app).
- **Fix**: Thêm `-G Xcode` vào CMake configure cho iOS build. `cmake --build` dùng `--config Release`.

### 3. `xcodebuild` riêng không cần thiết
- **Nguyên nhân**: Với generator Xcode, `cmake --build` đã build cả `kudroid_core` + `KuDroidShell` app.
- **Fix**: Bỏ bước `xcodebuild` riêng, dùng output từ `cmake --build` (`build-ios/Release-iphoneos/KuDroidShell.app`).

### 4. Upload path sai
- **Nguyên nhân**: Với Xcode generator, static lib nằm ở `Release-iphoneos/`.
- **Fix**: Đổi upload path thành `build-ios/Release-iphoneos/libkudroid_core.a`.

### 5. `ios-build` cần Avian
- **Nguyên nhân**: `kudroid_core` link `libavian.a`, nhưng `ios-build` không có nó.
- **Fix**: Thêm `build-avian-ios` vào `needs`, download `avian-ios-arm64` artifact, copy `libavian.a` vào `third_party/jvm/avian/build/`.

## File đã thay đổi
- `.github/workflows/build-avian.yml` (Zulu JDK 8, setup-java@v5)
- `.github/workflows/build.yml` (-G Xcode, bỏ xcodebuild, upload path, needs Avian)

## ⚠️ Lưu ý còn lại
- Avian build chưa set `bootimage=true` — `kudroid_jni.cpp` dùng `-Xbootclasspath:[bootJar]` cần boot jar. Cần tích hợp framework classes vào boot jar (phức tạp).
- `kudroid_core` link `libavian.a` — cần đảm bảo Avian build ra đúng file.