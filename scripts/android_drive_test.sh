#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

PACKAGE="${OPENRIDE_ANDROID_PACKAGE:-com.arthurthion.openride}"
EMULATOR_PORT="${OPENRIDE_EMULATOR_PORT:-5554}"
SERIAL="emulator-$EMULATOR_PORT"
DRIVE_SECONDS="${OPENRIDE_DRIVE_TEST_SECONDS:-45}"
START_PLACE="${OPENRIDE_DRIVE_START_PLACE:-Douai}"
DESTINATION_PLACE="${OPENRIDE_DRIVE_DESTINATION_PLACE:-Arras}"
GPS_SIM_Y_PCT="${OPENRIDE_DRIVE_GPS_SIM_Y_PCT:-0.540}"
KEEP_EMULATOR="${OPENRIDE_DRIVE_KEEP_EMULATOR:-0}"
OUTPUT_DIR="${OPENRIDE_DRIVE_TEST_OUTPUT:-$HOME/Downloads/openride-drive-$(date +%Y%m%d-%H%M%S)}"

case "$EMULATOR_PORT" in
    ''|*[!0-9]*)
        echo "ERROR: OPENRIDE_EMULATOR_PORT doit être numérique." >&2
        exit 2
        ;;
esac
case "$DRIVE_SECONDS" in
    ''|*[!0-9]*)
        echo "ERROR: OPENRIDE_DRIVE_TEST_SECONDS doit être un entier." >&2
        exit 2
        ;;
esac
if [ "$DRIVE_SECONDS" -lt 15 ] || [ "$DRIVE_SECONDS" -gt 75 ]; then
    echo "ERROR: OPENRIDE_DRIVE_TEST_SECONDS doit être compris entre 15 et 75 secondes." >&2
    exit 2
fi
case "$SERIAL" in
    emulator-*) ;;
    *)
        echo "ERROR: ce test refuse toute cible Android physique ($SERIAL)." >&2
        exit 2
        ;;
esac

SDK_ROOT=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
if [ -z "$SDK_ROOT" ] && [ -d "$HOME/Library/Android/sdk" ]; then
    SDK_ROOT="$HOME/Library/Android/sdk"
fi
if [ -n "$SDK_ROOT" ] && [ -d "$SDK_ROOT/platform-tools" ]; then
    PATH="$SDK_ROOT/platform-tools:$PATH"
fi
export PATH

command -v adb >/dev/null 2>&1 || {
    echo "ERROR: adb introuvable." >&2
    exit 1
}

LOG_DIR="$OUTPUT_DIR/logs"
SCREEN_DIR="$OUTPUT_DIR/screenshots"
METRIC_DIR="$OUTPUT_DIR/metrics"
DEVICE_DIR="$OUTPUT_DIR/device"
VIDEO_DIR="$OUTPUT_DIR/videos"
mkdir -p "$LOG_DIR" "$SCREEN_DIR/drive-mode" "$METRIC_DIR" "$DEVICE_DIR" "$VIDEO_DIR"

# Reuse the Android helpers maintained by global_audit.sh. Delimit the sourced
# block with executable code rather than comments so formatting changes cannot
# silently produce an empty helper set.
source <(
    awk '
        /^ADB=\(\)$/ {inside=1}
        /^make_manifest\(\)/ {exit}
        inside {print}
    ' "$SCRIPT_DIR/global_audit.sh"
)

for helper in \
    setup_adb android_device_info android_grant_test_permissions \
    android_logcat_clear android_launch_clean android_pid android_log_count \
    android_wait_log_count android_capture android_zoom_sweep_video_start \
    android_zoom_sweep_video_stop android_runtime_metric_sample \
    android_logcat_collect android_crash_scan; do
    if ! declare -F "$helper" >/dev/null 2>&1; then
        echo "ERROR: helper Android manquant après chargement: $helper" >&2
        echo "Vérifie scripts/global_audit.sh avant de relancer le build." >&2
        exit 1
    fi
