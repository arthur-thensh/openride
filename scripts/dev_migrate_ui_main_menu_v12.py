#!/usr/bin/env python3
"""One-shot source migration for UI Engine V1.2 main menu.

The migration is intentionally narrow and guarded. It:
- registers the UI toolbar and main-menu sources in CMake;
- exposes ui_main_menu.h to src/main.c;
- routes Android main-menu hit-testing through UI Engine;
- routes Android main-menu drawing through UI Engine.

Legacy main-menu branches remain in src/main.c for this validation pass, but
become unreachable for the Android main panel. They can be deleted after the
new component has been validated on-device.

This script never builds, tests, commits, or pushes anything.
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


def migrate_cmake() -> bool:
    text = CMAKE.read_text(encoding="utf-8")
    original = text
    text = replace_once(
        text,
        "    src/map/map_world.c\n    src/ui/ui.c\n)",
        "    src/map/map_world.c\n    src/ui/ui.c\n    src/ui/ui_toolbar.c\n    src/ui/ui_main_menu.c\n)",
        "CMake UI sources",
    )
    if text != original:
        CMAKE.write_text(text, encoding="utf-8")
        return True
    return False


def migrate_main() -> bool:
    text = MAIN.read_text(encoding="utf-8")
    original = text

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
    text = replace_once(text,
                        old_hit_struct,
                        new_hit_struct,
                        "main-menu hit-test wrapper")

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
    text = replace_once(text,
                        old_draw_entry,
                        new_draw_entry,
                        "main-menu renderer routing")

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
    text = replace_once(text,
                        old_event_hit,
                        new_event_hit,
                        "main-menu event routing")

    if text != original:
        MAIN.write_text(text, encoding="utf-8")
        return True
    return False


def main() -> int:
    # Evaluate both files from their current content before reporting success.
    # Each individual replacement is exact and refuses ambiguous source states.
    cmake_changed = migrate_cmake()
    main_changed = migrate_main()
    if not cmake_changed or not main_changed:
        raise RuntimeError(
            "migration did not change both expected files; restore working tree before retrying"
        )

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
