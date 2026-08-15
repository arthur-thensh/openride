#ifndef OPENRIDE_UI_NAVIGATION_OVERLAY_H
#define OPENRIDE_UI_NAVIGATION_OVERLAY_H

#include "openride/ui.h"

#include <stdint.h>

#define OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES 7U

typedef struct OpenRideUINavigationOverlayState {
    const char *title;
    const char *lines[OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES];
    uint32_t line_count;
} OpenRideUINavigationOverlayState;

void openride_ui_navigation_overlay_draw(
    OpenRideUIContext *ui,
    const OpenRideUINavigationOverlayState *state);

#endif
