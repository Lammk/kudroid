#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$1"
shift
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/classes" "$OUT"
javac -encoding UTF-8 -nowarn -source 8 -target 8 \
    -bootclasspath "$ROOT/framework/build/classes" -d "$TMP/classes" "$@"
for source in "$@"; do
    name="$(basename "$source" .java)"
    mkdir -p "$TMP/$name"
    java -cp "$ROOT/third_party/d8/r8.jar" com.android.tools.r8.D8 --min-api 29 \
        --classpath "$TMP/classes" --output "$TMP/$name" "$TMP/classes/$name.class"
    mv "$TMP/$name/classes.dex" "$OUT/$name.dex"
done
