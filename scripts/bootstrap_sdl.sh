#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SDL_DIR="$ROOT_DIR/vendor/SDL"
SDL_TAG="release-3.4.10"

if [ -f "$SDL_DIR/CMakeLists.txt" ]; then
    echo "SDL3 already present: $SDL_DIR"
    exit 0
fi

if ! command -v git >/dev/null 2>&1; then
    echo "Erreur : git est introuvable." >&2
    exit 1
fi

rm -rf "$SDL_DIR"
git clone --depth 1 --branch "$SDL_TAG" https://github.com/libsdl-org/SDL.git "$SDL_DIR"
echo "SDL3 $SDL_TAG downloaded into vendor/SDL"
