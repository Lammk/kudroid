#!/usr/bin/env bash
set -euo pipefail

# The pinned release's archive is committed under third_party/downloads, so a normal run
# unpacks what is already in the checkout and needs no network at all. Re-downloading it
# every time is what turned a transient 5xx from the release CDN into a failed iOS build
# even though the exact bytes were sitting in the tree.
PINNED_VERSION="1.4.2"
PINNED_ARCHIVE_BYTES=34535424

VERSION="${MOLTENVK_VERSION:-$PINNED_VERSION}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DOWNLOAD_DIR="$ROOT_DIR/third_party/downloads"
ARCHIVE="$DOWNLOAD_DIR/MoltenVK-ios-v${VERSION}.tar"
URL="https://github.com/KhronosGroup/MoltenVK/releases/download/v${VERSION}/MoltenVK-ios.tar"
LIBRARY="$ROOT_DIR/third_party/MoltenVK/MoltenVK/static/MoltenVK.xcframework/ios-arm64/libMoltenVK.a"

# Only the pinned release has a known size; an overridden version is fetch-only.
EXPECT_BYTES=""
if [[ "$VERSION" == "$PINNED_VERSION" ]]; then
    EXPECT_BYTES="$PINNED_ARCHIVE_BYTES"
fi

archive_ok() {
    [[ -f "$ARCHIVE" ]] || return 1
    if [[ -n "$EXPECT_BYTES" ]]; then
        [[ "$(wc -c < "$ARCHIVE" | tr -d '[:space:]')" == "$EXPECT_BYTES" ]] || return 1
    fi
    return 0
}

mkdir -p "$DOWNLOAD_DIR"

downloaded=0
if ! archive_ok; then
    # curl's --retry covers network errors only, so an HTTP status needs
    # --retry-all-errors; the outer loop adds backoff for longer outages. Each attempt
    # starts from a clean file, so a truncated archive is never unpacked.
    attempt=1
    while true; do
        rm -f "$ARCHIVE"
        if curl -fL --retry 3 --retry-delay 5 --retry-all-errors --retry-connrefused \
                -o "$ARCHIVE" "$URL"; then
            break
        fi
        if (( attempt >= 4 )); then
            echo "ERROR: could not download $URL after ${attempt} attempts" >&2
            exit 1
        fi
        echo "download attempt ${attempt} failed; retrying in $((attempt * 10))s" >&2
        sleep $((attempt * 10))
        attempt=$((attempt + 1))
    done
    downloaded=1
    if ! archive_ok; then
        echo "ERROR: $ARCHIVE is $(wc -c < "$ARCHIVE") bytes, expected ${EXPECT_BYTES:-any}" >&2
        exit 1
    fi
fi

# Extract when the tree is absent, or when the archive itself is new — a version bump
# changes the archive name, so a fresh download must replace whatever was unpacked.
if [[ "$downloaded" == 1 || ! -f "$LIBRARY" ]]; then
    rm -rf "$ROOT_DIR/third_party/MoltenVK"
    tar -xf "$ARCHIVE" -C "$ROOT_DIR/third_party"
fi

if [[ ! -f "$LIBRARY" ]]; then
    echo "ERROR: MoltenVK iOS ARM64 library not found after extraction." >&2
    exit 1
fi

echo "MoltenVK v${VERSION} installed at third_party/MoltenVK"
ls -lh "$LIBRARY"
