#!/usr/bin/env python3
"""Run the final one-shot Ride Planner V3.1 + async loading migration."""

from pathlib import Path
import runpy

root = Path(__file__).resolve().parents[1]
path = root / "scripts" / "dev_apply_ride_planner_v311_final.py"
runpy.run_path(str(path), run_name="__main__")
