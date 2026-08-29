#!/usr/bin/env bash
# Verify the guest vararg trampolines against the real AAPCS64 ABI.
#
# A guest .so is built for Linux AAPCS64: the first eight integer varargs arrive in x0-x7,
# the first eight floating-point varargs in v0-v7, the rest on the stack. Apple's arm64 ABI
# passes every variadic argument on the stack instead — so forwarding a guest's variadic
# call to the host implementation makes it read the wrong place. The failure is silent: it
# prints plausible numbers, which is how a shimmed snprintf reported a check counter of
# 1839197616 and a block size of 4337540760 on device.
#
# No x86-64 test can catch that, because both the register files and the convention differ
# there. This cross-compiles the real trampoline and the real formatter, calls them the way
# guest code does — genuine variadic calls, letting the compiler place arguments per
# AAPCS64 — and checks the text produced.
#
# Freestanding because the toolchain ships an aarch64 cross-compiler but no aarch64 libc
# sysroot; the harness talks to the kernel via svc directly, and GuestVarargs.cpp is
# written without libc for exactly this reason.
#
# Skips (exit 0) when the cross toolchain or qemu is unavailable, so it is safe to call
# unconditionally from CI.
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
