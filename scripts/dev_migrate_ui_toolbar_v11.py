#!/usr/bin/env python3
"""One-shot source migration for UI Engine V1.1.

This script performs three exact, guarded replacements in src/main.c:
- expose the UI toolbar API;
- route toolbar hit-testing through UI Engine;
- route toolbar rendering through UI Engine.

It intentionally does not build, test, commit, or push anything.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "src" / "main.c"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    text = MAIN.read_text(encoding="utf-8")
    original = text

    text = replace_once(
        text,
        '#include "openride/app_toolbar.h"\n#include "openride/drive_mode.h"',
        '#include "openride/app_toolbar.h"\n#include "openride/ui_toolbar.h"\n#include "openride/drive_mode.h"',
        "toolbar include",
    )

    old_hit_test = '''static OpenRideToolbarAction mobile_toolbar_hit_test(SDL_Renderer *renderer,
                                                       double x,
                                                       double y,
                                                       int viewport_width,
                                                       int viewport_height)
{
    const SDL_Rect safe = openride_render_safe_area(renderer, viewport_width, viewport_height);
    const double ui_scale = (double)openride_ui_scale(renderer);
    const int logical_width = (int)((double)safe.w / ui_scale);
    const int logical_height = (int)((double)safe.h / ui_scale);
    return openride_toolbar_hit_test((x - (double)safe.x) / ui_scale,
                                     (y - (double)safe.y) / ui_scale,
                                     logical_width,
                                     logical_height);
}
'''
    new_hit_test = '''static OpenRideToolbarAction mobile_toolbar_hit_test(SDL_Renderer *renderer,
                                                       double x,
                                                       double y,
                                                       int viewport_width,
                                                       int viewport_height)
{
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return OPENRIDE_TOOLBAR_NONE;
    }
    const OpenRideToolbarAction action =
        openride_ui_toolbar_hit_test(&ui, x, y);
    openride_ui_end(&ui);
    return action;
}
'''
    text = replace_once(text, old_hit_test, new_hit_test, "toolbar hit-test")

    old_draw = '''static void draw_mobile_toolbar(SDL_Renderer *renderer, int viewport_width, int viewport_height, bool route_ready)
{
    const SDL_Rect safe = openride_render_safe_area(renderer, viewport_width, viewport_height);
    const double ui_scale = (double)openride_ui_scale(renderer);
    const int logical_width = (int)((double)safe.w / ui_scale);
    const int logical_height = (int)((double)safe.h / ui_scale);
    OpenRideToolbarRect bar = openride_toolbar_bounds(logical_width, logical_height);
    bar.x = (double)safe.x + bar.x * ui_scale;
    bar.y = (double)safe.y + bar.y * ui_scale;
    bar.w *= ui_scale;
    bar.h *= ui_scale;
    if (bar.w <= 0.0 || bar.h <= 0.0) return;

    SDL_FRect box = {(float)bar.x, (float)bar.y, (float)bar.w, (float)bar.h};
    SDL_SetRenderDrawColor(renderer, 16, 20, 24, 228);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 70);
    SDL_RenderRect(renderer, &box);

    for (OpenRideToolbarAction action = OPENRIDE_TOOLBAR_MENU;
         action <= OPENRIDE_TOOLBAR_GPS;
         action = (OpenRideToolbarAction)(action + 1)) {
        OpenRideToolbarRect item = openride_toolbar_item_bounds(action,
                                                                  logical_width,
                                                                  logical_height);
        item.x = (double)safe.x + item.x * ui_scale;
        item.y = (double)safe.y + item.y * ui_scale;
        item.w *= ui_scale;
        item.h *= ui_scale;
        SDL_FRect item_rect = {(float)item.x, (float)item.y, (float)item.w, (float)item.h};
        if (action != OPENRIDE_TOOLBAR_MENU) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 35);
            SDL_RenderLine(renderer,
                           (float)item.x,
                           (float)item.y + 10.0f,
                           (float)item.x,
                           (float)(item.y + item.h) - 10.0f);
        }
        SDL_SetRenderDrawColor(renderer, 238, 241, 243, 255);
        const char *label = openride_toolbar_action_label(action);
        if (action == OPENRIDE_TOOLBAR_ROUTE && route_ready) label = "Demarrer";
        const float label_scale = (float)ui_scale;
        const float label_w = (float)strlen(label) * 8.0f * label_scale;
        const float label_h = 8.0f * label_scale;
        const float label_x = item_rect.x + (item_rect.w - label_w) * 0.5f;
        const float label_y = item_rect.y + (item_rect.h - label_h) * 0.5f;
        draw_scaled_text(renderer, label_x, label_y, label_scale, label);
    }
}
'''
    new_draw = '''static void draw_mobile_toolbar(SDL_Renderer *renderer, int viewport_width, int viewport_height, bool route_ready)
{
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return;
    }
    (void)openride_ui_toolbar_draw(&ui, route_ready);
    openride_ui_end(&ui);
}
'''
    text = replace_once(text, old_draw, new_draw, "toolbar renderer")

    if text == original:
        raise RuntimeError("migration produced no change")

    MAIN.write_text(text, encoding="utf-8")
    print("OK: UI Engine V1.1 toolbar migration applied to src/main.c")
    print("Next: git diff -- src/main.c")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
