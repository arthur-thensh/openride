#!/usr/bin/env python3
"""OpenRide UI V3.0.3 — hierarchy & panels.

One-shot guarded migration intended for a local tree where V3.0.1 visual polish
and V3.0.2 scalable typography are already applied.

Changes only presentation/layout:
- hide map status overlay while a modal/search is open;
- make the map status card contextual and disappear when idle;
- group route actions into DEPART / ARRIVEE + one CTA;
- add compact empty states for Favorites/History;
- turn offline-map technical lines into three readable status cards;
- split Settings into APPLICATION / DEVELOPPEUR sections.

No routing, GPS, region preparation, storage, or semantic action logic changes.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    "runtime": ROOT / "src" / "app_runtime.c",
    "overlay": ROOT / "src" / "ui" / "ui_map_overlay.c",
    "route": ROOT / "src" / "ui" / "ui_route_panel.c",
    "places": ROOT / "src" / "ui" / "ui_places_panel.c",
    "regions": ROOT / "src" / "ui" / "ui_regions_panel.c",
    "settings": ROOT / "src" / "ui" / "ui_settings_panel.c",
}


def fail(message: str) -> None:
    raise RuntimeError(message)


def ensure_include(text: str, after: str, include: str, label: str) -> str:
    if include in text:
        return text
    count = text.count(after)
    if count != 1:
        fail(f"{label}: expected one include anchor, found {count}")
    return text.replace(after, after + include, 1)


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
            if c == "\n": in_line_comment = False
        elif in_block_comment:
            if c == "*" and n == "/":
                in_block_comment = False
                i += 1
        elif in_string:
            if escaped: escaped = False
            elif c == "\\": escaped = True
            elif c == '"': in_string = False
        elif in_char:
            if escaped: escaped = False
            elif c == "\\": escaped = True
            elif c == "'": in_char = False
        else:
            if c == "/" and n == "/":
                in_line_comment = True
                i += 1
            elif c == "/" and n == "*":
                in_block_comment = True
                i += 1
            elif c == '"': in_string = True
            elif c == "'": in_char = True
            elif c == "{": depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    end = i + 1
                    break
        i += 1
    if end < 0:
        fail(f"{name}: closing brace not found")

    while end < len(text) and text[end] in " \t": end += 1
    if end < len(text) and text[end] == "\n": end += 1
    return text[:start] + replacement.rstrip() + "\n\n" + text[end:]


def prepare_runtime(text: str) -> str:
    old = """        if (!drive_mode.active) {\n            openride_app_ui_draw_map_status_overlay(renderer,\n"""
    new = """        if (!drive_mode.active\n            && app_panel == OPENRIDE_APP_PANEL_NONE\n            && !place_search_active) {\n            openride_app_ui_draw_map_status_overlay(renderer,\n"""
    return replace_once(text, old, new, "modal map overlay suppression")


def prepare_overlay(text: str) -> str:
    text = ensure_include(text,
                          "#include <stddef.h>\n",
                          "#include <string.h>\n",
                          "overlay string include")
    replacement = r'''static void draw_compact(OpenRideUIContext *ui,
                         const OpenRideUIMapOverlayState *state,
                         OpenRideUIRect safe)
{
    const char *summary = state->summary ? state->summary : "";
    const bool idle = !state->route_ready
        && (summary[0] == '\0'
            || strcmp(summary, "pret") == 0
            || strcmp(summary, "prêt") == 0);

    /* Idle map = map first. Keep only legal attribution, no permanent brand card. */
    if (idle) {
        draw_attribution(ui, state);
        return;
    }

    const float panel_w = minf_openride(safe.w,
                                        state->route_ready ? 310.0f : 300.0f);
    const float panel_h = state->route_ready ? 62.0f : 50.0f;
    const float panel_x = safe.x + (safe.w - panel_w) * 0.5f;
    OpenRideUIRect panel = openride_ui_rect(panel_x,
                                            safe.y,
                                            panel_w,
                                            panel_h);
    openride_ui_panel(ui, panel, true);

    const OpenRideUIRect icon_rect = openride_ui_rect(panel.x + 12.0f,
                                                       panel.y + 13.0f,
                                                       24.0f,
                                                       24.0f);
    openride_ui_icon_draw(ui,
                          state->route_ready
                              ? OPENRIDE_UI_ICON_ROUTE
                              : OPENRIDE_UI_ICON_MAP,
                          icon_rect,
                          state->route_ready
                              ? ui->theme.primary
                              : ui->theme.text_secondary,
                          1.7f);

    if (state->route_ready) {
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
    } else {
        openride_ui_text(ui,
                         openride_ui_rect(panel.x + 48.0f,
                                          panel.y + 8.0f,
                                          panel.w - 60.0f,
                                          30.0f),
                         summary,
                         OPENRIDE_UI_TEXT_BODY,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
    }

    draw_attribution(ui, state);
}'''
    return replace_c_function(text, "draw_compact", replacement)


def prepare_route(text: str) -> str:
    text = ensure_include(text,
                          '#include "openride/ui_route_panel.h"\n',
                          '#include "openride/ui_icon.h"\n',
                          "route SVG include")
    layout = r'''OpenRideUIRoutePanelLayout openride_ui_route_panel_layout(
    const OpenRideUIContext *ui)
{
    OpenRideUIRoutePanelLayout layout = {0};
    if (!ui) return layout;

    OpenRideUIRect safe = openride_ui_inset(openride_ui_safe_rect(ui), 10.0f);
    if (safe.w < 120.0f || safe.h < 360.0f) return layout;

    const float panel_w = safe.w < 350.0f ? safe.w : 350.0f;
    const float max_h = safe.h * 0.92f;
    const float panel_h = max_h < 505.0f ? max_h : 505.0f;
    const float panel_x = safe.x + (safe.w - panel_w) * 0.5f;
    const float panel_y = safe.y + (safe.h - panel_h) * 0.42f;
    layout.panel = openride_ui_rect(panel_x, panel_y, panel_w, panel_h);

    layout.title = openride_ui_rect(panel_x + 18.0f,
                                    panel_y + 12.0f,
                                    panel_w - 36.0f,
                                    28.0f);
    layout.subtitle = openride_ui_rect(panel_x + 18.0f,
                                       panel_y + 40.0f,
                                       panel_w - 36.0f,
                                       20.0f);

    const float inner_x = panel_x + 10.0f;
    const float inner_w = panel_w - 20.0f;
    const float back_h = 46.0f;
    layout.back = openride_ui_rect(inner_x,
                                   panel_y + panel_h - back_h - 10.0f,
                                   inner_w,
                                   back_h);

    const float content_top = panel_y + 82.0f;
    const float content_bottom = layout.back.y - 12.0f;
    const float fixed = 18.0f + 18.0f + 52.0f + 20.0f + 54.0f;
    float row_h = (content_bottom - content_top - fixed) / 5.0f;
    if (row_h > 48.0f) row_h = 48.0f;
    if (row_h < 36.0f) row_h = 36.0f;
    const float gap = 6.0f;

    float y = content_top + 18.0f;
    for (uint32_t i = 0U; i < 3U; ++i) {
        layout.items[i] = openride_ui_rect(inner_x, y, inner_w, row_h);
        y += row_h + gap;
    }

    y += 18.0f;
    for (uint32_t i = 3U; i < 5U; ++i) {
        layout.items[i] = openride_ui_rect(inner_x, y, inner_w, row_h);
        y += row_h + gap;
    }

    y += 8.0f;
    layout.items[5] = openride_ui_rect(inner_x, y, inner_w, 52.0f);
    layout.hint = openride_ui_rect(inner_x,
                                   y + 55.0f,
                                   inner_w,
                                   20.0f);
    return layout;
}'''
    text = replace_c_function(text, "openride_ui_route_panel_layout", layout)

    draw = r'''OpenRideUIRoutePanelAction openride_ui_route_panel_draw(
    OpenRideUIContext *ui,
    const OpenRideUIRoutePanelState *state)
{
    if (!ui || !ui->renderer) return OPENRIDE_UI_ROUTE_PANEL_NONE;

    const OpenRideUIRoutePanelState empty = {0};
    if (!state) state = &empty;
    const OpenRideUIRoutePanelLayout layout = openride_ui_route_panel_layout(ui);
    if (layout.panel.w <= 0.0f || layout.panel.h <= 0.0f) {
        return OPENRIDE_UI_ROUTE_PANEL_NONE;
    }

    SDL_FRect screen = {0.0f, 0.0f,
                        (float)ui->viewport_width,
                        (float)ui->viewport_height};
    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 92);
    SDL_RenderFillRect(ui->renderer, &screen);
    openride_ui_panel(ui, layout.panel, true);

    openride_ui_icon_draw(ui,
                          OPENRIDE_UI_ICON_ROUTE,
                          openride_ui_rect(layout.panel.x + 16.0f,
                                           layout.panel.y + 14.0f,
                                           24.0f,
                                           24.0f),
                          ui->theme.primary,
                          1.8f);
    openride_ui_text(ui,
                     openride_ui_rect(layout.title.x + 34.0f,
                                      layout.title.y,
                                      layout.title.w - 34.0f,
                                      layout.title.h),
                     "Itinéraire",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    char subtitle[128];
    snprintf(subtitle,
             sizeof(subtitle),
             "Départ %s  ·  Arrivée %s",
             state->has_start ? "choisi" : "à choisir",
             state->has_destination ? "choisie" : "à choisir");
    openride_ui_text(ui, layout.subtitle, subtitle,
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    openride_ui_text_color(ui,
                           openride_ui_rect(layout.items[0].x + 4.0f,
                                            layout.items[0].y - 18.0f,
                                            layout.items[0].w - 8.0f,
                                            16.0f),
                           "DÉPART",
                           OPENRIDE_UI_TEXT_CAPTION,
                           OPENRIDE_UI_TEXT_ALIGN_LEFT,
                           ui->theme.primary);
    openride_ui_text_color(ui,
                           openride_ui_rect(layout.items[3].x + 4.0f,
                                            layout.items[3].y - 18.0f,
                                            layout.items[3].w - 8.0f,
                                            16.0f),
                           "ARRIVÉE",
                           OPENRIDE_UI_TEXT_CAPTION,
                           OPENRIDE_UI_TEXT_ALIGN_LEFT,
                           ui->theme.primary);

    char gps_label[80];
    if (state->gps_valid && isfinite(state->gps_accuracy_m)) {
        snprintf(gps_label, sizeof(gps_label), "Ma position GPS  ·  %.0f m",
                 state->gps_accuracy_m);
    } else {
        snprintf(gps_label, sizeof(gps_label), "Ma position GPS");
    }

    const char *labels[OPENRIDE_UI_ROUTE_ITEMS] = {
        gps_label,
        "Rechercher un lieu",
        "Choisir sur la carte",
        "Rechercher un lieu",
        "Choisir sur la carte",
        "Calculer l’itinéraire"
    };
    const OpenRideUIIcon icons[OPENRIDE_UI_ROUTE_ITEMS] = {
        OPENRIDE_UI_ICON_GPS,
        OPENRIDE_UI_ICON_SEARCH,
        OPENRIDE_UI_ICON_MAP,
        OPENRIDE_UI_ICON_SEARCH,
        OPENRIDE_UI_ICON_MAP,
        OPENRIDE_UI_ICON_ROUTE
    };

    const bool ready = state->has_start && state->has_destination;
    OpenRideUIRoutePanelAction clicked = OPENRIDE_UI_ROUTE_PANEL_NONE;
    for (uint32_t i = 0U; i < OPENRIDE_UI_ROUTE_ITEMS; ++i) {
        const bool calculate = i == OPENRIDE_UI_ROUTE_ITEMS - 1U;
        const bool enabled = !calculate || ready;
        const OpenRideUIButtonStyle style = calculate && ready
            ? OPENRIDE_UI_BUTTON_PRIMARY
            : OPENRIDE_UI_BUTTON_SECONDARY;
        if (openride_ui_button(ui,
                               route_item_id(i),
                               layout.items[i],
                               "",
                               style,
                               enabled,
                               false)) {
            clicked = (OpenRideUIRoutePanelAction)(
                OPENRIDE_UI_ROUTE_PANEL_GPS_START + (int)i);
        }

        OpenRideUIColor tint = enabled ? ui->theme.text_secondary : ui->theme.disabled;
        if (calculate && ready) tint = ui->theme.text;
        openride_ui_icon_draw(ui,
                              icons[i],
                              openride_ui_rect(layout.items[i].x + 13.0f,
                                               layout.items[i].y
                                                   + (layout.items[i].h - 23.0f) * 0.5f,
                                               23.0f,
                                               23.0f),
                              tint,
                              1.7f);
        openride_ui_text_color(ui,
                               openride_ui_rect(layout.items[i].x + 48.0f,
                                                layout.items[i].y,
                                                layout.items[i].w - 60.0f,
                                                layout.items[i].h),
                               labels[i],
                               OPENRIDE_UI_TEXT_BODY,
                               OPENRIDE_UI_TEXT_ALIGN_LEFT,
                               calculate && ready ? ui->theme.text : tint);
    }

    openride_ui_text(ui,
                     layout.hint,
                     "Les points peuvent aussi être déplacés directement sur la carte",
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_CENTER);

    if (openride_ui_button(ui,
                           OPENRIDE_UI_ID("route-back"),
                           layout.back,
                           "Retour",
                           OPENRIDE_UI_BUTTON_GHOST,
                           true,
                           false)) {
        clicked = OPENRIDE_UI_ROUTE_PANEL_BACK;
    }
    return clicked;
}'''
    return replace_c_function(text, "openride_ui_route_panel_draw", draw)


def prepare_places(text: str) -> str:
    text = ensure_include(text,
                          '#include "openride/ui_places_panel.h"\n',
                          '#include "openride/ui_icon.h"\n',
                          "places SVG include")
    layout = r'''OpenRideUIPlacesPanelLayout openride_ui_places_panel_layout(
    const OpenRideUIContext *ui,
    uint32_t item_count)
{
    OpenRideUIPlacesPanelLayout layout = {0};
    if (!ui) return layout;
    if (item_count > OPENRIDE_UI_PLACES_PANEL_MAX_ITEMS) {
        item_count = OPENRIDE_UI_PLACES_PANEL_MAX_ITEMS;
    }

    OpenRideUIRect safe = openride_ui_inset(openride_ui_safe_rect(ui), 10.0f);
    if (safe.w < 120.0f || safe.h < 220.0f) return layout;

    const float panel_w = safe.w < 350.0f ? safe.w : 350.0f;
    float panel_h = item_count == 0U
        ? 255.0f
        : 92.0f + (float)item_count * 54.0f
            + (float)(item_count > 0U ? item_count - 1U : 0U) * 7.0f
            + 62.0f;
    const float max_h = safe.h * 0.88f;
    if (panel_h > max_h) panel_h = max_h;
    if (panel_h < 230.0f) panel_h = 230.0f;

    const float x = safe.x + (safe.w - panel_w) * 0.5f;
    const float y = safe.y + (safe.h - panel_h) * (item_count == 0U ? 0.36f : 0.45f);
    layout.panel = openride_ui_rect(x, y, panel_w, panel_h);
    layout.title = openride_ui_rect(x + 18.0f, y + 12.0f, panel_w - 36.0f, 28.0f);
    layout.subtitle = openride_ui_rect(x + 18.0f, y + 40.0f, panel_w - 36.0f, 18.0f);
    layout.back = openride_ui_rect(x + 10.0f,
                                   y + panel_h - 54.0f,
                                   panel_w - 20.0f,
                                   44.0f);
    layout.item_count = item_count;
    if (item_count == 0U) return layout;

    const float rows_top = y + 72.0f;
    float available = layout.back.y - 10.0f - rows_top;
    available -= 7.0f * (float)(item_count - 1U);
    float row_h = available / (float)item_count;
    if (row_h > 54.0f) row_h = 54.0f;
    if (row_h < 36.0f) row_h = 36.0f;
    for (uint32_t i = 0U; i < item_count; ++i) {
        layout.items[i] = openride_ui_rect(x + 10.0f,
                                           rows_top + (row_h + 7.0f) * (float)i,
                                           panel_w - 20.0f,
                                           row_h);
    }
    return layout;
}'''
    text = replace_c_function(text, "openride_ui_places_panel_layout", layout)

    draw = r'''OpenRideUIPlacesPanelHit openride_ui_places_panel_draw(
    OpenRideUIContext *ui,
    const OpenRideUIPlacesPanelState *state)
{
    OpenRideUIPlacesPanelHit hit = {OPENRIDE_UI_PLACES_PANEL_NONE, -1};
    if (!ui || !ui->renderer || !state) return hit;

    uint32_t count = state->count;
    if (count > OPENRIDE_UI_PLACES_PANEL_MAX_ITEMS) {
        count = OPENRIDE_UI_PLACES_PANEL_MAX_ITEMS;
    }
    const OpenRideUIPlacesPanelLayout layout =
        openride_ui_places_panel_layout(ui, count);
    if (layout.panel.w <= 0.0f || layout.panel.h <= 0.0f) return hit;

    SDL_FRect screen = {0.0f, 0.0f,
                        (float)ui->viewport_width,
                        (float)ui->viewport_height};
    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 92);
    SDL_RenderFillRect(ui->renderer, &screen);
    openride_ui_panel(ui, layout.panel, true);

    const bool history = state->mode == OPENRIDE_UI_PLACES_PANEL_HISTORY;
    const OpenRideUIIcon header_icon = history
        ? OPENRIDE_UI_ICON_HISTORY
        : OPENRIDE_UI_ICON_FAVORITE;
    openride_ui_icon_draw(ui,
                          header_icon,
                          openride_ui_rect(layout.panel.x + 16.0f,
                                           layout.panel.y + 14.0f,
                                           23.0f,
                                           23.0f),
                          ui->theme.primary,
                          1.7f);
    openride_ui_text(ui,
                     openride_ui_rect(layout.title.x + 34.0f,
                                      layout.title.y,
                                      layout.title.w - 34.0f,
                                      layout.title.h),
                     history ? "Historique" : "Favoris",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);
    if (count > 0U) {
        openride_ui_text(ui, layout.subtitle,
                         history ? "Tes destinations récentes" : "Tes lieux enregistrés",
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
    }

    if (count == 0U) {
        const float cx = layout.panel.x + layout.panel.w * 0.5f;
        openride_ui_icon_draw(ui,
                              header_icon,
                              openride_ui_rect(cx - 22.0f,
                                               layout.panel.y + 82.0f,
                                               44.0f,
                                               44.0f),
                              ui->theme.text_secondary,
                              1.5f);
        openride_ui_text(ui,
                         openride_ui_rect(layout.panel.x + 24.0f,
                                          layout.panel.y + 132.0f,
                                          layout.panel.w - 48.0f,
                                          28.0f),
                         history ? "Aucun trajet récent" : "Aucun favori",
                         OPENRIDE_UI_TEXT_BODY,
                         OPENRIDE_UI_TEXT_ALIGN_CENTER);
        openride_ui_text(ui,
                         openride_ui_rect(layout.panel.x + 28.0f,
                                          layout.panel.y + 160.0f,
                                          layout.panel.w - 56.0f,
                                          36.0f),
                         history
                             ? "Les destinations utilisées apparaîtront ici."
                             : "Enregistre un lieu pour le retrouver rapidement.",
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_CENTER);
    }

    for (uint32_t i = 0U; i < count; ++i) {
        const char *label = state->items[i] && state->items[i][0]
            ? state->items[i]
            : "Position enregistrée";
        if (openride_ui_button(ui,
                               places_item_id(i),
                               layout.items[i],
                               "",
                               OPENRIDE_UI_BUTTON_SECONDARY,
                               true,
                               i == state->selected)) {
            hit.action = OPENRIDE_UI_PLACES_PANEL_PLACE;
            hit.index = (int)i;
        }
        openride_ui_icon_draw(ui,
                              history ? OPENRIDE_UI_ICON_HISTORY : OPENRIDE_UI_ICON_FAVORITE,
                              openride_ui_rect(layout.items[i].x + 13.0f,
                                               layout.items[i].y
                                                   + (layout.items[i].h - 22.0f) * 0.5f,
                                               22.0f,
                                               22.0f),
                              i == state->selected
                                  ? ui->theme.primary
                                  : ui->theme.text_secondary,
                              1.5f);
        openride_ui_text(ui,
                         openride_ui_rect(layout.items[i].x + 46.0f,
                                          layout.items[i].y,
                                          layout.items[i].w - 58.0f,
                                          layout.items[i].h),
                         label,
                         OPENRIDE_UI_TEXT_BODY,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
    }

    if (openride_ui_button(ui,
                           OPENRIDE_UI_ID("places-back"),
                           layout.back,
                           "Retour",
                           OPENRIDE_UI_BUTTON_GHOST,
                           true,
                           false)) {
        hit.action = OPENRIDE_UI_PLACES_PANEL_BACK;
        hit.index = -1;
    }
    return hit;
}'''
    return replace_c_function(text, "openride_ui_places_panel_draw", draw)


def prepare_regions(text: str) -> str:
    layout = r'''OpenRideUIRegionsPanelLayout openride_ui_regions_panel_layout(
    const OpenRideUIContext *ui)
{
    OpenRideUIRegionsPanelLayout layout = {0};
    if (!ui) return layout;

    OpenRideUIRect safe = openride_ui_inset(openride_ui_safe_rect(ui), 10.0f);
    if (safe.w < 120.0f || safe.h < 380.0f) return layout;
    const float panel_w = safe.w < 350.0f ? safe.w : 350.0f;
    const float max_h = safe.h * 0.88f;
    const float panel_h = max_h < 480.0f ? max_h : 480.0f;
    const float x = safe.x + (safe.w - panel_w) * 0.5f;
    const float y = safe.y + (safe.h - panel_h) * 0.42f;
    layout.panel = openride_ui_rect(x, y, panel_w, panel_h);
    layout.title = openride_ui_rect(x + 18.0f, y + 12.0f, panel_w - 36.0f, 28.0f);
    layout.subtitle = openride_ui_rect(x + 18.0f, y + 40.0f, panel_w - 36.0f, 20.0f);

    const float inner_x = x + 10.0f;
    const float inner_w = panel_w - 20.0f;
    const float nav_y = y + 72.0f;
    const float half = (inner_w - 8.0f) * 0.5f;
    layout.previous = openride_ui_rect(inner_x, nav_y, half, 40.0f);
    layout.next = openride_ui_rect(inner_x + half + 8.0f, nav_y, half, 40.0f);

    const float status_y = nav_y + 50.0f;
    for (uint32_t i = 0U; i < 3U; ++i) {
        layout.status[i] = openride_ui_rect(inner_x,
                                            status_y + 48.0f * (float)i,
                                            inner_w,
                                            42.0f);
    }

    layout.back = openride_ui_rect(inner_x,
                                   y + panel_h - 54.0f,
                                   inner_w,
                                   44.0f);
    layout.remove = openride_ui_rect(inner_x,
                                     layout.back.y - 48.0f,
                                     inner_w,
                                     40.0f);
    layout.install = openride_ui_rect(inner_x,
                                      layout.remove.y - 62.0f,
                                      inner_w,
                                      52.0f);
    layout.work_status = openride_ui_rect(inner_x,
                                          layout.install.y - 26.0f,
                                          inner_w,
                                          20.0f);
    return layout;
}'''
    text = replace_c_function(text, "openride_ui_regions_panel_layout", layout)

    draw = r'''OpenRideUIRegionsPanelAction openride_ui_regions_panel_draw(
    OpenRideUIContext *ui,
    const OpenRideUIRegionsPanelState *state)
{
    if (!ui || !ui->renderer || !state) return OPENRIDE_UI_REGIONS_PANEL_NONE;
    const OpenRideUIRegionsPanelLayout layout = openride_ui_regions_panel_layout(ui);
    if (layout.panel.w <= 0.0f || layout.panel.h <= 0.0f) {
        return OPENRIDE_UI_REGIONS_PANEL_NONE;
    }

    SDL_FRect screen = {0.0f, 0.0f,
                        (float)ui->viewport_width,
                        (float)ui->viewport_height};
    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 92);
    SDL_RenderFillRect(ui->renderer, &screen);
    openride_ui_panel(ui, layout.panel, true);

    openride_ui_text(ui, layout.title, "Cartes hors ligne",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);
    char subtitle[128];
    snprintf(subtitle, sizeof(subtitle), "%s%s  ·  %.0f Mo",
             state->region_name ? state->region_name : "Région",
             state->region_is_active ? "  ·  active" : "",
             state->total_size_mb);
    openride_ui_text(ui, layout.subtitle, subtitle,
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    OpenRideUIRegionsPanelAction clicked = OPENRIDE_UI_REGIONS_PANEL_NONE;
    if (openride_ui_button(ui, OPENRIDE_UI_ID("regions-previous"),
                           layout.previous, "Précédente",
                           OPENRIDE_UI_BUTTON_GHOST, true, false)) {
        clicked = OPENRIDE_UI_REGIONS_PANEL_PREVIOUS;
    }
    if (openride_ui_button(ui, OPENRIDE_UI_ID("regions-next"),
                           layout.next, "Suivante",
                           OPENRIDE_UI_BUTTON_GHOST, true, false)) {
        clicked = OPENRIDE_UI_REGIONS_PANEL_NEXT;
    }

    const char *names[3] = {"Carte", "Navigation", "Recherche"};
    const bool installed[3] = {
        state->ormap_installed,
        state->routing_installed,
        state->search_installed
    };
    for (uint32_t i = 0U; i < 3U; ++i) {
        openride_ui_panel(ui, layout.status[i], false);
        openride_ui_text(ui,
                         openride_ui_rect(layout.status[i].x + 12.0f,
                                          layout.status[i].y,
                                          layout.status[i].w * 0.58f,
                                          layout.status[i].h),
                         names[i],
                         OPENRIDE_UI_TEXT_BODY,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
        OpenRideUIColor tint = installed[i]
            ? ui->theme.success
            : ui->theme.text_secondary;
        openride_ui_text_color(ui,
                               openride_ui_rect(layout.status[i].x
                                                   + layout.status[i].w * 0.56f,
                                                layout.status[i].y,
                                                layout.status[i].w * 0.40f - 10.0f,
                                                layout.status[i].h),
                               installed[i] ? "Prête" : "Absente",
                               OPENRIDE_UI_TEXT_CAPTION,
                               OPENRIDE_UI_TEXT_ALIGN_RIGHT,
                               tint);
    }

    char primary_label[112];
    const bool install_enabled = !state->busy;
    if (state->busy) {
        if (state->progress >= 0.0) {
            snprintf(primary_label, sizeof(primary_label),
                     "Préparation  ·  %.0f %%", state->progress * 100.0);
        } else {
            snprintf(primary_label, sizeof(primary_label), "Préparation en cours…");
        }
    } else if (state->ready && !state->poly_present) {
        snprintf(primary_label, sizeof(primary_label), "Ajouter l’aperçu de région");
    } else if (state->ready) {
        snprintf(primary_label, sizeof(primary_label), "%s",
                 state->region_is_active ? "Région active" : "Utiliser cette région");
    } else if (state->source_pbf_present) {
        snprintf(primary_label, sizeof(primary_label), "Préparer les données locales");
    } else {
        snprintf(primary_label, sizeof(primary_label), "Télécharger la région");
    }

    if (state->work_status && state->work_status[0]) {
        openride_ui_text(ui, layout.work_status, state->work_status,
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_CENTER);
    }
    if (openride_ui_button(ui, OPENRIDE_UI_ID("regions-install"),
                           layout.install, primary_label,
                           OPENRIDE_UI_BUTTON_PRIMARY,
                           install_enabled,
                           state->region_is_active && state->ready)) {
        clicked = OPENRIDE_UI_REGIONS_PANEL_INSTALL;
    }

    if (openride_ui_button(ui, OPENRIDE_UI_ID("regions-remove"),
                           layout.remove, "",
                           OPENRIDE_UI_BUTTON_GHOST,
                           !state->busy, false)) {
        clicked = OPENRIDE_UI_REGIONS_PANEL_REMOVE;
    }
    openride_ui_text_color(ui, layout.remove,
                           "Supprimer les données locales",
                           OPENRIDE_UI_TEXT_CAPTION,
                           OPENRIDE_UI_TEXT_ALIGN_CENTER,
                           ui->theme.danger);

    if (openride_ui_button(ui, OPENRIDE_UI_ID("regions-back"),
                           layout.back, "Retour",
                           OPENRIDE_UI_BUTTON_GHOST, true, false)) {
        clicked = OPENRIDE_UI_REGIONS_PANEL_BACK;
    }
    return clicked;
}'''
    return replace_c_function(text, "openride_ui_regions_panel_draw", draw)


def prepare_settings(text: str) -> str:
    layout = r'''OpenRideUISettingsPanelLayout openride_ui_settings_panel_layout(
    const OpenRideUIContext *ui)
{
    OpenRideUISettingsPanelLayout layout = {0};
    if (!ui) return layout;

    OpenRideUIRect safe = openride_ui_inset(openride_ui_safe_rect(ui), 10.0f);
    if (safe.w < 120.0f || safe.h < 390.0f) return layout;
    const float panel_w = safe.w < 350.0f ? safe.w : 350.0f;
    const float max_h = safe.h * 0.92f;
    const float panel_h = max_h < 535.0f ? max_h : 535.0f;
    const float x = safe.x + (safe.w - panel_w) * 0.5f;
    const float y = safe.y + (safe.h - panel_h) * 0.44f;
    layout.panel = openride_ui_rect(x, y, panel_w, panel_h);
    layout.title = openride_ui_rect(x + 18.0f, y + 12.0f, panel_w - 36.0f, 28.0f);
    layout.subtitle = openride_ui_rect(x + 18.0f, y + 40.0f, panel_w - 36.0f, 18.0f);
    layout.back = openride_ui_rect(x + 10.0f,
                                   y + panel_h - 54.0f,
                                   panel_w - 20.0f,
                                   44.0f);

    const float top = y + 82.0f;
    const float bottom = layout.back.y - 10.0f;
    const float section_space = 38.0f;
    const float gaps = 7.0f * 7.0f;
    float row_h = (bottom - top - section_space - gaps) / 9.0f;
    if (row_h > 42.0f) row_h = 42.0f;
    if (row_h < 30.0f) row_h = 30.0f;

    float row_y = top + 18.0f;
    for (uint32_t i = 0U; i < 5U; ++i) {
        layout.items[i] = openride_ui_rect(x + 10.0f,
                                           row_y,
                                           panel_w - 20.0f,
                                           row_h);
        row_y += row_h + 7.0f;
    }
    row_y += 20.0f;
    for (uint32_t i = 5U; i < OPENRIDE_UI_SETTINGS_ITEMS; ++i) {
        layout.items[i] = openride_ui_rect(x + 10.0f,
                                           row_y,
                                           panel_w - 20.0f,
                                           row_h);
        row_y += row_h + 7.0f;
    }
    return layout;
}'''
    text = replace_c_function(text, "openride_ui_settings_panel_layout", layout)

    draw = r'''OpenRideUISettingsPanelAction openride_ui_settings_panel_draw(
    OpenRideUIContext *ui,
    const OpenRideUISettingsPanelState *state)
{
    if (!ui || !ui->renderer || !state) return OPENRIDE_UI_SETTINGS_PANEL_NONE;
    const OpenRideUISettingsPanelLayout layout = openride_ui_settings_panel_layout(ui);
    if (layout.panel.w <= 0.0f || layout.panel.h <= 0.0f) {
        return OPENRIDE_UI_SETTINGS_PANEL_NONE;
    }

    SDL_FRect screen = {0.0f, 0.0f,
                        (float)ui->viewport_width,
                        (float)ui->viewport_height};
    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 92);
    SDL_RenderFillRect(ui->renderer, &screen);
    openride_ui_panel(ui, layout.panel, true);
    openride_ui_text(ui, layout.title, "Paramètres",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);
    openride_ui_text(ui, layout.subtitle,
                     "Navigation et comportement d’OpenRide",
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    openride_ui_text_color(ui,
                           openride_ui_rect(layout.items[0].x + 4.0f,
                                            layout.items[0].y - 18.0f,
                                            layout.items[0].w - 8.0f,
                                            16.0f),
                           "APPLICATION",
                           OPENRIDE_UI_TEXT_CAPTION,
                           OPENRIDE_UI_TEXT_ALIGN_LEFT,
                           ui->theme.primary);
    openride_ui_text_color(ui,
                           openride_ui_rect(layout.items[5].x + 4.0f,
                                            layout.items[5].y - 18.0f,
                                            layout.items[5].w - 8.0f,
                                            16.0f),
                           "DÉVELOPPEUR",
                           OPENRIDE_UI_TEXT_CAPTION,
                           OPENRIDE_UI_TEXT_ALIGN_LEFT,
                           ui->theme.text_secondary);

    char labels[OPENRIDE_UI_SETTINGS_ITEMS][112];
    snprintf(labels[0], sizeof(labels[0]), "Style de carte  ·  %s",
             state->map_style_name ? state->map_style_name : "-");
    snprintf(labels[1], sizeof(labels[1]), "Profil de route  ·  %s",
             state->routing_profile_name ? state->routing_profile_name : "-");
    snprintf(labels[2], sizeof(labels[2]), "Suivi GPS  ·  %s",
             state->follow_gps ? "Activé" : "Désactivé");
    snprintf(labels[3], sizeof(labels[3]), "Recalcul automatique  ·  %s",
             state->auto_reroute ? "Activé" : "Désactivé");
    snprintf(labels[4], sizeof(labels[4]), "Guidage vocal  ·  %s",
             state->voice_enabled ? "Activé" : "Désactivé");
    snprintf(labels[5], sizeof(labels[5]), "GPS simulé  ·  %s",
             state->simulated_gps_active ? "Activé" : "Désactivé");
    snprintf(labels[6], sizeof(labels[6]), "Déviation 80 m  ·  %s",
             state->simulated_gps_deviation
                 ? "En cours"
                 : state->simulated_gps_active ? "Déclencher" : "GPS simulé requis");
    snprintf(labels[7], sizeof(labels[7]), "Vitesse simulation  ·  x%.0f",
             state->simulated_gps_time_scale);
    snprintf(labels[8], sizeof(labels[8]), "Virage raté  ·  %s",
             state->simulated_missed_turn_active
                 ? "Mauvaise route"
                 : state->simulated_missed_turn_armed
                     ? "Armé"
                     : state->simulated_gps_active ? "Déclencher" : "GPS simulé requis");

    OpenRideUISettingsPanelAction clicked = OPENRIDE_UI_SETTINGS_PANEL_NONE;
    for (uint32_t i = 0U; i < OPENRIDE_UI_SETTINGS_ITEMS; ++i) {
        const bool selected =
            (i == 5U && state->simulated_gps_active)
            || (i == 6U && state->simulated_gps_deviation)
            || (i == 8U
                && (state->simulated_missed_turn_armed
                    || state->simulated_missed_turn_active));
        if (openride_ui_button(ui,
                               settings_id(i),
                               layout.items[i],
                               "",
                               i < 5U
                                   ? OPENRIDE_UI_BUTTON_SECONDARY
                                   : OPENRIDE_UI_BUTTON_GHOST,
                               true,
                               selected)) {
            clicked = (OpenRideUISettingsPanelAction)(
                OPENRIDE_UI_SETTINGS_PANEL_STYLE + (int)i);
        }
        OpenRideUIColor tint = i < 5U
            ? ui->theme.text
            : ui->theme.text_secondary;
        if (selected) tint = ui->theme.primary;
        openride_ui_text_color(ui,
                               openride_ui_inset_xy(layout.items[i], 13.0f, 0.0f),
                               labels[i],
                               OPENRIDE_UI_TEXT_BODY,
                               OPENRIDE_UI_TEXT_ALIGN_LEFT,
                               tint);
    }

    if (openride_ui_button(ui, OPENRIDE_UI_ID("settings-back"),
                           layout.back, "Retour",
                           OPENRIDE_UI_BUTTON_GHOST, true, false)) {
        clicked = OPENRIDE_UI_SETTINGS_PANEL_BACK;
    }
    return clicked;
}'''
    return replace_c_function(text, "openride_ui_settings_panel_draw", draw)


def main() -> int:
    missing = [str(path) for path in FILES.values() if not path.exists()]
    if missing:
        fail("V3.0.3: missing files: " + ", ".join(missing))

    original = {key: path.read_text(encoding="utf-8") for key, path in FILES.items()}

    # Strong local-state guards. V3.0.2 should already be present locally.
    if '#include "openride/ui_font.h"' not in (ROOT / "src/ui/ui.c").read_text(encoding="utf-8"):
        fail("V3.0.3: scalable typography V3.0.2 is not applied locally")
    if "OPENRIDE_UI_FONT_COMPAT_HEIGHT" not in (ROOT / "src/ui/ui.c").read_text(encoding="utf-8"):
        fail("V3.0.3: expected V3.0.2 UI font marker is missing")

    changed = {
        "runtime": prepare_runtime(original["runtime"]),
        "overlay": prepare_overlay(original["overlay"]),
        "route": prepare_route(original["route"]),
        "places": prepare_places(original["places"]),
        "regions": prepare_regions(original["regions"]),
        "settings": prepare_settings(original["settings"]),
    }

    for key, text in changed.items():
        if text == original[key]:
            fail(f"V3.0.3: {key} produced no change")

    required = {
        "runtime": ["app_panel == OPENRIDE_APP_PANEL_NONE", "!place_search_active"],
        "overlay": ["Idle map = map first", "Itinéraire prêt"],
        "route": ["DÉPART", "ARRIVÉE", "Calculer l’itinéraire"],
        "places": ["Aucun trajet récent", "Aucun favori"],
        "regions": ["Navigation", "Supprimer les données locales"],
        "settings": ["APPLICATION", "DÉVELOPPEUR"],
    }
    for key, tokens in required.items():
        for token in tokens:
            if token not in changed[key]:
                fail(f"V3.0.3: validation failed for {key}: {token!r}")

    # Transactional write: nothing above this point modified the tree.
    for key, path in FILES.items():
        path.write_text(changed[key], encoding="utf-8")

    print("OK: OpenRide UI V3.0.3 hierarchy & panels applied")
    print("Changed: app runtime overlay visibility, map status, route, places, offline maps, settings")
    print("Map overlay: hidden behind modal/search; absent while idle")
    print("Route: grouped DEPART / ARRIVEE + primary CTA")
    print("Places: compact empty states")
    print("Offline maps: user-facing status cards; destructive action demoted")
    print("Settings: APPLICATION / DEVELOPPEUR sections")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
