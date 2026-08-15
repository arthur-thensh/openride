#!/usr/bin/env python3
"""One-shot guarded migration for UI Engine V1.6 offline search.

Moves Android offline-search layout, rendering, and touch hit-testing to the
reusable ui_search_overlay component. Search execution, text input, selection
semantics, route-start/destination behavior, and history remain owned by main.c.

All replacements are prepared in memory before any file is written. This script
never builds, tests, commits, or pushes anything.
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


def replace_scoped_block(text: str,
                         scope_start: str,
                         block_start: str,
                         block_end: str,
                         replacement: str,
                         label: str) -> str:
    scope_index = text.find(scope_start)
    if scope_index < 0:
        raise RuntimeError(f"{label}: scope start not found")
    start = text.find(block_start, scope_index)
    if start < 0:
        raise RuntimeError(f"{label}: block start not found")
    end = text.find(block_end, start)
    if end < 0:
        raise RuntimeError(f"{label}: block end not found")
    if text.find(block_start, start + len(block_start), end) >= 0:
        raise RuntimeError(f"{label}: ambiguous nested block start")
    return text[:start] + replacement + text[end:]


def prepare_cmake(text: str) -> str:
    return replace_once(
        text,
        "    src/ui/ui_regions_panel.c\n    src/ui/ui_places_panel.c\n)",
        "    src/ui/ui_regions_panel.c\n    src/ui/ui_places_panel.c\n    src/ui/ui_search_overlay.c\n)",
        "CMake V1.6 UI source",
    )


def prepare_main(text: str) -> str:
    text = replace_once(
        text,
        '#include "openride/ui_places_panel.h"\n#include "openride/drive_mode.h"',
        '#include "openride/ui_places_panel.h"\n#include "openride/ui_search_overlay.h"\n#include "openride/drive_mode.h"',
        "V1.6 UI include",
    )

    legacy_layout_start = "#ifdef __ANDROID__\ntypedef struct OpenRideMobilePlaceSearchLayout {"
    draw_marker = "static void draw_place_search_overlay("
    layout_start = text.find(legacy_layout_start)
    if layout_start < 0:
        raise RuntimeError("V1.6 legacy mobile search layout: start not found")
    draw_start = text.find(draw_marker, layout_start)
    if draw_start < 0:
        raise RuntimeError("V1.6 legacy mobile search layout: draw marker not found")
    layout_block = text[layout_start:draw_start]
    if not layout_block.endswith("#endif\n\n"):
        raise RuntimeError("V1.6 legacy mobile search layout: unexpected block ending")
    text = text[:layout_start] + text[draw_start:]

    android_draw = '''#ifdef __ANDROID__
    int viewport_height = 0;
    int queried_width = viewport_width;
    SDL_GetCurrentRenderOutputSize(renderer, &queried_width, &viewport_height);
    if (queried_width > 0) viewport_width = queried_width;
    if (viewport_height <= 0) return;

    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return;
    }

    uint32_t count = result_count;
    if (count > OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS) {
        count = OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS;
    }
    OpenRideUISearchOverlayState state = {
        .available = available,
        .title = title,
        .query = query,
        .count = count,
        .selected = selected_result
    };
    char secondary[OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS][96];
    for (uint32_t i = 0U; i < count; ++i) {
        const OpenRideRegionDefinition *result_region =
            results[i].region_id[0] != '\\0'
                ? openride_region_find(results[i].region_id)
                : NULL;
        snprintf(secondary[i],
                 sizeof(secondary[i]),
                 "%s%s%s%s",
                 openride_place_kind_name(results[i].kind),
                 result_region ? " - " : "",
                 result_region ? result_region->name : "",
                 results[i].bundled_lite ? " - France" : "");
        state.items[i].name = results[i].name;
        state.items[i].secondary = secondary[i];
    }
    openride_ui_search_overlay_draw(&ui, &state);
    openride_ui_end(&ui);
    return;
#else
'''
    text = replace_scoped_block(
        text,
        draw_marker,
        "#ifdef __ANDROID__\n",
        "#else\n    const float w = 620.0f;",
        android_draw,
        "V1.6 Android search renderer",
    )

    result_marker = "static int place_search_result_at("
    android_hit = '''#ifdef __ANDROID__
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return -1;
    }
    const int result = openride_ui_search_overlay_result_at(
        &ui,
        result_count,
        x,
        y);
    openride_ui_end(&ui);
    return result;
#else
'''
    text = replace_scoped_block(
        text,
        result_marker,
        "#ifdef __ANDROID__\n",
        "#else\n    (void)renderer;",
        android_hit,
        "V1.6 Android search hit-test",
    )
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

    for path, content in prepared.items():
        path.write_text(content, encoding="utf-8")

    print("OK: UI Engine V1.6 offline-search migration applied")
    print("Changed: CMakeLists.txt, src/main.c")
    print("Next: git diff --check && git diff -- CMakeLists.txt src/main.c")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
