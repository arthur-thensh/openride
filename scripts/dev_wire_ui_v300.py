#!/usr/bin/env python3
"""Wire the UI V3.0 SVG renderer into the application build.

This script intentionally changes CMakeLists.txt only. UI source/header files are
already versioned normally. It is safe to run once and performs all validation
before writing.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
CMAKE = ROOT / "CMakeLists.txt"

OLD = "    src/ui/ui.c\n    src/ui/ui_toolbar.c\n"
NEW = "    src/ui/ui.c\n    src/ui/ui_icon.c\n    src/ui/ui_toolbar.c\n"


def main() -> int:
    text = CMAKE.read_text(encoding="utf-8")
    if "    src/ui/ui_icon.c\n" in text:
        print("OK: UI V3.0 SVG renderer already wired")
        return 0
    count = text.count(OLD)
    if count != 1:
        raise RuntimeError(
            f"UI V3.0 CMake anchor: expected exactly one match, found {count}"
        )
    prepared = text.replace(OLD, NEW, 1)
    if prepared.count("    src/ui/ui_icon.c\n") != 1:
        raise RuntimeError("UI V3.0 SVG source count is invalid")
    CMAKE.write_text(prepared, encoding="utf-8")
    print("OK: UI V3.0 SVG renderer wired into CMakeLists.txt")
    print("Next: git diff --check && git diff --stat")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
