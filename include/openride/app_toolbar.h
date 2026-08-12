#ifndef OPENRIDE_APP_TOOLBAR_H
#define OPENRIDE_APP_TOOLBAR_H

typedef enum OpenRideToolbarAction {
    OPENRIDE_TOOLBAR_NONE = 0,
    OPENRIDE_TOOLBAR_MENU,
    OPENRIDE_TOOLBAR_SEARCH,
    OPENRIDE_TOOLBAR_ROUTE,
    OPENRIDE_TOOLBAR_LOOP,
    OPENRIDE_TOOLBAR_GPS
} OpenRideToolbarAction;

typedef struct OpenRideToolbarRect {
    double x;
    double y;
    double w;
    double h;
} OpenRideToolbarRect;

OpenRideToolbarRect openride_toolbar_bounds(int viewport_width, int viewport_height);
OpenRideToolbarRect openride_toolbar_item_bounds(OpenRideToolbarAction action,
                                                  int viewport_width,
                                                  int viewport_height);
OpenRideToolbarAction openride_toolbar_hit_test(double x,
                                                double y,
                                                int viewport_width,
                                                int viewport_height);
const char *openride_toolbar_action_label(OpenRideToolbarAction action);

#endif
