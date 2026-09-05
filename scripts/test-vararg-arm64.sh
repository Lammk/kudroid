#!/usr/bin/env bash
# Verify the guest vararg trampolines against the real AAPCS64 ABI (x86-64 cannot:
# different register files and convention, and the failure is silent wrong numbers).
# Freestanding harness under qemu; skips (exit 0) without toolchain/qemu.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CC=aarch64-linux-gnu-gcc
CXX=aarch64-linux-gnu-g++
QEMU=qemu-aarch64-static

if ! command -v "$CC" > /dev/null 2>&1 || ! command -v "$CXX" > /dev/null 2>&1; then
    echo "SKIP: $CC/$CXX not found (install gcc-aarch64-linux-gnu to run this)"
    exit 0
fi
if ! command -v "$QEMU" > /dev/null 2>&1; then
    echo "SKIP: $QEMU not found (install qemu-user-static to run this)"
    exit 0
fi

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

# -fno-exceptions/-fno-rtti: freestanding, no unwinder or type-info support present.
"$CXX" -O2 -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector \
    -I "$ROOT_DIR/include" \
    -c -o "$OUT/GuestVarargs.o" \
    "$ROOT_DIR/src/abi/GuestVarargs.cpp"

"$CC" -O2 -ffreestanding -nostdlib -fno-stack-protector -static \
    -o "$OUT/arm64_vararg_test" \
    "$ROOT_DIR/kuart-tests/arm64_vararg_test.c" \
    "$ROOT_DIR/src/bionic_log_trampoline.S" \
    "$OUT/GuestVarargs.o"

"$QEMU" "$OUT/arm64_vararg_test"
