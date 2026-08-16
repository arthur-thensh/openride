#ifndef OPENRIDE_APP_ROUTE_RUNTIME_H
#define OPENRIDE_APP_ROUTE_RUNTIME_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_atomic.h>

#include "openride/gps_simulator.h"
#include "openride/gpx.h"
#include "openride/location_filter.h"
#include "openride/loop_generator.h"
#include "openride/map_camera.h"
#include "openride/map_selection.h"
#include "openride/navigation_engine.h"
#include "openride/navigation_instructions.h"
#include "openride/navigation_session.h"
#include "openride/platform_paths.h"
#include "openride/region_manager.h"
#include "openride/routing_engine.h"
#include "openride/routing_graph.h"
#include "openride/routing_world.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct OpenRideRoutingWorldThreadContext {
    OpenRidePlatformPaths paths;
    const OpenRideRegionDefinition *active_region;
    const OpenRideRoutingGraph *active_graph;
    OpenRideRoutingWorldCache *cache;
    OpenRideMapSelection selection;
    OpenRideRoutingProfile profile;
    bool installed_alternative;
    bool reroute;
    bool resume_simulator;
    SDL_AtomicInt done;
    SDL_AtomicInt success;
    OpenRideRoute route;
    OpenRideRoutingWorldResult result;
    char error[256];
} OpenRideRoutingWorldThreadContext;

void openride_app_route_fit_camera_to_gpx(
    OpenRideMapCamera *camera,
    const OpenRideGPXDocument *document,
    int viewport_width,
    int viewport_height,
    double min_zoom,
    double max_zoom);

bool openride_app_route_load_gpx_overlay(
    const char *path,
    OpenRideGPXDocument *document,
    char *status,
    size_t status_size);

void openride_app_route_record_gps_sample(
    OpenRideGPXDocument *recording,
    const OpenRideGPSSample *sample,
    double *last_recorded_position_m);

bool openride_app_route_recalculate(
    const OpenRideRoutingGraph *graph,
    bool graph_loaded,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile,
    OpenRideRoute *route,
    OpenRideRoutingSnap *start_snap,
    OpenRideRoutingSnap *destination_snap,
    char *status,
    size_t status_size);

SDL_Thread *openride_app_route_start_world_thread(
    OpenRideRoutingWorldThreadContext *context,
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *active_region,
    const OpenRideRoutingGraph *active_graph,
    OpenRideRoutingWorldCache *cache,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile,
    bool reroute,
    bool resume_simulator);

SDL_Thread *openride_app_route_start_world_installed_alternative_thread(
    OpenRideRoutingWorldThreadContext *context,
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *active_region,
    const OpenRideRoutingGraph *active_graph,
    OpenRideRoutingWorldCache *cache,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile);

bool openride_app_route_world_request_matches(
    const OpenRideRoutingWorldThreadContext *context,
    const OpenRideRegionDefinition *active_region,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile);

bool openride_app_route_generate_loop(
    const OpenRideRoutingGraph *graph,
    bool graph_loaded,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile,
    double target_distance_m,
    OpenRideLoopDirection direction,
    uint32_t seed,
    OpenRideRoute *route,
    OpenRideLoopStats *stats,
    OpenRideRoutePoint waypoints[OPENRIDE_LOOP_MAX_WAYPOINTS],
    uint32_t *waypoint_count,
    OpenRideRoutingSnap *start_snap,
    char *status,
    size_t status_size);

void openride_app_route_clear_navigation_session(
    OpenRideNavigationEngine *navigation,
    OpenRideGPSSimulator *simulator,
    OpenRideNavigationState *navigation_state,
    OpenRideGPSSample *gps_sample,
    bool *gps_sample_valid);

bool openride_app_route_prepare_navigation_session(
    OpenRideNavigationEngine *navigation,
    OpenRideGPSSimulator *simulator,
    OpenRideNavigationInstructionList *instructions,
    const OpenRideRoutingGraph *graph,
    const OpenRideRoute *route,
    char *status,
    size_t status_size);

bool openride_app_route_reroute_from_position(
    const OpenRideRoutingGraph *graph,
    bool graph_loaded,
    OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile,
    double lat,
    double lon,
    OpenRideRoute *route,
    OpenRideRoutingSnap *start_snap,
    OpenRideRoutingSnap *destination_snap,
    OpenRideNavigationEngine *navigation,
    OpenRideGPSSimulator *simulator,
    OpenRideNavigationInstructionList *instructions,
    OpenRideNavigationSession *session,
    OpenRideLocationFilter *location_filter,
    bool resume_simulator,
    char *status,
    size_t status_size);

bool openride_app_route_prepare_gpx_navigation(
    const OpenRideGPXDocument *document,
    OpenRideRoutingGraph *graph,
    OpenRideMapSelection *selection,
    OpenRideRoute *route,
    OpenRideNavigationEngine *navigation,
    OpenRideGPSSimulator *simulator,
    OpenRideNavigationInstructionList *instructions,
    OpenRideNavigationSession *session,
    OpenRideLocationFilter *location_filter,
    char *status,
    size_t status_size);

bool openride_app_route_add_selection_from_screen(
    OpenRideMapSelection *selection,
    const OpenRideMapCamera *camera,
    double screen_x,
    double screen_y,
    int viewport_width,
    int viewport_height,
    bool *route_dirty,
    bool *loop_active,
    uint32_t *loop_waypoint_count,
    char *status,
    size_t status_size);

#endif
