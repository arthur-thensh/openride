#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

if [ ! -f build/CMakeCache.txt ]; then
    echo "Le projet n'est pas encore configuré." >&2
    echo "Lance : ./scripts/configure.sh" >&2
    exit 1
fi

cmake --build build -j

echo
echo "Compilation terminée."
echo "Tests     : ./scripts/test.sh"
echo "Lancement : ./scripts/run.sh"
