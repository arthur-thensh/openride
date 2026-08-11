#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

echo "Configuration..."
./scripts/configure.sh

echo
echo "Compilation..."
./scripts/build.sh

echo
echo "Tests..."
./scripts/test.sh

echo
echo "Prêt. Lance OpenRide avec : ./scripts/run.sh"
