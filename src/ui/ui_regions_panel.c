#include "openride/ui_regions_panel.h"
#include "openride/ui_icon.h"

#include <stdio.h>

#define OPENRIDE_UI_REGIONS_MARGIN 8.0f
#define OPENRIDE_UI_REGIONS_GAP 8.0f
#define OPENRIDE_UI_REGIONS_HEADER 56.0f
#define OPENRIDE_UI_REGIONS_NAV_HEIGHT 44.0f
#define OPENRIDE_UI_REGIONS_BUTTON_HEIGHT 56.0f
#define OPENRIDE_UI_REGIONS_BACK_HEIGHT 48.0f
#define OPENRIDE_UI_REGIONS_MAX_WIDTH 410.0f
#define OPENRIDE_UI_REGIONS_MAX_HEIGHT 470.0f

OpenRideUIRegionsPanelLayout openride_ui_regions_panel_layout(
    const OpenRideUIContext *ui)
{
    OpenRideUIRegionsPanelLayout layout = {0};
    if (!ui) return layout;

    OpenRideUIRect safe = openride_ui_inset(openride_ui_safe_rect(ui), 10.0f);
    if (safe.w < 120.0f || safe.h < 380.0f) return layout;
    const float panel_w = safe.w < 350.0f ? safe.w : 350.0f;
    const float max_h = safe.h * 0.88f;
    const float panel_h = max_h < 480.0f ? max_h : 480.0f;
    const float x = safe.x + (safe.w - panel_w) * 0.5f;
    const float y = safe.y + (safe.h - panel_h) * 0.42f;
    layout.panel = openride_ui_rect(x, y, panel_w, panel_h);
    layout.title = openride_ui_rect(x + 18.0f, y + 12.0f, panel_w - 36.0f, 28.0f);
    layout.subtitle = openride_ui_rect(x + 18.0f, y + 40.0f, panel_w - 36.0f, 20.0f);

    const float inner_x = x + 10.0f;
    const float inner_w = panel_w - 20.0f;
    const float nav_y = y + 72.0f;
    const float half = (inner_w - 8.0f) * 0.5f;
    layout.previous = openride_ui_rect(inner_x, nav_y, half, 40.0f);
    layout.next = openride_ui_rect(inner_x + half + 8.0f, nav_y, half, 40.0f);

    const float status_y = nav_y + 50.0f;
    for (uint32_t i = 0U; i < 3U; ++i) {
        layout.status[i] = openride_ui_rect(inner_x,
                                            status_y + 48.0f * (float)i,
                                            inner_w,
                                            42.0f);
    }

    layout.back = openride_ui_rect(inner_x,
                                   y + panel_h - 54.0f,
                                   inner_w,
                                   44.0f);
    layout.remove = openride_ui_rect(inner_x,
                                     layout.back.y - 48.0f,
                                     inner_w,
                                     40.0f);
    layout.install = openride_ui_rect(inner_x,
                                      layout.remove.y - 62.0f,
                                      inner_w,
                                      52.0f);
    layout.work_status = openride_ui_rect(inner_x,
                                          layout.install.y - 26.0f,
                                          inner_w,
                                          20.0f);
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
    if (!ui || !ui->renderer || !state) return OPENRIDE_UI_REGIONS_PANEL_NONE;
    const OpenRideUIRegionsPanelLayout layout = openride_ui_regions_panel_layout(ui);
    if (layout.panel.w <= 0.0f || layout.panel.h <= 0.0f) {
        return OPENRIDE_UI_REGIONS_PANEL_NONE;
    }

    SDL_FRect screen = {0.0f, 0.0f,
                        (float)ui->viewport_width,
                        (float)ui->viewport_height};
    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 92);
    SDL_RenderFillRect(ui->renderer, &screen);
    openride_ui_panel(ui, layout.panel, true);

    openride_ui_text(ui, layout.title, "Cartes hors ligne",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);
    char subtitle[128];
    snprintf(subtitle, sizeof(subtitle), "%s%s  ·  %.0f Mo",
             state->region_name ? state->region_name : "Région",
             state->region_is_active ? "  ·  active" : "",
             state->total_size_mb);
    openride_ui_text(ui, layout.subtitle, subtitle,
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    OpenRideUIRegionsPanelAction clicked = OPENRIDE_UI_REGIONS_PANEL_NONE;
    if (openride_ui_button(ui, OPENRIDE_UI_ID("regions-previous"),
                           layout.previous, "Précédente",
                           OPENRIDE_UI_BUTTON_GHOST, true, false)) {
        clicked = OPENRIDE_UI_REGIONS_PANEL_PREVIOUS;
    }
    if (openride_ui_button(ui, OPENRIDE_UI_ID("regions-next"),
                           layout.next, "Suivante",
                           OPENRIDE_UI_BUTTON_GHOST, true, false)) {
        clicked = OPENRIDE_UI_REGIONS_PANEL_NEXT;
    }

    const char *names[3] = {"Carte", "Navigation", "Recherche"};
    const bool installed[3] = {
        state->ormap_installed,
        state->routing_installed,
        state->search_installed
    };
    for (uint32_t i = 0U; i < 3U; ++i) {
        openride_ui_panel(ui, layout.status[i], false);
        openride_ui_text(ui,
                         openride_ui_rect(layout.status[i].x + 12.0f,
                                          layout.status[i].y,
                                          layout.status[i].w * 0.58f,
                                          layout.status[i].h),
                         names[i],
                         OPENRIDE_UI_TEXT_BODY,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
        OpenRideUIColor tint = installed[i]
            ? ui->theme.success
            : ui->theme.text_secondary;
        openride_ui_text_color(ui,
                               openride_ui_rect(layout.status[i].x
                                                   + layout.status[i].w * 0.56f,
                                                layout.status[i].y,
                                                layout.status[i].w * 0.40f - 10.0f,
                                                layout.status[i].h),
                               installed[i] ? "Prête" : "Absente",
                               OPENRIDE_UI_TEXT_CAPTION,
                               OPENRIDE_UI_TEXT_ALIGN_RIGHT,
                               tint);
    }

    char primary_label[112];
    const bool install_enabled = !state->busy;
    if (state->busy) {
        if (state->progress >= 0.0) {
            snprintf(primary_label, sizeof(primary_label),
                     "Préparation  ·  %.0f %%", state->progress * 100.0);
        } else {
            snprintf(primary_label, sizeof(primary_label), "Préparation en cours…");
        }
    } else if (state->ready && !state->poly_present) {
        snprintf(primary_label, sizeof(primary_label), "Ajouter l’aperçu de région");
    } else if (state->ready) {
        snprintf(primary_label, sizeof(primary_label), "%s",
                 state->region_is_active ? "Région active" : "Utiliser cette région");
    } else if (state->source_pbf_present) {
        snprintf(primary_label, sizeof(primary_label), "Préparer les données locales");
    } else {
        snprintf(primary_label, sizeof(primary_label), "Télécharger la région");
    }

    if (state->work_status && state->work_status[0]) {
        openride_ui_text(ui, layout.work_status, state->work_status,
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_CENTER);
    }
    if (openride_ui_button(ui, OPENRIDE_UI_ID("regions-install"),
                           layout.install, primary_label,
                           OPENRIDE_UI_BUTTON_PRIMARY,
                           install_enabled,
                           state->region_is_active && state->ready)) {
        clicked = OPENRIDE_UI_REGIONS_PANEL_INSTALL;
    }

    if (openride_ui_button(ui, OPENRIDE_UI_ID("regions-remove"),
                           layout.remove, "",
                           OPENRIDE_UI_BUTTON_GHOST,
                           !state->busy, false)) {
        clicked = OPENRIDE_UI_REGIONS_PANEL_REMOVE;
    }
    openride_ui_text_color(ui, layout.remove,
                           "Supprimer les données locales",
                           OPENRIDE_UI_TEXT_CAPTION,
                           OPENRIDE_UI_TEXT_ALIGN_CENTER,
                           ui->theme.danger);

    if (openride_ui_button(ui, OPENRIDE_UI_ID("regions-back"),
                           layout.back, "Retour",
                           OPENRIDE_UI_BUTTON_GHOST, true, false)) {
        clicked = OPENRIDE_UI_REGIONS_PANEL_BACK;
    }
    return clicked;
}

