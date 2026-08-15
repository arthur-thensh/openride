#include "openride/ui_search_overlay.h"

#include <stdio.h>
#include <string.h>

#define OPENRIDE_UI_SEARCH_MARGIN 8.0f
#define OPENRIDE_UI_SEARCH_GAP 6.0f
#define OPENRIDE_UI_SEARCH_TITLE_HEIGHT 38.0f
#define OPENRIDE_UI_SEARCH_QUERY_HEIGHT 50.0f
#define OPENRIDE_UI_SEARCH_FOOTER_HEIGHT 12.0f
#define OPENRIDE_UI_SEARCH_DESIRED_ROW_HEIGHT 54.0f
#define OPENRIDE_UI_SEARCH_MIN_ROW_HEIGHT 31.0f

static OpenRideUIID search_row_id(uint32_t index)
{
    char id[40];
    snprintf(id, sizeof(id), "search-row-%u", index);
    return openride_ui_id(id);
}

OpenRideUISearchOverlayLayout openride_ui_search_overlay_layout(
    const OpenRideUIContext *ui,
    uint32_t result_count)
{
    OpenRideUISearchOverlayLayout layout = {0};
    if (!ui) return layout;

    if (result_count > OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS) {
        result_count = OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS;
    }

    const OpenRideUIRect safe = openride_ui_safe_rect(ui);
    if (safe.w <= 0.0f || safe.h <= 0.0f) return layout;

    const float margin = OPENRIDE_UI_SEARCH_MARGIN;
    const float max_panel_h = safe.h * 0.57f;
    const uint32_t visible_rows = result_count > 0U ? result_count : 1U;
    const float desired_h =
        OPENRIDE_UI_SEARCH_TITLE_HEIGHT
        + OPENRIDE_UI_SEARCH_GAP
        + OPENRIDE_UI_SEARCH_QUERY_HEIGHT
        + OPENRIDE_UI_SEARCH_GAP
        + OPENRIDE_UI_SEARCH_DESIRED_ROW_HEIGHT * (float)visible_rows
        + OPENRIDE_UI_SEARCH_FOOTER_HEIGHT
        + margin;

    float panel_h = desired_h < max_panel_h ? desired_h : max_panel_h;
    if (panel_h < 190.0f) panel_h = 190.0f;
    const float safe_max_h = safe.h - margin * 2.0f;
    if (panel_h > safe_max_h) panel_h = safe_max_h;

    layout.panel = openride_ui_rect(safe.x + margin,
                                    safe.y + margin,
                                    safe.w - margin * 2.0f,
                                    panel_h);
    if (layout.panel.w <= 0.0f || layout.panel.h <= 0.0f) return layout;

    layout.title = openride_ui_rect(layout.panel.x + 12.0f,
                                    layout.panel.y + 5.0f,
                                    layout.panel.w - 24.0f,
                                    OPENRIDE_UI_SEARCH_TITLE_HEIGHT - 8.0f);
    layout.query = openride_ui_rect(layout.panel.x + margin,
                                    layout.panel.y
                                        + OPENRIDE_UI_SEARCH_TITLE_HEIGHT
                                        + OPENRIDE_UI_SEARCH_GAP,
                                    layout.panel.w - margin * 2.0f,
                                    OPENRIDE_UI_SEARCH_QUERY_HEIGHT);

    layout.row_count = result_count;
    const float rows_top = layout.query.y
        + layout.query.h
        + OPENRIDE_UI_SEARCH_GAP;
    const float rows_bottom = layout.panel.y
        + layout.panel.h
        - OPENRIDE_UI_SEARCH_FOOTER_HEIGHT
        - margin;

    float row_height = result_count > 0U
        ? (rows_bottom - rows_top) / (float)result_count
        : OPENRIDE_UI_SEARCH_DESIRED_ROW_HEIGHT;
    if (row_height > OPENRIDE_UI_SEARCH_DESIRED_ROW_HEIGHT) {
        row_height = OPENRIDE_UI_SEARCH_DESIRED_ROW_HEIGHT;
    }
    if (row_height < OPENRIDE_UI_SEARCH_MIN_ROW_HEIGHT) {
        row_height = OPENRIDE_UI_SEARCH_MIN_ROW_HEIGHT;
    }

    for (uint32_t i = 0U; i < result_count; ++i) {
        layout.rows[i] = openride_ui_rect(layout.panel.x + margin,
                                          rows_top + row_height * (float)i,
                                          layout.panel.w - margin * 2.0f,
                                          row_height - 2.0f);
    }

    layout.message = openride_ui_rect(layout.query.x,
                                      rows_top,
                                      layout.query.w,
                                      34.0f);
    return layout;
}

