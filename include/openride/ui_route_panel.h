#ifndef OPENRIDE_UI_ROUTE_PANEL_H
#define OPENRIDE_UI_ROUTE_PANEL_H

#include "openride/loop_generator.h"
#include "openride/ride_planner.h"
#include "openride/routing_engine.h"
#include "openride/ui.h"

#include <stdbool.h>

typedef enum OpenRideUIRoutePanelAction {
    OPENRIDE_UI_ROUTE_PANEL_NONE = 0,
    OPENRIDE_UI_ROUTE_PANEL_MODE_ROUTE,
    OPENRIDE_UI_ROUTE_PANEL_MODE_LOOP,
    OPENRIDE_UI_ROUTE_PANEL_GPS_START,
    OPENRIDE_UI_ROUTE_PANEL_SEARCH_START,
    OPENRIDE_UI_ROUTE_PANEL_MAP_START,
    OPENRIDE_UI_ROUTE_PANEL_SEARCH_DESTINATION,
    OPENRIDE_UI_ROUTE_PANEL_MAP_DESTINATION,
    OPENRIDE_UI_ROUTE_PANEL_PROFILE_FASTEST,
    OPENRIDE_UI_ROUTE_PANEL_PROFILE_TOURING,
    OPENRIDE_UI_ROUTE_PANEL_PROFILE_TRAIL,
    OPENRIDE_UI_ROUTE_PANEL_LOOP_DISTANCE_DOWN,
    OPENRIDE_UI_ROUTE_PANEL_LOOP_DISTANCE_UP,
    OPENRIDE_UI_ROUTE_PANEL_LOOP_DIRECTION,
    OPENRIDE_UI_ROUTE_PANEL_CALCULATE,
    OPENRIDE_UI_ROUTE_PANEL_BACK
} OpenRideUIRoutePanelAction;

typedef struct OpenRideUIRoutePanelState {
    OpenRideRidePlannerMode mode;
    OpenRideRidePlannerBusy busy;
    const char *feedback;
    bool has_start;
    bool has_destination;
    bool gps_valid;
    double gps_accuracy_m;
    OpenRideRoutingProfile profile;
    double loop_target_distance_m;
    OpenRideLoopDirection loop_direction;
} OpenRideUIRoutePanelState;

typedef struct OpenRideUIRoutePanelLayout {
    OpenRideUIRect panel;
    OpenRideUIRect title;
    OpenRideUIRect subtitle;
    OpenRideUIRect mode_route;
    OpenRideUIRect mode_loop;
    OpenRideUIRect start[3];
    OpenRideUIRect destination[2];
    OpenRideUIRect profiles[3];
    OpenRideUIRect distance_down;
    OpenRideUIRect distance_value;
    OpenRideUIRect distance_up;
    OpenRideUIRect direction;
    OpenRideUIRect primary;
    OpenRideUIRect hint;
    OpenRideUIRect back;
} OpenRideUIRoutePanelLayout;

OpenRideUIRoutePanelLayout openride_ui_route_panel_layout(
    const OpenRideUIContext *ui,
    OpenRideRidePlannerMode mode);

OpenRideUIRoutePanelAction openride_ui_route_panel_hit_test(
    const OpenRideUIContext *ui,
    OpenRideRidePlannerMode mode,
    double x_px,
    double y_px);

OpenRideUIRoutePanelAction openride_ui_route_panel_draw(
    OpenRideUIContext *ui,
    const OpenRideUIRoutePanelState *state);

#endif
