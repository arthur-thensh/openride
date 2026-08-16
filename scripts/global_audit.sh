#!/usr/bin/env bash
#
# OpenRide Global Audit V2.2
# ------------------------
# Full project audit orchestrator for macOS + a connected Android device.
#
# It deliberately continues after individual failures so the final archive
# contains as much diagnostic evidence as possible.
#
# Default output:
#   ~/Downloads/openride-audit-YYYYMMDD-HHMMSS/
#   ~/Downloads/openride-audit-YYYYMMDD-HHMMSS.zip
#
set -uo pipefail

PACKAGE="${OPENRIDE_ANDROID_PACKAGE:-com.arthurthion.openride}"
AUDIT_LAT="${OPENRIDE_AUDIT_LAT:-50.368014}"
AUDIT_LON="${OPENRIDE_AUDIT_LON:-3.079392}"
AUDIT_LOOP_KM="${OPENRIDE_AUDIT_LOOP_KM:-25}"
AUDIT_LOOP_PROFILE="${OPENRIDE_AUDIT_LOOP_PROFILE:-trail}"

OUTPUT_DIR=""
SKIP_ANDROID=0
SKIP_BUILD=0
SKIP_BENCHMARKS=0
NO_INSTALL=0
MAKE_ZIP=1
AUDIT_PROFILE="full"
ONLY_STEPS=""
SKIP_STEPS=""
REUSE_ANDROID=0
LIST_STEPS=0
SWEEP_VIDEO=1

usage() {
    cat <<'EOF'
Usage:
  ./scripts/global_audit.sh [options]

Fast profiles:
  --profile full        Complete audit (default, historical behavior)
  --profile map         Android map/ORMap visual + zoom checks
  --profile perf        Android startup/runtime/map performance checks
  --profile ui          Android Back/UI/lifecycle checks
  --profile host        Git + macOS build/CTest + routing benchmarks
  --profile smoke       Short repository + Android interaction sanity check

Fine-grained selection:
  --only LIST           Run only comma-separated step IDs (+ cheap prerequisites)
  --skip LIST           Skip comma-separated step IDs
  --list-steps          Print selectable step IDs and profile contents

Execution:
  --reuse-android       Reuse the already-installed APK; skip Android check/build/install
  --output DIR          Audit output directory
  --skip-android        Do not run Android steps
  --skip-build          Reuse the existing macOS build directory
  --skip-benchmarks     Skip graph/loop benchmarks
  --no-install          Build Android but do not reinstall the APK
  --no-zip              Do not create the final ZIP archive
  --no-sweep-video      Disable MP4 recording during android_zoom_sweep
  -h, --help            Show this help

Examples:
  ./scripts/global_audit.sh --profile map --reuse-android
  ./scripts/global_audit.sh --profile perf --reuse-android --no-zip
  ./scripts/global_audit.sh --only android_zoom_sweep --no-zip
  ./scripts/global_audit.sh --only android_zoom_gallery,android_zoom_sweep
  ./scripts/global_audit.sh --profile map --skip android_map_styles

Environment:
  ANDROID_SERIAL                 Select the Android device when several exist
  OPENRIDE_ANDROID_PACKAGE       Android package (default: com.arthurthion.openride)
  OPENRIDE_AUDIT_LAT             Loop benchmark latitude
  OPENRIDE_AUDIT_LON             Loop benchmark longitude
  OPENRIDE_AUDIT_LOOP_KM         Loop benchmark distance (default: 25)
  OPENRIDE_AUDIT_LOOP_PROFILE    Loop profile (default: trail)

Selection semantics:
  --profile full preserves the complete V2.1 audit behavior.
  --only overrides the profile and selects exact test IDs. For Android tests,
  device detection/inventory and the cheap deterministic prerequisites are
  enabled automatically. Screenshot integrity/logcat/crash evidence are also
  collected when relevant.
  --skip always wins over profile/--only selection.

Important:
  Partial profiles are diagnostic runs, not full release gates. Their report
  explicitly records the profile/selection used.
EOF
}


list_steps() {
    cat <<'EOF'
OpenRide Global Audit V2.2 step IDs

Host/repository:
  repo_info
  environment
  git_diff_check
  git_worktree
  data_inventory
  configure_macos
  build_macos
  ctest_inventory
  ctest_all
  build_warnings
  benchmark_spatial
  benchmark_segment
  benchmark_loop

Android setup/evidence:
  android_setup
  android_device
  android_check
  android_build
  android_install
  android_package
  android_permissions
  android_data
  android_logcat_clear

Android runtime/map/UI:
  android_startup
  android_idle_profile
  android_map_stability
  android_pan
  android_back
  android_zoom_gallery
  android_zoom_sweep
  android_map_styles
  android_ui_tour
  android_lifecycle
  android_logcat
  android_crash_scan

Evidence/review:
  screenshot_integrity
  screenshot_duplicates
  android_physical_multitouch
  visual_review

Profiles:
  full
    Everything.

  map
    Repository identity/diff + Android build/install + data inventory +
    map stability + pan + zoom gallery + renderer sweep + map styles +
    logcat/crash/screenshot evidence.

  perf
    Repository identity/diff + Android build/install + startup x3 +
    T+2/T+10/T+30 runtime profile + map stability + renderer sweep +
    logcat/crash/screenshot evidence.

  ui
    Repository identity/diff + Android build/install + Back semantics +
    UI tour + lifecycle stress + logcat/crash/screenshot evidence.

  host
    Repository/toolchain + macOS configure/build + complete CTest +
    warning inventory + routing/loop benchmarks.

  smoke
    Repository identity/diff + Android build/install + data inventory +
    map stability + pan + Back + logcat/crash/screenshot evidence.

Tip:
  Add --reuse-android to map/perf/ui/smoke when the currently installed APK
  already matches the code you want to test.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --profile)
            [ "$#" -ge 2 ] || { echo "ERROR: --profile requires a name" >&2; exit 2; }
            AUDIT_PROFILE="$2"
            shift 2
            ;;
        --only)
            [ "$#" -ge 2 ] || { echo "ERROR: --only requires a comma-separated step list" >&2; exit 2; }
            ONLY_STEPS="$2"
            shift 2
            ;;
        --skip)
            [ "$#" -ge 2 ] || { echo "ERROR: --skip requires a comma-separated step list" >&2; exit 2; }
            SKIP_STEPS="$2"
            shift 2
            ;;
        --reuse-android)
            REUSE_ANDROID=1
            shift
            ;;
        --list-steps)
            LIST_STEPS=1
            shift
            ;;
        --output)
            [ "$#" -ge 2 ] || { echo "ERROR: --output requires a directory" >&2; exit 2; }
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --skip-android)
            SKIP_ANDROID=1
            shift
            ;;
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        --skip-benchmarks)
            SKIP_BENCHMARKS=1
            shift
            ;;
        --no-install)
            NO_INSTALL=1
            shift
            ;;
        --no-zip)
            MAKE_ZIP=0
            shift
            ;;
        --no-sweep-video)
            SWEEP_VIDEO=0
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

if [ "$LIST_STEPS" -eq 1 ]; then
    list_steps
    exit 0
fi


KNOWN_STEPS="repo_info,environment,git_diff_check,git_worktree,data_inventory,configure_macos,build_macos,ctest_inventory,ctest_all,build_warnings,benchmark_spatial,benchmark_segment,benchmark_loop,android_setup,android_device,android_check,android_build,android_install,android_package,android_permissions,android_data,android_logcat_clear,android_startup,android_idle_profile,android_map_stability,android_pan,android_back,android_zoom_gallery,android_zoom_sweep,android_map_styles,android_ui_tour,android_lifecycle,android_logcat,android_crash_scan,screenshot_integrity,screenshot_duplicates,android_physical_multitouch,visual_review"

csv_has() {
    local list="$1"
    local item="$2"
    case ",$list," in
        *",$item,"*) return 0 ;;
        *) return 1 ;;
    esac
}

validate_step_csv() {
    local list="$1"
    local option_name="$2"
    [ -n "$list" ] || return 0
    local old_ifs="$IFS"
    IFS=','
    for item in $list; do
        [ -n "$item" ] || continue
        if ! csv_has "$KNOWN_STEPS" "$item"; then
            IFS="$old_ifs"
            echo "ERROR: unknown step ID for $option_name: $item" >&2
            echo "Use --list-steps to see valid IDs." >&2
            exit 2
        fi
    done
    IFS="$old_ifs"
}

PROFILE_STEPS=""
case "$AUDIT_PROFILE" in
    full)
        PROFILE_STEPS="*"
        ;;
    map)
        PROFILE_STEPS="repo_info,git_diff_check,git_worktree,android_setup,android_device,android_check,android_build,android_install,android_package,android_permissions,android_data,android_logcat_clear,android_map_stability,android_pan,android_zoom_gallery,android_zoom_sweep,android_map_styles,android_logcat,android_crash_scan,screenshot_integrity,screenshot_duplicates,android_physical_multitouch,visual_review"
        ;;
    perf)
        PROFILE_STEPS="repo_info,git_diff_check,git_worktree,android_setup,android_device,android_check,android_build,android_install,android_package,android_permissions,android_data,android_logcat_clear,android_startup,android_idle_profile,android_map_stability,android_zoom_sweep,android_logcat,android_crash_scan,screenshot_integrity,screenshot_duplicates,visual_review"
        ;;
    ui)
        PROFILE_STEPS="repo_info,git_diff_check,git_worktree,android_setup,android_device,android_check,android_build,android_install,android_package,android_permissions,android_logcat_clear,android_back,android_ui_tour,android_lifecycle,android_logcat,android_crash_scan,screenshot_integrity,screenshot_duplicates,visual_review"
        ;;
    host)
        PROFILE_STEPS="repo_info,environment,git_diff_check,git_worktree,data_inventory,configure_macos,build_macos,ctest_inventory,ctest_all,build_warnings,benchmark_spatial,benchmark_segment,benchmark_loop"
        ;;
    smoke)
        PROFILE_STEPS="repo_info,git_diff_check,git_worktree,android_setup,android_device,android_check,android_build,android_install,android_package,android_permissions,android_data,android_logcat_clear,android_map_stability,android_pan,android_back,android_logcat,android_crash_scan,screenshot_integrity,screenshot_duplicates,visual_review"
        ;;
    *)
        echo "ERROR: unknown audit profile: $AUDIT_PROFILE" >&2
        echo "Valid profiles: full, map, perf, ui, host, smoke" >&2
        exit 2
        ;;