done

EMULATOR_STARTED_BY_TEST=0
VIDEO_ACTIVE=0
VIDEO_PATH="$VIDEO_DIR/android_drive_mode.mp4"
DRIVE_PID=""
TEST_RC=0
FAIL_REASON=""

cleanup() {
    local rc=$?
    set +e
    if [ "$VIDEO_ACTIVE" -eq 1 ]; then
        android_zoom_sweep_video_stop "$VIDEO_PATH" >/dev/null 2>&1 || true
        VIDEO_ACTIVE=0
    fi
    if [ "$EMULATOR_STARTED_BY_TEST" -eq 1 ] && [ "$KEEP_EMULATOR" != "1" ]; then
        "$SCRIPT_DIR/android_emulator_stop.sh" >/dev/null 2>&1 || true
    fi
    if [ "$rc" -ne 0 ]; then
        echo "Drive test interrompu (rc=$rc). Résultats partiels: $OUTPUT_DIR" >&2
    fi
}
trap cleanup EXIT INT TERM

pct_px() {
    awk -v total="$1" -v pct="$2" 'BEGIN {printf "%d", total * pct + 0.5}'
}

adb_input_text() {
    local text="$1"
    text="${text// /%s}"
    "${ADB[@]}" shell input text "$text" >/dev/null
}

wait_marker() {
    local pattern="$1"
    local previous="$2"
    local timeout="${3:-12}"
    android_wait_log_count "$DRIVE_PID" "$pattern" $((previous + 1)) "$timeout"
}

capture_drive() {
    android_capture "$SCREEN_DIR/drive-mode/$1"
}

write_telemetry() {
    android_logcat_for_pid "$DRIVE_PID" \
        | grep -E 'AUDIT_ROUTE_SIM_READY|AUDIT_SIM_GPS_|AUDIT_DRIVE_MODE_ACTIVE|AUDIT_DRIVE_STATE' \
        > "$METRIC_DIR/drive_telemetry.log" || true
}

