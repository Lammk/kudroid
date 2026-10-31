#!/usr/bin/env bash
#
# build-local-ios.sh - 1-Click build and deploy KuDroid for iOS on Linux.
#
# Usage:
#   ./scripts/build-local-ios.sh          # Build KuDroidShell.ipa (Debug)
#   ./scripts/build-local-ios.sh --release # Build KuDroidShell.ipa (Release)
#   ./scripts/build-local-ios.sh --run     # Build & install/run directly on iPhone via USB
#   ./scripts/build-local-ios.sh --clean   # Clean build cache before building
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Ensure swift, swiftly, and xtool are in PATH
export PATH="${HOME}/.local/share/swiftly/bin:${HOME}/.local/bin:${PATH}"

BUILD_TYPE="Release"
CONFIG="debug"
DO_RUN=false
DO_CLEAN=false

for arg in "$@"; do
    case "${arg}" in
        --release)
            BUILD_TYPE="Release"
            CONFIG="release"
            ;;
        --debug)
            BUILD_TYPE="Debug"
            CONFIG="debug"
            ;;
        --run|-r)
            DO_RUN=true
            ;;
        --clean)
            DO_CLEAN=true
            ;;
        -h|--help)
            echo "Usage: $0 [--release|--debug] [--run] [--clean]"
            exit 0
            ;;
        *)
            echo "Unknown option: ${arg}"
            exit 1
            ;;
    esac
done

cd "${ROOT_DIR}"
python3 scripts/flutter-runtime/verify-asset.py --asset ios-app/KuDroidShell/Resources/runtime
python3 scripts/verify-system-fonts.py

if [ "${DO_CLEAN}" = true ]; then
    echo "==> Cleaning build artifacts..."
    rm -rf build-ios ios-app/KuDroidShell/.build ios-app/KuDroidShell/xtool
fi

echo "========================================================"
echo " KuDroid iOS Local Build (${CONFIG})"
echo "========================================================"

# 1. Build C++ core library with CMake & Ninja
echo "==> [1/3] Configuring & building kudroid_core with CMake..."
cmake -B build-ios -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/ios-linux-toolchain.cmake \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

cmake --build build-ios --target kudroid_core -j"$(nproc)"

# 2. Combine kudroid_core and art_dex into a single static library
echo "==> [2/3] Packaging libkudroid_core_combined.a..."
mkdir -p ios-app/KuDroidShell/libs
llvm-ar -M << AR_EOF
create ios-app/KuDroidShell/libs/libkudroid_core_combined.a
addlib build-ios/libkudroid_core.a
addlib build-ios/third_party/art_dex/libart_dex.a
addlib build-ios/_deps/libvpx-build/libvpx.a
addlib build-ios/libkudroid_vorbis.a
addlib build-ios/_deps/ogg-build/libogg.a
save
end
AR_EOF

# 3. Build Swift app with xtool
echo "==> [3/3] Building KuDroidShell with xtool..."
cd "${ROOT_DIR}/ios-app/KuDroidShell"

if [ "${DO_RUN}" = true ]; then
    echo "==> Deploying directly to connected iOS device..."
    xtool dev run --usb -c "${CONFIG}"
else
    xtool dev build --ipa -c "${CONFIG}"
    IPA_PATH="${ROOT_DIR}/ios-app/KuDroidShell/xtool/KuDroidShell.ipa"
    python3 "${ROOT_DIR}/scripts/embed_standalone_launcher.py" "${IPA_PATH}"
    python3 "${ROOT_DIR}/scripts/flutter-runtime/verify-asset.py" "${IPA_PATH}"
    python3 "${ROOT_DIR}/scripts/verify-system-fonts.py" "${IPA_PATH}"
    echo ""
    echo "========================================================"
    echo " BUILD SUCCESSFUL!"
    echo " IPA Output: ${IPA_PATH}"
    echo " File Size : $(du -h "${IPA_PATH}" | cut -f1)"
    echo "========================================================"
    echo "Tip: Run './scripts/build-local-ios.sh --run' to deploy directly to iPhone."
fi
