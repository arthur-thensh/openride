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

        OpenRideUIColor tint = ui->theme.text_secondary;
        if (!enabled) tint.a = 150U;
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
}

