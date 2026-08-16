#include "openride/ui_settings_panel.h"
#include "openride/ui_icon.h"

#include <stddef.h>
#include <stdio.h>

#define OPENRIDE_UI_SETTINGS_ITEMS 9U
#define OPENRIDE_UI_SETTINGS_MARGIN 8.0f
#define OPENRIDE_UI_SETTINGS_GAP 8.0f
#define OPENRIDE_UI_SETTINGS_HEADER 56.0f
#define OPENRIDE_UI_SETTINGS_BACK_HEIGHT 48.0f
#define OPENRIDE_UI_SETTINGS_MAX_ROW_HEIGHT 54.0f
#define OPENRIDE_UI_SETTINGS_MAX_WIDTH 420.0f
#define OPENRIDE_UI_SETTINGS_MAX_HEIGHT 620.0f

static OpenRideUIID settings_id(uint32_t index)
{
    static const char *ids[OPENRIDE_UI_SETTINGS_ITEMS] = {
        "settings-style",
        "settings-profile",
        "settings-follow",
        "settings-reroute",
        "settings-voice",
        "settings-gps-simulation",
        "settings-gps-deviation",
        "settings-gps-speed",
        "settings-gps-missed-turn"
    };
    if (index >= OPENRIDE_UI_SETTINGS_ITEMS) return 0U;
    return OPENRIDE_UI_ID(ids[index]);
}

OpenRideUISettingsPanelLayout openride_ui_settings_panel_layout(
    const OpenRideUIContext *ui)
{
    OpenRideUISettingsPanelLayout layout = {0};
    if (!ui) return layout;

    OpenRideUIRect safe = openride_ui_inset(openride_ui_safe_rect(ui), 10.0f);
    if (safe.w < 120.0f || safe.h < 390.0f) return layout;
    const float panel_w = safe.w < 350.0f ? safe.w : 350.0f;
    const float max_h = safe.h * 0.92f;
    const float panel_h = max_h < 535.0f ? max_h : 535.0f;
    const float x = safe.x + (safe.w - panel_w) * 0.5f;
    const float y = safe.y + (safe.h - panel_h) * 0.44f;
    layout.panel = openride_ui_rect(x, y, panel_w, panel_h);
    layout.title = openride_ui_rect(x + 18.0f, y + 12.0f, panel_w - 36.0f, 28.0f);
    layout.subtitle = openride_ui_rect(x + 18.0f, y + 40.0f, panel_w - 36.0f, 18.0f);
    layout.back = openride_ui_rect(x + 10.0f,
                                   y + panel_h - 54.0f,
                                   panel_w - 20.0f,
                                   44.0f);

    const float top = y + 82.0f;
    const float bottom = layout.back.y - 10.0f;
    const float section_space = 38.0f;
    const float gaps = 7.0f * 7.0f;
    float row_h = (bottom - top - section_space - gaps) / 9.0f;
    if (row_h > 42.0f) row_h = 42.0f;
    if (row_h < 30.0f) row_h = 30.0f;

    float row_y = top + 18.0f;
    for (uint32_t i = 0U; i < 5U; ++i) {
        layout.items[i] = openride_ui_rect(x + 10.0f,
                                           row_y,
                                           panel_w - 20.0f,
                                           row_h);
        row_y += row_h + 7.0f;
    }
    row_y += 20.0f;
    for (uint32_t i = 5U; i < OPENRIDE_UI_SETTINGS_ITEMS; ++i) {
        layout.items[i] = openride_ui_rect(x + 10.0f,
                                           row_y,
                                           panel_w - 20.0f,
                                           row_h);
        row_y += row_h + 7.0f;
    }
    return layout;
}


OpenRideUISettingsPanelAction openride_ui_settings_panel_hit_test(
    const OpenRideUIContext *ui,
    double x_px,
    double y_px)
{
    if (!ui) return OPENRIDE_UI_SETTINGS_PANEL_NONE;
    const double scale = ui->scale > 0.0f ? (double)ui->scale : 1.0;
    const double x = x_px / scale;
    const double y = y_px / scale;
    const OpenRideUISettingsPanelLayout layout =
        openride_ui_settings_panel_layout(ui);

    for (uint32_t i = 0U; i < OPENRIDE_UI_SETTINGS_ITEMS; ++i) {
        if (openride_ui_point_in_rect(x, y, layout.items[i])) {
            return (OpenRideUISettingsPanelAction)(
                OPENRIDE_UI_SETTINGS_PANEL_STYLE + (int)i);
        }
    }
    if (openride_ui_point_in_rect(x, y, layout.back)) {
        return OPENRIDE_UI_SETTINGS_PANEL_BACK;
    }
    return OPENRIDE_UI_SETTINGS_PANEL_NONE;
}

