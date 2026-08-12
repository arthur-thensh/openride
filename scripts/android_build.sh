#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

# Gradle 8.12 utilisé par le squelette SDL n'est pas compatible avec toutes
# les JVM récentes. Le projet de référence OpenRide utilise JDK 17.
if [ "$(uname -s)" = "Darwin" ] && [ -x /usr/libexec/java_home ]; then
    JAVA17_HOME=$(/usr/libexec/java_home -v 17 2>/dev/null || true)
    if [ -n "$JAVA17_HOME" ]; then
        export JAVA_HOME="$JAVA17_HOME"
        export PATH="$JAVA_HOME/bin:$PATH"
    fi
fi

./scripts/android_check.sh
./scripts/android_prepare.sh

cd build/android
./gradlew -PBUILD_WITH_CMAKE=1 :app:assembleDebug

echo
echo "APK généré :"
find app/build/outputs/apk -name '*.apk' -type f -print
