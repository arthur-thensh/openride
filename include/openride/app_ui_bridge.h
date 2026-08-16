#ifndef OPENRIDE_APP_UI_BRIDGE_H
#define OPENRIDE_APP_UI_BRIDGE_H

#include <SDL3/SDL.h>

#include "openride/app_storage.h"
#include "openride/app_toolbar.h"
#include "openride/drive_mode.h"
#include "openride/gps_simulator.h"
#include "openride/gpx.h"
#include "openride/loop_generator.h"
#include "openride/map_camera.h"
#include "openride/map_selection.h"
#include "openride/map_style.h"
#include "openride/mbtiles.h"
#include "openride/navigation_engine.h"
#include "openride/navigation_instructions.h"
#include "openride/navigation_session.h"
#include "openride/place_search.h"
#include "openride/region_manager.h"
#include "openride/ride_planner.h"
#include "openride/routing_engine.h"
#include "openride/routing_world.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum OpenRidePlaceSearchPurpose {
    OPENRIDE_PLACE_SEARCH_BROWSE = 0,
    OPENRIDE_PLACE_SEARCH_ROUTE_START,
    OPENRIDE_PLACE_SEARCH_ROUTE_DESTINATION
} OpenRidePlaceSearchPurpose;

typedef struct OpenRideRouteDownloadPlan {
    bool available;
    bool downloading;
    bool has_installed_alternative;
    uint32_t count;
    uint32_t index;
    char region_ids[OPENRIDE_ROUTING_WORLD_MAX_CORRIDOR_REGIONS]
                   [OPENRIDE_ROUTING_WORLD_REGION_ID_SIZE];
    OpenRideMapSelection selection;
    OpenRideRoutingProfile profile;
} OpenRideRouteDownloadPlan;

typedef enum OpenRideAppPanel {
    OPENRIDE_APP_PANEL_NONE = 0,
    OPENRIDE_APP_PANEL_MAIN,
    OPENRIDE_APP_PANEL_ROUTE,
    OPENRIDE_APP_PANEL_LOOP_PROPOSALS,
    OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS,
    OPENRIDE_APP_PANEL_FAVORITES,
    OPENRIDE_APP_PANEL_HISTORY,
    OPENRIDE_APP_PANEL_REGIONS,
    OPENRIDE_APP_PANEL_SETTINGS
} OpenRideAppPanel;

typedef enum OpenRideDriveAction {
    OPENRIDE_DRIVE_ACTION_NONE = 0,
    OPENRIDE_DRIVE_ACTION_EXIT,
    OPENRIDE_DRIVE_ACTION_RECENTER,
    OPENRIDE_DRIVE_ACTION_ORIENTATION,
    OPENRIDE_DRIVE_ACTION_GPS
} OpenRideDriveAction;

OpenRideToolbarAction openride_app_ui_toolbar_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height);

void openride_app_ui_draw_map_status_overlay(
    SDL_Renderer *renderer,
    const OpenRideMapCamera *camera,
    const OpenRideMapSelection *selection,
    const OpenRideMBTilesMetadata *metadata,
    bool scalable_map,
    bool graph_loaded,
    OpenRideRoutingProfile profile,
    OpenRideMapStyle map_style,
    const OpenRideRoute *route,
    bool route_valid,
    const char *route_status,
    const OpenRideRoutingSnap *start_snap,
    const OpenRideRoutingSnap *destination_snap,
    bool loop_active,
    double loop_target_distance_m,
    OpenRideLoopDirection loop_direction,
    const OpenRideLoopStats *loop_stats,
    const OpenRideGPXDocument *gpx_document,
    bool gpx_loaded,
    bool gpx_recording,
    bool gpx_navigation,
    bool compact,
    int viewport_width,
    int viewport_height);

void openride_app_ui_draw_place_search_overlay(
    SDL_Renderer *renderer,
    bool active,
    bool available,
    const char *title,
    const char *query,
    const OpenRidePlaceSearchResult *results,
    uint32_t result_count,
    uint32_t selected_result,
    int viewport_width);

int openride_app_ui_place_search_result_at(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height,
    uint32_t result_count);

OpenRideAppPanel openride_app_ui_panel_main_at(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height);

bool openride_app_ui_panel_main_search_at(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height);

int openride_app_ui_panel_place_at(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height,
    uint32_t count);

int openride_app_ui_panel_region_action_at(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height);

void openride_app_ui_draw_panel(
    SDL_Renderer *renderer,
    OpenRideAppPanel panel,
    const OpenRideStoredPlace *favorites,
    uint32_t favorite_count,
    const OpenRideStoredPlace *history,
    uint32_t history_count,
    uint32_t selected,
    OpenRideMapStyle map_style,
    OpenRideRoutingProfile profile,
    bool follow_gps,
    bool auto_reroute,
    bool voice_enabled,
    bool simulated_gps_active,
    bool simulated_gps_deviation,
    double simulated_gps_time_scale,
    bool simulated_missed_turn_armed,
    bool simulated_missed_turn_active,
    const OpenRideRegionDefinition *region,
    const OpenRideRegionStatus *region_status,
    bool region_is_active,
    bool region_busy,
    double region_progress,
    const char *region_work_status,
    const OpenRideMapSelection *selection,
    bool gps_valid,
    double gps_accuracy_m,
    OpenRideRidePlannerMode planner_mode,
    OpenRideRidePlannerBusy planner_busy,
    const char *planner_feedback,
    double loop_target_distance_m,
    OpenRideLoopDirection loop_direction,
    const OpenRideRouteChoice *route_choice,
    const OpenRideLoopProposalSet *loop_proposals,
    const OpenRideRouteDownloadPlan *route_download_plan_state,
    int viewport_width);

void openride_app_ui_draw_navigation_overlay(
    SDL_Renderer *renderer,
    const OpenRideNavigationState *navigation,
    const OpenRideNavigationInstructionList *instructions,
    const OpenRideGPSSimulator *simulator,
    const OpenRideRoute *route,
    const OpenRideNavigationSession *session,
    bool gps_sample_valid,
    bool follow_gps,
    bool auto_reroute,
    bool deviation_enabled,
    bool gpx_navigation,
    int viewport_height);

void openride_app_ui_draw_toolbar(
    SDL_Renderer *renderer,
    int viewport_width,
    int viewport_height,
    bool route_ready);

OpenRideDriveAction openride_app_ui_drive_controls_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height);

void openride_app_ui_draw_drive_mode(
    SDL_Renderer *renderer,
    const OpenRideMBTilesMetadata *metadata,
    const OpenRideNavigationState *navigation,
    const OpenRideNavigationInstructionList *instructions,
    const OpenRideRoute *route,
    const OpenRideNavigationSession *session,
    const OpenRideDriveModeState *drive,
    bool auto_reroute,
    bool simulated_gps,
    bool simulated_gps_deviation,
    double simulated_gps_time_scale,
    bool simulated_missed_turn_armed,
    bool simulated_missed_turn_active,
    int viewport_width,
    int viewport_height);

#endif