esac

ONLY_STEPS="$(printf '%s' "$ONLY_STEPS" | tr -d '[:space:]')"
SKIP_STEPS="$(printf '%s' "$SKIP_STEPS" | tr -d '[:space:]')"
validate_step_csv "$ONLY_STEPS" "--only"
validate_step_csv "$SKIP_STEPS" "--skip"

only_has_android_step() {
    [ -n "$ONLY_STEPS" ] || return 1
    local old_ifs="$IFS"
    IFS=','
    for item in $ONLY_STEPS; do
        case "$item" in
            android_*)
                IFS="$old_ifs"
                return 0
                ;;
        esac
    done
    IFS="$old_ifs"
    return 1
}

only_has_interactive_android_step() {
    local id
    for id in \
        android_startup android_idle_profile android_map_stability android_pan \
        android_back android_zoom_gallery android_zoom_sweep android_map_styles \
        android_ui_tour android_lifecycle; do
        if csv_has "$ONLY_STEPS" "$id"; then
            return 0
        fi
    done
    return 1
}

audit_step_enabled() {
    local id="$1"
    if csv_has "$SKIP_STEPS" "$id"; then
        return 1
    fi

    if [ -n "$ONLY_STEPS" ]; then
        if csv_has "$ONLY_STEPS" "$id"; then
            return 0
        fi
        if only_has_android_step; then
            case "$id" in
                android_setup|android_device)
                    return 0
                    ;;
            esac
        fi
        if only_has_interactive_android_step; then
            case "$id" in
                android_permissions|android_logcat_clear|android_logcat|android_crash_scan|screenshot_integrity)
                    return 0
                    ;;
            esac
        fi
        if csv_has "$ONLY_STEPS" "android_crash_scan"; then
            case "$id" in
                android_logcat_clear|android_logcat)
                    return 0
                    ;;
            esac
        fi
        return 1
    fi

    [ "$PROFILE_STEPS" = "*" ] && return 0
    csv_has "$PROFILE_STEPS" "$id"
}

audit_filter_reason() {
    local id="$1"
    if csv_has "$SKIP_STEPS" "$id"; then
        printf '%s\n' "--skip requested for $id"
    elif [ -n "$ONLY_STEPS" ]; then
        printf '%s\n' "not selected by --only=$ONLY_STEPS"
    else
        printf '%s\n' "not selected by --profile $AUDIT_PROFILE"
    fi
}

audit_any_android_enabled() {
    local id
    for id in \
        android_setup android_device android_check android_build android_install \
        android_package android_permissions android_data android_logcat_clear \
        android_startup android_idle_profile android_map_stability android_pan \
        android_back android_zoom_gallery android_zoom_sweep android_map_styles \
        android_ui_tour android_lifecycle android_logcat android_crash_scan; do
        if audit_step_enabled "$id"; then
            return 0
        fi
    done
    return 1
}

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$SCRIPT_DIR/../CMakeLists.txt" ]; then
    ROOT_DIR="$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)"
elif [ -f "$PWD/CMakeLists.txt" ]; then
    ROOT_DIR="$PWD"
else
    echo "ERROR: unable to locate the OpenRide repository root." >&2
    echo "Place this file in scripts/global_audit.sh or run it from the repository root." >&2
    exit 1
fi
cd "$ROOT_DIR"

if [ -z "$OUTPUT_DIR" ]; then
    OUTPUT_DIR="$HOME/Downloads/openride-audit-$(date +%Y%m%d-%H%M%S)"
fi

LOG_DIR="$OUTPUT_DIR/logs"
SCREEN_DIR="$OUTPUT_DIR/screenshots"
METRIC_DIR="$OUTPUT_DIR/metrics"
DEVICE_DIR="$OUTPUT_DIR/device"
VIDEO_DIR="$OUTPUT_DIR/videos"
STATUS_FILE="$OUTPUT_DIR/status.tsv"
REPORT_FILE="$OUTPUT_DIR/report.md"
REVIEW_FILE="$OUTPUT_DIR/REVIEW_WITH_CHATGPT.md"
MANIFEST_FILE="$OUTPUT_DIR/manifest.sha256"

mkdir -p "$LOG_DIR" "$SCREEN_DIR" "$METRIC_DIR" "$DEVICE_DIR" "$VIDEO_DIR"
printf 'id\tlabel\tstatus\trc\tduration_s\tlog\n' > "$STATUS_FILE"

record_status() {
    local id="$1"
    local label="$2"
    local status="$3"
    local rc="$4"
    local duration="$5"
    local log="$6"
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$id" "$label" "$status" "$rc" "$duration" "$log" >> "$STATUS_FILE"
}

record_skip() {
    local id="$1"
    local label="$2"
    local reason="$3"
    local log="$LOG_DIR/${id}.log"
    printf '%s\n' "$reason" > "$log"
    record_status "$id" "$label" "SKIP" "0" "0" "logs/${id}.log"
    printf '[SKIP] %s — %s\n' "$label" "$reason"
}

record_warn() {
    local id="$1"
    local label="$2"
    local reason="$3"
    local log="$LOG_DIR/${id}.log"
    printf '%s\n' "$reason" > "$log"
    record_status "$id" "$label" "WARN" "0" "0" "logs/${id}.log"
    printf '[WARN] %s — %s\n' "$label" "$reason"
}

run_step() {
    local id="$1"
    local label="$2"
    local severity="$3"
    shift 3

    if ! audit_step_enabled "$id"; then
        record_skip "$id" "$label" "$(audit_filter_reason "$id")"
        return 0
    fi

    local log="$LOG_DIR/${id}.log"
    local start end duration rc status
    start="$(date +%s)"

    echo
    echo "================================================================"
    echo "$label"
    echo "================================================================"

    # Do not pipe the tested command through tee here. On macOS's Bash 3.2,
    # a function on the left side of a pipeline runs in a subshell; that would
    # discard state produced by setup functions (ADB selection, screen size).
    # Capture first, then print the log so shell state remains in this process.
    "$@" > "$log" 2>&1
    rc=$?
    cat "$log"

    end="$(date +%s)"
    duration=$((end - start))

    if [ "$rc" -eq 0 ]; then
        status="PASS"
    elif [ "$severity" = "required" ]; then
        status="FAIL"
    else
        status="WARN"
    fi

    record_status "$id" "$label" "$status" "$rc" "$duration" "logs/${id}.log"
    printf '[%s] %s (%ss)\n' "$status" "$label" "$duration"
    return 0
}

hash_file() {
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    elif command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        return 1
    fi
}

repo_info() {
    {
        echo "Repository: $ROOT_DIR"
        echo "Date: $(date '+%Y-%m-%d %H:%M:%S %z')"
        echo "Branch: $(git branch --show-current 2>/dev/null || echo '?')"
        echo "HEAD: $(git rev-parse HEAD 2>/dev/null || echo '?')"
        echo "Describe: $(git describe --always --dirty 2>/dev/null || echo '?')"
        echo
        echo "Remote:"
        git remote -v 2>/dev/null || true
        echo
        echo "Worktree:"
        git status --short 2>/dev/null || true
    } | tee "$OUTPUT_DIR/repository.txt"
}

environment_info() {
    {
        echo "OpenRide Global Audit environment"
        echo "Date: $(date '+%Y-%m-%d %H:%M:%S %z')"
        echo
        echo "uname:"
        uname -a
        echo
        echo "macOS:"
        sw_vers 2>/dev/null || true
        echo
        echo "clang:"
        clang --version 2>/dev/null | head -n 3 || true
        echo
        echo "cmake:"
        cmake --version 2>/dev/null | head -n 1 || true
        echo
        echo "python:"
        python3 --version 2>&1 || true
        echo
        echo "java:"
        java -version 2>&1 | head -n 3 || true
        echo
        echo "adb:"
        adb version 2>/dev/null | head -n 2 || true
    } | tee "$OUTPUT_DIR/environment.txt"
}

git_cleanliness() {
    git diff --check
    git diff --cached --check
}

run_all_ctest() {
    ./scripts/test.sh
}

collect_ctest_inventory() {
    if [ ! -f build/CTestTestfile.cmake ]; then
        echo "CTest build tree absent"
        return 1
    fi
    ctest --test-dir build -N
}

collect_build_warning_inventory() {
    local out="$METRIC_DIR/build_warnings.txt"
    : > "$out"

    for file in "$LOG_DIR/configure_macos.log" "$LOG_DIR/build_macos.log"; do
        [ -f "$file" ] || continue
        grep -nEi '(^|[^a-z])(warning|deprecated|could NOT find)([^a-z]|$)' "$file" >> "$out" || true
    done

    local count
    count="$(wc -l < "$out" | tr -d ' ')"
    echo "Build/configuration warning-like lines: $count"
    if [ "$count" -gt 0 ]; then
        echo "See metrics/build_warnings.txt"
    fi
    return 0
}

data_inventory() {
    {
        echo "Local OpenRide data inventory"
        echo
        for dir in data/maps data/routing data/search data/downloads data/osm; do
            if [ -d "$dir" ]; then
                echo "[$dir]"
                find "$dir" -maxdepth 1 -type f -print 2>/dev/null | sort | while IFS= read -r file; do
                    [ -n "$file" ] || continue
                    bytes="$(wc -c < "$file" | tr -d ' ')"
                    printf '%12s  %s\n' "$bytes" "$file"
                done
                echo
            fi
        done
    } | tee "$OUTPUT_DIR/data_inventory.txt"
}

benchmark_spatial() {
    ./scripts/benchmark_spatial_index.sh
}

benchmark_segment() {
    ./scripts/benchmark_segment_index.sh
}

benchmark_loop() {
    ./scripts/benchmark_loop_generator.sh \
        "data/routing/nord-pas-de-calais.orgraph" \
        "$AUDIT_LAT" "$AUDIT_LON" "$AUDIT_LOOP_KM" "$AUDIT_LOOP_PROFILE"
}

# ---------------------------------------------------------------------------
# Android helpers
# ---------------------------------------------------------------------------

ADB=()
ANDROID_READY=0
SCREEN_W=0
SCREEN_H=0

