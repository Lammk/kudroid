#!/usr/bin/env bash
set -e

echo "Bundling assets into KuDroidShell.app..."

# Move the built testing libraries so we can embed them after Xcode build
mkdir -p tests
cp multi-elf-artifact/libkudroid_provider_arm64.so tests/libkudroid_provider_arm64.so
cp multi-elf-artifact/libkudroid_consumer_arm64.so tests/libkudroid_consumer_arm64.so

# Copy pre-built ARM64 test .so from downloaded artifacts into app bundle
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

# Bundle GPU/syscall/JNI test .so files into the app bundle so the
# Debug tab buttons can find them via Bundle.main.
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

# Embed ALL dynamic frameworks into the app bundle so dyld finds them at runtime
mkdir -p KuDroidShell.app/Frameworks
if [ -d third_party/ANGLE/lib/ios-arm64/libEGL.framework ]; then
  cp -R third_party/ANGLE/lib/ios-arm64/libEGL.framework KuDroidShell.app/Frameworks/
fi
if [ -d third_party/ANGLE/lib/ios-arm64/libGLESv2.framework ]; then
  cp -R third_party/ANGLE/lib/ios-arm64/libGLESv2.framework KuDroidShell.app/Frameworks/
fi
if [ -d third_party/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework ]; then
  cp -R third_party/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework KuDroidShell.app/Frameworks/
fi

echo "Asset bundling complete."
