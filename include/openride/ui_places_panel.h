#ifndef OPENRIDE_UI_PLACES_PANEL_H
#define OPENRIDE_UI_PLACES_PANEL_H

#include "openride/ui.h"

#include <stdint.h>

#define OPENRIDE_UI_PLACES_PANEL_MAX_ITEMS 12U

typedef enum OpenRideUIPlacesPanelMode {
    OPENRIDE_UI_PLACES_PANEL_FAVORITES = 0,
    OPENRIDE_UI_PLACES_PANEL_HISTORY
} OpenRideUIPlacesPanelMode;

typedef enum OpenRideUIPlacesPanelAction {
    OPENRIDE_UI_PLACES_PANEL_NONE = 0,
    OPENRIDE_UI_PLACES_PANEL_PLACE,
    OPENRIDE_UI_PLACES_PANEL_BACK
} OpenRideUIPlacesPanelAction;

typedef struct OpenRideUIPlacesPanelHit {
    OpenRideUIPlacesPanelAction action;
    int index;
} OpenRideUIPlacesPanelHit;

typedef struct OpenRideUIPlacesPanelState {
    OpenRideUIPlacesPanelMode mode;
    const char *items[OPENRIDE_UI_PLACES_PANEL_MAX_ITEMS];
    uint32_t count;
    uint32_t selected;
} OpenRideUIPlacesPanelState;

typedef struct OpenRideUIPlacesPanelLayout {
    OpenRideUIRect panel;
    OpenRideUIRect title;
    OpenRideUIRect subtitle;
    OpenRideUIRect items[OPENRIDE_UI_PLACES_PANEL_MAX_ITEMS];
    uint32_t item_count;
    OpenRideUIRect back;
} OpenRideUIPlacesPanelLayout;

OpenRideUIPlacesPanelLayout openride_ui_places_panel_layout(
    const OpenRideUIContext *ui,
    uint32_t item_count);

OpenRideUIPlacesPanelHit openride_ui_places_panel_hit_test(
    const OpenRideUIContext *ui,
    uint32_t item_count,
    double x_px,
    double y_px);

OpenRideUIPlacesPanelHit openride_ui_places_panel_draw(
    OpenRideUIContext *ui,
    const OpenRideUIPlacesPanelState *state);

#endif
