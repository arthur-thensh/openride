#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
MAP_DIR="$ROOT_DIR/data/maps"
TARGET="$MAP_DIR/nord-pas-de-calais-shortbread.mbtiles"
ZIP_FILE="$MAP_DIR/nord-pas-de-calais-shortbread.zip"
URL="https://download3.bbbike.org/osm/mbtiles/region/europe/france/nord-pas-de-calais/nord-pas-de-calais.osm.mbtiles-shortbread.zip"

mkdir -p "$MAP_DIR"

if [ -f "$TARGET" ] && [ "${1:-}" != "--force" ]; then
    echo "La vraie carte OSM est déjà présente :"
    echo "  $TARGET"
    echo
    echo "Pour la retélécharger : ./scripts/download_real_map.sh --force"
    exit 0
fi

for tool in curl unzip sqlite3; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Erreur : $tool est introuvable." >&2
        exit 1
    fi
done

rm -f "$ZIP_FILE" "$TARGET.tmp"

echo "Téléchargement de la carte vectorielle OpenStreetMap Nord-Pas-de-Calais..."
echo "Source : BBBike / Shortbread (environ 100-120 Mo à télécharger)."
echo
curl -L --fail --retry 3 --progress-bar "$URL" -o "$ZIP_FILE"

MEMBER=$(unzip -Z1 "$ZIP_FILE" | awk '/\.mbtiles$/ { print; exit }')
if [ -z "$MEMBER" ]; then
    echo "Erreur : aucune base .mbtiles trouvée dans l'archive téléchargée." >&2
    rm -f "$ZIP_FILE"
    exit 1
fi

echo
echo "Extraction de $MEMBER..."
unzip -p "$ZIP_FILE" "$MEMBER" > "$TARGET.tmp"
mv "$TARGET.tmp" "$TARGET"
rm -f "$ZIP_FILE"

# Pour le prototype, démarrer directement autour de Douai.
# MBTiles n'impose pas de contrainte UNIQUE sur metadata.name, donc on fait
# UPDATE puis INSERT si nécessaire.
sqlite3 "$TARGET" <<'SQL'
UPDATE metadata SET value='pbf' WHERE name='format';
INSERT INTO metadata(name,value)
SELECT 'format','pbf'
WHERE NOT EXISTS (SELECT 1 FROM metadata WHERE name='format');

UPDATE metadata SET value='3.080200,50.370800,12' WHERE name='center';
INSERT INTO metadata(name,value)
SELECT 'center','3.080200,50.370800,12'
WHERE NOT EXISTS (SELECT 1 FROM metadata WHERE name='center');

UPDATE metadata SET value='© OpenStreetMap contributors | tiles: BBBike / Shortbread' WHERE name='attribution';
INSERT INTO metadata(name,value)
SELECT 'attribution','© OpenStreetMap contributors | tiles: BBBike / Shortbread'
WHERE NOT EXISTS (SELECT 1 FROM metadata WHERE name='attribution');
SQL

echo
echo "Carte installée :"
echo "  $TARGET"
echo
echo "Tu peux maintenant couper Internet et lancer :"
echo "  ./scripts/run.sh"
