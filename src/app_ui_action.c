#include "openride/app_ui_action.h"

#include "openride/ride_planner.h"
#include "openride/ui.h"
#include "openride/ui_loop_proposals_panel.h"
#include "openride/ui_main_menu.h"
#include "openride/ui_places_panel.h"
#include "openride/ui_regions_panel.h"
#include "openride/ui_route_downloads_panel.h"
#include "openride/ui_route_panel.h"
#include "openride/ui_settings_panel.h"

static OpenRideAppUIAction no_action(void)
{
    const OpenRideAppUIAction action = {OPENRIDE_APP_UI_NONE, -1};
    return action;
}

static bool begin_ui(OpenRideUIContext *ui,
                     SDL_Renderer *renderer,
                     int viewport_width,
                     int viewport_height)
{
    openride_ui_init(ui);
    return openride_ui_begin(ui, renderer, viewport_width, viewport_height);
}

OpenRideAppUIAction openride_app_ui_main_menu_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height)
{
    OpenRideAppUIAction hit = no_action();
    OpenRideUIContext ui;
    if (!begin_ui(&ui, renderer, viewport_width, viewport_height)) return hit;

    switch (openride_ui_main_menu_hit_test(&ui, x, y)) {
        case OPENRIDE_UI_MAIN_MENU_SEARCH: hit.action = OPENRIDE_APP_UI_SEARCH; break;
        case OPENRIDE_UI_MAIN_MENU_FAVORITES: hit.action = OPENRIDE_APP_UI_FAVORITES; break;
        case OPENRIDE_UI_MAIN_MENU_HISTORY: hit.action = OPENRIDE_APP_UI_HISTORY; break;
        case OPENRIDE_UI_MAIN_MENU_REGIONS: hit.action = OPENRIDE_APP_UI_REGIONS; break;
        case OPENRIDE_UI_MAIN_MENU_SETTINGS: hit.action = OPENRIDE_APP_UI_SETTINGS; break;
        case OPENRIDE_UI_MAIN_MENU_MAP_ZOOM_TEST: hit.action = OPENRIDE_APP_UI_MAP_ZOOM_TEST; break;
        case OPENRIDE_UI_MAIN_MENU_CLOSE: hit.action = OPENRIDE_APP_UI_CLOSE; break;
        case OPENRIDE_UI_MAIN_MENU_NONE:
        default: break;
    }
    openride_ui_end(&ui);
    return hit;
}

OpenRideAppUIAction openride_app_ui_route_panel_hit_test(
    SDL_Renderer *renderer,
    int planner_mode,
    double x,
    double y,
    int viewport_width,
    int viewport_height)
{
    OpenRideAppUIAction hit = no_action();
    OpenRideUIContext ui;
    if (!begin_ui(&ui, renderer, viewport_width, viewport_height)) return hit;
    OpenRideRidePlannerMode mode = planner_mode == (int)OPENRIDE_RIDE_PLANNER_LOOP
        ? OPENRIDE_RIDE_PLANNER_LOOP : OPENRIDE_RIDE_PLANNER_ROUTE;

    switch (openride_ui_route_panel_hit_test(&ui, mode, x, y)) {
        case OPENRIDE_UI_ROUTE_PANEL_MODE_ROUTE:
            hit.action = OPENRIDE_APP_UI_ROUTE_MODE_ROUTE; break;
        case OPENRIDE_UI_ROUTE_PANEL_MODE_LOOP:
            hit.action = OPENRIDE_APP_UI_ROUTE_MODE_LOOP; break;
        case OPENRIDE_UI_ROUTE_PANEL_GPS_START:
            hit.action = OPENRIDE_APP_UI_ROUTE_GPS_START; break;
        case OPENRIDE_UI_ROUTE_PANEL_SEARCH_START:
            hit.action = OPENRIDE_APP_UI_ROUTE_SEARCH_START; break;
        case OPENRIDE_UI_ROUTE_PANEL_MAP_START:
            hit.action = OPENRIDE_APP_UI_ROUTE_MAP_START; break;
        case OPENRIDE_UI_ROUTE_PANEL_SEARCH_DESTINATION:
            hit.action = OPENRIDE_APP_UI_ROUTE_SEARCH_DESTINATION; break;
        case OPENRIDE_UI_ROUTE_PANEL_MAP_DESTINATION:
            hit.action = OPENRIDE_APP_UI_ROUTE_MAP_DESTINATION; break;
        case OPENRIDE_UI_ROUTE_PANEL_PROFILE_FASTEST:
            hit.action = OPENRIDE_APP_UI_ROUTE_PROFILE_FASTEST; break;
        case OPENRIDE_UI_ROUTE_PANEL_PROFILE_TOURING:
            hit.action = OPENRIDE_APP_UI_ROUTE_PROFILE_TOURING; break;
        case OPENRIDE_UI_ROUTE_PANEL_PROFILE_TRAIL:
            hit.action = OPENRIDE_APP_UI_ROUTE_PROFILE_TRAIL; break;
        case OPENRIDE_UI_ROUTE_PANEL_LOOP_DISTANCE_DOWN:
            hit.action = OPENRIDE_APP_UI_ROUTE_LOOP_DISTANCE_DOWN; break;
        case OPENRIDE_UI_ROUTE_PANEL_LOOP_DISTANCE_UP:
            hit.action = OPENRIDE_APP_UI_ROUTE_LOOP_DISTANCE_UP; break;
        case OPENRIDE_UI_ROUTE_PANEL_LOOP_DIRECTION:
            hit.action = OPENRIDE_APP_UI_ROUTE_LOOP_DIRECTION; break;
        case OPENRIDE_UI_ROUTE_PANEL_CALCULATE:
            hit.action = OPENRIDE_APP_UI_ROUTE_CALCULATE; break;
        case OPENRIDE_UI_ROUTE_PANEL_BACK:
            hit.action = OPENRIDE_APP_UI_BACK; break;
        case OPENRIDE_UI_ROUTE_PANEL_NONE:
        default: break;
    }
    openride_ui_end(&ui);
    return hit;
}