OpenRideUISettingsPanelAction openride_ui_settings_panel_draw(
    OpenRideUIContext *ui,
    const OpenRideUISettingsPanelState *state)
{
    if (!ui || !ui->renderer || !state) return OPENRIDE_UI_SETTINGS_PANEL_NONE;
    const OpenRideUISettingsPanelLayout layout = openride_ui_settings_panel_layout(ui);
    if (layout.panel.w <= 0.0f || layout.panel.h <= 0.0f) {
        return OPENRIDE_UI_SETTINGS_PANEL_NONE;
    }

    SDL_FRect screen = {0.0f, 0.0f,
                        (float)ui->viewport_width,
                        (float)ui->viewport_height};
    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 92);
    SDL_RenderFillRect(ui->renderer, &screen);
    openride_ui_panel(ui, layout.panel, true);
    openride_ui_text(ui, layout.title, "Paramètres",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);
    openride_ui_text(ui, layout.subtitle,
                     "Navigation et comportement d’OpenRide",
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    openride_ui_text_color(ui,
                           openride_ui_rect(layout.items[0].x + 4.0f,
                                            layout.items[0].y - 18.0f,
                                            layout.items[0].w - 8.0f,
                                            16.0f),
                           "APPLICATION",
                           OPENRIDE_UI_TEXT_CAPTION,
                           OPENRIDE_UI_TEXT_ALIGN_LEFT,
                           ui->theme.primary);
    openride_ui_text_color(ui,
                           openride_ui_rect(layout.items[5].x + 4.0f,
                                            layout.items[5].y - 18.0f,
                                            layout.items[5].w - 8.0f,
                                            16.0f),
                           "DÉVELOPPEUR",
                           OPENRIDE_UI_TEXT_CAPTION,
                           OPENRIDE_UI_TEXT_ALIGN_LEFT,
                           ui->theme.text_secondary);

    char labels[OPENRIDE_UI_SETTINGS_ITEMS][112];
    snprintf(labels[0], sizeof(labels[0]), "Style de carte  ·  %s",
             state->map_style_name ? state->map_style_name : "-");
    snprintf(labels[1], sizeof(labels[1]), "Profil de route  ·  %s",
             state->routing_profile_name ? state->routing_profile_name : "-");
    snprintf(labels[2], sizeof(labels[2]), "Suivi GPS  ·  %s",
             state->follow_gps ? "Activé" : "Désactivé");
    snprintf(labels[3], sizeof(labels[3]), "Recalcul automatique  ·  %s",
             state->auto_reroute ? "Activé" : "Désactivé");
    snprintf(labels[4], sizeof(labels[4]), "Guidage vocal  ·  %s",
             state->voice_enabled ? "Activé" : "Désactivé");
    snprintf(labels[5], sizeof(labels[5]), "GPS simulé  ·  %s",
             state->simulated_gps_active ? "Activé" : "Désactivé");
    snprintf(labels[6], sizeof(labels[6]), "Déviation 80 m  ·  %s",
             state->simulated_gps_deviation
                 ? "En cours"
                 : state->simulated_gps_active ? "Déclencher" : "GPS simulé requis");
    snprintf(labels[7], sizeof(labels[7]), "Vitesse simulation  ·  x%.0f",
             state->simulated_gps_time_scale);
    snprintf(labels[8], sizeof(labels[8]), "Virage raté  ·  %s",
             state->simulated_missed_turn_active
                 ? "Mauvaise route"
                 : state->simulated_missed_turn_armed
                     ? "Armé"
                     : state->simulated_gps_active ? "Déclencher" : "GPS simulé requis");

    OpenRideUISettingsPanelAction clicked = OPENRIDE_UI_SETTINGS_PANEL_NONE;
    for (uint32_t i = 0U; i < OPENRIDE_UI_SETTINGS_ITEMS; ++i) {
        const bool selected =
            (i == 5U && state->simulated_gps_active)
            || (i == 6U && state->simulated_gps_deviation)
            || (i == 8U
                && (state->simulated_missed_turn_armed
                    || state->simulated_missed_turn_active));
        if (openride_ui_button(ui,
                               settings_id(i),
                               layout.items[i],
                               "",
                               i < 5U
                                   ? OPENRIDE_UI_BUTTON_SECONDARY
                                   : OPENRIDE_UI_BUTTON_GHOST,
                               true,
                               selected)) {
            clicked = (OpenRideUISettingsPanelAction)(
                OPENRIDE_UI_SETTINGS_PANEL_STYLE + (int)i);
        }
        OpenRideUIColor tint = i < 5U
            ? ui->theme.text
            : ui->theme.text_secondary;
        if (selected) tint = ui->theme.primary;
        openride_ui_text_color(ui,
                               openride_ui_inset_xy(layout.items[i], 13.0f, 0.0f),
                               labels[i],
                               OPENRIDE_UI_TEXT_BODY,
                               OPENRIDE_UI_TEXT_ALIGN_LEFT,
                               tint);
    }

    if (openride_ui_button(ui, OPENRIDE_UI_ID("settings-back"),
                           layout.back, "Retour",
                           OPENRIDE_UI_BUTTON_GHOST, true, false)) {
        clicked = OPENRIDE_UI_SETTINGS_PANEL_BACK;
    }
    return clicked;
}

