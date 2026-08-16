#include "openride/ui_map_overlay.h"
#include "openride/ui_icon.h"
#include "openride/ui_toolbar.h"

#include <stddef.h>

#define OPENRIDE_UI_MAP_OVERLAY_MARGIN 12.0f
#define OPENRIDE_UI_MAP_OVERLAY_PANEL_MAX_WIDTH 500.0f
#define OPENRIDE_UI_MAP_OVERLAY_COMPACT_MAX_WIDTH 340.0f
#define OPENRIDE_UI_MAP_OVERLAY_LINE_HEIGHT 16.0f

static float minf_openride(float a, float b)
{
    return a < b ? a : b;
}

static void draw_attribution(OpenRideUIContext *ui,
                             const OpenRideUIMapOverlayState *state)
{
    if (!ui || !state || !state->attribution || !state->attribution[0]) return;

    const OpenRideUIRect safe = openride_ui_safe_rect(ui);
    OpenRideUIRect attribution = {0};

    if (state->compact) {
        const OpenRideUIToolbarLayout toolbar = openride_ui_toolbar_layout(ui);
        if (toolbar.bar.w <= 0.0f || toolbar.bar.h <= 0.0f) return;

        const float y = toolbar.bar.y + toolbar.bar.h + 1.0f;
        const float bottom = safe.y + safe.h;
        if (bottom <= y + 6.0f) return;
        attribution = openride_ui_rect(toolbar.bar.x + 4.0f,
                                       y,
                                       toolbar.bar.w - 8.0f,
                                       bottom - y);
    } else {
        attribution = openride_ui_rect(safe.x + 8.0f,
                                       safe.y + safe.h - 16.0f,
                                       minf_openride(safe.w - 16.0f, 360.0f),
                                       14.0f);
    }

    /* Legal attribution stays readable but should not compete with navigation.
       No backing surface: it should visually disappear into the map. */
    OpenRideUIColor tint = ui->theme.text_secondary;
    tint.a = 72U;
    openride_ui_text_color(ui,
                           attribution,
                           state->attribution,
                           OPENRIDE_UI_TEXT_CAPTION,
                           state->compact
                               ? OPENRIDE_UI_TEXT_ALIGN_CENTER
                               : OPENRIDE_UI_TEXT_ALIGN_LEFT,
                           tint);
}