validate_telemetry() {
    local telemetry="$METRIC_DIR/drive_telemetry.log"
    [ -s "$telemetry" ] || {
        FAIL_REASON="aucune télémétrie Drive"
        return 1
    }

    local sim_started drive_active gps_samples drive_samples
    sim_started="$(grep -c 'AUDIT_SIM_GPS_STARTED' "$telemetry" || true)"
    drive_active="$(grep -c 'AUDIT_DRIVE_MODE_ACTIVE active=1' "$telemetry" || true)"
    gps_samples="$(grep -c 'AUDIT_SIM_GPS_SAMPLE' "$telemetry" || true)"
    drive_samples="$(grep -c 'AUDIT_DRIVE_STATE' "$telemetry" || true)"

    {
        echo "sim_started=$sim_started"
        echo "drive_active=$drive_active"
        echo "gps_samples=$gps_samples"
        echo "drive_samples=$drive_samples"
    } > "$METRIC_DIR/drive_validation.txt"

    [ "$sim_started" -ge 1 ] || { FAIL_REASON="GPS simulé non démarré"; return 1; }
    [ "$drive_active" -ge 1 ] || { FAIL_REASON="Drive Mode non activé"; return 1; }
    [ "$gps_samples" -ge 3 ] || { FAIL_REASON="moins de 3 échantillons GPS"; return 1; }
    [ "$drive_samples" -ge 3 ] || { FAIL_REASON="moins de 3 états caméra Drive"; return 1; }

    local first_position last_position progress
    first_position="$(grep 'AUDIT_SIM_GPS_SAMPLE' "$telemetry" | head -n 1 \
        | sed -n 's/.*route_position_m=\([0-9.][0-9.]*\).*/\1/p')"
    last_position="$(grep 'AUDIT_SIM_GPS_SAMPLE' "$telemetry" | tail -n 1 \
        | sed -n 's/.*route_position_m=\([0-9.][0-9.]*\).*/\1/p')"
    progress="$(awk -v a="${first_position:-0}" -v b="${last_position:-0}" 'BEGIN {printf "%.1f", b-a}')"
    {
        echo "first_route_position_m=${first_position:-0}"
        echo "last_route_position_m=${last_position:-0}"
        echo "observed_progress_m=$progress"
    } >> "$METRIC_DIR/drive_validation.txt"

    awk -v p="$progress" 'BEGIN {exit !(p >= 100.0)}' || {
        FAIL_REASON="progression GPS inférieure à 100 m"
        return 1
    }

    local zoom_min zoom_max
    read -r zoom_min zoom_max < <(
        grep 'AUDIT_DRIVE_STATE' "$telemetry" \
            | sed -n 's/.*camera_zoom=\([0-9.][0-9.]*\).*/\1/p' \
            | awk 'NR==1 {min=$1; max=$1} {if ($1<min) min=$1; if ($1>max) max=$1} END {printf "%.3f %.3f\n", min, max}'
    )
    {
        echo "camera_zoom_min=${zoom_min:-0}"
        echo "camera_zoom_max=${zoom_max:-0}"
    } >> "$METRIC_DIR/drive_validation.txt"

    awk -v z="${zoom_min:-0}" 'BEGIN {exit !(z >= 16.4)}' || {
        FAIL_REASON="zoom caméra inférieur à la plage Drive"
        return 1
    }
    awk -v z="${zoom_max:-99}" 'BEGIN {exit !(z <= 19.1)}' || {
        FAIL_REASON="zoom caméra supérieur à la plage Drive"
        return 1
    }

    local first_lat first_lon last_lat last_lon movement
    first_lat="$(grep 'AUDIT_DRIVE_STATE' "$telemetry" | head -n 1 \
        | sed -n 's/.*camera_lat=\([-0-9.][0-9.-]*\).*/\1/p')"
    first_lon="$(grep 'AUDIT_DRIVE_STATE' "$telemetry" | head -n 1 \
        | sed -n 's/.*camera_lon=\([-0-9.][0-9.-]*\).*/\1/p')"
    last_lat="$(grep 'AUDIT_DRIVE_STATE' "$telemetry" | tail -n 1 \
        | sed -n 's/.*camera_lat=\([-0-9.][0-9.-]*\).*/\1/p')"
    last_lon="$(grep 'AUDIT_DRIVE_STATE' "$telemetry" | tail -n 1 \
        | sed -n 's/.*camera_lon=\([-0-9.][0-9.-]*\).*/\1/p')"
    movement="$(awk -v a="${first_lat:-0}" -v b="${last_lat:-0}" -v c="${first_lon:-0}" -v d="${last_lon:-0}" \
        'BEGIN {x=b-a; if (x<0) x=-x; y=d-c; if (y<0) y=-y; printf "%.8f", x+y}')"
    echo "camera_coordinate_delta=$movement" >> "$METRIC_DIR/drive_validation.txt"
    awk -v m="$movement" 'BEGIN {exit !(m > 0.00001)}' || {
        FAIL_REASON="la caméra Drive ne s'est pas déplacée"
        return 1
    }

    return 0
}

