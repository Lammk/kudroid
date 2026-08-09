#!/usr/bin/env bash
set -euo pipefail

VERSION="${MOLTENVK_VERSION:-1.4.2}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DOWNLOAD_DIR="$ROOT_DIR/third_party/downloads"
ARCHIVE="$DOWNLOAD_DIR/MoltenVK-ios-v${VERSION}.tar"
URL="https://github.com/KhronosGroup/MoltenVK/releases/download/v${VERSION}/MoltenVK-ios.tar"

mkdir -p "$DOWNLOAD_DIR"
curl -fL --retry 3 -o "$ARCHIVE" "$URL"
rm -rf "$ROOT_DIR/third_party/MoltenVK"
tar -xf "$ARCHIVE" -C "$ROOT_DIR/third_party"

LIBRARY="$ROOT_DIR/third_party/MoltenVK/MoltenVK/static/MoltenVK.xcframework/ios-arm64/libMoltenVK.a"
if [[ ! -f "$LIBRARY" ]]; then
    echo "ERROR: MoltenVK iOS ARM64 library not found after extraction." >&2
    exit 1
fi

echo "MoltenVK v${VERSION} installed at third_party/MoltenVK"
ls -lh "$LIBRARY"
