#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SDK_ROOT=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
if [ -z "$SDK_ROOT" ] && [ -d "$HOME/Library/Android/sdk" ]; then
    SDK_ROOT="$HOME/Library/Android/sdk"
fi
ADB=${ADB:-adb}
if ! command -v "$ADB" >/dev/null 2>&1 && [ -n "$SDK_ROOT" ] && [ -x "$SDK_ROOT/platform-tools/adb" ]; then
    ADB="$SDK_ROOT/platform-tools/adb"
fi
if ! command -v "$ADB" >/dev/null 2>&1; then
    echo "adb introuvable." >&2
    exit 1
fi

adb_run() {
    if [ -n "${ANDROID_SERIAL:-}" ]; then
        "$ADB" -s "$ANDROID_SERIAL" "$@"
    else
        "$ADB" "$@"
    fi
}

APK=$(find "$ROOT_DIR/build/android/app/build/outputs/apk" -name '*debug*.apk' -type f 2>/dev/null | head -n 1 || true)
if [ -z "$APK" ]; then
    echo "APK absent. Lance d'abord : ./scripts/android_build.sh" >&2
    exit 1
fi

adb_run get-state >/dev/null
adb_run install -r "$APK"
echo
echo "OpenRide est installé. Copie maintenant les données hors ligne avec :"
echo "  ./scripts/android_push_data.sh"
