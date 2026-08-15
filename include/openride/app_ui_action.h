#ifndef OPENRIDE_APP_UI_ACTION_H
#define OPENRIDE_APP_UI_ACTION_H

#include <SDL3/SDL.h>

#include <stdint.h>

typedef enum OpenRideAppUIActionType {
    OPENRIDE_APP_UI_NONE = 0,
    OPENRIDE_APP_UI_CLOSE,
    OPENRIDE_APP_UI_BACK,
    OPENRIDE_APP_UI_SEARCH,
    OPENRIDE_APP_UI_ROUTE_GPS_START,
    OPENRIDE_APP_UI_ROUTE_SEARCH_START,
    OPENRIDE_APP_UI_ROUTE_MAP_START,
    OPENRIDE_APP_UI_ROUTE_SEARCH_DESTINATION,
    OPENRIDE_APP_UI_ROUTE_MAP_DESTINATION,
    OPENRIDE_APP_UI_ROUTE_CALCULATE,
    OPENRIDE_APP_UI_ROUTE_DOWNLOAD_REQUIRED,
    OPENRIDE_APP_UI_ROUTE_USE_INSTALLED,
    OPENRIDE_APP_UI_FAVORITES,
    OPENRIDE_APP_UI_HISTORY,
    OPENRIDE_APP_UI_REGIONS,
    OPENRIDE_APP_UI_SETTINGS,
    OPENRIDE_APP_UI_PLACE,
    OPENRIDE_APP_UI_REGION_PREVIOUS,
    OPENRIDE_APP_UI_REGION_NEXT,
    OPENRIDE_APP_UI_REGION_INSTALL,
    OPENRIDE_APP_UI_REGION_REMOVE,
    OPENRIDE_APP_UI_SETTINGS_STYLE,
    OPENRIDE_APP_UI_SETTINGS_PROFILE,
    OPENRIDE_APP_UI_SETTINGS_FOLLOW,
    OPENRIDE_APP_UI_SETTINGS_REROUTE,
    OPENRIDE_APP_UI_SETTINGS_VOICE,
    OPENRIDE_APP_UI_SETTINGS_GPS_SIMULATION,
    OPENRIDE_APP_UI_SETTINGS_GPS_DEVIATION,
    OPENRIDE_APP_UI_SETTINGS_GPS_SPEED,
    OPENRIDE_APP_UI_SETTINGS_GPS_MISSED_TURN,
    OPENRIDE_APP_UI_MAP_ZOOM_TEST
} OpenRideAppUIActionType;

typedef struct OpenRideAppUIAction {
    OpenRideAppUIActionType action;
    int index;
} OpenRideAppUIAction;

OpenRideAppUIAction openride_app_ui_main_menu_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height);

OpenRideAppUIAction openride_app_ui_route_panel_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height);

OpenRideAppUIAction openride_app_ui_route_downloads_panel_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height);

OpenRideAppUIAction openride_app_ui_settings_panel_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height);

OpenRideAppUIAction openride_app_ui_regions_panel_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height);

OpenRideAppUIAction openride_app_ui_places_panel_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height,
    uint32_t item_count);

#endif
