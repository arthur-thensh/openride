#!/bin/sh
set -eu

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

adb_run get-state >/dev/null
adb_run shell am start -n com.arthurthion.openride/.OpenRideActivity
