#!/usr/bin/env bash
# Compiles Java test classes against the framework classes and dexes them for KuART.
# Usage: build-java-test.sh <out.dex> <file.java>...
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$1"
shift
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
JAVAC="${JAVA_HOME:+$JAVA_HOME/bin/}javac"
JAVA="${JAVA_HOME:+$JAVA_HOME/bin/}java"
"$JAVAC" -encoding UTF-8 -nowarn -source 8 -target 8 \
    -bootclasspath "$ROOT/framework/build/classes" -d "$TMP/classes" "$@" 2>&1 | grep -v '^Note:\|warning:' || true
mkdir -p "$TMP/out"
"$JAVA" -cp "$ROOT/third_party/d8/r8.jar" com.android.tools.r8.D8 --min-api 29 \
    --output "$TMP/out" $(find "$TMP/classes" -name '*.class') 2>&1 | grep -v '^Warning\|^Info' || true
mv "$TMP/out/classes.dex" "$OUT"
