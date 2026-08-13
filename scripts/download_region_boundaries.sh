#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

BASE_URL="https://download.geofabrik.de/europe/france"

if ! command -v curl >/dev/null 2>&1; then
    echo "Erreur : curl est introuvable." >&2
    exit 1
fi

mkdir -p data/maps

download_one() {
    slug=$1
    output="data/maps/${slug}.poly"
    url="${BASE_URL}/${slug}.poly"

    if [ -f "$output" ]; then
        echo "Contour deja present : $output"
        return 0
    fi

    echo "Telechargement du contour Geofabrik : $slug"
    if curl -L --fail --progress-bar "$url" -o "$output.part"; then
        mv "$output.part" "$output"
        echo "  -> $output"
    else
        rm -f "$output.part"
        echo "Avertissement : contour indisponible pour $slug" >&2
        return 1
    fi
}

failures=0
if [ "$#" -gt 0 ]; then
    for slug in "$@"; do
        if ! download_one "$slug"; then
            failures=$((failures + 1))
        fi
    done
else
    found=0
    for map in data/maps/*.ormap; do
        [ -f "$map" ] || continue
        found=1
        name=$(basename "$map")
        slug=${name%.ormap}
        if ! download_one "$slug"; then
            failures=$((failures + 1))
        fi
    done
    if [ "$found" -eq 0 ]; then
        echo "Aucune region .ormap installee dans data/maps/."
    fi
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures contour(s) n'ont pas pu etre telecharges." >&2
    exit 1
fi
