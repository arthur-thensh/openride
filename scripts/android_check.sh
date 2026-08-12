#!/bin/sh
set -eu

# OpenRide est validé avec JDK 17. Sur macOS, sélectionne automatiquement
# cette JVM lorsqu'elle est installée, même si une version plus récente est
# la JVM globale du système.
if [ "$(uname -s)" = "Darwin" ] && [ -x /usr/libexec/java_home ]; then
    JAVA17_HOME=$(/usr/libexec/java_home -v 17 2>/dev/null || true)
    if [ -n "$JAVA17_HOME" ]; then
        export JAVA_HOME="$JAVA17_HOME"
        export PATH="$JAVA_HOME/bin:$PATH"
    fi
fi

SDK_ROOT=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
if [ -z "$SDK_ROOT" ] && [ -d "$HOME/Library/Android/sdk" ]; then
    SDK_ROOT="$HOME/Library/Android/sdk"
fi

status=0
if [ -z "$SDK_ROOT" ] || [ ! -d "$SDK_ROOT" ]; then
    echo "[ERREUR] Android SDK introuvable." >&2
    echo "         Définis ANDROID_HOME/ANDROID_SDK_ROOT ou installe les Android command-line tools." >&2
    status=1
else
    echo "[OK] Android SDK : $SDK_ROOT"
fi

if command -v java >/dev/null 2>&1; then
    java_line=$(java -version 2>&1 | head -n 1)
    java_major=$(java -version 2>&1 | awk -F'[\".]' '/version/ { if ($2 == "1") print $3; else print $2; exit }')
    if [ "$java_major" = "17" ]; then
        echo "[OK] Java : $java_line"
    else
        echo "[ERREUR] OpenRide utilise JDK 17 pour le build Android (détecté : $java_line)." >&2
        echo "         Sur macOS : export JAVA_HOME=\$(/usr/libexec/java_home -v 17)" >&2
        status=1
    fi
else
    echo "[ERREUR] Java/JDK introuvable." >&2
    status=1
fi

ADB_PATH=""
if command -v adb >/dev/null 2>&1; then
    ADB_PATH=$(command -v adb)
elif [ -n "$SDK_ROOT" ] && [ -x "$SDK_ROOT/platform-tools/adb" ]; then
    ADB_PATH="$SDK_ROOT/platform-tools/adb"
fi
if [ -n "$ADB_PATH" ]; then
    echo "[OK] adb : $ADB_PATH"
else
    echo "[ERREUR] adb introuvable (package platform-tools)." >&2
    status=1
fi

if [ -n "$SDK_ROOT" ]; then
    if [ -d "$SDK_ROOT/platforms/android-35" ]; then
        echo "[OK] Android Platform 35"
    else
        echo "[ERREUR] Android Platform 35 absente." >&2
        status=1
    fi

    if [ -d "$SDK_ROOT/build-tools" ] && [ -n "$(ls "$SDK_ROOT/build-tools" 2>/dev/null | head -n 1 || true)" ]; then
        echo "[OK] Android build-tools installés"
    else
        echo "[ERREUR] Android build-tools absents." >&2
        status=1
    fi

    ndk_revision=$(python3 - "$SDK_ROOT" <<'PY'
from pathlib import Path
import re, sys
root = Path(sys.argv[1]) / "ndk"
best = None
for props in root.glob("*/source.properties") if root.exists() else []:
    text = props.read_text(errors="ignore")
    m = re.search(r"Pkg\.Revision\s*=\s*([0-9]+(?:\.[0-9]+){1,3})", text)
    if not m:
        continue
    nums = tuple(int(x) for x in m.group(1).split('.'))
    while len(nums) < 4:
        nums += (0,)
    if best is None or nums > best[0]:
        best = (nums, m.group(1))
print(best[1] if best else "")
PY
)
    if [ -n "$ndk_revision" ]; then
        ndk_ok=$(python3 - "$ndk_revision" <<'PY'
import sys
v = tuple(int(x) for x in sys.argv[1].split('.'))
v += (0,) * (2 - len(v))
print("yes" if v[:2] >= (28, 2) else "no")
PY
)
        if [ "$ndk_ok" = yes ]; then
            echo "[OK] Android NDK : $ndk_revision"
        else
            echo "[ERREUR] NDK $ndk_revision trop ancien ; SDL3 3.4.x requiert au minimum r28c (28.2)." >&2
            status=1
        fi
    else
        echo "[ERREUR] aucun Android NDK side-by-side trouvé." >&2
        status=1
    fi

    if [ -d "$SDK_ROOT/cmake" ] && [ -n "$(ls "$SDK_ROOT/cmake" 2>/dev/null | head -n 1 || true)" ]; then
        echo "[OK] CMake Android SDK installé"
    else
        echo "[ERREUR] CMake du SDK Android absent." >&2
        status=1
    fi
fi

if [ "$status" -ne 0 ]; then
    echo >&2
    echo "Une fois sdkmanager disponible, OpenRide peut installer les paquets requis avec :" >&2
    echo "  ./scripts/android_install_sdk_packages.sh" >&2
fi
exit "$status"
