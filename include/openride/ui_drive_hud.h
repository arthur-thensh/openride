#ifndef OPENRIDE_UI_DRIVE_HUD_H
#define OPENRIDE_UI_DRIVE_HUD_H

#include "openride/ui.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum OpenRideUIDriveHUDAction {
    OPENRIDE_UI_DRIVE_HUD_NONE = 0,
    OPENRIDE_UI_DRIVE_HUD_EXIT,
    OPENRIDE_UI_DRIVE_HUD_RECENTER,
    OPENRIDE_UI_DRIVE_HUD_ORIENTATION,
    OPENRIDE_UI_DRIVE_HUD_GPS
} OpenRideUIDriveHUDAction;

typedef enum OpenRideUIDriveHUDStatus {
    OPENRIDE_UI_DRIVE_HUD_ACTIVE = 0,
    OPENRIDE_UI_DRIVE_HUD_OFF_ROUTE,
    OPENRIDE_UI_DRIVE_HUD_ARRIVED
} OpenRideUIDriveHUDStatus;

typedef enum OpenRideUIDriveHUDManeuver {
    OPENRIDE_UI_DRIVE_MANEUVER_DEPART = 0,
    OPENRIDE_UI_DRIVE_MANEUVER_CONTINUE,
    OPENRIDE_UI_DRIVE_MANEUVER_SLIGHT_LEFT,
    OPENRIDE_UI_DRIVE_MANEUVER_LEFT,
    OPENRIDE_UI_DRIVE_MANEUVER_SHARP_LEFT,
    OPENRIDE_UI_DRIVE_MANEUVER_SLIGHT_RIGHT,
    OPENRIDE_UI_DRIVE_MANEUVER_RIGHT,
    OPENRIDE_UI_DRIVE_MANEUVER_SHARP_RIGHT,
    OPENRIDE_UI_DRIVE_MANEUVER_UTURN,
    OPENRIDE_UI_DRIVE_MANEUVER_ROUNDABOUT,
    OPENRIDE_UI_DRIVE_MANEUVER_ARRIVE
} OpenRideUIDriveHUDManeuver;

typedef enum OpenRideUIDriveHUDGPSQuality {
    OPENRIDE_UI_DRIVE_GPS_UNAVAILABLE = 0,
    OPENRIDE_UI_DRIVE_GPS_LOST,
    OPENRIDE_UI_DRIVE_GPS_POOR,
    OPENRIDE_UI_DRIVE_GPS_FAIR,
    OPENRIDE_UI_DRIVE_GPS_GOOD
} OpenRideUIDriveHUDGPSQuality;

typedef struct OpenRideUIDriveHUDState {
    OpenRideUIDriveHUDStatus status;
    OpenRideUIDriveHUDManeuver maneuver;
    const char *primary_text;
    const char *maneuver_text;

    bool show_following;
    const char *following_text;
    bool auto_reroute;

    OpenRideUIDriveHUDGPSQuality gps_quality;
    const char *gps_text;

    double speed_kph;
    double remaining_m;
    const char *arrival_text;
    uint32_t reroute_count;

    bool heading_up;
    bool show_attribution;
} OpenRideUIDriveHUDState;

typedef struct OpenRideUIDriveHUDLayout {
    OpenRideUIRect top;
    OpenRideUIRect following;
    OpenRideUIRect stats;
    OpenRideUIRect controls;
    OpenRideUIRect control_items[4];
} OpenRideUIDriveHUDLayout;

OpenRideUIDriveHUDLayout openride_ui_drive_hud_layout(
    const OpenRideUIContext *ui,
    bool show_following);

OpenRideUIDriveHUDAction openride_ui_drive_hud_hit_test(
    const OpenRideUIContext *ui,
    double x_px,
    double y_px);

void openride_ui_drive_hud_draw(OpenRideUIContext *ui,
                                const OpenRideUIDriveHUDState *state);

#endif
