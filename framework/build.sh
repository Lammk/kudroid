#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# xây dựng các lớp khuôn khổ kudroid android thành một tệp jar.
#
# khuôn khổ này là mã java cung cấp các lớp android.* tối thiểu mà
# các ứng dụng cần để tải java khi khởi động (trước khi chuyển sang mã .so gốc).
#
# cách sử dụng:
#   ./build.sh                 # biên dịch thành framework/build/classes + framework/build/framework.jar
#   ./build.sh --bootimage     # cũng tạo ra boot.jar để nhúng avian
#
# yêu cầu: jdk (javac + jar) trên path hoặc java_home.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FRAMEWORK_DIR="$ROOT_DIR/framework"
SRC_DIR="$FRAMEWORK_DIR/android"
BUILD_DIR="$FRAMEWORK_DIR/build"
CLASSES_DIR="$BUILD_DIR/classes"
JAR_PATH="$BUILD_DIR/framework.jar"

# xác định vị trí javac/jar.
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

# dọn dẹp và tạo lại các thư mục xây dựng.
rm -rf "$BUILD_DIR"
mkdir -p "$CLASSES_DIR"

# thu thập tất cả các tệp .java (tránh mapfile; bash mặc định của macos là 3.2).
JAVA_FILES=$(find "$SRC_DIR" -name '*.java' | sort)
if [[ -z "$JAVA_FILES" ]]; then
    echo "ERROR: No .java files found under $SRC_DIR" >&2
    exit 1
fi

echo "Compiling $(echo "$JAVA_FILES" | wc -l | tr -d ' ') Java files..."
# shellcheck disable=SC2086
"$JAVAC" -encoding UTF-8 -d "$CLASSES_DIR" $JAVA_FILES

echo "Creating $JAR_PATH..."
(cd "$CLASSES_DIR" && "$JAR" cf "$JAR_PATH" .)

echo "Framework JAR built: $JAR_PATH"
ls -lh "$JAR_PATH"

# tùy chọn: tạo ra boot.jar để nhúng avian.
if [[ "${1:-}" == "--bootimage" ]]; then
    BOOT_JAR="$BUILD_DIR/boot.jar"
    cp "$JAR_PATH" "$BOOT_JAR"
    echo "Boot JAR: $BOOT_JAR"
fi

echo "Done."
