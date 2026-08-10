#!/bin/bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

if ! command -v aarch64-linux-gnu-g++ &> /dev/null; then
    echo "[!] Error: aarch64-linux-gnu-g++ not found."
    echo "[!] Please install gcc-aarch64-linux-gnu to compile Android ELF binaries."
    exit 1
fi

echo "[*] Building TriangleGLES..."
aarch64-linux-gnu-g++ -shared -fPIC -o libtriangle_gles.so src/triangle_gles.cpp -ldl

echo "[*] Building TriangleVulkan..."
aarch64-linux-gnu-g++ -shared -fPIC -o libtriangle_vulkan.so src/triangle_vulkan.cpp -ldl

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
