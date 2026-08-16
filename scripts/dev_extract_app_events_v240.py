#!/usr/bin/env python3
"""Architecture V2.4: extract the full SDL input loop + pending UI dispatch.

This is intentionally a large batch. It moves, in one migration / one test cycle:
- SDL keyboard event handling;
- text-input search handling;
- desktop mouse map editing/pan/zoom;
- Android touch/pinch handling;
- Android application-panel action dispatch;
- toolbar pending-action dispatch;
- Drive HUD pending-action dispatch.

The code bodies are copied from the already-tested src/main.c and mechanically
rewired through OpenRideAppEventContext. Lifecycle handling, asynchronous region
work, RoutingWorld completion, GPS updates and rendering remain in main.c for
this stage. The script does not build, test, commit or push anything.
"""

from pathlib import Path
import re
import sys
import textwrap

ROOT = Path(__file__).resolve().parents[1]
CMAKE = ROOT / "CMakeLists.txt"
MAIN = ROOT / "src" / "main.c"
EVENT_C = ROOT / "src" / "app_event_runtime.c"
EVENT_H = ROOT / "src" / "app_event_runtime.h"

EVENT_START = "        SDL_Event event;\n\n        while (SDL_PollEvent(&event)) {"
EVENT_END = "\n\n        const int lifecycle_signal ="
PENDING_START = "        if (pending_drive_action != OPENRIDE_DRIVE_ACTION_NONE) {"
PENDING_END = "\n\n#ifdef __ANDROID__\n        if (region_download_started) {"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def take_between(text: str,
                 start_marker: str,
                 end_marker: str,
                 label: str) -> tuple[str, str]:
    starts = text.count(start_marker)
    if starts != 1:
        raise RuntimeError(f"{label}: expected one start marker, found {starts}")
    start = text.find(start_marker)
    end = text.find(end_marker, start + len(start_marker))
    if end < 0:
        raise RuntimeError(f"{label}: end marker not found")
    if end <= start:
        raise RuntimeError(f"{label}: invalid marker order")
    return text[start:end], text[:start] + text[end:]


