#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

URL="https://download.geofabrik.de/europe/france/nord-pas-de-calais-latest.osm.pbf"
OUTPUT="data/osm/nord-pas-de-calais-latest.osm.pbf"

mkdir -p data/osm

if [ -f "$OUTPUT" ]; then
    echo "Données OSM déjà présentes : $OUTPUT"
    exit 0
fi

if ! command -v curl >/dev/null 2>&1; then
    echo "Erreur : curl est introuvable." >&2
    exit 1
fi

echo "Téléchargement des données routières OpenStreetMap..."
echo "Source : Geofabrik / Nord-Pas-de-Calais"
echo "Fichier : $OUTPUT"
echo
curl -L --fail --progress-bar "$URL" -o "$OUTPUT.part"
mv "$OUTPUT.part" "$OUTPUT"

echo
echo "Téléchargement terminé."
echo "Tu peux maintenant lancer : ./scripts/prepare_routing_graph.sh"
