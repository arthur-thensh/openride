#!/usr/bin/env bash
set -u

PACKAGE="${OPENRIDE_ANDROID_PACKAGE:-com.arthurthion.openride}"
OUTPUT_DIR=""
MAKE_ZIP=1
PROFILE_TIMEOUT="${OPENRIDE_PROFILE_TIMEOUT:-120}"

usage() {
    cat <<'EOF'
Usage: bash scripts/android_profile_ormap.sh [options]

Targeted OpenRide ORMap frame profiler.
Launches the Android app, waits for the ten V3.4.2 profiled frames, collects
per-layer timings, cache counters, thread CPU snapshots and screenshots.

Options:
  --output DIR       Output directory
  --timeout SECONDS  Maximum wait for all profiled frames (default: 120)
  --no-zip           Do not create a ZIP archive
  -h, --help         Show this help

Environment:
  ANDROID_SERIAL
  OPENRIDE_ANDROID_PACKAGE
  OPENRIDE_PROFILE_TIMEOUT
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --output)
            [ "$#" -ge 2 ] || { echo "ERROR: --output requires a path" >&2; exit 2; }
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --timeout)
            [ "$#" -ge 2 ] || { echo "ERROR: --timeout requires seconds" >&2; exit 2; }
            PROFILE_TIMEOUT="$2"
            shift 2
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
    echo "ERROR: adb not found" >&2
    exit 1
}

ADB=(adb)
if [ -n "${ANDROID_SERIAL:-}" ]; then
    ADB=(adb -s "$ANDROID_SERIAL")
