#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

PBF="data/osm/nord-pas-de-calais-latest.osm.pbf"
GRAPH="data/routing/nord-pas-de-calais.orgraph"

if [ ! -f "$PBF" ]; then
    echo "Erreur : données OSM routières absentes : $PBF" >&2
    echo >&2
    echo "Télécharge-les d'abord avec :" >&2
    echo "  ./scripts/download_routing_data.sh" >&2
    echo >&2
    echo "Ou place manuellement un extrait .osm.pbf à cet emplacement." >&2
    exit 1
fi

if [ ! -x build/openride_osm_import ]; then
    echo "Erreur : l'importateur OpenRide n'est pas compilé." >&2
    echo "Lance d'abord :" >&2
    echo "  ./scripts/configure.sh" >&2
    echo "  ./scripts/build.sh" >&2
    exit 1
fi

mkdir -p data/routing
rm -f "$GRAPH.part"

echo "Construction du graphe routier hors ligne..."
echo "Source : $PBF"
echo "Sortie : $GRAPH"
echo "Cette étape ne nécessite aucune connexion Internet."
echo
./build/openride_osm_import "$PBF" "$GRAPH.part"
mv "$GRAPH.part" "$GRAPH"

echo
echo "Graphe prêt : $GRAPH"
echo "Le fichier contient maintenant le graphe et son index spatial."
echo "OpenRide peut calculer des itinéraires sans Internet."
echo
echo "Benchmark facultatif :"
echo "  ./scripts/benchmark_spatial_index.sh"
