#!/usr/bin/env python3
"""Batched Architecture V2.2 migration: extract application runtime helpers.

This is deliberately a large batch so one build / Android smoke test validates
several architectural extractions at once.

Moved out of src/main.c in one pass:
- offline place-search/storage helper flow;
- GPX import/record helpers;
- local routing + loop generation helpers;
- RoutingWorld worker context/start/match helpers;
- navigation prepare/clear/reroute/GPX helpers;
- map route-point selection helper;
- Android region download/prepare worker helpers;
- region metadata/camera/select/activate/MapWorld refresh helpers.

The implementations are copied from the current, already-tested main.c and only
renamed to internal app-runtime APIs. Business behavior is intentionally kept
unchanged. The script does not build, test, commit, or push anything.
"""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
CMAKE = ROOT / "CMakeLists.txt"
MAIN = ROOT / "src" / "main.c"
SEARCH_C = ROOT / "src" / "app_search_runtime.c"
ROUTE_C = ROOT / "src" / "app_route_runtime.c"
REGION_C = ROOT / "src" / "app_region_runtime.c"


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


def prepare_cmake(text: str) -> str:
    if "    src/app_search_runtime.c\n" in text:
        raise RuntimeError("V2.2 runtime sources already present in CMake")
    return replace_once(
        text,
        "    src/app_ui_action.c\n"
        "    src/app_ui_bridge.c\n",
        "    src/app_ui_action.c\n"
        "    src/app_ui_bridge.c\n"
        "    src/app_search_runtime.c\n"
        "    src/app_route_runtime.c\n"
        "    src/app_region_runtime.c\n",
        "CMake V2.2 runtime sources",
    )


def rename_calls(text: str, mapping: dict[str, str]) -> str:
    for old, new in mapping.items():
        text = text.replace(old + "(", new + "(")
    return text


def make_public(text: str, signatures: list[tuple[str, str]]) -> str:
    for old, new in signatures:
        count = text.count(old)
        if count != 1:
            raise RuntimeError(
                f"V2.2 public definition {old}: expected one match, found {count}"
            )
        text = text.replace(old, new, 1)
    return text


def extract_search(text: str) -> tuple[str, str]:
    block, text = take_block(
        text,
        "static void utf8_backspace(",
        "static void clear_navigation_session(",
        "V2.2 search/storage helpers",
    )

    mapping = {
        "utf8_backspace": "openride_app_search_utf8_backspace",
        "refresh_place_search": "openride_app_search_refresh",
        "set_destination_from_place":
            "openride_app_search_set_destination_from_place",
        "refresh_stored_places": "openride_app_search_refresh_stored_places",
        "open_place_search": "openride_app_search_open",
    }
    block = rename_calls(block, mapping)
    block = make_public(block, [
        ("static void openride_app_search_utf8_backspace(",
         "void openride_app_search_utf8_backspace("),
        ("static bool openride_app_search_refresh(",
         "bool openride_app_search_refresh("),
        ("static void openride_app_search_set_destination_from_place(",
         "void openride_app_search_set_destination_from_place("),
        ("static void openride_app_search_refresh_stored_places(",
         "void openride_app_search_refresh_stored_places("),
        ("static void openride_app_search_open(",
         "void openride_app_search_open("),
    ])

    source = r'''#include "app_search_runtime.h"

#include <stdio.h>
#include <string.h>

#define OPENRIDE_SEARCH_MAX_RESULTS 8U
#define OPENRIDE_APP_LIST_MAX 12U

''' + block
    text = rename_calls(text, mapping)
    return source, text


