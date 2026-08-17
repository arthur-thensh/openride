#!/bin/sh
set -eu

SDK_ROOT=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
if [ -z "$SDK_ROOT" ] && [ -d "$HOME/Library/Android/sdk" ]; then
    SDK_ROOT="$HOME/Library/Android/sdk"
fi
if [ -z "$SDK_ROOT" ]; then
    echo "Android SDK introuvable." >&2
    exit 1
fi

ADB="$SDK_ROOT/platform-tools/adb"
EMULATOR_PORT=${OPENRIDE_EMULATOR_PORT:-5554}
EMULATOR_SNAPSHOT=${OPENRIDE_EMULATOR_SNAPSHOT:-}
SERIAL="emulator-$EMULATOR_PORT"

if [ ! -x "$ADB" ]; then
    echo "adb absent : $ADB" >&2
    exit 1
fi

if "$ADB" -s "$SERIAL" get-state >/dev/null 2>&1 && [ -n "$EMULATOR_SNAPSHOT" ]; then
    "$ADB" -s "$SERIAL" shell am force-stop com.arthurthion.openride >/dev/null 2>&1 || true
    "$ADB" -s "$SERIAL" shell sync
    "$ADB" -s "$SERIAL" emu avd snapshot save "$EMULATOR_SNAPSHOT"
fi

if [ "$(uname -s)" = "Darwin" ]; then
    LAUNCH_LABEL="com.openride.android-emulator-$EMULATOR_PORT"
    if launchctl print "gui/$(id -u)/$LAUNCH_LABEL" >/dev/null 2>&1; then
        launchctl remove "$LAUNCH_LABEL"
    fi
elif "$ADB" -s "$SERIAL" get-state >/dev/null 2>&1; then
    "$ADB" -s "$SERIAL" emu kill >/dev/null
fi

deadline=$(( $(date +%s) + 15 ))
while [ "$(date +%s)" -lt "$deadline" ]; do
    if ! "$ADB" -s "$SERIAL" get-state >/dev/null 2>&1; then
        echo "AVD arrêté : $SERIAL"
        exit 0
    fi
    sleep 0.20
done

echo "Timeout pendant l'arrêt de $SERIAL." >&2
exit 1
