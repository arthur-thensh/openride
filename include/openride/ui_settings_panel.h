#ifndef OPENRIDE_UI_SETTINGS_PANEL_H
#define OPENRIDE_UI_SETTINGS_PANEL_H

#include "openride/ui.h"

#include <stdbool.h>

typedef enum OpenRideUISettingsPanelAction {
    OPENRIDE_UI_SETTINGS_PANEL_NONE = 0,
    OPENRIDE_UI_SETTINGS_PANEL_STYLE,
    OPENRIDE_UI_SETTINGS_PANEL_PROFILE,
    OPENRIDE_UI_SETTINGS_PANEL_FOLLOW,
    OPENRIDE_UI_SETTINGS_PANEL_REROUTE,
    OPENRIDE_UI_SETTINGS_PANEL_VOICE,
    OPENRIDE_UI_SETTINGS_PANEL_GPS_SIMULATION,
    OPENRIDE_UI_SETTINGS_PANEL_GPS_DEVIATION,
    OPENRIDE_UI_SETTINGS_PANEL_GPS_SPEED,
    OPENRIDE_UI_SETTINGS_PANEL_GPS_MISSED_TURN,
    OPENRIDE_UI_SETTINGS_PANEL_BACK
} OpenRideUISettingsPanelAction;

typedef struct OpenRideUISettingsPanelState {
    const char *map_style_name;
    const char *routing_profile_name;
    bool follow_gps;
    bool auto_reroute;
    bool voice_enabled;
    bool simulated_gps_active;
    bool simulated_gps_deviation;
    double simulated_gps_time_scale;
    bool simulated_missed_turn_armed;
    bool simulated_missed_turn_active;
} OpenRideUISettingsPanelState;

typedef struct OpenRideUISettingsPanelLayout {
    OpenRideUIRect panel;
    OpenRideUIRect title;
    OpenRideUIRect subtitle;
    OpenRideUIRect items[9];
    OpenRideUIRect back;
} OpenRideUISettingsPanelLayout;

OpenRideUISettingsPanelLayout openride_ui_settings_panel_layout(
    const OpenRideUIContext *ui);

OpenRideUISettingsPanelAction openride_ui_settings_panel_hit_test(
    const OpenRideUIContext *ui,
    double x_px,
    double y_px);

OpenRideUISettingsPanelAction openride_ui_settings_panel_draw(
    OpenRideUIContext *ui,
    const OpenRideUISettingsPanelState *state);

#endif
