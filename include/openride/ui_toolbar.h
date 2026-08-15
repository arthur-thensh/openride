#ifndef OPENRIDE_UI_TOOLBAR_H
#define OPENRIDE_UI_TOOLBAR_H

#include "openride/app_toolbar.h"
#include "openride/ui.h"

#include <stdbool.h>

typedef struct OpenRideUIToolbarLayout {
    OpenRideUIRect bar;
    OpenRideUIRect items[5];
} OpenRideUIToolbarLayout;

OpenRideUIToolbarLayout openride_ui_toolbar_layout(const OpenRideUIContext *ui);
OpenRideToolbarAction openride_ui_toolbar_hit_test(const OpenRideUIContext *ui,
                                                   double x,
                                                   double y);
OpenRideToolbarAction openride_ui_toolbar_draw(OpenRideUIContext *ui,
                                               bool route_ready);

#endif
