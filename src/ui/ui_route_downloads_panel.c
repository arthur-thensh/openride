#include "openride/ui_route_downloads_panel.h"

#include <stdio.h>

#define OPENRIDE_UI_ROUTE_DOWNLOADS_MARGIN 8.0f
#define OPENRIDE_UI_ROUTE_DOWNLOADS_GAP 8.0f
#define OPENRIDE_UI_ROUTE_DOWNLOADS_HEADER 58.0f
#define OPENRIDE_UI_ROUTE_DOWNLOADS_REGION_HEIGHT 24.0f
#define OPENRIDE_UI_ROUTE_DOWNLOADS_BUTTON_HEIGHT 54.0f
#define OPENRIDE_UI_ROUTE_DOWNLOADS_BACK_HEIGHT 54.0f

OpenRideUIRouteDownloadsPanelLayout openride_ui_route_downloads_panel_layout(
    const OpenRideUIContext *ui,
    uint32_t region_count)
{
    OpenRideUIRouteDownloadsPanelLayout layout = {0};
    if (!ui) return layout;

    if (region_count > OPENRIDE_UI_ROUTE_DOWNLOADS_MAX_REGIONS) {
        region_count = OPENRIDE_UI_ROUTE_DOWNLOADS_MAX_REGIONS;
    }

    OpenRideUIRect safe = openride_ui_inset(openride_ui_safe_rect(ui),
                                            OPENRIDE_UI_ROUTE_DOWNLOADS_MARGIN);
    if (safe.w < 120.0f || safe.h < 240.0f) return layout;

    layout.panel = safe;
    layout.title = openride_ui_rect(safe.x + 14.0f,
                                    safe.y + 8.0f,
                                    safe.w - 28.0f,
                                    24.0f);
    layout.subtitle = openride_ui_rect(safe.x + 14.0f,
                                       safe.y + 34.0f,
                                       safe.w - 28.0f,
                                       18.0f);
    layout.region_count = region_count;

    float y = safe.y + OPENRIDE_UI_ROUTE_DOWNLOADS_HEADER;
    for (uint32_t i = 0U; i < region_count; ++i) {
        layout.regions[i] = openride_ui_rect(
            safe.x + 16.0f,
            y,
            safe.w - 32.0f,
            OPENRIDE_UI_ROUTE_DOWNLOADS_REGION_HEIGHT);
        y += OPENRIDE_UI_ROUTE_DOWNLOADS_REGION_HEIGHT;
    }

    layout.alternative = openride_ui_rect(safe.x + 16.0f,
                                          y + 4.0f,
                                          safe.w - 32.0f,
                                          20.0f);

    layout.back = openride_ui_rect(
        safe.x + OPENRIDE_UI_ROUTE_DOWNLOADS_GAP,
        safe.y + safe.h - OPENRIDE_UI_ROUTE_DOWNLOADS_BACK_HEIGHT
            - OPENRIDE_UI_ROUTE_DOWNLOADS_GAP,
        safe.w - OPENRIDE_UI_ROUTE_DOWNLOADS_GAP * 2.0f,
        OPENRIDE_UI_ROUTE_DOWNLOADS_BACK_HEIGHT);

    layout.use_installed = openride_ui_rect(
        safe.x + OPENRIDE_UI_ROUTE_DOWNLOADS_GAP,
        layout.back.y - OPENRIDE_UI_ROUTE_DOWNLOADS_GAP
            - OPENRIDE_UI_ROUTE_DOWNLOADS_BUTTON_HEIGHT,
        safe.w - OPENRIDE_UI_ROUTE_DOWNLOADS_GAP * 2.0f,
        OPENRIDE_UI_ROUTE_DOWNLOADS_BUTTON_HEIGHT);

    layout.download = openride_ui_rect(
        safe.x + OPENRIDE_UI_ROUTE_DOWNLOADS_GAP,
        layout.use_installed.y - OPENRIDE_UI_ROUTE_DOWNLOADS_GAP
            - OPENRIDE_UI_ROUTE_DOWNLOADS_BUTTON_HEIGHT,
        safe.w - OPENRIDE_UI_ROUTE_DOWNLOADS_GAP * 2.0f,
        OPENRIDE_UI_ROUTE_DOWNLOADS_BUTTON_HEIGHT);

    layout.progress = openride_ui_rect(safe.x + 16.0f,
                                       layout.download.y - 24.0f,
                                       safe.w - 32.0f,
                                       18.0f);
    layout.status = openride_ui_rect(safe.x + 16.0f,
                                     layout.progress.y - 24.0f,
                                     safe.w - 32.0f,
                                     18.0f);
    return layout;
}

