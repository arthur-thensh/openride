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

# Pratique pendant les mises à jour par archives : si la v0.5 est conservée
# juste à côté, réutiliser sa grosse carte sans la télécharger une seconde fois.
for previous in \
    ../openride-starter-v0.5-real-osm/data/maps/nord-pas-de-calais-shortbread.mbtiles \
    ../openride-starter-v0.5/data/maps/nord-pas-de-calais-shortbread.mbtiles
 do
    if [ -f "$previous" ]; then
        echo "Réutilisation de la carte de la version précédente :"
        echo "  $previous"
        exec ./build/openride "$previous"
    fi
 done

# Sinon main.c retombera sur la petite carte de démonstration.
exec ./build/openride
