#!/usr/bin/env python3
"""OpenRide UI V3.0.4 — final visual polish before Ride Planner.

One-shot guarded migration intended for a local tree where V3.0.1, V3.0.2
and V3.0.3 have already been applied.

Presentation-only changes:
- make the disabled Route CTA readable;
- render an active offline region as a compact status badge instead of a CTA;
- turn non-route map status cards into compact one-line toasts;
- make the main toolbar one continuous cockpit surface;
- reuse OpenRide rounded surfaces in Drive HUD.

No routing, navigation, GPS, storage, region business logic, or semantic action
changes are made.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    "route": ROOT / "src" / "ui" / "ui_route_panel.c",
    "regions": ROOT / "src" / "ui" / "ui_regions_panel.c",
    "overlay": ROOT / "src" / "ui" / "ui_map_overlay.c",
    "toolbar": ROOT / "src" / "ui" / "ui_toolbar.c",
    "drive": ROOT / "src" / "ui" / "ui_drive_hud.c",
}


def fail(message: str) -> None:
    raise RuntimeError(message)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def replace_c_function(text: str, name: str, replacement: str) -> str:
    pattern = re.compile(rf"(?m)^[A-Za-z_][^\n]*\b{re.escape(name)}\s*\(")
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


def prepare_route(text: str) -> str:
    if "DÉPART" not in text or "ARRIVÉE" not in text or "Calculer l’itinéraire" not in text:
        fail("route: V3.0.3 grouped route panel not detected")

    old = """        OpenRideUIColor tint = enabled ? ui->theme.text_secondary : ui->theme.disabled;\n        if (calculate && ready) tint = ui->theme.text;\n"""
    new = """        OpenRideUIColor tint = ui->theme.text_secondary;\n        if (!enabled) tint.a = 150U;\n        if (calculate && ready) tint = ui->theme.text;\n"""
    text = replace_once(text, old, new, "route disabled CTA tint")
    return text


def prepare_regions(text: str) -> str:
    if "Supprimer les données locales" not in text or "Navigation" not in text:
        fail("regions: V3.0.3 offline-map panel not detected")

    old = """    if (openride_ui_button(ui, OPENRIDE_UI_ID(\"regions-install\"),\n                           layout.install, primary_label,\n                           OPENRIDE_UI_BUTTON_PRIMARY,\n                           install_enabled,\n                           state->region_is_active && state->ready)) {\n        clicked = OPENRIDE_UI_REGIONS_PANEL_INSTALL;\n    }\n"""
    new = """    const bool show_active_badge = state->region_is_active\n        && state->ready\n        && state->poly_present\n        && !state->busy;\n    if (show_active_badge) {\n        const float badge_w = layout.install.w < 150.0f ? layout.install.w : 150.0f;\n        const OpenRideUIRect badge = openride_ui_rect(\n            layout.install.x + (layout.install.w - badge_w) * 0.5f,\n            layout.install.y + 8.0f,\n            badge_w,\n            36.0f);\n        const OpenRideUIColor saved_surface = ui->theme.surface;\n        const OpenRideUIColor saved_border = ui->theme.border;\n        ui->theme.surface = ui->theme.primary_soft;\n        ui->theme.surface.a = 150U;\n        ui->theme.border = ui->theme.primary;\n        ui->theme.border.a = 48U;\n        openride_ui_panel(ui, badge, false);\n        ui->theme.surface = saved_surface;\n        ui->theme.border = saved_border;\n        openride_ui_text_color(ui, badge,\n                               \"Région active\",\n                               OPENRIDE_UI_TEXT_CAPTION,\n                               OPENRIDE_UI_TEXT_ALIGN_CENTER,\n                               ui->theme.primary);\n    } else if (openride_ui_button(ui, OPENRIDE_UI_ID(\"regions-install\"),\n                                  layout.install, primary_label,\n                                  OPENRIDE_UI_BUTTON_PRIMARY,\n                                  install_enabled, false)) {\n        clicked = OPENRIDE_UI_REGIONS_PANEL_INSTALL;\n    }\n"""
    text = replace_once(text, old, new, "offline active-region badge")
    return text


def prepare_overlay(text: str) -> str:
    if "Idle map = map first" not in text or "Itinéraire prêt" not in text:
        fail("overlay: V3.0.3 contextual map overlay not detected")

    replacement = r'''static void draw_compact(OpenRideUIContext *ui,
                         const OpenRideUIMapOverlayState *state,
                         OpenRideUIRect safe)
{
    const char *summary = state->summary ? state->summary : "";
    const bool idle = !state->route_ready
        && (summary[0] == '\0'
            || strcmp(summary, "pret") == 0
            || strcmp(summary, "prêt") == 0);

    /* Idle map = map first. Legal attribution remains independently visible. */
    if (idle) {
        draw_attribution(ui, state);
        return;
    }

    if (!state->route_ready) {
        /* Transient map state (loop/start/map-pick) is a toast, not a brand card. */
        const float toast_w = minf_openride(safe.w, 330.0f);
        const float toast_h = 38.0f;
        const float toast_x = safe.x + (safe.w - toast_w) * 0.5f;
        const OpenRideUIRect toast = openride_ui_rect(toast_x,
                                                      safe.y,
                                                      toast_w,
                                                      toast_h);
        openride_ui_panel(ui, toast, true);
        openride_ui_icon_draw(ui,
                              OPENRIDE_UI_ICON_MAP,
                              openride_ui_rect(toast.x + 11.0f,
                                               toast.y + 10.0f,
                                               18.0f,
                                               18.0f),
                              ui->theme.text_secondary,
                              1.45f);
        openride_ui_text(ui,
                         openride_ui_rect(toast.x + 38.0f,
                                          toast.y + 2.0f,
                                          toast.w - 49.0f,
                                          toast.h - 4.0f),
                         summary,
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
        draw_attribution(ui, state);
        return;
    }

    const float panel_w = minf_openride(safe.w, 310.0f);
    const float panel_h = 62.0f;
    const float panel_x = safe.x + (safe.w - panel_w) * 0.5f;
    const OpenRideUIRect panel = openride_ui_rect(panel_x,
                                                  safe.y,
                                                  panel_w,
                                                  panel_h);
    openride_ui_panel(ui, panel, true);
    openride_ui_icon_draw(ui,
                          OPENRIDE_UI_ICON_ROUTE,
                          openride_ui_rect(panel.x + 12.0f,
                                           panel.y + 18.0f,
                                           24.0f,
                                           24.0f),
                          ui->theme.primary,
                          1.7f);
    openride_ui_text_color(ui,
                           openride_ui_rect(panel.x + 48.0f,
                                            panel.y + 7.0f,
                                            panel.w - 60.0f,
                                            22.0f),
                           "Itinéraire prêt",
                           OPENRIDE_UI_TEXT_BODY,
                           OPENRIDE_UI_TEXT_ALIGN_LEFT,
                           ui->theme.primary);
    openride_ui_text(ui,
                     openride_ui_rect(panel.x + 48.0f,
                                      panel.y + 30.0f,
                                      panel.w - 60.0f,
                                      18.0f),
                     summary,
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);
    draw_attribution(ui, state);
}'''
    return replace_c_function(text, "draw_compact", replacement)


def prepare_toolbar(text: str) -> str:
    if "Toolbar items live inside one cockpit surface" not in text:
        fail("toolbar: V3.0.1 continuous toolbar base not detected")

    replacement = r'''OpenRideToolbarAction openride_ui_toolbar_draw(OpenRideUIContext *ui,
                                               bool route_ready)
{
    if (!ui || !ui->renderer) return OPENRIDE_TOOLBAR_NONE;
    const OpenRideUIToolbarLayout layout = openride_ui_toolbar_layout(ui);
    if (layout.bar.w <= 0.0f || layout.bar.h <= 0.0f) {
        return OPENRIDE_TOOLBAR_NONE;
    }

    openride_ui_panel(ui, layout.bar, true);

    OpenRideToolbarAction clicked = OPENRIDE_TOOLBAR_NONE;
    for (OpenRideToolbarAction action = OPENRIDE_TOOLBAR_MENU;
         action <= OPENRIDE_TOOLBAR_GPS;
         action = (OpenRideToolbarAction)(action + 1)) {
        const uint32_t index = (uint32_t)(action - OPENRIDE_TOOLBAR_MENU);
        const OpenRideUIID id = toolbar_id(action);
        const bool selected = action == OPENRIDE_TOOLBAR_ROUTE && route_ready;
        const OpenRideUIRect item = openride_ui_inset(layout.items[index],
                                                      OPENRIDE_UI_TOOLBAR_ITEM_INSET);
        const bool inside = openride_ui_point_in_rect(ui->pointer_x,
                                                      ui->pointer_y,
                                                      item);
        if (inside) ui->hot_id = id;
        if (inside && ui->pointer_pressed) {
            ui->active_id = id;
            ui->pointer_consumed = true;
        }
        const bool active = ui->active_id == id;
        if (active && ui->pointer_down) ui->pointer_consumed = true;
        if (active && ui->pointer_released) {
            ui->pointer_consumed = true;
            if (inside) clicked = action;
        }

        /* One continuous bar: only selected/pressed state gets a pill. */
        if (selected || active) {
            const OpenRideUIColor saved_surface = ui->theme.surface;
            const OpenRideUIColor saved_border = ui->theme.border;
            ui->theme.surface = selected
                ? ui->theme.primary_soft
                : ui->theme.surface_elevated;
            ui->theme.surface.a = selected ? 205U : 120U;
            ui->theme.border = selected ? ui->theme.primary : saved_border;
            ui->theme.border.a = selected ? 42U : 18U;
            openride_ui_panel(ui, openride_ui_inset(item, 4.0f), false);
            ui->theme.surface = saved_surface;
            ui->theme.border = saved_border;
        }

        float icon_size = 24.0f;
        if (icon_size > item.h * 0.40f) icon_size = item.h * 0.40f;
        const OpenRideUIRect icon_rect = openride_ui_rect(
            item.x + (item.w - icon_size) * 0.5f,
            item.y + 8.0f,
            icon_size,
            icon_size);
        const OpenRideUIColor icon_color = selected
            ? ui->theme.primary
            : (inside ? ui->theme.text : ui->theme.text_secondary);
        openride_ui_icon_draw(ui,
                              toolbar_icon(action),
                              icon_rect,
                              icon_color,
                              selected ? 2.0f : 1.7f);

        const OpenRideUIRect label_rect = openride_ui_rect(
            item.x + 2.0f,
            item.y + item.h - 22.0f,
            item.w - 4.0f,
            16.0f);
        openride_ui_text_color(ui,
                               label_rect,
                               toolbar_label(action, route_ready),
                               OPENRIDE_UI_TEXT_CAPTION,
                               OPENRIDE_UI_TEXT_ALIGN_CENTER,
                               selected
                                   ? ui->theme.primary
                                   : (inside ? ui->theme.text
                                             : ui->theme.text_secondary));
    }
    return clicked;
}'''
    return replace_c_function(text, "openride_ui_toolbar_draw", replacement)


def prepare_drive(text: str) -> str:
    if '#include "openride/ui_font.h"' not in text:
        fail("drive: V3.0.2 scalable typography not detected")
    if "OPENRIDE_UI_DRIVE_ATTRIBUTION_HEIGHT" not in text:
        fail("drive: V3.0.1 Drive HUD polish not detected")

    replacements = [
        (
            """    SDL_SetRenderDrawColor(renderer, 13, 16, 18, 238);\n    SDL_RenderFillRect(renderer, &top);\n    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 30);\n    SDL_RenderRect(renderer, &top);\n""",
            """    openride_ui_panel(ui, layout.top, true);\n""",
            "Drive top rounded card",
        ),
        (
            """        SDL_SetRenderDrawColor(renderer, 22, 26, 29, 226);\n        SDL_RenderFillRect(renderer, &following);\n        SDL_SetRenderDrawColor(renderer, 47, 198, 181, 70);\n        SDL_RenderRect(renderer, &following);\n""",
            """        openride_ui_panel(ui, layout.following, false);\n""",
            "Drive following rounded pill",
        ),
        (
            """    SDL_SetRenderDrawColor(renderer, 22, 26, 29, 232);\n    SDL_RenderFillRect(renderer, &stats);\n    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 28);\n    SDL_RenderRect(renderer, &stats);\n""",
            """    openride_ui_panel(ui, layout.stats, false);\n""",
            "Drive stats rounded card",
        ),
        (
            """    SDL_SetRenderDrawColor(renderer, 13, 16, 18, 242);\n    SDL_RenderFillRect(renderer, &controls);\n    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 30);\n    SDL_RenderRect(renderer, &controls);\n""",
            """    openride_ui_panel(ui, layout.controls, true);\n""",
            "Drive controls rounded bar",
        ),
    ]
    for old, new, label in replacements:
        text = replace_once(text, old, new, label)
    return text


def main() -> int:
    for path in FILES.values():
        if not path.exists():
            fail(f"missing required file: {path.relative_to(ROOT)}")

    original = {key: path.read_text(encoding="utf-8") for key, path in FILES.items()}
    changed = {
        "route": prepare_route(original["route"]),
        "regions": prepare_regions(original["regions"]),
        "overlay": prepare_overlay(original["overlay"]),
        "toolbar": prepare_toolbar(original["toolbar"]),
        "drive": prepare_drive(original["drive"]),
    }

    required = {
        "route": ["if (!enabled) tint.a = 150U", "Calculer l’itinéraire"],
        "regions": ["show_active_badge", "Région active"],
        "overlay": ["Transient map state", "toast_h = 38.0f"],
        "toolbar": ["One continuous bar", "selected || active"],
        "drive": [
            "openride_ui_panel(ui, layout.top, true)",
            "openride_ui_panel(ui, layout.controls, true)",
        ],
    }
    for key, tokens in required.items():
        if changed[key] == original[key]:
            fail(f"V3.0.4: {key} produced no change")
        for token in tokens:
            if token not in changed[key]:
                fail(f"V3.0.4: {key} lost required token: {token}")

    # Transactional write point: no source file is touched until every transform
    # and validation above has succeeded.
    for key, path in FILES.items():
        path.write_text(changed[key], encoding="utf-8")

    print("OK: OpenRide UI V3.0.4 final polish applied")
    print("Changed: route CTA, offline active badge, map toast, toolbar, Drive HUD")
    print("Toolbar: one continuous surface; only active state draws a pill")
    print("Drive HUD: OpenRide rounded cards, navigation behavior unchanged")
    print("Next: git diff --check && git diff --stat")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
