#!/usr/bin/env python3
"""Batched Architecture V2.1 migration: extract the application UI bridge.

This intentionally groups several previously separate cleanup steps into one
migration / one test cycle. It moves the remaining UI adaptation layer out of
src/main.c without moving routing, GPS, storage, region-install or other
business logic.

Moved in one batch:
- toolbar UI hit-test + draw adapter;
- map status overlay formatting/draw adapter;
- offline search overlay draw + result hit-test;
- application panel UI state types and desktop panel hit-tests;
- application panel rendering adapter;
- desktop navigation diagnostic overlay;
- Drive HUD formatting/draw + control hit-test.

The migration creates src/app_ui_bridge.c from the exact, already-tested
implementations currently present in main.c, then rewires main.c to the public
bridge names. It does not build, test, commit, or push anything.
"""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
CMAKE = ROOT / "CMakeLists.txt"
MAIN = ROOT / "src" / "main.c"
BRIDGE = ROOT / "src" / "app_ui_bridge.c"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def take_block(text: str,
               start_marker: str,
               end_marker: str,
               label: str) -> tuple[str, str]:
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
    return text[start:end], text[:start] + text[end:]


def remove_block(text: str,
                 start_marker: str,
                 end_marker: str,
                 label: str) -> str:
    _, result = take_block(text, start_marker, end_marker, label)
    return result


def prepare_cmake(text: str) -> str:
    if "    src/app_ui_bridge.c\n" in text:
        return text
    return replace_once(
        text,
        "    src/main.c\n"
        "    src/app_ui_action.c\n",
        "    src/main.c\n"
        "    src/app_ui_action.c\n"
        "    src/app_ui_bridge.c\n",
        "CMake V2.1 UI bridge source",
    )


def transform_block(block: str,
                    replacements: list[tuple[str, str, str]]) -> str:
    for old, new, label in replacements:
        block = replace_once(block, old, new, label)
    return block


