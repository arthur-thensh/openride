#!/usr/bin/env python3
"""One-shot guarded migration for UI Engine V1.5 saved places panels.

Moves Android Favorites and History rendering/hit-testing to the reusable
ui_places_panel component. Application state and actions remain owned by main.c.
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
        "    src/ui/ui_settings_panel.c\n    src/ui/ui_regions_panel.c\n)",
        "    src/ui/ui_settings_panel.c\n    src/ui/ui_regions_panel.c\n    src/ui/ui_places_panel.c\n)",
        "CMake V1.5 UI source",
    )


def prepare_main(text: str) -> str:
    text = replace_once(
        text,
        '#include "openride/ui_regions_panel.h"\n#include "openride/drive_mode.h"',
        '#include "openride/ui_regions_panel.h"\n#include "openride/ui_places_panel.h"\n#include "openride/drive_mode.h"',
        "V1.5 UI include",
    )

    old_wrapper_tail = '''        case OPENRIDE_UI_REGIONS_PANEL_NONE:
        default:
            break;
    }
    openride_ui_end(&ui);
    return hit;
}

typedef struct OpenRideMobilePanelLayout {'''
    new_wrapper_tail = '''        case OPENRIDE_UI_REGIONS_PANEL_NONE:
        default:
            break;
    }
    openride_ui_end(&ui);
    return hit;
}

static OpenRideMobilePanelHit mobile_places_panel_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height,
    uint32_t item_count)
{
    OpenRideMobilePanelHit hit = {OPENRIDE_MOBILE_PANEL_NONE, -1};
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return hit;
    }

    const OpenRideUIPlacesPanelHit places_hit =
        openride_ui_places_panel_hit_test(&ui, item_count, x, y);
    if (places_hit.action == OPENRIDE_UI_PLACES_PANEL_PLACE) {
        hit.action = OPENRIDE_MOBILE_PANEL_PLACE;
        hit.index = places_hit.index;
    } else if (places_hit.action == OPENRIDE_UI_PLACES_PANEL_BACK) {
        hit.action = OPENRIDE_MOBILE_PANEL_BACK;
    }

    openride_ui_end(&ui);
    return hit;
}

typedef struct OpenRideMobilePanelLayout {'''
    text = replace_once(
        text,
        old_wrapper_tail,
        new_wrapper_tail,
        "V1.5 saved places hit-test wrapper",
    )

    old_draw_tail = '''                .work_status = region_work_status
            };
            (void)openride_ui_regions_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

    uint32_t rows = 0U;'''
    new_draw_tail = '''                .work_status = region_work_status
            };
            (void)openride_ui_regions_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_FAVORITES
        || panel == OPENRIDE_APP_PANEL_HISTORY) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            const bool favorites_panel =
                panel == OPENRIDE_APP_PANEL_FAVORITES;
            const OpenRideStoredPlace *items =
                favorites_panel ? favorites : history;
            uint32_t count = favorites_panel ? favorite_count : history_count;
            if (count > OPENRIDE_UI_PLACES_PANEL_MAX_ITEMS) {
                count = OPENRIDE_UI_PLACES_PANEL_MAX_ITEMS;
            }

            OpenRideUIPlacesPanelState state = {
                .mode = favorites_panel
                    ? OPENRIDE_UI_PLACES_PANEL_FAVORITES
                    : OPENRIDE_UI_PLACES_PANEL_HISTORY,
                .count = count,
                .selected = selected
            };
            for (uint32_t i = 0U; i < count; ++i) {
                state.items[i] = items[i].name;
            }
            (void)openride_ui_places_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

    uint32_t rows = 0U;'''
    text = replace_once(
        text,
        old_draw_tail,
        new_draw_tail,
        "V1.5 saved places renderer routing",
    )

    old_event = '''                            : app_panel == OPENRIDE_APP_PANEL_REGIONS
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
    new_event = '''                            : app_panel == OPENRIDE_APP_PANEL_REGIONS
                                ? mobile_regions_panel_hit_test(renderer,
                                                                x,
                                                                y,
                                                                width,
                                                                height)
                            : app_panel == OPENRIDE_APP_PANEL_FAVORITES
                              || app_panel == OPENRIDE_APP_PANEL_HISTORY
                                ? mobile_places_panel_hit_test(renderer,
                                                               x,
                                                               y,
                                                               width,
                                                               height,
                                                               mobile_place_count)
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
        "V1.5 saved places event routing",
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

    print("OK: UI Engine V1.5 Favorites + History migration applied")
    print("Changed: CMakeLists.txt, src/main.c")
    print("Next: git diff --check && git diff -- CMakeLists.txt src/main.c")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
