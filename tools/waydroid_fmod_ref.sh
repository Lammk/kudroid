#!/usr/bin/env bash
# Android reference capture for the FMOD/Sound investigation (adb, no root).
#
# Runs the game inside Waydroid and records whether the platform itself can
# create its FMOD sounds. Separates "our host shim hands FMOD something bad"
# from "this repacked APK is already broken on real Android".
#
# Artifacts: /tmp/wdref/logcat.txt
# Usage: bash tools/waydroid_fmod_ref.sh [capture_seconds]
set -u

DEV=192.168.240.112:5555
PKG=com.aarongamingdev.ULTRAKILL
CAPTURE="${1:-240}"
OUT=/tmp/wdref
mkdir -p "$OUT"

echo "[1/5] device"
adb connect "$DEV" >/dev/null 2>&1
adb -s "$DEV" get-state 2>&1 | head -2
echo "    boot=$(adb -s "$DEV" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')"
echo "    bridge=$(adb -s "$DEV" shell getprop ro.dalvik.vm.native.bridge 2>/dev/null | tr -d '\r')"

echo "[2/5] clearing logcat"
adb -s "$DEV" logcat -c 2>/dev/null

echo "[3/5] capturing ${CAPTURE}s in the background"
( timeout "$CAPTURE" adb -s "$DEV" logcat -v time > "$OUT/logcat.txt" 2>&1 ) &
CAP=$!
sleep 3

echo "[4/5] launching $PKG"
adb -s "$DEV" shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 2>&1 | tail -2
for s in $(seq 1 $((CAPTURE / 10))); do
    sleep 10
    p=$(adb -s "$DEV" shell pidof "$PKG" 2>/dev/null | tr -d '\r')
    if [ -z "$p" ]; then echo "    t=$((s * 10))s  not running"; else echo "    t=$((s * 10))s  pid=$p"; fi
done
wait "$CAP" 2>/dev/null

echo "[5/5] verdicts"
wc -l "$OUT/logcat.txt" 2>/dev/null
for pat in "Cannot create FMOD" "Cannot load audio data" "Failed getting load state" "AudioTrack" "OpenSL" "AAudio" "FMOD"; do
    n=$(grep -c "$pat" "$OUT/logcat.txt" 2>/dev/null)
    echo "    ${pat} : ${n:-0}"
done
echo
echo "--- audio/FMOD lines (first 25) ---"
grep -iE "FMOD|Cannot load audio|Failed getting load state|AudioTrack|OpenSL|AAudio" "$OUT/logcat.txt" 2>/dev/null | head -25
echo
echo "--- Unity lines (first 40) ---"
grep -i "unity" "$OUT/logcat.txt" 2>/dev/null | head -40
echo
echo "artifact: $OUT/logcat.txt"