setup_adb() {
    command -v adb >/dev/null 2>&1 || {
        echo "adb not found in PATH"
        return 1
    }

    if [ -n "${ANDROID_SERIAL:-}" ]; then
        ADB=(adb -s "$ANDROID_SERIAL")
    else
        local devices=""
        devices="$(adb devices | awk '$2 == "device" {print $1}')"
        local count
        count="$(printf '%s\n' "$devices" | awk 'NF {n++} END {print n+0}')"

        if [ "$count" -eq 0 ]; then
            echo "No authorized Android device detected."
            adb devices
            return 1
        fi
        if [ "$count" -gt 1 ]; then
            echo "Multiple Android devices detected; set ANDROID_SERIAL."
            printf '%s\n' "$devices"
            return 1
        fi

        local serial
        serial="$(printf '%s\n' "$devices" | awk 'NF {print; exit}')"
        ADB=(adb -s "$serial")
    fi

    "${ADB[@]}" get-state
    ANDROID_READY=1
}

android_device_info() {
    [ "$ANDROID_READY" -eq 1 ] || return 1

    {
        echo "serial=$("${ADB[@]}" get-serialno | tr -d '\r')"
        echo "manufacturer=$("${ADB[@]}" shell getprop ro.product.manufacturer | tr -d '\r')"
        echo "brand=$("${ADB[@]}" shell getprop ro.product.brand | tr -d '\r')"
        echo "model=$("${ADB[@]}" shell getprop ro.product.model | tr -d '\r')"
        echo "device=$("${ADB[@]}" shell getprop ro.product.device | tr -d '\r')"
        echo "android=$("${ADB[@]}" shell getprop ro.build.version.release | tr -d '\r')"
        echo "sdk=$("${ADB[@]}" shell getprop ro.build.version.sdk | tr -d '\r')"
        echo "security_patch=$("${ADB[@]}" shell getprop ro.build.version.security_patch | tr -d '\r')"
        echo "build=$("${ADB[@]}" shell getprop ro.build.fingerprint | tr -d '\r')"
        echo
        "${ADB[@]}" shell wm size
        "${ADB[@]}" shell wm density
    } | tee "$DEVICE_DIR/device.txt"

    local size
    size="$("${ADB[@]}" shell wm size 2>/dev/null \
        | sed -n 's/.*size: \([0-9][0-9]*x[0-9][0-9]*\).*/\1/p' \
        | tail -n 1 | tr -d '\r')"

    case "$size" in
        *x*)
            SCREEN_W="${size%x*}"
            SCREEN_H="${size#*x}"
            ;;
        *)
            SCREEN_W=0
            SCREEN_H=0
            ;;
    esac
}

android_package_info() {
    [ "$ANDROID_READY" -eq 1 ] || return 1
    "${ADB[@]}" shell dumpsys package "$PACKAGE"
}

android_data_inventory() {
    [ "$ANDROID_READY" -eq 1 ] || return 1
    "${ADB[@]}" shell run-as "$PACKAGE" \
        ls -lh files/data/maps files/data/routing files/data/search files/data/downloads
}

android_grant_test_permissions() {
    [ "$ANDROID_READY" -eq 1 ] || return 1

    # Preserve app data; only make sure runtime location dialogs cannot obstruct
    # deterministic screenshot/gesture checks.
    "${ADB[@]}" shell pm grant "$PACKAGE" android.permission.ACCESS_COARSE_LOCATION >/dev/null 2>&1 || true
    "${ADB[@]}" shell pm grant "$PACKAGE" android.permission.ACCESS_FINE_LOCATION >/dev/null 2>&1 || true
    echo "Runtime location grant attempted (non-destructive)."
}

