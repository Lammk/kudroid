#!/usr/bin/env bash
# Execute the JIT's generated code on real arm64.
#
# test_kuart_jit checks the encoder against GNU as byte for byte, on whatever host it
# runs on. That cannot catch a sequence that is validly encoded and computes the wrong
# thing — a load from the wrong register offset, a clamp that loses the sign, a branch
# patched to the wrong instruction. Running the bytes is the only way to know.
#
# Two stages: build the host dumper (which uses the real encoder from kudroid_core) to
# emit the code as a header, then cross-compile a freestanding harness around it and run
# that under qemu. Freestanding because the toolchain has an aarch64 compiler but no
# aarch64 sysroot.
#
# Skips (exit 0) when the cross toolchain or qemu is missing, so CI can call it
# unconditionally.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CC=aarch64-linux-gnu-gcc
QEMU=qemu-aarch64-static

if ! command -v "$CC" > /dev/null 2>&1; then
    echo "SKIP: $CC not found (install gcc-aarch64-linux-gnu to run this)"
    exit 0
fi
if ! command -v "$QEMU" > /dev/null 2>&1; then
    echo "SKIP: $QEMU not found (install qemu-user-static to run this)"
    exit 0
fi

BUILD_DIR="${1:-$ROOT_DIR/build}"
JIT_OBJ="$BUILD_DIR/CMakeFiles/kudroid_core.dir/src/kuart/JitCompiler.cpp.o"
CACHE_OBJ="$BUILD_DIR/CMakeFiles/kudroid_core.dir/src/kuart/JitCache.cpp.o"
MEMORY_OBJ="$BUILD_DIR/CMakeFiles/kudroid_core.dir/src/platform/MemoryInfo.cpp.o"
ART_LIB="$BUILD_DIR/third_party/art_dex/libart_dex.a"

if [[ ! -f "$JIT_OBJ" || ! -f "$CACHE_OBJ" || ! -f "$MEMORY_OBJ" || ! -f "$ART_LIB" ]]; then
    echo "SKIP: build kudroid_core first (missing $JIT_OBJ)"
    exit 0
fi

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

# The dumper links the REAL encoder, so the bytes under test are the ones the runtime
# would emit — not a copy that could drift from it.
g++ -std=c++17 -w \
    -I "$ROOT_DIR/include" -I "$ROOT_DIR/third_party/art_dex" \
    -o "$OUT/dump" \
    "$ROOT_DIR/kuart-tests/jit_codegen_dump.cpp" \
    "$JIT_OBJ" "$CACHE_OBJ" "$MEMORY_OBJ" "$ART_LIB" -lz

"$OUT/dump" > "$OUT/gen_code.h"

"$CC" -O1 -ffreestanding -nostdlib -fno-stack-protector -static \
    -I "$OUT" \
    -o "$OUT/jit_exec" \
    "$ROOT_DIR/kuart-tests/arm64_jit_exec_test.c"

"$QEMU" "$OUT/jit_exec"
