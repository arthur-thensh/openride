#!/usr/bin/env bash
set -u

PACKAGE="${OPENRIDE_ANDROID_PACKAGE:-com.arthurthion.openride}"
OUTPUT_DIR="${1:-$HOME/Downloads/openride-france-overview-$(date +%Y%m%d-%H%M%S)}"

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

mkdir -p "$OUTPUT_DIR"

logcat_for_pid() {
    local pid="$1"
    if "${ADB[@]}" logcat -d --pid="$pid" -v brief >/dev/null 2>&1; then
        "${ADB[@]}" logcat -d --pid="$pid" -v brief 2>/dev/null
    else
        "${ADB[@]}" logcat -d -v brief 2>/dev/null \
            | grep -E "^[A-Z]/.*\\($pid\\):| pid=$pid " || true
    fi
}

log_count() {
    local pid="$1"
    local marker="$2"
    logcat_for_pid "$pid" | grep -c "$marker" || true
}

wait_log_count() {
    local pid="$1"
    local marker="$2"
    local previous="$3"
    local timeout_s="$4"
    local deadline=$((SECONDS + timeout_s))

    while [ "$SECONDS" -lt "$deadline" ]; do
        local current
        current="$(log_count "$pid" "$marker")"
        if [ "$current" -gt "$previous" ]; then
            return 0
        fi
        sleep 0.10
    done
    return 1
}

wait_first_frame() {
    local pid="$1"
    local deadline=$((SECONDS + 45))

    echo "Waiting for OpenRide first frame..."
    while [ "$SECONDS" -lt "$deadline" ]; do
        if logcat_for_pid "$pid" \
            | grep -q 'AUDIT_FIRST_FRAME_READY'; then
            echo "OK: first frame ready"
            return 0
        fi
        sleep 0.10
    done
    return 1
}

wait_present_zoom() {
    local pid="$1"
    local previous_present_count="$2"
    local expected_zoom="$3"
    local timeout_s="$4"
    local deadline=$((SECONDS + timeout_s))

    while [ "$SECONDS" -lt "$deadline" ]; do
        local current
        current="$(log_count "$pid" 'AUDIT_FRAME_PRESENT')"
        if [ "$current" -gt "$previous_present_count" ]; then
            local latest
            latest="$(logcat_for_pid "$pid" \
                | grep 'AUDIT_FRAME_PRESENT' \
                | tail -n 1 || true)"
            if printf '%s\n' "$latest" \
                | grep -q "zoom=${expected_zoom}"; then
                return 0
            fi
        fi
        sleep 0.10
    done
    return 1
}

capture() {
    local path="$1"
    "${ADB[@]}" exec-out screencap -p > "$path"
    if [ ! -s "$path" ]; then
        echo "ERROR: empty screenshot: $path" >&2
        return 1
    fi
}

apply_pinch_in() {
    local pid="$1"
    local previous_pinch
    local previous_present
    previous_pinch="$(log_count "$pid" 'AUDIT_PINCH_APPLIED')"
    previous_present="$(log_count "$pid" 'AUDIT_FRAME_PRESENT')"

    "${ADB[@]}" shell input keyevent 141 >/dev/null 2>&1

    if ! wait_log_count \
        "$pid" \
        'AUDIT_PINCH_APPLIED' \
        "$previous_pinch" \
        8; then
        echo "ERROR: F11 pinch event was not acknowledged" >&2
        return 1
    fi

    local pinch_line
    pinch_line="$(logcat_for_pid "$pid" \
        | grep 'AUDIT_PINCH_APPLIED' \
        | tail -n 1 || true)"

    local zoom_after
    zoom_after="$(printf '%s\n' "$pinch_line" \
        | sed -n 's/.*zoom_after=\([0-9][0-9.]*\).*/\1/p')"

    if [ -z "$zoom_after" ]; then
        echo "ERROR: unable to parse zoom_after from:" >&2
        echo "$pinch_line" >&2
        return 1
    fi

    if ! wait_present_zoom \
        "$pid" \
        "$previous_present" \
        "$zoom_after" \
        8; then
        echo "ERROR: rendered frame for zoom $zoom_after not observed" >&2
        return 1
    fi

    printf '%s\n' "$zoom_after"
}

echo "OpenRide France Overview capture"
echo "Output: $OUTPUT_DIR"
echo

"${ADB[@]}" logcat -c >/dev/null 2>&1 || true
"${ADB[@]}" shell am force-stop "$PACKAGE" >/dev/null 2>&1 || true

echo "Launching OpenRide..."
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
        | tr -d '\r' \
        | awk '{print $1}')"
    [ -n "$PID" ] && break
    sleep 0.10
done

if [ -z "$PID" ]; then
    echo "ERROR: OpenRide process did not start" >&2
    exit 1
fi
echo "PID: $PID"

if ! wait_first_frame "$PID"; then
    echo "ERROR: AUDIT_FIRST_FRAME_READY not observed within 45s" >&2
    echo
    echo "Recent OpenRide log:"
    logcat_for_pid "$PID" | tail -n 80
    exit 1
fi

echo
echo "Jumping to Paris z7..."

jump_ok=0
for attempt in 1 2 3; do
    previous_jump="$(log_count "$PID" 'AUDIT_FRANCE_OVERVIEW_JUMP')"
    previous_present="$(log_count "$PID" 'AUDIT_FRAME_PRESENT')"

    echo "  F9 attempt $attempt/3"
    "${ADB[@]}" shell input keyevent 139 >/dev/null 2>&1

    if wait_log_count \
        "$PID" \
        'AUDIT_FRANCE_OVERVIEW_JUMP' \
        "$previous_jump" \
        4; then
        if wait_present_zoom \
            "$PID" \
            "$previous_present" \
            "7.000" \
            8; then
            jump_ok=1
            break
        fi
    fi

    sleep 0.25
done

if [ "$jump_ok" -ne 1 ]; then
    echo "ERROR: France Overview F9 jump was not completed" >&2
    echo
    echo "Recent OpenRide log:"
    logcat_for_pid "$PID" | tail -n 120
    exit 1
fi

echo "OK: Paris z7 rendered"
capture "$OUTPUT_DIR/01_paris_z7.png" || exit 1

echo
echo "Zooming to z8..."
zoom8="$(apply_pinch_in "$PID")" || exit 1
echo "OK: rendered z$zoom8"

echo "Zooming to z9..."
zoom9="$(apply_pinch_in "$PID")" || exit 1
echo "OK: rendered z$zoom9"

capture "$OUTPUT_DIR/02_paris_z9.png" || exit 1

logcat_for_pid "$PID" \
    | grep -E \
        'AUDIT_FIRST_FRAME_READY|AUDIT_FRANCE_OVERVIEW_JUMP|AUDIT_PINCH_APPLIED|AUDIT_FRAME_PRESENT|AUDIT_DIRTY_RENDER' \
    > "$OUTPUT_DIR/logcat_overview.txt" || true

echo
echo "France Overview captures complete:"
echo "  $OUTPUT_DIR/01_paris_z7.png"
echo "  $OUTPUT_DIR/02_paris_z9.png"
echo "  $OUTPUT_DIR/logcat_overview.txt"
echo
echo "DONE"