else
    DEVICES=()
    while IFS= read -r serial; do
        [ -n "$serial" ] && DEVICES[${#DEVICES[@]}]="$serial"
    done <<EOF
$(adb devices | awk '$2 == "device" {print $1}')
EOF

    if [ "${#DEVICES[@]}" -eq 0 ]; then
        echo "ERROR: no authorized Android device" >&2
        exit 1
    fi
    if [ "${#DEVICES[@]}" -gt 1 ]; then
        echo "ERROR: multiple Android devices; set ANDROID_SERIAL" >&2
        printf '  %s\n' "${DEVICES[@]}" >&2
        exit 1
    fi
    ADB=(adb -s "${DEVICES[0]}")
fi

if [ -z "$OUTPUT_DIR" ]; then
    OUTPUT_DIR="$HOME/Downloads/openride-ormap-profile-$(date +%Y%m%d-%H%M%S)"
fi

mkdir -p "$OUTPUT_DIR/screenshots" "$OUTPUT_DIR/threads"

logcat_for_pid() {
    local pid="$1"
    if "${ADB[@]}" logcat -d --pid="$pid" -v brief >/dev/null 2>&1; then
        "${ADB[@]}" logcat -d --pid="$pid" -v brief 2>/dev/null
    else
        "${ADB[@]}" logcat -d -v brief 2>/dev/null | grep "pid=$pid" || true
    fi
}

capture() {
    local path="$1"
    "${ADB[@]}" exec-out screencap -p > "$path"
    [ -s "$path" ]
}

sample_threads() {
    local label="$1"
    local pid="$2"

    {
        echo "label=$label"
        echo "pid=$pid"
        echo
        echo "PROCESS:"
        "${ADB[@]}" shell top -b -n 1 2>/dev/null | grep "$PACKAGE" || true
        echo
        echo "THREADS:"
        if "${ADB[@]}" shell top -H -b -n 1 -p "$pid" 2>/dev/null; then
            :
        else
            "${ADB[@]}" shell top -H -b -n 1 2>/dev/null \
                | grep -E "$PACKAGE|SDLThread|PID|TID|CPU" || true
        fi
    } > "$OUTPUT_DIR/threads/${label}.txt"
}

echo "OpenRide ORMap frame profiler"
echo "Output: $OUTPUT_DIR"
echo

"${ADB[@]}" logcat -c >/dev/null 2>&1 || true
"${ADB[@]}" shell am force-stop "$PACKAGE" >/dev/null 2>&1 || true

"${ADB[@]}" shell monkey \
    -p "$PACKAGE" \
    -c android.intent.category.LAUNCHER \
    1 >/dev/null 2>&1 || {
        echo "ERROR: unable to launch $PACKAGE" >&2
        exit 1
    }

PID=""
deadline=$((SECONDS + 15))
while [ "$SECONDS" -lt "$deadline" ]; do
    PID="$("${ADB[@]}" shell pidof "$PACKAGE" 2>/dev/null \
        | tr -d '\r' | awk '{print $1}')"
    [ -n "$PID" ] && break
    sleep 0.10
done

if [ -z "$PID" ]; then
    echo "ERROR: OpenRide process did not start" >&2
    exit 1
fi

echo "PID: $PID"

first_deadline=$((SECONDS + 45))
FIRST_FRAME=""
while [ "$SECONDS" -lt "$first_deadline" ]; do
    FIRST_FRAME="$(logcat_for_pid "$PID" \
        | grep 'AUDIT_FIRST_FRAME_READY' \
        | tail -n 1 || true)"
    [ -n "$FIRST_FRAME" ] && break
    sleep 0.10
done

if [ -z "$FIRST_FRAME" ]; then
    echo "ERROR: first rendered frame not observed within 45 seconds" >&2
else
    echo "$FIRST_FRAME"
    capture "$OUTPUT_DIR/screenshots/01_first_frame.png" || true
fi

sample_threads "t_first" "$PID"

PROFILE_DONE=0
SAMPLED_5=0
SAMPLED_15=0
SAMPLED_30=0
wait_started="$SECONDS"
deadline=$((SECONDS + PROFILE_TIMEOUT))

while [ "$SECONDS" -lt "$deadline" ]; do
    elapsed=$((SECONDS - wait_started))

    if [ "$SAMPLED_5" -eq 0 ] && [ "$elapsed" -ge 5 ]; then
        sample_threads "t_plus_05s" "$PID"
        SAMPLED_5=1
    fi
    if [ "$SAMPLED_15" -eq 0 ] && [ "$elapsed" -ge 15 ]; then
        sample_threads "t_plus_15s" "$PID"
        SAMPLED_15=1
    fi
    if [ "$SAMPLED_30" -eq 0 ] && [ "$elapsed" -ge 30 ]; then
        sample_threads "t_plus_30s" "$PID"
        SAMPLED_30=1
    fi

    done_line="$(logcat_for_pid "$PID" \
        | grep 'AUDIT_FRAME_PROFILE_DONE' \
        | tail -n 1 || true)"
    if [ -n "$done_line" ]; then
        echo "$done_line"
        PROFILE_DONE=1
        break
    fi

    sleep 0.25
done

sample_threads "t_final" "$PID"
capture "$OUTPUT_DIR/screenshots/02_profile_done.png" || true

"${ADB[@]}" shell dumpsys meminfo "$PACKAGE" \
    > "$OUTPUT_DIR/meminfo.txt" 2>&1 || true
"${ADB[@]}" shell dumpsys gfxinfo "$PACKAGE" \
    > "$OUTPUT_DIR/gfxinfo.txt" 2>&1 || true

logcat_for_pid "$PID" > "$OUTPUT_DIR/logcat_pid.txt" || true
grep 'AUDIT_FRAME_PROFILE idx=' "$OUTPUT_DIR/logcat_pid.txt" \
    > "$OUTPUT_DIR/frame_profile.txt" || true
grep 'AUDIT_FRAME_PROFILE_ROADS' "$OUTPUT_DIR/logcat_pid.txt" \
    > "$OUTPUT_DIR/road_profile.txt" || true
grep 'AUDIT_FRAME_PROFILE_AREAS' "$OUTPUT_DIR/logcat_pid.txt" \
    > "$OUTPUT_DIR/area_profile.txt" || true
grep 'AUDIT_LABEL_STATS' "$OUTPUT_DIR/logcat_pid.txt" \
    > "$OUTPUT_DIR/label_profile.txt" || true
grep 'AUDIT_FRAME_PACING' "$OUTPUT_DIR/logcat_pid.txt" \
    > "$OUTPUT_DIR/frame_pacing.txt" || true

awk '
BEGIN {
    print "idx\tzoom\tframe_ms\tupdate_ms\tmap_ms\tui_ms\tpresent_ms\tworld_detail_ms\tmasks_ms\tareas_layer_ms\twaterways_ms\troads_layer_ms\tlabels_ms\tregions"
}
/AUDIT_FRAME_PROFILE idx=/ {
    delete v
    for (i = 1; i <= NF; ++i) {
        split($i, a, "=")
        if (length(a[1]) > 0 && length(a[2]) > 0) v[a[1]] = a[2]
    }
    printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
        v["idx"], v["zoom"], v["frame_ms"], v["update_ms"], v["map_ms"],
        v["ui_ms"], v["present_ms"], v["world_detail_ms"], v["masks_ms"],
        v["areas_layer_ms"], v["waterways_ms"], v["roads_layer_ms"],
        v["labels_ms"], v["regions"]
}
' "$OUTPUT_DIR/frame_profile.txt" > "$OUTPUT_DIR/frame_profile.tsv"

awk -F '\t' '
NR == 1 { next }
{
    n++
    frame += $3; map += $5; ui += $6; present += $7
    masks += $9; areas += $10; water += $11; roads += $12; labels += $13
    if ($3 > frame_max) frame_max = $3
    if ($5 > map_max) map_max = $5
}
END {
    if (n == 0) {
        print "profile_frames=0"
        exit
    }
    printf "profile_frames=%d\n", n
    printf "frame_mean_ms=%.3f\n", frame/n
    printf "frame_max_ms=%.3f\n", frame_max
    printf "map_mean_ms=%.3f\n", map/n
    printf "map_max_ms=%.3f\n", map_max
    printf "ui_mean_ms=%.3f\n", ui/n
    printf "present_mean_ms=%.3f\n", present/n
    printf "masks_mean_ms=%.3f\n", masks/n
    printf "areas_mean_ms=%.3f\n", areas/n
    printf "waterways_mean_ms=%.3f\n", water/n
    printf "roads_mean_ms=%.3f\n", roads/n
    printf "labels_mean_ms=%.3f\n", labels/n
}
' "$OUTPUT_DIR/frame_profile.tsv" > "$OUTPUT_DIR/summary.txt"

{
    echo "OpenRide ORMap frame profiler"
    echo "Date: $(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "Package: $PACKAGE"
    echo "PID: $PID"
    echo "Device: $("${ADB[@]}" shell getprop ro.product.model | tr -d '\r')"
    echo "Android: $("${ADB[@]}" shell getprop ro.build.version.release | tr -d '\r')"
    echo "Profile complete: $PROFILE_DONE"
    echo
    cat "$OUTPUT_DIR/summary.txt"
} > "$OUTPUT_DIR/report.txt"

cat "$OUTPUT_DIR/report.txt"

if [ "$MAKE_ZIP" -eq 1 ] && command -v zip >/dev/null 2>&1; then
    ZIP_PATH="${OUTPUT_DIR}.zip"
    (
        cd "$(dirname "$OUTPUT_DIR")" || exit 1
        zip -qr "$ZIP_PATH" "$(basename "$OUTPUT_DIR")"
    )
    echo
    echo "ZIP: $ZIP_PATH"
fi

if [ "$PROFILE_DONE" -ne 1 ]; then
    echo "ERROR: profiling did not complete within ${PROFILE_TIMEOUT}s" >&2
    exit 1
fi

PROFILE_COUNT="$(grep -c 'AUDIT_FRAME_PROFILE idx=' "$OUTPUT_DIR/logcat_pid.txt" || true)"
if [ "$PROFILE_COUNT" -lt 8 ]; then
    echo "ERROR: only $PROFILE_COUNT profiled frames were collected" >&2
    exit 1
fi

echo
echo "DONE"
echo "Send the generated ZIP for analysis."
