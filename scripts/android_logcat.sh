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
if [ -n "${ANDROID_SERIAL:-}" ]; then
    exec "$ADB" -s "$ANDROID_SERIAL" logcat -s SDL:V OpenRide:V AndroidRuntime:E '*:S'
fi
exec "$ADB" logcat -s SDL:V OpenRide:V AndroidRuntime:E '*:S'
