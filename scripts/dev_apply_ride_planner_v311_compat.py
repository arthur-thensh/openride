#!/usr/bin/env python3
"""Compatibility entry point for the final Ride Planner V3.1 async migration.

Accepts either the clean V3.0.5 orchestration files or a local tree where the
original V3.1 Ride Planner migrator was already applied. In both cases the
async/loading layer is applied transactionally exactly once.
"""

from __future__ import annotations

from pathlib import Path
import importlib.util
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
ASYNC_PATH = ROOT / "scripts" / "dev_apply_ride_planner_v311_async.py"

spec = importlib.util.spec_from_file_location("openride_v311_async_compat", ASYNC_PATH)
if not spec or not spec.loader:
    print("ERROR: unable to load final Ride Planner migrator", file=sys.stderr)
    raise SystemExit(1)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

base = module.load_base_module()


def fail(message: str) -> None:
    raise RuntimeError(message)


def patch_route_ui_final(text: str) -> str:
    changed = module.patch_route_ui(text)
    pattern = re.compile(
        r"(draw_icon_row\([^;]*?),\s*true,"
        r"(\s*OPENRIDE_UI_ROUTE_PANEL_[A-Z_]+\s*,\s*&clicked\s*\);)",
        re.DOTALL,
    )
    changed, count = pattern.subn(r"\1, interactive,\2", changed)
    if count > 2:
        fail("planner UI: unexpectedly changed more than two remaining icon rows")
    if re.search(
        r"draw_icon_row\([^;]*?,\s*true,\s*OPENRIDE_UI_ROUTE_PANEL_",
        changed,
        flags=re.DOTALL,
    ):
        fail("planner UI: enabled icon row remains while busy")
    return changed


def base_already_applied(original: dict[str, str]) -> bool:
    markers = {
        "cmake": "src/ui/ui_loop_proposals_panel.c",
        "loop": "bool openride_loop_generator_generate_proposals(",
        "route_runtime": "bool openride_app_route_generate_loop_proposals(",
        "event": "OPENRIDE_APP_UI_LOOP_PROPOSAL_SELECT",
        "runtime": "OpenRideLoopProposalSet loop_proposals = {0};",
        "bridge": "openride_ui_loop_proposals_draw",
    }
    present = {key: token in original[key] for key, token in markers.items()}
    if all(present.values()):
        return True
    if any(present.values()):
        details = ", ".join(f"{key}={'yes' if value else 'no'}"
                            for key, value in present.items())
        fail(f"partial V3.1 base migration detected ({details})")
    return False


def normalize_proposal_move(text: str) -> str:
    buggy = '''    memset(proposal, 0, sizeof(*proposal));\n    proposal->route = candidate->route;\n    memset(&candidate->route, 0, sizeof(candidate->route));\n    memcpy(proposal->waypoints, candidate->waypoints, sizeof(proposal->waypoints));\n    proposal->waypoint_count = OPENRIDE_LOOP_MAX_WAYPOINTS;\n    proposal->source_candidate_index = source_index;\n    proposal_fill_stats(&proposal->stats, candidate);\n'''
    fixed = '''    memset(proposal, 0, sizeof(*proposal));\n    proposal_fill_stats(&proposal->stats, candidate);\n    proposal->route = candidate->route;\n    memset(&candidate->route, 0, sizeof(candidate->route));\n    memcpy(proposal->waypoints, candidate->waypoints, sizeof(proposal->waypoints));\n    proposal->waypoint_count = OPENRIDE_LOOP_MAX_WAYPOINTS;\n    proposal->source_candidate_index = source_index;\n'''
    if buggy in text:
        return text.replace(buggy, fixed, 1)
    if fixed not in text:
        fail("loop proposal ownership pattern is neither known fixed nor known buggy form")
    return text


