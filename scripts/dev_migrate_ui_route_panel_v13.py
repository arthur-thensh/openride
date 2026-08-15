#!/usr/bin/env python3
"""One-shot guarded migration for UI Engine V1.3 route panel.

All expected replacements are prepared in memory before any file is written.
The migration:
- registers ui_route_panel.c in CMake;
- exposes ui_route_panel.h to src/main.c;
- routes Android route-panel hit testing and rendering through UI Engine;
- removes the legacy source-level include of ui_toolbar.c from ui.c;
- removes the temporary standalone guard from ui_toolbar.c.

It never builds, tests, commits, or pushes anything.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
CMAKE = ROOT / "CMakeLists.txt"
MAIN = ROOT / "src" / "main.c"
UI = ROOT / "src" / "ui" / "ui.c"
TOOLBAR = ROOT / "src" / "ui" / "ui_toolbar.c"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def prepare_cmake(text: str) -> str:
    return replace_once(
        text,
        "    src/ui/ui.c\n    src/ui/ui_toolbar.c\n    src/ui/ui_main_menu.c\n)",
        "    src/ui/ui.c\n    src/ui/ui_toolbar.c\n    src/ui/ui_main_menu.c\n    src/ui/ui_route_panel.c\n)",
        "CMake route panel source",
    )


def prepare_ui(text: str) -> str:
    return replace_once(
        text,
        '\n#include "ui_toolbar.c"',
        "",
        "legacy ui toolbar source include",
    )


def prepare_toolbar(text: str) -> str:
    text = replace_once(
        text,
        '''#ifndef OPENRIDE_UI_H
#define OPENRIDE_UI_TOOLBAR_STANDALONE 1
#endif

#include "openride/ui_toolbar.h"

#ifdef OPENRIDE_UI_TOOLBAR_STANDALONE

#include <stddef.h>
''',
        '''#include "openride/ui_toolbar.h"

#include <stddef.h>
''',
        "toolbar temporary standalone prologue",
    )
    return replace_once(
        text,
        '\n#endif /* OPENRIDE_UI_TOOLBAR_STANDALONE */\n',
        '\n',
        "toolbar temporary standalone epilogue",
    )


def prepare_main(text: str) -> str:
    text = replace_once(
        text,
        '#include "openride/ui_main_menu.h"\n#include "openride/drive_mode.h"',
        '#include "openride/ui_main_menu.h"\n#include "openride/ui_route_panel.h"\n#include "openride/drive_mode.h"',
        "route panel include",
    )

    old_wrapper_tail = '''    openride_ui_end(&ui);
    return hit;
}

typedef struct OpenRideMobilePanelLayout {'''
    new_wrapper_tail = '''    openride_ui_end(&ui);
    return hit;
}

static OpenRideMobilePanelHit mobile_route_panel_hit_test(
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

    switch (openride_ui_route_panel_hit_test(&ui, x, y)) {
        case OPENRIDE_UI_ROUTE_PANEL_GPS_START:
            hit.action = OPENRIDE_MOBILE_PANEL_ROUTE_GPS_START;
            break;
        case OPENRIDE_UI_ROUTE_PANEL_SEARCH_START:
            hit.action = OPENRIDE_MOBILE_PANEL_ROUTE_SEARCH_START;
            break;
        case OPENRIDE_UI_ROUTE_PANEL_MAP_START:
            hit.action = OPENRIDE_MOBILE_PANEL_ROUTE_MAP_START;
            break;
        case OPENRIDE_UI_ROUTE_PANEL_SEARCH_DESTINATION:
            hit.action = OPENRIDE_MOBILE_PANEL_ROUTE_SEARCH_DESTINATION;
            break;
        case OPENRIDE_UI_ROUTE_PANEL_MAP_DESTINATION:
            hit.action = OPENRIDE_MOBILE_PANEL_ROUTE_MAP_DESTINATION;
            break;
        case OPENRIDE_UI_ROUTE_PANEL_CALCULATE:
            hit.action = OPENRIDE_MOBILE_PANEL_ROUTE_CALCULATE;
            break;
        case OPENRIDE_UI_ROUTE_PANEL_BACK:
            hit.action = OPENRIDE_MOBILE_PANEL_BACK;
            break;
        case OPENRIDE_UI_ROUTE_PANEL_NONE:
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
        "route panel hit-test wrapper",
    )

    old_draw = '''    if (panel == OPENRIDE_APP_PANEL_MAIN) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            (void)openride_ui_main_menu_draw(&ui);
            openride_ui_end(&ui);
        }
        return;
    }

    uint32_t rows = 0U;'''
    new_draw = '''    if (panel == OPENRIDE_APP_PANEL_MAIN) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            (void)openride_ui_main_menu_draw(&ui);
            openride_ui_end(&ui);
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_ROUTE) {
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
    text = replace_once(
        text,
        old_draw,
        new_draw,
        "route panel renderer routing",
    )

    old_event = '''                        const OpenRideMobilePanelHit mobile_hit =
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
        "route panel event routing",
    )


def main() -> int:
    originals = {
        CMAKE: CMAKE.read_text(encoding="utf-8"),
        MAIN: MAIN.read_text(encoding="utf-8"),
        UI: UI.read_text(encoding="utf-8"),
        TOOLBAR: TOOLBAR.read_text(encoding="utf-8"),
    }

    prepared = {
        CMAKE: prepare_cmake(originals[CMAKE]),
        MAIN: prepare_main(originals[MAIN]),
        UI: prepare_ui(originals[UI]),
        TOOLBAR: prepare_toolbar(originals[TOOLBAR]),
    }

    for path, text in prepared.items():
        if text == originals[path]:
            raise RuntimeError(f"{path}: migration produced no change")

    for path, text in prepared.items():
        path.write_text(text, encoding="utf-8")

    print("OK: UI Engine V1.3 route-panel migration applied")
    print("Changed: CMakeLists.txt, src/main.c, src/ui/ui.c, src/ui/ui_toolbar.c")
    print("Next: git diff --check && git diff -- CMakeLists.txt src/main.c src/ui/ui.c src/ui/ui_toolbar.c")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