def generate_bridge_and_main(original: str) -> tuple[str, str]:
    text = original

    toolbar, text = take_block(
        text,
        "static OpenRideToolbarAction mobile_toolbar_hit_test(",
        "static void format_duration(",
        "V2.1 toolbar adapter",
    )
    toolbar = transform_block(toolbar, [
        (
            "static OpenRideToolbarAction mobile_toolbar_hit_test(",
            "OpenRideToolbarAction openride_app_ui_toolbar_hit_test(",
            "V2.1 public toolbar hit-test",
        ),
    ])

    map_overlay, text = take_block(
        text,
        "static void format_duration(",
        "static void utf8_backspace(",
        "V2.1 map status overlay",
    )
    map_overlay = transform_block(map_overlay, [
        (
            "static void draw_map_status_overlay(",
            "void openride_app_ui_draw_map_status_overlay(",
            "V2.1 public map status overlay",
        ),
    ])

    search_overlay, text = take_block(
        text,
        "static void draw_place_search_overlay(",
        "typedef enum OpenRidePlaceSearchPurpose",
        "V2.1 search overlay",
    )
    search_overlay = transform_block(search_overlay, [
        (
            "static void draw_place_search_overlay(",
            "void openride_app_ui_draw_place_search_overlay(",
            "V2.1 public search overlay",
        ),
        (
            "static int place_search_result_at(",
            "int openride_app_ui_place_search_result_at(",
            "V2.1 public search result hit-test",
        ),
    ])

    # These three application/UI-flow types are now declared by app_ui_bridge.h.
    text = remove_block(
        text,
        "typedef enum OpenRidePlaceSearchPurpose",
        "static OpenRideAppPanel app_panel_main_at(",
        "V2.1 UI state typedefs",
    )

    panel_hits, text = take_block(
        text,
        "static OpenRideAppPanel app_panel_main_at(",
        "static void set_destination_from_place(",
        "V2.1 desktop panel hit-tests",
    )
    panel_hits = transform_block(panel_hits, [
        (
            "static OpenRideAppPanel app_panel_main_at(",
            "OpenRideAppPanel openride_app_ui_panel_main_at(",
            "V2.1 public main-panel hit-test",
        ),
        (
            "static bool app_panel_main_search_at(",
            "bool openride_app_ui_panel_main_search_at(",
            "V2.1 public main search hit-test",
        ),
        (
            "static int app_panel_place_at(",
            "int openride_app_ui_panel_place_at(",
            "V2.1 public place hit-test",
        ),
    ])

    panel_draw, text = take_block(
        text,
        "static void draw_ui_app_panel(",
        "static void clear_navigation_session(",
        "V2.1 application panel renderer",
    )
    panel_draw = transform_block(panel_draw, [
        (
            "static int app_panel_region_action_at(",
            "int openride_app_ui_panel_region_action_at(",
            "V2.1 public region action hit-test",
        ),
        (
            "static void draw_app_panel(",
            "void openride_app_ui_draw_panel(",
            "V2.1 public panel draw",
        ),
    ])

    nav_drive, text = take_block(
        text,
        "static void draw_navigation_overlay(",
        "static bool add_selection_from_screen(",
        "V2.1 navigation/drive UI adapters",
    )

    enum_pattern = re.compile(
        r"typedef enum OpenRideDriveAction \{.*?\} OpenRideDriveAction;\n\n",
        re.S,
    )
    nav_drive, enum_count = enum_pattern.subn("", nav_drive, count=1)
    if enum_count != 1:
        raise RuntimeError(
            f"V2.1 drive action enum: expected one removal, found {enum_count}"
        )

    nav_drive = transform_block(nav_drive, [
        (
            "static void draw_navigation_overlay(",
            "void openride_app_ui_draw_navigation_overlay(",
            "V2.1 public navigation overlay",
        ),
        (
            "static void draw_mobile_toolbar(",
            "void openride_app_ui_draw_toolbar(",
            "V2.1 public toolbar draw",
        ),
        (
            "static OpenRideDriveAction drive_controls_hit_test(",
            "OpenRideDriveAction openride_app_ui_drive_controls_hit_test(",
            "V2.1 public drive hit-test",
        ),
        (
            "static void draw_drive_mode_ui(",
            "void openride_app_ui_draw_drive_mode(",
            "V2.1 public drive HUD draw",
        ),
    ])

    source_prefix = r'''#include "openride/app_ui_bridge.h"

#include "openride/ui.h"
#include "openride/ui_toolbar.h"
#include "openride/ui_main_menu.h"
#include "openride/ui_route_panel.h"
#include "openride/ui_settings_panel.h"
#include "openride/ui_regions_panel.h"
#include "openride/ui_places_panel.h"
#include "openride/ui_search_overlay.h"
#include "openride/ui_route_downloads_panel.h"
#include "openride/ui_drive_hud.h"
#include "openride/ui_map_overlay.h"
#include "openride/ui_navigation_overlay.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static double clampd(double value, double min_value, double max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

'''

    bridge = (
        source_prefix
        + toolbar
        + map_overlay
        + search_overlay
        + panel_hits
        + panel_draw
        + nav_drive
    )

    # Rewire remaining callers in main.c to the extracted bridge API.
    call_replacements = {
        "mobile_toolbar_hit_test(": "openride_app_ui_toolbar_hit_test(",
        "draw_map_status_overlay(": "openride_app_ui_draw_map_status_overlay(",
        "draw_place_search_overlay(": "openride_app_ui_draw_place_search_overlay(",
        "place_search_result_at(": "openride_app_ui_place_search_result_at(",
        "app_panel_main_at(": "openride_app_ui_panel_main_at(",
        "app_panel_main_search_at(": "openride_app_ui_panel_main_search_at(",
        "app_panel_place_at(": "openride_app_ui_panel_place_at(",
        "app_panel_region_action_at(": "openride_app_ui_panel_region_action_at(",
        "draw_app_panel(": "openride_app_ui_draw_panel(",
        "draw_navigation_overlay(": "openride_app_ui_draw_navigation_overlay(",
        "draw_mobile_toolbar(": "openride_app_ui_draw_toolbar(",
        "drive_controls_hit_test(": "openride_app_ui_drive_controls_hit_test(",
        "draw_drive_mode_ui(": "openride_app_ui_draw_drive_mode(",
    }
    for old, new in call_replacements.items():
        text = text.replace(old, new)

    include_block = (
        '#include "openride/app_toolbar.h"\n'
        '#include "openride/app_ui_action.h"\n'
        '#include "openride/ui_toolbar.h"\n'
        '#include "openride/ui_main_menu.h"\n'
        '#include "openride/ui_route_panel.h"\n'
        '#include "openride/ui_settings_panel.h"\n'
        '#include "openride/ui_regions_panel.h"\n'
        '#include "openride/ui_places_panel.h"\n'
        '#include "openride/ui_search_overlay.h"\n'
        '#include "openride/ui_route_downloads_panel.h"\n'
        '#include "openride/ui_drive_hud.h"\n'
        '#include "openride/ui_map_overlay.h"\n'
        '#include "openride/ui_navigation_overlay.h"\n'
    )
    new_include_block = (
        '#include "openride/app_toolbar.h"\n'
        '#include "openride/app_ui_action.h"\n'
        '#include "openride/app_ui_bridge.h"\n'
    )
    text = replace_once(
        text,
        include_block,
        new_include_block,
        "V2.1 main UI includes",
    )

    # main.c should no longer directly depend on UI Engine component internals.
    forbidden_main = (
        "OpenRideUIContext",
        "openride_ui_begin(",
        "openride_ui_end(",
        "openride_ui_toolbar_",
        "openride_ui_main_menu_",
        "openride_ui_route_panel_",
        "openride_ui_settings_panel_",
        "openride_ui_regions_panel_",
        "openride_ui_places_panel_",
        "openride_ui_search_overlay_",
        "openride_ui_route_downloads_panel_",
        "openride_ui_drive_hud_",
        "openride_ui_map_overlay_",
        "openride_ui_navigation_overlay_",
        "static void draw_ui_app_panel(",
        "typedef struct OpenRideRouteDownloadPlan",
        "typedef enum OpenRideAppPanel",
        "typedef enum OpenRideDriveAction",
    )
    for token in forbidden_main:
        if token in text:
            raise RuntimeError(f"V2.1: UI implementation token remains in main.c: {token}")

    required_main = (
        "openride_app_ui_toolbar_hit_test(",
        "openride_app_ui_draw_map_status_overlay(",
        "openride_app_ui_draw_place_search_overlay(",
        "openride_app_ui_draw_panel(",
        "openride_app_ui_draw_navigation_overlay(",
        "openride_app_ui_draw_toolbar(",
        "openride_app_ui_drive_controls_hit_test(",
        "openride_app_ui_draw_drive_mode(",
    )
    for token in required_main:
        if token not in text:
            raise RuntimeError(f"V2.1: expected bridge call missing from main.c: {token}")

    required_bridge = (
        "OpenRideToolbarAction openride_app_ui_toolbar_hit_test(",
        "void openride_app_ui_draw_map_status_overlay(",
        "void openride_app_ui_draw_place_search_overlay(",
        "void openride_app_ui_draw_panel(",
        "void openride_app_ui_draw_navigation_overlay(",
        "void openride_app_ui_draw_toolbar(",
        "OpenRideDriveAction openride_app_ui_drive_controls_hit_test(",
        "void openride_app_ui_draw_drive_mode(",
    )
    for token in required_bridge:
        if token not in bridge:
            raise RuntimeError(f"V2.1: generated bridge missing: {token}")

    return bridge, text


