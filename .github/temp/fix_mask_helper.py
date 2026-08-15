#!/usr/bin/env python3
from pathlib import Path

path = Path(__file__).with_name("apply_mask_geometry_cache.py")
text = path.read_text()
old = '''    if count != 1:\n        raise SystemExit(f"{label}: expected exactly one match, found {count}")\n    return text.replace(old, new, 1)\n'''
new = '''    if count != 1:\n        if label == "draw_masks debug activation" and count == 2:\n            return text.replace(old, new, 1)\n        raise SystemExit(f"{label}: expected exactly one match, found {count}")\n    return text.replace(old, new, 1)\n'''
if text.count(old) != 1:
    raise SystemExit("replace_once helper signature changed")
path.write_text(text.replace(old, new, 1))
