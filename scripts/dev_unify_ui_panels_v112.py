#!/usr/bin/env python3
"""One-shot guarded migration for UI Engine V1.12 application panels.

Unifies the existing main-menu, route, required-maps, settings, offline-regions,
and favorites/history panel rendering across desktop and Android by reusing the
same UI Engine components. Existing desktop/fallback touch hit-tests for main
menu, place rows, and region install/remove are also routed through those
component layouts so drawing and hit targets stay aligned.

Business logic, keyboard shortcuts, Android actions, routing, storage, region
installation, GPS, and search remain owned by main.c.

This script does not build, test, commit, or push anything.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
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


def prepare_main(text: str) -> str:
    panel_hits = r'''static OpenRideAppPanel app_panel_main_at(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height)
{
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return OPENRIDE_APP_PANEL_NONE;
    }
    const OpenRideUIMainMenuAction action =
        openride_ui_main_menu_hit_test(&ui, x, y);
    openride_ui_end(&ui);

    switch (action) {
        case OPENRIDE_UI_MAIN_MENU_FAVORITES:
            return OPENRIDE_APP_PANEL_FAVORITES;
        case OPENRIDE_UI_MAIN_MENU_HISTORY:
            return OPENRIDE_APP_PANEL_HISTORY;
        case OPENRIDE_UI_MAIN_MENU_REGIONS:
            return OPENRIDE_APP_PANEL_REGIONS;
        case OPENRIDE_UI_MAIN_MENU_SETTINGS:
            return OPENRIDE_APP_PANEL_SETTINGS;
        case OPENRIDE_UI_MAIN_MENU_NONE:
        case OPENRIDE_UI_MAIN_MENU_SEARCH:
        case OPENRIDE_UI_MAIN_MENU_MAP_ZOOM_TEST:
        case OPENRIDE_UI_MAIN_MENU_CLOSE:
        default:
            return OPENRIDE_APP_PANEL_NONE;
    }
}

static bool app_panel_main_search_at(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height)
{
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return false;
    }
    const OpenRideUIMainMenuAction action =
        openride_ui_main_menu_hit_test(&ui, x, y);
    openride_ui_end(&ui);
    return action == OPENRIDE_UI_MAIN_MENU_SEARCH;
}

static int app_panel_place_at(SDL_Renderer *renderer,
                              double x,
                              double y,
                              int viewport_width,
                              int viewport_height,
                              uint32_t count)
{
    if (count == 0U) return -1;

    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return -1;
    }
    const OpenRideUIPlacesPanelHit hit =
        openride_ui_places_panel_hit_test(&ui, count, x, y);
    openride_ui_end(&ui);
    return hit.action == OPENRIDE_UI_PLACES_PANEL_PLACE ? hit.index : -1;
}

'''
    text = replace_block(
        text,
        "static OpenRideAppPanel app_panel_main_at(",
        "static void set_destination_from_place(",
        panel_hits,
        "V1.12 generic panel hit-tests",
    )

    text = replace_once(
        text,
        "static void draw_mobile_app_panel(",
        "#endif\n\nstatic void draw_ui_app_panel(",
        "V1.12 expose cross-platform panel renderer",
    )
    text = replace_once(
        text,
        "\n}\n#endif\n\nstatic int app_panel_region_action_at(",
        "\n}\n\nstatic int app_panel_region_action_at(",
        "V1.12 close Android block before panel renderer",
    )

    region_hit = r'''static int app_panel_region_action_at(SDL_Renderer *renderer,
                                      double x,
                                      double y,
                                      int viewport_width,
                                      int viewport_height)
{
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return 0;
    }
    const OpenRideUIRegionsPanelAction action =
        openride_ui_regions_panel_hit_test(&ui, x, y);
    openride_ui_end(&ui);
    if (action == OPENRIDE_UI_REGIONS_PANEL_INSTALL) return 1;
    if (action == OPENRIDE_UI_REGIONS_PANEL_REMOVE) return 2;
    return 0;
}

'''
    text = replace_block(
        text,
        "static int app_panel_region_action_at(",
        "static void draw_app_panel(",
        region_hit,
        "V1.12 region hit-test",
    )

    draw_wrapper = r'''static void draw_app_panel(SDL_Renderer *renderer,
                           OpenRideAppPanel panel,
                           const OpenRideStoredPlace *favorites,
                           uint32_t favorite_count,
                           const OpenRideStoredPlace *history,
                           uint32_t history_count,
                           uint32_t selected,
                           OpenRideMapStyle map_style,
                           OpenRideRoutingProfile profile,
                           bool follow_gps,
                           bool auto_reroute,
                           bool voice_enabled,
                           bool simulated_gps_active,
                           bool simulated_gps_deviation,
                           double simulated_gps_time_scale,
                           bool simulated_missed_turn_armed,
                           bool simulated_missed_turn_active,
                           const OpenRideRegionDefinition *region,
                           const OpenRideRegionStatus *region_status,
                           bool region_is_active,
                           bool region_busy,
                           double region_progress,
                           const char *region_work_status,
                           const OpenRideMapSelection *selection,
                           bool gps_valid,
                           double gps_accuracy_m,
                           const OpenRideRouteDownloadPlan *route_download_plan_state,
                           int viewport_width)
{
    if (panel == OPENRIDE_APP_PANEL_NONE) return;
    draw_ui_app_panel(renderer,
                      panel,
                      favorites,
                      favorite_count,
                      history,
                      history_count,
                      selected,
                      map_style,
                      profile,
                      follow_gps,
                      auto_reroute,
                      voice_enabled,
                      simulated_gps_active,
                      simulated_gps_deviation,
                      simulated_gps_time_scale,
                      simulated_missed_turn_armed,
                      simulated_missed_turn_active,
                      region,
                      region_status,
                      region_is_active,
                      region_busy,
                      region_progress,
                      region_work_status,
                      selection,
                      gps_valid,
                      gps_accuracy_m,
                      route_download_plan_state,
                      viewport_width);
}

'''
    text = replace_block(
        text,
        "static void draw_app_panel(",
        "static void clear_navigation_session(",
        draw_wrapper,
        "V1.12 remove duplicate desktop panel renderer",
    )

    text = replace_once(
        text,
        "app_panel_main_search_at(x, y, width)",
        "app_panel_main_search_at(renderer, x, y, width, height)",
        "V1.12 main search touch call",
    )
    text = replace_once(
        text,
        "app_panel_main_at(x, y, width)",
        "app_panel_main_at(renderer, x, y, width, height)",
        "V1.12 main panel touch call",
    )
    text = replace_once(
        text,
        "app_panel_place_at(x, y, width, count)",
        "app_panel_place_at(renderer, x, y, width, height, count)",
        "V1.12 places touch call",
    )
    text = replace_once(
        text,
        "app_panel_region_action_at(x, y, width)",
        "app_panel_region_action_at(renderer, x, y, width, height)",
        "V1.12 regions touch call",
    )

    if "draw_mobile_app_panel(" in text:
        raise RuntimeError("V1.12: old mobile-only renderer name remains")
    if text.count("draw_ui_app_panel(") != 2:
        raise RuntimeError(
            f"V1.12: expected definition + call for draw_ui_app_panel, found {text.count('draw_ui_app_panel(')}"
        )
    if "OPENRIDE - MENU" in text:
        raise RuntimeError("V1.12: legacy desktop main-menu renderer remains")
    if "Cartes / donnees installees" in text:
        raise RuntimeError("V1.12: legacy desktop panel labels remain")
    if text.count("openride_ui_main_menu_hit_test(&ui, x, y)") < 3:
        raise RuntimeError("V1.12: expected UI Engine main-menu hit-tests")
    if text.count("openride_ui_regions_panel_hit_test(&ui, x, y)") < 2:
        raise RuntimeError("V1.12: expected UI Engine region hit-tests")

    return text


def main() -> int:
    original = MAIN.read_text(encoding="utf-8")
    prepared = prepare_main(original)

    if prepared == original:
        raise RuntimeError("src/main.c: migration produced no change")

    removed = len(original) - len(prepared)
    if removed < 5000 or removed > 22000:
        raise RuntimeError(
            f"src/main.c: unexpected V1.12 size delta ({removed} bytes removed net)"
        )

    MAIN.write_text(prepared, encoding="utf-8")

    print("OK: UI Engine V1.12 cross-platform panel migration applied")
    print("Changed: src/main.c")
    print("Main panels now render through the same UI Engine components on desktop and Android")
    print("Existing desktop/fallback touch targets now follow UI component layouts")
    print("Business logic and keyboard handling remain in main.c")
    print("Next: git diff --check && git diff --stat && git diff -- src/main.c")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
