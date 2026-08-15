#!/usr/bin/env python3
"""One-shot guarded migration for UI Engine V1.2 main menu.

It prepares all replacements in memory first and writes files only after every
expected source fragment has matched exactly once. It never builds, tests,
commits, or pushes anything.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "src" / "main.c"
CMAKE = ROOT / "CMakeLists.txt"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def migrated_cmake(text: str) -> str:
    return replace_once(
        text,
        "    src/map/map_world.c\n    src/ui/ui.c\n)",
        "    src/map/map_world.c\n    src/ui/ui.c\n    src/ui/ui_toolbar.c\n    src/ui/ui_main_menu.c\n)",
        "CMake UI sources",
    )


def migrated_main(text: str) -> str:
    text = replace_once(
        text,
        '#include "openride/ui_toolbar.h"\n#include "openride/drive_mode.h"',
        '#include "openride/ui_toolbar.h"\n#include "openride/ui_main_menu.h"\n#include "openride/drive_mode.h"',
        "main-menu include",
    )

    old_hit_struct = '''typedef struct OpenRideMobilePanelHit {
    OpenRideMobilePanelAction action;
    int index;
} OpenRideMobilePanelHit;

typedef struct OpenRideMobilePanelLayout {'''
    new_hit_struct = '''typedef struct OpenRideMobilePanelHit {
    OpenRideMobilePanelAction action;
    int index;
} OpenRideMobilePanelHit;

static OpenRideMobilePanelHit mobile_main_menu_hit_test(
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

    switch (openride_ui_main_menu_hit_test(&ui, x, y)) {
        case OPENRIDE_UI_MAIN_MENU_SEARCH:
            hit.action = OPENRIDE_MOBILE_PANEL_SEARCH;
            break;
        case OPENRIDE_UI_MAIN_MENU_FAVORITES:
            hit.action = OPENRIDE_MOBILE_PANEL_FAVORITES;
            break;
        case OPENRIDE_UI_MAIN_MENU_HISTORY:
            hit.action = OPENRIDE_MOBILE_PANEL_HISTORY;
            break;
        case OPENRIDE_UI_MAIN_MENU_REGIONS:
            hit.action = OPENRIDE_MOBILE_PANEL_REGIONS;
            break;
        case OPENRIDE_UI_MAIN_MENU_SETTINGS:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS;
            break;
        case OPENRIDE_UI_MAIN_MENU_MAP_ZOOM_TEST:
            hit.action = OPENRIDE_MOBILE_PANEL_MAP_ZOOM_TEST;
            break;
        case OPENRIDE_UI_MAIN_MENU_CLOSE:
            hit.action = OPENRIDE_MOBILE_PANEL_CLOSE;
            break;
        case OPENRIDE_UI_MAIN_MENU_NONE:
        default:
            break;
    }
    openride_ui_end(&ui);
    return hit;
}

typedef struct OpenRideMobilePanelLayout {'''
    text = replace_once(
        text, old_hit_struct, new_hit_struct, "main-menu hit-test wrapper")

    old_draw_entry = '''    int width = viewport_width;
    int height = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
    if (width <= 0 || height <= 0) return;

    uint32_t rows = 0U;'''
    new_draw_entry = '''    int width = viewport_width;
    int height = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
    if (width <= 0 || height <= 0) return;

    if (panel == OPENRIDE_APP_PANEL_MAIN) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            (void)openride_ui_main_menu_draw(&ui);
            openride_ui_end(&ui);
        }
        return;
    }

    uint32_t rows = 0U;'''
    text = replace_once(
        text, old_draw_entry, new_draw_entry, "main-menu renderer routing")

    old_event_hit = '''                        const OpenRideMobilePanelHit mobile_hit = mobile_app_panel_hit_test(
                            renderer,
                            app_panel,
                            x,
                            y,
                            width,
                            height,
                            mobile_place_count);'''
    new_event_hit = '''                        const OpenRideMobilePanelHit mobile_hit =
                            app_panel == OPENRIDE_APP_PANEL_MAIN
                                ? mobile_main_menu_hit_test(renderer,
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
        text, old_event_hit, new_event_hit, "main-menu event routing")


def main() -> int:
    cmake_original = CMAKE.read_text(encoding="utf-8")
    main_original = MAIN.read_text(encoding="utf-8")

    # No file is written until every exact replacement has succeeded.
    cmake_new = migrated_cmake(cmake_original)
    main_new = migrated_main(main_original)
    if cmake_new == cmake_original or main_new == main_original:
        raise RuntimeError("migration unexpectedly produced no change")

    CMAKE.write_text(cmake_new, encoding="utf-8")
    MAIN.write_text(main_new, encoding="utf-8")

    print("OK: UI Engine V1.2 main-menu migration applied")
    print("Changed: CMakeLists.txt, src/main.c")
    print("Next: git diff --check && git diff -- CMakeLists.txt src/main.c")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
