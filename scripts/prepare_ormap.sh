#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

PBF="data/osm/nord-pas-de-calais-latest.osm.pbf"
GRAPH="data/routing/nord-pas-de-calais.orgraph"
SEARCH="data/search/nord-pas-de-calais.orplaces.sqlite"
MAP="data/maps/nord-pas-de-calais.ormap"
PART="$MAP.part"

for file in "$PBF" "$GRAPH" "$SEARCH"; do
    if [ ! -f "$file" ]; then
        echo "Donnée requise absente : $file" >&2
        echo "Utilise ./scripts/prepare_region.sh pour construire toute la région." >&2
        exit 1
    fi
done

if [ ! -x build/openride_ormap_import ]; then
    echo "L'importateur .ormap n'est pas compilé." >&2
    echo "Lance : ./scripts/configure.sh puis ./scripts/build.sh" >&2
    exit 1
fi

mkdir -p data/maps
rm -f "$PART"

echo "Construction de la carte OpenRide .ormap..."
echo "Source OSM : $PBF"
echo "Routage    : $GRAPH"
echo "Recherche  : $SEARCH"
echo "Sortie     : $MAP"
echo
echo "Les bâtiments individuels ne sont pas conservés :"
echo "ils sont regroupés en zones bâties simplifiées."
echo

./build/openride_ormap_import "$PBF" "$GRAPH" "$SEARCH" "$PART"
mv "$PART" "$MAP"

echo
echo "Carte OpenRide prête : $MAP"
