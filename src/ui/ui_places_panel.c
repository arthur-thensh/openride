#include "openride/ui_places_panel.h"
#include "openride/ui_icon.h"

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
}

