#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

if ! command -v cmake >/dev/null 2>&1; then
    echo "Erreur : CMake est introuvable." >&2
    echo "Installe-le avec Homebrew : brew install cmake" >&2
    exit 1
fi

if ! command -v clang >/dev/null 2>&1; then
    echo "Erreur : clang est introuvable." >&2
    echo "Sur macOS : xcode-select --install" >&2
    exit 1
fi

if [ ! -f vendor/SDL/CMakeLists.txt ]; then
    echo "Erreur : SDL3 n'est pas présent dans vendor/SDL." >&2
    echo "Lance d'abord : ./scripts/bootstrap_sdl.sh" >&2
    exit 1
fi

cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo
echo "Configuration terminée."
echo "Compilation : ./scripts/build.sh"
