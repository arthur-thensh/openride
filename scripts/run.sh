#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

if [ ! -x build/openride ]; then
    echo "OpenRide n'est pas compilé." >&2
    echo "Lance : ./scripts/build.sh" >&2
    exit 1
fi

# Un chemin fourni explicitement reste prioritaire.
if [ "$#" -gt 0 ]; then
    exec ./build/openride "$@"
fi

LOCAL_MAP="data/maps/nord-pas-de-calais-shortbread.mbtiles"
if [ -f "$LOCAL_MAP" ]; then
    exec ./build/openride "$LOCAL_MAP"
fi

# Si la vraie carte n'est pas encore installée, main.c utilise la petite carte
# de démonstration suivie par Git.
exec ./build/openride
