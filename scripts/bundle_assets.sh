#!/usr/bin/env bash
set -e

echo "Bundling assets into KuDroidShell.app..."

# di chuyển các thư viện kiểm tra đã xây dựng để chúng ta có thể nhúng chúng sau khi xây dựng xcode
mkdir -p tests
cp multi-elf-artifact/libkudroid_provider_arm64.so tests/libkudroid_provider_arm64.so
cp multi-elf-artifact/libkudroid_consumer_arm64.so tests/libkudroid_consumer_arm64.so

# sao chép test .so arm64 được tạo sẵn từ các tạo tác đã tải xuống vào gói ứng dụng
if [ -f test-lib-artifact/test_lib_arm64.so ]; then
  cp test-lib-artifact/test_lib_arm64.so KuDroidShell.app/test_lib.so
  echo "Bundled ARM64 test_lib.so ($(wc -c < KuDroidShell.app/test_lib.so) bytes)"
else
  echo "ERROR: test_lib_arm64.so not found in artifact!"
  exit 1
fi

if [ -f test-bionic-artifact/test_bionic_lib_arm64.so ]; then
  cp test-bionic-artifact/test_bionic_lib_arm64.so KuDroidShell.app/test_bionic_lib.so
  echo "Bundled ARM64 test_bionic_lib.so ($(wc -c < KuDroidShell.app/test_bionic_lib.so) bytes)"
else
  echo "ERROR: test_bionic_lib_arm64.so not found in artifact!"
  exit 1
fi

if [ -f tests/libkudroid_provider_arm64.so ] && [ -f tests/libkudroid_consumer_arm64.so ]; then
  cp tests/libkudroid_provider_arm64.so KuDroidShell.app/libkudroid_provider.so
  cp tests/libkudroid_consumer_arm64.so KuDroidShell.app/libkudroid_consumer.so
  echo "Bundled Multi-ELF provider and consumer libraries"
else
  echo "ERROR: Multi-ELF provider/consumer libraries not found in repo!"
  exit 1
fi

# đóng gói các tệp .so kiểm tra gpu/syscall/jni vào gói ứng dụng để
# các nút tab gỡ lỗi có thể tìm thấy chúng qua bundle.main.
if [ -f gpu-test-artifact/test_gpu_vulkan_arm64.so ]; then
  cp gpu-test-artifact/test_gpu_vulkan_arm64.so KuDroidShell.app/test_gpu_vulkan.so
  echo "Bundled test_gpu_vulkan.so"
else
  echo "ERROR: test_gpu_vulkan_arm64.so not found in artifact!"
  exit 1
fi

if [ -f gpu-test-artifact/test_gpu_opengl_arm64.so ]; then
  cp gpu-test-artifact/test_gpu_opengl_arm64.so KuDroidShell.app/test_gpu_opengl.so
  echo "Bundled test_gpu_opengl.so"
else
  echo "ERROR: test_gpu_opengl_arm64.so not found in artifact!"
  exit 1
fi

if [ -f gpu-test-artifact/test_syscalls_arm64.so ]; then
  cp gpu-test-artifact/test_syscalls_arm64.so KuDroidShell.app/test_syscalls.so
  echo "Bundled test_syscalls.so"
else
  echo "ERROR: test_syscalls_arm64.so not found in artifact!"
  exit 1
fi

if [ -f gpu-test-artifact/test_jni_massive_arm64.so ]; then
  cp gpu-test-artifact/test_jni_massive_arm64.so KuDroidShell.app/test_jni_massive.so
  echo "Bundled test_jni_massive.so"
else
  echo "ERROR: test_jni_massive_arm64.so not found in artifact!"
  exit 1
fi

# nhúng tất cả các framework động vào gói ứng dụng để dyld tìm thấy chúng khi chạy
mkdir -p KuDroidShell.app/Frameworks
if [ -d third_party/ANGLE/lib/ios-arm64/libEGL.framework ]; then
  cp -R third_party/ANGLE/lib/ios-arm64/libEGL.framework KuDroidShell.app/Frameworks/
fi
if [ -d third_party/ANGLE/lib/ios-arm64/libGLESv2.framework ]; then
  cp -R third_party/ANGLE/lib/ios-arm64/libGLESv2.framework KuDroidShell.app/Frameworks/
fi
# Ghi metadata phiên bản build vào KuDroidShell.app
COMMIT_HASH=$(git rev-parse HEAD 2>/dev/null || echo "unknown")
SHORT_HASH=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
COMMIT_MSG=$(git log -1 --pretty=%s 2>/dev/null || echo "local build")
BUILD_TIME=$(date -u +"%Y-%m-%d %H:%M:%S UTC")

cat <<EOF > KuDroidShell.app/build_info.json
{
  "commit": "$COMMIT_HASH",
  "short_commit": "$SHORT_HASH",
  "message": "$COMMIT_MSG",
  "build_time": "$BUILD_TIME"
}
EOF
echo "Generated build_info.json (Commit: $SHORT_HASH, Time: $BUILD_TIME)"

echo "Asset bundling complete."
