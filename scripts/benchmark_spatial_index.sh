#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

GRAPH=${1:-data/routing/nord-pas-de-calais.orgraph}

if [ ! -f "$GRAPH" ]; then
    echo "Erreur : graphe absent : $GRAPH" >&2
    echo "Lance d'abord ./scripts/prepare_routing_graph.sh" >&2
    exit 1
fi

if [ ! -x build/openride_spatial_benchmark ]; then
    echo "Erreur : benchmark non compile." >&2
    echo "Lance ./scripts/configure.sh puis ./scripts/build.sh" >&2
    exit 1
fi

./build/openride_spatial_benchmark "$GRAPH"
