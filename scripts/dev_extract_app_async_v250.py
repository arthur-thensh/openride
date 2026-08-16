#!/usr/bin/env python3
"""Architecture V2.5: extract asynchronous region/routing orchestration.

Large single-test migration. It moves one contiguous frame block from main.c:
- Android region download polling and preparation completion;
- required-map download chaining;
- region activation/reload;
- RoutingWorld worker completion and missing-region planning;
- local route recalculation and RoutingWorld fallback startup.

The existing C statements are copied from the tested main.c and only variable
references are mechanically rebound through OpenRideAppAsyncContext. Strings and
comments are never rewritten. The script is transactional and does not build,
test, commit or push.
"""

from pathlib import Path
import re
import sys
import textwrap

ROOT = Path(__file__).resolve().parents[1]
CMAKE = ROOT / "CMakeLists.txt"
MAIN = ROOT / "src" / "main.c"
ASYNC_H = ROOT / "src" / "app_async_runtime.h"
ASYNC_C = ROOT / "src" / "app_async_runtime.c"

BLOCK_START = "#ifdef __ANDROID__\n        if (region_download_started) {"
BLOCK_END = "        const Uint64 current_ticks = SDL_GetTicks();"


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


def replace_identifier(text: str, name: str, expression: str) -> str:
    """Replace a C identifier, ignoring strings, chars, comments and members."""
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        if text.startswith("//", i):
            end = text.find("\n", i)
            if end < 0:
                out.append(text[i:])
                break
            out.append(text[i:end + 1])
            i = end + 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end < 0:
                out.append(text[i:])
                break
            out.append(text[i:end + 2])
            i = end + 2
            continue
        if text[i] in ('"', "'"):
            quote = text[i]
            start = i
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if i < n and text[i] == quote:
                    i += 1
                    break
                i += 1
            out.append(text[start:i])
            continue
        if text[i].isalpha() or text[i] == "_":
            start = i
            i += 1
            while i < n and (text[i].isalnum() or text[i] == "_"):
                i += 1
            token = text[start:i]
            previous = text[start - 1] if start > 0 else ""
            previous_two = text[start - 2:start] if start >= 2 else ""
            if token == name and previous != "." and previous_two != "->":
                out.append(expression)
            else:
                out.append(token)
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


