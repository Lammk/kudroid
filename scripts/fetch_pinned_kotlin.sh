#!/usr/bin/env bash
# Fetch the exact pinned kotlin-stdlib artifact used by Local and CI framework builds.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="1.9.24"
JAR="$ROOT_DIR/third_party/kotlin/kotlin-stdlib-$VERSION.jar"
BASE="https://repo1.maven.org/maven2/org/jetbrains/kotlin/kotlin-stdlib/$VERSION/kotlin-stdlib-$VERSION.jar"

mkdir -p "$(dirname "$JAR")"
if [[ ! -f "$JAR" ]]; then
    echo "Downloading kotlin-stdlib $VERSION..."
    curl --fail --location --retry 3 --output "$JAR" "$BASE"
fi

# Maven Central publishes the pinned SHA-1 sidecar. Verify checksum.
curl --fail --location --retry 3 --output "$JAR.sha1" "$BASE.sha1"
EXPECTED="$(tr -d '[:space:]' < "$JAR.sha1")"
if command -v sha1sum > /dev/null 2>&1; then
    ACTUAL="$(sha1sum "$JAR" | cut -d' ' -f1)"
else
    ACTUAL="$(shasum -a 1 "$JAR" | cut -d' ' -f1)"
fi

if [[ "$EXPECTED" != "$ACTUAL" ]]; then
    echo "ERROR: kotlin-stdlib checksum mismatch: expected=$EXPECTED actual=$ACTUAL" >&2
    exit 1
fi
echo "Pinned kotlin-stdlib $VERSION verified: $JAR sha1=$ACTUAL"
