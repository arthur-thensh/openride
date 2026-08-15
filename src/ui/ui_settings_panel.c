#include "openride/ui_settings_panel.h"

#include <stddef.h>
#include <stdio.h>

#define OPENRIDE_UI_SETTINGS_ITEMS 9U
#define OPENRIDE_UI_SETTINGS_MARGIN 8.0f
#define OPENRIDE_UI_SETTINGS_GAP 8.0f
#define OPENRIDE_UI_SETTINGS_HEADER 56.0f
#define OPENRIDE_UI_SETTINGS_BACK_HEIGHT 54.0f
#define OPENRIDE_UI_SETTINGS_MAX_ROW_HEIGHT 64.0f

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

    OpenRideUIRect safe = openride_ui_safe_rect(ui);
    safe = openride_ui_inset(safe, OPENRIDE_UI_SETTINGS_MARGIN);
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
        safe.x + OPENRIDE_UI_SETTINGS_GAP,
        safe.y + safe.h - OPENRIDE_UI_SETTINGS_BACK_HEIGHT
            - OPENRIDE_UI_SETTINGS_GAP,
        safe.w - OPENRIDE_UI_SETTINGS_GAP * 2.0f,
        OPENRIDE_UI_SETTINGS_BACK_HEIGHT);

    const float rows_top = safe.y + OPENRIDE_UI_SETTINGS_HEADER
        + OPENRIDE_UI_SETTINGS_GAP;
    float available = layout.back.y - OPENRIDE_UI_SETTINGS_GAP - rows_top;
    available -= OPENRIDE_UI_SETTINGS_GAP
        * (float)(OPENRIDE_UI_SETTINGS_ITEMS - 1U);
    float row_height = available / (float)OPENRIDE_UI_SETTINGS_ITEMS;
    if (row_height > OPENRIDE_UI_SETTINGS_MAX_ROW_HEIGHT) {
        row_height = OPENRIDE_UI_SETTINGS_MAX_ROW_HEIGHT;
    }
    if (row_height < 1.0f) row_height = 1.0f;

    for (uint32_t i = 0U; i < OPENRIDE_UI_SETTINGS_ITEMS; ++i) {
        layout.items[i] = openride_ui_rect(
            safe.x + OPENRIDE_UI_SETTINGS_GAP,
            rows_top + (row_height + OPENRIDE_UI_SETTINGS_GAP) * (float)i,
            safe.w - OPENRIDE_UI_SETTINGS_GAP * 2.0f,
            row_height);
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
    if (!ui || !ui->renderer || !state) {
        return OPENRIDE_UI_SETTINGS_PANEL_NONE;
    }

    const OpenRideUISettingsPanelLayout layout =
        openride_ui_settings_panel_layout(ui);
    if (layout.panel.w <= 0.0f || layout.panel.h <= 0.0f) {
        return OPENRIDE_UI_SETTINGS_PANEL_NONE;
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
                     "PARAMETRES",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);
    openride_ui_text(ui,
                     layout.subtitle,
                     "Touche une ligne pour modifier",
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    char labels[OPENRIDE_UI_SETTINGS_ITEMS][112];
    snprintf(labels[0], sizeof(labels[0]),
             "Style carte : %s",
             state->map_style_name ? state->map_style_name : "-");
    snprintf(labels[1], sizeof(labels[1]),
             "Profil routage : %s",
             state->routing_profile_name ? state->routing_profile_name : "-");
    snprintf(labels[2], sizeof(labels[2]),
             "Suivi GPS : %s", state->follow_gps ? "OUI" : "NON");
    snprintf(labels[3], sizeof(labels[3]),
             "Recalcul auto : %s", state->auto_reroute ? "OUI" : "NON");
    snprintf(labels[4], sizeof(labels[4]),
             "Guidage vocal : %s", state->voice_enabled ? "OUI" : "NON");
    snprintf(labels[5], sizeof(labels[5]),
             "GPS simule [DEV] : %s",
             state->simulated_gps_active ? "OUI" : "NON");
    snprintf(labels[6], sizeof(labels[6]),
             "Deviation 80 m [DEV] : %s",
             state->simulated_gps_deviation
                 ? "EN COURS"
                 : state->simulated_gps_active
                     ? "DECLENCHER"
                     : "GPS SIMULE REQUIS");
    snprintf(labels[7], sizeof(labels[7]),
             "Vitesse simulation [DEV] : x%.0f",
             state->simulated_gps_time_scale);
    snprintf(labels[8], sizeof(labels[8]),
             "Virage rate reel [DEV] : %s",
             state->simulated_missed_turn_active
                 ? "MAUVAISE ROUTE"
                 : state->simulated_missed_turn_armed
                     ? "ARME"
                     : state->simulated_gps_active
                         ? "DECLENCHER"
                         : "GPS SIMULE REQUIS");

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
                               labels[i],
                               OPENRIDE_UI_BUTTON_SECONDARY,
                               true,
                               selected)) {
            clicked = (OpenRideUISettingsPanelAction)(
                OPENRIDE_UI_SETTINGS_PANEL_STYLE + (int)i);
        }
    }

    if (openride_ui_button(ui,
                           OPENRIDE_UI_ID("settings-back"),
                           layout.back,
                           "Retour",
                           OPENRIDE_UI_BUTTON_GHOST,
                           true,
                           false)) {
        clicked = OPENRIDE_UI_SETTINGS_PANEL_BACK;
    }

    return clicked;
}
