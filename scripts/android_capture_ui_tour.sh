#!/usr/bin/env bash
set -euo pipefail

PACKAGE="${OPENRIDE_ANDROID_PACKAGE:-com.arthurthion.openride}"
DELAY="${OPENRIDE_CAPTURE_DELAY:-1.1}"
START_DELAY="${OPENRIDE_CAPTURE_START_DELAY:-2.0}"
OUTPUT_DIR=""
MAKE_ZIP=1
CAPTURE_CURRENT_DRIVE=0

usage() {
    cat <<'EOF'
Usage: bash scripts/android_capture_ui_tour.sh [options]

Automatically navigates through OpenRide's main Android UI and saves screenshots.
The tour uses normalized screen coordinates, so it is independent of resolution.

Options:
  --output DIR             Output directory (default: ~/Downloads/openride-ui-review-TIMESTAMP)
  --delay SECONDS          Delay after each tap (default: 1.1)
  --start-delay SECONDS    Delay after launching OpenRide (default: 2.0)
  --capture-current-drive  Capture the current screen as Drive HUD before restarting the app
  --no-zip                 Do not create a .zip archive at the end
  -h, --help               Show this help

Environment:
  ANDROID_SERIAL                  Select a device when several are connected
  OPENRIDE_ANDROID_PACKAGE        Android package name
  OPENRIDE_CAPTURE_DELAY          Default tap delay
  OPENRIDE_CAPTURE_START_DELAY    Default launch delay

Captured screens:
  01_map.png
  02_main_menu.png
  03_search.png
  04_route.png
  05_loop.png
  06_favorites.png
  07_history.png
  08_offline_maps.png
  09_settings.png

With --capture-current-drive, 00_drive_hud.png is captured before the tour.
Enter Drive mode manually first, then run the script with that option.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --output)
            [[ $# -ge 2 ]] || { echo "ERROR: --output requires a directory" >&2; exit 2; }
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --delay)
            [[ $# -ge 2 ]] || { echo "ERROR: --delay requires seconds" >&2; exit 2; }
            DELAY="$2"
            shift 2
            ;;
        --start-delay)
            [[ $# -ge 2 ]] || { echo "ERROR: --start-delay requires seconds" >&2; exit 2; }
            START_DELAY="$2"
            shift 2
            ;;
        --capture-current-drive)
            CAPTURE_CURRENT_DRIVE=1
            shift
            ;;
        --no-zip)
            MAKE_ZIP=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "ERROR: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

command -v adb >/dev/null 2>&1 || {
    echo "ERROR: adb not found in PATH" >&2
    exit 1
}

ADB=(adb)
if [[ -n "${ANDROID_SERIAL:-}" ]]; then
    ADB=(adb -s "$ANDROID_SERIAL")
else
    mapfile_supported=0
    if help mapfile >/dev/null 2>&1; then
        mapfile_supported=1
    fi

    if [[ $mapfile_supported -eq 1 ]]; then
        mapfile -t DEVICES < <(adb devices | awk '$2 == "device" {print $1}')
    else
        DEVICES=()
        while IFS= read -r serial; do
            [[ -n "$serial" ]] && DEVICES+=("$serial")
        done < <(adb devices | awk '$2 == "device" {print $1}')
    fi

    if [[ ${#DEVICES[@]} -eq 0 ]]; then
        echo "ERROR: no authorized Android device detected" >&2
        adb devices >&2 || true
        exit 1
    fi
    if [[ ${#DEVICES[@]} -gt 1 ]]; then
        echo "ERROR: multiple Android devices detected." >&2
        echo "Set ANDROID_SERIAL to select one:" >&2
        printf '  %s\n' "${DEVICES[@]}" >&2
        exit 1
    fi
    ADB=(adb -s "${DEVICES[0]}")
fi

if [[ -z "$OUTPUT_DIR" ]]; then
    OUTPUT_DIR="$HOME/Downloads/openride-ui-review-$(date +%Y%m%d-%H%M%S)"
fi
mkdir -p "$OUTPUT_DIR"

SCREEN_SIZE="$(${ADB[@]} shell wm size 2>/dev/null \
    | sed -n 's/.*size: \([0-9][0-9]*x[0-9][0-9]*\).*/\1/p' \
    | tail -n 1 \
    | tr -d '\r')"

if [[ ! "$SCREEN_SIZE" =~ ^[0-9]+x[0-9]+$ ]]; then
    echo "ERROR: unable to determine Android screen size" >&2
    exit 1
fi

SCREEN_W="${SCREEN_SIZE%x*}"
SCREEN_H="${SCREEN_SIZE#*x}"
if (( SCREEN_W >= SCREEN_H )); then
    echo "ERROR: screenshot tour currently expects portrait orientation (${SCREEN_W}x${SCREEN_H})" >&2
    exit 1
fi

pct_to_px() {
    awk -v total="$1" -v pct="$2" 'BEGIN { printf "%d", total * pct + 0.5 }'
}

tap_pct() {
    local px py
    px="$(pct_to_px "$SCREEN_W" "$1")"
    py="$(pct_to_px "$SCREEN_H" "$2")"
    "${ADB[@]}" shell input tap "$px" "$py" >/dev/null
    sleep "$DELAY"
}

capture() {
    local name="$1"
    local path="$OUTPUT_DIR/$name"
    printf '  capture %-24s' "$name"
    "${ADB[@]}" exec-out screencap -p > "$path"
    if [[ ! -s "$path" ]]; then
        echo "FAILED"
        echo "ERROR: screenshot is empty: $path" >&2
        exit 1
    fi
    echo "OK"
}

wait_first_frame() {
    local pid=""
    local deadline=$((SECONDS + 12))
    local line=""

    while [[ $SECONDS -lt $deadline ]]; do
        pid="$("${ADB[@]}" shell pidof "$PACKAGE" 2>/dev/null \
            | tr -d '\r' | awk '{print $1}')"
        if [[ -n "$pid" ]]; then
            if "${ADB[@]}" logcat -d --pid="$pid" -v brief >/dev/null 2>&1; then
                line="$("${ADB[@]}" logcat -d --pid="$pid" -v brief 2>/dev/null \
                    | grep 'AUDIT_FIRST_FRAME_READY' | tail -n 1 || true)"
            else
                line="$("${ADB[@]}" logcat -d -v brief 2>/dev/null \
                    | grep "AUDIT_FIRST_FRAME_READY pid=$pid" \
                    | tail -n 1 || true)"
            fi
            if [[ -n "$line" ]]; then
                printf '  first frame %s\n' "$line"
                return 0
            fi
        fi
        sleep 0.10
    done

    echo "ERROR: OpenRide first rendered frame was not observed" >&2
    return 1
}

launch_clean() {
    "${ADB[@]}" shell am force-stop "$PACKAGE" >/dev/null 2>&1 || true
    "${ADB[@]}" shell monkey \
        -p "$PACKAGE" \
        -c android.intent.category.LAUNCHER \
        1 >/dev/null 2>&1 || {
            echo "ERROR: unable to launch $PACKAGE" >&2
            exit 1
        }
    wait_first_frame || exit 1
    sleep "$START_DELAY"
}

# Normalized hit points based on the UI engine's portrait layout.
# Keep these centralized so future visual layout changes require one edit only.
TOOLBAR_MENU_X=0.178
TOOLBAR_SEARCH_X=0.337
TOOLBAR_ROUTE_X=0.500
TOOLBAR_LOOP_X=0.662
TOOLBAR_Y=0.938

MENU_CENTER_X=0.500
MENU_SEARCH_Y=0.380
MENU_FAVORITES_Y=0.446
MENU_HISTORY_Y=0.511
MENU_OFFLINE_Y=0.577
MENU_SETTINGS_Y=0.642

open_menu() {
    tap_pct "$TOOLBAR_MENU_X" "$TOOLBAR_Y"
}

capture_from_menu() {
    local filename="$1"
    local row_y="$2"
    launch_clean
    open_menu
    tap_pct "$MENU_CENTER_X" "$row_y"
    capture "$filename"
}

{
    echo "OpenRide Android UI screenshot tour"
    echo "Date: $(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "Package: $PACKAGE"
    echo "Screen: ${SCREEN_W}x${SCREEN_H}"
    echo "Device: $(${ADB[@]} shell getprop ro.product.model | tr -d '\r')"
    echo "Android: $(${ADB[@]} shell getprop ro.build.version.release | tr -d '\r')"
    echo "Delay: ${DELAY}s"
} > "$OUTPUT_DIR/info.txt"

printf 'OpenRide UI screenshot tour\n'
printf 'Device screen: %sx%s\n' "$SCREEN_W" "$SCREEN_H"
printf 'Output: %s\n\n' "$OUTPUT_DIR"

if [[ $CAPTURE_CURRENT_DRIVE -eq 1 ]]; then
    echo "Drive HUD"
    echo "  Capturing current screen before OpenRide is restarted."
    sleep "$DELAY"
    capture "00_drive_hud.png"
fi

echo "Map"
launch_clean
capture "01_map.png"

echo "Main menu"
launch_clean
open_menu
capture "02_main_menu.png"

echo "Search"
launch_clean
tap_pct "$TOOLBAR_SEARCH_X" "$TOOLBAR_Y"
capture "03_search.png"

echo "Route planner"
launch_clean
tap_pct "$TOOLBAR_ROUTE_X" "$TOOLBAR_Y"
capture "04_route.png"

echo "Loop / ride"
launch_clean
tap_pct "$TOOLBAR_LOOP_X" "$TOOLBAR_Y"
capture "05_loop.png"

echo "Favorites"
capture_from_menu "06_favorites.png" "$MENU_FAVORITES_Y"

echo "History"
capture_from_menu "07_history.png" "$MENU_HISTORY_Y"

echo "Offline maps"
capture_from_menu "08_offline_maps.png" "$MENU_OFFLINE_Y"

echo "Settings"
capture_from_menu "09_settings.png" "$MENU_SETTINGS_Y"

if [[ $MAKE_ZIP -eq 1 ]] && command -v zip >/dev/null 2>&1; then
    ZIP_PATH="${OUTPUT_DIR}.zip"
    (
        cd "$(dirname "$OUTPUT_DIR")"
        zip -qr "$ZIP_PATH" "$(basename "$OUTPUT_DIR")"
    )
    echo
    echo "ZIP: $ZIP_PATH"
fi

echo
echo "DONE"
echo "Screenshots: $OUTPUT_DIR"
echo "You can now upload the PNG files here for UI review."
