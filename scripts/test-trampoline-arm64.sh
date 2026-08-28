#!/usr/bin/env bash
# Verify src/kuart/jni_trampoline.S against the real AAPCS64 ABI.
#
# The JNI float/double path is pure register plumbing, so the x86-64 host test
# (test_kuart_jni_float) proves the SysV path only. The product target is arm64,
# where the register files and the calling convention differ — this script
# cross-compiles a freestanding harness and runs it under qemu so the arm64 path
# is covered too.
#
# Freestanding on purpose: the toolchain ships an aarch64 cross-compiler but no
# aarch64 libc sysroot, so the harness talks to the kernel via svc directly.
#
# Skips (exit 0) when the cross toolchain or qemu is unavailable, so it is safe
# to call unconditionally from CI.
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

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

"$CC" -O2 -ffreestanding -nostdlib -fno-stack-protector -static \
    -o "$OUT/arm64_trampoline_test" \
    "$ROOT_DIR/kuart-tests/arm64_trampoline_test.c" \
    "$ROOT_DIR/src/kuart/jni_trampoline.S"

"$QEMU" "$OUT/arm64_trampoline_test"