resolve_activity() {
    local component
    component="$("${ADB[@]}" shell cmd package resolve-activity --brief "$PACKAGE" 2>/dev/null \
        | tr -d '\r' \
        | awk '/\// {line=$0} END {print line}')"
    case "$component" in
        */*) printf '%s\n' "$component" ;;
        *) return 1 ;;
    esac
}

android_pid() {
    "${ADB[@]}" shell pidof "$PACKAGE" 2>/dev/null \
        | tr -d '\r' \
        | awk '{print $1}'
}

android_logcat_for_pid() {
    local pid="$1"
    if "${ADB[@]}" logcat -d --pid="$pid" -v brief >/dev/null 2>&1; then
        "${ADB[@]}" logcat -d --pid="$pid" -v brief 2>/dev/null
    else
        "${ADB[@]}" logcat -d -v brief 2>/dev/null \
            | grep "pid=$pid" || true
    fi
}

ANDROID_FIRST_FRAME_LINE=""

android_wait_first_frame() {
    local pid="$1"
    local timeout_s="${2:-12}"
    local deadline=$((SECONDS + timeout_s))
    local line=""

    while [ "$SECONDS" -lt "$deadline" ]; do
        line="$(android_logcat_for_pid "$pid" \
            | grep 'AUDIT_FIRST_FRAME_READY' \
            | tail -n 1 || true)"
        if [ -n "$line" ]; then
            ANDROID_FIRST_FRAME_LINE="$line"
            printf '%s\n' "$line"
            return 0
        fi
        sleep 0.10
    done

    echo "ERROR: no AUDIT_FIRST_FRAME_READY marker for pid=$pid after ${timeout_s}s"
    return 1
}

android_launch_clean() {
    "${ADB[@]}" shell am force-stop "$PACKAGE" >/dev/null 2>&1 || true
    "${ADB[@]}" shell monkey \
        -p "$PACKAGE" \
        -c android.intent.category.LAUNCHER \
        1 >/dev/null 2>&1 || return 1

    local pid=""
    local deadline=$((SECONDS + 8))
    while [ "$SECONDS" -lt "$deadline" ]; do
        pid="$(android_pid)"
        [ -n "$pid" ] && break
        sleep 0.10
    done
    [ -n "$pid" ] || {
        echo "ERROR: OpenRide process did not appear after launch"
        return 1
    }

    android_wait_first_frame "$pid" 12 || return 1
    sleep 0.20
    printf 'pid=%s\n' "$pid"
}

android_startup_benchmark() {
    [ "$ANDROID_READY" -eq 1 ] || return 1

    local component
    component="$(resolve_activity)" || {
        echo "Unable to resolve launcher activity for $PACKAGE"
        return 1
    }

    echo "Component: $component"
    : > "$METRIC_DIR/android_startup.txt"
    : > "$METRIC_DIR/android_first_frame.txt"

    local i
    for i in 1 2 3; do
        "${ADB[@]}" shell am force-stop "$PACKAGE" >/dev/null 2>&1 || true
        sleep 0.25
        echo "--- cold start $i ---" | tee -a "$METRIC_DIR/android_startup.txt"
        "${ADB[@]}" shell am start -W -n "$component" \
            | tr -d '\r' \
            | tee -a "$METRIC_DIR/android_startup.txt"

        local pid=""
        local deadline=$((SECONDS + 8))
        while [ "$SECONDS" -lt "$deadline" ]; do
            pid="$(android_pid)"
            [ -n "$pid" ] && break
            sleep 0.10
        done
        [ -n "$pid" ] || return 1

        local marker
        marker="$(android_wait_first_frame "$pid" 12)" || return 1
        printf '%s\n' "$marker" | tee -a "$METRIC_DIR/android_first_frame.txt"
    done

    awk -F: '
        /TotalTime:/ {
            gsub(/[[:space:]]/, "", $2);
            if ($2 ~ /^[0-9]+$/) {
                sum += $2; n++;
                if (min == 0 || $2 < min) min = $2;
                if ($2 > max) max = $2;
            }
        }
        END {
            if (n > 0) {
                printf "activity_samples=%d\nactivity_mean_ms=%.1f\nactivity_min_ms=%d\nactivity_max_ms=%d\n",
                       n, sum/n, min, max;
            }
        }
    ' "$METRIC_DIR/android_startup.txt" \
        > "$METRIC_DIR/android_startup_summary.txt"

    awk '
        {
            if (match($0, /elapsed_ms=[0-9.]+/)) {
                v = substr($0, RSTART + 11, RLENGTH - 11) + 0.0;
                sum += v; n++;
                if (n == 1 || v < min) min = v;
                if (n == 1 || v > max) max = v;
            }
        }
        END {
            if (n > 0) {
                printf "first_frame_samples=%d\nfirst_frame_mean_ms=%.1f\nfirst_frame_min_ms=%.1f\nfirst_frame_max_ms=%.1f\n",
                       n, sum/n, min, max;
            }
        }
    ' "$METRIC_DIR/android_first_frame.txt" \
        >> "$METRIC_DIR/android_startup_summary.txt"

    cat "$METRIC_DIR/android_startup_summary.txt"
    android_pid >/dev/null
}

android_capture() {
    local path="$1"
    "${ADB[@]}" exec-out screencap -p > "$path"
    [ -s "$path" ]
}

pct_px() {
    awk -v total="$1" -v pct="$2" 'BEGIN {printf "%d", total * pct + 0.5}'
}

android_runtime_metric_sample() {
    local label="$1"
    local pid
    pid="$(android_pid)"

    {
        echo "PID:"
        printf '%s\n' "$pid"
        echo
        echo "TOP:"
        "${ADB[@]}" shell top -b -n 1 2>/dev/null | grep "$PACKAGE" || true
    } > "$METRIC_DIR/android_process_${label}.txt"

    {
        echo "OpenRide thread CPU snapshot ($label)"
        echo "pid=$pid"
        echo
        if "${ADB[@]}" shell top -H -b -n 1 -p "$pid" \
            > "$METRIC_DIR/.threads_${label}.tmp" 2>/dev/null; then
            cat "$METRIC_DIR/.threads_${label}.tmp"
        else
            "${ADB[@]}" shell top -H -b -n 1 2>/dev/null \
                | grep -E "$PACKAGE|PID|TID|CPU" || true
        fi
        rm -f "$METRIC_DIR/.threads_${label}.tmp"
    } > "$METRIC_DIR/android_threads_${label}.txt"

    "${ADB[@]}" shell dumpsys meminfo "$PACKAGE" \
        > "$METRIC_DIR/android_meminfo_${label}.txt" 2>&1 || true

    "${ADB[@]}" shell dumpsys gfxinfo "$PACKAGE" \
        > "$METRIC_DIR/android_gfxinfo_${label}.txt" 2>&1 || true

    android_logcat_for_pid "$pid" \
        | grep -E 'AUDIT_FRAME_PACING|AUDIT_DIRTY_RENDER|AUDIT_DIRTY_FOLLOWUP_CAPPED' \
        | tail -n 16 \
        > "$METRIC_DIR/android_pacing_${label}.txt" || true

    echo "[$label]"
    cat "$METRIC_DIR/android_process_${label}.txt"
    grep -E 'TOTAL PSS|TOTAL RSS|TOTAL SWAP PSS|Java Heap|Native Heap|Graphics' \
        "$METRIC_DIR/android_meminfo_${label}.txt" | head -n 30 || true
    echo
    echo "Recent OpenRide pacing / dirty rendering:"
    cat "$METRIC_DIR/android_pacing_${label}.txt" || true
}

android_idle_runtime_profile() {
    [ "$ANDROID_READY" -eq 1 ] || return 1

    local dir="$SCREEN_DIR/startup"
    mkdir -p "$dir"

    android_launch_clean || return 1
    android_capture "$dir/00_first_frame_ready.png" || return 1

    sleep 0.50
    android_capture "$dir/01_after_500ms.png" || return 1

    sleep 1.50
    android_runtime_metric_sample "t02"
    android_capture "$dir/02_idle_t02.png" || return 1

    sleep 8
    android_runtime_metric_sample "t10"

    sleep 20
    android_runtime_metric_sample "t30"
    android_capture "$dir/03_idle_t30.png" || return 1

    android_pid >/dev/null
}


android_map_stability_test() {
    [ "$ANDROID_READY" -eq 1 ] || return 1

    local dir="$SCREEN_DIR/startup"
    local metrics="$METRIC_DIR/android_map_stability.txt"
    mkdir -p "$dir"
    : > "$metrics"

    android_launch_clean || return 1

    local previous_hash=""
    local stable_count=0
    local sample=0
    local start_s="$SECONDS"
    local stable_after_ms=-1
    local tmp="$dir/.stability.png"

    while [ "$sample" -lt 20 ]; do
        android_capture "$tmp" || return 1
        local hash
        hash="$(hash_file "$tmp" || true)"
        local elapsed_ms=$(( (SECONDS - start_s) * 1000 ))
        printf 'sample=%d elapsed_ms=%d sha256=%s\n' \
            "$sample" "$elapsed_ms" "$hash" >> "$metrics"

        if [ -n "$hash" ] && [ "$hash" = "$previous_hash" ]; then
            stable_count=$((stable_count + 1))
        else
            stable_count=0
        fi

        if [ "$stable_count" -ge 2 ]; then
            stable_after_ms="$elapsed_ms"
            cp "$tmp" "$dir/04_stable_map.png"
            break
        fi

        previous_hash="$hash"
        sample=$((sample + 1))
        sleep 0.50
    done

    rm -f "$tmp"
    echo "stable_after_ms=$stable_after_ms" | tee -a "$metrics"

    if [ "$stable_after_ms" -lt 0 ]; then
        echo "ERROR: map did not reach three identical consecutive captures within the stability window."
        return 1
    fi
}

android_pan_gesture_test() {
    [ "$ANDROID_READY" -eq 1 ] || return 1
    [ "$SCREEN_W" -gt 0 ] && [ "$SCREEN_H" -gt 0 ] || {
        echo "Invalid screen dimensions: ${SCREEN_W}x${SCREEN_H}"
        return 1
    }

    local dir="$SCREEN_DIR/gestures"
    mkdir -p "$dir"

    android_launch_clean || return 1
    android_capture "$dir/01_map_before_pan.png" || return 1

    local x1 y1 x2 y2
    x1="$(pct_px "$SCREEN_W" 0.56)"
    y1="$(pct_px "$SCREEN_H" 0.50)"
    x2="$(pct_px "$SCREEN_W" 0.30)"
    y2="$y1"

    "${ADB[@]}" shell input swipe "$x1" "$y1" "$x2" "$y2" 450 >/dev/null
    sleep 0.75
    android_capture "$dir/02_map_after_pan.png" || return 1

    local h1 h2
    h1="$(hash_file "$dir/01_map_before_pan.png" || true)"
    h2="$(hash_file "$dir/02_map_after_pan.png" || true)"

    echo "before_sha256=$h1"
    echo "after_sha256=$h2"

    if [ -n "$h1" ] && [ "$h1" = "$h2" ]; then
        echo "ERROR: map screenshot did not change after the Android swipe."
        return 1
    fi

    android_pid >/dev/null
}

android_back_navigation_test() {
    [ "$ANDROID_READY" -eq 1 ] || return 1
    [ "$SCREEN_W" -gt 0 ] && [ "$SCREEN_H" -gt 0 ] || return 1

    local dir="$SCREEN_DIR/back"
    mkdir -p "$dir"

    android_launch_clean || return 1

    local menu_x toolbar_y settings_x settings_y search_x
    menu_x="$(pct_px "$SCREEN_W" 0.178)"
    search_x="$(pct_px "$SCREEN_W" 0.337)"
    toolbar_y="$(pct_px "$SCREEN_H" 0.938)"
    settings_x="$(pct_px "$SCREEN_W" 0.500)"
    settings_y="$(pct_px "$SCREEN_H" 0.642)"

    "${ADB[@]}" shell input tap "$menu_x" "$toolbar_y" >/dev/null
    sleep 0.45
    "${ADB[@]}" shell input tap "$settings_x" "$settings_y" >/dev/null
    sleep 0.55
    android_capture "$dir/01_settings.png" || return 1

    "${ADB[@]}" shell input keyevent KEYCODE_BACK >/dev/null
    sleep 0.55
    android_capture "$dir/02_back_to_menu.png" || return 1

    "${ADB[@]}" shell input keyevent KEYCODE_BACK >/dev/null
    sleep 0.55
    android_capture "$dir/03_back_to_map.png" || return 1

    "${ADB[@]}" shell input tap "$search_x" "$toolbar_y" >/dev/null
    sleep 0.65
    android_capture "$dir/04_search_with_keyboard.png" || return 1

    # First Back is normally consumed by the Android IME.
    "${ADB[@]}" shell input keyevent KEYCODE_BACK >/dev/null
    sleep 0.65
    android_capture "$dir/05_search_keyboard_closed.png" || return 1

    # Second Back must now reach OpenRide and close Search itself.
    "${ADB[@]}" shell input keyevent KEYCODE_BACK >/dev/null
    sleep 0.65
    android_capture "$dir/06_search_closed.png" || return 1

    local h1 h2 h3 h4 h5 h6
    h1="$(hash_file "$dir/01_settings.png" || true)"
    h2="$(hash_file "$dir/02_back_to_menu.png" || true)"
    h3="$(hash_file "$dir/03_back_to_map.png" || true)"
    h4="$(hash_file "$dir/04_search_with_keyboard.png" || true)"
    h5="$(hash_file "$dir/05_search_keyboard_closed.png" || true)"
    h6="$(hash_file "$dir/06_search_closed.png" || true)"

    echo "settings=$h1"
    echo "menu=$h2"
    echo "map=$h3"
    echo "search_keyboard=$h4"
    echo "search_no_keyboard=$h5"
    echo "search_closed=$h6"

    [ -z "$h1" ] || [ "$h1" != "$h2" ] || return 1
    [ -z "$h2" ] || [ "$h2" != "$h3" ] || return 1
    [ -z "$h4" ] || [ "$h4" != "$h5" ] || return 1
    [ -z "$h5" ] || [ "$h5" != "$h6" ] || {
        echo "ERROR: second Android Back did not close Search."
        return 1
    }

    android_pid >/dev/null
}

android_log_count() {
    local pid="$1"
    local pattern="$2"
    android_logcat_for_pid "$pid" | grep -c "$pattern" || true
}

android_wait_log_count() {
    local pid="$1"
    local pattern="$2"
    local wanted="$3"
    local timeout_s="${4:-6}"
    local deadline=$((SECONDS + timeout_s))

    while [ "$SECONDS" -lt "$deadline" ]; do
        local count
        count="$(android_log_count "$pid" "$pattern")"
        if [ "$count" -ge "$wanted" ]; then
            return 0
        fi
        sleep 0.08
    done
    return 1
}

AUDIT_PINCH_ZOOM=""

android_apply_audit_pinch() {
    local pid="$1"
    local keycode="$2"

    local pinch_before present_before
    pinch_before="$(android_log_count "$pid" 'AUDIT_PINCH_APPLIED')"
    present_before="$(android_log_count "$pid" 'AUDIT_FRAME_PRESENT')"

    "${ADB[@]}" shell input keyevent "$keycode" >/dev/null

    android_wait_log_count "$pid" 'AUDIT_PINCH_APPLIED' \
        $((pinch_before + 1)) 6 || {
        echo "ERROR: pinch event was not acknowledged by OpenRide."
        return 1
    }

    local line
    line="$(android_last_pinch_line "$pid")"
    AUDIT_PINCH_ZOOM="$(
        printf '%s\n' "$line" \
            | sed -n 's/.*zoom_after=\([0-9.][0-9.]*\).*/\1/p'
    )"
    [ -n "$AUDIT_PINCH_ZOOM" ] || {
        echo "ERROR: unable to parse zoom_after from: $line"
        return 1
    }

    android_wait_log_count "$pid" 'AUDIT_FRAME_PRESENT' \
        $((present_before + 1)) 6 || {
        echo "ERROR: no rendered frame observed after pinch to z$AUDIT_PINCH_ZOOM."
        return 1
    }

    local deadline=$((SECONDS + 3))
    while [ "$SECONDS" -lt "$deadline" ]; do
        local presented
        presented="$(
            android_logcat_for_pid "$pid" \
                | grep 'AUDIT_FRAME_PRESENT' \
                | tail -n 1 || true
        )"
        case "$presented" in
            *"zoom=$AUDIT_PINCH_ZOOM"*) return 0 ;;
        esac
        sleep 0.06
    done

    echo "ERROR: rendered zoom did not reach z$AUDIT_PINCH_ZOOM."
    return 1
}

android_last_pinch_line() {
    local pid="$1"
    android_logcat_for_pid "$pid" \
        | grep 'AUDIT_PINCH_APPLIED' \
        | tail -n 1 || true
}

