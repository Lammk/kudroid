#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "ERROR: ANGLE iOS must be built on macOS with Xcode installed." >&2
    exit 1
fi

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TOOLS_DIR="${ANGLE_TOOLS_DIR:-$ROOT_DIR/third_party/build-tools}"
SOURCE_DIR="${ANGLE_SOURCE_DIR:-$ROOT_DIR/third_party/angle-src}"
OUTPUT_DIR="${ANGLE_OUTPUT_DIR:-$ROOT_DIR/third_party/ANGLE}"
ANGLE_REF="${ANGLE_REF:-be80ce591a481c12d60c50d6040d40c035b40a2b}"
DEPOT_TOOLS="$TOOLS_DIR/depot_tools"
BUILD_DIR="$SOURCE_DIR/out/ios-arm64-vulkan"

mkdir -p "$TOOLS_DIR" "$OUTPUT_DIR/include" "$OUTPUT_DIR/lib/ios-arm64"

if [[ ! -d "$DEPOT_TOOLS/.git" ]]; then
    git clone --depth=1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS"
fi
export PATH="$DEPOT_TOOLS:$PATH"

if [[ ! -d "$SOURCE_DIR/.git" ]]; then
    mkdir -p "$SOURCE_DIR"
    pushd "$SOURCE_DIR" >/dev/null
    git init
    git remote add origin https://github.com/google/angle.git
    popd >/dev/null
fi

pushd "$SOURCE_DIR" >/dev/null
if ! git cat-file -e "$ANGLE_REF^{commit}" 2>/dev/null; then
    git fetch --depth=1 origin "$ANGLE_REF"
fi
git checkout -B pinned-angle "$ANGLE_REF"
python3 scripts/bootstrap.py
gclient sync --no-history --shallow

rm -rf "$BUILD_DIR"
gn gen "$BUILD_DIR" --args='target_os="ios"
target_cpu="arm64"
target_environment="device"
ios_enable_code_signing=false
ios_code_signing_identity=""
is_component_build=false
is_debug=false
angle_enable_vulkan=true
angle_shared_libvulkan=false
angle_enable_metal=false
angle_enable_gl=false
angle_enable_null=false
angle_enable_swiftshader=false
angle_enable_wgpu=false
angle_build_tests=false
angle_build_capture_replay_tests=false
use_custom_libcxx=false
ios_deployment_target="15.0"
extra_cflags_c="-DSYSCONFDIR=\"/etc\" -DFALLBACK_CONFIG_DIRS=\"/etc/xdg\" -DFALLBACK_DATA_DIRS=\"/usr/local/share:/usr/share\""'

autoninja -C "$BUILD_DIR" libEGL libGLESv2

cp -R include/EGL include/GLES include/GLES2 include/GLES3 include/KHR "$OUTPUT_DIR/include/"
find "$BUILD_DIR" -type f \( -name 'libEGL.a' -o -name 'libGLESv2.a' \) -exec cp {} "$OUTPUT_DIR/lib/ios-arm64/" \;
popd >/dev/null

if [[ ! -f "$OUTPUT_DIR/lib/ios-arm64/libEGL.a" || ! -f "$OUTPUT_DIR/lib/ios-arm64/libGLESv2.a" ]]; then
    echo "ERROR: ANGLE build finished but libEGL.a/libGLESv2.a were not found." >&2
    exit 1
fi

echo "ANGLE iOS ARM64 installed at: $OUTPUT_DIR"
ls -lh "$OUTPUT_DIR/lib/ios-arm64/libEGL.a" "$OUTPUT_DIR/lib/ios-arm64/libGLESv2.a"
