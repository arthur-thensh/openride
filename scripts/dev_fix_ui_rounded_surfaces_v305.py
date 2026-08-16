#!/usr/bin/env python3
"""OpenRide UI V3.0.5 — rounded surface blending fix.

Fixes a core rendering artifact in src/ui/ui.c:
- rounded fills previously used overlapping rectangles/scanlines;
- translucent colors therefore blended more than once near corners/edges;
- elevated shadows also appeared as a visible dark band below panels.

This migration changes only the UI primitive implementation. Every button,
panel, toolbar and future screen benefits automatically.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "src" / "ui" / "ui.c"


def fail(message: str) -> None:
    raise RuntimeError(message)


def replace_c_function(text: str, name: str, replacement: str) -> str:
    pattern = re.compile(rf"(?m)^static\s+[^\n]*\b{re.escape(name)}\s*\(")
    matches = list(pattern.finditer(text))
    if len(matches) != 1:
        fail(f"{name}: expected exactly one function signature, found {len(matches)}")

    start = matches[0].start()
    brace = text.find("{", matches[0].end())
    if brace < 0:
        fail(f"{name}: opening brace not found")

    depth = 0
    i = brace
    in_string = False
    in_char = False
    in_line_comment = False
    in_block_comment = False
    escaped = False
    end = -1

    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ""
        if in_line_comment:
            if c == "\n":
                in_line_comment = False
        elif in_block_comment:
            if c == "*" and n == "/":
                in_block_comment = False
                i += 1
        elif in_string:
            if escaped:
                escaped = False
            elif c == "\\":
                escaped = True
            elif c == '"':
                in_string = False
        elif in_char:
            if escaped:
                escaped = False
            elif c == "\\":
                escaped = True
            elif c == "'":
                in_char = False
        else:
            if c == "/" and n == "/":
                in_line_comment = True
                i += 1
            elif c == "/" and n == "*":
                in_block_comment = True
                i += 1
            elif c == '"':
                in_string = True
            elif c == "'":
                in_char = True
            elif c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    end = i + 1
                    break
        i += 1

    if end < 0:
        fail(f"{name}: closing brace not found")

    while end < len(text) and text[end] in " \t":
        end += 1
    if end < len(text) and text[end] == "\n":
        end += 1

    return text[:start] + replacement.rstrip() + "\n\n" + text[end:]


def main() -> int:
    if not UI.exists():
        fail("missing src/ui/ui.c")

    original = UI.read_text(encoding="utf-8")
    if "OpenRide rounded fill: exactly one blend operation per scanline" in original:
        fail("V3.0.5 already applied")

    replacement = r'''static void ui_fill_rounded_rect(SDL_Renderer *renderer,
                                 SDL_FRect rect,
                                 float radius,
                                 OpenRideUIColor color)
{
    if (!renderer || rect.w <= 0.0f || rect.h <= 0.0f) return;

    const float max_radius = fminf(rect.w, rect.h) * 0.5f;
    radius = ui_clampf(radius, 0.0f, max_radius);
    ui_set_draw_color(renderer, color);

    if (radius < 1.0f) {
        SDL_RenderFillRect(renderer, &rect);
        return;
    }

    /* OpenRide rounded fill: exactly one blend operation per scanline.
       The previous implementation composed horizontal + vertical rectangles
       and corner scanlines. With translucent UI colors those shapes overlapped,
       causing visibly darker bands around rounded edges. */
    const int rows = (int)ceilf(rect.h);
    const float bottom = rect.y + rect.h;

    for (int row = 0; row < rows; ++row) {
        const float y = rect.y + (float)row;
        float row_h = bottom - y;
        if (row_h > 1.0f) row_h = 1.0f;
        if (row_h <= 0.0f) continue;

        const float center_y = (float)row + row_h * 0.5f;
        float inset = 0.0f;

        if (center_y < radius) {
            const float dy = radius - center_y;
            const float inside = radius * radius - dy * dy;
            const float span = inside > 0.0f ? sqrtf(inside) : 0.0f;
            inset = radius - span;
        } else if (center_y > rect.h - radius) {
            const float dy = center_y - (rect.h - radius);
            const float inside = radius * radius - dy * dy;
            const float span = inside > 0.0f ? sqrtf(inside) : 0.0f;
            inset = radius - span;
        }

        const float width = rect.w - inset * 2.0f;
        if (width <= 0.0f) continue;

        const SDL_FRect scanline = {
            rect.x + inset,
            y,
            width,
            row_h
        };
        SDL_RenderFillRect(renderer, &scanline);
    }
}'''

    changed = replace_c_function(original, "ui_fill_rounded_rect", replacement)

    old_shadow = '''    if (elevated) {\n        SDL_FRect shadow = pixels;\n        shadow.y += 3.0f * scale;\n        ui_fill_rounded_rect(ui->renderer,\n                             shadow,\n                             radius,\n                             ui_color(0U, 0U, 0U, 72U));\n    }\n'''
    new_shadow = '''    if (elevated) {\n        /* Keep depth without a visible dark strip under cards/toolbars. */\n        SDL_FRect shadow = pixels;\n        shadow.y += 2.0f * scale;\n        ui_fill_rounded_rect(ui->renderer,\n                             shadow,\n                             radius,\n                             ui_color(0U, 0U, 0U, 28U));\n    }\n'''

    count = changed.count(old_shadow)
    if count != 1:
        fail(f"elevated shadow: expected exactly one match, found {count}")
    changed = changed.replace(old_shadow, new_shadow, 1)

    required = (
        "exactly one blend operation per scanline",
        "shadow.y += 2.0f * scale",
        "ui_color(0U, 0U, 0U, 28U)",
    )
    for token in required:
        if token not in changed:
            fail(f"generated ui.c missing required token: {token}")

    if changed == original:
        fail("V3.0.5 produced no change")

    # Transactional write point.
    UI.write_text(changed, encoding="utf-8")

    print("OK: OpenRide UI V3.0.5 rounded surface blending fix applied")
    print("Changed: src/ui/ui.c only")
    print("Rounded fills: single-pass scanlines, no translucent overdraw")
    print("Elevated shadow: reduced to a subtle 2 px / alpha 28 depth cue")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
