#include "openride/ui_map_overlay.h"

#include <stddef.h>

#define OPENRIDE_UI_MAP_OVERLAY_MARGIN 8.0f
#define OPENRIDE_UI_MAP_OVERLAY_PANEL_MAX_WIDTH 500.0f
#define OPENRIDE_UI_MAP_OVERLAY_COMPACT_MAX_WIDTH 390.0f
#define OPENRIDE_UI_MAP_OVERLAY_LINE_HEIGHT 16.0f
#define OPENRIDE_UI_MAP_OVERLAY_TOOLBAR_CLEARANCE 90.0f

static float minf_openride(float a, float b)
{
    return a < b ? a : b;
}

static void draw_attribution(OpenRideUIContext *ui,
                             const OpenRideUIMapOverlayState *state,
                             OpenRideUIRect safe)
{
    if (!state->attribution || !state->attribution[0]) return;

    const float y = safe.y + safe.h - OPENRIDE_UI_MAP_OVERLAY_TOOLBAR_CLEARANCE;
    if (y <= safe.y + 24.0f) return;

    OpenRideUIRect backing = openride_ui_rect(
        safe.x,
        y,
        minf_openride(safe.w, 300.0f),
        18.0f);
    openride_ui_panel(ui, backing, false);
    openride_ui_text(ui,
                     openride_ui_inset_xy(backing, 6.0f, 2.0f),
                     state->attribution,
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);
}

static void draw_compact(OpenRideUIContext *ui,
                         const OpenRideUIMapOverlayState *state,
                         OpenRideUIRect safe)
{
    const float panel_w = minf_openride(safe.w,
                                        OPENRIDE_UI_MAP_OVERLAY_COMPACT_MAX_WIDTH);
    const float panel_h = state->route_ready ? 68.0f : 50.0f;
    OpenRideUIRect panel = openride_ui_rect(safe.x,
                                            safe.y,
                                            panel_w,
                                            panel_h);
    openride_ui_panel(ui, panel, true);

    openride_ui_text(ui,
                     openride_ui_rect(panel.x + 10.0f,
                                      panel.y + 6.0f,
                                      panel.w - 20.0f,
                                      20.0f),
                     state->title ? state->title : "OpenRide",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    openride_ui_text(ui,
                     openride_ui_rect(panel.x + 10.0f,
                                      panel.y + 27.0f,
                                      panel.w - 20.0f,
                                      18.0f),
                     state->summary ? state->summary : "pret",
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    if (state->route_ready) {
        openride_ui_text(ui,
                         openride_ui_rect(panel.x + 10.0f,
                                          panel.y + 46.0f,
                                          panel.w - 20.0f,
                                          18.0f),
                         state->route_ready_text
                             ? state->route_ready_text
                             : "TRAJET PRET - touche DEMARRER",
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
    }

    draw_attribution(ui, state, safe);
}

static void draw_distance_card(OpenRideUIContext *ui,
                               const OpenRideUIMapOverlayState *state,
                               OpenRideUIRect safe,
                               OpenRideUIRect main_panel)
{
    if (!state->show_distance) return;

    const float card_w = minf_openride(230.0f, safe.w);
    const float card_h = state->duration_text && state->duration_text[0]
        ? 82.0f
        : 66.0f;

    float x = safe.x;
    float y = main_panel.y + main_panel.h + OPENRIDE_UI_MAP_OVERLAY_MARGIN;
    if (safe.w >= main_panel.w + card_w + OPENRIDE_UI_MAP_OVERLAY_MARGIN) {
        x = safe.x + safe.w - card_w;
        y = safe.y;
    }

    OpenRideUIRect card = openride_ui_rect(x, y, card_w, card_h);
    openride_ui_panel(ui, card, true);
    openride_ui_text(ui,
                     openride_ui_rect(card.x + 12.0f,
                                      card.y + 8.0f,
                                      card.w - 24.0f,
                                      18.0f),
                     state->distance_title ? state->distance_title : "DISTANCE",
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);
    openride_ui_text(ui,
                     openride_ui_rect(card.x + 12.0f,
                                      card.y + 28.0f,
                                      card.w - 24.0f,
                                      28.0f),
                     state->distance_text ? state->distance_text : "--",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);
    if (state->duration_text && state->duration_text[0]) {
        openride_ui_text(ui,
                         openride_ui_rect(card.x + 12.0f,
                                          card.y + 60.0f,
                                          card.w - 24.0f,
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
    const float panel_h = 38.0f
        + OPENRIDE_UI_MAP_OVERLAY_LINE_HEIGHT * (float)count
        + 10.0f;
    OpenRideUIRect panel = openride_ui_rect(safe.x,
                                            safe.y,
                                            panel_w,
                                            panel_h);
    openride_ui_panel(ui, panel, true);

    openride_ui_text(ui,
                     openride_ui_rect(panel.x + 12.0f,
                                      panel.y + 8.0f,
                                      panel.w - 24.0f,
                                      22.0f),
                     state->title ? state->title : "OpenRide",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    for (uint32_t i = 0U; i < count; ++i) {
        const char *line = state->lines[i];
        if (!line || !line[0]) continue;
        openride_ui_text(ui,
                         openride_ui_rect(panel.x + 12.0f,
                                          panel.y + 34.0f
                                              + OPENRIDE_UI_MAP_OVERLAY_LINE_HEIGHT
                                                  * (float)i,
                                          panel.w - 24.0f,
                                          OPENRIDE_UI_MAP_OVERLAY_LINE_HEIGHT),
                         line,
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
    }

    draw_distance_card(ui, state, safe, panel);
    draw_attribution(ui, state, safe);
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
