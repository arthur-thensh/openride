#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

./scripts/android_check.sh
./scripts/android_prepare.sh

cd build/android
./gradlew -PBUILD_WITH_CMAKE=1 :app:assembleDebug

echo
echo "APK généré :"
find app/build/outputs/apk -name '*.apk' -type f -print
