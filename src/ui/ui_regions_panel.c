#include "openride/ui_regions_panel.h"

#include <stdio.h>

#define OPENRIDE_UI_REGIONS_MARGIN 8.0f
#define OPENRIDE_UI_REGIONS_GAP 8.0f
#define OPENRIDE_UI_REGIONS_HEADER 56.0f
#define OPENRIDE_UI_REGIONS_NAV_HEIGHT 44.0f
#define OPENRIDE_UI_REGIONS_BUTTON_HEIGHT 56.0f
#define OPENRIDE_UI_REGIONS_BACK_HEIGHT 54.0f

OpenRideUIRegionsPanelLayout openride_ui_regions_panel_layout(
    const OpenRideUIContext *ui)
{
    OpenRideUIRegionsPanelLayout layout = {0};
    if (!ui) return layout;

    OpenRideUIRect safe = openride_ui_safe_rect(ui);
    safe = openride_ui_inset(safe, OPENRIDE_UI_REGIONS_MARGIN);
    if (safe.w < 120.0f || safe.h < 260.0f) return layout;

    layout.panel = safe;
    layout.title = openride_ui_rect(safe.x + 14.0f,
                                    safe.y + 8.0f,
                                    safe.w - 28.0f,
                                    28.0f);
    layout.subtitle = openride_ui_rect(safe.x + 14.0f,
                                       safe.y + 34.0f,
                                       safe.w - 28.0f,
                                       18.0f);

    const float inner_x = safe.x + OPENRIDE_UI_REGIONS_GAP;
    const float inner_w = safe.w - OPENRIDE_UI_REGIONS_GAP * 2.0f;
    const float half_w = (inner_w - OPENRIDE_UI_REGIONS_GAP) * 0.5f;
    const float nav_y = safe.y + OPENRIDE_UI_REGIONS_HEADER
        + OPENRIDE_UI_REGIONS_GAP;

    layout.previous = openride_ui_rect(inner_x,
                                       nav_y,
                                       half_w,
                                       OPENRIDE_UI_REGIONS_NAV_HEIGHT);
    layout.next = openride_ui_rect(inner_x + half_w + OPENRIDE_UI_REGIONS_GAP,
                                   nav_y,
                                   half_w,
                                   OPENRIDE_UI_REGIONS_NAV_HEIGHT);

    const float status_y = nav_y + OPENRIDE_UI_REGIONS_NAV_HEIGHT
        + OPENRIDE_UI_REGIONS_GAP;
    for (uint32_t i = 0U; i < 3U; ++i) {
        layout.status[i] = openride_ui_rect(inner_x,
                                            status_y + 24.0f * (float)i,
                                            inner_w,
                                            20.0f);
    }

    layout.back = openride_ui_rect(
        inner_x,
        safe.y + safe.h - OPENRIDE_UI_REGIONS_BACK_HEIGHT
            - OPENRIDE_UI_REGIONS_GAP,
        inner_w,
        OPENRIDE_UI_REGIONS_BACK_HEIGHT);

    float action_y = status_y + 80.0f;
    const float latest = layout.back.y - OPENRIDE_UI_REGIONS_GAP
        - OPENRIDE_UI_REGIONS_BUTTON_HEIGHT * 2.0f
        - OPENRIDE_UI_REGIONS_GAP
        - 28.0f;
    if (action_y > latest) action_y = latest;

    layout.install = openride_ui_rect(inner_x,
                                      action_y,
                                      inner_w,
                                      OPENRIDE_UI_REGIONS_BUTTON_HEIGHT);
    layout.remove = openride_ui_rect(
        inner_x,
        action_y + OPENRIDE_UI_REGIONS_BUTTON_HEIGHT + OPENRIDE_UI_REGIONS_GAP,
        inner_w,
        OPENRIDE_UI_REGIONS_BUTTON_HEIGHT);
    layout.work_status = openride_ui_rect(
        inner_x,
        layout.remove.y + layout.remove.h + 4.0f,
        inner_w,
        24.0f);
    return layout;
}

OpenRideUIRegionsPanelAction openride_ui_regions_panel_hit_test(
    const OpenRideUIContext *ui,
    double x_px,
    double y_px)
{
    if (!ui) return OPENRIDE_UI_REGIONS_PANEL_NONE;
    const double scale = ui->scale > 0.0f ? (double)ui->scale : 1.0;
    const double x = x_px / scale;
    const double y = y_px / scale;
    const OpenRideUIRegionsPanelLayout layout =
        openride_ui_regions_panel_layout(ui);

    if (openride_ui_point_in_rect(x, y, layout.previous)) {
        return OPENRIDE_UI_REGIONS_PANEL_PREVIOUS;
    }
    if (openride_ui_point_in_rect(x, y, layout.next)) {
        return OPENRIDE_UI_REGIONS_PANEL_NEXT;
    }
    if (openride_ui_point_in_rect(x, y, layout.install)) {
        return OPENRIDE_UI_REGIONS_PANEL_INSTALL;
    }
    if (openride_ui_point_in_rect(x, y, layout.remove)) {
        return OPENRIDE_UI_REGIONS_PANEL_REMOVE;
    }
    if (openride_ui_point_in_rect(x, y, layout.back)) {
        return OPENRIDE_UI_REGIONS_PANEL_BACK;
    }
    return OPENRIDE_UI_REGIONS_PANEL_NONE;
}

