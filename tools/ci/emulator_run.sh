#!/usr/bin/env bash
# Installs the APK on a running emulator/device, starts the benchmark through
# the automation intent and validates the result JSON.
#
# Usage: tools/ci/emulator_run.sh APK [MODE] [FRAMES] [OUTDIR] [ABI]
#   MODE    quick | standard            (default quick)
#   FRAMES  0 = specification frame counts (hashes are verified), N = smoke test
#   ABI     optional: force the native ABI (e.g. arm64-v8a on an x86_64
#           emulator, executed through Android's ARM translation layer)
set -euo pipefail

APK="$1"
MODE="${2:-quick}"
FRAMES="${3:-0}"
OUT="${4:-emulator-out}"
ABI="${5:-}"
PKG="com.plpp555.trisync3d"
RESULT="/sdcard/Android/data/$PKG/files/result.json"
TIMEOUT_S="${TIMEOUT_S:-2700}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

mkdir -p "$OUT"
adb wait-for-device
echo "device: sdk=$(adb shell getprop ro.build.version.sdk | tr -d '\r') abilist=$(adb shell getprop ro.product.cpu.abilist | tr -d '\r')"
adb shell svc power stayon true || true
adb shell settings put system screen_off_timeout 2147483647 || true
adb shell input keyevent KEYCODE_WAKEUP || true
adb shell wm dismiss-keyguard || true
adb shell input keyevent 82 || true

adb uninstall "$PKG" >/dev/null 2>&1 || true
if [ -n "$ABI" ]; then
    adb install --abi "$ABI" "$APK"
else
    adb install "$APK"
fi
echo "installed: $(adb shell dumpsys package "$PKG" | grep -E 'primaryCpuAbi|versionName' | tr -d '\r' | xargs)"

adb logcat -c
adb shell am start -W -n "$PKG/.MainActivity" --es autostart "$MODE" --ei frames "$FRAMES"

start=$SECONDS
n=0
status=running
while :; do
    sleep 10
    n=$((n + 1))
    if adb shell "test -f $RESULT && echo yes" | grep -q yes; then
        status=done
        break
    fi
    if [ -z "$(adb shell pidof "$PKG" | tr -d '\r')" ]; then
        status=crashed
        break
    fi
    if [ $((n % 6)) -eq 1 ]; then
        adb exec-out screencap -p > "$OUT/screen_$(printf %03d $n).png" || true
        adb logcat -d -s TriSync3D:V | tail -n 2 || true
    fi
    if [ $((SECONDS - start)) -gt "$TIMEOUT_S" ]; then
        status=timeout
        break
    fi
done
echo "status: $status after $((SECONDS - start)) s"

adb exec-out screencap -p > "$OUT/screen_final.png" || true
adb logcat -d > "$OUT/logcat.txt" || true
echo "---- TriSync3D log ----"
grep -E "TriSync3D" "$OUT/logcat.txt" | grep -v "RESULT " | tail -n 60 || true
echo "---- activity / window events ----"
grep -iE "trisync" "$OUT/logcat.txt" | grep -vE "TriSync3D" | tail -n 25 || true
if [ "$status" != done ]; then
    echo "---- crash / error log ----"
    grep -E "AndroidRuntime|DEBUG  |libc    |F/|E/TriSync3D" "$OUT/logcat.txt" | tail -n 80 || true
    exit 1
fi
adb pull "$RESULT" "$OUT/result.json"
if [ "$FRAMES" = "0" ]; then
    python3 "$ROOT/tools/ci/check_result.py" "$OUT/result.json" --require-match
else
    python3 "$ROOT/tools/ci/check_result.py" "$OUT/result.json"
fi
