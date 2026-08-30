#!/usr/bin/env bash
# Fetch the exact R8/D8 artifact used by Local and CI framework builds.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="8.3.37"
JAR="$ROOT_DIR/third_party/d8/r8.jar"
BASE="https://dl.google.com/dl/android/maven2/com/android/tools/r8/$VERSION/r8-$VERSION.jar"

mkdir -p "$(dirname "$JAR")"
if [[ ! -f "$JAR" ]]; then
    curl --fail --location --retry 3 --output "$JAR" "$BASE"
fi

# Maven Central publishes the pinned SHA-1 sidecar. Verify the downloaded
# artifact before it is used by javac/d8; this also makes CI failures explicit.
curl --fail --location --retry 3 --output "$JAR.sha1" "$BASE.sha1"
EXPECTED="$(tr -d '[:space:]' < "$JAR.sha1")"
if command -v sha1sum > /dev/null 2>&1; then
    ACTUAL="$(sha1sum "$JAR" | cut -d' ' -f1)"
else
    ACTUAL="$(shasum -a 1 "$JAR" | cut -d' ' -f1)"
fi
if [[ "$EXPECTED" != "$ACTUAL" ]]; then
    echo "ERROR: R8 checksum mismatch: expected=$EXPECTED actual=$ACTUAL" >&2
    exit 1
fi
echo "Pinned R8/D8 $VERSION verified: $JAR sha1=$ACTUAL"
