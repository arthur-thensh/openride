#!/usr/bin/env python3
"""One-shot guarded migration for UI Engine V1.4.

Moves the Android Settings and Offline Regions panels to dedicated UI Engine
components while preserving the existing application actions and state ownership.
All replacements are prepared in memory before any file is written.

This script never builds, tests, commits, or pushes anything.
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


def prepare_cmake(text: str) -> str:
    return replace_once(
        text,
        "    src/ui/ui_main_menu.c\n    src/ui/ui_route_panel.c\n)",
        "    src/ui/ui_main_menu.c\n    src/ui/ui_route_panel.c\n    src/ui/ui_settings_panel.c\n    src/ui/ui_regions_panel.c\n)",
        "CMake V1.4 UI sources",
    )


def prepare_main(text: str) -> str:
    text = replace_once(
        text,
        '#include "openride/ui_route_panel.h"\n#include "openride/drive_mode.h"',
        '#include "openride/ui_route_panel.h"\n#include "openride/ui_settings_panel.h"\n#include "openride/ui_regions_panel.h"\n#include "openride/drive_mode.h"',
        "V1.4 UI includes",
    )

    old_wrapper_tail = '''    openride_ui_end(&ui);
    return hit;
}

typedef struct OpenRideMobilePanelLayout {'''
    new_wrapper_tail = '''    openride_ui_end(&ui);
    return hit;
}

static OpenRideMobilePanelHit mobile_settings_panel_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height)
{
    OpenRideMobilePanelHit hit = {OPENRIDE_MOBILE_PANEL_NONE, -1};
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return hit;
    }

    switch (openride_ui_settings_panel_hit_test(&ui, x, y)) {
        case OPENRIDE_UI_SETTINGS_PANEL_STYLE:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_STYLE;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_PROFILE:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_PROFILE;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_FOLLOW:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_FOLLOW;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_REROUTE:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_REROUTE;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_VOICE:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_VOICE;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_GPS_SIMULATION:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_SIMULATION;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_GPS_DEVIATION:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_DEVIATION;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_GPS_SPEED:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_SPEED;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_GPS_MISSED_TURN:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_MISSED_TURN;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_BACK:
            hit.action = OPENRIDE_MOBILE_PANEL_BACK;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_NONE:
        default:
            break;
    }
    openride_ui_end(&ui);
    return hit;
}

static OpenRideMobilePanelHit mobile_regions_panel_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height)
{
    OpenRideMobilePanelHit hit = {OPENRIDE_MOBILE_PANEL_NONE, -1};
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return hit;
    }

    switch (openride_ui_regions_panel_hit_test(&ui, x, y)) {
        case OPENRIDE_UI_REGIONS_PANEL_PREVIOUS:
            hit.action = OPENRIDE_MOBILE_PANEL_REGION_PREVIOUS;
            break;
        case OPENRIDE_UI_REGIONS_PANEL_NEXT:
            hit.action = OPENRIDE_MOBILE_PANEL_REGION_NEXT;
            break;
        case OPENRIDE_UI_REGIONS_PANEL_INSTALL:
            hit.action = OPENRIDE_MOBILE_PANEL_REGION_INSTALL;
            break;
        case OPENRIDE_UI_REGIONS_PANEL_REMOVE:
            hit.action = OPENRIDE_MOBILE_PANEL_REGION_REMOVE;
            break;
        case OPENRIDE_UI_REGIONS_PANEL_BACK:
            hit.action = OPENRIDE_MOBILE_PANEL_BACK;
            break;
        case OPENRIDE_UI_REGIONS_PANEL_NONE:
        default:
            break;
    }
    openride_ui_end(&ui);
    return hit;
}

typedef struct OpenRideMobilePanelLayout {'''
    text = replace_once(
        text,
        old_wrapper_tail,
        new_wrapper_tail,
        "V1.4 panel hit-test wrappers",
    )

    old_draw_tail = '''    if (panel == OPENRIDE_APP_PANEL_ROUTE) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            const OpenRideUIRoutePanelState state = {
                .has_start = selection && selection->has_start,
                .has_destination = selection && selection->has_destination,
                .gps_valid = gps_valid,
                .gps_accuracy_m = gps_accuracy_m
            };
            (void)openride_ui_route_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

    uint32_t rows = 0U;'''
    new_draw_tail = '''    if (panel == OPENRIDE_APP_PANEL_ROUTE) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            const OpenRideUIRoutePanelState state = {
                .has_start = selection && selection->has_start,
                .has_destination = selection && selection->has_destination,
                .gps_valid = gps_valid,
                .gps_accuracy_m = gps_accuracy_m
            };
            (void)openride_ui_route_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_SETTINGS) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            const OpenRideUISettingsPanelState state = {
                .map_style_name = openride_map_style_name(map_style),
                .routing_profile_name = openride_routing_profile_name(profile),
                .follow_gps = follow_gps,
                .auto_reroute = auto_reroute,
                .voice_enabled = voice_enabled,
                .simulated_gps_active = simulated_gps_active,
                .simulated_gps_deviation = simulated_gps_deviation,
                .simulated_gps_time_scale = simulated_gps_time_scale,
                .simulated_missed_turn_armed = simulated_missed_turn_armed,
                .simulated_missed_turn_active = simulated_missed_turn_active
            };
            (void)openride_ui_settings_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_REGIONS) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            const OpenRideUIRegionsPanelState state = {
                .region_name = region ? region->name : "Region",
                .region_is_active = region_is_active,
                .ormap_installed = region_status && region_status->ormap_installed,
                .routing_installed = region_status && region_status->routing_installed,
                .search_installed = region_status && region_status->search_installed,
                .source_pbf_present = region_status && region_status->source_pbf_present,
                .poly_present = region_status && region_status->poly_present,
                .ready = openride_region_status_ready(region_status),
                .total_size_mb = region_status ? region_status->total_size_mb : 0.0,
                .busy = region_busy,
                .progress = region_progress,
                .work_status = region_work_status
            };
            (void)openride_ui_regions_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

    uint32_t rows = 0U;'''
    text = replace_once(
        text,
        old_draw_tail,
        new_draw_tail,
        "V1.4 panel renderer routing",
    )

    old_event = '''                        const OpenRideMobilePanelHit mobile_hit =
                            app_panel == OPENRIDE_APP_PANEL_MAIN
                                ? mobile_main_menu_hit_test(renderer,
                                                            x,
                                                            y,
                                                            width,
                                                            height)
                            : app_panel == OPENRIDE_APP_PANEL_ROUTE
                                ? mobile_route_panel_hit_test(renderer,
                                                              x,
                                                              y,
                                                              width,
                                                              height)
                                : mobile_app_panel_hit_test(renderer,
                                                            app_panel,
                                                            x,
                                                            y,
                                                            width,
                                                            height,
                                                            mobile_place_count);'''
    new_event = '''                        const OpenRideMobilePanelHit mobile_hit =
                            app_panel == OPENRIDE_APP_PANEL_MAIN
                                ? mobile_main_menu_hit_test(renderer,
                                                            x,
                                                            y,
                                                            width,
                                                            height)
                            : app_panel == OPENRIDE_APP_PANEL_ROUTE
                                ? mobile_route_panel_hit_test(renderer,
                                                              x,
                                                              y,
                                                              width,
                                                              height)
                            : app_panel == OPENRIDE_APP_PANEL_SETTINGS
                                ? mobile_settings_panel_hit_test(renderer,
                                                                 x,
                                                                 y,
                                                                 width,
                                                                 height)
                            : app_panel == OPENRIDE_APP_PANEL_REGIONS
                                ? mobile_regions_panel_hit_test(renderer,
                                                                x,
                                                                y,
                                                                width,
                                                                height)
                                : mobile_app_panel_hit_test(renderer,
                                                            app_panel,
                                                            x,
                                                            y,
                                                            width,
                                                            height,
                                                            mobile_place_count);'''
    return replace_once(
        text,
        old_event,
        new_event,
        "V1.4 panel event routing",
    )


def main() -> int:
    originals = {
        CMAKE: CMAKE.read_text(encoding="utf-8"),
        MAIN: MAIN.read_text(encoding="utf-8"),
    }
    prepared = {
        CMAKE: prepare_cmake(originals[CMAKE]),
        MAIN: prepare_main(originals[MAIN]),
    }

    for path, text in prepared.items():
        if text == originals[path]:
            raise RuntimeError(f"{path}: migration produced no change")

    for path, text in prepared.items():
        path.write_text(text, encoding="utf-8")

    print("OK: UI Engine V1.4 Settings + Offline Regions migration applied")
    print("Changed: CMakeLists.txt, src/main.c")
    print("Next: git diff --check && git diff -- CMakeLists.txt src/main.c")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
