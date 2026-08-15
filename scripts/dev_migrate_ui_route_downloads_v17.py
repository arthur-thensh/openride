#!/usr/bin/env python3
"""One-shot guarded migration for UI Engine V1.7 route-downloads panel.

Moves Android route-required-maps rendering and touch hit-testing to the
ui_route_downloads_panel component. Download orchestration, routing-world state,
region activation, and retry behavior remain owned by main.c.

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


def prepare_cmake(text: str) -> str:
    return replace_once(
        text,
        "    src/ui/ui_places_panel.c\n    src/ui/ui_search_overlay.c\n)",
        "    src/ui/ui_places_panel.c\n    src/ui/ui_search_overlay.c\n    src/ui/ui_route_downloads_panel.c\n)",
        "CMake V1.7 UI source",
    )


def prepare_main(text: str) -> str:
    text = replace_once(
        text,
        '#include "openride/ui_search_overlay.h"\n#include "openride/drive_mode.h"',
        '#include "openride/ui_search_overlay.h"\n#include "openride/ui_route_downloads_panel.h"\n#include "openride/drive_mode.h"',
        "V1.7 UI include",
    )

    old_wrapper_tail = '''        case OPENRIDE_UI_ROUTE_PANEL_NONE:
        default:
            break;
    }
    openride_ui_end(&ui);
    return hit;
}

static OpenRideMobilePanelHit mobile_settings_panel_hit_test('''
    new_wrapper_tail = '''        case OPENRIDE_UI_ROUTE_PANEL_NONE:
        default:
            break;
    }
    openride_ui_end(&ui);
    return hit;
}

static OpenRideMobilePanelHit mobile_route_downloads_panel_hit_test(
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

    switch (openride_ui_route_downloads_panel_hit_test(&ui, x, y)) {
        case OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_DOWNLOAD:
            hit.action = OPENRIDE_MOBILE_PANEL_ROUTE_DOWNLOAD_REQUIRED;
            break;
        case OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_USE_INSTALLED:
            hit.action = OPENRIDE_MOBILE_PANEL_ROUTE_USE_INSTALLED;
            break;
        case OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_BACK:
            hit.action = OPENRIDE_MOBILE_PANEL_BACK;
            break;
        case OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_NONE:
        default:
            break;
    }
    openride_ui_end(&ui);
    return hit;
}

static OpenRideMobilePanelHit mobile_settings_panel_hit_test('''
    text = replace_once(
        text,
        old_wrapper_tail,
        new_wrapper_tail,
        "V1.7 route-downloads hit-test wrapper",
    )

    old_draw_tail = '''            (void)openride_ui_route_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_SETTINGS) {'''
    new_draw_tail = '''            (void)openride_ui_route_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            OpenRideUIRouteDownloadsPanelState state = {
                .downloading = route_download_plan.downloading,
                .has_installed_alternative =
                    route_download_plan.has_installed_alternative,
                .count = route_download_plan.count,
                .current_index = route_download_plan.index,
                .progress = region_progress,
                .work_status = region_work_status
            };
            uint32_t count = route_download_plan.count;
            if (count > OPENRIDE_UI_ROUTE_DOWNLOADS_MAX_REGIONS) {
                count = OPENRIDE_UI_ROUTE_DOWNLOADS_MAX_REGIONS;
            }
            state.count = count;
            for (uint32_t i = 0U; i < count; ++i) {
                const OpenRideRegionDefinition *required =
                    openride_region_find(route_download_plan.region_ids[i]);
                state.region_names[i] = required
                    ? required->name
                    : route_download_plan.region_ids[i];
            }
            (void)openride_ui_route_downloads_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_SETTINGS) {'''
    text = replace_once(
        text,
        old_draw_tail,
        new_draw_tail,
        "V1.7 route-downloads renderer routing",
    )

    old_event = '''                            : app_panel == OPENRIDE_APP_PANEL_ROUTE
                                ? mobile_route_panel_hit_test(renderer,
                                                              x,
                                                              y,
                                                              width,
                                                              height)
                            : app_panel == OPENRIDE_APP_PANEL_SETTINGS'''
    new_event = '''                            : app_panel == OPENRIDE_APP_PANEL_ROUTE
                                ? mobile_route_panel_hit_test(renderer,
                                                              x,
                                                              y,
                                                              width,
                                                              height)
                            : app_panel == OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS
                                ? mobile_route_downloads_panel_hit_test(renderer,
                                                                        x,
                                                                        y,
                                                                        width,
                                                                        height)
                            : app_panel == OPENRIDE_APP_PANEL_SETTINGS'''
    return replace_once(
        text,
        old_event,
        new_event,
        "V1.7 route-downloads event routing",
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

    for path, content in prepared.items():
        if content == originals[path]:
            raise RuntimeError(f"{path}: migration produced no change")

    for path, content in prepared.items():
        path.write_text(content, encoding="utf-8")

    print("OK: UI Engine V1.7 route-downloads migration applied")
    print("Changed: CMakeLists.txt, src/main.c")
    print("Next: git diff --check && git diff -- CMakeLists.txt src/main.c")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