android_zoom_gallery() {
    [ "$ANDROID_READY" -eq 1 ] || return 1
    [ "$SCREEN_W" -gt 0 ] && [ "$SCREEN_H" -gt 0 ] || return 1

    local dir="$SCREEN_DIR/map-zoom-gallery"
    local table="$METRIC_DIR/map_zoom_gallery.tsv"
    mkdir -p "$dir"
    printf 'index\tdirection\tzoom\tsha256\tfile\n' > "$table"

    android_launch_clean || return 1
    local pid
    pid="$(android_pid)"
    [ -n "$pid" ] || return 1

    # Reach the exact minimum one acknowledged/rendered pinch at a time.
    local guard=0
    while [ "$guard" -lt 16 ]; do
        android_apply_audit_pinch "$pid" 142 || return 1
        awk -v z="$AUDIT_PINCH_ZOOM" 'BEGIN {exit !(z <= 6.001)}' && break
        guard=$((guard + 1))
    done

    local file hash
    file="$dir/00_zoom_min.png"
    android_capture "$file" || return 1
    hash="$(hash_file "$file" || true)"
    printf '0\tout_to_min\t%s\t%s\t%s\n' \
        "$AUDIT_PINCH_ZOOM" "$hash" "${file#$OUTPUT_DIR/}" >> "$table"

    local i=1
    while [ "$i" -le 12 ]; do
        android_apply_audit_pinch "$pid" 141 || return 1
        file="$dir/$(printf '%02d' "$i")_zoom_in.png"
        android_capture "$file" || return 1
        hash="$(hash_file "$file" || true)"
        printf '%s\tin\t%s\t%s\t%s\n' \
            "$i" "$AUDIT_PINCH_ZOOM" "$hash" "${file#$OUTPUT_DIR/}" >> "$table"
        i=$((i + 1))
    done

    android_apply_audit_pinch "$pid" 142 || return 1
    file="$dir/13_zoom_out_return.png"
    android_capture "$file" || return 1
    hash="$(hash_file "$file" || true)"
    printf '13\tout\t%s\t%s\t%s\n' \
        "$AUDIT_PINCH_ZOOM" "$hash" "${file#$OUTPUT_DIR/}" >> "$table"

    cat "$table"

    # Validate the expected sequence, not only screenshot diversity.
    local expected="6 7 8 9 10 11 12 13 14 15 16 17 18 17"
    local actual
    actual="$(
        tail -n +2 "$table" \
            | cut -f3 \
            | awk '{printf "%d%s", $1 + 0.5, (NR == 14 ? "" : " ")}'
    )"
    echo "expected_zoom_sequence=$expected"
    echo "actual_zoom_sequence=$actual"
    [ "$actual" = "$expected" ] || {
        echo "ERROR: synchronized pinch gallery did not produce the exact z6..z18..z17 sequence."
        return 1
    }

    local distinct
    distinct="$(
        tail -n +2 "$table" | cut -f4 \
            | awk 'NF {seen[$0]=1} END {for (k in seen) n++; print n+0}'
    )"
    echo "distinct_screenshots=$distinct"
    [ "$distinct" -ge 12 ] || {
        echo "ERROR: too few distinct rendered zoom states."
        return 1
    }
}

ANDROID_SWEEP_VIDEO_REMOTE=""
ANDROID_SWEEP_VIDEO_PID=""

android_zoom_sweep_video_start() {
    local destination="$1"
    mkdir -p "$(dirname "$destination")"

    ANDROID_SWEEP_VIDEO_REMOTE="/sdcard/openride-zoom-sweep-$$.mp4"
    ANDROID_SWEEP_VIDEO_PID=""

    "${ADB[@]}" shell rm -f "$ANDROID_SWEEP_VIDEO_REMOTE" >/dev/null 2>&1 || true

    local remote_pid
    remote_pid="$(
        "${ADB[@]}" shell \
            "screenrecord --bit-rate 6000000 --time-limit 90 '$ANDROID_SWEEP_VIDEO_REMOTE' >/dev/null 2>&1 & echo \$!" \
            2>/dev/null | tr -d '\r' | tail -n 1
    )"

    case "$remote_pid" in
        ''|*[!0-9]*)
            echo "Invalid screenrecord PID: ${remote_pid:-<empty>}" >&2
            return 1
            ;;
    esac

    ANDROID_SWEEP_VIDEO_PID="$remote_pid"

    sleep 0.20
    "${ADB[@]}" shell \
        "kill -0 '$ANDROID_SWEEP_VIDEO_PID' >/dev/null 2>&1" \
        >/dev/null 2>&1 || {
        echo "screenrecord exited immediately." >&2
        return 1
    }

    return 0
}

android_zoom_sweep_video_stop() {
    local destination="$1"

    [ -n "$ANDROID_SWEEP_VIDEO_PID" ] || return 1
    [ -n "$ANDROID_SWEEP_VIDEO_REMOTE" ] || return 1

    "${ADB[@]}" shell \
        "kill -INT '$ANDROID_SWEEP_VIDEO_PID' >/dev/null 2>&1 || true" \
        >/dev/null 2>&1 || true

    local deadline=$((SECONDS + 6))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if ! "${ADB[@]}" shell \
            "kill -0 '$ANDROID_SWEEP_VIDEO_PID' >/dev/null 2>&1" \
            >/dev/null 2>&1; then
            break
        fi
        sleep 0.20
    done

    if "${ADB[@]}" shell \
        "kill -0 '$ANDROID_SWEEP_VIDEO_PID' >/dev/null 2>&1" \
        >/dev/null 2>&1; then
        "${ADB[@]}" shell \
            "kill -TERM '$ANDROID_SWEEP_VIDEO_PID' >/dev/null 2>&1 || true" \
            >/dev/null 2>&1 || true
        sleep 0.50
    fi

    sleep 0.40

    local remote_bytes
    remote_bytes="$(
        "${ADB[@]}" shell \
            "wc -c < '$ANDROID_SWEEP_VIDEO_REMOTE' 2>/dev/null || echo 0" \
            | tr -d '\r[:space:]'
    )"
    case "$remote_bytes" in
        ''|*[!0-9]*) remote_bytes=0 ;;
    esac

    if [ "$remote_bytes" -lt 16384 ]; then
        echo "Recorded MP4 missing or too small: ${remote_bytes} bytes" >&2
        return 1
    fi

    rm -f "$destination"
    "${ADB[@]}" pull "$ANDROID_SWEEP_VIDEO_REMOTE" "$destination" \
        >/dev/null 2>&1 || return 1

    [ -s "$destination" ] || return 1

    "${ADB[@]}" shell rm -f "$ANDROID_SWEEP_VIDEO_REMOTE" >/dev/null 2>&1 || true

    echo "Video size: $(wc -c < "$destination" | tr -d ' ') bytes"

    ANDROID_SWEEP_VIDEO_REMOTE=""
    ANDROID_SWEEP_VIDEO_PID=""
    return 0
}

android_zoom_sweep() {
    [ "$ANDROID_READY" -eq 1 ] || return 1

    local dir="$SCREEN_DIR/map-zoom-sweep"
    mkdir -p "$dir"

    android_launch_clean || return 1
    local pid
    pid="$(android_pid)"
    [ -n "$pid" ] || return 1

    local video_started=0
    local video_path="$VIDEO_DIR/android_zoom_sweep.mp4"

    if [ "$SWEEP_VIDEO" -eq 1 ]; then
        android_zoom_sweep_video_start "$video_path" || {
            echo "ERROR: unable to start zoom-sweep video recording."
            return 1
        }
        video_started=1
        echo "Video recording: $video_path"
        sleep 0.35
    else
        echo "Zoom-sweep video disabled (--no-sweep-video)."
    fi

    local sweep_rc=0
    local start_before
    start_before="$(android_log_count "$pid" 'AUDIT_ZOOM_SWEEP_STARTED')"
    "${ADB[@]}" shell input keyevent 140 >/dev/null

    android_wait_log_count "$pid" 'AUDIT_ZOOM_SWEEP_STARTED' \
        $((start_before + 1)) 8 || {
        echo "ERROR: zoom sweep start event was not acknowledged."
        sweep_rc=1
    }

    if [ "$sweep_rc" -eq 0 ]; then
        android_logcat_for_pid "$pid" \
            | grep 'AUDIT_ZOOM_SWEEP_STARTED' \
            | tail -n 1
    fi

    local csv="$METRIC_DIR/android_map_zoom_test.csv"
    rm -f "$csv"

    local i=0
    local deadline=$((SECONDS + 50))
    while [ "$sweep_rc" -eq 0 ] && [ "$SECONDS" -lt "$deadline" ]; do
        if [ "$i" -le 8 ]; then
            android_capture "$dir/$(printf '%02d' "$i")_sweep.png" || {
                echo "ERROR: screenshot capture failed during zoom sweep."
                sweep_rc=1
                break
            }
            i=$((i + 1))
        fi

        "${ADB[@]}" exec-out run-as "$PACKAGE" cat files/data/map-zoom-test.csv \
            > "$csv" 2>/dev/null || true

        if [ -s "$csv" ] && grep -q '^# samples=' "$csv"; then
            break
        fi
        sleep 3.5
    done

    if [ "$sweep_rc" -eq 0 ] \
        && { [ ! -s "$csv" ] || ! grep -q '^# samples=' "$csv"; }; then
        echo "ERROR: completed data/map-zoom-test.csv was not observed within 50 seconds."
        sweep_rc=1
    fi

    if [ "$video_started" -eq 1 ]; then
        android_zoom_sweep_video_stop "$video_path" || {
            echo "ERROR: zoom-sweep video could not be finalized."
            sweep_rc=1
        }
    fi

    [ "$sweep_rc" -eq 0 ] || return 1

    grep '^#' "$csv" | head -n 50

    if [ "$SWEEP_VIDEO" -eq 1 ]; then
        echo
        echo "LOD loading video:"
        echo "  videos/android_zoom_sweep.mp4"
        echo "NOTE: use --no-sweep-video for strict performance comparisons."
    fi
}


android_map_style_gallery() {
    [ "$ANDROID_READY" -eq 1 ] || return 1

    local dir="$SCREEN_DIR/map-styles"
    mkdir -p "$dir"
    android_launch_clean || return 1

    android_capture "$dir/00_style_initial.png" || return 1

    local i=1
    while [ "$i" -le 3 ]; do
        "${ADB[@]}" shell input keyevent 41 >/dev/null 2>&1 || true
        sleep 0.45
        android_capture "$dir/$(printf '%02d' "$i")_style_cycle.png" || return 1
        i=$((i + 1))
    done

    echo "Captured initial style + 3 cycles; final cycle should restore the original style."
}

