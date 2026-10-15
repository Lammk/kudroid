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
gclient sync --no-history --shallow --reset --force

# Work around vulkan-loader error: define sysconfdir / fallback_*_dirs.
# ANGLE's gn build passes no extra_cflags to third_party targets, while
# vulkan-loader needs these macros (cmake builds define them), so patch them
# directly into the sources that reference them.
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

# Patch: inline stubs for mac-only vulkan display symbols on ios.
# Display.cpp references rx::IsVulkanMacDisplayAvailable() and
# rx::CreateVulkanMacDisplay(), which exist only on macos; on ios they are
# undefined and linking libGLESv2 fails. Fix: inline the definitions directly
# into Display.cpp.
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

# insertion point: after the last #include line
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

# keep $build_dir for incremental rebuilds!
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
angle_enable_metal=true
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

# angle on ios produces .framework packages
if [[ -d "$BUILD_DIR/libEGL.framework" ]]; then
    cp -R "$BUILD_DIR/libEGL.framework" "$OUTPUT_DIR/lib/ios-arm64/"
fi
if [[ -d "$BUILD_DIR/libGLESv2.framework" ]]; then
    cp -R "$BUILD_DIR/libGLESv2.framework" "$OUTPUT_DIR/lib/ios-arm64/"
fi

# also collect any libegl/libglesv2 .a or .dylib
find "$BUILD_DIR" -type f \( -name 'libEGL.a' -o -name 'libGLESv2.a' -o -name 'libEGL.dylib' -o -name 'libGLESv2.dylib' \) -exec cp {} "$OUTPUT_DIR/lib/ios-arm64/" \;

# if framework binaries exist, ensure libegl.dylib and libglesv2.dylib exist for -legl / -lglesv2 link steps
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
