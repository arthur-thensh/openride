#!/usr/bin/env python3
"""Run the V2.4 migrator with its one-line syntax hotfix applied in memory."""

from pathlib import Path

script = Path(__file__).with_name("dev_extract_app_events_v240.py")
source = script.read_text(encoding="utf-8")
old = "    if !EVENT_H.exists():\n"
new = "    if not EVENT_H.exists():\n"
if source.count(old) != 1:
    raise RuntimeError(
        "V2.4 launcher expected exactly one syntax-hotfix target in the migrator"
    )
source = source.replace(old, new, 1)
namespace = {
    "__name__": "__main__",
    "__file__": str(script),
}
exec(compile(source, str(script), "exec"), namespace)
