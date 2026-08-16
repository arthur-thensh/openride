#!/usr/bin/env python3
"""OpenRide UI V3.0.2: replace debug bitmap text with scalable TrueType UI text.

The migration is intentionally local and guarded because the current UI polish
may still be uncommitted on the developer machine. It changes only:
- CMake wiring for src/ui/ui_font.c;
- src/ui/ui.c text measurement/rendering;
- src/ui/ui_drive_hud.c text measurement/rendering.

Font dependencies themselves are prepared by scripts/bootstrap_ui_font.sh.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
CMAKE = ROOT / "CMakeLists.txt"
UI = ROOT / "src" / "ui" / "ui.c"
DRIVE = ROOT / "src" / "ui" / "ui_drive_hud.c"
FONT_C = ROOT / "src" / "ui" / "ui_font.c"
FONT_H = ROOT / "include" / "openride" / "ui_font.h"
STB = ROOT / "vendor" / "stb" / "stb_truetype.h"
FONT_DATA = ROOT / "vendor" / "ui-font" / "roboto_data.inc"

COMPAT_HEIGHT = "10.5f"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def remove_function(text: str, name: str, label: str) -> str:
    pattern = re.compile(
        rf"static\s+[^\n]+\s+{re.escape(name)}\([^{{]*\)\n\{{.*?\n\}}\n\n",
        re.DOTALL,
    )
    matches = list(pattern.finditer(text))
    if len(matches) != 1:
        raise RuntimeError(f"{label}: expected one function, found {len(matches)}")
    match = matches[0]
    return text[:match.start()] + text[match.end():]


def replace_function(text: str, name: str, replacement: str, label: str) -> str:
    pattern = re.compile(
        rf"static\s+[^\n]+\s+{re.escape(name)}\([^{{]*\)\n\{{.*?\n\}}\n",
        re.DOTALL,
    )
    matches = list(pattern.finditer(text))
    if len(matches) != 1:
        raise RuntimeError(f"{label}: expected one function, found {len(matches)}")
    match = matches[0]
    return text[:match.start()] + replacement.rstrip() + "\n" + text[match.end():]


def prepare_cmake(text: str) -> str:
    if "    src/ui/ui_font.c\n" in text:
        raise RuntimeError("V3.0.2: ui_font.c is already wired in CMake")

    if "    src/ui/ui_icon.c\n" in text:
        text = replace_once(
            text,
            "    src/ui/ui_icon.c\n",
            "    src/ui/ui_icon.c\n    src/ui/ui_font.c\n",
            "CMake UI font source after SVG icons",
        )
    else:
        text = replace_once(
            text,
            "    src/ui/ui.c\n",
            "    src/ui/ui.c\n    src/ui/ui_font.c\n",
            "CMake UI font source",
        )

    marker = '''if(ANDROID)\n    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite/sqlite3.c")\n'''
    check = '''if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb/stb_truetype.h"\n   OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ui-font/roboto_data.inc")\n    message(FATAL_ERROR\n        "UI font dependencies are missing. Run: ./scripts/bootstrap_ui_font.sh")\nendif()\n\n'''
    if check not in text:
        text = replace_once(text, marker, check + marker, "CMake UI font dependency check")
    return text


def prepare_ui(text: str) -> str:
    if '#include "openride/ui_font.h"' in text:
        raise RuntimeError("V3.0.2: ui.c already includes ui_font.h")
    text = replace_once(
        text,
        '#include "openride/ui.h"\n',
        '#include "openride/ui.h"\n#include "openride/ui_font.h"\n',
        "ui font include",
    )
    text = replace_once(
        text,
        "#include <string.h>\n",
        "#include <string.h>\n\n#define OPENRIDE_UI_FONT_COMPAT_HEIGHT " + COMPAT_HEIGHT + "\n",
        "ui font compatibility height",
    )

    text = remove_function(text, "ui_utf8_glyph_count", "remove bitmap glyph counter")
    text = replace_function(
        text,
        "ui_draw_scaled_text",
        '''static void ui_draw_scaled_text(SDL_Renderer *renderer,
                                float x,
                                float y,
                                float scale,
                                const char *text)
{
    if (!renderer || !text || !text[0] || scale <= 0.0f) return;
    Uint8 r = 255U;
    Uint8 g = 255U;
    Uint8 b = 255U;
    Uint8 a = 255U;
    SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
    const OpenRideUIColor color = {r, g, b, a};
    (void)openride_ui_font_draw(renderer,
                                x,
                                y,
                                OPENRIDE_UI_FONT_COMPAT_HEIGHT * scale,
                                text,
                                color);
}''',
        "replace UI debug text renderer",
    )

    old_text_metrics = '''    const float scale = ui_text_style_scale(ui, style);\n    const float width = (float)ui_utf8_glyph_count(text) * 8.0f * scale;\n    const float height = 8.0f * scale;\n'''
    new_text_metrics = '''    const float scale = ui_text_style_scale(ui, style);\n    const float pixel_height = OPENRIDE_UI_FONT_COMPAT_HEIGHT * scale;\n    const float width = openride_ui_font_measure_width(text, pixel_height);\n    const float height = openride_ui_font_line_height(pixel_height);\n'''
    text = replace_once(text,
                        old_text_metrics,
                        new_text_metrics,
                        "UI text proportional metrics")

    old_button_metrics = '''        float text_scale = ui->text_scale;\n        const float natural_width =\n            (float)ui_utf8_glyph_count(label) * 8.0f * text_scale;\n        const float max_width = pixels.w - 24.0f * ui->scale;\n        if (natural_width > max_width && natural_width > 0.0f && max_width > 0.0f) {\n            text_scale *= max_width / natural_width;\n            if (text_scale < 1.0f) text_scale = 1.0f;\n        }\n        const float text_width =\n            (float)ui_utf8_glyph_count(label) * 8.0f * text_scale;\n        const float text_height = 8.0f * text_scale;\n'''
    new_button_metrics = '''        float text_scale = ui->text_scale;\n        float pixel_height = OPENRIDE_UI_FONT_COMPAT_HEIGHT * text_scale;\n        const float natural_width = openride_ui_font_measure_width(label, pixel_height);\n        const float max_width = pixels.w - 24.0f * ui->scale;\n        if (natural_width > max_width && natural_width > 0.0f && max_width > 0.0f) {\n            text_scale *= max_width / natural_width;\n            if (text_scale < 1.0f) text_scale = 1.0f;\n            pixel_height = OPENRIDE_UI_FONT_COMPAT_HEIGHT * text_scale;\n        }\n        const float text_width = openride_ui_font_measure_width(label, pixel_height);\n        const float text_height = openride_ui_font_line_height(pixel_height);\n'''
    text = replace_once(text,
                        old_button_metrics,
                        new_button_metrics,
                        "UI button proportional metrics")

    for forbidden in ("SDL_RenderDebugText", "ui_utf8_glyph_count"):
        if forbidden in text:
            raise RuntimeError(f"V3.0.2: legacy UI text token remains: {forbidden}")
    return text


def prepare_drive(text: str) -> str:
    if '#include "openride/ui_font.h"' in text:
        raise RuntimeError("V3.0.2: Drive HUD already includes ui_font.h")

    if '#include "openride/ui_icon.h"\n' in text:
        text = replace_once(
            text,
            '#include "openride/ui_icon.h"\n',
            '#include "openride/ui_icon.h"\n#include "openride/ui_font.h"\n',
            "Drive font include after SVG icons",
        )
    else:
        text = replace_once(
            text,
            '#include "openride/ui_drive_hud.h"\n',
            '#include "openride/ui_drive_hud.h"\n#include "openride/ui_font.h"\n',
            "Drive font include",
        )

    text = replace_once(
        text,
        "#define OPENRIDE_UI_DRIVE_CONTROL_COUNT 4U\n",
        "#define OPENRIDE_UI_DRIVE_CONTROL_COUNT 4U\n"
        "#define OPENRIDE_UI_FONT_COMPAT_HEIGHT " + COMPAT_HEIGHT + "\n",
        "Drive font compatibility height",
    )

    text = remove_function(text, "drive_glyph_count", "remove Drive bitmap glyph counter")
    text = replace_function(
        text,
        "drive_draw_scaled_text",
        '''static void drive_draw_scaled_text(SDL_Renderer *renderer,
                                   float x,
                                   float y,
                                   float scale,
                                   const char *text)
{
    if (!renderer || !text || !text[0] || scale <= 0.0f) return;
    Uint8 r = 255U;
    Uint8 g = 255U;
    Uint8 b = 255U;
    Uint8 a = 255U;
    SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
    const OpenRideUIColor color = {r, g, b, a};
    (void)openride_ui_font_draw(renderer,
                                x,
                                y,
                                OPENRIDE_UI_FONT_COMPAT_HEIGHT * scale,
                                text,
                                color);
}''',
        "replace Drive debug text renderer",
    )

    text = replace_once(
        text,
        "    const float natural = (float)drive_glyph_count(text) * 8.0f * requested_scale;\n",
        "    const float natural = openride_ui_font_measure_width(\n"
        "        text, OPENRIDE_UI_FONT_COMPAT_HEIGHT * requested_scale);\n",
        "Drive fit proportional width",
    )

    pattern = re.compile(
        r"\(float\)drive_glyph_count\(([^\n]+?)\) \* 8\.0f \* ([A-Za-z_][A-Za-z0-9_]*)"
    )
    text, replacements = pattern.subn(
        r"openride_ui_font_measure_width(\1, OPENRIDE_UI_FONT_COMPAT_HEIGHT * \2)",
        text,
    )
    if replacements < 2:
        raise RuntimeError(
            f"Drive proportional width migration: expected at least 2 uses, found {replacements}"
        )

    for forbidden in ("SDL_RenderDebugText", "drive_glyph_count"):
        if forbidden in text:
            raise RuntimeError(f"V3.0.2: legacy Drive text token remains: {forbidden}")
    return text


def main() -> int:
    for path in (CMAKE, UI, DRIVE, FONT_C, FONT_H):
        if not path.exists():
            raise RuntimeError(f"missing required file: {path.relative_to(ROOT)}")
    if not STB.exists() or not FONT_DATA.exists():
        raise RuntimeError(
            "UI font dependencies missing; run ./scripts/bootstrap_ui_font.sh first"
        )

    original_cmake = CMAKE.read_text(encoding="utf-8")
    original_ui = UI.read_text(encoding="utf-8")
    original_drive = DRIVE.read_text(encoding="utf-8")

    cmake = prepare_cmake(original_cmake)
    ui = prepare_ui(original_ui)
    drive = prepare_drive(original_drive)

    # Transactional write point.
    CMAKE.write_text(cmake, encoding="utf-8")
    UI.write_text(ui, encoding="utf-8")
    DRIVE.write_text(drive, encoding="utf-8")

    print("OK: OpenRide UI V3.0.2 scalable typography applied")
    print("Changed: CMakeLists.txt, src/ui/ui.c, src/ui/ui_drive_hud.c")
    print("Renderer: stb_truetype + cached 1024x1024 glyph atlas")
    print("Font: Roboto, embedded in generated C data; no runtime network access")
    print("Legacy SDL debug text removed from primary UI and Drive HUD")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