OpenRideUIRegionsPanelAction openride_ui_regions_panel_draw(
    OpenRideUIContext *ui,
    const OpenRideUIRegionsPanelState *state)
{
    if (!ui || !ui->renderer || !state) {
        return OPENRIDE_UI_REGIONS_PANEL_NONE;
    }

    const OpenRideUIRegionsPanelLayout layout =
        openride_ui_regions_panel_layout(ui);
    if (layout.panel.w <= 0.0f || layout.panel.h <= 0.0f) {
        return OPENRIDE_UI_REGIONS_PANEL_NONE;
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
                     "CARTES HORS LIGNE",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    char subtitle[128];
    snprintf(subtitle,
             sizeof(subtitle),
             "%s%s",
             state->region_name ? state->region_name : "Region",
             state->region_is_active ? "  [ACTIVE]" : "");
    openride_ui_text(ui,
                     layout.subtitle,
                     subtitle,
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    OpenRideUIRegionsPanelAction clicked = OPENRIDE_UI_REGIONS_PANEL_NONE;
    if (openride_ui_button(ui,
                           OPENRIDE_UI_ID("regions-previous"),
                           layout.previous,
                           "Region precedente",
                           OPENRIDE_UI_BUTTON_SECONDARY,
                           true,
                           false)) {
        clicked = OPENRIDE_UI_REGIONS_PANEL_PREVIOUS;
    }
    if (openride_ui_button(ui,
                           OPENRIDE_UI_ID("regions-next"),
                           layout.next,
                           "Region suivante",
                           OPENRIDE_UI_BUTTON_SECONDARY,
                           true,
                           false)) {
        clicked = OPENRIDE_UI_REGIONS_PANEL_NEXT;
    }

    char line[128];
    snprintf(line,
             sizeof(line),
             "Carte : %s | Routage : %s",
             state->ormap_installed ? "OK" : "absente",
             state->routing_installed ? "OK" : "absent");
    openride_ui_text(ui,
                     layout.status[0],
                     line,
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    snprintf(line,
             sizeof(line),
             "Recherche : %s | PBF : %s",
             state->search_installed ? "OK" : "absente",
             state->source_pbf_present ? "present" : "absent");
    openride_ui_text(ui,
                     layout.status[1],
                     line,
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    snprintf(line, sizeof(line), "Taille locale : %.1f Mo", state->total_size_mb);
    openride_ui_text(ui,
                     layout.status[2],
                     line,
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    char primary_label[112];
    bool install_enabled = !state->busy;
    if (state->busy) {
        if (state->progress >= 0.0) {
            snprintf(primary_label,
                     sizeof(primary_label),
                     "Preparation en cours : %.0f %%",
                     state->progress * 100.0);
        } else {
            snprintf(primary_label,
                     sizeof(primary_label),
                     "Preparation en cours...");
        }
    } else if (state->ready && !state->poly_present) {
        snprintf(primary_label, sizeof(primary_label), "Ajouter apercu de region");
    } else if (state->ready) {
        snprintf(primary_label,
                 sizeof(primary_label),
                 "%s",
                 state->region_is_active
                     ? "Region active"
                     : "Utiliser cette region");
    } else if (state->source_pbf_present) {
        snprintf(primary_label, sizeof(primary_label), "Preparer le PBF local");
    } else {
        snprintf(primary_label, sizeof(primary_label), "Telecharger OSM et preparer");
    }

    if (openride_ui_button(ui,
                           OPENRIDE_UI_ID("regions-install"),
                           layout.install,
                           primary_label,
                           OPENRIDE_UI_BUTTON_PRIMARY,
                           install_enabled,
                           state->region_is_active && state->ready)) {
        clicked = OPENRIDE_UI_REGIONS_PANEL_INSTALL;
    }

    if (openride_ui_button(ui,
                           OPENRIDE_UI_ID("regions-remove"),
                           layout.remove,
                           "Supprimer les donnees",
                           OPENRIDE_UI_BUTTON_DANGER,
                           !state->busy,
                           false)) {
        clicked = OPENRIDE_UI_REGIONS_PANEL_REMOVE;
    }

    if (state->work_status && state->work_status[0]) {
        openride_ui_text(ui,
                         layout.work_status,
                         state->work_status,
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
    }

    if (openride_ui_button(ui,
                           OPENRIDE_UI_ID("regions-back"),
                           layout.back,
                           "Retour",
                           OPENRIDE_UI_BUTTON_GHOST,
                           true,
                           false)) {
        clicked = OPENRIDE_UI_REGIONS_PANEL_BACK;
    }

    return clicked;
}
