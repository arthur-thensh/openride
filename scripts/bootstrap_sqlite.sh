#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET_DIR="$ROOT_DIR/vendor/sqlite"
VERSION_CODE="3530400"
URL="https://www.sqlite.org/2026/sqlite-amalgamation-${VERSION_CODE}.zip"
TMP="$ROOT_DIR/vendor/sqlite-amalgamation-${VERSION_CODE}.zip"

if [ -f "$TARGET_DIR/sqlite3.c" ] && [ -f "$TARGET_DIR/sqlite3.h" ]; then
    echo "SQLite amalgamation déjà présente dans vendor/sqlite."
    exit 0
fi

for tool in curl unzip; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Erreur : $tool est introuvable." >&2
        exit 1
    fi
done

mkdir -p "$TARGET_DIR"
rm -f "$TMP"

echo "Téléchargement de SQLite 3.53.4 (amalgamation)..."
curl -L --fail --retry 3 --progress-bar "$URL" -o "$TMP"
unzip -j -o "$TMP" \
    "sqlite-amalgamation-${VERSION_CODE}/sqlite3.c" \
    "sqlite-amalgamation-${VERSION_CODE}/sqlite3.h" \
    -d "$TARGET_DIR" >/dev/null
rm -f "$TMP"

echo "SQLite installé dans vendor/sqlite."