def extract_route(text: str) -> tuple[str, str]:
    route_block, text = take_block(
        text,
        "static void fit_camera_to_gpx(",
        "static void draw_loop_waypoints(",
        "V2.2 route/GPX/RoutingWorld helpers",
    )
    nav_block, text = take_block(
        text,
        "static void clear_navigation_session(",
        "static void draw_navigation_position(",
        "V2.2 navigation helpers",
    )
    selection_block, text = take_block(
        text,
        "static bool add_selection_from_screen(",
        "static OpenRideMapCamera camera_from_metadata(",
        "V2.2 map selection helper",
    )

    context_pattern = re.compile(
        r"typedef struct OpenRideRoutingWorldThreadContext \{.*?\} "
        r"OpenRideRoutingWorldThreadContext;\n\n",
        re.S,
    )
    route_block, context_count = context_pattern.subn("", route_block, count=1)
    if context_count != 1:
        raise RuntimeError(
            f"V2.2 routing-world context: expected one typedef, found {context_count}"
        )

    mapping = {
        "fit_camera_to_gpx": "openride_app_route_fit_camera_to_gpx",
        "load_gpx_overlay": "openride_app_route_load_gpx_overlay",
        "record_gps_sample": "openride_app_route_record_gps_sample",
        "recalculate_route": "openride_app_route_recalculate",
        "start_routing_world_thread": "openride_app_route_start_world_thread",
        "start_routing_world_installed_alternative_thread":
            "openride_app_route_start_world_installed_alternative_thread",
        "routing_world_request_matches":
            "openride_app_route_world_request_matches",
        "generate_loop_route": "openride_app_route_generate_loop",
        "clear_navigation_session":
            "openride_app_route_clear_navigation_session",
        "prepare_navigation_session":
            "openride_app_route_prepare_navigation_session",
        "reroute_navigation_from_position":
            "openride_app_route_reroute_from_position",
        "prepare_gpx_navigation":
            "openride_app_route_prepare_gpx_navigation",
        "add_selection_from_screen":
            "openride_app_route_add_selection_from_screen",
    }

    combined = route_block + nav_block + selection_block
    combined = rename_calls(combined, mapping)
    combined = combined.replace(
        "file_exists(path)",
        "openride_platform_file_exists(path)",
    )
    combined = make_public(combined, [
        ("static void openride_app_route_fit_camera_to_gpx(",
         "void openride_app_route_fit_camera_to_gpx("),
        ("static bool openride_app_route_load_gpx_overlay(",
         "bool openride_app_route_load_gpx_overlay("),
        ("static void openride_app_route_record_gps_sample(",
         "void openride_app_route_record_gps_sample("),
        ("static bool openride_app_route_recalculate(",
         "bool openride_app_route_recalculate("),
        ("static SDL_Thread *openride_app_route_start_world_thread(",
         "SDL_Thread *openride_app_route_start_world_thread("),
        ("static SDL_Thread *openride_app_route_start_world_installed_alternative_thread(",
         "SDL_Thread *openride_app_route_start_world_installed_alternative_thread("),
        ("static bool openride_app_route_world_request_matches(",
         "bool openride_app_route_world_request_matches("),
        ("static bool openride_app_route_generate_loop(",
         "bool openride_app_route_generate_loop("),
        ("static void openride_app_route_clear_navigation_session(",
         "void openride_app_route_clear_navigation_session("),
        ("static bool openride_app_route_prepare_navigation_session(",
         "bool openride_app_route_prepare_navigation_session("),
        ("static bool openride_app_route_reroute_from_position(",
         "bool openride_app_route_reroute_from_position("),
        ("static bool openride_app_route_prepare_gpx_navigation(",
         "bool openride_app_route_prepare_gpx_navigation("),
        ("static bool openride_app_route_add_selection_from_screen(",
         "bool openride_app_route_add_selection_from_screen("),
    ])

    source = r'''#include "app_route_runtime.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define OPENRIDE_MAX_SNAP_DISTANCE_M 2000.0
#define OPENRIDE_GPX_RECORDING_MIN_STEP_M 10.0
#define OPENRIDE_GPX_NAVIGATION_SPEED_KPH 50.0

static double app_route_clampd(double value, double min_value, double max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

''' + combined.replace("clampd(", "app_route_clampd(")

    text = rename_calls(text, mapping)
    return source, text


