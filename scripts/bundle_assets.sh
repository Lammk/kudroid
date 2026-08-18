#!/usr/bin/env bash
set -e

echo "Bundling assets into KuDroidShell.app..."

# Tạo build_info.json nhúng Git commit hash và timestamp
BUILD_HASH=$(git rev-parse HEAD 2>/dev/null || echo "unknown")
BUILD_DATE=$(date -u +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || echo "unknown")

cat <<EOF > KuDroidShell.app/build_info.json
{
  "commit": "${BUILD_HASH}",
  "build_date": "${BUILD_DATE}",
  "arch": "arm64",
  "platform": "ios"
}
EOF
echo "✔ Embedded build_info.json (commit: ${BUILD_HASH})"

# Copy Android Framework JAR
if [ -f framework/build/framework.jar ]; then
  cp framework/build/framework.jar KuDroidShell.app/framework.jar
  echo "✔ Bundled framework.jar"
fi

echo "Asset bundling complete."
