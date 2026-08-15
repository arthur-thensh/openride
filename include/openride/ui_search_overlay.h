#ifndef OPENRIDE_UI_SEARCH_OVERLAY_H
#define OPENRIDE_UI_SEARCH_OVERLAY_H

#include "openride/ui.h"

#include <stdbool.h>
#include <stdint.h>

#define OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS 8U

typedef struct OpenRideUISearchOverlayItem {
    const char *name;
    const char *secondary;
} OpenRideUISearchOverlayItem;

typedef struct OpenRideUISearchOverlayState {
    bool available;
    const char *title;
    const char *query;
    uint32_t count;
    uint32_t selected;
    OpenRideUISearchOverlayItem items[OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS];
} OpenRideUISearchOverlayState;

typedef struct OpenRideUISearchOverlayLayout {
    OpenRideUIRect panel;
    OpenRideUIRect title;
    OpenRideUIRect query;
    OpenRideUIRect rows[OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS];
    uint32_t row_count;
    OpenRideUIRect message;
} OpenRideUISearchOverlayLayout;

OpenRideUISearchOverlayLayout openride_ui_search_overlay_layout(
    const OpenRideUIContext *ui,
    uint32_t result_count);

int openride_ui_search_overlay_result_at(
    const OpenRideUIContext *ui,
    uint32_t result_count,
    double x_px,
    double y_px);

void openride_ui_search_overlay_draw(
    OpenRideUIContext *ui,
    const OpenRideUISearchOverlayState *state);

#endif