android_lifecycle_stress() {
    [ "$ANDROID_READY" -eq 1 ] || return 1

    local i
    for i in 1 2 3 4 5; do
        echo "Cycle $i/5"
        android_launch_clean || return 1

        "${ADB[@]}" shell input keyevent KEYCODE_HOME >/dev/null
        sleep 0.25

        "${ADB[@]}" shell monkey \
            -p "$PACKAGE" \
            -c android.intent.category.LAUNCHER \
            1 >/dev/null 2>&1 || return 1

        local pid=""
        local deadline=$((SECONDS + 6))
        while [ "$SECONDS" -lt "$deadline" ]; do
            pid="$(android_pid)"
            [ -n "$pid" ] && break
            sleep 0.10
        done
        [ -n "$pid" ] || return 1
        android_wait_first_frame "$pid" 8 >/dev/null || return 1
    done
}

android_ui_tour() {
    [ "$ANDROID_READY" -eq 1 ] || return 1
    [ "$SCREEN_W" -gt 0 ] && [ "$SCREEN_H" -gt 0 ] || return 1

    local dir="$SCREEN_DIR/ui-tour"
    mkdir -p "$dir"

    local toolbar_y menu_x search_x route_x loop_x
    local menu_center_x fav_y history_y offline_y settings_y
    toolbar_y="$(pct_px "$SCREEN_H" 0.938)"
    menu_x="$(pct_px "$SCREEN_W" 0.178)"
    search_x="$(pct_px "$SCREEN_W" 0.337)"
    route_x="$(pct_px "$SCREEN_W" 0.500)"
    loop_x="$(pct_px "$SCREEN_W" 0.662)"
    menu_center_x="$(pct_px "$SCREEN_W" 0.500)"
    fav_y="$(pct_px "$SCREEN_H" 0.446)"
    history_y="$(pct_px "$SCREEN_H" 0.511)"
    offline_y="$(pct_px "$SCREEN_H" 0.577)"
    settings_y="$(pct_px "$SCREEN_H" 0.642)"

    local baseline_hash target_hash

    android_launch_clean || return 1
    android_capture "$dir/01_map.png" || return 1
    baseline_hash="$(hash_file "$dir/01_map.png" || true)"

    android_launch_clean || return 1
    "${ADB[@]}" shell input tap "$menu_x" "$toolbar_y" >/dev/null
    sleep 0.55
    android_capture "$dir/02_main_menu.png" || return 1
    target_hash="$(hash_file "$dir/02_main_menu.png" || true)"
    [ -z "$baseline_hash" ] || [ "$target_hash" != "$baseline_hash" ] || {
        echo "ERROR: Main menu capture is identical to the clean map."
        return 1
    }

    android_launch_clean || return 1
    "${ADB[@]}" shell input tap "$search_x" "$toolbar_y" >/dev/null
    sleep 0.75
    android_capture "$dir/03_search.png" || return 1
    target_hash="$(hash_file "$dir/03_search.png" || true)"
    [ -z "$baseline_hash" ] || [ "$target_hash" != "$baseline_hash" ] || {
        echo "ERROR: Search capture is identical to the clean map."
        return 1
    }

    android_launch_clean || return 1
    "${ADB[@]}" shell input tap "$route_x" "$toolbar_y" >/dev/null
    sleep 0.60
    android_capture "$dir/04_route.png" || return 1
    target_hash="$(hash_file "$dir/04_route.png" || true)"
    [ -z "$baseline_hash" ] || [ "$target_hash" != "$baseline_hash" ] || {
        echo "ERROR: Route capture is identical to the clean map."
        return 1
    }

    android_launch_clean || return 1
    "${ADB[@]}" shell input tap "$loop_x" "$toolbar_y" >/dev/null
    sleep 0.60
    android_capture "$dir/05_loop.png" || return 1
    target_hash="$(hash_file "$dir/05_loop.png" || true)"
    [ -z "$baseline_hash" ] || [ "$target_hash" != "$baseline_hash" ] || {
        echo "ERROR: Loop capture is identical to the clean map."
        return 1
    }

    local name row_y
    for spec in \
        "06_favorites.png:$fav_y" \
        "07_history.png:$history_y" \
        "08_offline_maps.png:$offline_y" \
        "09_settings.png:$settings_y"; do
        name="${spec%%:*}"
        row_y="${spec#*:}"
        android_launch_clean || return 1
        "${ADB[@]}" shell input tap "$menu_x" "$toolbar_y" >/dev/null
        sleep 0.45
        "${ADB[@]}" shell input tap "$menu_center_x" "$row_y" >/dev/null
        sleep 0.60
        android_capture "$dir/$name" || return 1
        target_hash="$(hash_file "$dir/$name" || true)"
        [ -z "$baseline_hash" ] || [ "$target_hash" != "$baseline_hash" ] || {
            echo "ERROR: $name is identical to the clean map."
            return 1
        }
    done

    local distinct
    distinct="$(
        find "$dir" -name '*.png' -type f -print \
            | while IFS= read -r file; do hash_file "$file"; done \
            | awk 'NF {seen[$0]=1} END {for (k in seen) n++; print n+0}'
    )"
    echo "ui_tour_distinct_screens=$distinct"
    [ "$distinct" -ge 7 ] || {
        echo "ERROR: UI tour produced only $distinct distinct visual states."
        return 1
    }
}

android_logcat_clear() {
    [ "$ANDROID_READY" -eq 1 ] || return 1
    "${ADB[@]}" logcat -c
}

android_logcat_collect() {
    [ "$ANDROID_READY" -eq 1 ] || return 1

    "${ADB[@]}" logcat -d -v threadtime \
        > "$DEVICE_DIR/logcat_full.txt" 2>&1 || true

    grep -Ei \
        "$PACKAGE|OpenRide|AUDIT_|AndroidRuntime|FATAL EXCEPTION|ANR in|Fatal signal|DEBUG.*backtrace" \
        "$DEVICE_DIR/logcat_full.txt" \
        > "$DEVICE_DIR/logcat_relevant.txt" || true

    echo "Relevant log lines: $(wc -l < "$DEVICE_DIR/logcat_relevant.txt" | tr -d ' ')"
    tail -n 120 "$DEVICE_DIR/logcat_relevant.txt" || true
}

android_crash_scan() {
    local file="$DEVICE_DIR/logcat_full.txt"
    [ -f "$file" ] || {
        echo "logcat_full.txt not found"
        return 1
    }

    local hits="$DEVICE_DIR/logcat_crash_hits.txt"
    grep -Ei \
        'FATAL EXCEPTION|ANR in|Fatal signal [0-9]+|native crash|tombstone|Process has died|OutOfMemoryError' \
        "$file" > "$hits" || true

    if [ -s "$hits" ]; then
        echo "Potential crash/ANR/OOM signatures detected after logcat was cleared:"
        cat "$hits"
        return 1
    fi

    echo "No crash/FATAL/ANR/OOM signature detected."
}

screenshot_integrity() {
    local list="$METRIC_DIR/screenshots.txt"
    : > "$list"

    find "$SCREEN_DIR" -type f -name '*.png' -print 2>/dev/null \
        | sort > "$list"

    local count
    count="$(wc -l < "$list" | tr -d ' ')"
    echo "PNG screenshots: $count"
    [ "$count" -gt 0 ] || return 1

    local failed=0
    while IFS= read -r file; do
        [ -n "$file" ] || continue
        if [ ! -s "$file" ]; then
            echo "EMPTY: $file"
            failed=1
            continue
        fi

        if command -v sips >/dev/null 2>&1; then
            if ! sips -g pixelWidth -g pixelHeight "$file" >/dev/null 2>&1; then
                echo "INVALID PNG: $file"
                failed=1
            fi
        fi
    done < "$list"

    [ "$failed" -eq 0 ]
}

screenshot_duplicate_analysis() {
    local hashes="$METRIC_DIR/screenshot_hashes.tsv"
    local duplicates="$METRIC_DIR/screenshot_duplicates.txt"
    : > "$hashes"
    : > "$duplicates"

    if ! command -v shasum >/dev/null 2>&1 && ! command -v sha256sum >/dev/null 2>&1; then
        echo "No SHA-256 command available."
        return 0
    fi

    find "$SCREEN_DIR" -type f -name '*.png' -print 2>/dev/null | sort \
        | while IFS= read -r file; do
            [ -n "$file" ] || continue
            printf '%s\t%s\n' "$(hash_file "$file")" "$file"
        done > "$hashes"

    awk -F '\t' '
        {
            if (seen[$1] != "") {
                print "DUPLICATE:";
                print "  " seen[$1];
                print "  " $2;
            } else {
                seen[$1] = $2;
            }
        }
    ' "$hashes" > "$duplicates"

    if [ -s "$duplicates" ]; then
        cat "$duplicates"
        return 2
    fi

    echo "No byte-identical screenshots detected."
}

make_manifest() {
    : > "$MANIFEST_FILE"
    find "$OUTPUT_DIR" -type f ! -name "$(basename "$MANIFEST_FILE")" -print \
        | sort \
        | while IFS= read -r file; do
            [ -n "$file" ] || continue
            local_hash="$(hash_file "$file" 2>/dev/null || true)"
            [ -n "$local_hash" ] || continue
            rel="${file#$OUTPUT_DIR/}"
            printf '%s  %s\n' "$local_hash" "$rel"
        done > "$MANIFEST_FILE"
}