# Context fields are pointers to the existing main() variables. Expressions are
# deliberately parenthesized so existing uses such as &camera, camera.zoom,
# route = ..., sizeof(route), etc. remain valid after mechanical substitution.
VARIABLES = {
    "window": "context->window",
    "renderer": "context->renderer",
    "running": "(*context->running)",
    "platform_paths": "(*context->platform_paths)",
    "camera": "(*context->camera)",
    "map_zoom_test": "(*context->map_zoom_test)",
    "selection": "(*context->selection)",
    "route": "(*context->route)",
    "routing_graph": "(*context->routing_graph)",
    "graph_loaded": "(*context->graph_loaded)",
    "routing_profile": "(*context->routing_profile)",
    "map_style": "(*context->map_style)",
    "scalable_map": "(*context->scalable_map)",
    "ormap_map": "(*context->ormap_map)",
    "vector_map": "(*context->vector_map)",
    "ormap_renderer": "(*context->ormap_renderer)",
    "vector_renderer": "(*context->vector_renderer)",
    "metadata": "(*context->metadata)",
    "map_world": "context->map_world",
    "routing_world_cache": "(*context->routing_world_cache)",
    "routing_world_context": "(*context->routing_world_context)",
    "routing_world_thread": "(*context->routing_world_thread)",
    "routing_world_pending_reroute": "(*context->routing_world_pending_reroute)",
    "routing_world_pending_resume_simulator": "(*context->routing_world_pending_resume_simulator)",
    "navigation": "(*context->navigation)",
    "navigation_instructions": "(*context->navigation_instructions)",
    "navigation_session": "(*context->navigation_session)",
    "location_filter": "(*context->location_filter)",
    "filtered_location": "(*context->filtered_location)",
    "voice_guidance": "(*context->voice_guidance)",
    "gps_simulator": "(*context->gps_simulator)",
    "navigation_state": "(*context->navigation_state)",
    "gps_sample": "(*context->gps_sample)",
    "gps_sample_valid": "(*context->gps_sample_valid)",
    "drive_mode": "(*context->drive_mode)",
    "follow_gps": "(*context->follow_gps)",
    "auto_reroute": "(*context->auto_reroute)",
    "voice_enabled": "(*context->voice_enabled)",
    "simulator_deviation": "(*context->simulator_deviation)",
    "gpx_navigation_active": "(*context->gpx_navigation_active)",
    "gpx_overlay": "(*context->gpx_overlay)",
    "gpx_recording": "(*context->gpx_recording)",
    "gpx_loaded": "(*context->gpx_loaded)",
    "gpx_recording_active": "(*context->gpx_recording_active)",
    "gpx_last_recorded_position_m": "(*context->gpx_last_recorded_position_m)",
    "gpx_import_path": "context->gpx_import_path",
    "gpx_route_export_path": "context->gpx_route_export_path",
    "gpx_recording_export_path": "context->gpx_recording_export_path",
    "loop_target_distance_m": "(*context->loop_target_distance_m)",
    "loop_direction": "(*context->loop_direction)",
    "loop_stats": "(*context->loop_stats)",
    "loop_waypoints": "context->loop_waypoints",
    "loop_waypoint_count": "(*context->loop_waypoint_count)",
    "loop_seed": "(*context->loop_seed)",
    "loop_active": "(*context->loop_active)",
    "start_snap": "(*context->start_snap)",
    "destination_snap": "(*context->destination_snap)",
    "route_valid": "(*context->route_valid)",
    "route_dirty": "(*context->route_dirty)",
    "place_world": "context->place_world",
    "app_storage": "context->app_storage",
    "place_search_active": "(*context->place_search_active)",
    "place_search_purpose": "(*context->place_search_purpose)",
    "place_search_query": "context->place_search_query",
    "place_search_results": "context->place_search_results",
    "place_search_result_count": "(*context->place_search_result_count)",
    "place_search_selected": "(*context->place_search_selected)",
    "favorite_places": "context->favorite_places",
    "history_places": "context->history_places",
    "favorite_count": "(*context->favorite_count)",
    "history_count": "(*context->history_count)",
    "app_panel": "(*context->app_panel)",
    "app_panel_selected": "(*context->app_panel_selected)",
    "route_download_plan": "(*context->route_download_plan)",
    "route_map_pick_marker": "(*context->route_map_pick_marker)",
    "region": "(*context->region)",
    "active_region": "(*context->active_region)",
    "region_status": "(*context->region_status)",
    "region_busy": "(*context->region_busy)",
    "region_activation_requested": "(*context->region_activation_requested)",
    "region_progress": "(*context->region_progress)",
    "region_work_status": "context->region_work_status",
    "dragging_map": "(*context->dragging_map)",
    "map_drag_moved": "(*context->map_drag_moved)",
    "mouse_down_x": "(*context->mouse_down_x)",
    "mouse_down_y": "(*context->mouse_down_y)",
    "dragging_marker": "(*context->dragging_marker)",
    "touch_input": "(*context->touch_input)",
    "pending_toolbar_action": "(*context->pending_toolbar_action)",
    "pending_drive_action": "(*context->pending_drive_action)",
    "route_status": "context->route_status",
    "error": "context->error",
    # Android-only variables. They only occur inside existing #ifdef blocks.
    "location_provider": "(*context->location_provider)",
    "simulated_location_provider": "(*context->simulated_location_provider)",
    "simulated_location_context": "(*context->simulated_location_context)",
    "real_gps_active": "(*context->real_gps_active)",
    "real_gps_requested": "(*context->real_gps_requested)",
    "simulated_gps_active": "(*context->simulated_gps_active)",
    "route_start_gps_pending": "(*context->route_start_gps_pending)",
    "android_gps_sample_age_s": "(*context->android_gps_sample_age_s)",
    "android_gps_accuracy_m": "(*context->android_gps_accuracy_m)",
    "missed_turn_dev": "(*context->missed_turn_dev)",
    "region_prepare_context": "(*context->region_prepare_context)",
    "region_prepare_thread": "(*context->region_prepare_thread)",
    "region_download_started": "(*context->region_download_started)",
    "region_download_is_poly": "(*context->region_download_is_poly)",
}


