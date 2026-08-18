#!/usr/bin/env bash
set -e

echo "Bundling assets into KuDroidShell.app..."

# Tạo build_info.json nhúng Git commit hash và timestamp
BUILD_HASH=$(git rev-parse HEAD 2>/dev/null || echo "unknown")
BUILD_SHORT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
BUILD_DATE=$(date -u +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || echo "unknown")

cat <<EOF > KuDroidShell.app/build_info.json
{
  "commit": "${BUILD_HASH}",
  "short_commit": "${BUILD_SHORT}",
  "build_date": "${BUILD_DATE}",
  "arch": "arm64",
  "platform": "ios"
}
EOF
echo "✔ Embedded build_info.json (commit: ${BUILD_HASH})"

# Nhúng tất cả các Frameworks đồ họa (ANGLE + MoltenVK) vào app bundle
mkdir -p KuDroidShell.app/Frameworks
if [ -d third_party/ANGLE/lib/ios-arm64/libEGL.framework ]; then
  cp -R third_party/ANGLE/lib/ios-arm64/libEGL.framework KuDroidShell.app/Frameworks/
  echo "✔ Bundled libEGL.framework"
fi
if [ -d third_party/ANGLE/lib/ios-arm64/libGLESv2.framework ]; then
  cp -R third_party/ANGLE/lib/ios-arm64/libGLESv2.framework KuDroidShell.app/Frameworks/
  echo "✔ Bundled libGLESv2.framework"
fi
if [ -d third_party/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework ]; then
  cp -R third_party/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework KuDroidShell.app/Frameworks/
  echo "✔ Bundled MoltenVK.framework"
fi

# Copy Android Framework JAR
if [ -f framework/build/framework.jar ]; then
  cp framework/build/framework.jar KuDroidShell.app/framework.jar
  echo "✔ Bundled framework.jar"
fi

echo "Asset bundling complete."
