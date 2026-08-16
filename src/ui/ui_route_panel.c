#include "openride/ui_route_panel.h"
#include "openride/ui_icon.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#define OPENRIDE_UI_ROUTE_ITEMS 6U
#define OPENRIDE_UI_ROUTE_MARGIN 12.0f
#define OPENRIDE_UI_ROUTE_GAP 6.0f
#define OPENRIDE_UI_ROUTE_HEADER 62.0f
#define OPENRIDE_UI_ROUTE_BACK_HEIGHT 46.0f
#define OPENRIDE_UI_ROUTE_HINT_HEIGHT 20.0f
#define OPENRIDE_UI_ROUTE_MAX_ROW_HEIGHT 54.0f
#define OPENRIDE_UI_ROUTE_MAX_WIDTH 410.0f
#define OPENRIDE_UI_ROUTE_MAX_HEIGHT 500.0f

static OpenRideUIID route_item_id(uint32_t index)
{
    static const char *ids[OPENRIDE_UI_ROUTE_ITEMS] = {
        "route-gps-start",
        "route-search-start",
        "route-map-start",
        "route-search-destination",
        "route-map-destination",
        "route-calculate"
    };
    if (index >= OPENRIDE_UI_ROUTE_ITEMS) return 0U;
    return OPENRIDE_UI_ID(ids[index]);
}

OpenRideUIRoutePanelLayout openride_ui_route_panel_layout(
    const OpenRideUIContext *ui)
{
    OpenRideUIRoutePanelLayout layout = {0};
    if (!ui) return layout;

    OpenRideUIRect safe = openride_ui_safe_rect(ui);
    safe = openride_ui_inset(safe, OPENRIDE_UI_ROUTE_MARGIN);
    if (safe.w < 120.0f || safe.h < 220.0f) return layout;

    float panel_w = safe.w < OPENRIDE_UI_ROUTE_MAX_WIDTH
        ? safe.w : OPENRIDE_UI_ROUTE_MAX_WIDTH;
    float panel_h = safe.h < OPENRIDE_UI_ROUTE_MAX_HEIGHT
        ? safe.h : OPENRIDE_UI_ROUTE_MAX_HEIGHT;
    layout.panel = openride_ui_rect(safe.x + (safe.w - panel_w) * 0.5f,
                                    safe.y + (safe.h - panel_h) * 0.5f,
                                    panel_w,
                                    panel_h);
    safe = layout.panel;
    layout.title = openride_ui_rect(safe.x + 48.0f,
                                    safe.y + 8.0f,
                                    safe.w - 62.0f,
                                    28.0f);
    layout.subtitle = openride_ui_rect(safe.x + 48.0f,
                                       safe.y + 34.0f,
                                       safe.w - 62.0f,
                                       18.0f);

    layout.back = openride_ui_rect(
        safe.x + OPENRIDE_UI_ROUTE_GAP,
        safe.y + safe.h - OPENRIDE_UI_ROUTE_BACK_HEIGHT
            - OPENRIDE_UI_ROUTE_GAP,
        safe.w - OPENRIDE_UI_ROUTE_GAP * 2.0f,
        OPENRIDE_UI_ROUTE_BACK_HEIGHT);

    layout.hint = openride_ui_rect(
        safe.x + 14.0f,
        layout.back.y - OPENRIDE_UI_ROUTE_GAP - OPENRIDE_UI_ROUTE_HINT_HEIGHT,
        safe.w - 28.0f,
        OPENRIDE_UI_ROUTE_HINT_HEIGHT);

    const float rows_top = safe.y + OPENRIDE_UI_ROUTE_HEADER
        + OPENRIDE_UI_ROUTE_GAP;
    float available = layout.hint.y - OPENRIDE_UI_ROUTE_GAP - rows_top;
    available -= OPENRIDE_UI_ROUTE_GAP * (float)(OPENRIDE_UI_ROUTE_ITEMS - 1U);
    float row_height = available / (float)OPENRIDE_UI_ROUTE_ITEMS;
    if (row_height > OPENRIDE_UI_ROUTE_MAX_ROW_HEIGHT) {
        row_height = OPENRIDE_UI_ROUTE_MAX_ROW_HEIGHT;
    }
    if (row_height < 1.0f) row_height = 1.0f;

    for (uint32_t i = 0U; i < OPENRIDE_UI_ROUTE_ITEMS; ++i) {
        layout.items[i] = openride_ui_rect(
            safe.x + OPENRIDE_UI_ROUTE_GAP,
            rows_top + (row_height + OPENRIDE_UI_ROUTE_GAP) * (float)i,
            safe.w - OPENRIDE_UI_ROUTE_GAP * 2.0f,
            row_height);
    }
    return layout;
}

