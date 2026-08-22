#!/bin/bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

# Find Android NDK compiler
CXX="aarch64-linux-android29-clang++"
if [ -n "$ANDROID_NDK_LATEST_HOME" ]; then
    CXX="$ANDROID_NDK_LATEST_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang++"
elif [ -n "$ANDROID_NDK_HOME" ]; then
    # Try linux first, then darwin (macOS)
    if [ -d "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64" ]; then
        CXX="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang++"
    elif [ -d "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64" ]; then
        CXX="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android29-clang++"
    fi
fi

if ! command -v "$CXX" &> /dev/null; then
    echo "[!] Error: Android NDK clang++ not found at $CXX"
    echo "[!] Please set ANDROID_NDK_HOME or ANDROID_NDK_LATEST_HOME to your NDK path."
    exit 1
fi

echo "[*] Using compiler: $CXX"

echo "[*] Building TriangleGLES..."
"$CXX" -shared -fPIC -o libtriangle_gles.so src/triangle_gles.cpp -ldl -llog

echo "[*] Building TriangleVulkan..."
"$CXX" -shared -fPIC -o libtriangle_vulkan.so src/triangle_vulkan.cpp -ldl -llog

echo "[*] Packaging APK..."
mkdir -p apk_root/lib/arm64-v8a
mkdir -p apk_root/assets
mkdir -p apk_root/META-INF

cp libtriangle_gles.so apk_root/lib/arm64-v8a/
cp libtriangle_vulkan.so apk_root/lib/arm64-v8a/

# Fake classes.dex
echo "DEX_FILE_CONTENT" > apk_root/classes.dex
echo "DEX_FILE_CONTENT_2" > apk_root/classes2.dex
echo "ASSET_CONTENT" > apk_root/assets/test_asset.txt
echo "<?xml version=\"1.0\" encoding=\"utf-8\"?><manifest></manifest>" > apk_root/AndroidManifest.xml

cd apk_root
zip -r ../test_triangle.apk *
cd ..

echo "[*] Done! APK created at $DIR/test_triangle.apk"
