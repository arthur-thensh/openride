#!/usr/bin/env python3
"""One-shot guarded migration for Architecture V2.0: app UI actions.

Moves panel-action definitions and UI Engine hit-test adapters out of main.c
into app_ui_action.{h,c}. Business handling of those actions deliberately stays
in main.c for this step.

This script does not build, test, commit, or push anything.
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


def replace_block(text: str,
                  start_marker: str,
                  end_marker: str,
                  replacement: str,
                  label: str) -> str:
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
    return text[:start] + replacement + text[end:]


def prepare_cmake(text: str) -> str:
    if "    src/app_ui_action.c\n" in text:
        return text
    return replace_once(
        text,
        "set(OPENRIDE_APP_SOURCES\n"
        "    src/main.c\n",
        "set(OPENRIDE_APP_SOURCES\n"
        "    src/main.c\n"
        "    src/app_ui_action.c\n",
        "CMake V2.0 app UI action source",
    )


def prepare_main(text: str) -> str:
    if '#include "openride/app_ui_action.h"' not in text:
        text = replace_once(
            text,
            '#include "openride/app_toolbar.h"\n'
            '#include "openride/ui_toolbar.h"',
            '#include "openride/app_toolbar.h"\n'
            '#include "openride/app_ui_action.h"\n'
            '#include "openride/ui_toolbar.h"',
            "V2.0 app UI action include",
        )

    text = replace_block(
        text,
        "#ifdef __ANDROID__\ntypedef enum OpenRideMobilePanelAction",
        "static void draw_ui_app_panel(",
        "",
        "V2.0 remove local panel action adapters",
    )

    replacements = {
        "OpenRideMobilePanelHit": "OpenRideAppUIAction",
        "mobile_main_menu_hit_test": "openride_app_ui_main_menu_hit_test",
        "mobile_route_panel_hit_test": "openride_app_ui_route_panel_hit_test",
        "mobile_route_downloads_panel_hit_test":
            "openride_app_ui_route_downloads_panel_hit_test",
        "mobile_settings_panel_hit_test": "openride_app_ui_settings_panel_hit_test",
        "mobile_regions_panel_hit_test": "openride_app_ui_regions_panel_hit_test",
        "mobile_places_panel_hit_test": "openride_app_ui_places_panel_hit_test",
        "OPENRIDE_MOBILE_PANEL_NONE": "OPENRIDE_APP_UI_NONE",
        "OPENRIDE_MOBILE_PANEL_CLOSE": "OPENRIDE_APP_UI_CLOSE",
        "OPENRIDE_MOBILE_PANEL_BACK": "OPENRIDE_APP_UI_BACK",
        "OPENRIDE_MOBILE_PANEL_SEARCH": "OPENRIDE_APP_UI_SEARCH",
        "OPENRIDE_MOBILE_PANEL_ROUTE_GPS_START": "OPENRIDE_APP_UI_ROUTE_GPS_START",
        "OPENRIDE_MOBILE_PANEL_ROUTE_SEARCH_START": "OPENRIDE_APP_UI_ROUTE_SEARCH_START",
        "OPENRIDE_MOBILE_PANEL_ROUTE_MAP_START": "OPENRIDE_APP_UI_ROUTE_MAP_START",
        "OPENRIDE_MOBILE_PANEL_ROUTE_SEARCH_DESTINATION":
            "OPENRIDE_APP_UI_ROUTE_SEARCH_DESTINATION",
        "OPENRIDE_MOBILE_PANEL_ROUTE_MAP_DESTINATION":
            "OPENRIDE_APP_UI_ROUTE_MAP_DESTINATION",
        "OPENRIDE_MOBILE_PANEL_ROUTE_CALCULATE": "OPENRIDE_APP_UI_ROUTE_CALCULATE",
        "OPENRIDE_MOBILE_PANEL_ROUTE_DOWNLOAD_REQUIRED":
            "OPENRIDE_APP_UI_ROUTE_DOWNLOAD_REQUIRED",
        "OPENRIDE_MOBILE_PANEL_ROUTE_USE_INSTALLED":
            "OPENRIDE_APP_UI_ROUTE_USE_INSTALLED",
        "OPENRIDE_MOBILE_PANEL_FAVORITES": "OPENRIDE_APP_UI_FAVORITES",
        "OPENRIDE_MOBILE_PANEL_HISTORY": "OPENRIDE_APP_UI_HISTORY",
        "OPENRIDE_MOBILE_PANEL_REGIONS": "OPENRIDE_APP_UI_REGIONS",
        "OPENRIDE_MOBILE_PANEL_SETTINGS": "OPENRIDE_APP_UI_SETTINGS",
        "OPENRIDE_MOBILE_PANEL_PLACE": "OPENRIDE_APP_UI_PLACE",
        "OPENRIDE_MOBILE_PANEL_REGION_PREVIOUS": "OPENRIDE_APP_UI_REGION_PREVIOUS",
        "OPENRIDE_MOBILE_PANEL_REGION_NEXT": "OPENRIDE_APP_UI_REGION_NEXT",
        "OPENRIDE_MOBILE_PANEL_REGION_INSTALL": "OPENRIDE_APP_UI_REGION_INSTALL",
        "OPENRIDE_MOBILE_PANEL_REGION_REMOVE": "OPENRIDE_APP_UI_REGION_REMOVE",
        "OPENRIDE_MOBILE_PANEL_SETTINGS_STYLE": "OPENRIDE_APP_UI_SETTINGS_STYLE",
        "OPENRIDE_MOBILE_PANEL_SETTINGS_PROFILE": "OPENRIDE_APP_UI_SETTINGS_PROFILE",
        "OPENRIDE_MOBILE_PANEL_SETTINGS_FOLLOW": "OPENRIDE_APP_UI_SETTINGS_FOLLOW",
        "OPENRIDE_MOBILE_PANEL_SETTINGS_REROUTE": "OPENRIDE_APP_UI_SETTINGS_REROUTE",
        "OPENRIDE_MOBILE_PANEL_SETTINGS_VOICE": "OPENRIDE_APP_UI_SETTINGS_VOICE",
        "OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_SIMULATION":
            "OPENRIDE_APP_UI_SETTINGS_GPS_SIMULATION",
        "OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_DEVIATION":
            "OPENRIDE_APP_UI_SETTINGS_GPS_DEVIATION",
        "OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_SPEED":
            "OPENRIDE_APP_UI_SETTINGS_GPS_SPEED",
        "OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_MISSED_TURN":
            "OPENRIDE_APP_UI_SETTINGS_GPS_MISSED_TURN",
        "OPENRIDE_MOBILE_PANEL_MAP_ZOOM_TEST": "OPENRIDE_APP_UI_MAP_ZOOM_TEST",
    }

    for old, new in replacements.items():
        text = text.replace(old, new)

    forbidden = (
        "OpenRideMobilePanelAction",
        "OpenRideMobilePanelHit",
        "OPENRIDE_MOBILE_PANEL_",
        "mobile_main_menu_hit_test(",
        "mobile_route_panel_hit_test(",
        "mobile_route_downloads_panel_hit_test(",
        "mobile_settings_panel_hit_test(",
        "mobile_regions_panel_hit_test(",
        "mobile_places_panel_hit_test(",
    )
    for token in forbidden:
        if token in text:
            raise RuntimeError(f"V2.0: legacy token remains: {token}")

    if text.count("static void draw_ui_app_panel(") != 1:
        raise RuntimeError("V2.0: draw_ui_app_panel declaration count changed")
    if text.count("openride_app_ui_main_menu_hit_test(") != 1:
        raise RuntimeError("V2.0: expected one app main-menu hit-test call")
    if text.count("openride_app_ui_route_panel_hit_test(") != 1:
        raise RuntimeError("V2.0: expected one app route-panel hit-test call")
    if text.count("openride_app_ui_places_panel_hit_test(") != 1:
        raise RuntimeError("V2.0: expected one app places-panel hit-test call")

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

    removed = len(originals[MAIN]) - len(prepared[MAIN])
    if removed < 7000 or removed > 22000:
        raise RuntimeError(
            f"src/main.c: unexpected V2.0 size delta ({removed} bytes removed net)"
        )

    for path, content in prepared.items():
        path.write_text(content, encoding="utf-8")

    print("OK: Architecture V2.0 app UI action migration applied")
    print("Changed: CMakeLists.txt, src/main.c")
    print("Panel action types + UI hit-test adapters now live outside main.c")
    print("Business action handling intentionally remains in main.c")
    print("Next: git diff --check && git diff --stat && git diff -- CMakeLists.txt src/main.c")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
