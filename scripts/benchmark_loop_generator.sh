#!/bin/sh
set -eu

GRAPH="${1:-data/routing/nord-pas-de-calais.orgraph}"
LAT="${2:-50.3708}"
LON="${3:-3.0802}"
DISTANCE_KM="${4:-100}"
PROFILE="${5:-touring}"

if [ ! -x ./build/openride_loop_benchmark ]; then
    echo "Benchmark non compile. Lance d'abord ./scripts/configure.sh puis ./scripts/build.sh"
    exit 1
fi

exec ./build/openride_loop_benchmark "$GRAPH" "$LAT" "$LON" "$DISTANCE_KM" "$PROFILE"
