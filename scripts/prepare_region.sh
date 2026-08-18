#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

PBF="data/osm/nord-pas-de-calais-latest.osm.pbf"

if [ ! -f "$PBF" ]; then
    echo "Données OSM absentes : $PBF" >&2
    echo "Lance d'abord : ./scripts/download_routing_data.sh" >&2
    exit 1
fi

./scripts/prepare_routing_graph.sh
./scripts/prepare_place_index.sh
./scripts/prepare_ormap.sh
./scripts/prepare_ormap11.sh

echo
echo "Région OpenRide complète :"
echo "  data/maps/nord-pas-de-calais.ormap"
echo "  data/maps/nord-pas-de-calais.ormap11"
echo "  data/routing/nord-pas-de-calais.orgraph"
echo "  data/search/nord-pas-de-calais.orplaces.sqlite"