OpenRideAppUIAction openride_app_ui_loop_proposals_hit_test(
    SDL_Renderer *renderer,
    uint32_t proposal_count,
    double x,
    double y,
    int viewport_width,
    int viewport_height)
{
    OpenRideAppUIAction hit = no_action();
    OpenRideUIContext ui;
    if (!begin_ui(&ui, renderer, viewport_width, viewport_height)) return hit;
    const OpenRideUILoopProposalsHit proposal_hit =
        openride_ui_loop_proposals_hit_test(&ui, proposal_count, x, y);
    if (proposal_hit.action == OPENRIDE_UI_LOOP_PROPOSALS_SELECT) {
        hit.action = OPENRIDE_APP_UI_LOOP_PROPOSAL_SELECT;
        hit.index = proposal_hit.index;
    } else if (proposal_hit.action == OPENRIDE_UI_LOOP_PROPOSALS_CONFIRM) {
        hit.action = OPENRIDE_APP_UI_LOOP_PROPOSAL_CONFIRM;
        hit.index = proposal_hit.index;
    } else if (proposal_hit.action == OPENRIDE_UI_LOOP_PROPOSALS_REGENERATE) {
        hit.action = OPENRIDE_APP_UI_LOOP_PROPOSALS_REGENERATE;
    } else if (proposal_hit.action == OPENRIDE_UI_LOOP_PROPOSALS_BACK) {
        hit.action = OPENRIDE_APP_UI_BACK;
    }
    openride_ui_end(&ui);
    return hit;
}

OpenRideAppUIMapInsets openride_app_ui_loop_proposals_map_insets(
    SDL_Renderer *renderer,
    uint32_t proposal_count,
    int viewport_width,
    int viewport_height)
{
    OpenRideAppUIMapInsets insets = {24.0, 24.0, 24.0, 24.0};
    if (!renderer || viewport_width <= 0 || viewport_height <= 0) return insets;

    OpenRideUIContext ui;
    if (!begin_ui(&ui, renderer, viewport_width, viewport_height)) return insets;

    const OpenRideUILoopProposalsLayout layout =
        openride_ui_loop_proposals_layout(&ui, proposal_count);
    if (layout.panel.w > 0.0f && layout.panel.h > 0.0f) {
        const SDL_FRect panel_px = openride_ui_rect_pixels(&ui, layout.panel);
        const double margin = 18.0 * (ui.scale > 0.0f ? (double)ui.scale : 1.0);
        insets.left = (double)ui.safe_area_px.x + margin;
        insets.top = (double)ui.safe_area_px.y + margin;
        insets.right =
            (double)viewport_width
            - (double)(ui.safe_area_px.x + ui.safe_area_px.w)
            + margin;
        insets.bottom = (double)viewport_height - (double)panel_px.y + margin;

        if (insets.left < 0.0) insets.left = 0.0;
        if (insets.top < 0.0) insets.top = 0.0;
        if (insets.right < 0.0) insets.right = 0.0;
        if (insets.bottom < 0.0) insets.bottom = 0.0;
    }
    openride_ui_end(&ui);
    return insets;
}