def replace_variable(text: str, name: str, expression: str) -> str:
    # Do not rewrite a same-named struct member after '.' or '->'.
    pattern = rf"(?<![A-Za-z0-9_.>])\b{re.escape(name)}\b"
    return re.sub(pattern, expression, text)


def transform_block(block: str, frame_local: bool) -> str:
    block = textwrap.dedent(block)

    # Arrays become pointers in the context, so preserve their original sizes.
    block = block.replace("sizeof(route_status)", "context->route_status_size")
    block = block.replace("sizeof(error)", "context->error_size")
    block = block.replace("sizeof(region_work_status)", "context->region_work_status_size")
    block = block.replace("sizeof(place_search_query)", "context->place_search_query_size")

    if frame_local:
        block = replace_variable(
            block,
            "map_zoom_loop_started_ns",
            "(*map_zoom_loop_started_ns)",
        )

    for name in sorted(VARIABLES, key=len, reverse=True):
        block = replace_variable(block, name, VARIABLES[name])
    return block


CONTEXT_INITIALIZER = r'''    OpenRideAppEventContext event_context = {
        .window = window,
        .renderer = renderer,
        .running = &running,
        .platform_paths = &platform_paths,
        .camera = &camera,
        .map_zoom_test = &map_zoom_test,
        .selection = &selection,
        .route = &route,
        .routing_graph = &routing_graph,
        .graph_loaded = &graph_loaded,
        .routing_profile = &routing_profile,
        .map_style = &map_style,
        .scalable_map = &scalable_map,
        .ormap_map = &ormap_map,
        .vector_map = &vector_map,
        .ormap_renderer = &ormap_renderer,
        .vector_renderer = &vector_renderer,
        .metadata = &metadata,
        .map_world = map_world,
        .routing_world_cache = &routing_world_cache,
        .routing_world_context = &routing_world_context,
        .routing_world_thread = &routing_world_thread,
        .routing_world_pending_reroute = &routing_world_pending_reroute,
        .routing_world_pending_resume_simulator = &routing_world_pending_resume_simulator,
        .navigation = &navigation,
        .navigation_instructions = &navigation_instructions,
        .navigation_session = &navigation_session,
        .location_filter = &location_filter,
        .filtered_location = &filtered_location,
        .voice_guidance = &voice_guidance,
        .gps_simulator = &gps_simulator,
        .navigation_state = &navigation_state,
        .gps_sample = &gps_sample,
        .gps_sample_valid = &gps_sample_valid,
        .drive_mode = &drive_mode,
        .follow_gps = &follow_gps,
        .auto_reroute = &auto_reroute,
        .voice_enabled = &voice_enabled,
        .simulator_deviation = &simulator_deviation,
        .gpx_navigation_active = &gpx_navigation_active,
        .gpx_overlay = &gpx_overlay,
        .gpx_recording = &gpx_recording,
        .gpx_loaded = &gpx_loaded,
        .gpx_recording_active = &gpx_recording_active,
        .gpx_last_recorded_position_m = &gpx_last_recorded_position_m,
        .gpx_import_path = gpx_import_path,
        .gpx_route_export_path = gpx_route_export_path,
        .gpx_recording_export_path = gpx_recording_export_path,
        .loop_target_distance_m = &loop_target_distance_m,
        .loop_direction = &loop_direction,
        .loop_stats = &loop_stats,
        .loop_waypoints = loop_waypoints,
        .loop_waypoint_count = &loop_waypoint_count,
        .loop_seed = &loop_seed,
        .loop_active = &loop_active,
        .start_snap = &start_snap,
        .destination_snap = &destination_snap,
        .route_valid = &route_valid,
        .route_dirty = &route_dirty,
        .place_world = place_world,
        .app_storage = app_storage,
        .place_search_active = &place_search_active,
        .place_search_purpose = &place_search_purpose,
        .place_search_query = place_search_query,
        .place_search_query_size = sizeof(place_search_query),
        .place_search_results = place_search_results,
        .place_search_result_count = &place_search_result_count,
        .place_search_selected = &place_search_selected,
        .favorite_places = favorite_places,
        .history_places = history_places,
        .favorite_count = &favorite_count,
        .history_count = &history_count,
        .app_panel = &app_panel,
        .app_panel_selected = &app_panel_selected,
        .route_download_plan = &route_download_plan,
        .route_map_pick_marker = &route_map_pick_marker,
        .region = &region,
        .active_region = &active_region,
        .region_status = &region_status,
        .region_busy = &region_busy,
        .region_activation_requested = &region_activation_requested,
        .region_progress = &region_progress,
        .region_work_status = region_work_status,
        .region_work_status_size = sizeof(region_work_status),
        .dragging_map = &dragging_map,
        .map_drag_moved = &map_drag_moved,
        .mouse_down_x = &mouse_down_x,
        .mouse_down_y = &mouse_down_y,
        .dragging_marker = &dragging_marker,
        .touch_input = &touch_input,
        .pending_toolbar_action = &pending_toolbar_action,
        .pending_drive_action = &pending_drive_action,
        .route_status = route_status,
        .route_status_size = sizeof(route_status),
        .error = error,
        .error_size = sizeof(error),
#ifdef __ANDROID__
        .location_provider = &location_provider,
        .simulated_location_provider = &simulated_location_provider,
        .simulated_location_context = &simulated_location_context,
        .real_gps_active = &real_gps_active,
        .real_gps_requested = &real_gps_requested,
        .simulated_gps_active = &simulated_gps_active,
        .route_start_gps_pending = &route_start_gps_pending,
        .android_gps_sample_age_s = &android_gps_sample_age_s,
        .android_gps_accuracy_m = &android_gps_accuracy_m,
        .missed_turn_dev = &missed_turn_dev,
        .region_prepare_context = &region_prepare_context,
        .region_prepare_thread = &region_prepare_thread,
        .region_download_started = &region_download_started,
        .region_download_is_poly = &region_download_is_poly,
#endif
    };

'''


