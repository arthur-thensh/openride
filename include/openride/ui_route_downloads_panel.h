#ifndef OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_H
#define OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_H

#include "openride/ui.h"

#include <stdbool.h>
#include <stdint.h>

#define OPENRIDE_UI_ROUTE_DOWNLOADS_MAX_REGIONS 6U

typedef enum OpenRideUIRouteDownloadsPanelAction {
    OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_NONE = 0,
    OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_DOWNLOAD,
    OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_USE_INSTALLED,
    OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_BACK
} OpenRideUIRouteDownloadsPanelAction;

typedef struct OpenRideUIRouteDownloadsPanelState {
    bool downloading;
    bool has_installed_alternative;
    uint32_t count;
    uint32_t current_index;
    const char *region_names[OPENRIDE_UI_ROUTE_DOWNLOADS_MAX_REGIONS];
    double progress;
    const char *work_status;
} OpenRideUIRouteDownloadsPanelState;

typedef struct OpenRideUIRouteDownloadsPanelLayout {
    OpenRideUIRect panel;
    OpenRideUIRect title;
    OpenRideUIRect subtitle;
    OpenRideUIRect regions[OPENRIDE_UI_ROUTE_DOWNLOADS_MAX_REGIONS];
    uint32_t region_count;
    OpenRideUIRect alternative;
    OpenRideUIRect status;
    OpenRideUIRect progress;
    OpenRideUIRect download;
    OpenRideUIRect use_installed;
    OpenRideUIRect back;
} OpenRideUIRouteDownloadsPanelLayout;

OpenRideUIRouteDownloadsPanelLayout openride_ui_route_downloads_panel_layout(
    const OpenRideUIContext *ui,
    uint32_t region_count);

OpenRideUIRouteDownloadsPanelAction openride_ui_route_downloads_panel_hit_test(
    const OpenRideUIContext *ui,
    double x_px,
    double y_px);

OpenRideUIRouteDownloadsPanelAction openride_ui_route_downloads_panel_draw(
    OpenRideUIContext *ui,
    const OpenRideUIRouteDownloadsPanelState *state);

#endif
