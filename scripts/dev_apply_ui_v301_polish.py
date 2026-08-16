#!/usr/bin/env python3
"""OpenRide UI V3.0.1 visual polish.

One-shot guarded migration for the remaining high-visibility UI screens:
- search overlay;
- route panel;
- offline regions panel;
- settings panel;
- Drive HUD.

Only presentation/layout code is changed. Routing, navigation, GPS and region
business behavior are untouched. All edits are staged and validated before the
first write.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]

FILES = {
    "search": ROOT / "src/ui/ui_search_overlay.c",
    "route": ROOT / "src/ui/ui_route_panel.c",
    "regions": ROOT / "src/ui/ui_regions_panel.c",
    "settings": ROOT / "src/ui/ui_settings_panel.c",
    "drive": ROOT / "src/ui/ui_drive_hud.c",
}


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError("%s: expected exactly one match, found %d" % (label, count))
    return text.replace(old, new, 1)


def polish_search(text):
    text = replace_once(
        text,
        '#include "openride/ui_search_overlay.h"\n',
        '#include "openride/ui_search_overlay.h"\n#include "openride/ui_icon.h"\n',
        "search icon include",
    )
    text = replace_once(
        text,
        '''#define OPENRIDE_UI_SEARCH_MARGIN 8.0f\n#define OPENRIDE_UI_SEARCH_GAP 6.0f\n#define OPENRIDE_UI_SEARCH_TITLE_HEIGHT 38.0f\n#define OPENRIDE_UI_SEARCH_QUERY_HEIGHT 50.0f\n#define OPENRIDE_UI_SEARCH_FOOTER_HEIGHT 12.0f\n#define OPENRIDE_UI_SEARCH_DESIRED_ROW_HEIGHT 54.0f\n#define OPENRIDE_UI_SEARCH_MIN_ROW_HEIGHT 31.0f\n''',
        '''#define OPENRIDE_UI_SEARCH_MARGIN 12.0f\n#define OPENRIDE_UI_SEARCH_GAP 7.0f\n#define OPENRIDE_UI_SEARCH_TITLE_HEIGHT 44.0f\n#define OPENRIDE_UI_SEARCH_QUERY_HEIGHT 52.0f\n#define OPENRIDE_UI_SEARCH_FOOTER_HEIGHT 12.0f\n#define OPENRIDE_UI_SEARCH_DESIRED_ROW_HEIGHT 52.0f\n#define OPENRIDE_UI_SEARCH_MIN_ROW_HEIGHT 34.0f\n#define OPENRIDE_UI_SEARCH_MAX_WIDTH 410.0f\n''',
        "search visual constants",
    )
    text = replace_once(
        text,
        '''    layout.panel = openride_ui_rect(safe.x + margin,\n                                    safe.y + margin,\n                                    safe.w - margin * 2.0f,\n                                    panel_h);\n''',
        '''    float panel_w = safe.w - margin * 2.0f;\n    if (panel_w > OPENRIDE_UI_SEARCH_MAX_WIDTH) panel_w = OPENRIDE_UI_SEARCH_MAX_WIDTH;\n    layout.panel = openride_ui_rect(safe.x + (safe.w - panel_w) * 0.5f,\n                                    safe.y + margin,\n                                    panel_w,\n                                    panel_h);\n''',
        "search centered panel",
    )
    text = replace_once(
        text,
        '''    layout.title = openride_ui_rect(layout.panel.x + 12.0f,\n                                    layout.panel.y + 5.0f,\n                                    layout.panel.w - 24.0f,\n                                    OPENRIDE_UI_SEARCH_TITLE_HEIGHT - 8.0f);\n''',
        '''    layout.title = openride_ui_rect(layout.panel.x + 46.0f,\n                                    layout.panel.y + 5.0f,\n                                    layout.panel.w - 60.0f,\n                                    OPENRIDE_UI_SEARCH_TITLE_HEIGHT - 8.0f);\n''',
        "search title layout",
    )
    text = replace_once(
        text,
        "    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 72);\n",
        "    SDL_SetRenderDrawColor(ui->renderer, 4, 7, 8, 112);\n",
        "search backdrop",
    )
    text = replace_once(
        text,
        '''    openride_ui_panel(ui, layout.panel, true);\n    openride_ui_text(ui,\n                     layout.title,\n''',
        '''    openride_ui_panel(ui, layout.panel, true);\n    openride_ui_icon_draw(ui,\n                          OPENRIDE_UI_ICON_SEARCH,\n                          openride_ui_rect(layout.panel.x + 15.0f,\n                                           layout.panel.y + 12.0f,\n                                           22.0f,\n                                           22.0f),\n                          ui->theme.primary,\n                          1.7f);\n    openride_ui_text(ui,\n                     layout.title,\n''',
        "search header icon",
    )
    text = replace_once(
        text,
        '''    openride_ui_panel(ui, layout.query, false);\n    char query_text[96];\n''',
        '''    openride_ui_panel(ui, layout.query, false);\n    openride_ui_icon_draw(ui,\n                          OPENRIDE_UI_ICON_SEARCH,\n                          openride_ui_rect(layout.query.x + 13.0f,\n                                           layout.query.y + (layout.query.h - 20.0f) * 0.5f,\n                                           20.0f,\n                                           20.0f),\n                          ui->theme.text_secondary,\n                          1.55f);\n    char query_text[96];\n''',
        "search query icon",
    )
    text = replace_once(
        text,
        "                     openride_ui_inset_xy(layout.query, 12.0f, 4.0f),\n",
        '''                     openride_ui_rect(layout.query.x + 43.0f,\n                                              layout.query.y + 4.0f,\n                                              layout.query.w - 55.0f,\n                                              layout.query.h - 8.0f),\n''',
        "search query text inset",
    )
    text = replace_once(
        text,
        '''        (void)openride_ui_button(ui,\n                                 search_row_id(i),\n                                 layout.rows[i],\n                                 "",\n                                 OPENRIDE_UI_BUTTON_SECONDARY,\n                                 true,\n                                 i == state->selected);\n\n        const OpenRideUIRect name_rect = openride_ui_rect(\n            layout.rows[i].x + 12.0f,\n            layout.rows[i].y + 3.0f,\n            layout.rows[i].w - 24.0f,\n            layout.rows[i].h * 0.52f);\n        const OpenRideUIRect secondary_rect = openride_ui_rect(\n            layout.rows[i].x + 12.0f,\n            layout.rows[i].y + layout.rows[i].h * 0.50f,\n            layout.rows[i].w - 24.0f,\n            layout.rows[i].h * 0.46f);\n''',
        '''        const bool selected = i == state->selected;\n        (void)openride_ui_button(ui,\n                                 search_row_id(i),\n                                 layout.rows[i],\n                                 "",\n                                 OPENRIDE_UI_BUTTON_GHOST,\n                                 true,\n                                 selected);\n        openride_ui_icon_draw(ui,\n                              OPENRIDE_UI_ICON_LOCATION,\n                              openride_ui_rect(layout.rows[i].x + 12.0f,\n                                               layout.rows[i].y\n                                                   + (layout.rows[i].h - 19.0f) * 0.5f,\n                                               19.0f,\n                                               19.0f),\n                              selected ? ui->theme.primary : ui->theme.text_secondary,\n                              1.5f);\n\n        const OpenRideUIRect name_rect = openride_ui_rect(\n            layout.rows[i].x + 42.0f,\n            layout.rows[i].y + 3.0f,\n            layout.rows[i].w - 54.0f,\n            layout.rows[i].h * 0.52f);\n        const OpenRideUIRect secondary_rect = openride_ui_rect(\n            layout.rows[i].x + 42.0f,\n            layout.rows[i].y + layout.rows[i].h * 0.50f,\n            layout.rows[i].w - 54.0f,\n            layout.rows[i].h * 0.46f);\n''',
        "search result cards",
    )
    return text


def polish_route(text):
    text = replace_once(
        text,
        '#include "openride/ui_route_panel.h"\n',
        '#include "openride/ui_route_panel.h"\n#include "openride/ui_icon.h"\n',
        "route icon include",
    )
    text = replace_once(
        text,
        '''#define OPENRIDE_UI_ROUTE_ITEMS 6U\n#define OPENRIDE_UI_ROUTE_MARGIN 8.0f\n#define OPENRIDE_UI_ROUTE_GAP 8.0f\n#define OPENRIDE_UI_ROUTE_HEADER 56.0f\n#define OPENRIDE_UI_ROUTE_BACK_HEIGHT 54.0f\n#define OPENRIDE_UI_ROUTE_HINT_HEIGHT 24.0f\n#define OPENRIDE_UI_ROUTE_MAX_ROW_HEIGHT 64.0f\n''',
        '''#define OPENRIDE_UI_ROUTE_ITEMS 6U\n#define OPENRIDE_UI_ROUTE_MARGIN 12.0f\n#define OPENRIDE_UI_ROUTE_GAP 6.0f\n#define OPENRIDE_UI_ROUTE_HEADER 62.0f\n#define OPENRIDE_UI_ROUTE_BACK_HEIGHT 46.0f\n#define OPENRIDE_UI_ROUTE_HINT_HEIGHT 20.0f\n#define OPENRIDE_UI_ROUTE_MAX_ROW_HEIGHT 54.0f\n#define OPENRIDE_UI_ROUTE_MAX_WIDTH 410.0f\n#define OPENRIDE_UI_ROUTE_MAX_HEIGHT 500.0f\n''',
        "route visual constants",
    )
    text = replace_once(
        text,
        '''    layout.panel = safe;\n    layout.title = openride_ui_rect(safe.x + 14.0f,\n                                    safe.y + 8.0f,\n                                    safe.w - 28.0f,\n                                    28.0f);\n    layout.subtitle = openride_ui_rect(safe.x + 14.0f,\n                                       safe.y + 34.0f,\n                                       safe.w - 28.0f,\n                                       18.0f);\n''',
        '''    float panel_w = safe.w < OPENRIDE_UI_ROUTE_MAX_WIDTH\n        ? safe.w : OPENRIDE_UI_ROUTE_MAX_WIDTH;\n    float panel_h = safe.h < OPENRIDE_UI_ROUTE_MAX_HEIGHT\n        ? safe.h : OPENRIDE_UI_ROUTE_MAX_HEIGHT;\n    layout.panel = openride_ui_rect(safe.x + (safe.w - panel_w) * 0.5f,\n                                    safe.y + (safe.h - panel_h) * 0.5f,\n                                    panel_w,\n                                    panel_h);\n    safe = layout.panel;\n    layout.title = openride_ui_rect(safe.x + 48.0f,\n                                    safe.y + 8.0f,\n                                    safe.w - 62.0f,\n                                    28.0f);\n    layout.subtitle = openride_ui_rect(safe.x + 48.0f,\n                                       safe.y + 34.0f,\n                                       safe.w - 62.0f,\n                                       18.0f);\n''',
        "route centered panel",
    )
    text = replace_once(
        text,
        "    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 115);\n",
        "    SDL_SetRenderDrawColor(ui->renderer, 4, 7, 8, 138);\n",
        "route backdrop",
    )
    text = replace_once(
        text,
        '''    openride_ui_panel(ui, layout.panel, true);\n    openride_ui_text(ui,\n                     layout.title,\n                     "ITINERAIRE",\n''',
        '''    openride_ui_panel(ui, layout.panel, true);\n    openride_ui_icon_draw(ui,\n                          OPENRIDE_UI_ICON_ROUTE,\n                          openride_ui_rect(layout.panel.x + 17.0f,\n                                           layout.panel.y + 14.0f,\n                                           22.0f,\n                                           22.0f),\n                          ui->theme.primary,\n                          1.7f);\n    openride_ui_text(ui,\n                     layout.title,\n                     "Itineraire",\n''',
        "route header icon",
    )
    old_loop = '''    OpenRideUIRoutePanelAction clicked = OPENRIDE_UI_ROUTE_PANEL_NONE;\n    for (uint32_t i = 0U; i < OPENRIDE_UI_ROUTE_ITEMS; ++i) {\n        const bool calculate = i == OPENRIDE_UI_ROUTE_ITEMS - 1U;\n        if (openride_ui_button(ui,\n                               route_item_id(i),\n                               layout.items[i],\n                               labels[i],\n                               calculate && ready\n                                   ? OPENRIDE_UI_BUTTON_PRIMARY\n                                   : OPENRIDE_UI_BUTTON_SECONDARY,\n                               !calculate || ready,\n                               false)) {\n            clicked = (OpenRideUIRoutePanelAction)(\n                OPENRIDE_UI_ROUTE_PANEL_GPS_START + (int)i);\n        }\n    }\n'''
    new_loop = '''    static const OpenRideUIIcon icons[OPENRIDE_UI_ROUTE_ITEMS] = {\n        OPENRIDE_UI_ICON_GPS,\n        OPENRIDE_UI_ICON_SEARCH,\n        OPENRIDE_UI_ICON_MAP,\n        OPENRIDE_UI_ICON_SEARCH,\n        OPENRIDE_UI_ICON_MAP,\n        OPENRIDE_UI_ICON_ROUTE\n    };\n\n    OpenRideUIRoutePanelAction clicked = OPENRIDE_UI_ROUTE_PANEL_NONE;\n    for (uint32_t i = 0U; i < OPENRIDE_UI_ROUTE_ITEMS; ++i) {\n        const bool calculate = i == OPENRIDE_UI_ROUTE_ITEMS - 1U;\n        const bool enabled = !calculate || ready;\n        if (openride_ui_button(ui,\n                               route_item_id(i),\n                               layout.items[i],\n                               "",\n                               calculate && ready\n                                   ? OPENRIDE_UI_BUTTON_PRIMARY\n                                   : OPENRIDE_UI_BUTTON_GHOST,\n                               enabled,\n                               false)) {\n            clicked = (OpenRideUIRoutePanelAction)(\n                OPENRIDE_UI_ROUTE_PANEL_GPS_START + (int)i);\n        }\n        OpenRideUIColor tint = enabled ? ui->theme.text_secondary : ui->theme.disabled;\n        if (calculate && ready) tint = ui->theme.text;\n        openride_ui_icon_draw(ui,\n                              icons[i],\n                              openride_ui_rect(layout.items[i].x + 13.0f,\n                                               layout.items[i].y\n                                                   + (layout.items[i].h - 21.0f) * 0.5f,\n                                               21.0f,\n                                               21.0f),\n                              tint,\n                              1.6f);\n        openride_ui_text_color(ui,\n                               openride_ui_rect(layout.items[i].x + 45.0f,\n                                                layout.items[i].y,\n                                                layout.items[i].w - 56.0f,\n                                                layout.items[i].h),\n                               labels[i],\n                               OPENRIDE_UI_TEXT_BODY,\n                               OPENRIDE_UI_TEXT_ALIGN_LEFT,\n                               enabled ? ui->theme.text : ui->theme.text_secondary);\n    }\n'''
    text = replace_once(text, old_loop, new_loop, "route icon rows")
    return text


def center_panel(text, max_width, max_height, label):
    old = '''    layout.panel = safe;\n    layout.title = openride_ui_rect(safe.x + 14.0f,\n                                    safe.y + 8.0f,\n                                    safe.w - 28.0f,\n                                    28.0f);\n    layout.subtitle = openride_ui_rect(safe.x + 14.0f,\n                                       safe.y + 34.0f,\n                                       safe.w - 28.0f,\n                                       18.0f);\n'''
    new = '''    const float panel_w = safe.w < %s ? safe.w : %s;\n    const float panel_h = safe.h < %s ? safe.h : %s;\n    layout.panel = openride_ui_rect(safe.x + (safe.w - panel_w) * 0.5f,\n                                    safe.y + (safe.h - panel_h) * 0.5f,\n                                    panel_w,\n                                    panel_h);\n    safe = layout.panel;\n    layout.title = openride_ui_rect(safe.x + 48.0f,\n                                    safe.y + 8.0f,\n                                    safe.w - 62.0f,\n                                    28.0f);\n    layout.subtitle = openride_ui_rect(safe.x + 48.0f,\n                                       safe.y + 34.0f,\n                                       safe.w - 62.0f,\n                                       18.0f);\n''' % (max_width, max_width, max_height, max_height)
    return replace_once(text, old, new, label)


def polish_regions(text):
    text = replace_once(
        text,
        '#include "openride/ui_regions_panel.h"\n',
        '#include "openride/ui_regions_panel.h"\n#include "openride/ui_icon.h"\n',
        "regions icon include",
    )
    text = replace_once(
        text,
        "#define OPENRIDE_UI_REGIONS_BACK_HEIGHT 54.0f\n",
        '''#define OPENRIDE_UI_REGIONS_BACK_HEIGHT 48.0f\n#define OPENRIDE_UI_REGIONS_MAX_WIDTH 410.0f\n#define OPENRIDE_UI_REGIONS_MAX_HEIGHT 470.0f\n''',
        "regions constants",
    )
    text = center_panel(text,
                        "OPENRIDE_UI_REGIONS_MAX_WIDTH",
                        "OPENRIDE_UI_REGIONS_MAX_HEIGHT",
                        "regions centered panel")
    text = replace_once(
        text,
        "    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 115);\n",
        "    SDL_SetRenderDrawColor(ui->renderer, 4, 7, 8, 138);\n",
        "regions backdrop",
    )
    text = replace_once(
        text,
        '''    openride_ui_panel(ui, layout.panel, true);\n    openride_ui_text(ui,\n                     layout.title,\n                     "CARTES HORS LIGNE",\n''',
        '''    openride_ui_panel(ui, layout.panel, true);\n    openride_ui_icon_draw(ui,\n                          OPENRIDE_UI_ICON_DOWNLOAD,\n                          openride_ui_rect(layout.panel.x + 17.0f,\n                                           layout.panel.y + 14.0f,\n                                           22.0f,\n                                           22.0f),\n                          ui->theme.primary,\n                          1.7f);\n    openride_ui_text(ui,\n                     layout.title,\n                     "Cartes hors ligne",\n''',
        "regions header icon",
    )
    return text


def polish_settings(text):
    text = replace_once(
        text,
        '#include "openride/ui_settings_panel.h"\n',
        '#include "openride/ui_settings_panel.h"\n#include "openride/ui_icon.h"\n',
        "settings icon include",
    )
    text = replace_once(
        text,
        "#define OPENRIDE_UI_SETTINGS_BACK_HEIGHT 54.0f\n#define OPENRIDE_UI_SETTINGS_MAX_ROW_HEIGHT 64.0f\n",
        '''#define OPENRIDE_UI_SETTINGS_BACK_HEIGHT 48.0f\n#define OPENRIDE_UI_SETTINGS_MAX_ROW_HEIGHT 54.0f\n#define OPENRIDE_UI_SETTINGS_MAX_WIDTH 420.0f\n#define OPENRIDE_UI_SETTINGS_MAX_HEIGHT 620.0f\n''',
        "settings constants",
    )
    text = center_panel(text,
                        "OPENRIDE_UI_SETTINGS_MAX_WIDTH",
                        "OPENRIDE_UI_SETTINGS_MAX_HEIGHT",
                        "settings centered panel")
    text = replace_once(
        text,
        "    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 115);\n",
        "    SDL_SetRenderDrawColor(ui->renderer, 4, 7, 8, 138);\n",
        "settings backdrop",
    )
    text = replace_once(
        text,
        '''    openride_ui_panel(ui, layout.panel, true);\n    openride_ui_text(ui,\n                     layout.title,\n                     "PARAMETRES",\n''',
        '''    openride_ui_panel(ui, layout.panel, true);\n    openride_ui_icon_draw(ui,\n                          OPENRIDE_UI_ICON_SETTINGS,\n                          openride_ui_rect(layout.panel.x + 17.0f,\n                                           layout.panel.y + 14.0f,\n                                           22.0f,\n                                           22.0f),\n                          ui->theme.primary,\n                          1.7f);\n    openride_ui_text(ui,\n                     layout.title,\n                     "Parametres",\n''',
        "settings header icon",
    )
    text = replace_once(
        text,
        "                               OPENRIDE_UI_BUTTON_SECONDARY,\n                               true,\n                               selected)) {\n",
        "                               OPENRIDE_UI_BUTTON_GHOST,\n                               true,\n                               selected)) {\n",
        "settings light rows",
    )
    return text


def polish_drive(text):
    text = replace_once(
        text,
        '#include "openride/ui_drive_hud.h"\n',
        '#include "openride/ui_drive_hud.h"\n#include "openride/ui_icon.h"\n',
        "drive icon include",
    )
    text = replace_once(
        text,
        '''#define OPENRIDE_UI_DRIVE_MARGIN 6.0f\n#define OPENRIDE_UI_DRIVE_TOP_HEIGHT 86.0f\n#define OPENRIDE_UI_DRIVE_FOLLOWING_HEIGHT 28.0f\n#define OPENRIDE_UI_DRIVE_STATS_HEIGHT 54.0f\n#define OPENRIDE_UI_DRIVE_CONTROLS_HEIGHT 62.0f\n#define OPENRIDE_UI_DRIVE_CONTROL_COUNT 4U\n''',
        '''#define OPENRIDE_UI_DRIVE_MARGIN 10.0f\n#define OPENRIDE_UI_DRIVE_TOP_HEIGHT 78.0f\n#define OPENRIDE_UI_DRIVE_FOLLOWING_HEIGHT 26.0f\n#define OPENRIDE_UI_DRIVE_STATS_HEIGHT 52.0f\n#define OPENRIDE_UI_DRIVE_CONTROLS_HEIGHT 64.0f\n#define OPENRIDE_UI_DRIVE_ATTRIBUTION_HEIGHT 14.0f\n#define OPENRIDE_UI_DRIVE_CONTROL_COUNT 4U\n''',
        "drive visual constants",
    )
    text = replace_once(
        text,
        '''    layout.controls = openride_ui_rect(\n        safe.x,\n        safe.y + safe.h - OPENRIDE_UI_DRIVE_CONTROLS_HEIGHT,\n        safe.w,\n        OPENRIDE_UI_DRIVE_CONTROLS_HEIGHT);\n''',
        '''    layout.controls = openride_ui_rect(\n        safe.x,\n        safe.y + safe.h - OPENRIDE_UI_DRIVE_ATTRIBUTION_HEIGHT\n            - OPENRIDE_UI_DRIVE_CONTROLS_HEIGHT,\n        safe.w,\n        OPENRIDE_UI_DRIVE_CONTROLS_HEIGHT);\n''',
        "drive attribution clearance",
    )
    text = text.replace("SDL_SetRenderDrawColor(renderer, 255, 214, 83, 255);",
                        "SDL_SetRenderDrawColor(renderer, 47, 198, 181, 255);")
    text = replace_once(
        text,
        '''    SDL_SetRenderDrawColor(renderer, 13, 17, 21, 232);\n    SDL_RenderFillRect(renderer, &top);\n    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 65);\n''',
        '''    SDL_SetRenderDrawColor(renderer, 13, 16, 18, 238);\n    SDL_RenderFillRect(renderer, &top);\n    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 30);\n''',
        "drive top surface",
    )
    text = replace_once(
        text,
        '''        SDL_SetRenderDrawColor(renderer, 20, 25, 30, 224);\n        SDL_RenderFillRect(renderer, &following);\n        SDL_SetRenderDrawColor(renderer, 255, 214, 83, 85);\n        SDL_RenderRect(renderer, &following);\n        SDL_SetRenderDrawColor(renderer, 245, 223, 153, 255);\n''',
        '''        SDL_SetRenderDrawColor(renderer, 22, 26, 29, 226);\n        SDL_RenderFillRect(renderer, &following);\n        SDL_SetRenderDrawColor(renderer, 47, 198, 181, 70);\n        SDL_RenderRect(renderer, &following);\n        SDL_SetRenderDrawColor(renderer, 190, 226, 221, 255);\n''',
        "drive following surface",
    )
    text = replace_once(
        text,
        '''    SDL_SetRenderDrawColor(renderer, 13, 17, 21, 225);\n    SDL_RenderFillRect(renderer, &stats);\n    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 50);\n''',
        '''    SDL_SetRenderDrawColor(renderer, 22, 26, 29, 232);\n    SDL_RenderFillRect(renderer, &stats);\n    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 28);\n''',
        "drive stats surface",
    )
    old_attr = '''    if (state->show_attribution) {\n        SDL_SetRenderDrawColor(renderer, 65, 68, 70, 255);\n        drive_draw_scaled_text(renderer,\n                               stats.x + 3.0f * ui_scale,\n                               stats.y - 13.0f * ui_scale,\n                               ui_scale > 1.4f ? 1.4f : ui_scale,\n                               "(c) OpenStreetMap contributors | ODbL");\n    }\n\n'''
    text = replace_once(text, old_attr, "", "drive old attribution removal")
    text = replace_once(
        text,
        '''    SDL_SetRenderDrawColor(renderer, 13, 17, 21, 238);\n    SDL_RenderFillRect(renderer, &controls);\n    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 65);\n    SDL_RenderRect(renderer, &controls);\n\n    const char *labels[OPENRIDE_UI_DRIVE_CONTROL_COUNT] = {\n''',
        '''    SDL_SetRenderDrawColor(renderer, 13, 16, 18, 242);\n    SDL_RenderFillRect(renderer, &controls);\n    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 30);\n    SDL_RenderRect(renderer, &controls);\n\n    if (state->show_attribution) {\n        SDL_SetRenderDrawColor(renderer, 155, 163, 167, 70);\n        drive_draw_scaled_text(renderer,\n                               controls.x + 4.0f * ui_scale,\n                               controls.y + controls.h + 2.0f * ui_scale,\n                               ui_scale > 1.15f ? 1.15f : ui_scale,\n                               "(c) OpenStreetMap contributors | ODbL");\n    }\n\n    const char *labels[OPENRIDE_UI_DRIVE_CONTROL_COUNT] = {\n''',
        "drive controls and attribution",
    )
    old_controls = '''    const float item_width = controls.w / (float)OPENRIDE_UI_DRIVE_CONTROL_COUNT;\n    const float control_scale = ui_scale > 2.4f ? 2.4f : ui_scale;\n    for (uint32_t i = 0U; i < OPENRIDE_UI_DRIVE_CONTROL_COUNT; ++i) {\n        const float item_x = controls.x + item_width * (float)i;\n        if (i > 0U) {\n            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 38);\n            SDL_RenderLine(renderer,\n                           item_x,\n                           controls.y + 10.0f * ui_scale,\n                           item_x,\n                           controls.y + controls.h - 10.0f * ui_scale);\n        }\n        const float label_width =\n            (float)drive_glyph_count(labels[i]) * 8.0f * control_scale;\n        const float label_height = 8.0f * control_scale;\n        SDL_SetRenderDrawColor(renderer, 243, 245, 247, 255);\n        drive_draw_scaled_text(renderer,\n                               item_x + (item_width - label_width) * 0.5f,\n                               controls.y + (controls.h - label_height) * 0.5f,\n                               control_scale,\n                               labels[i]);\n    }\n'''
    new_controls = '''    static const OpenRideUIIcon control_icons[OPENRIDE_UI_DRIVE_CONTROL_COUNT] = {\n        OPENRIDE_UI_ICON_MAP,\n        OPENRIDE_UI_ICON_LOCATION,\n        OPENRIDE_UI_ICON_COMPASS,\n        OPENRIDE_UI_ICON_GPS\n    };\n    for (uint32_t i = 0U; i < OPENRIDE_UI_DRIVE_CONTROL_COUNT; ++i) {\n        const OpenRideUIRect item = layout.control_items[i];\n        const OpenRideUIColor tint = i == 1U\n            ? ui->theme.primary\n            : ui->theme.text_secondary;\n        openride_ui_icon_draw(ui,\n                              control_icons[i],\n                              openride_ui_rect(item.x + (item.w - 22.0f) * 0.5f,\n                                               item.y + 8.0f,\n                                               22.0f,\n                                               22.0f),\n                              tint,\n                              1.65f);\n        openride_ui_text_color(ui,\n                               openride_ui_rect(item.x + 2.0f,\n                                                item.y + item.h - 22.0f,\n                                                item.w - 4.0f,\n                                                16.0f),\n                               labels[i],\n                               OPENRIDE_UI_TEXT_CAPTION,\n                               OPENRIDE_UI_TEXT_ALIGN_CENTER,\n                               tint);\n    }\n'''
    text = replace_once(text, old_controls, new_controls, "drive SVG controls")
    return text


def main():
    originals = {name: path.read_text(encoding="utf-8") for name, path in FILES.items()}
    prepared = {
        "search": polish_search(originals["search"]),
        "route": polish_route(originals["route"]),
        "regions": polish_regions(originals["regions"]),
        "settings": polish_settings(originals["settings"]),
        "drive": polish_drive(originals["drive"]),
    }

    required = {
        "search": ("OPENRIDE_UI_ICON_SEARCH", "OPENRIDE_UI_ICON_LOCATION", "OPENRIDE_UI_SEARCH_MAX_WIDTH"),
        "route": ("OPENRIDE_UI_ICON_ROUTE", "OPENRIDE_UI_ROUTE_MAX_WIDTH", "static const OpenRideUIIcon icons"),
        "regions": ("OPENRIDE_UI_ICON_DOWNLOAD", "OPENRIDE_UI_REGIONS_MAX_WIDTH"),
        "settings": ("OPENRIDE_UI_ICON_SETTINGS", "OPENRIDE_UI_SETTINGS_MAX_WIDTH"),
        "drive": ("OPENRIDE_UI_DRIVE_ATTRIBUTION_HEIGHT", "control_icons", "OpenStreetMap contributors"),
    }
    for name, tokens in required.items():
        for token in tokens:
            if token not in prepared[name]:
                raise RuntimeError("%s: generated polish lost required token %s" % (name, token))
        if prepared[name] == originals[name]:
            raise RuntimeError("%s: no change generated" % name)

    # Transactional write point.
    for name, path in FILES.items():
        path.write_text(prepared[name], encoding="utf-8")

    print("OK: OpenRide UI V3.0.1 polish applied")
    print("Changed: search, route, offline maps, settings, Drive HUD")
    print("Drive controls now use scalable SVG icons")
    print("OSM attribution in Drive mode moved below the control bar")
    print("Next: git diff --check && git diff --stat")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        raise SystemExit(1)
