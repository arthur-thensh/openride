#!/usr/bin/env python3
"""Final guarded launcher logic for the one-shot Ride Planner V3.1 migration."""

from __future__ import annotations

from pathlib import Path
import importlib.util
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "scripts" / "dev_apply_ride_planner_v311_async.py"

spec = importlib.util.spec_from_file_location("openride_v311_async", PATH)
if not spec or not spec.loader:
    print("ERROR: unable to load async Ride Planner migrator", file=sys.stderr)
    raise SystemExit(1)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

base_patch_route_ui = module.patch_route_ui


def patch_route_ui_final(text: str) -> str:
    changed = base_patch_route_ui(text)
    # The start rows are covered by the base patch. Destination rows are split
    # over more lines, so normalize every remaining icon-row `true` argument.
    pattern = re.compile(
        r"(draw_icon_row\([^;]*?),\s*true,"
        r"(\s*OPENRIDE_UI_ROUTE_PANEL_[A-Z_]+\s*,\s*&clicked\s*\);)",
        re.DOTALL,
    )
    changed, count = pattern.subn(r"\1, interactive,\2", changed)
    if count > 2:
        raise RuntimeError(
            "planner UI final guard: unexpectedly replaced more than two icon rows"
        )
    remaining = re.findall(
        r"draw_icon_row\([^;]*?,\s*true,\s*OPENRIDE_UI_ROUTE_PANEL_",
        changed,
        flags=re.DOTALL,
    )
    if remaining:
        raise RuntimeError("planner UI final guard: enabled icon row remains while busy")
    return changed


module.patch_route_ui = patch_route_ui_final

if __name__ == "__main__":
    try:
        raise SystemExit(module.main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
