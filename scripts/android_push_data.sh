#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PACKAGE="com.arthurthion.openride"
# `run-as` démarre dans /data/user/0/<package>. Le répertoire `files`
# correspond au stockage interne retourné par Context.getFilesDir() et par
# SDL_GetAndroidInternalStoragePath().
REMOTE="files/data"
SDK_ROOT=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
if [ -z "$SDK_ROOT" ] && [ -d "$HOME/Library/Android/sdk" ]; then
    SDK_ROOT="$HOME/Library/Android/sdk"
fi
ADB=${ADB:-adb}
if ! command -v "$ADB" >/dev/null 2>&1 && [ -n "$SDK_ROOT" ] && [ -x "$SDK_ROOT/platform-tools/adb" ]; then
    ADB="$SDK_ROOT/platform-tools/adb"
fi
if ! command -v "$ADB" >/dev/null 2>&1; then
    echo "adb introuvable." >&2
    exit 1
fi

MAP="$ROOT_DIR/data/maps/nord-pas-de-calais.ormap"
GRAPH="$ROOT_DIR/data/routing/nord-pas-de-calais.orgraph"
SEARCH="$ROOT_DIR/data/search/nord-pas-de-calais.orplaces.sqlite"

for file in "$MAP" "$GRAPH" "$SEARCH"; do
    if [ ! -f "$file" ]; then
        echo "Donnée absente : $file" >&2
        exit 1
    fi
done

"$ADB" get-state >/dev/null
if ! "$ADB" shell pm path "$PACKAGE" >/dev/null 2>&1; then
    echo "OpenRide n'est pas installé sur le téléphone. Lance d'abord : ./scripts/android_install.sh" >&2
    exit 1
fi

if ! "$ADB" shell run-as "$PACKAGE" true >/dev/null 2>&1; then
    echo "Impossible d'utiliser run-as pour $PACKAGE." >&2
    echo "Vérifie que l'APK debug OpenRide est bien installé." >&2
    exit 1
fi

"$ADB" shell run-as "$PACKAGE" mkdir -p \
    "$REMOTE/maps" \
    "$REMOTE/routing" \
    "$REMOTE/search" \
    "$REMOTE/gpx"

push_app_file() {
    local_file=$1
    remote_dir=$2
    remote_name=$(basename "$local_file")
    temporary="/data/local/tmp/openride-$remote_name"

    "$ADB" push "$local_file" "$temporary"

    # Même méthode que le workflow Android NDK de débogage sans root :
    # dépôt temporaire par adb, puis copie par `run-as` dans l'espace privé.
    "$ADB" shell run-as "$PACKAGE" cp "$temporary" "$remote_dir/$remote_name"
    "$ADB" shell rm -f "$temporary"
}

echo "Copie de la carte .ormap..."
push_app_file "$MAP" "$REMOTE/maps"
echo "Copie du graphe de routage..."
push_app_file "$GRAPH" "$REMOTE/routing"
echo "Copie de l'index de recherche..."
push_app_file "$SEARCH" "$REMOTE/search"

echo
echo "Vérification des fichiers installés..."
"$ADB" shell run-as "$PACKAGE" ls -lh \
    "$REMOTE/maps/nord-pas-de-calais.ormap" \
    "$REMOTE/routing/nord-pas-de-calais.orgraph" \
    "$REMOTE/search/nord-pas-de-calais.orplaces.sqlite"

echo
echo "Données hors ligne installées dans le stockage interne OpenRide."
echo "Lancement d'OpenRide..."
"$ADB" shell am start -n "$PACKAGE/.OpenRideActivity"