def extract_region(text: str) -> tuple[str, str]:
    android_block, text = take_block(
        text,
        "typedef struct OpenRideRegionPrepareThreadContext",
        "#endif\n\nstatic bool SDLCALL openride_lifecycle_event_watch",
        "V2.2 Android region prepare helpers",
    )
    metadata_block, text = take_block(
        text,
        "static void metadata_from_ormap(",
        "static const char *default_map_path(",
        "V2.2 ORMap metadata helper",
    )
    runtime_block, text = take_block(
        text,
        "static OpenRideMapCamera camera_from_metadata(",
        "int main(int argc, char **argv)",
        "V2.2 region runtime helpers",
    )

    context_pattern = re.compile(
        r"typedef struct OpenRideRegionPrepareThreadContext \{.*?\} "
        r"OpenRideRegionPrepareThreadContext;\n\n",
        re.S,
    )
    android_block, context_count = context_pattern.subn("", android_block, count=1)
    if context_count != 1:
        raise RuntimeError(
            f"V2.2 region context: expected one typedef, found {context_count}"
        )

    mapping = {
        "start_region_prepare_thread": "openride_app_region_start_prepare_thread",
        "region_prepare_stage_text": "openride_app_region_prepare_stage_text",
        "region_prepare_stage_progress":
            "openride_app_region_prepare_stage_progress",
        "start_android_region_file_download":
            "openride_app_region_start_android_file_download",
        "begin_android_region_install":
            "openride_app_region_begin_android_install",
        "metadata_from_ormap": "openride_app_region_metadata_from_ormap",
        "camera_from_metadata": "openride_app_region_camera_from_metadata",
        "region_step": "openride_app_region_step",
        "select_initial_region": "openride_app_region_select_initial",
        "activate_region_runtime": "openride_app_region_activate_runtime",
        "refresh_map_world_overview":
            "openride_app_region_refresh_map_world_overview",
    }

    android_block = rename_calls(android_block, mapping)
    android_block = make_public(android_block, [
        ("static SDL_Thread *openride_app_region_start_prepare_thread(",
         "SDL_Thread *openride_app_region_start_prepare_thread("),
        ("static const char *openride_app_region_prepare_stage_text(",
         "const char *openride_app_region_prepare_stage_text("),
        ("static double openride_app_region_prepare_stage_progress(",
         "double openride_app_region_prepare_stage_progress("),
        ("static bool openride_app_region_start_android_file_download(",
         "bool openride_app_region_start_android_file_download("),
        ("static bool openride_app_region_begin_android_install(",
         "bool openride_app_region_begin_android_install("),
    ])

    metadata_block = rename_calls(metadata_block, mapping)
    metadata_block = make_public(metadata_block, [
        ("static void openride_app_region_metadata_from_ormap(",
         "void openride_app_region_metadata_from_ormap("),
    ])

    runtime_block = rename_calls(runtime_block, mapping)
    runtime_block = make_public(runtime_block, [
        ("static OpenRideMapCamera openride_app_region_camera_from_metadata(",
         "OpenRideMapCamera openride_app_region_camera_from_metadata("),
        ("static const OpenRideRegionDefinition *openride_app_region_step(",
         "const OpenRideRegionDefinition *openride_app_region_step("),
        ("static const OpenRideRegionDefinition *openride_app_region_select_initial(",
         "const OpenRideRegionDefinition *openride_app_region_select_initial("),
        ("static bool openride_app_region_activate_runtime(",
         "bool openride_app_region_activate_runtime("),
        ("static void openride_app_region_refresh_map_world_overview(",
         "void openride_app_region_refresh_map_world_overview("),
    ])

    source = r'''#include "app_region_runtime.h"

#ifdef __ANDROID__
#include "openride/android_region_download.h"
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double app_region_clampd(double value, double min_value, double max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

#ifdef __ANDROID__
''' + android_block + r'''#endif

''' + metadata_block + runtime_block.replace("clampd(", "app_region_clampd(")

    text = rename_calls(text, mapping)
    return source, text