# Most state already lives behind the V2.4 event context. V2.5 reuses it rather
# than creating a second giant flat structure. Only resources absent from that
# context are stored directly in OpenRideAppAsyncContext.
VARIABLES = {
    # V2.5-only resource handles.
    "map": "(*context->map)",
    "ormap": "(*context->ormap)",
    "metadata_storage": "(*context->metadata_storage)",
    "raster_renderer": "(*context->raster_renderer)",
    "place_index": "(*context->place_index)",
    "region_download_status": "(*context->region_download_status)",

    # Shared application state through V2.4 OpenRideAppEventContext.
    "renderer": "context->events->renderer",
    "platform_paths": "(*context->events->platform_paths)",
    "camera": "(*context->events->camera)",
    "selection": "(*context->events->selection)",
    "route": "(*context->events->route)",
    "routing_graph": "(*context->events->routing_graph)",
    "graph_loaded": "(*context->events->graph_loaded)",
    "routing_profile": "(*context->events->routing_profile)",
    "map_style": "(*context->events->map_style)",
    "scalable_map": "(*context->events->scalable_map)",
    "ormap_map": "(*context->events->ormap_map)",
    "vector_map": "(*context->events->vector_map)",
    "ormap_renderer": "(*context->events->ormap_renderer)",
    "vector_renderer": "(*context->events->vector_renderer)",
    "metadata": "(*context->events->metadata)",
    "map_world": "context->events->map_world",
    "routing_world_cache": "(*context->events->routing_world_cache)",
    "routing_world_context": "(*context->events->routing_world_context)",
    "routing_world_thread": "(*context->events->routing_world_thread)",
    "routing_world_pending_reroute": "(*context->events->routing_world_pending_reroute)",
    "routing_world_pending_resume_simulator": "(*context->events->routing_world_pending_resume_simulator)",
    "navigation": "(*context->events->navigation)",
    "navigation_instructions": "(*context->events->navigation_instructions)",
    "navigation_session": "(*context->events->navigation_session)",
    "location_filter": "(*context->events->location_filter)",
    "filtered_location": "(*context->events->filtered_location)",
    "voice_guidance": "(*context->events->voice_guidance)",
    "gps_simulator": "(*context->events->gps_simulator)",
    "navigation_state": "(*context->events->navigation_state)",
    "gps_sample": "(*context->events->gps_sample)",
    "gps_sample_valid": "(*context->events->gps_sample_valid)",
    "drive_mode": "(*context->events->drive_mode)",
    "follow_gps": "(*context->events->follow_gps)",
    "simulator_deviation": "(*context->events->simulator_deviation)",
    "gpx_navigation_active": "(*context->events->gpx_navigation_active)",
    "loop_waypoint_count": "(*context->events->loop_waypoint_count)",
    "loop_active": "(*context->events->loop_active)",
    "start_snap": "(*context->events->start_snap)",
    "destination_snap": "(*context->events->destination_snap)",
    "route_valid": "(*context->events->route_valid)",
    "route_dirty": "(*context->events->route_dirty)",
    "place_world": "context->events->place_world",
    "app_storage": "context->events->app_storage",
    "history_places": "context->events->history_places",
    "history_count": "(*context->events->history_count)",
    "app_panel": "(*context->events->app_panel)",
    "app_panel_selected": "(*context->events->app_panel_selected)",
    "route_download_plan": "(*context->events->route_download_plan)",
    "region": "(*context->events->region)",
    "active_region": "(*context->events->active_region)",
    "region_status": "(*context->events->region_status)",
    "region_busy": "(*context->events->region_busy)",
    "region_activation_requested": "(*context->events->region_activation_requested)",
    "region_progress": "(*context->events->region_progress)",
    "region_work_status": "context->events->region_work_status",
    "route_status": "context->events->route_status",
    "error": "context->events->error",

    # Android members already present in the event context.
    "real_gps_active": "(*context->events->real_gps_active)",
    "simulated_gps_active": "(*context->events->simulated_gps_active)",
    "region_prepare_context": "(*context->events->region_prepare_context)",
    "region_prepare_thread": "(*context->events->region_prepare_thread)",
    "region_download_started": "(*context->events->region_download_started)",
    "region_download_is_poly": "(*context->events->region_download_is_poly)",
}


def transform_block(block: str) -> str:
    block = textwrap.dedent(block)

    # These three arrays become pointers through the contexts. Preserve their
    # real capacities rather than accidentally using sizeof(pointer).
    block = block.replace("sizeof(route_status)",
                          "context->events->route_status_size")
    block = block.replace("sizeof(region_work_status)",
                          "context->events->region_work_status_size")
    block = block.replace("sizeof(error)",
                          "context->events->error_size")

    for name in sorted(VARIABLES, key=len, reverse=True):
        block = replace_identifier(block, name, VARIABLES[name])
    return block


ASYNC_INITIALIZER = r'''    OpenRideAppAsyncContext async_context = {
        .events = &event_context,
        .map = &map,
        .ormap = &ormap,
        .metadata_storage = &metadata_storage,
        .raster_renderer = &raster_renderer,
        .place_index = &place_index,
#ifdef __ANDROID__
        .region_download_status = &region_download_status,
#endif
    };

'''


