#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SDL_PROJECT="$ROOT_DIR/vendor/SDL/android-project"
OUT="$ROOT_DIR/build/android"
PACKAGE_DIR="$OUT/app/src/main/java/com/arthurthion/openride"

if [ ! -d "$SDL_PROJECT" ]; then
    echo "Erreur : le projet Android SDL3 est absent." >&2
    echo "Lance d'abord : ./scripts/bootstrap_sdl.sh" >&2
    exit 1
fi
if [ ! -f "$ROOT_DIR/vendor/sqlite/sqlite3.c" ]; then
    echo "Erreur : SQLite Android n'est pas installé." >&2
    echo "Lance : ./scripts/bootstrap_sqlite.sh" >&2
    exit 1
fi

rm -rf "$OUT"
mkdir -p "$ROOT_DIR/build"
cp -R "$SDL_PROJECT" "$OUT"

rm -rf "$OUT/app/jni/SDL"
ln -s "$ROOT_DIR/vendor/SDL" "$OUT/app/jni/SDL"

cp "$ROOT_DIR/android/openride-native.cmake" "$OUT/app/jni/src/CMakeLists.txt"
mkdir -p "$PACKAGE_DIR"
cp "$ROOT_DIR/android/OpenRideActivity.java" "$PACKAGE_DIR/OpenRideActivity.java"
python3 "$ROOT_DIR/android/patch_android_project.py" "$OUT"

SDK_ROOT=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
if [ -z "$SDK_ROOT" ] && [ -d "$HOME/Library/Android/sdk" ]; then
    SDK_ROOT="$HOME/Library/Android/sdk"
fi
if [ -n "$SDK_ROOT" ]; then
    # Gradle's local.properties expects escaped backslashes on Windows; macOS paths are direct.
    printf 'sdk.dir=%s\n' "$SDK_ROOT" > "$OUT/local.properties"
fi

echo "Projet Android généré dans build/android."
