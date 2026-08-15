#ifndef OPENRIDE_UI_ROUTE_PANEL_H
#define OPENRIDE_UI_ROUTE_PANEL_H

#include "openride/ui.h"

#include <stdbool.h>

typedef enum OpenRideUIRoutePanelAction {
    OPENRIDE_UI_ROUTE_PANEL_NONE = 0,
    OPENRIDE_UI_ROUTE_PANEL_GPS_START,
    OPENRIDE_UI_ROUTE_PANEL_SEARCH_START,
    OPENRIDE_UI_ROUTE_PANEL_MAP_START,
    OPENRIDE_UI_ROUTE_PANEL_SEARCH_DESTINATION,
    OPENRIDE_UI_ROUTE_PANEL_MAP_DESTINATION,
    OPENRIDE_UI_ROUTE_PANEL_CALCULATE,
    OPENRIDE_UI_ROUTE_PANEL_BACK
} OpenRideUIRoutePanelAction;

typedef struct OpenRideUIRoutePanelState {
    bool has_start;
    bool has_destination;
    bool gps_valid;
    double gps_accuracy_m;
} OpenRideUIRoutePanelState;

typedef struct OpenRideUIRoutePanelLayout {
    OpenRideUIRect panel;
    OpenRideUIRect title;
    OpenRideUIRect subtitle;
    OpenRideUIRect items[6];
    OpenRideUIRect hint;
    OpenRideUIRect back;
} OpenRideUIRoutePanelLayout;

OpenRideUIRoutePanelLayout openride_ui_route_panel_layout(
    const OpenRideUIContext *ui);

OpenRideUIRoutePanelAction openride_ui_route_panel_hit_test(
    const OpenRideUIContext *ui,
    double x_px,
    double y_px);

OpenRideUIRoutePanelAction openride_ui_route_panel_draw(
    OpenRideUIContext *ui,
    const OpenRideUIRoutePanelState *state);

#endif
