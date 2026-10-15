#!/usr/bin/env bash
# Verify jni_trampoline.S against the real AAPCS64 ABI (host test covers SysV only).
# Freestanding harness under qemu; skips (exit 0) without toolchain/qemu.
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