def main() -> int:
    files = dict(base.FILES)
    files.update({
        "async": ROOT / "src" / "app_async_runtime.c",
        "icon": ROOT / "src" / "ui" / "ui_icon.c",
        "route_ui": ROOT / "src" / "ui" / "ui_route_panel.c",
    })
    for path in files.values():
        if not path.exists():
            fail(f"missing required file: {path.relative_to(ROOT)}")

    required_small = [
        ROOT / "src/app_planner_async_runtime.h",
        ROOT / "src/app_planner_async_runtime.c",
        ROOT / "include/openride/ride_planner.h",
        ROOT / "include/openride/ui_icon.h",
        ROOT / "include/openride/ui_route_panel.h",
        ROOT / "include/openride/ui_loop_proposals_panel.h",
        ROOT / "src/ui/ui_loop_proposals_panel.c",
    ]
    for path in required_small:
        if not path.exists():
            fail(f"missing async Ride Planner component: {path.relative_to(ROOT)}")

    original = {key: path.read_text(encoding="utf-8") for key, path in files.items()}
    already_base = base_already_applied(original)

    if already_base:
        changed = dict(original)
        changed["loop"] = normalize_proposal_move(changed["loop"])
    else:
        changed = {
            "cmake": base.patch_cmake(original["cmake"]),
            "loop": module.fix_base_proposal_move(base.patch_loop_generator(original["loop"])),
            "route_runtime": base.patch_route_runtime(original["route_runtime"]),
            "event": base.patch_event(original["event"]),
            "runtime": base.patch_runtime(original["runtime"]),
            "bridge": base.patch_bridge(original["bridge"]),
            "async": original["async"],
            "icon": original["icon"],
            "route_ui": original["route_ui"],
        }

    async_markers = [
        "src/app_planner_async_runtime.c" in changed["cmake"],
        "openride_app_planner_async_start_route" in changed["event"],
        "OpenRidePlannerAsyncContext planner_async_context" in changed["runtime"],
        "openride_app_planner_async_loop_request_matches" in changed["async"],
        "openride_ui_icon_draw_rotated" in changed["icon"],
        "Recherche de balades..." in changed["route_ui"],
    ]
    if any(async_markers):
        if all(async_markers):
            fail("async Ride Planner migration is already fully applied")
        fail("partial async Ride Planner migration detected; no files were written")

    changed["cmake"] = module.patch_cmake(changed["cmake"])
    changed["event"] = module.patch_event(changed["event"])
    changed["runtime"] = module.patch_runtime(changed["runtime"])
    changed["bridge"] = module.patch_bridge(changed["bridge"])
    changed["async"] = module.patch_async_runtime(changed["async"])
    changed["icon"] = module.patch_icon(changed["icon"])
    changed["route_ui"] = patch_route_ui_final(changed["route_ui"])

    required_tokens = {
        "cmake": ["app_planner_async_runtime.c", "ui_loop_proposals_panel.c"],
        "loop": ["openride_loop_generator_generate_proposals", "proposal_fill_stats"],
        "route_runtime": ["openride_app_route_generate_loop_proposals"],
        "event": ["openride_app_planner_async_start_route", "openride_app_planner_async_start_loops"],
        "runtime": ["OpenRidePlannerAsyncContext planner_async_context", "planner_async_thread"],
        "bridge": [".busy = planner_busy", "openride_ui_loop_proposals_draw"],
        "async": ["planner_async_thread", "openride_app_planner_async_loop_request_matches"],
        "icon": ["OPENRIDE_UI_ICON_LOADING", "openride_ui_icon_draw_rotated"],
        "route_ui": ["Recherche de balades...", "Calcul de l’itinéraire...", "interactive"],
    }
    for key, tokens in required_tokens.items():
        for token in tokens:
            if token not in changed[key]:
                fail(f"{key}: generated output missing {token}")

    changed_keys = [key for key in files if changed[key] != original[key]]
    expected = {"cmake", "loop", "route_runtime", "event", "runtime",
                "bridge", "async", "icon", "route_ui"}
    if not expected.issubset(set(changed_keys)):
        missing = sorted(expected.difference(changed_keys))
        # When the base was already applied, loop/route_runtime may only need no
        # further edits besides an already-correct ownership fix.
        allowed = {"loop", "route_runtime"} if already_base else set()
        unexpected_missing = [key for key in missing if key not in allowed]
        if unexpected_missing:
            fail("migration produced no change for: " + ", ".join(unexpected_missing))

    # Transactional write point.
    for key, path in files.items():
        if changed[key] != original[key]:
            path.write_text(changed[key], encoding="utf-8")

    print("OK: OpenRide Ride Planner V3.1 final async migration applied")
    print("Base Ride Planner: " + ("already present" if already_base else "applied now"))
    print("Loading: animated scalable SVG spinner in the planner primary action")
    print("Route: local calculation off the UI thread; inter-region fallback stays async")
    print("Loops: proposal generation off the UI thread; controls locked while running")
    print("UI: remains responsive until results or an error are available")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
