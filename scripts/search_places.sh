#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

INDEX="data/search/nord-pas-de-calais.orplaces.sqlite"

if [ "$#" -lt 1 ]; then
    echo "Usage: ./scripts/search_places.sh \"Douai\"" >&2
    exit 2
fi
if [ ! -x build/openride_place_search ]; then
    echo "OpenRide n'est pas compile." >&2
    exit 1
fi
if [ ! -f "$INDEX" ]; then
    echo "Index absent. Lance : ./scripts/prepare_place_index.sh" >&2
    exit 1
fi

exec ./build/openride_place_search "$INDEX" "$1" "${2:-10}"