def main() -> int:
    if BRIDGE.exists():
        raise RuntimeError(
            "src/app_ui_bridge.c already exists; refusing to overwrite a possibly tested file"
        )

    original_cmake = CMAKE.read_text(encoding="utf-8")
    original_main = MAIN.read_text(encoding="utf-8")

    prepared_cmake = prepare_cmake(original_cmake)
    bridge, prepared_main = generate_bridge_and_main(original_main)

    if prepared_cmake == original_cmake:
        raise RuntimeError("CMakeLists.txt: V2.1 produced no change")
    if prepared_main == original_main:
        raise RuntimeError("src/main.c: V2.1 produced no change")

    removed = len(original_main) - len(prepared_main)
    if removed < 25000 or removed > 80000:
        raise RuntimeError(
            f"src/main.c: unexpected V2.1 size delta ({removed} bytes removed net)"
        )
    if len(bridge) < 25000:
        raise RuntimeError(
            f"src/app_ui_bridge.c: generated file unexpectedly small ({len(bridge)} bytes)"
        )

    # Transactional enough for this one-shot migration: all validation above is
    # completed before any file is written.
    CMAKE.write_text(prepared_cmake, encoding="utf-8")
    MAIN.write_text(prepared_main, encoding="utf-8")
    BRIDGE.write_text(bridge, encoding="utf-8")

    print("OK: Architecture V2.1 batched UI bridge extraction applied")
    print("Changed: CMakeLists.txt, src/main.c")
    print("Created: src/app_ui_bridge.c")
    print(f"main.c reduced by {removed} bytes")
    print("No routing/GPS/storage/region business logic was moved")
    print("This batch intentionally replaces several micro-migrations with one test cycle")
    print("Next: git diff --check && git diff --stat")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
