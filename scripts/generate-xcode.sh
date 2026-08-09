#!/bin/bash
# generate-xcode.sh — Tạo Xcode project cho KuDroidShell iOS app
# Chạy trên macOS runner của GitHub Actions.
set -euo pipefail

PROJ_DIR="ios-app/KuDroidShell"
BUILD_DIR="build-ios"
LIB_NAME="kudroid_core"

# ── 1. Build kudroid_core static lib cho iOS ARM64 ──
cmake -B "$BUILD_DIR" -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build "$BUILD_DIR" --config Release --target "$LIB_NAME"

# ── 2. Copy static library vào app dir ──
mkdir -p "$PROJ_DIR/libs"
cp "$BUILD_DIR/Release-iphoneos/lib${LIB_NAME}.a" "$PROJ_DIR/libs/"

# ── 3. Tạo Xcode project bằng xcodebuild (manual config) ──
# Vì không thể tạo .xcodeproj từ script dễ dàng, ta tạo build trực tiếp bằng SwiftPM
# hoặc dùng file .xcodeproj template.

# Tạo Package.swift cho SPM
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