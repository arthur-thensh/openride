#!/usr/bin/env python3
"""One-shot guarded migration for UI Engine V1.14 navigation overlay.

Desktop keeps a non-drive navigation diagnostics overlay rendered by the UI
engine. Android deliberately has no intermediate navigation-status panel: the
normal map stays clear until the Drive HUD takes over.

The migration is intentionally re-runnable. It can fix both a pre-V1.14
main.c and a main.c that already received the first V1.14 implementation.

This script does not build, test, commit, or push anything.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
CMAKE = ROOT / "CMakeLists.txt"
MAIN = ROOT / "src" / "main.c"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def replace_block(text: str,
                  start_marker: str,
                  end_marker: str,
                  replacement: str,
                  label: str) -> str:
    starts = text.count(start_marker)
    if starts != 1:
        raise RuntimeError(
            f"{label}: expected exactly one start marker, found {starts}"
        )
    start = text.find(start_marker)
    end = text.find(end_marker, start + len(start_marker))
    if end < 0:
        raise RuntimeError(f"{label}: end marker not found")
    if end <= start:
        raise RuntimeError(f"{label}: invalid marker order")
    return text[:start] + replacement + text[end:]


def prepare_cmake(text: str) -> str:
    if "src/ui/ui_navigation_overlay.c" in text:
        return text
    return replace_once(
        text,
        "    src/ui/ui_drive_hud.c\n"
        "    src/ui/ui_map_overlay.c\n"
        ")",
        "    src/ui/ui_drive_hud.c\n"
        "    src/ui/ui_map_overlay.c\n"
        "    src/ui/ui_navigation_overlay.c\n"
        ")",
        "CMake V1.14 UI source",
    )


def final_navigation_overlay() -> str:
    return r'''static void draw_navigation_overlay(SDL_Renderer *renderer,
                                    const OpenRideNavigationState *navigation,
                                    const OpenRideNavigationInstructionList *instructions,
                                    const OpenRideGPSSimulator *simulator,
                                    const OpenRideRoute *route,
                                    const OpenRideNavigationSession *session,
                                    bool gps_sample_valid,
                                    bool follow_gps,
                                    bool auto_reroute,
                                    bool deviation_enabled,
                                    bool gpx_navigation,
                                    int viewport_height)
{
#ifdef __ANDROID__
    /* Android: keep the normal map clear until ui_drive_hud takes over. */
    (void)renderer;
    (void)navigation;
    (void)instructions;
    (void)simulator;
    (void)route;
    (void)session;
    (void)gps_sample_valid;
    (void)follow_gps;
    (void)auto_reroute;
    (void)deviation_enabled;
    (void)gpx_navigation;
    (void)viewport_height;
    return;
#else
    if (!gps_sample_valid || !navigation || !navigation->valid) return;

    int viewport_width = 0;
    int queried_height = viewport_height;
    SDL_GetCurrentRenderOutputSize(renderer, &viewport_width, &queried_height);
    if (viewport_width <= 0 || queried_height <= 0) return;
    viewport_height = queried_height;

    OpenRideUINavigationOverlayState state = {0};
    char title[64];
    char lines[OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES][160] = {{0}};
    uint32_t line_count = 0U;

    snprintf(title,
             sizeof(title),
             "NAVIGATION GPS SIMULEE%s",
             gpx_navigation ? " | GPX" : " | ROUTAGE");
    state.title = title;

    snprintf(lines[line_count],
             sizeof(lines[line_count]),
             "%s%s",
             openride_navigation_status_name(navigation->status),
             simulator && simulator->active ? " | lecture" : " | pause");
    state.lines[line_count] = lines[line_count];
    ++line_count;

    double instruction_distance_m = 0.0;
    const OpenRideNavigationInstruction *next_instruction =
        openride_navigation_instructions_next(instructions,
                                              navigation->traveled_m,
                                              &instruction_distance_m);
    if (next_instruction
        && line_count < OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES) {
        char maneuver_text[128];
        char distance_text[32];
        openride_navigation_instruction_text_fr(next_instruction,
                                                maneuver_text,
                                                sizeof(maneuver_text));
        openride_navigation_distance_text_fr(instruction_distance_m,
                                             distance_text,
                                             sizeof(distance_text));
        if (next_instruction->maneuver == OPENRIDE_MANEUVER_ARRIVE) {
            snprintf(lines[line_count],
                     sizeof(lines[line_count]),
                     "ARRIVEE dans %s",
                     distance_text);
        } else {
            snprintf(lines[line_count],
                     sizeof(lines[line_count]),
                     "Dans %s | %.110s",
                     distance_text,
                     maneuver_text);
        }
        state.lines[line_count] = lines[line_count];
        ++line_count;
    }

    char eta_text[32] = "--";
    if (route && route->distance_m > 0.0 && route->estimated_time_s > 0.0) {
        const double ratio = clampd(navigation->remaining_m / route->distance_m,
                                    0.0,
                                    1.0);
        format_duration(route->estimated_time_s * ratio,
                        eta_text,
                        sizeof(eta_text));
    }

    if (line_count < OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES) {
        snprintf(lines[line_count],
                 sizeof(lines[line_count]),
                 "reste %.1f km | ETA %s | progression %.1f%%",
                 navigation->remaining_m / 1000.0,
                 eta_text,
                 navigation->progress_ratio * 100.0);
        state.lines[line_count] = lines[line_count];
        ++line_count;
    }

    if (line_count < OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES) {
        snprintf(lines[line_count],
                 sizeof(lines[line_count]),
                 "ecart %.1f m | vitesse %.0f km/h",
                 navigation->distance_from_route_m,
                 navigation->speed_mps * 3.6);
        state.lines[line_count] = lines[line_count];
        ++line_count;
    }

    const OpenRideNavigationTripStats *stats =
        openride_navigation_session_stats(session);
    if (stats && line_count < OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES) {
        char elapsed_text[32];
        format_duration(stats->elapsed_s,
                        elapsed_text,
                        sizeof(elapsed_text));
        snprintf(lines[line_count],
                 sizeof(lines[line_count]),
                 "trajet %.1f km | %s | moy %.0f | max %.0f km/h",
                 stats->gps_distance_m / 1000.0,
                 elapsed_text,
                 stats->average_speed_mps * 3.6,
                 stats->max_speed_mps * 3.6);
        state.lines[line_count] = lines[line_count];
        ++line_count;
    }

    if (stats && line_count < OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES) {
        snprintf(lines[line_count],
                 sizeof(lines[line_count]),
                 "recalcul auto %s | recalculs %u",
                 auto_reroute ? "ON" : "OFF",
                 stats->reroute_count);
        state.lines[line_count] = lines[line_count];
        ++line_count;
    }

    if (line_count < OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES) {
        snprintf(lines[line_count],
                 sizeof(lines[line_count]),
                 "S lecture | F suivi %s | A auto %s | X deviation %s | R manuel",
                 follow_gps ? "ON" : "OFF",
                 auto_reroute ? "ON" : "OFF",
                 deviation_enabled ? "ON" : "OFF");
        state.lines[line_count] = lines[line_count];
        ++line_count;
    }

    state.line_count = line_count;

    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui,
                           renderer,
                           viewport_width,
                           viewport_height)) {
        return;
    }
    openride_ui_navigation_overlay_draw(&ui, &state);
    openride_ui_end(&ui);
#endif
}

'''


def prepare_main(text: str) -> str:
    if '#include "openride/ui_navigation_overlay.h"' not in text:
        text = replace_once(
            text,
            '#include "openride/ui_drive_hud.h"\n'
            '#include "openride/ui_map_overlay.h"\n'
            '#include "openride/drive_mode.h"',
            '#include "openride/ui_drive_hud.h"\n'
            '#include "openride/ui_map_overlay.h"\n'
            '#include "openride/ui_navigation_overlay.h"\n'
            '#include "openride/drive_mode.h"',
            "V1.14 UI include",
        )

    text = replace_block(
        text,
        "static void draw_navigation_overlay(",
        "static void draw_mobile_toolbar(",
        final_navigation_overlay(),
        "V1.14 navigation status overlay",
    )

    if "SDL_FRect panel = {x, y, w, h};" in text:
        raise RuntimeError("V1.14: raw legacy navigation panel remains")
    if text.count("openride_ui_navigation_overlay_draw(&ui, &state);") != 1:
        raise RuntimeError("V1.14: expected one desktop UI navigation overlay draw call")
    if text.count("static void draw_navigation_overlay(") != 1:
        raise RuntimeError("V1.14: navigation overlay definition count changed")
    if "NAVIGATION GPS REEL" in text:
        raise RuntimeError("V1.14: Android navigation HUD code still remains")
    if "Android: keep the normal map clear until ui_drive_hud takes over." not in text:
        raise RuntimeError("V1.14: Android early return missing")

    return text


def main() -> int:
    originals = {
        CMAKE: CMAKE.read_text(encoding="utf-8"),
        MAIN: MAIN.read_text(encoding="utf-8"),
    }
    prepared = {
        CMAKE: prepare_cmake(originals[CMAKE]),
        MAIN: prepare_main(originals[MAIN]),
    }

    changed = [path for path in prepared if prepared[path] != originals[path]]
    if not changed:
        print("OK: UI Engine V1.14 is already applied")
        print("Android intermediate navigation HUD is already disabled")
        return 0

    main_changed = prepared[MAIN] != originals[MAIN]
    if main_changed:
        delta = len(originals[MAIN]) - len(prepared[MAIN])
        if delta < -6000 or delta > 14000:
            raise RuntimeError(
                f"src/main.c: unexpected V1.14 size delta ({delta} bytes removed net)"
            )

    for path in changed:
        path.write_text(prepared[path], encoding="utf-8")

    print("OK: UI Engine V1.14 navigation overlay migration applied")
    for path in changed:
        print(f"Changed: {path.relative_to(ROOT)}")
    print("Desktop navigation diagnostics render through UI Engine")
    print("Android intermediate navigation HUD is disabled")
    print("Android Drive HUD remains the active-navigation interface")
    print("Next: git diff --check && git diff -- CMakeLists.txt src/main.c")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
