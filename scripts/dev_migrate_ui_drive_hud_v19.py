#!/usr/bin/env python3
"""One-shot guarded migration for UI Engine V1.9 drive HUD.

Routes Android drive-mode rendering and the four bottom control hit targets
through ui_drive_hud. Navigation, route, GPS, rerouting, simulation, session
statistics, and action handling remain owned by main.c.

All edits are prepared in memory before any file is written. This script never
builds, tests, commits, or pushes anything.
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
    if text.count(start_marker) != 1:
        raise RuntimeError(
            f"{label}: expected exactly one start marker, found {text.count(start_marker)}"
        )
    start = text.find(start_marker)
    end = text.find(end_marker, start + len(start_marker))
    if end < 0:
        raise RuntimeError(f"{label}: end marker not found")
    if end <= start:
        raise RuntimeError(f"{label}: invalid marker order")
    return text[:start] + replacement + text[end:]


def prepare_cmake(text: str) -> str:
    return replace_once(
        text,
        "    src/ui/ui_search_overlay.c\n"
        "    src/ui/ui_route_downloads_panel.c\n"
        ")",
        "    src/ui/ui_search_overlay.c\n"
        "    src/ui/ui_route_downloads_panel.c\n"
        "    src/ui/ui_drive_hud.c\n"
        ")",
        "CMake V1.9 UI source",
    )


def prepare_main(text: str) -> str:
    text = replace_once(
        text,
        '#include "openride/ui_route_downloads_panel.h"\n'
        '#include "openride/drive_mode.h"',
        '#include "openride/ui_route_downloads_panel.h"\n'
        '#include "openride/ui_drive_hud.h"\n'
        '#include "openride/drive_mode.h"',
        "V1.9 UI include",
    )

    hit_test_replacement = r'''static OpenRideDriveAction drive_controls_hit_test(SDL_Renderer *renderer,
                                                   double x,
                                                   double y,
                                                   int viewport_width,
                                                   int viewport_height)
{
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return OPENRIDE_DRIVE_ACTION_NONE;
    }

    const OpenRideUIDriveHUDAction action =
        openride_ui_drive_hud_hit_test(&ui, x, y);
    openride_ui_end(&ui);

    switch (action) {
        case OPENRIDE_UI_DRIVE_HUD_EXIT:
            return OPENRIDE_DRIVE_ACTION_EXIT;
        case OPENRIDE_UI_DRIVE_HUD_RECENTER:
            return OPENRIDE_DRIVE_ACTION_RECENTER;
        case OPENRIDE_UI_DRIVE_HUD_ORIENTATION:
            return OPENRIDE_DRIVE_ACTION_ORIENTATION;
        case OPENRIDE_UI_DRIVE_HUD_GPS:
            return OPENRIDE_DRIVE_ACTION_GPS;
        case OPENRIDE_UI_DRIVE_HUD_NONE:
        default:
            return OPENRIDE_DRIVE_ACTION_NONE;
    }
}

static OpenRideUIDriveHUDManeuver drive_hud_maneuver(
    OpenRideManeuverType maneuver)
{
    switch (maneuver) {
        case OPENRIDE_MANEUVER_DEPART:
            return OPENRIDE_UI_DRIVE_MANEUVER_DEPART;
        case OPENRIDE_MANEUVER_SLIGHT_LEFT:
            return OPENRIDE_UI_DRIVE_MANEUVER_SLIGHT_LEFT;
        case OPENRIDE_MANEUVER_LEFT:
            return OPENRIDE_UI_DRIVE_MANEUVER_LEFT;
        case OPENRIDE_MANEUVER_SHARP_LEFT:
            return OPENRIDE_UI_DRIVE_MANEUVER_SHARP_LEFT;
        case OPENRIDE_MANEUVER_SLIGHT_RIGHT:
            return OPENRIDE_UI_DRIVE_MANEUVER_SLIGHT_RIGHT;
        case OPENRIDE_MANEUVER_RIGHT:
            return OPENRIDE_UI_DRIVE_MANEUVER_RIGHT;
        case OPENRIDE_MANEUVER_SHARP_RIGHT:
            return OPENRIDE_UI_DRIVE_MANEUVER_SHARP_RIGHT;
        case OPENRIDE_MANEUVER_UTURN:
            return OPENRIDE_UI_DRIVE_MANEUVER_UTURN;
        case OPENRIDE_MANEUVER_ROUNDABOUT:
            return OPENRIDE_UI_DRIVE_MANEUVER_ROUNDABOUT;
        case OPENRIDE_MANEUVER_ARRIVE:
            return OPENRIDE_UI_DRIVE_MANEUVER_ARRIVE;
        case OPENRIDE_MANEUVER_CONTINUE:
        default:
            return OPENRIDE_UI_DRIVE_MANEUVER_CONTINUE;
    }
}

static OpenRideUIDriveHUDGPSQuality drive_hud_gps_quality(
    OpenRideGPSQuality quality)
{
    switch (quality) {
        case OPENRIDE_GPS_GOOD:
            return OPENRIDE_UI_DRIVE_GPS_GOOD;
        case OPENRIDE_GPS_FAIR:
            return OPENRIDE_UI_DRIVE_GPS_FAIR;
        case OPENRIDE_GPS_POOR:
            return OPENRIDE_UI_DRIVE_GPS_POOR;
        case OPENRIDE_GPS_LOST:
            return OPENRIDE_UI_DRIVE_GPS_LOST;
        case OPENRIDE_GPS_UNAVAILABLE:
        default:
            return OPENRIDE_UI_DRIVE_GPS_UNAVAILABLE;
    }
}

'''
    text = replace_block(
        text,
        "static OpenRideDriveAction drive_controls_hit_test(",
        "static void format_arrival_clock(",
        hit_test_replacement,
        "V1.9 drive controls hit-test",
    )

    draw_replacement = r'''static void draw_drive_mode_ui(SDL_Renderer *renderer,
                               const OpenRideMBTilesMetadata *metadata,
                               const OpenRideNavigationState *navigation,
                               const OpenRideNavigationInstructionList *instructions,
                               const OpenRideRoute *route,
                               const OpenRideNavigationSession *session,
                               const OpenRideDriveModeState *drive,
                               bool auto_reroute,
                               bool simulated_gps,
                               bool simulated_gps_deviation,
                               double simulated_gps_time_scale,
                               bool simulated_missed_turn_armed,
                               bool simulated_missed_turn_active,
                               int viewport_width,
                               int viewport_height)
{
    if (!renderer || !drive || !drive->active) return;

    OpenRideUIDriveHUDStatus hud_status = OPENRIDE_UI_DRIVE_HUD_ACTIVE;
    if (navigation && navigation->status == OPENRIDE_NAVIGATION_OFF_ROUTE) {
        hud_status = OPENRIDE_UI_DRIVE_HUD_OFF_ROUTE;
    } else if (navigation && navigation->status == OPENRIDE_NAVIGATION_ARRIVED) {
        hud_status = OPENRIDE_UI_DRIVE_HUD_ARRIVED;
    }

    double instruction_distance_m = INFINITY;
    const OpenRideNavigationInstruction *next_instruction = NULL;
    if (navigation && navigation->valid) {
        next_instruction = openride_navigation_instructions_next(
            instructions,
            navigation->traveled_m,
            &instruction_distance_m);
    }

    char distance_text[32] = "--";
    char maneuver_text[128] = "Suivre l'itineraire";
    char primary_text[64] = "DANS --";
    OpenRideUIDriveHUDManeuver maneuver =
        OPENRIDE_UI_DRIVE_MANEUVER_CONTINUE;
    if (next_instruction) {
        openride_navigation_distance_text_fr(instruction_distance_m,
                                             distance_text,
                                             sizeof(distance_text));
        openride_navigation_instruction_text_fr(next_instruction,
                                                maneuver_text,
                                                sizeof(maneuver_text));
        maneuver = drive_hud_maneuver(next_instruction->maneuver);
        if (next_instruction->maneuver == OPENRIDE_MANEUVER_ARRIVE) {
            snprintf(primary_text,
                     sizeof(primary_text),
                     "ARRIVEE %s",
                     distance_text);
        } else {
            snprintf(primary_text,
                     sizeof(primary_text),
                     "DANS %s",
                     distance_text);
        }
    }

    double following_gap_m = INFINITY;
    const OpenRideNavigationInstruction *following_instruction = NULL;
    if (next_instruction
        && next_instruction->maneuver != OPENRIDE_MANEUVER_ARRIVE) {
        following_instruction = openride_navigation_instructions_after(
            instructions,
            next_instruction->distance_from_start_m,
            &following_gap_m);
    }
    const bool show_following =
        following_instruction
        && isfinite(following_gap_m)
        && following_gap_m <= 300.0
        && hud_status == OPENRIDE_UI_DRIVE_HUD_ACTIVE;

    char following_text[180] = {0};
    if (show_following) {
        char following_distance_text[32];
        char following_maneuver_text[128];
        openride_navigation_distance_text_fr(
            following_gap_m,
            following_distance_text,
            sizeof(following_distance_text));
        openride_navigation_instruction_text_fr(
            following_instruction,
            following_maneuver_text,
            sizeof(following_maneuver_text));
        snprintf(following_text,
                 sizeof(following_text),
                 "PUIS %s | %.120s",
                 following_distance_text,
                 following_maneuver_text);
    }

    char simulation_prefix[40] = {0};
    if (simulated_gps) {
        const char *format =
            simulated_missed_turn_active
                ? "SIM x%.0f RATE | "
                : simulated_missed_turn_armed
                    ? "SIM x%.0f ARME | "
                    : simulated_gps_deviation
                        ? "SIM x%.0f +80m | "
                        : "SIM x%.0f DEV | ";
        snprintf(simulation_prefix,
                 sizeof(simulation_prefix),
                 format,
                 simulated_gps_time_scale);
    }

    char gps_text[80];
    if (drive->gps_quality == OPENRIDE_GPS_GOOD
        || drive->gps_quality == OPENRIDE_GPS_FAIR) {
        snprintf(gps_text,
                 sizeof(gps_text),
                 "%s%s %.0f m",
                 simulation_prefix,
                 openride_drive_mode_gps_quality_name(drive->gps_quality),
                 drive->gps_accuracy_m);
    } else {
        snprintf(gps_text,
                 sizeof(gps_text),
                 "%s%s",
                 simulation_prefix,
                 openride_drive_mode_gps_quality_name(drive->gps_quality));
    }

    const double speed_kph = navigation && navigation->valid
        ? navigation->speed_mps * 3.6
        : 0.0;
    const double remaining_m = navigation && navigation->valid
        ? navigation->remaining_m
        : (route ? route->distance_m : 0.0);
    double remaining_s = 0.0;
    if (route && route->distance_m > 0.0 && route->estimated_time_s > 0.0) {
        remaining_s = route->estimated_time_s
            * clampd(remaining_m / route->distance_m, 0.0, 1.0);
    }
    char arrival_text[16];
    format_arrival_clock(remaining_s,
                         arrival_text,
                         sizeof(arrival_text));

    uint32_t reroute_count = 0U;
    if (session) {
        const OpenRideNavigationTripStats *trip =
            openride_navigation_session_stats(session);
        if (trip) reroute_count = trip->reroute_count;
    }

    const OpenRideUIDriveHUDState state = {
        .status = hud_status,
        .maneuver = maneuver,
        .primary_text = primary_text,
        .maneuver_text = maneuver_text,
        .show_following = show_following,
        .following_text = following_text,
        .auto_reroute = auto_reroute,
        .gps_quality = drive_hud_gps_quality(drive->gps_quality),
        .gps_text = gps_text,
        .speed_kph = speed_kph,
        .remaining_m = remaining_m,
        .arrival_text = arrival_text,
        .reroute_count = reroute_count,
        .heading_up = drive->heading_up,
        .show_attribution = metadata && metadata->attribution[0] != '\0'
    };

    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui,
                           renderer,
                           viewport_width,
                           viewport_height)) {
        return;
    }
    openride_ui_drive_hud_draw(&ui, &state);
    openride_ui_end(&ui);
}

'''
    text = replace_block(
        text,
        "static void draw_drive_mode_ui(",
        "static bool add_selection_from_screen(",
        draw_replacement,
        "V1.9 drive HUD renderer",
    )

    if "openride_ui_drive_hud_draw(&ui, &state);" not in text:
        raise RuntimeError("V1.9 drive HUD renderer: new UI call missing")
    if "openride_ui_drive_hud_hit_test(&ui, x, y);" not in text:
        raise RuntimeError("V1.9 drive HUD hit-test: new UI call missing")
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

    for path, content in prepared.items():
        if content == originals[path]:
            raise RuntimeError(f"{path}: migration produced no change")

    removed = len(originals[MAIN]) - len(prepared[MAIN])
    if removed < 2000 or removed > 20000:
        raise RuntimeError(
            f"src/main.c: unexpected V1.9 size delta ({removed} bytes removed net)"
        )

    for path, content in prepared.items():
        path.write_text(content, encoding="utf-8")

    print("OK: UI Engine V1.9 drive HUD migration applied")
    print("Changed: CMakeLists.txt, src/main.c")
    print("Navigation/GPS/reroute action logic remains in main.c")
    print("Next: git diff --check && git diff -- CMakeLists.txt src/main.c")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
