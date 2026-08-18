#ifndef OPENRIDE_UI_REGIONS_PANEL_H
#define OPENRIDE_UI_REGIONS_PANEL_H

#include "openride/ui.h"

#include <stdbool.h>

typedef enum OpenRideUIRegionsPanelAction {
    OPENRIDE_UI_REGIONS_PANEL_NONE = 0,
    OPENRIDE_UI_REGIONS_PANEL_PREVIOUS,
    OPENRIDE_UI_REGIONS_PANEL_NEXT,
    OPENRIDE_UI_REGIONS_PANEL_INSTALL,
    OPENRIDE_UI_REGIONS_PANEL_REMOVE,
    OPENRIDE_UI_REGIONS_PANEL_BACK
} OpenRideUIRegionsPanelAction;

typedef struct OpenRideUIRegionsPanelState {
    const char *region_name;
    bool region_is_active;
    bool ormap_installed;
    bool ormap11_installed;
    bool routing_installed;
    bool search_installed;
    bool source_pbf_present;
    bool poly_present;
    bool ready;
    double total_size_mb;
    bool busy;
    double progress;
    const char *work_status;
} OpenRideUIRegionsPanelState;

typedef struct OpenRideUIRegionsPanelLayout {
    OpenRideUIRect panel;
    OpenRideUIRect title;
    OpenRideUIRect subtitle;
    OpenRideUIRect previous;
    OpenRideUIRect next;
    OpenRideUIRect status[3];
    OpenRideUIRect install;
    OpenRideUIRect remove;
    OpenRideUIRect work_status;
    OpenRideUIRect back;
} OpenRideUIRegionsPanelLayout;

OpenRideUIRegionsPanelLayout openride_ui_regions_panel_layout(
    const OpenRideUIContext *ui);

OpenRideUIRegionsPanelAction openride_ui_regions_panel_hit_test(
    const OpenRideUIContext *ui,
    double x_px,
    double y_px);

OpenRideUIRegionsPanelAction openride_ui_regions_panel_draw(
    OpenRideUIContext *ui,
    const OpenRideUIRegionsPanelState *state);

#endif
