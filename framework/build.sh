#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# Build các class framework android tối thiểu của KuDroid thành framework.dex.
#
# KuART nạp DEX trực tiếp (không còn dex2jar/JVM) nên đầu ra là .dex, không phải
# .jar. Chuỗi build: javac → .class → d8 → framework.dex → nhúng thành header C++.
#
# Cách dùng:
#   ./build.sh            # javac + d8 + nhúng vào include/kudroid/framework_dex_bytes.h
#   ./build.sh --no-embed # chỉ build framework.dex, không sinh header
#
# Yêu cầu: JDK (javac) trên PATH hoặc JAVA_HOME, và d8 từ Android SDK.
# Không có d8 thì script CẢNH BÁO và giữ header prebuilt (không làm fail build C++).
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FRAMEWORK_DIR="$ROOT_DIR/framework"
BUILD_DIR="$FRAMEWORK_DIR/build"
CLASSES_DIR="$BUILD_DIR/classes"
DEX_PATH="$BUILD_DIR/framework.dex"
EMBED_HEADER="$ROOT_DIR/include/kudroid/framework_dex_bytes.h"

EMBED=1
[[ "${1:-}" == "--no-embed" ]] && EMBED=0

# ── javac ────────────────────────────────────────────────────────────────────
if [[ -n "${JAVA_HOME:-}" && -x "$JAVA_HOME/bin/javac" ]]; then
    JAVAC="$JAVA_HOME/bin/javac"
    JAVA="$JAVA_HOME/bin/java"
else
    JAVAC="$(command -v javac || true)"
    JAVA="$(command -v java || true)"
fi
if [[ -z "$JAVAC" ]]; then
    echo "ERROR: javac không tìm thấy. Cài JDK hoặc đặt JAVA_HOME." >&2
    exit 1
fi
echo "javac: $JAVAC"

# ── tìm d8 ───────────────────────────────────────────────────────────────────
# Ba dạng: (1) script d8 trong build-tools, (2) d8 trên PATH, (3) d8.jar/r8.jar
# standalone chạy qua `java -cp`. D8_CMD rỗng = không có d8.
D8_CMD=""

find_d8_in_sdk() {
    local sdk="$1"
    [[ -d "$sdk/build-tools" ]] || return 1
    # Build-tools phiên bản cao nhất có d8.
    local candidate
    candidate="$(find "$sdk/build-tools" -maxdepth 2 -name 'd8' -type f 2>/dev/null | sort -V | tail -1)"
    if [[ -n "$candidate" && -x "$candidate" ]]; then
        D8_CMD="$candidate"
        return 0
    fi
    candidate="$(find "$sdk/build-tools" -maxdepth 3 -name 'd8.jar' -type f 2>/dev/null | sort -V | tail -1)"
    if [[ -n "$candidate" && -n "$JAVA" ]]; then
        D8_CMD="$JAVA -cp $candidate com.android.tools.r8.D8"
        return 0
    fi
    return 1
}

for sdk in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}"; do
    [[ -n "$sdk" ]] || continue
    find_d8_in_sdk "$sdk" && break
done

if [[ -z "$D8_CMD" ]] && command -v d8 > /dev/null 2>&1; then
    D8_CMD="$(command -v d8)"
fi

# Jar standalone do người dùng tự đặt vào repo.
if [[ -z "$D8_CMD" && -n "$JAVA" ]]; then
    for jar in "$ROOT_DIR/third_party/d8/d8.jar" "$ROOT_DIR/third_party/d8/r8.jar" \
               "$ROOT_DIR/tools/d8.jar" "$ROOT_DIR/tools/r8.jar"; do
        if [[ -f "$jar" ]]; then
            D8_CMD="$JAVA -cp $jar com.android.tools.r8.D8"
            break
        fi
    done
fi

if [[ -z "$D8_CMD" ]]; then
    echo "WARNING: không tìm thấy d8 (đã thử ANDROID_HOME, ANDROID_SDK_ROOT, PATH," >&2
    echo "         third_party/d8/, tools/). Bỏ qua bước build framework.dex." >&2
    if [[ -f "$EMBED_HEADER" ]]; then
        echo "         Giữ header prebuilt: $EMBED_HEADER" >&2
        exit 0
    fi
    echo "ERROR: cũng không có header prebuilt $EMBED_HEADER — build C++ sẽ fail." >&2
    echo "       Cài Android SDK build-tools hoặc đặt d8.jar vào third_party/d8/." >&2
    exit 1
fi
echo "d8: $D8_CMD"

# ── biên dịch ────────────────────────────────────────────────────────────────
rm -rf "$BUILD_DIR"
mkdir -p "$CLASSES_DIR"

# Tránh mapfile: bash mặc định của macOS là 3.2.
JAVA_FILES=$(find "$FRAMEWORK_DIR" -name '*.java' -not -path "$BUILD_DIR/*" | sort)
if [[ -z "$JAVA_FILES" ]]; then
    echo "ERROR: không có file .java nào dưới $FRAMEWORK_DIR" >&2
    exit 1
fi
echo "Biên dịch $(echo "$JAVA_FILES" | wc -l | tr -d ' ') file Java..."

# d8 chỉ nhận class file tới Java 8 (major 52); JDK mới mặc định sinh cao hơn.
RELEASE_FLAG=""
if "$JAVAC" --help 2>&1 | grep -q -- '--release'; then
    RELEASE_FLAG="--release 8"
fi
# shellcheck disable=SC2086
"$JAVAC" -encoding UTF-8 -nowarn $RELEASE_FLAG -d "$CLASSES_DIR" $JAVA_FILES

# ── .class → .dex ────────────────────────────────────────────────────────────
CLASS_FILES=$(find "$CLASSES_DIR" -name '*.class' | sort)
echo "d8: $(echo "$CLASS_FILES" | wc -l | tr -d ' ') class → framework.dex"

# min-api 29 (Android 10, đúng phiên bản ART mà KuART port từ): từ API 24 trở
# lên d8 KHÔNG desugar default/static interface method nữa, nên không cần --lib
# trỏ vào JDK — mà truyền --lib với JDK mới lại làm d8 chết vì major version.
# shellcheck disable=SC2086
$D8_CMD --min-api 29 --output "$BUILD_DIR" $CLASS_FILES

# d8 luôn đặt tên output là classes.dex.
if [[ ! -f "$DEX_PATH" ]]; then
    if [[ -f "$BUILD_DIR/classes.dex" ]]; then
        mv "$BUILD_DIR/classes.dex" "$DEX_PATH"
    else
        echo "ERROR: d8 không sinh ra dex nào trong $BUILD_DIR" >&2
        exit 1
    fi
fi

echo "Framework DEX: $DEX_PATH"
ls -lh "$DEX_PATH"

# ── nhúng vào header C++ ─────────────────────────────────────────────────────
if [[ "$EMBED" == "1" ]]; then
    python3 "$ROOT_DIR/scripts/embed_framework_dex.py"
fi

echo "Done."