int openride_ui_search_overlay_result_at(
    const OpenRideUIContext *ui,
    uint32_t result_count,
    double x_px,
    double y_px)
{
    if (!ui || result_count == 0U) return -1;
    const double scale = ui->scale > 0.0f ? (double)ui->scale : 1.0;
    const double x = x_px / scale;
    const double y = y_px / scale;
    const OpenRideUISearchOverlayLayout layout =
        openride_ui_search_overlay_layout(ui, result_count);

    for (uint32_t i = 0U; i < layout.row_count; ++i) {
        if (openride_ui_point_in_rect(x, y, layout.rows[i])) {
            return (int)i;
        }
    }
    return -1;
}

void openride_ui_search_overlay_draw(
    OpenRideUIContext *ui,
    const OpenRideUISearchOverlayState *state)
{
    if (!ui || !ui->renderer || !state) return;

    uint32_t count = state->count;
    if (count > OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS) {
        count = OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS;
    }

    const OpenRideUISearchOverlayLayout layout =
        openride_ui_search_overlay_layout(ui, count);
    if (layout.panel.w <= 0.0f || layout.panel.h <= 0.0f) return;

    SDL_FRect screen = {
        0.0f,
        0.0f,
        (float)ui->viewport_width,
        (float)ui->viewport_height
    };
    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 72);
    SDL_RenderFillRect(ui->renderer, &screen);

    openride_ui_panel(ui, layout.panel, true);
    openride_ui_text(ui,
                     layout.title,
                     state->title && state->title[0]
                         ? state->title
                         : "RECHERCHER UN LIEU",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    openride_ui_panel(ui, layout.query, false);
    char query_text[96];
    snprintf(query_text,
             sizeof(query_text),
             "%s%s",
             state->query && state->query[0]
                 ? state->query
                 : "Tapez un lieu",
             state->query && state->query[0] ? "_" : "");
    openride_ui_text(ui,
                     openride_ui_inset_xy(layout.query, 12.0f, 4.0f),
                     query_text,
                     OPENRIDE_UI_TEXT_BODY,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    if (!state->available) {
        openride_ui_text(ui,
                         layout.message,
                         "Aucun index de recherche regional installe",
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
        return;
    }

    if (count == 0U) {
        const char *message =
            state->query && strlen(state->query) >= 2U
                ? "Aucun resultat"
                : "Saisissez au moins 2 caracteres";
        openride_ui_text(ui,
                         layout.message,
                         message,
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
        return;
    }

    for (uint32_t i = 0U; i < count; ++i) {
        (void)openride_ui_button(ui,
                                 search_row_id(i),
                                 layout.rows[i],
                                 "",
                                 OPENRIDE_UI_BUTTON_SECONDARY,
                                 true,
                                 i == state->selected);

        const OpenRideUIRect name_rect = openride_ui_rect(
            layout.rows[i].x + 12.0f,
            layout.rows[i].y + 3.0f,
            layout.rows[i].w - 24.0f,
            layout.rows[i].h * 0.52f);
        const OpenRideUIRect secondary_rect = openride_ui_rect(
            layout.rows[i].x + 12.0f,
            layout.rows[i].y + layout.rows[i].h * 0.50f,
            layout.rows[i].w - 24.0f,
            layout.rows[i].h * 0.46f);

        openride_ui_text(ui,
                         name_rect,
                         state->items[i].name && state->items[i].name[0]
                             ? state->items[i].name
                             : "Lieu",
                         OPENRIDE_UI_TEXT_BODY,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
        if (state->items[i].secondary
            && state->items[i].secondary[0]) {
            openride_ui_text(ui,
                             secondary_rect,
                             state->items[i].secondary,
                             OPENRIDE_UI_TEXT_CAPTION,
                             OPENRIDE_UI_TEXT_ALIGN_LEFT);
        }
    }
}