generate_report() {
    local head branch dirty
    head="$(git rev-parse HEAD 2>/dev/null || echo unknown)"
    branch="$(git branch --show-current 2>/dev/null || echo unknown)"
    dirty="$(git status --porcelain 2>/dev/null | wc -l | tr -d ' ')"

    local pass fail warn skip
    pass="$(awk -F '\t' 'NR>1 && $3=="PASS" {n++} END {print n+0}' "$STATUS_FILE")"
    fail="$(awk -F '\t' 'NR>1 && $3=="FAIL" {n++} END {print n+0}' "$STATUS_FILE")"
    warn="$(awk -F '\t' 'NR>1 && $3=="WARN" {n++} END {print n+0}' "$STATUS_FILE")"
    skip="$(awk -F '\t' 'NR>1 && $3=="SKIP" {n++} END {print n+0}' "$STATUS_FILE")"

    local test_summary=""
    if [ -f "$LOG_DIR/ctest_all.log" ]; then
        test_summary="$(grep -E '[0-9]+% tests passed|tests failed out of' "$LOG_DIR/ctest_all.log" | tail -n 1 || true)"
    fi

    local screenshot_count=0
    if [ -d "$SCREEN_DIR" ]; then
        screenshot_count="$(find "$SCREEN_DIR" -type f -name '*.png' 2>/dev/null | wc -l | tr -d ' ')"
    fi

    {
        echo "# OpenRide Global Audit V2.2"
        echo
        echo "- Date: $(date '+%Y-%m-%d %H:%M:%S %z')"
        echo "- Branch: \`$branch\`"
        echo "- Commit: \`$head\`"
        echo "- Worktree entries at report time: $dirty"
        echo "- Android package: \`$PACKAGE\`"
        echo "- Audit profile: \`$AUDIT_PROFILE\`"
        if [ -n "$ONLY_STEPS" ]; then
            echo "- Explicit only-selection: \`$ONLY_STEPS\`"
        fi
        if [ -n "$SKIP_STEPS" ]; then
            echo "- Explicit skipped steps: \`$SKIP_STEPS\`"
        fi
        if [ "$REUSE_ANDROID" -eq 1 ]; then
            echo "- Android APK mode: reuse already-installed APK"
        fi
        echo "- Screenshots captured: $screenshot_count"
        echo "- Zoom-sweep video: $([ "$SWEEP_VIDEO" -eq 1 ] && echo enabled || echo disabled)"
        if [ -n "$test_summary" ]; then
            echo "- CTest: $test_summary"
        fi
        echo
        echo "## Automated summary"
        echo
        echo "| Result | Count |"
        echo "|---|---:|"
        echo "| PASS | $pass |"
        echo "| FAIL | $fail |"
        echo "| WARN | $warn |"
        echo "| SKIP | $skip |"
        echo
        echo "## Step results"
        echo
        echo "| Step | Status | Time | Evidence |"
        echo "|---|---|---:|---|"

        tail -n +2 "$STATUS_FILE" | while IFS=$'\t' read -r id label status rc duration log; do
            printf '| %s | **%s** | %ss | `%s` |\n' \
                "$label" "$status" "$duration" "$log"
        done

        echo
        echo "## Coverage"
        echo
        echo "| Area | Coverage |"
        echo "|---|---|"
        echo "| Core C/CMake | Full automated build + CTest suite |"
        echo "| Routing/navigation | Unit/scenario tests + optional real graph benchmarks |"
        echo "| Tap/pan touch semantics | Native touch tests |"
        echo "| Pinch semantics | Native touch tests |"
        echo "| Real Android pan | Physical ADB swipe + before/after screenshots |"
        echo "| Real toolbar/menu taps | Android UI tour |"
        echo "| Android Back | Physical key event + before/after screenshots |"
        echo "| Lifecycle | 5 stop/start + Home/resume cycles |"
        echo "| Crash/ANR | Fresh logcat window scanned after Android audit |"
        echo "| Android startup | Activity timing x3 + OpenRide first rendered frame marker x3 |"
        echo "| Android performance | CPU/memory/gfx + per-thread CPU + internal frame pacing at 2s, 10s and 30s |"
        echo "| Map pan | Physical ADB swipe + before/after screenshots after first-frame readiness |"
        echo "| Pinch application path | Android F11/F12 audit hooks call the same production pinch helper as SDL_EVENT_PINCH_UPDATE |"
        echo "| Map zoom gallery | Event + rendered-frame synchronized exact z6 -> z18 -> z17 sequence |"
        echo "| Map renderer sweep | z9 -> z17 -> z9 screenshots + per-frame CSV profiling around the current map center |"
        echo "| Map styles | Initial map + three style cycles, restoring the initial style |"
        echo "| UI screens | Map, menu, search, route, loop, favorites, history, offline maps, settings; unchanged captures fail the step |"
        echo "| Visual quality/coherence | Evidence captured; human/vision review required |"
        echo "| Final physical two-finger OS->SDL link | Stock adb cannot reliably synthesize this on every retail device; the application pinch path is nevertheless exercised |"
        echo "| Real-world GPS/riding behavior | Navigation scenarios are simulated; an actual ride remains a field test |"
        echo
        echo "## Visual review checklist"
        echo
        echo "The automated audit intentionally does not pretend that aesthetics can be reduced to a numeric pixel score."
        echo "Review the screenshots for:"
        echo
        echo "- compare the z6..z18 gallery for LOD transitions, missing layers, seams and sudden style changes;"
        echo "- inspect videos/android_zoom_sweep.mp4 for LOD popping, delayed loading, seams and transient layer changes;"
        echo "- compare the z9->z17->z9 sweep for transient loading artifacts and frame-time spikes;"
        echo "- inspect first-frame-ready versus +500ms captures for black/blank startup frames;"
        echo "- map hierarchy: roads, paths, buildings, water, landcover and route contrast;"
        echo "- clipping/seams or incomplete vector/mask rendering;"
        echo "- safe-area handling and overlap with Android system UI;"
        echo "- text truncation, baseline alignment, typography consistency and contrast;"
        echo "- minimum practical touch sizes and spacing;"
        echo "- consistency of panels, bottom sheets, toolbar and empty states;"
        echo "- visual feedback after navigation/back/pan actions;"
        echo "- information hierarchy while riding: map remains primary, controls secondary;"
        echo "- any screen that is visually inconsistent with the rest of OpenRide."
        echo
        echo "## Decision gate"
        echo
        if [ "$AUDIT_PROFILE" != "full" ] || [ -n "$ONLY_STEPS" ]; then
            if [ "$fail" -gt 0 ]; then
                echo "**Partial audit failed.** Fix the selected failing checks before relying on this diagnostic run."
            else
                echo "**Partial diagnostic audit completed.** This profile/selection is intentionally not a full release gate."
            fi
        elif [ "$fail" -gt 0 ]; then
            echo "**STOP / fix first.** At least one required automated check failed."
        elif [ "$warn" -gt 0 ]; then
            echo "**Technical gate mostly green.** Resolve or consciously accept WARN items, then perform the visual review before new feature work."
        else
            echo "**Automated technical gate green.** Perform the visual review of the captured evidence before new feature work."
        fi
        echo
        echo "## Evidence"
        echo
        echo "- \`repository.txt\` — exact Git state"
        echo "- \`environment.txt\` — host/toolchain"
        echo "- \`data_inventory.txt\` — local offline data"
        echo "- \`logs/\` — build/test/audit logs"
        echo "- \`metrics/android_startup_summary.txt\` — Activity and OpenRide first-frame timings"
        echo "- \`metrics/map_zoom_gallery.tsv\` — pinch-driven zoom levels and screenshot hashes"
        echo "- \`metrics/android_map_zoom_test.csv\` — z9->z17->z9 per-frame renderer profile"
        echo "- \`metrics/\` — warnings, startup, CPU/memory/gfx and screenshot hashes"
        echo "- \`device/\` — Android device/package/logcat evidence"
        echo "- \`screenshots/\` — UI tour + real gesture captures"
        echo "- \`videos/android_zoom_sweep.mp4\` — continuous LOD-loading video during renderer sweep"
        echo "- \`manifest.sha256\` — artifact integrity"
    } > "$REPORT_FILE"

    {
        echo "# Review this OpenRide audit with ChatGPT"
        echo
        echo "Upload the generated ZIP to the OpenRide project conversation."
        echo
        echo "Ask for a global review covering:"
        echo
        echo "1. automated failures/warnings and their likely causes;"
        echo "2. map rendering quality and hierarchy;"
        echo "3. UI visual coherence and consistency;"
        echo "4. Android gesture/navigation evidence;"
        echo "5. performance signals;"
        echo "6. architecture/test coverage gaps;"
        echo "7. a prioritized P0/P1/P2/P3 roadmap before feature development resumes."
        echo
        echo "The report is in: report.md"
    } > "$REVIEW_FILE"

    cat "$REPORT_FILE"
}

make_zip() {
    [ "$MAKE_ZIP" -eq 1 ] || return 0

    local parent base zip_path
    parent="$(dirname "$OUTPUT_DIR")"
    base="$(basename "$OUTPUT_DIR")"
    zip_path="${OUTPUT_DIR}.zip"

    rm -f "$zip_path"

    if command -v zip >/dev/null 2>&1; then
        (
            cd "$parent" || exit 1
            zip -qr "$zip_path" "$base"
        )
    elif command -v ditto >/dev/null 2>&1; then
        ditto -c -k --sequesterRsrc --keepParent "$OUTPUT_DIR" "$zip_path"
    else
        echo "No zip or ditto command available."
        return 1
    fi

    echo "$zip_path"
}

echo "=============================================================="
echo " OpenRide Global Audit"
echo "=============================================================="
echo "Repository : $ROOT_DIR"
echo "Output     : $OUTPUT_DIR"
echo "Package    : $PACKAGE"
echo "Profile    : $AUDIT_PROFILE"
[ -z "$ONLY_STEPS" ] || echo "Only       : $ONLY_STEPS"
[ -z "$SKIP_STEPS" ] || echo "Skip       : $SKIP_STEPS"
[ "$REUSE_ANDROID" -eq 0 ] || echo "Android APK: reuse installed"
echo

# ---------------------------------------------------------------------------
# Host/repository audit
# ---------------------------------------------------------------------------

run_step "repo_info" "Repository identity" "required" repo_info
run_step "environment" "Host/toolchain inventory" "required" environment_info
run_step "git_diff_check" "Git whitespace/diff consistency" "required" git_cleanliness

if audit_step_enabled "git_worktree"; then
    WORKTREE_COUNT="$(git status --porcelain 2>/dev/null | wc -l | tr -d ' ')"
    if [ "$WORKTREE_COUNT" -gt 0 ]; then
        record_warn "git_worktree" "Git worktree cleanliness" \
            "$WORKTREE_COUNT worktree entry/entries detected; see repository.txt"
    else
        record_status "git_worktree" "Git worktree cleanliness" "PASS" "0" "0" "repository.txt"
    fi
else
    record_skip "git_worktree" "Git worktree cleanliness" \
        "$(audit_filter_reason "git_worktree")"
fi

run_step "data_inventory" "Offline data inventory" "optional" data_inventory

# ---------------------------------------------------------------------------
# Native build + tests
# ---------------------------------------------------------------------------

if [ "$SKIP_BUILD" -eq 0 ]; then
    run_step "configure_macos" "Configure macOS build" "required" ./scripts/configure.sh
    run_step "build_macos" "Compile complete macOS target set" "required" ./scripts/build.sh