OpenRideUIRouteDownloadsPanelAction openride_ui_route_downloads_panel_hit_test(
    const OpenRideUIContext *ui,
    double x_px,
    double y_px)
{
    if (!ui) return OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_NONE;

    const double scale = ui->scale > 0.0f ? (double)ui->scale : 1.0;
    const double x = x_px / scale;
    const double y = y_px / scale;
    const OpenRideUIRouteDownloadsPanelLayout layout =
        openride_ui_route_downloads_panel_layout(ui,
            OPENRIDE_UI_ROUTE_DOWNLOADS_MAX_REGIONS);

    if (openride_ui_point_in_rect(x, y, layout.download)) {
        return OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_DOWNLOAD;
    }
    if (openride_ui_point_in_rect(x, y, layout.use_installed)) {
        return OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_USE_INSTALLED;
    }
    if (openride_ui_point_in_rect(x, y, layout.back)) {
        return OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_BACK;
    }
    return OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_NONE;
}

OpenRideUIRouteDownloadsPanelAction openride_ui_route_downloads_panel_draw(
    OpenRideUIContext *ui,
    const OpenRideUIRouteDownloadsPanelState *state)
{
    if (!ui || !ui->renderer || !state) {
        return OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_NONE;
    }

    uint32_t count = state->count;
    if (count > OPENRIDE_UI_ROUTE_DOWNLOADS_MAX_REGIONS) {
        count = OPENRIDE_UI_ROUTE_DOWNLOADS_MAX_REGIONS;
    }
    const OpenRideUIRouteDownloadsPanelLayout layout =
        openride_ui_route_downloads_panel_layout(ui, count);
    if (layout.panel.w <= 0.0f || layout.panel.h <= 0.0f) {
        return OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_NONE;
    }

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
                     "CARTES REQUISES",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);
    openride_ui_text(ui,
                     layout.subtitle,
                     state->downloading
                         ? "Telechargement pour l'itineraire"
                         : "Le corridor recommande traverse des cartes absentes",
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    char line[128];
    for (uint32_t i = 0U; i < count; ++i) {
        const char *name = state->region_names[i] && state->region_names[i][0]
            ? state->region_names[i]
            : "Region requise";
        snprintf(line,
                 sizeof(line),
                 "%s %s",
                 state->downloading && i == state->current_index ? ">" : "-",
                 name);
        openride_ui_text(ui,
                         layout.regions[i],
                         line,
                         OPENRIDE_UI_TEXT_BODY,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
    }

    if (state->has_installed_alternative) {
        openride_ui_text(ui,
                         layout.alternative,
                         "Une alternative avec les cartes actuelles existe",
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
    }

    if (state->downloading) {
        openride_ui_text(ui,
                         layout.status,
                         state->work_status && state->work_status[0]
                             ? state->work_status
                             : "Preparation de la carte...",
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
        if (state->progress >= 0.0) {
            char progress[48];
            snprintf(progress,
                     sizeof(progress),
                     "Progression : %.0f %%",
                     state->progress * 100.0);
            openride_ui_text(ui,
                             layout.progress,
                             progress,
                             OPENRIDE_UI_TEXT_CAPTION,
                             OPENRIDE_UI_TEXT_ALIGN_LEFT);
        }
    }

    OpenRideUIRouteDownloadsPanelAction action =
        OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_NONE;

    if (openride_ui_button(ui,
                           OPENRIDE_UI_ID("route-downloads-download"),
                           layout.download,
                           state->downloading
                               ? "TELECHARGEMENT EN COURS..."
                               : "TELECHARGER ET CALCULER",
                           OPENRIDE_UI_BUTTON_PRIMARY,
                           !state->downloading,
                           false)) {
        action = OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_DOWNLOAD;
    }

    if (openride_ui_button(ui,
                           OPENRIDE_UI_ID("route-downloads-use-installed"),
                           layout.use_installed,
                           state->has_installed_alternative
                               ? "CALCULER AVEC MES CARTES ACTUELLES"
                               : "AUCUNE ALTERNATIVE INSTALLEE",
                           OPENRIDE_UI_BUTTON_SECONDARY,
                           state->has_installed_alternative
                               && !state->downloading,
                           false)) {
        action = OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_USE_INSTALLED;
    }

    if (openride_ui_button(ui,
                           OPENRIDE_UI_ID("route-downloads-back"),
                           layout.back,
                           "Retour",
                           OPENRIDE_UI_BUTTON_GHOST,
                           true,
                           false)) {
        action = OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_BACK;
    }
    return action;
}
