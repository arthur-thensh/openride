#ifndef OPENRIDE_APP_EVENT_RUNTIME_H
#define OPENRIDE_APP_EVENT_RUNTIME_H

#include "app_support_runtime.h"
#include "app_planner_async_runtime.h"

#include <stddef.h>
#include <stdint.h>

typedef struct OpenRideAppEventContext {
    SDL_Window *window;
    SDL_Renderer *renderer;
    bool *running;

    OpenRidePlatformPaths *platform_paths;
    OpenRideMapCamera *camera;
    OpenRideMapZoomTest *map_zoom_test;
    OpenRideMapSelection *selection;
    OpenRideRoute *route;
    OpenRideRoutingGraph *routing_graph;
    bool *graph_loaded;
    OpenRideRoutingProfile *routing_profile;
    OpenRideMapStyle *map_style;
    bool *scalable_map;
    bool *ormap_map;
    bool *vector_map;
    OpenRideORMapRenderer *ormap_renderer;
    OpenRideVectorMapRenderer *vector_renderer;
    const OpenRideMBTilesMetadata **metadata;
    OpenRideMapWorld *map_world;

    OpenRideRoutingWorldCache *routing_world_cache;
    OpenRideRoutingWorldThreadContext *routing_world_context;
    SDL_Thread **routing_world_thread;
    bool *routing_world_pending_reroute;
    bool *routing_world_pending_resume_simulator;

    OpenRideNavigationEngine *navigation;
    OpenRideNavigationInstructionList *navigation_instructions;
    OpenRideNavigationSession *navigation_session;
    OpenRideLocationFilter *location_filter;
    OpenRideFilteredLocation *filtered_location;
    OpenRideVoiceGuidance *voice_guidance;
    OpenRideGPSSimulator *gps_simulator;
    OpenRideNavigationState *navigation_state;
    OpenRideGPSSample *gps_sample;
    bool *gps_sample_valid;
    OpenRideDriveModeState *drive_mode;

    bool *follow_gps;
    bool *auto_reroute;
    bool *voice_enabled;
    bool *simulator_deviation;
    bool *gpx_navigation_active;

    OpenRideGPXDocument *gpx_overlay;
    OpenRideGPXDocument *gpx_recording;
    bool *gpx_loaded;
    bool *gpx_recording_active;
    double *gpx_last_recorded_position_m;
    const char *gpx_import_path;
    const char *gpx_route_export_path;
    const char *gpx_recording_export_path;

    OpenRideRidePlannerMode *planner_mode;
    OpenRideRidePlannerBusy *planner_busy;
    OpenRidePlannerAsyncContext *planner_async_context;
    SDL_Thread **planner_async_thread;
    OpenRideLoopProposalSet *loop_proposals;
    OpenRideRouteChoice *route_choice;
    double *loop_target_distance_m;
    OpenRideLoopDirection *loop_direction;
    OpenRideLoopStats *loop_stats;
    OpenRideRoutePoint *loop_waypoints;
    uint32_t *loop_waypoint_count;
    uint32_t *loop_seed;
    bool *loop_active;
    OpenRideRoutingSnap *start_snap;
    OpenRideRoutingSnap *destination_snap;
    bool *route_valid;
    bool *route_dirty;

    OpenRidePlaceWorld *place_world;
    OpenRideAppStorage *app_storage;
    bool *place_search_active;
    OpenRidePlaceSearchPurpose *place_search_purpose;
    char *place_search_query;
    size_t place_search_query_size;
    OpenRidePlaceSearchResult *place_search_results;
    uint32_t *place_search_result_count;
    uint32_t *place_search_selected;
    OpenRideStoredPlace *favorite_places;
    OpenRideStoredPlace *history_places;
    uint32_t *favorite_count;
    uint32_t *history_count;

    OpenRideAppPanel *app_panel;
    uint32_t *app_panel_selected;
    OpenRideRouteDownloadPlan *route_download_plan;
    OpenRideSelectionMarker *route_map_pick_marker;

    const OpenRideRegionDefinition **region;
    const OpenRideRegionDefinition **active_region;
    OpenRideRegionStatus *region_status;
    bool *region_busy;
    bool *region_activation_requested;
    double *region_progress;
    char *region_work_status;
    size_t region_work_status_size;

    bool *dragging_map;
    bool *map_drag_moved;
    double *mouse_down_x;
    double *mouse_down_y;
    OpenRideSelectionMarker *dragging_marker;
    OpenRideTouchInput *touch_input;
    OpenRideToolbarAction *pending_toolbar_action;
    OpenRideDriveAction *pending_drive_action;

    char *route_status;
    size_t route_status_size;
    char *error;
    size_t error_size;

#ifdef __ANDROID__
    OpenRideLocationProvider *location_provider;
    OpenRideLocationProvider *simulated_location_provider;
    OpenRideSimulatedLocationContext *simulated_location_context;
    bool *real_gps_active;
    bool *real_gps_requested;
    bool *simulated_gps_active;
    bool *route_start_gps_pending;
    double *android_gps_sample_age_s;
    double *android_gps_accuracy_m;
    OpenRideAndroidMissedTurnDev *missed_turn_dev;
    OpenRideRegionPrepareThreadContext *region_prepare_context;
    SDL_Thread **region_prepare_thread;
    bool *region_download_started;
    bool *region_download_is_poly;
#endif
} OpenRideAppEventContext;

void openride_app_events_poll(OpenRideAppEventContext *context,
                              uint64_t *map_zoom_loop_started_ns);

void openride_app_events_dispatch_pending(OpenRideAppEventContext *context);

void openride_app_events_fit_route_choice_preview(
    OpenRideAppEventContext *context);

#endif