write_report() {
    local status="$1"
    local video_bytes=0
    [ -f "$VIDEO_PATH" ] && video_bytes="$(wc -c < "$VIDEO_PATH" | tr -d ' ')"
    {
        echo "# OpenRide Android Drive Mode test"
        echo
        echo "- Status: **$status**"
        echo "- Target: \`$SERIAL\` (emulator only)"
        echo "- Route search: \`$START_PLACE\` → \`$DESTINATION_PLACE\`"
        echo "- Observation: ${DRIVE_SECONDS}s"
        echo "- Video bytes: $video_bytes"
        [ -z "$FAIL_REASON" ] || echo "- Failure: $FAIL_REASON"
        echo
        echo "## Evidence"
        echo
        echo "- \`videos/android_drive_mode.mp4\`"
        echo "- \`screenshots/drive-mode/\`"
        echo "- \`metrics/drive_telemetry.log\`"
        echo "- \`metrics/drive_validation.txt\`"
        echo "- \`metrics/android_process_drive_*.txt\`"
        echo "- \`metrics/android_meminfo_drive_*.txt\`"
        echo "- \`metrics/android_gfxinfo_drive_*.txt\`"
        echo "- \`device/logcat_full.txt\`"
        echo "- \`device/logcat_relevant.txt\`"
    } > "$OUTPUT_DIR/report.md"
}

echo "=============================================================="
echo " OpenRide Android Drive Mode test"
echo "=============================================================="
echo "Target : $SERIAL"
echo "Route  : $START_PLACE -> $DESTINATION_PLACE"
echo "Output : $OUTPUT_DIR"
echo

if adb -s "$SERIAL" get-state >/dev/null 2>&1; then
    echo "$SERIAL est déjà démarré; il ne sera pas arrêté automatiquement."
else
    EMULATOR_STARTED_BY_TEST=1
fi

"$SCRIPT_DIR/android_emulator_start.sh"
export ANDROID_SERIAL="$SERIAL"

# Existing authoritative project pipeline.
"$SCRIPT_DIR/android_build.sh"
"$SCRIPT_DIR/android_install.sh"
"$SCRIPT_DIR/android_push_data.sh"

setup_adb
android_device_info
android_grant_test_permissions
android_logcat_clear
android_launch_clean
DRIVE_PID="$(android_pid)"
[ -n "$DRIVE_PID" ] || {
    echo "ERROR: processus OpenRide introuvable." >&2
    exit 1
}

echo "OpenRide pid=$DRIVE_PID screen=${SCREEN_W}x${SCREEN_H}"

# Real UI flow: Route -> search start -> search destination -> calculate.
TOOLBAR_Y="$(pct_px "$SCREEN_H" 0.938)"
ROUTE_X="$(pct_px "$SCREEN_W" 0.500)"
MENU_X="$(pct_px "$SCREEN_W" 0.178)"
CENTER_X="$(pct_px "$SCREEN_W" 0.500)"
SETTINGS_Y="$(pct_px "$SCREEN_H" 0.642)"
GPS_SIM_Y="$(pct_px "$SCREEN_H" "$GPS_SIM_Y_PCT")"

"${ADB[@]}" shell input tap "$ROUTE_X" "$TOOLBAR_Y" >/dev/null
sleep 0.50
"${ADB[@]}" shell input keyevent KEYCODE_D >/dev/null
sleep 0.35
adb_input_text "$START_PLACE"
sleep 0.80
"${ADB[@]}" shell input keyevent KEYCODE_ENTER >/dev/null
sleep 0.70
"${ADB[@]}" shell input keyevent KEYCODE_A >/dev/null
sleep 0.35
adb_input_text "$DESTINATION_PLACE"
sleep 0.80
"${ADB[@]}" shell input keyevent KEYCODE_ENTER >/dev/null
sleep 0.70

ROUTE_READY_BEFORE="$(android_log_count "$DRIVE_PID" 'AUDIT_ROUTE_SIM_READY')"
"${ADB[@]}" shell input keyevent KEYCODE_ENTER >/dev/null
if ! wait_marker 'AUDIT_ROUTE_SIM_READY' "$ROUTE_READY_BEFORE" 20; then
    capture_drive "00_route_failed.png" || true
    write_telemetry
    FAIL_REASON="la route de référence n'a pas atteint AUDIT_ROUTE_SIM_READY"
    write_report "FAIL"
    echo "ERROR: $FAIL_REASON" >&2
    exit 1
