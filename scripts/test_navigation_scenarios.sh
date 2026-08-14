#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"

if [ ! -d "$BUILD_DIR" ]; then
    echo "[ERREUR] Le dossier build n'existe pas."
    echo "Lance d'abord : ./scripts/build_macos.sh"
    exit 1
fi

echo "[1/2] Compilation du Scenario Runner..."
cmake --build "$BUILD_DIR" --target test_navigation_scenarios

echo
echo "[2/2] Rejeu des scenarios GPS preprogrammes..."
"$BUILD_DIR/test_navigation_scenarios"