def prepare_main(original: str) -> tuple[str, str, str, str]:
    text = original

    # Order matters because the route/search blocks sit next to each other.
    search_source, text = extract_search(text)
    route_source, text = extract_route(text)
    region_source, text = extract_region(text)

    include_old = (
        '#include "openride/app_ui_action.h"\n'
        '#include "openride/app_ui_bridge.h"\n'
        '#include "openride/drive_mode.h"\n'
    )
    include_new = (
        '#include "openride/app_ui_action.h"\n'
        '#include "openride/app_ui_bridge.h"\n'
        '#include "app_search_runtime.h"\n'
        '#include "app_route_runtime.h"\n'
        '#include "app_region_runtime.h"\n'
        '#include "openride/drive_mode.h"\n'
    )
    text = replace_once(text, include_old, include_new, "V2.2 main runtime includes")

    forbidden = (
        "static void utf8_backspace(",
        "static bool refresh_place_search(",
        "static void set_destination_from_place(",
        "static void refresh_stored_places(",
        "static void open_place_search(",
        "static void fit_camera_to_gpx(",
        "static bool load_gpx_overlay(",
        "static void record_gps_sample(",
        "static bool recalculate_route(",
        "typedef struct OpenRideRoutingWorldThreadContext",
        "static SDL_Thread *start_routing_world_thread(",
        "static bool generate_loop_route(",
        "static void clear_navigation_session(",
        "static bool prepare_navigation_session(",
        "static bool reroute_navigation_from_position(",
        "static bool prepare_gpx_navigation(",
        "static bool add_selection_from_screen(",
        "typedef struct OpenRideRegionPrepareThreadContext",
        "static SDL_Thread *start_region_prepare_thread(",
        "static bool begin_android_region_install(",
        "static void metadata_from_ormap(",
        "static OpenRideMapCamera camera_from_metadata(",
        "static const OpenRideRegionDefinition *region_step(",
        "static const OpenRideRegionDefinition *select_initial_region(",
        "static bool activate_region_runtime(",
        "static void refresh_map_world_overview(",
    )
    for token in forbidden:
        if token in text:
            raise RuntimeError(f"V2.2 legacy runtime definition remains: {token}")

    required = (
        "openride_app_search_open(",
        "openride_app_search_refresh(",
        "openride_app_route_recalculate(",
        "openride_app_route_start_world_thread(",
        "openride_app_route_generate_loop(",
        "openride_app_route_prepare_navigation_session(",
        "openride_app_route_reroute_from_position(",
        "openride_app_region_select_initial(",
        "openride_app_region_activate_runtime(",
        "openride_app_region_refresh_map_world_overview(",
    )
    for token in required:
        if token not in text:
            raise RuntimeError(f"V2.2 expected runtime call missing from main.c: {token}")

    return text, search_source, route_source, region_source


def main() -> int:
    for generated in (SEARCH_C, ROUTE_C, REGION_C):
        if generated.exists():
            raise RuntimeError(
                f"{generated.relative_to(ROOT)} already exists; refusing to overwrite it"
            )

    original_cmake = CMAKE.read_text(encoding="utf-8")
    original_main = MAIN.read_text(encoding="utf-8")

    prepared_cmake = prepare_cmake(original_cmake)
    prepared_main, search_source, route_source, region_source = prepare_main(
        original_main
    )

    removed = len(original_main) - len(prepared_main)
    if removed < 30000 or removed > 120000:
        raise RuntimeError(
            f"src/main.c: unexpected V2.2 size delta ({removed} bytes removed net)"
        )
    if len(search_source) < 4000:
        raise RuntimeError("src/app_search_runtime.c unexpectedly small")
    if len(route_source) < 18000:
        raise RuntimeError("src/app_route_runtime.c unexpectedly small")
    if len(region_source) < 12000:
        raise RuntimeError("src/app_region_runtime.c unexpectedly small")

    # All extraction/transformation checks happen before any write.
    CMAKE.write_text(prepared_cmake, encoding="utf-8")
    MAIN.write_text(prepared_main, encoding="utf-8")
    SEARCH_C.write_text(search_source, encoding="utf-8")
    ROUTE_C.write_text(route_source, encoding="utf-8")
    REGION_C.write_text(region_source, encoding="utf-8")

    print("OK: Architecture V2.2 batched runtime extraction applied")
    print("Changed: CMakeLists.txt, src/main.c")
    print("Created: src/app_search_runtime.c")
    print("Created: src/app_route_runtime.c")
    print("Created: src/app_region_runtime.c")
    print(f"main.c reduced by {removed} bytes")
    print("Search/storage, route/navigation and region runtime helpers extracted together")
    print("Next: git diff --check && git diff --stat")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