else
    record_skip "configure_macos" "Configure macOS build" "--skip-build requested"
    record_skip "build_macos" "Compile complete macOS target set" "--skip-build requested"
fi

run_step "ctest_inventory" "CTest inventory" "required" collect_ctest_inventory
run_step "ctest_all" "Complete CTest suite" "required" run_all_ctest
run_step "build_warnings" "Build/configuration warning inventory" "optional" collect_build_warning_inventory

# ---------------------------------------------------------------------------
# Real-data benchmarks
# ---------------------------------------------------------------------------

if [ "$SKIP_BENCHMARKS" -eq 1 ]; then
    record_skip "benchmark_spatial" "Routing spatial-index benchmark" "--skip-benchmarks requested"
    record_skip "benchmark_segment" "Routing segment-index benchmark" "--skip-benchmarks requested"
    record_skip "benchmark_loop" "Loop generator benchmark" "--skip-benchmarks requested"
elif [ -f data/routing/nord-pas-de-calais.orgraph ]; then
    run_step "benchmark_spatial" "Routing spatial-index benchmark" "optional" benchmark_spatial
    run_step "benchmark_segment" "Routing segment-index benchmark" "optional" benchmark_segment
    run_step "benchmark_loop" "Loop generator benchmark (${AUDIT_LOOP_KM} km ${AUDIT_LOOP_PROFILE})" "optional" benchmark_loop
else
    record_skip "benchmark_spatial" "Routing spatial-index benchmark" "data/routing/nord-pas-de-calais.orgraph absent"
    record_skip "benchmark_segment" "Routing segment-index benchmark" "data/routing/nord-pas-de-calais.orgraph absent"
    record_skip "benchmark_loop" "Loop generator benchmark" "data/routing/nord-pas-de-calais.orgraph absent"
fi

# ---------------------------------------------------------------------------
# Android audit
# ---------------------------------------------------------------------------

if [ "$SKIP_ANDROID" -eq 1 ] || ! audit_any_android_enabled; then
    record_skip "android_setup" "Android device detection" "Android steps disabled by --skip-android/profile selection"
    record_skip "android_check" "Android toolchain validation" "Android steps disabled by --skip-android/profile selection"
    record_skip "android_build" "Android APK build" "Android steps disabled by --skip-android/profile selection"
    record_skip "android_install" "Android APK install/update" "Android steps disabled by --skip-android/profile selection"
    record_skip "android_startup" "Android Activity + first-frame benchmark" "Android steps disabled by --skip-android/profile selection"
    record_skip "android_idle_profile" "Android stabilized runtime profile" "Android steps disabled by --skip-android/profile selection"
    record_skip "android_map_stability" "Android time-to-stable-map measurement" "Android steps disabled by --skip-android/profile selection"
    record_skip "android_pan" "Real Android map pan gesture" "Android steps disabled by --skip-android/profile selection"
    record_skip "android_back" "Android Back navigation semantics" "Android steps disabled by --skip-android/profile selection"
    record_skip "android_zoom_gallery" "Android pinch-path map zoom gallery" "Android steps disabled by --skip-android/profile selection"
    record_skip "android_zoom_sweep" "Android map renderer zoom sweep" "Android steps disabled by --skip-android/profile selection"
    record_skip "android_map_styles" "Android map-style gallery" "Android steps disabled by --skip-android/profile selection"
    record_skip "android_ui_tour" "Android UI screenshot tour" "Android steps disabled by --skip-android/profile selection"
else
    run_step "android_setup" "Android device detection" "required" setup_adb

    if [ "$ANDROID_READY" -eq 1 ]; then
        run_step "android_device" "Android device inventory" "required" android_device_info

        if [ "$REUSE_ANDROID" -eq 0 ]; then
            run_step "android_check" "Android toolchain validation" "required" ./scripts/android_check.sh
            run_step "android_build" "Android APK build" "required" ./scripts/android_build.sh

            if [ "$NO_INSTALL" -eq 0 ]; then
                run_step "android_install" "Android APK install/update" "required" ./scripts/android_install.sh
            else
                record_skip "android_install" "Android APK install/update" "--no-install requested"
            fi
        else
            record_skip "android_check" "Android toolchain validation" "--reuse-android requested"
            record_skip "android_build" "Android APK build" "--reuse-android requested"
            record_skip "android_install" "Android APK install/update" "--reuse-android requested"
        fi

        run_step "android_package" "Android package inventory" "optional" android_package_info
        run_step "android_permissions" "Android deterministic runtime permissions" "optional" android_grant_test_permissions
        run_step "android_data" "Android offline data inventory" "optional" android_data_inventory
        run_step "android_logcat_clear" "Clear Android logcat audit window" "required" android_logcat_clear
        run_step "android_startup" "Android Activity + first-frame benchmark x3" "required" android_startup_benchmark
        run_step "android_idle_profile" "Android stabilized runtime profile T+2/T+10/T+30" "required" android_idle_runtime_profile
        run_step "android_map_stability" "Android time-to-stable-map measurement" "required" android_map_stability_test
        run_step "android_pan" "Real Android map pan gesture" "required" android_pan_gesture_test
        run_step "android_back" "Android Back navigation semantics" "required" android_back_navigation_test
        run_step "android_zoom_gallery" "Android pinch-path map zoom gallery" "required" android_zoom_gallery
        run_step "android_zoom_sweep" "Android z9->z17->z9 renderer sweep" "required" android_zoom_sweep
        run_step "android_map_styles" "Android map-style gallery" "optional" android_map_style_gallery
        run_step "android_ui_tour" "Android UI screenshot tour" "required" android_ui_tour
        run_step "android_lifecycle" "Android lifecycle/relaunch stress" "required" android_lifecycle_stress
        run_step "android_logcat" "Collect Android logcat evidence" "required" android_logcat_collect
        run_step "android_crash_scan" "Android crash/FATAL/ANR/OOM scan" "required" android_crash_scan
    else
        for item in \
            android_device android_check android_build android_install android_package \
            android_permissions android_data android_startup android_idle_profile android_map_stability android_pan \
            android_back android_zoom_gallery android_zoom_sweep android_map_styles android_ui_tour \
            android_lifecycle android_logcat android_crash_scan; do
            record_skip "$item" "$item" "device detection failed"
        done
    fi
fi

# ---------------------------------------------------------------------------
# Screenshot/evidence validation
# ---------------------------------------------------------------------------

if find "$SCREEN_DIR" -type f -name '*.png' -print -quit 2>/dev/null | grep -q .; then
    run_step "screenshot_integrity" "Screenshot PNG integrity" "required" screenshot_integrity

    # Duplicate screens are useful evidence, but explicit/profile selection must
    # be respected just like every run_step-managed check.
    if audit_step_enabled "screenshot_duplicates"; then
        DUP_LOG="$LOG_DIR/screenshot_duplicates.log"
        screenshot_duplicate_analysis 2>&1 | tee "$DUP_LOG"
        DUP_RC=${PIPESTATUS[0]}
        if [ "$DUP_RC" -eq 2 ]; then
            record_status "screenshot_duplicates" "Exact screenshot duplicate analysis" "WARN" "2" "0" "logs/screenshot_duplicates.log"
        elif [ "$DUP_RC" -eq 0 ]; then
            record_status "screenshot_duplicates" "Exact screenshot duplicate analysis" "PASS" "0" "0" "logs/screenshot_duplicates.log"
        else
            record_status "screenshot_duplicates" "Exact screenshot duplicate analysis" "WARN" "$DUP_RC" "0" "logs/screenshot_duplicates.log"
        fi
    else
        record_skip "screenshot_duplicates" "Exact screenshot duplicate analysis" \
            "$(audit_filter_reason "screenshot_duplicates")"
    fi
else
    record_skip "screenshot_integrity" "Screenshot PNG integrity" "no screenshots produced"
    record_skip "screenshot_duplicates" "Exact screenshot duplicate analysis" "no screenshots produced"
fi

# The physical two-finger limitation is only relevant when that coverage was
# actually selected in this run.
if audit_step_enabled "android_physical_multitouch"; then
    record_warn "android_physical_multitouch" "Physical two-finger Android injection" \
        "Application pinch handling is exercised and captured by android_zoom_gallery; stock adb still cannot reliably synthesize the final physical two-finger OS->SDL event stream on every retail device."
else
    record_skip "android_physical_multitouch" "Physical two-finger Android injection" \
        "$(audit_filter_reason "android_physical_multitouch")"
fi

# Visual/aesthetic review must use actual images, not fabricated pixel scores.
if audit_step_enabled "visual_review" \
    && find "$SCREEN_DIR" -type f -name '*.png' -print -quit 2>/dev/null | grep -q .; then
    record_status "visual_review" "Visual UI/map coherence review" "PENDING" "0" "0" "screenshots/"
else
    record_skip "visual_review" "Visual UI/map coherence review" \
        "$(audit_filter_reason "visual_review")"
fi

# ---------------------------------------------------------------------------
# Final report + integrity manifest + ZIP
# ---------------------------------------------------------------------------

generate_report
make_manifest

ZIP_PATH=""
if [ "$MAKE_ZIP" -eq 1 ]; then
    ZIP_LOG="$LOG_DIR/package_zip.log"
    make_zip 2>&1 | tee "$ZIP_LOG"
    ZIP_RC=${PIPESTATUS[0]}
    if [ "$ZIP_RC" -eq 0 ]; then
        ZIP_PATH="$(tail -n 1 "$ZIP_LOG")"
    fi
fi

FAIL_COUNT="$(awk -F '\t' 'NR>1 && $3=="FAIL" {n++} END {print n+0}' "$STATUS_FILE")"
WARN_COUNT="$(awk -F '\t' 'NR>1 && $3=="WARN" {n++} END {print n+0}' "$STATUS_FILE")"

echo
echo "=============================================================="
echo " OpenRide Global Audit finished"
echo "=============================================================="
echo "Report : $REPORT_FILE"
echo "Folder : $OUTPUT_DIR"
if [ -n "$ZIP_PATH" ]; then
    echo "ZIP    : $ZIP_PATH"
fi
echo "FAIL   : $FAIL_COUNT"
echo "WARN   : $WARN_COUNT"
echo
echo "Upload the ZIP to ChatGPT for the visual/coherence review and"
echo "the final P0/P1/P2/P3 application assessment."

if [ "$FAIL_COUNT" -gt 0 ]; then
    exit 1
fi
exit 0
