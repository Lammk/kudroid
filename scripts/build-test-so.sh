#!/usr/bin/env bash
# Build the guest .so probes under tests/so/ for on-device testing with kdb.
#
# These are Android arm64 shared objects, loaded on the device through
#
#     so build/test-so/vfs_probe.so
#
# which uploads them and runs them under LibraryManager — so every libc call inside binds
# to KuDroid's shim table exactly as a real guest app's would. That is the point: the host
# tests cover the remapper's logic, but only a guest .so on a device exercises the symbol
# routing, the bionic struct layouts, and the container path resolved at runtime.
#
# Built with the plain aarch64 cross compiler rather than the NDK, and -nostdlib, so the
# objects carry no DT_NEEDED. A dependency on libc.so would send LibraryManager looking
# for a library that does not exist on the device, and the failure would be about loading
# rather than about what is being probed. The undefined symbols are resolved by KuDroid at
# load time, which is what makes them a test of the shims.
#
# Skips (exit 0) when the cross toolchain is missing, so this can be called
# unconditionally.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$ROOT_DIR/tests/so"
OUT_DIR="${1:-$ROOT_DIR/build/test-so}"

CC=aarch64-linux-gnu-gcc
READELF=aarch64-linux-gnu-readelf

if ! command -v "$CC" > /dev/null 2>&1; then
    echo "SKIP: $CC not found (install gcc-aarch64-linux-gnu to build the .so probes)"
    exit 0
fi

mkdir -p "$OUT_DIR"

built=0
for src in "$SRC_DIR"/*.c; do
    [[ -e "$src" ]] || continue
    name="$(basename "$src" .c)"
    out="$OUT_DIR/$name.so"

    # -nostdlib: no DT_NEEDED, see above.
    # -fvisibility=default: the entry point has to be findable by name through
    #   LibraryManager::resolveGlobalSymbol, which reads .dynsym.
    # No --no-undefined: undefined symbols are the whole mechanism here.
    "$CC" -shared -fPIC -O1 \
        -nostdlib -ffreestanding -fno-stack-protector \
        -fvisibility=default \
        -Wall -Wextra \
        -o "$out" "$src"

    # A DT_NEEDED would defeat the point, and it is easy to reintroduce by adding a
    # library to the link line. Fail loudly rather than shipping something that fails on
    # the device for an unrelated reason.
    if "$READELF" -d "$out" 2>/dev/null | grep -q "(NEEDED)"; then
        echo "ERROR: $out has a DT_NEEDED entry; it must be freestanding" >&2
        "$READELF" -d "$out" | grep "(NEEDED)" >&2
        exit 1
    fi

    # The entry point kudroid_run_so_test looks for. Without it the device reports
    # "no recognized entrypoint symbol", which is a confusing way to learn about a
    # visibility or naming mistake.
    if ! "$READELF" --dyn-syms -W "$out" | grep -q "kudroid_test_main"; then
        echo "ERROR: $out does not export kudroid_test_main" >&2
        exit 1
    fi

    size_kb=$(( $(stat -c%s "$out" 2>/dev/null || stat -f%z "$out") / 1024 ))
    echo "built $out (${size_kb} KB)"
    echo "      undefined symbols resolved by KuDroid at load time:"
    "$READELF" --dyn-syms -W "$out" \
        | awk '$5=="GLOBAL" && $7=="UND" {printf "%s ", $8}' \
        | fold -s -w 74 | sed 's/^/        /'
    echo
    built=$((built + 1))
done

if [[ "$built" -eq 0 ]]; then
    echo "no sources in $SRC_DIR"
    exit 0
fi

echo "Run on device with kdb:"
for src in "$SRC_DIR"/*.c; do
    [[ -e "$src" ]] || continue
    echo "  so $(realpath --relative-to="$PWD" "$OUT_DIR/$(basename "$src" .c).so" 2>/dev/null \
                 || echo "$OUT_DIR/$(basename "$src" .c).so")"
done
