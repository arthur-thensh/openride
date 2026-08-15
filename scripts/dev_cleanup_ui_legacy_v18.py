#!/usr/bin/env python3
"""One-shot guarded cleanup for UI Engine V1.8.

All Android app panels now have dedicated UI Engine components. This cleanup
removes the unreachable generic mobile layout/render/hit-test implementation
left in main.c during the incremental V1.1-V1.7 migration.

The script prepares every edit in memory and writes src/main.c only after all
expected markers have been validated. It never builds, tests, commits, or pushes.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "src" / "main.c"


def remove_between(text: str,
                   start_marker: str,
                   end_marker: str,
                   label: str,
                   keep_end: bool = True) -> str:
    start_count = text.count(start_marker)
    end_count = text.count(end_marker)
    if start_count != 1:
        raise RuntimeError(
            f"{label}: expected exactly one start marker, found {start_count}"
        )
    if end_count != 1:
        raise RuntimeError(
            f"{label}: expected exactly one end marker, found {end_count}"
        )
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    if end <= start:
        raise RuntimeError(f"{label}: invalid marker order")
    suffix = text[end:] if keep_end else text[end + len(end_marker):]
    return text[:start] + suffix


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def prepare_main(text: str) -> str:
    # All dedicated panel wrappers are above this point. Everything from the
    # old generic layout type through mobile_app_panel_hit_test is now dead.
    text = remove_between(
        text,
        "typedef struct OpenRideMobilePanelLayout {",
        "static void draw_mobile_app_panel(",
        "V1.8 generic mobile panel helpers",
    )

    # Every valid OpenRideAppPanel is explicitly routed to a dedicated wrapper.
    # Keep a defensive NONE fallback rather than the removed legacy dispatcher.
    old_fallback = '''                                : mobile_app_panel_hit_test(renderer,
                                                            app_panel,
                                                            x,
                                                            y,
                                                            width,
                                                            height,
                                                            mobile_place_count);'''
    new_fallback = '''                                : (OpenRideMobilePanelHit){
                                      OPENRIDE_MOBILE_PANEL_NONE,
                                      -1
                                  };'''
    text = replace_once(
        text,
        old_fallback,
        new_fallback,
        "V1.8 mobile event fallback",
    )

    # draw_mobile_app_panel already returns from a dedicated UI component for
    # MAIN, ROUTE, ROUTE_DOWNLOADS, SETTINGS, REGIONS, FAVORITES and HISTORY.
    # The remaining generic renderer is therefore unreachable for every panel.
    legacy_draw_start = "    uint32_t rows = 0U;\n"
    legacy_draw_end = "}\n#endif\n\nstatic int app_panel_region_action_at("
    start_count = text.count(legacy_draw_start)
    if start_count != 1:
        raise RuntimeError(
            "V1.8 legacy mobile renderer: expected exactly one start marker, "
            f"found {start_count}"
        )
    end_count = text.count(legacy_draw_end)
    if end_count != 1:
        raise RuntimeError(
            "V1.8 legacy mobile renderer: expected exactly one end marker, "
            f"found {end_count}"
        )
    start = text.index(legacy_draw_start)
    end = text.index(legacy_draw_end, start)
    text = text[:start] + text[end:]

    # Guard against a partial cleanup. These names should disappear entirely.
    forbidden = (
        "OpenRideMobilePanelLayout",
        "mobile_point_in_rect(",
        "mobile_panel_layout(",
        "mobile_panel_region_buttons(",
        "mobile_draw_button(",
        "mobile_draw_panel_title(",
        "mobile_app_panel_hit_test(",
    )
    leftovers = [name for name in forbidden if name in text]
    if leftovers:
        raise RuntimeError(
            "V1.8 legacy cleanup left unexpected symbols: " + ", ".join(leftovers)
        )

    return text


def main() -> int:
    original = MAIN.read_text(encoding="utf-8")
    prepared = prepare_main(original)
    if prepared == original:
        raise RuntimeError("src/main.c: cleanup produced no change")

    removed_lines = original.count("\n") - prepared.count("\n")
    if removed_lines < 200:
        raise RuntimeError(
            f"V1.8 safety check: expected substantial dead-code removal, got {removed_lines} lines"
        )
    if removed_lines > 1200:
        raise RuntimeError(
            f"V1.8 safety check: refusing unexpectedly large removal ({removed_lines} lines)"
        )

    MAIN.write_text(prepared, encoding="utf-8")
    print("OK: UI Engine V1.8 legacy cleanup applied")
    print(f"Removed dead legacy UI lines: {removed_lines}")
    print("Changed: src/main.c")
    print("Next: git diff --check && git diff --stat && git diff -- src/main.c")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
