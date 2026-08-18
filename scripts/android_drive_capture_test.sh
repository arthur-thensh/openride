#!/usr/bin/env bash
set -euo pipefail

# OpenRide automated drive capture test
# Usage:
#   ./scripts/android_drive_capture_test.sh
#
# This script:
# - builds OpenRide with existing Android scripts
# - installs the APK
# - launches the app
# - starts an emulator screen recording
# - waits for a simulated GPS drive session
#
# The GPS simulation activation remains manual because the UI position
# can evolve. Adapt the tap coordinates below once the emulator layout
# is stable.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/test_capture"
VIDEO="$OUT/openride_drive_test.mp4"

mkdir -p "$OUT"

cd "$ROOT"

echo "== OpenRide drive capture =="

echo "[1/5] Start emulator"
./scripts/android_emulator_start.sh

echo "[2/5] Build Android"
./scripts/android_build.sh

echo "[3/5] Install APK"
./scripts/android_install.sh

echo "[4/5] Launch application"
./scripts/android_run.sh

sleep 5

echo "[5/5] Recording drive session"
echo "Activate route + GPS simulation in the emulator."
echo "Recording starts now."

adb shell screenrecord \
    --time-limit 90 \
    /sdcard/openride_drive_test.mp4 &

REC_PID=$!

# Give time for the user or future automation hook to start GPS simulation
sleep 85

wait "$REC_PID" || true

adb pull /sdcard/openride_drive_test.mp4 "$VIDEO" >/dev/null

echo
echo "Video generated:"
echo "$VIDEO"

echo
echo "Next:"
echo "1. Inspect the video"
echo "2. Compare HUD / route rendering"
echo "3. Commit visual changes if validated"
