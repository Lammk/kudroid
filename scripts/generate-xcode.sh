#!/bin/bash
# generate-xcode.sh — generate Xcode project for KuDroidShell iOS app
# runs on GitHub Actions macOS runner.
set -euo pipefail

PROJ_DIR="ios-app/KuDroidShell"
BUILD_DIR="build-ios"
LIB_NAME="kudroid_core"

# ── 1. build kudroid_core static lib for ios arm64 ──
cmake -B "$BUILD_DIR" -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build "$BUILD_DIR" --config Release --target "$LIB_NAME"

# 2. copy static library into app dir
mkdir -p "$PROJ_DIR/libs"
cp "$BUILD_DIR/Release-iphoneos/lib${LIB_NAME}.a" "$PROJ_DIR/libs/"

# 3. create xcode project with xcodebuild (manual config)
# .xcodeproj cannot be created from an idempotent script, so build directly with swiftpm
# or use a .xcodeproj template file.

# create package.swift for spm
cat > "$PROJ_DIR/Package.swift" << 'SWIFTEOF'
// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "KuDroidShell",
    platforms: [.iOS(.v15)],
    products: [
        .executable(name: "KuDroidShell", targets: ["KuDroidShell"])
    ],
    targets: [
        .executableTarget(
            name: "KuDroidShell",
            path: ".",
            sources: ["KuDroidApp.swift", "ContentView.swift"],
            linkerSettings: [
                .linkedLibrary("kudroid_core"),
                .unsafeFlags(["-L", "libs"])
            ]
        )
    ]
)
SWIFTEOF

echo "✅ Xcode project generated at $PROJ_DIR"
echo "📱 Open ios-app/KuDroidShell/ in Xcode, select your team, and build to device."