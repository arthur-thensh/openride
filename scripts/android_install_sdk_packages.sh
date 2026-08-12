#!/bin/sh
set -eu

SDK_ROOT=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
if [ -z "$SDK_ROOT" ] && [ -d "$HOME/Library/Android/sdk" ]; then
    SDK_ROOT="$HOME/Library/Android/sdk"
fi
if [ -z "$SDK_ROOT" ]; then
    echo "Android SDK introuvable. Installe d'abord les Android command-line tools et définis ANDROID_HOME." >&2
    exit 1
fi

SDKMANAGER=""
for candidate in \
    "$SDK_ROOT/cmdline-tools/latest/bin/sdkmanager" \
    "$SDK_ROOT/cmdline-tools/bin/sdkmanager" \
    "$SDK_ROOT/tools/bin/sdkmanager"
do
    if [ -x "$candidate" ]; then
        SDKMANAGER="$candidate"
        break
    fi
done
if [ -z "$SDKMANAGER" ] && command -v sdkmanager >/dev/null 2>&1; then
    SDKMANAGER=$(command -v sdkmanager)
fi
if [ -z "$SDKMANAGER" ]; then
    echo "sdkmanager introuvable. Installe les Android SDK Command-line Tools." >&2
    exit 1
fi

echo "Installation des paquets Android nécessaires à OpenRide..."
"$SDKMANAGER" \
    "platform-tools" \
    "platforms;android-35" \
    "build-tools;35.0.0" \
    "ndk;28.2.13676358" \
    "cmake;3.22.1"

echo
echo "Si Android signale des licences non acceptées, exécute :"
echo "  $SDKMANAGER --licenses"
