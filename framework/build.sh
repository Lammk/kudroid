#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# Build the KuDroid Android framework classes into a JAR.
#
# The framework is Java code that provides the minimal android.* classes that
# apps need to load Java at startup (before switching to native .so code).
#
# Usage:
#   ./build.sh                 # compile to framework/build/classes + framework/build/framework.jar
#   ./build.sh --bootimage     # also produce a boot.jar for Avian embedding
#
# Requires: a JDK (javac + jar) on PATH or JAVA_HOME.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FRAMEWORK_DIR="$ROOT_DIR/framework"
SRC_DIR="$FRAMEWORK_DIR/android"
BUILD_DIR="$FRAMEWORK_DIR/build"
CLASSES_DIR="$BUILD_DIR/classes"
JAR_PATH="$BUILD_DIR/framework.jar"

# Locate javac/jar.
if [[ -n "${JAVA_HOME:-}" ]]; then
    JAVAC="$JAVA_HOME/bin/javac"
    JAR="$JAVA_HOME/bin/jar"
else
    JAVAC="$(command -v javac)"
    JAR="$(command -v jar)"
fi

if [[ -z "$JAVAC" || -z "$JAR" ]]; then
    echo "ERROR: javac/jar not found. Install a JDK or set JAVA_HOME." >&2
    exit 1
fi

echo "Using javac: $JAVAC"
echo "Using jar:   $JAR"

# Clean and recreate build dirs.
rm -rf "$BUILD_DIR"
mkdir -p "$CLASSES_DIR"

# Collect all .java files.
mapfile -t JAVA_FILES < <(find "$SRC_DIR" -name '*.java' | sort)
if [[ ${#JAVA_FILES[@]} -eq 0 ]]; then
    echo "ERROR: No .java files found under $SRC_DIR" >&2
    exit 1
fi

echo "Compiling ${#JAVA_FILES[@]} Java files..."
"$JAVAC" -encoding UTF-8 -d "$CLASSES_DIR" "${JAVA_FILES[@]}"

echo "Creating $JAR_PATH..."
(cd "$CLASSES_DIR" && "$JAR" cf "$JAR_PATH" .)

echo "Framework JAR built: $JAR_PATH"
ls -lh "$JAR_PATH"

# Optional: produce a boot.jar for Avian embedding.
if [[ "${1:-}" == "--bootimage" ]]; then
    BOOT_JAR="$BUILD_DIR/boot.jar"
    cp "$JAR_PATH" "$BOOT_JAR"
    echo "Boot JAR: $BOOT_JAR"
fi

echo "Done."
