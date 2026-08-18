#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

PBF="data/osm/nord-pas-de-calais-latest.osm.pbf"
MAP="data/maps/nord-pas-de-calais.ormap"
PYRAMID="data/maps/nord-pas-de-calais.ormap11"
PART="$PYRAMID.part"

for file in "$PBF" "$MAP"; do
    if [ ! -f "$file" ]; then
        echo "Donnée requise absente : $file" >&2
        echo "Utilise ./scripts/prepare_region.sh pour construire toute la région." >&2
        exit 1
    fi
done

for tool in \
    build/openride_ormap_pyramid_surface_import \
    build/openride_ormap_pyramid_overlay_append; do
    if [ ! -x "$tool" ]; then
        echo "Outil non compilé : $tool" >&2
        echo "Lance : ./scripts/configure.sh puis ./scripts/build.sh" >&2
        exit 1
    fi
done

mkdir -p data/maps
rm -f "$PART"
trap 'rm -f "$PART"' 0

echo "Construction de la carte détaillée OpenRide .ormap11..."
./build/openride_ormap_pyramid_surface_import "$PBF" "$PART"
./build/openride_ormap_pyramid_overlay_append "$MAP" "$PART"
mv "$PART" "$PYRAMID"

echo
echo "Carte OpenRide détaillée prête : $PYRAMID"