OpenRideUIRoutePanelAction openride_ui_route_panel_hit_test(
    const OpenRideUIContext *ui,
    double x_px,
    double y_px)
{
    if (!ui) return OPENRIDE_UI_ROUTE_PANEL_NONE;
    const double scale = ui->scale > 0.0f ? (double)ui->scale : 1.0;
    const double x = x_px / scale;
    const double y = y_px / scale;
    const OpenRideUIRoutePanelLayout layout = openride_ui_route_panel_layout(ui);

    for (uint32_t i = 0U; i < OPENRIDE_UI_ROUTE_ITEMS; ++i) {
        if (openride_ui_point_in_rect(x, y, layout.items[i])) {
            return (OpenRideUIRoutePanelAction)(
                OPENRIDE_UI_ROUTE_PANEL_GPS_START + (int)i);
        }
    }
    if (openride_ui_point_in_rect(x, y, layout.back)) {
        return OPENRIDE_UI_ROUTE_PANEL_BACK;
    }
    return OPENRIDE_UI_ROUTE_PANEL_NONE;
}

OpenRideUIRoutePanelAction openride_ui_route_panel_draw(
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

    SDL_FRect screen = {
        0.0f,
        0.0f,
        (float)ui->viewport_width,
        (float)ui->viewport_height
    };
    SDL_SetRenderDrawColor(ui->renderer, 4, 7, 8, 138);
    SDL_RenderFillRect(ui->renderer, &screen);

    openride_ui_panel(ui, layout.panel, true);
    openride_ui_icon_draw(ui,
                          OPENRIDE_UI_ICON_ROUTE,
                          openride_ui_rect(layout.panel.x + 17.0f,
                                           layout.panel.y + 14.0f,
                                           22.0f,
                                           22.0f),
                          ui->theme.primary,
                          1.7f);
    openride_ui_text(ui,
                     layout.title,
                     "Itineraire",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    char subtitle[128];
    snprintf(subtitle,
             sizeof(subtitle),
             "Depart %s  |  Arrivee %s",
             state->has_start ? "OK" : "a choisir",
             state->has_destination ? "OK" : "a choisir");
    openride_ui_text(ui,
                     layout.subtitle,
                     subtitle,
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    char gps_label[96];
    if (state->gps_valid && isfinite(state->gps_accuracy_m)) {
        snprintf(gps_label,
                 sizeof(gps_label),
                 "Depart : ma position GPS (%.0f m)",
                 state->gps_accuracy_m);
    } else {
        snprintf(gps_label,
                 sizeof(gps_label),
                 "Depart : utiliser ma position GPS");
    }

    const bool ready = state->has_start && state->has_destination;
    const char *labels[OPENRIDE_UI_ROUTE_ITEMS] = {
        gps_label,
        "Depart : rechercher un lieu",
        "Depart : choisir sur la carte",
        "Arrivee : rechercher un lieu",
        "Arrivee : choisir sur la carte",
        ready
            ? "CALCULER L'ITINERAIRE"
            : "Calculer - depart et arrivee requis"
    };

    static const OpenRideUIIcon icons[OPENRIDE_UI_ROUTE_ITEMS] = {
        OPENRIDE_UI_ICON_GPS,
        OPENRIDE_UI_ICON_SEARCH,
        OPENRIDE_UI_ICON_MAP,
        OPENRIDE_UI_ICON_SEARCH,
        OPENRIDE_UI_ICON_MAP,
        OPENRIDE_UI_ICON_ROUTE
    };

    OpenRideUIRoutePanelAction clicked = OPENRIDE_UI_ROUTE_PANEL_NONE;
    for (uint32_t i = 0U; i < OPENRIDE_UI_ROUTE_ITEMS; ++i) {
        const bool calculate = i == OPENRIDE_UI_ROUTE_ITEMS - 1U;
        const bool enabled = !calculate || ready;
        if (openride_ui_button(ui,
                               route_item_id(i),
                               layout.items[i],
                               "",
                               calculate && ready
                                   ? OPENRIDE_UI_BUTTON_PRIMARY
                                   : OPENRIDE_UI_BUTTON_GHOST,
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
                                                   + (layout.items[i].h - 21.0f) * 0.5f,
                                               21.0f,
                                               21.0f),
                              tint,
                              1.6f);
        openride_ui_text_color(ui,
                               openride_ui_rect(layout.items[i].x + 45.0f,
                                                layout.items[i].y,
                                                layout.items[i].w - 56.0f,
                                                layout.items[i].h),
                               labels[i],
                               OPENRIDE_UI_TEXT_BODY,
                               OPENRIDE_UI_TEXT_ALIGN_LEFT,
                               enabled ? ui->theme.text : ui->theme.text_secondary);
    }

    openride_ui_text(ui,
                     layout.hint,
                     "Tu peux aussi fermer et choisir les points sur la carte",
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

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
}
