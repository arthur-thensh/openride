#ifndef OPENRIDE_UI_MAIN_MENU_H
#define OPENRIDE_UI_MAIN_MENU_H

#include "openride/ui.h"

typedef enum OpenRideUIMainMenuAction {
    OPENRIDE_UI_MAIN_MENU_NONE = 0,
    OPENRIDE_UI_MAIN_MENU_SEARCH,
    OPENRIDE_UI_MAIN_MENU_FAVORITES,
    OPENRIDE_UI_MAIN_MENU_HISTORY,
    OPENRIDE_UI_MAIN_MENU_REGIONS,
    OPENRIDE_UI_MAIN_MENU_SETTINGS,
    OPENRIDE_UI_MAIN_MENU_MAP_ZOOM_TEST,
    OPENRIDE_UI_MAIN_MENU_CLOSE
} OpenRideUIMainMenuAction;

typedef struct OpenRideUIMainMenuLayout {
    OpenRideUIRect panel;
    OpenRideUIRect title;
    OpenRideUIRect subtitle;
    OpenRideUIRect items[6];
    OpenRideUIRect close;
} OpenRideUIMainMenuLayout;

OpenRideUIMainMenuLayout openride_ui_main_menu_layout(
    const OpenRideUIContext *ui);

OpenRideUIMainMenuAction openride_ui_main_menu_hit_test(
    const OpenRideUIContext *ui,
    double x_px,
    double y_px);

OpenRideUIMainMenuAction openride_ui_main_menu_draw(OpenRideUIContext *ui);

#endif