OpenRideAppUIAction openride_app_ui_route_downloads_panel_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height)
{
    OpenRideAppUIAction hit = no_action();
    OpenRideUIContext ui;
    if (!begin_ui(&ui, renderer, viewport_width, viewport_height)) return hit;
    switch (openride_ui_route_downloads_panel_hit_test(&ui, x, y)) {
        case OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_DOWNLOAD:
            hit.action = OPENRIDE_APP_UI_ROUTE_DOWNLOAD_REQUIRED; break;
        case OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_USE_INSTALLED:
            hit.action = OPENRIDE_APP_UI_ROUTE_USE_INSTALLED; break;
        case OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_BACK:
            hit.action = OPENRIDE_APP_UI_BACK; break;
        case OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_NONE:
        default: break;
    }
    openride_ui_end(&ui);
    return hit;
}

OpenRideAppUIAction openride_app_ui_settings_panel_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height)
{
    OpenRideAppUIAction hit = no_action();
    OpenRideUIContext ui;
    if (!begin_ui(&ui, renderer, viewport_width, viewport_height)) return hit;
    switch (openride_ui_settings_panel_hit_test(&ui, x, y)) {
        case OPENRIDE_UI_SETTINGS_PANEL_STYLE: hit.action = OPENRIDE_APP_UI_SETTINGS_STYLE; break;
        case OPENRIDE_UI_SETTINGS_PANEL_PROFILE: hit.action = OPENRIDE_APP_UI_SETTINGS_PROFILE; break;
        case OPENRIDE_UI_SETTINGS_PANEL_FOLLOW: hit.action = OPENRIDE_APP_UI_SETTINGS_FOLLOW; break;
        case OPENRIDE_UI_SETTINGS_PANEL_REROUTE: hit.action = OPENRIDE_APP_UI_SETTINGS_REROUTE; break;
        case OPENRIDE_UI_SETTINGS_PANEL_VOICE: hit.action = OPENRIDE_APP_UI_SETTINGS_VOICE; break;
        case OPENRIDE_UI_SETTINGS_PANEL_GPS_SIMULATION: hit.action = OPENRIDE_APP_UI_SETTINGS_GPS_SIMULATION; break;
        case OPENRIDE_UI_SETTINGS_PANEL_GPS_DEVIATION: hit.action = OPENRIDE_APP_UI_SETTINGS_GPS_DEVIATION; break;
        case OPENRIDE_UI_SETTINGS_PANEL_GPS_SPEED: hit.action = OPENRIDE_APP_UI_SETTINGS_GPS_SPEED; break;
        case OPENRIDE_UI_SETTINGS_PANEL_GPS_MISSED_TURN: hit.action = OPENRIDE_APP_UI_SETTINGS_GPS_MISSED_TURN; break;
        case OPENRIDE_UI_SETTINGS_PANEL_BACK: hit.action = OPENRIDE_APP_UI_BACK; break;
        case OPENRIDE_UI_SETTINGS_PANEL_NONE:
        default: break;
    }
    openride_ui_end(&ui);
    return hit;
}

OpenRideAppUIAction openride_app_ui_regions_panel_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height)
{
    OpenRideAppUIAction hit = no_action();
    OpenRideUIContext ui;
    if (!begin_ui(&ui, renderer, viewport_width, viewport_height)) return hit;
    switch (openride_ui_regions_panel_hit_test(&ui, x, y)) {
        case OPENRIDE_UI_REGIONS_PANEL_PREVIOUS: hit.action = OPENRIDE_APP_UI_REGION_PREVIOUS; break;
        case OPENRIDE_UI_REGIONS_PANEL_NEXT: hit.action = OPENRIDE_APP_UI_REGION_NEXT; break;
        case OPENRIDE_UI_REGIONS_PANEL_INSTALL: hit.action = OPENRIDE_APP_UI_REGION_INSTALL; break;
        case OPENRIDE_UI_REGIONS_PANEL_REMOVE: hit.action = OPENRIDE_APP_UI_REGION_REMOVE; break;
        case OPENRIDE_UI_REGIONS_PANEL_BACK: hit.action = OPENRIDE_APP_UI_BACK; break;
        case OPENRIDE_UI_REGIONS_PANEL_NONE:
        default: break;
    }
    openride_ui_end(&ui);
    return hit;
}

OpenRideAppUIAction openride_app_ui_places_panel_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height,
    uint32_t item_count)
{
    OpenRideAppUIAction hit = no_action();
    OpenRideUIContext ui;
    if (!begin_ui(&ui, renderer, viewport_width, viewport_height)) return hit;
    const OpenRideUIPlacesPanelHit places_hit =
        openride_ui_places_panel_hit_test(&ui, item_count, x, y);
    if (places_hit.action == OPENRIDE_UI_PLACES_PANEL_PLACE) {
        hit.action = OPENRIDE_APP_UI_PLACE;
        hit.index = places_hit.index;
    } else if (places_hit.action == OPENRIDE_UI_PLACES_PANEL_BACK) {
        hit.action = OPENRIDE_APP_UI_BACK;
    }
    openride_ui_end(&ui);
    return hit;
}
