#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

DEFAULT_PBF="$ROOT_DIR/data/osm/france-latest.osm.pbf"
PBF_PATH=${1:-${OPENRIDE_FRANCE_PBF:-$DEFAULT_PBF}}
PBF_URL=${OPENRIDE_FRANCE_PBF_URL:-https://download.geofabrik.de/europe/france-latest.osm.pbf}
OUTPUT="$ROOT_DIR/src/map/france_overview_network_data.inc"
TMP_OUTPUT="$OUTPUT.tmp"

mkdir -p "$ROOT_DIR/data/osm"

if [ ! -f "$PBF_PATH" ]; then
    if [ "$#" -ge 1 ]; then
        echo "ERROR: PBF introuvable: $PBF_PATH" >&2
        exit 1
    fi

    command -v curl >/dev/null 2>&1 || {
        echo "ERROR: curl est requis pour télécharger le PBF France." >&2
        exit 1
    }

    echo "Téléchargement du PBF France Geofabrik (développement uniquement)."
    echo "URL : $PBF_URL"
    echo "Cible : $PBF_PATH"
    echo
    echo "Le téléchargement est volumineux et reprend automatiquement si possible."

    PART="$PBF_PATH.part"
    curl \
        --fail \
        --location \
        --continue-at - \
        --progress-bar \
        --output "$PART" \
        "$PBF_URL"
    mv "$PART" "$PBF_PATH"
fi

if [ ! -f build/CMakeCache.txt ]; then
    echo "Configuration CMake absente; lancement de ./scripts/configure.sh"
    ./scripts/configure.sh
fi

echo
echo "Compilation du générateur France Overview..."
cmake --build build --target openride_france_overview_import -j

echo
echo "Génération de la côte + motorway/trunk/primary..."
rm -f "$TMP_OUTPUT"
./build/openride_france_overview_import \
    "$PBF_PATH" \
    "$TMP_OUTPUT"

[ -s "$TMP_OUTPUT" ] || {
    echo "ERROR: le générateur n'a produit aucune donnée." >&2
    rm -f "$TMP_OUTPUT"
    exit 1
}

mv "$TMP_OUTPUT" "$OUTPUT"

echo
echo "Atlas généré:"
ls -lh "$OUTPUT"

echo
echo "Recompilation d'OpenRide avec l'atlas embarqué..."
cmake --build build -j

if [ "${OPENRIDE_FRANCE_DELETE_PBF:-0}" = "1" ]; then
    echo
    echo "Suppression du PBF source demandée."
    rm -f "$PBF_PATH"
fi

echo
echo "France Overview Road Atlas: OK"
echo "Suite:"
echo "  ./scripts/test.sh"
echo "  ./scripts/android_build.sh"
echo "  ./scripts/android_install.sh"
echo "  ./scripts/android_capture_france_overview.sh"
