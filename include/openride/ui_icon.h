#ifndef OPENRIDE_UI_ICON_H
#define OPENRIDE_UI_ICON_H

#include "openride/ui.h"

#include <stdbool.h>

typedef enum OpenRideUIIcon {
    OPENRIDE_UI_ICON_MENU = 0,
    OPENRIDE_UI_ICON_SEARCH,
    OPENRIDE_UI_ICON_ROUTE,
    OPENRIDE_UI_ICON_LOOP,
    OPENRIDE_UI_ICON_GPS,
    OPENRIDE_UI_ICON_FAVORITE,
    OPENRIDE_UI_ICON_HISTORY,
    OPENRIDE_UI_ICON_DOWNLOAD,
    OPENRIDE_UI_ICON_SETTINGS,
    OPENRIDE_UI_ICON_CLOSE,
    OPENRIDE_UI_ICON_MAP,
    OPENRIDE_UI_ICON_COMPASS,
    OPENRIDE_UI_ICON_LOCATION,
    OPENRIDE_UI_ICON_LOADING,
    OPENRIDE_UI_ICON_COUNT
} OpenRideUIIcon;

/*
 * Returns the canonical SVG source used by the UI engine for an icon.
 * Sources intentionally use a small portable SVG subset so they can be parsed
 * and rendered directly by OpenRide without a platform SVG dependency.
 */
const char *openride_ui_icon_svg(OpenRideUIIcon icon);

/*
 * Render a cached SVG icon into a logical UI rectangle. The same source can be
 * drawn at any size and tinted at runtime by the current UI theme/state.
 */
bool openride_ui_icon_draw(OpenRideUIContext *ui,
                           OpenRideUIIcon icon,
                           OpenRideUIRect rect,
                           OpenRideUIColor color,
                           float stroke_width);

/* Render the same cached SVG after rotating it around the target center. */
bool openride_ui_icon_draw_rotated(OpenRideUIContext *ui,
                                   OpenRideUIIcon icon,
                                   OpenRideUIRect rect,
                                   OpenRideUIColor color,
                                   float stroke_width,
                                   float angle_degrees);

#endif
