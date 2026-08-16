#!/usr/bin/env python3
"""Run the Ride Planner V3.1 migrator with final preflight fixes in memory."""

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
path = root / "scripts" / "dev_apply_ride_planner_v310.py"
source = path.read_text(encoding="utf-8")

old = '''    memset(proposal, 0, sizeof(*proposal));\n    proposal->route = candidate->route;\n    memset(&candidate->route, 0, sizeof(candidate->route));\n    memcpy(proposal->waypoints, candidate->waypoints, sizeof(proposal->waypoints));\n    proposal->waypoint_count = OPENRIDE_LOOP_MAX_WAYPOINTS;\n    proposal->source_candidate_index = source_index;\n    proposal_fill_stats(&proposal->stats, candidate);\n'''
new = '''    memset(proposal, 0, sizeof(*proposal));\n    proposal_fill_stats(&proposal->stats, candidate);\n    proposal->route = candidate->route;\n    memset(&candidate->route, 0, sizeof(candidate->route));\n    memcpy(proposal->waypoints, candidate->waypoints, sizeof(proposal->waypoints));\n    proposal->waypoint_count = OPENRIDE_LOOP_MAX_WAYPOINTS;\n    proposal->source_candidate_index = source_index;\n'''
count = source.count(old)
if count != 1:
    print("ERROR: Ride Planner launcher could not apply proposal stats fix (%d matches)" % count,
          file=sys.stderr)
    raise SystemExit(1)
source = source.replace(old, new, 1)

namespace = {
    "__name__": "__main__",
    "__file__": str(path),
}
exec(compile(source, str(path), "exec"), namespace, namespace)
