#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

SOURCE="data/osm/nord-pas-de-calais-latest.osm.pbf"
OUTPUT_DIR="data/search"
OUTPUT="$OUTPUT_DIR/nord-pas-de-calais.orplaces.sqlite"
PART="$OUTPUT.part"

if [ ! -x build/openride_place_import ]; then
    echo "L'importateur de lieux n'est pas compile." >&2
    echo "Lance : ./scripts/configure.sh puis ./scripts/build.sh" >&2
    exit 1
fi

if [ ! -f "$SOURCE" ]; then
    echo "Donnees OSM absentes : $SOURCE" >&2
    echo "Lance d'abord : ./scripts/download_routing_data.sh" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
rm -f "$PART"

echo "Construction de l'index de recherche hors ligne..."
echo "Source : $SOURCE"
echo "Sortie : $OUTPUT"
echo "Cette etape ne necessite aucune connexion Internet."
echo

./build/openride_place_import "$SOURCE" "$PART"
mv "$PART" "$OUTPUT"

echo
echo "Index de recherche pret : $OUTPUT"
echo "Dans OpenRide, appuie sur / pour rechercher un lieu."
