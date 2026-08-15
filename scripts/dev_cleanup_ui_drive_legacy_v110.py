#!/usr/bin/env python3
"""One-shot guarded cleanup for UI Engine V1.10 drive HUD.

Removes only the legacy drive HUD drawing/layout helpers that became unused
after V1.9 routed drive-mode rendering and hit testing through ui_drive_hud.

The script does not build, test, commit, or push anything.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "src" / "main.c"


def remove_block(text: str,
                 start_marker: str,
                 end_marker: str,
                 label: str) -> str:
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

    start = text.find(start_marker)
    end = text.find(end_marker, start + len(start_marker))
    if end < 0 or end <= start:
        raise RuntimeError(f"{label}: invalid marker order")

    return text[:start] + text[end:]


def prepare_main(text: str) -> str:
    required_live_symbols = (
        "openride_ui_drive_hud_hit_test(&ui, x, y)",
        "openride_ui_drive_hud_draw(&ui, &state)",
        "static OpenRideDriveAction drive_controls_hit_test(",
        "static void format_arrival_clock(",
        "static OpenRideUIDriveHUDManeuver drive_hud_maneuver(",
        "static OpenRideUIDriveHUDGPSQuality drive_hud_gps_quality(",
    )
    for symbol in required_live_symbols:
        if symbol not in text:
            raise RuntimeError(f"V1.10 precondition missing: {symbol}")

    text = remove_block(
        text,
        "static SDL_FRect drive_controls_bounds(",
        "static OpenRideDriveAction drive_controls_hit_test(",
        "V1.10 legacy drive controls layout",
    )

    text = remove_block(
        text,
        "static void draw_drive_controls(",
        "static void draw_drive_mode_ui(",
        "V1.10 legacy drive HUD drawing helpers",
    )

    forbidden = (
        "static SDL_FRect drive_controls_bounds(",
        "static void draw_drive_controls(",
        "static void draw_drive_maneuver_icon(",
    )
    for symbol in forbidden:
        if symbol in text:
            raise RuntimeError(f"V1.10 cleanup incomplete: {symbol}")

    required_after = (
        "static OpenRideDriveAction drive_controls_hit_test(",
        "openride_ui_drive_hud_hit_test(&ui, x, y)",
        "static void format_arrival_clock(",
        "static void draw_drive_mode_ui(",
        "openride_ui_drive_hud_draw(&ui, &state)",
    )
    for symbol in required_after:
        if symbol not in text:
            raise RuntimeError(f"V1.10 cleanup removed live code: {symbol}")

    return text


def main() -> int:
    original = MAIN.read_text(encoding="utf-8")
    prepared = prepare_main(original)

    if prepared == original:
        raise RuntimeError("src/main.c: cleanup produced no change")

    removed = len(original) - len(prepared)
    if removed < 2500 or removed > 9000:
        raise RuntimeError(
            f"src/main.c: unexpected V1.10 size delta ({removed} bytes removed)"
        )

    MAIN.write_text(prepared, encoding="utf-8")

    print("OK: UI Engine V1.10 drive HUD legacy cleanup applied")
    print("Changed: src/main.c")
    print("Removed: drive_controls_bounds, draw_drive_controls, draw_drive_maneuver_icon")
    print("Kept: action mapping, navigation snapshot, arrival formatting, ui_drive_hud calls")
    print("Next: git diff --check && git diff -- src/main.c")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
