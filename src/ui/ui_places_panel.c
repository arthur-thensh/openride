#include "openride/ui_places_panel.h"

#include <stdio.h>

#define OPENRIDE_UI_PLACES_MARGIN 8.0f
#define OPENRIDE_UI_PLACES_GAP 8.0f
#define OPENRIDE_UI_PLACES_HEADER 56.0f
#define OPENRIDE_UI_PLACES_BACK_HEIGHT 54.0f
#define OPENRIDE_UI_PLACES_MAX_ROW_HEIGHT 64.0f

static const char *places_title(OpenRideUIPlacesPanelMode mode)
{
    return mode == OPENRIDE_UI_PLACES_PANEL_HISTORY ? "HISTORIQUE" : "FAVORIS";
}

static const char *places_empty(OpenRideUIPlacesPanelMode mode)
{
    return mode == OPENRIDE_UI_PLACES_PANEL_HISTORY
        ? "Historique vide"
        : "Aucun favori";
}

static OpenRideUIID places_item_id(uint32_t index)
{
    char id[40];
    snprintf(id, sizeof(id), "places-item-%u", index);
    return openride_ui_id(id);
}

OpenRideUIPlacesPanelLayout openride_ui_places_panel_layout(
    const OpenRideUIContext *ui,
    uint32_t item_count)
{
    OpenRideUIPlacesPanelLayout layout = {0};
    if (!ui) return layout;

    if (item_count > OPENRIDE_UI_PLACES_PANEL_MAX_ITEMS) {
        item_count = OPENRIDE_UI_PLACES_PANEL_MAX_ITEMS;
    }

    OpenRideUIRect safe = openride_ui_inset(openride_ui_safe_rect(ui),
                                            OPENRIDE_UI_PLACES_MARGIN);
    if (safe.w < 120.0f || safe.h < 180.0f) return layout;

    layout.panel = safe;
    layout.title = openride_ui_rect(safe.x + 14.0f,
                                    safe.y + 8.0f,
                                    safe.w - 28.0f,
                                    28.0f);
    layout.subtitle = openride_ui_rect(safe.x + 14.0f,
                                       safe.y + 34.0f,
                                       safe.w - 28.0f,
                                       18.0f);
    layout.back = openride_ui_rect(
        safe.x + OPENRIDE_UI_PLACES_GAP,
        safe.y + safe.h - OPENRIDE_UI_PLACES_BACK_HEIGHT - OPENRIDE_UI_PLACES_GAP,
        safe.w - OPENRIDE_UI_PLACES_GAP * 2.0f,
        OPENRIDE_UI_PLACES_BACK_HEIGHT);

    layout.item_count = item_count;
    if (item_count == 0U) return layout;

    const float rows_top = safe.y + OPENRIDE_UI_PLACES_HEADER + OPENRIDE_UI_PLACES_GAP;
    float available = layout.back.y - OPENRIDE_UI_PLACES_GAP - rows_top;
    available -= OPENRIDE_UI_PLACES_GAP * (float)(item_count - 1U);
    float row_height = available / (float)item_count;
    if (row_height > OPENRIDE_UI_PLACES_MAX_ROW_HEIGHT) {
        row_height = OPENRIDE_UI_PLACES_MAX_ROW_HEIGHT;
    }
    if (row_height < 1.0f) row_height = 1.0f;

    for (uint32_t i = 0U; i < item_count; ++i) {
        layout.items[i] = openride_ui_rect(
            safe.x + OPENRIDE_UI_PLACES_GAP,
            rows_top + (row_height + OPENRIDE_UI_PLACES_GAP) * (float)i,
            safe.w - OPENRIDE_UI_PLACES_GAP * 2.0f,
            row_height);
    }
    return layout;
}

OpenRideUIPlacesPanelHit openride_ui_places_panel_hit_test(
    const OpenRideUIContext *ui,
    uint32_t item_count,
    double x_px,
    double y_px)
{
    OpenRideUIPlacesPanelHit hit = {OPENRIDE_UI_PLACES_PANEL_NONE, -1};
    if (!ui) return hit;

    const double scale = ui->scale > 0.0f ? (double)ui->scale : 1.0;
    const double x = x_px / scale;
    const double y = y_px / scale;
    const OpenRideUIPlacesPanelLayout layout =
        openride_ui_places_panel_layout(ui, item_count);

    for (uint32_t i = 0U; i < layout.item_count; ++i) {
        if (openride_ui_point_in_rect(x, y, layout.items[i])) {
            hit.action = OPENRIDE_UI_PLACES_PANEL_PLACE;
            hit.index = (int)i;
            return hit;
        }
    }
    if (openride_ui_point_in_rect(x, y, layout.back)) {
        hit.action = OPENRIDE_UI_PLACES_PANEL_BACK;
    }
    return hit;
}

OpenRideUIPlacesPanelHit openride_ui_places_panel_draw(
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

    SDL_FRect screen = {
        0.0f,
        0.0f,
        (float)ui->viewport_width,
        (float)ui->viewport_height
    };
    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 115);
    SDL_RenderFillRect(ui->renderer, &screen);

    openride_ui_panel(ui, layout.panel, true);
    openride_ui_text(ui,
                     layout.title,
                     places_title(state->mode),
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    const char *subtitle = count > 0U
        ? "Touche une destination"
        : places_empty(state->mode);
    openride_ui_text(ui,
                     layout.subtitle,
                     subtitle,
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    for (uint32_t i = 0U; i < count; ++i) {
        const char *label = state->items[i] && state->items[i][0]
            ? state->items[i]
            : "Position enregistree";
        if (openride_ui_button(ui,
                               places_item_id(i),
                               layout.items[i],
                               label,
                               OPENRIDE_UI_BUTTON_SECONDARY,
                               true,
                               i == state->selected)) {
            hit.action = OPENRIDE_UI_PLACES_PANEL_PLACE;
            hit.index = (int)i;
        }
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
}