def prepare_main(original: str) -> tuple[str, str]:
    block, text = take_between(
        original,
        BLOCK_START,
        BLOCK_END,
        "V2.5 async region/routing frame block",
    )
    transformed = transform_block(block)

    text = replace_once(
        text,
        BLOCK_END,
        "        openride_app_async_update(&async_context);\n\n" + BLOCK_END,
        "V2.5 async update call",
    )
    text = replace_once(
        text,
        "    while (running) {\n        uint64_t map_zoom_loop_started_ns =",
        ASYNC_INITIALIZER
        + "    while (running) {\n        uint64_t map_zoom_loop_started_ns =",
        "V2.5 async context initializer",
    )
    text = replace_once(
        text,
        '#include "app_event_runtime.h"\n',
        '#include "app_event_runtime.h"\n#include "app_async_runtime.h"\n',
        "V2.5 async runtime include",
    )

    source = r'''#include "app_async_runtime.h"

#include <stdio.h>
#include <string.h>

void openride_app_async_update(OpenRideAppAsyncContext *context)
{
    if (!context || !context->events || !context->events->renderer
        || !context->map || !context->ormap || !context->metadata_storage
        || !context->raster_renderer || !context->place_index) return;

''' + textwrap.indent(transformed, "    ") + r'''
}
'''

    forbidden_main = (
        "if (region_download_started) {",
        "if (region_prepare_thread) {",
        "if (region_activation_requested && !region_busy && !routing_world_thread)",
        "if (routing_world_thread\n            && SDL_GetAtomicInt(&routing_world_context.done))",
        "if (route_dirty && !routing_world_thread) {",
    )
    for token in forbidden_main:
        if token in text:
            raise RuntimeError(
                f"V2.5: extracted orchestration remains in main.c: {token}"
            )

    for token in (
        '#include "app_async_runtime.h"',
        "OpenRideAppAsyncContext async_context = {",
        "openride_app_async_update(&async_context);",
    ):
        if token not in text:
            raise RuntimeError(f"V2.5: main wiring missing: {token}")

    for token in (
        "void openride_app_async_update(",
        "openride_android_region_download_poll",
        "openride_app_region_activate_runtime",
        "openride_app_route_world_request_matches",
        "openride_app_route_recalculate",
        "openride_app_route_start_world_thread",
    ):
        if token not in source:
            raise RuntimeError(f"V2.5: generated async runtime missing: {token}")

    # Verify no main-scope identifier survived the transformation. The scanner
    # ignores strings/comments and ignores same-named struct members after . / ->.
    for name in VARIABLES:
        if replace_identifier(source, name, "__OPENRIDE_UNBOUND__") != source:
            raise RuntimeError(
                f"V2.5: unbound main variable remains in async runtime: {name}"
            )

    return text, source


def prepare_cmake(text: str) -> str:
    if "    src/app_async_runtime.c\n" in text:
        raise RuntimeError("V2.5 async runtime already present in CMake")
    return replace_once(
        text,
        "    src/app_event_runtime.c\n",
        "    src/app_event_runtime.c\n"
        "    src/app_async_runtime.c\n",
        "CMake V2.5 async runtime source",
    )


def main() -> int:
    if not ASYNC_H.exists():
        raise RuntimeError("src/app_async_runtime.h is missing; git pull first")
    if ASYNC_C.exists():
        raise RuntimeError("src/app_async_runtime.c already exists; refusing to overwrite")

    original_cmake = CMAKE.read_text(encoding="utf-8")
    original_main = MAIN.read_text(encoding="utf-8")

    prepared_cmake = prepare_cmake(original_cmake)
    prepared_main, async_source = prepare_main(original_main)

    removed = len(original_main) - len(prepared_main)
    if removed < 18000 or removed > 90000:
        raise RuntimeError(
            f"src/main.c: unexpected V2.5 size delta ({removed} bytes removed)"
        )
    if len(async_source) < 18000:
        raise RuntimeError(
            f"src/app_async_runtime.c unexpectedly small ({len(async_source)} bytes)"
        )

    # Transactional: all guards above pass before the first write.
    CMAKE.write_text(prepared_cmake, encoding="utf-8")
    MAIN.write_text(prepared_main, encoding="utf-8")
    ASYNC_C.write_text(async_source, encoding="utf-8")

    print("OK: Architecture V2.5 batched async controller extraction applied")
    print("Changed: CMakeLists.txt, src/main.c")
    print("Created: src/app_async_runtime.c")
    print(f"main.c reduced by {removed} bytes")
    print("Moved: region workers + activation + RoutingWorld completion + route fallback")
    print("Next: git diff --check && git diff --stat")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
