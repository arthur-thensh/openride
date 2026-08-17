#!/bin/sh
set -eu

SDK_ROOT=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
if [ -z "$SDK_ROOT" ] && [ -d "$HOME/Library/Android/sdk" ]; then
    SDK_ROOT="$HOME/Library/Android/sdk"
fi
if [ -z "$SDK_ROOT" ]; then
    echo "Android SDK introuvable." >&2
    exit 1
fi

EMULATOR="$SDK_ROOT/emulator/emulator"
ADB="$SDK_ROOT/platform-tools/adb"
AVD_NAME=${OPENRIDE_ANDROID_AVD:-openride_pixel_9a_api36}
EMULATOR_PORT=${OPENRIDE_EMULATOR_PORT:-5554}
EMULATOR_SNAPSHOT=${OPENRIDE_EMULATOR_SNAPSHOT:-}
SERIAL="emulator-$EMULATOR_PORT"
LOG_PATH="${TMPDIR:-/tmp}/openride-$AVD_NAME-$EMULATOR_PORT.log"

if [ ! -x "$EMULATOR" ]; then
    echo "Émulateur Android absent : $EMULATOR" >&2
    exit 1
fi
if [ ! -x "$ADB" ]; then
    echo "adb absent : $ADB" >&2
    exit 1
fi
case "$EMULATOR_PORT" in
    ''|*[!0-9]*)
        echo "OPENRIDE_EMULATOR_PORT doit être un nombre pair entre 5554 et 5682." >&2
        exit 1
        ;;
esac
if [ "$EMULATOR_PORT" -lt 5554 ] || [ "$EMULATOR_PORT" -gt 5682 ] || [ $((EMULATOR_PORT % 2)) -ne 0 ]; then
    echo "OPENRIDE_EMULATOR_PORT doit être un nombre pair entre 5554 et 5682." >&2
    exit 1
fi
if ! "$EMULATOR" -list-avds | grep -Fx "$AVD_NAME" >/dev/null 2>&1; then
    echo "AVD absent : $AVD_NAME" >&2
    exit 1
fi

if "$ADB" -s "$SERIAL" get-state >/dev/null 2>&1; then
    echo "$SERIAL est déjà démarré."
else
    set -- "$EMULATOR" \
        -avd "$AVD_NAME" \
        -port "$EMULATOR_PORT" \
        -gpu host \
        -no-audio \
        -no-boot-anim
    if [ "${OPENRIDE_EMULATOR_HEADLESS:-0}" = "1" ]; then
        set -- "$@" -no-window
    fi
    if [ -n "$EMULATOR_SNAPSHOT" ]; then
        set -- "$@" -snapshot "$EMULATOR_SNAPSHOT"
    fi
    if [ "$(uname -s)" = "Darwin" ]; then
        LAUNCH_LABEL="com.openride.android-emulator-$EMULATOR_PORT"
        if launchctl print "gui/$(id -u)/$LAUNCH_LABEL" >/dev/null 2>&1; then
            echo "$AVD_NAME est déjà en cours de démarrage."
        else
            launchctl submit \
                -l "$LAUNCH_LABEL" \
                -o "$LOG_PATH" \
                -e "$LOG_PATH" \
                -- "$@"
        fi
    else
        nohup "$@" </dev/null >"$LOG_PATH" 2>&1 &
    fi
    echo "Démarrage de $AVD_NAME ($SERIAL), log : $LOG_PATH"
fi

"$ADB" -s "$SERIAL" wait-for-device
deadline=$(( $(date +%s) + 180 ))
while [ "$(date +%s)" -lt "$deadline" ]; do
    boot_completed=$("$ADB" -s "$SERIAL" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')
    if [ "$boot_completed" = "1" ]; then
        "$ADB" -s "$SERIAL" shell input keyevent 82 >/dev/null 2>&1 || true
        echo "AVD prêt : $AVD_NAME ($SERIAL)"
        echo "Utilise : ANDROID_SERIAL=$SERIAL <commande>"
        exit 0
    fi
    sleep 1
done

echo "Timeout pendant le démarrage de $SERIAL. Consulte : $LOG_PATH" >&2
exit 1
