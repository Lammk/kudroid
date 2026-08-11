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

# ── vá lỗi vulkan-loader: định nghĩa sysconfdir / fallback_*_dirs ─────────────────
# bản dựng gn của angle không truyền extra_cflags cho tất cả các mục tiêu third_party.
# vulkan-loader cần các macro này nhưng chúng chỉ được xác định trong các bản dựng cmake.
# chúng tôi đưa chúng trực tiếp vào các tệp nguồn tham chiếu đến chúng.
LOADER_PATCH='
#ifndef SYSCONFDIR
#define SYSCONFDIR "/etc"
#endif
#ifndef FALLBACK_CONFIG_DIRS
#define FALLBACK_CONFIG_DIRS "/etc/xdg"
#endif
#ifndef FALLBACK_DATA_DIRS
#define FALLBACK_DATA_DIRS "/usr/local/share:/usr/share"
#endif
'
for vkfile in third_party/vulkan-loader/src/loader/loader.c \
              third_party/vulkan-loader/src/loader/settings.c; do
    if [[ -f "$vkfile" ]] && ! grep -q 'SYSCONFDIR' <(head -5 "$vkfile"); then
        echo "Patching $vkfile with SYSCONFDIR/FALLBACK_*_DIRS defines"
        printf '%s\n' "$LOADER_PATCH" | cat - "$vkfile" > "$vkfile.tmp"
        mv "$vkfile.tmp" "$vkfile"
    fi
done

# ── bản vá: inline-stub mac-only các ký hiệu hiển thị vulkan cho ios ────────────────
# display.cpp tham chiếu rx::isvulkanmacdisplayavailable() và
# rx::createvulkanmacdisplay() chỉ được biên dịch trên macos.
# trên ios, các ký hiệu này không được xác định → lỗi trình liên kết ở bước solink libglesv2.
# sửa lỗi: đưa các định nghĩa nội tuyến trực tiếp vào display.cpp để trình biên dịch
# phân giải chúng cục bộ mà không cần các ký hiệu bên ngoài.
DISPLAY_FILE="src/libANGLE/Display.cpp"
if [[ -f "$DISPLAY_FILE" ]] && ! grep -q 'KuDroid' "$DISPLAY_FILE"; then
    python3 << 'PYEOF'
filepath = "src/libANGLE/Display.cpp"
with open(filepath, "r") as f:
    lines = f.readlines()

stub = [
    "\n",
    "// ── kudroid: inline stubs for mac-only vulkan display on ios ────────────\n",
    "#if defined(__APPLE__)\n",
    "#include <TargetConditionals.h>\n",
    "#if TARGET_OS_IOS || TARGET_OS_SIMULATOR\n",
    "namespace rx {\n",
    "class DisplayImpl;\n",
    "inline bool IsVulkanMacDisplayAvailable() { return false; }\n",
    "inline DisplayImpl *CreateVulkanMacDisplay(const egl::DisplayState &) {\n",
    "    return nullptr;\n",
    "}\n",
    "}  // namespace rx\n",
    "#endif  // TARGET_OS_IOS\n",
    "#endif  // __APPLE__\n",
    "// ── end kudroid patch ────────────────────────────────────────────────────\n",
    "\n",
]

# tìm điểm chèn: sau dòng #include cuối cùng
insert_after = 0
for i, line in enumerate(lines):
    if line.strip().startswith("#include"):
        insert_after = i + 1

lines[insert_after:insert_after] = stub

with open(filepath, "w") as f:
    f.writelines(lines)

print(f"Patched {filepath}: added inline Mac display stubs for iOS")
PYEOF
fi

# không xóa $build_dir để cho phép các bản dựng tăng dần từ bộ đệm!
mkdir -p "$BUILD_DIR"
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
treat_warnings_as_errors=false
extra_cflags="-Wno-unsafe-buffer-usage"'

autoninja -C "$BUILD_DIR" libEGL libGLESv2

cp -R include/EGL include/GLES include/GLES2 include/GLES3 include/KHR "$OUTPUT_DIR/include/"

# angle trên ios tạo ra các gói .framework
if [[ -d "$BUILD_DIR/libEGL.framework" ]]; then
    cp -R "$BUILD_DIR/libEGL.framework" "$OUTPUT_DIR/lib/ios-arm64/"
fi
if [[ -d "$BUILD_DIR/libGLESv2.framework" ]]; then
    cp -R "$BUILD_DIR/libGLESv2.framework" "$OUTPUT_DIR/lib/ios-arm64/"
fi

# cũng tìm kiếm đệ quy bất kỳ tệp .a hoặc .dylib libegl/libglesv2 nào
find "$BUILD_DIR" -type f \( -name 'libEGL.a' -o -name 'libGLESv2.a' -o -name 'libEGL.dylib' -o -name 'libGLESv2.dylib' \) -exec cp {} "$OUTPUT_DIR/lib/ios-arm64/" \;

# nếu các tệp nhị phân framework tồn tại, hãy đảm bảo libegl.dylib và libglesv2.dylib tồn tại cho các cờ trình liên kết -legl / -lglesv2
if [[ -f "$OUTPUT_DIR/lib/ios-arm64/libEGL.framework/libEGL" && ! -f "$OUTPUT_DIR/lib/ios-arm64/libEGL.dylib" && ! -f "$OUTPUT_DIR/lib/ios-arm64/libEGL.a" ]]; then
    cp "$OUTPUT_DIR/lib/ios-arm64/libEGL.framework/libEGL" "$OUTPUT_DIR/lib/ios-arm64/libEGL.dylib"
fi
if [[ -f "$OUTPUT_DIR/lib/ios-arm64/libGLESv2.framework/libGLESv2" && ! -f "$OUTPUT_DIR/lib/ios-arm64/libGLESv2.dylib" && ! -f "$OUTPUT_DIR/lib/ios-arm64/libGLESv2.a" ]]; then
    cp "$OUTPUT_DIR/lib/ios-arm64/libGLESv2.framework/libGLESv2" "$OUTPUT_DIR/lib/ios-arm64/libGLESv2.dylib"
fi

popd >/dev/null

if [[ ! -e "$OUTPUT_DIR/lib/ios-arm64/libEGL.framework" && ! -f "$OUTPUT_DIR/lib/ios-arm64/libEGL.a" && ! -f "$OUTPUT_DIR/lib/ios-arm64/libEGL.dylib" ]]; then
    echo "ERROR: ANGLE build finished but libEGL/libGLESv2 output was not found." >&2
    exit 1
fi

echo "ANGLE iOS ARM64 installed at: $OUTPUT_DIR"
ls -la "$OUTPUT_DIR/lib/ios-arm64/"
