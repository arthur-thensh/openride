#ifndef OPENRIDE_UI_MAP_OVERLAY_H
#define OPENRIDE_UI_MAP_OVERLAY_H

#include "openride/ui.h"

#include <stdbool.h>
#include <stdint.h>

#define OPENRIDE_UI_MAP_OVERLAY_MAX_LINES 12U

typedef struct OpenRideUIMapOverlayState {
    bool compact;
    const char *title;
    const char *summary;
    bool route_ready;
    const char *route_ready_text;

    uint32_t line_count;
    const char *lines[OPENRIDE_UI_MAP_OVERLAY_MAX_LINES];

    bool show_distance;
    const char *distance_title;
    const char *distance_text;
    const char *duration_text;

    const char *attribution;
} OpenRideUIMapOverlayState;

void openride_ui_map_overlay_draw(OpenRideUIContext *ui,
                                  const OpenRideUIMapOverlayState *state);

#endif