fi
capture_drive "01_route_ready.png"

# Open Settings through the same toolbar/menu path used by the Android UI audit.
"${ADB[@]}" shell input tap "$MENU_X" "$TOOLBAR_Y" >/dev/null
sleep 0.45
"${ADB[@]}" shell input tap "$CENTER_X" "$SETTINGS_Y" >/dev/null
sleep 0.55
capture_drive "02_settings_before_simulation.png"

SIM_STARTED_BEFORE="$(android_log_count "$DRIVE_PID" 'AUDIT_SIM_GPS_STARTED')"
DRIVE_ACTIVE_BEFORE="$(android_log_count "$DRIVE_PID" 'AUDIT_DRIVE_MODE_ACTIVE active=1')"
DRIVE_STATE_BEFORE="$(android_log_count "$DRIVE_PID" 'AUDIT_DRIVE_STATE')"

android_zoom_sweep_video_start "$VIDEO_PATH"
VIDEO_ACTIVE=1
sleep 0.30
"${ADB[@]}" shell input tap "$CENTER_X" "$GPS_SIM_Y" >/dev/null

if ! wait_marker 'AUDIT_SIM_GPS_STARTED' "$SIM_STARTED_BEFORE" 10 \
   || ! wait_marker 'AUDIT_DRIVE_MODE_ACTIVE active=1' "$DRIVE_ACTIVE_BEFORE" 10 \
   || ! wait_marker 'AUDIT_DRIVE_STATE' "$DRIVE_STATE_BEFORE" 10; then
    capture_drive "03_drive_activation_failed.png" || true
    write_telemetry
    FAIL_REASON="le GPS simulé / Drive Mode ne s'est pas activé; vérifie OPENRIDE_DRIVE_GPS_SIM_Y_PCT si le layout AVD a changé"
    android_zoom_sweep_video_stop "$VIDEO_PATH" || true
    VIDEO_ACTIVE=0
    android_logcat_collect || true
    write_report "FAIL"
    echo "ERROR: $FAIL_REASON" >&2
    exit 1
fi

capture_drive "03_drive_started.png"
android_runtime_metric_sample "drive_t00"

FIRST_SLEEP=$((DRIVE_SECONDS / 2))
SECOND_SLEEP=$((DRIVE_SECONDS - FIRST_SLEEP))
sleep "$FIRST_SLEEP"
capture_drive "04_drive_mid.png"
android_runtime_metric_sample "drive_mid"
sleep "$SECOND_SLEEP"
capture_drive "05_drive_end.png"
android_runtime_metric_sample "drive_end"

android_zoom_sweep_video_stop "$VIDEO_PATH"
VIDEO_ACTIVE=0

write_telemetry
android_logcat_collect

if ! validate_telemetry; then
    TEST_RC=1
fi
if ! android_crash_scan > "$LOG_DIR/android_drive_crash_scan.log" 2>&1; then
    TEST_RC=1
    [ -n "$FAIL_REASON" ] || FAIL_REASON="signature crash/FATAL/ANR/OOM détectée"
fi

if [ "$TEST_RC" -eq 0 ]; then
    write_report "PASS"
    echo
    echo "PASS: Drive Mode a progressé avec GPS simulé pendant ${DRIVE_SECONDS}s."
else
    write_report "FAIL"
    echo
    echo "FAIL: ${FAIL_REASON:-validation Drive échouée}" >&2
fi

echo "Video  : $VIDEO_PATH"
echo "Report : $OUTPUT_DIR/report.md"
echo "Logs   : $DEVICE_DIR/logcat_relevant.txt"
echo

if [ "$EMULATOR_STARTED_BY_TEST" -eq 1 ] && [ "$KEEP_EMULATOR" != "1" ]; then
    "$SCRIPT_DIR/android_emulator_stop.sh"
    EMULATOR_STARTED_BY_TEST=0
fi

exit "$TEST_RC"