def prepare_main(original: str) -> tuple[str, str]:
    event_block, text = take_between(
        original, EVENT_START, EVENT_END, "V2.4 SDL event loop"
    )
    pending_block, text = take_between(
        text, PENDING_START, PENDING_END, "V2.4 pending UI dispatch"
    )

    event_source = transform_block(event_block, frame_local=True)
    pending_source = transform_block(pending_block, frame_local=False)

    text = replace_once(
        text,
        EVENT_END,
        "\n\n        openride_app_events_poll(&event_context,\n"
        "                                 &map_zoom_loop_started_ns);"
        + EVENT_END,
        "V2.4 event poll call",
    )
    text = replace_once(
        text,
        PENDING_END,
        "\n\n        openride_app_events_dispatch_pending(&event_context);"
        + PENDING_END,
        "V2.4 pending dispatch call",
    )

    text = replace_once(
        text,
        "    while (running) {\n",
        CONTEXT_INITIALIZER + "    while (running) {\n",
        "V2.4 event context initializer",
    )
    text = replace_once(
        text,
        '#include "app_support_runtime.h"\n',
        '#include "app_support_runtime.h"\n#include "app_event_runtime.h"\n',
        "V2.4 event runtime include",
    )

    source = r'''#include "app_event_runtime.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define OPENRIDE_CLICK_DRAG_THRESHOLD 5.0
#define OPENRIDE_LOOP_DISTANCE_STEP_M 25000.0
#define OPENRIDE_LOOP_DISTANCE_MIN_M 25000.0
#define OPENRIDE_LOOP_DISTANCE_MAX_M 300000.0

void openride_app_events_poll(OpenRideAppEventContext *context,
                              uint64_t *map_zoom_loop_started_ns)
{
    if (!context || !context->window || !context->renderer
        || !context->running || !map_zoom_loop_started_ns) return;

''' + textwrap.indent(event_source, "    ") + r'''
}

void openride_app_events_dispatch_pending(OpenRideAppEventContext *context)
{
    if (!context || !context->window || !context->renderer) return;

''' + textwrap.indent(pending_source, "    ") + r'''
}
'''

    forbidden_main = (
        "while (SDL_PollEvent(&event))",
        "if (pending_drive_action != OPENRIDE_DRIVE_ACTION_NONE)",
        "if (pending_toolbar_action != OPENRIDE_TOOLBAR_NONE)",
    )
    for token in forbidden_main:
        if token in text:
            raise RuntimeError(f"V2.4: extracted implementation remains in main.c: {token}")

    required_main = (
        "OpenRideAppEventContext event_context = {",
        "openride_app_events_poll(&event_context",
        "openride_app_events_dispatch_pending(&event_context)",
        '#include "app_event_runtime.h"',
    )
    for token in required_main:
        if token not in text:
            raise RuntimeError(f"V2.4: required main wiring missing: {token}")

    required_source = (
        "void openride_app_events_poll(",
        "SDL_EVENT_KEY_DOWN",
        "SDL_EVENT_MOUSE_BUTTON_DOWN",
        "SDL_EVENT_FINGER_DOWN",
        "void openride_app_events_dispatch_pending(",
        "OPENRIDE_DRIVE_ACTION_EXIT",
        "OPENRIDE_TOOLBAR_ROUTE",
    )
    for token in required_source:
        if token not in source:
            raise RuntimeError(f"V2.4: generated event runtime missing: {token}")

    # Catch the most important local-variable names if a context rewrite failed.
    for name in (
        "pending_toolbar_action",
        "pending_drive_action",
        "place_search_active",
        "route_valid",
        "route_dirty",
        "app_panel",
        "dragging_marker",
        "routing_profile",
        "region_busy",
    ):
        pattern = rf"(?<![A-Za-z0-9_.>])\b{re.escape(name)}\b"
        if re.search(pattern, source):
            raise RuntimeError(
                f"V2.4: unbound main variable remains in event runtime: {name}"
            )

    return text, source


