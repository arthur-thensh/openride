#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

if [ ! -f build/CTestTestfile.cmake ]; then
    echo "Les tests ne sont pas configurés." >&2
    echo "Lance d'abord : ./scripts/configure.sh puis ./scripts/build.sh" >&2
    exit 1
fi

ctest --test-dir build --output-on-failure
