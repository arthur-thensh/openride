#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

if [ ! -x build/openride ]; then
    echo "OpenRide n'est pas compilé." >&2
    echo "Lance : ./scripts/build.sh" >&2
    exit 1
fi

LOCAL_MAP="data/maps/nord-pas-de-calais-shortbread.mbtiles"
LOCAL_GRAPH="data/routing/nord-pas-de-calais.orgraph"

# Import GPX pratique sans avoir à répéter les chemins carte/graphe :
#   ./scripts/run.sh --gpx ~/Downloads/balade.gpx
if [ "${1:-}" = "--gpx" ]; then
    if [ "$#" -ne 2 ]; then
        echo "Usage: ./scripts/run.sh --gpx chemin/trace.gpx" >&2
        exit 1
    fi
    if [ ! -f "$LOCAL_MAP" ] || [ ! -f "$LOCAL_GRAPH" ]; then
        echo "--gpx nécessite la carte et le graphe régionaux installés." >&2
        exit 1
    fi
    exec ./build/openride "$LOCAL_MAP" "$LOCAL_GRAPH" "$2"
fi

# Des chemins fournis explicitement restent prioritaires :
#   ./scripts/run.sh carte.mbtiles [graphe.orgraph] [trace.gpx]
if [ "$#" -gt 0 ]; then
    exec ./build/openride "$@"
fi

if [ -f "$LOCAL_MAP" ] && [ -f "$LOCAL_GRAPH" ]; then
    exec ./build/openride "$LOCAL_MAP" "$LOCAL_GRAPH"
fi

if [ -f "$LOCAL_MAP" ]; then
    exec ./build/openride "$LOCAL_MAP"
fi

# Si la vraie carte n'est pas encore installée, main.c utilise la petite carte
# de démonstration suivie par Git. Le routage restera désactivé tant que le
# fichier .orgraph n'aura pas été préparé.
exec ./build/openride