def prepare_cmake(text: str) -> str:
    if "    src/app_event_runtime.c\n" in text:
        raise RuntimeError("V2.4 event runtime already present in CMake")
    return replace_once(
        text,
        "    src/app_support_runtime.c\n",
        "    src/app_support_runtime.c\n"
        "    src/app_event_runtime.c\n",
        "CMake V2.4 event runtime source",
    )


def main() -> int:
    if !EVENT_H.exists():
        raise RuntimeError("src/app_event_runtime.h is missing; git pull first")
    if EVENT_C.exists():
        raise RuntimeError("src/app_event_runtime.c already exists; refusing to overwrite")

    original_cmake = CMAKE.read_text(encoding="utf-8")
    original_main = MAIN.read_text(encoding="utf-8")

    prepared_cmake = prepare_cmake(original_cmake)
    prepared_main, event_source = prepare_main(original_main)

    removed = len(original_main) - len(prepared_main)
    if removed < 50000 or removed > 190000:
        raise RuntimeError(
            f"src/main.c: unexpected V2.4 size delta ({removed} bytes removed)"
        )
    if len(event_source) < 50000:
        raise RuntimeError(
            f"src/app_event_runtime.c unexpectedly small ({len(event_source)} bytes)"
        )

    # Transactional: validate all generated content before touching disk.
    CMAKE.write_text(prepared_cmake, encoding="utf-8")
    MAIN.write_text(prepared_main, encoding="utf-8")
    EVENT_C.write_text(event_source, encoding="utf-8")

    print("OK: Architecture V2.4 batched event/controller extraction applied")
    print("Changed: CMakeLists.txt, src/main.c")
    print("Created: src/app_event_runtime.c")
    print(f"main.c reduced by {removed} bytes")
    print("Moved: SDL input loop + Android panel actions + toolbar/Drive dispatch")
    print("Kept in main: lifecycle, async workers, GPS update and rendering")
    print("Next: git diff --check && git diff --stat")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