static void draw_compact(OpenRideUIContext *ui,
                         const OpenRideUIMapOverlayState *state,
                         OpenRideUIRect safe)
{
    const float panel_w = minf_openride(safe.w,
                                        OPENRIDE_UI_MAP_OVERLAY_COMPACT_MAX_WIDTH);
    const float panel_h = state->route_ready ? 78.0f : 58.0f;
    OpenRideUIRect panel = openride_ui_rect(safe.x,
                                            safe.y,
                                            panel_w,
                                            panel_h);
    openride_ui_panel(ui, panel, true);

    const OpenRideUIRect icon_rect = openride_ui_rect(panel.x + 13.0f,
                                                       panel.y + 12.0f,
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

    openride_ui_text(ui,
                     openride_ui_rect(panel.x + 48.0f,
                                      panel.y + 7.0f,
                                      panel.w - 60.0f,
                                      22.0f),
                     state->title ? state->title : "OpenRide",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    openride_ui_text(ui,
                     openride_ui_rect(panel.x + 48.0f,
                                      panel.y + 28.0f,
                                      panel.w - 60.0f,
                                      16.0f),
                     state->summary ? state->summary : "pret",
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    if (state->route_ready) {
        openride_ui_text_color(ui,
                               openride_ui_rect(panel.x + 13.0f,
                                                panel.y + 54.0f,
                                                panel.w - 26.0f,
                                                15.0f),
                               state->route_ready_text
                                   ? state->route_ready_text
                                   : "TRAJET PRET - DEMARRER",
                               OPENRIDE_UI_TEXT_CAPTION,
                               OPENRIDE_UI_TEXT_ALIGN_LEFT,
                               ui->theme.primary);
    }

    draw_attribution(ui, state);
}

static void draw_distance_card(OpenRideUIContext *ui,
                               const OpenRideUIMapOverlayState *state,
                               OpenRideUIRect safe,
                               OpenRideUIRect main_panel)
{
    if (!state->show_distance) return;

    const float card_w = minf_openride(230.0f, safe.w);
    const float card_h = state->duration_text && state->duration_text[0]
        ? 84.0f
        : 68.0f;

    float x = safe.x;
    float y = main_panel.y + main_panel.h + OPENRIDE_UI_MAP_OVERLAY_MARGIN;
    if (safe.w >= main_panel.w + card_w + OPENRIDE_UI_MAP_OVERLAY_MARGIN) {
        x = safe.x + safe.w - card_w;
        y = safe.y;
    }

    OpenRideUIRect card = openride_ui_rect(x, y, card_w, card_h);
    openride_ui_panel(ui, card, true);
    openride_ui_text(ui,
                     openride_ui_rect(card.x + 14.0f,
                                      card.y + 8.0f,
                                      card.w - 28.0f,
                                      18.0f),
                     state->distance_title ? state->distance_title : "DISTANCE",
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);
    openride_ui_text_color(ui,
                           openride_ui_rect(card.x + 14.0f,
                                            card.y + 28.0f,
                                            card.w - 28.0f,
                                            30.0f),
                           state->distance_text ? state->distance_text : "--",
                           OPENRIDE_UI_TEXT_TITLE,
                           OPENRIDE_UI_TEXT_ALIGN_LEFT,
                           ui->theme.primary);
    if (state->duration_text && state->duration_text[0]) {
        openride_ui_text(ui,
                         openride_ui_rect(card.x + 14.0f,
                                          card.y + 62.0f,
                                          card.w - 28.0f,
                                          18.0f),
                         state->duration_text,
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
    }
}

static void draw_diagnostic(OpenRideUIContext *ui,
                            const OpenRideUIMapOverlayState *state,
                            OpenRideUIRect safe)
{
    const float panel_w = minf_openride(safe.w,
                                        OPENRIDE_UI_MAP_OVERLAY_PANEL_MAX_WIDTH);
    const uint32_t count = state->line_count > OPENRIDE_UI_MAP_OVERLAY_MAX_LINES
        ? OPENRIDE_UI_MAP_OVERLAY_MAX_LINES
        : state->line_count;
    const float panel_h = 42.0f
        + OPENRIDE_UI_MAP_OVERLAY_LINE_HEIGHT * (float)count
        + 12.0f;
    OpenRideUIRect panel = openride_ui_rect(safe.x,
                                            safe.y,
                                            panel_w,
                                            panel_h);
    openride_ui_panel(ui, panel, true);

    openride_ui_icon_draw(ui,
                          OPENRIDE_UI_ICON_MAP,
                          openride_ui_rect(panel.x + 12.0f,
                                           panel.y + 10.0f,
                                           22.0f,
                                           22.0f),
                          ui->theme.primary,
                          1.6f);
    openride_ui_text(ui,
                     openride_ui_rect(panel.x + 44.0f,
                                      panel.y + 8.0f,
                                      panel.w - 56.0f,
                                      24.0f),
                     state->title ? state->title : "OpenRide",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    for (uint32_t i = 0U; i < count; ++i) {
        const char *line = state->lines[i];
        if (!line || !line[0]) continue;
        openride_ui_text(ui,
                         openride_ui_rect(panel.x + 14.0f,
                                          panel.y + 38.0f
                                              + OPENRIDE_UI_MAP_OVERLAY_LINE_HEIGHT
                                                  * (float)i,
                                          panel.w - 28.0f,
                                          OPENRIDE_UI_MAP_OVERLAY_LINE_HEIGHT),
                         line,
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
    }

    draw_distance_card(ui, state, safe, panel);
    draw_attribution(ui, state);
}

void openride_ui_map_overlay_draw(OpenRideUIContext *ui,
                                  const OpenRideUIMapOverlayState *state)
{
    if (!ui || !ui->renderer || !state) return;

    OpenRideUIRect safe = openride_ui_safe_rect(ui);
    safe = openride_ui_inset(safe, OPENRIDE_UI_MAP_OVERLAY_MARGIN);
    if (safe.w <= 0.0f || safe.h <= 0.0f) return;

    if (state->compact) {
        draw_compact(ui, state, safe);
    } else {
        draw_diagnostic(ui, state, safe);
    }
}
