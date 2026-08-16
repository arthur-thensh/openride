#include "app_runtime.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_atomic.h>

#include "map/map_renderer.h"
#include "map/vector_map_renderer.h"
#include "map/ormap_renderer.h"
#include "map/map_zoom_test_logger.h"
#include "map/map_world.h"
#include "openride/map_camera.h"
#include "openride/map_selection.h"
#include "openride/loop_generator.h"
#include "openride/gps_simulator.h"
#include "openride/dev_missed_turn.h"
#include "openride/gpx.h"
#include "openride/navigation_engine.h"
#include "openride/navigation_instructions.h"
#include "openride/navigation_session.h"
#include "openride/voice_guidance.h"
#include "openride/location_filter.h"
#include "openride/location_provider.h"
#ifdef __ANDROID__
#include "openride/android_location_provider.h"
#include "openride/simulated_location_provider.h"
#include "openride/android_voice_guidance.h"
#include "openride/android_region_download.h"
#include <SDL3/SDL_system.h>
#include <unistd.h>
#endif
#include "openride/place_search.h"
#include "openride/place_world.h"
#include "openride/app_storage.h"
#include "openride/platform_paths.h"
#include "openride/region_manager.h"
#include "openride/region_install.h"
#include "openride/touch_input.h"
#include "openride/app_toolbar.h"
#include "openride/app_ui_action.h"
#include "openride/app_ui_bridge.h"
#include "app_search_runtime.h"
#include "app_route_runtime.h"
#include "app_region_runtime.h"
#include "app_support_runtime.h"
#include "app_event_runtime.h"
#include "app_async_runtime.h"
#include "openride/drive_mode.h"
#include "openride/app_lifecycle.h"
#include "openride/mbtiles.h"
#include "openride/ormap.h"
#include "openride/routing_engine.h"
#include "openride/routing_world.h"
#include "openride/routing_graph.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define OPENRIDE_CLICK_DRAG_THRESHOLD 5.0
#define OPENRIDE_MARKER_HIT_RADIUS 26.0
#define OPENRIDE_MAX_SNAP_DISTANCE_M 2000.0
#define OPENRIDE_LOOP_DISTANCE_STEP_M 25000.0
#define OPENRIDE_LOOP_DISTANCE_MIN_M 25000.0
#define OPENRIDE_LOOP_DISTANCE_MAX_M 300000.0
#define OPENRIDE_GPS_SIMULATION_TIME_SCALE 20.0
#define OPENRIDE_ANDROID_GPS_SIMULATION_TIME_SCALE 5.0
#define OPENRIDE_GPX_RECORDING_MIN_STEP_M 10.0
#define OPENRIDE_GPX_NAVIGATION_SPEED_KPH 50.0
#define OPENRIDE_SEARCH_MAX_RESULTS 8U
#define OPENRIDE_INTERACTIVE_FRAME_NS 16666667ULL
#define OPENRIDE_ACTIVE_FRAME_NS 33333333ULL
#define OPENRIDE_IDLE_FRAME_NS 66666667ULL
#define OPENRIDE_INTERACTIVE_HOLD_NS 750000000ULL
#define OPENRIDE_STARTUP_INTERACTIVE_NS 1500000000ULL
#define OPENRIDE_RENDER_FOLLOWUP_BUDGET 48U

#ifdef __ANDROID__
typedef struct OpenRideDirtyRenderAudit {
    Uint64 window_started_ns;
    uint32_t loops;
    uint32_t renders;
    uint32_t skips;
} OpenRideDirtyRenderAudit;

static void openride_dirty_render_audit_tick(
    OpenRideDirtyRenderAudit *audit,
    bool rendered)
{
    if (!audit) return;
    const Uint64 now_ns = SDL_GetTicksNS();
    if (audit->window_started_ns == 0U) audit->window_started_ns = now_ns;

    ++audit->loops;
    if (rendered) ++audit->renders;
    else ++audit->skips;

    const Uint64 elapsed_ns = now_ns - audit->window_started_ns;
    if (elapsed_ns < 2000000000ULL) return;

    const double seconds = (double)elapsed_ns / 1000000000.0;
    SDL_Log(
        "AUDIT_DIRTY_RENDER window_s=%.3f loop_hz=%.2f render_fps=%.2f loops=%u renders=%u skips=%u",
        seconds,
        seconds > 0.0 ? (double)audit->loops / seconds : 0.0,
        seconds > 0.0 ? (double)audit->renders / seconds : 0.0,
        audit->loops,
        audit->renders,
        audit->skips);

    audit->window_started_ns = now_ns;
    audit->loops = 0U;
    audit->renders = 0U;
    audit->skips = 0U;
}
#endif
#define OPENRIDE_APP_LIST_MAX 12U
#define OPENRIDE_REAL_MAP_PATH "data/maps/nord-pas-de-calais.ormap"
#define OPENRIDE_ROUTING_GRAPH_PATH "data/routing/nord-pas-de-calais.orgraph"

int openride_app_run(int argc, char **argv)
{
    char error[512] = {0};

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    const Uint64 app_started_ns = SDL_GetTicksNS();

#ifdef __ANDROID__
    /*
     * SDL leaves Android Back to the system by default. Trap it so OpenRide can
     * unwind its own panels/search first, then explicitly hand root-level Back
     * back to Android.
     */
    if (!SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1")) {
        SDL_Log("Unable to enable Android Back trapping");
    }
#endif

    OpenRidePlatformPaths platform_paths;
    OpenRidePlatformKind platform_kind = OPENRIDE_PLATFORM_DESKTOP;
    const char *platform_root = ".";
#ifdef __ANDROID__
    platform_kind = OPENRIDE_PLATFORM_ANDROID;
    platform_root = SDL_GetAndroidInternalStoragePath();
    if (!platform_root || platform_root[0] == '\0') {
        platform_root = SDL_GetAndroidExternalStoragePath();
    }
    if (!platform_root || platform_root[0] == '\0') {
        SDL_Log("Unable to resolve Android application storage: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
#endif

    if (!openride_platform_paths_init(&platform_paths,
                                      platform_kind,
                                      platform_root,
                                      error,
                                      sizeof(error))
        || !openride_platform_paths_ensure_directories(&platform_paths,
                                                        error,
                                                        sizeof(error))) {
        SDL_Log("Unable to initialize platform paths: %s", error);
        SDL_Quit();
        return 1;
    }

    OpenRideRegionStatus region_status;
    memset(&region_status, 0, sizeof(region_status));
    char saved_region_id[96] = {0};
    OpenRideAppStorage *startup_storage = openride_app_storage_open(platform_paths.app_storage_path,
                                                                    error,
                                                                    sizeof(error));
    if (startup_storage) {
        openride_app_storage_get_text(startup_storage,
                                      "active_region_id",
                                      "nord-pas-de-calais",
                                      saved_region_id,
                                      sizeof(saved_region_id));
        openride_app_storage_close(startup_storage);
    } else {
        snprintf(saved_region_id, sizeof(saved_region_id), "nord-pas-de-calais");
        error[0] = '\0';
    }
    const OpenRideRegionDefinition *region = openride_app_region_select_initial(&platform_paths,
                                                                   saved_region_id,
                                                                   &region_status,
                                                                   error,
                                                                   sizeof(error));
    if (!region) region = openride_region_default();
    const OpenRideRegionDefinition *active_region =
        openride_region_status_ready(&region_status) ? region : NULL;

    const char *map_path = argc >= 2 ? argv[1] : NULL;
    const char *routing_graph_path = argc >= 3 ? argv[2] : NULL;
    char gpx_default_import_path[512] = {0};
    char gpx_route_export_path[512] = {0};
    char gpx_recording_export_path[512] = {0};
    openride_platform_path_join(gpx_default_import_path,
                                sizeof(gpx_default_import_path),
                                platform_paths.gpx_dir,
                                "import.gpx");
    openride_platform_path_join(gpx_route_export_path,
                                sizeof(gpx_route_export_path),
                                platform_paths.gpx_dir,
                                "openride-route.gpx");
    openride_platform_path_join(gpx_recording_export_path,
                                sizeof(gpx_recording_export_path),
                                platform_paths.gpx_dir,
                                "openride-recording.gpx");
    const char *gpx_import_path = argc >= 4 ? argv[3] : gpx_default_import_path;

    if (!map_path) {
        if (region_status.map_installed) {
            map_path = region_status.map_path;
        } else {
#ifndef __ANDROID__
            map_path = openride_app_support_default_map_path();
#endif
        }
    }
    if (!routing_graph_path && region_status.routing_installed) {
        routing_graph_path = region_status.routing_path;
    }
#ifndef __ANDROID__
    if (!routing_graph_path) routing_graph_path = openride_app_support_default_routing_graph_path();
#endif

    OpenRideMBTiles *map = NULL;
    OpenRideORMap *ormap = NULL;
    bool ormap_map = false;
    OpenRideMBTilesMetadata metadata_storage;
    memset(&metadata_storage, 0, sizeof(metadata_storage));
    metadata_storage.min_zoom = 10;
    metadata_storage.max_zoom = 18;
    metadata_storage.has_center = true;
    metadata_storage.center_lat = 50.370800;
    metadata_storage.center_lon = 3.080200;
    metadata_storage.center_zoom = 11.5;
    snprintf(metadata_storage.name, sizeof(metadata_storage.name), "OpenRide");
    snprintf(metadata_storage.attribution, sizeof(metadata_storage.attribution), "OpenStreetMap contributors");

    if (map_path && openride_app_support_file_exists(map_path)) {
        ormap_map = openride_app_support_has_suffix(map_path, ".ormap");
        if (ormap_map) {
            ormap = openride_ormap_open(map_path, error, sizeof(error));
            if (!ormap) {
                SDL_Log("Unable to open OpenRide map %s: %s",
                        map_path, error[0] ? error : "unknown error");
                SDL_Quit();
                return 1;
            }
            openride_app_region_metadata_from_ormap(&metadata_storage, openride_ormap_metadata(ormap));
        } else {
            map = openride_mbtiles_open(map_path, error, sizeof(error));
            if (!map) {
                SDL_Log("Unable to open offline map %s: %s",
                        map_path, error[0] ? error : "unknown error");
                SDL_Quit();
                return 1;
            }
        }
    } else {
#ifdef __ANDROID__
        SDL_Log("No offline map installed yet; region manager will remain available.");
#else
        SDL_Log("Offline map is missing: %s", map_path ? map_path : "(null)");
        SDL_Quit();
        return 1;
#endif
    }

    OpenRideRoutingGraph routing_graph = {0};
    bool graph_loaded = false;
    if (routing_graph_path) {
        graph_loaded = openride_routing_graph_load(&routing_graph,
                                                   routing_graph_path,
                                                   error,
                                                   sizeof(error));
        if (!graph_loaded) {
            fprintf(stderr,
                    "Routing graph unavailable (%s): %s\n",
                    routing_graph_path,
                    error[0] ? error : "unknown error");
        } else {
            fprintf(stdout,
                    "Routing graph loaded: %u nodes, %u directed edges, %u segments, %u spatial cells\n",
                    routing_graph.node_count,
                    routing_graph.edge_count,
                    routing_graph.segment_index.segment_count,
                    routing_graph.spatial_index.cell_count);
        }
    } else {
        fprintf(stdout,
                "Routing graph not installed. Run ./scripts/prepare_routing_graph.sh\n");
    }

    const OpenRideMBTilesMetadata *metadata = map
        ? openride_mbtiles_metadata(map) : &metadata_storage;
    bool vector_map = map && openride_app_support_is_vector_map(metadata);
    bool scalable_map = vector_map || ormap_map;
    OpenRideMapCamera camera = openride_app_region_camera_from_metadata(metadata);
    OpenRideMapZoomTest map_zoom_test = {0};
    OpenRideMapSelection selection;
    openride_map_selection_init(&selection);
    OpenRideRoute route = {0};
    OpenRideRoutingWorldCache routing_world_cache;
    openride_routing_world_cache_init(&routing_world_cache);
    OpenRideRoutingWorldThreadContext routing_world_context;
    memset(&routing_world_context, 0, sizeof(routing_world_context));
    SDL_Thread *routing_world_thread = NULL;
    bool routing_world_pending_reroute = false;
    bool routing_world_pending_resume_simulator = false;
    OpenRideNavigationEngine navigation;
    OpenRideNavigationInstructionList navigation_instructions = {0};
    OpenRideNavigationSession navigation_session;
    OpenRideVoiceGuidance voice_guidance;
    OpenRideLocationFilter location_filter;
    OpenRideFilteredLocation filtered_location = {0};
    OpenRideDriveModeState drive_mode;
    OpenRideGPSQuality last_drive_gps_quality = OPENRIDE_GPS_UNAVAILABLE;
    OpenRideAppLifecycle app_lifecycle;
    OpenRideLifecycleWatch lifecycle_watch = {0};
    OpenRideGPSSimulator gps_simulator;
#ifdef __ANDROID__
    OpenRideLocationProvider location_provider;
    OpenRideAndroidLocationContext android_location_context;
    OpenRideLocationProvider simulated_location_provider;
    OpenRideSimulatedLocationContext simulated_location_context;
    OpenRideAndroidMissedTurnDev missed_turn_dev;
    openride_android_location_provider_init(&location_provider, &android_location_context);
    bool real_gps_active = false;
    bool real_gps_requested = false;
    bool simulated_gps_active = false;
    bool route_start_gps_pending = false;
    double android_gps_sample_age_s = INFINITY;
    double android_gps_accuracy_m = 0.0;
#endif
    OpenRideNavigationState navigation_state = {0};
    OpenRideGPSSample gps_sample = {0};
    bool gps_sample_valid = false;
    OpenRideGPXDocument gpx_overlay;
    OpenRideGPXDocument gpx_recording;
    openride_gpx_document_init(&gpx_overlay);
    openride_gpx_document_init(&gpx_recording);
    bool gpx_loaded = false;
    bool gpx_recording_active = false;
    double gpx_last_recorded_position_m = -1.0;
    bool follow_gps = true;
    bool auto_reroute = true;
    bool voice_enabled = true;
    bool voice_drive_active = false;
    bool simulator_deviation = false;
    bool gpx_navigation_active = false;
    Uint64 last_frame_ticks = 0;
    openride_navigation_engine_init(&navigation);
    openride_navigation_session_init(&navigation_session);
    openride_voice_guidance_init(&voice_guidance);
#ifdef __ANDROID__
    openride_voice_guidance_set_backend(&voice_guidance,
                                        openride_android_voice_guidance_backend());
#endif
    openride_location_filter_init(&location_filter);
    openride_drive_mode_init(&drive_mode);
    openride_app_lifecycle_init(&app_lifecycle);
    openride_gps_simulator_init(&gps_simulator);
#ifdef __ANDROID__
    openride_android_missed_turn_dev_init(&missed_turn_dev);
    openride_simulated_location_provider_init(
        &simulated_location_provider,
        &simulated_location_context,
        &gps_simulator,
        OPENRIDE_ANDROID_GPS_SIMULATION_TIME_SCALE,
        3.0);
#endif
    OpenRideRoutingProfile routing_profile = OPENRIDE_ROUTING_PROFILE_TOURING;
    OpenRideMapStyle map_style = OPENRIDE_MAP_STYLE_TRAIL;
    double loop_target_distance_m = 100000.0;
    OpenRideLoopDirection loop_direction = OPENRIDE_LOOP_DIRECTION_ANY;
    OpenRideLoopStats loop_stats = {0};
    OpenRideRoutePoint loop_waypoints[OPENRIDE_LOOP_MAX_WAYPOINTS] = {{0}};
    uint32_t loop_waypoint_count = 0U;
    uint32_t loop_seed = 1U;
    bool loop_active = false;
    OpenRideRidePlannerMode planner_mode = OPENRIDE_RIDE_PLANNER_ROUTE;
    OpenRideRidePlannerBusy planner_busy = OPENRIDE_RIDE_PLANNER_IDLE;
    OpenRidePlannerAsyncContext planner_async_context = {0};
    SDL_Thread *planner_async_thread = NULL;
    OpenRideLoopProposalSet loop_proposals = {0};
    OpenRideRouteChoice route_choice;
    openride_route_choice_reset(&route_choice);
    bool route_valid = false;
    bool route_dirty = false;
    OpenRideRoutingSnap start_snap = {0};
    OpenRideRoutingSnap destination_snap = {0};
    start_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    destination_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    char route_status[256];
    snprintf(route_status,
             sizeof(route_status),
             "%s",
             graph_loaded ? "pret" : "graphe non installe");

    OpenRidePlaceIndex *place_index = NULL;
    OpenRidePlaceWorld *place_world = NULL;
    bool place_search_active = false;
    OpenRidePlaceSearchPurpose place_search_purpose =
        OPENRIDE_PLACE_SEARCH_BROWSE;
    OpenRideSelectionMarker route_map_pick_marker =
        OPENRIDE_MARKER_NONE;
    OpenRideRouteDownloadPlan route_download_plan;
    memset(&route_download_plan, 0, sizeof(route_download_plan));
    char place_search_query[128] = {0};
    OpenRidePlaceSearchResult place_search_results[OPENRIDE_SEARCH_MAX_RESULTS];
    uint32_t place_search_result_count = 0U;
    uint32_t place_search_selected = 0U;

    OpenRideAppStorage *app_storage = NULL;
    OpenRideAppPanel app_panel = OPENRIDE_APP_PANEL_NONE;
#ifdef __ANDROID__
    if (!map && !ormap) app_panel = OPENRIDE_APP_PANEL_REGIONS;
#endif
    OpenRideStoredPlace favorite_places[OPENRIDE_APP_LIST_MAX];
    OpenRideStoredPlace history_places[OPENRIDE_APP_LIST_MAX];
    uint32_t favorite_count = 0U;
    uint32_t history_count = 0U;
    uint32_t app_panel_selected = 0U;

    bool region_busy = false;
    bool region_activation_requested = false;
    double region_progress = -1.0;
    char region_work_status[192] = {0};
#ifdef __ANDROID__
    OpenRideAndroidDownloadStatus region_download_status = {0};
    bool region_download_started = false;
    bool region_download_is_poly = false;
    OpenRideRegionPrepareThreadContext region_prepare_context;
    memset(&region_prepare_context, 0, sizeof(region_prepare_context));
    SDL_Thread *region_prepare_thread = NULL;
#endif

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    OpenRideMapRenderer raster_renderer;
    OpenRideVectorMapRenderer vector_renderer;
    OpenRideORMapRenderer ormap_renderer;
    bool renderer_initialized = false;
    bool renderer_vsync_enabled = false;
    bool first_frame_logged = false;
    Uint64 previous_frame_started_ns = 0U;
    Uint64 interactive_until_ns =
        SDL_GetTicksNS() + OPENRIDE_STARTUP_INTERACTIVE_NS;
#ifdef __ANDROID__
    Uint64 audit_pacing_window_started_ns = SDL_GetTicksNS();
    uint32_t audit_pacing_interactive_frames = 0U;
    uint32_t audit_pacing_active_frames = 0U;
    uint32_t audit_pacing_idle_frames = 0U;
    double audit_last_presented_zoom = NAN;

    /*
     * Temporary V3.4.2 diagnostic: profile the first ten normal Android
     * frames in detail. It is deliberately bounded so instrumentation cannot
     * become permanent per-frame overhead.
     */
    uint32_t audit_frame_profile_remaining = 10U;
    uint32_t audit_frame_profile_index = 0U;
#endif
    bool running = true;
    bool render_suspended = false;
    bool render_dirty = true;
    uint32_t render_followup_budget = OPENRIDE_RENDER_FOLLOWUP_BUDGET;
#ifdef __ANDROID__
    OpenRideDirtyRenderAudit dirty_render_audit = {
        .window_started_ns = SDL_GetTicksNS()
    };
#endif
    bool lifecycle_watch_installed = false;
    bool dragging_map = false;
    bool map_drag_moved = false;
    double mouse_down_x = 0.0;
    double mouse_down_y = 0.0;
    OpenRideSelectionMarker dragging_marker = OPENRIDE_MARKER_NONE;
    OpenRideTouchInput touch_input;
    OpenRideToolbarAction pending_toolbar_action = OPENRIDE_TOOLBAR_NONE;
    OpenRideDriveAction pending_drive_action = OPENRIDE_DRIVE_ACTION_NONE;
    openride_touch_input_init(&touch_input, 7.0);

    if (!SDL_CreateWindowAndRenderer(
            "OpenRide - Offline map",
            1200,
            800,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
            &window,
            &renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        openride_gpx_document_destroy(&gpx_recording);
        openride_gpx_document_destroy(&gpx_overlay);
        openride_gps_simulator_destroy(&gps_simulator);
        openride_navigation_engine_destroy(&navigation);
        openride_routing_graph_destroy(&routing_graph);
        openride_mbtiles_close(map);
        SDL_Quit();
        return 1;
    }

    renderer_vsync_enabled = SDL_SetRenderVSync(renderer, 1);
    if (!renderer_vsync_enabled) {
        SDL_Log("Renderer VSync unavailable: %s; 60 fps frame cap remains active",
                SDL_GetError());
    } else {
        int actual_vsync = 0;
        if (SDL_GetRenderVSync(renderer, &actual_vsync)) {
            SDL_Log("Renderer VSync enabled (interval=%d), OpenRide capped at <=60 fps",
                    actual_vsync);
        } else {
            SDL_Log("Renderer VSync enabled, OpenRide capped at <=60 fps");
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    last_frame_ticks = SDL_GetTicks();

    if (ormap_map) {
        renderer_initialized = openride_ormap_renderer_init(&ormap_renderer, renderer, ormap);
        if (renderer_initialized) openride_ormap_renderer_set_style(&ormap_renderer, map_style);
    } else if (vector_map) {
        renderer_initialized = openride_vector_map_renderer_init(&vector_renderer, renderer, map);
        if (renderer_initialized) {
            openride_vector_map_renderer_set_style(&vector_renderer, map_style);
        }
    } else if (map) {
        renderer_initialized = openride_map_renderer_init(&raster_renderer, renderer, map);
    } else {
        /* Android can start without data so the user can download a region. */
        renderer_initialized = true;
    }

    if (!renderer_initialized) {
        SDL_Log("Unable to initialize offline map renderer");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        openride_gpx_document_destroy(&gpx_recording);
        openride_gpx_document_destroy(&gpx_overlay);
        openride_gps_simulator_destroy(&gps_simulator);
        openride_navigation_engine_destroy(&navigation);
        openride_routing_graph_destroy(&routing_graph);
        openride_mbtiles_close(map);
        openride_ormap_close(ormap);
        SDL_Quit();
        return 1;
    }

    lifecycle_watch_installed = SDL_AddEventWatch(openride_app_support_lifecycle_event_watch,
                                                   &lifecycle_watch);
    if (!lifecycle_watch_installed) {
        SDL_Log("Unable to install lifecycle event watch: %s", SDL_GetError());
    }

    if (openride_app_support_file_exists(region_status.search_path)) {
        place_index = openride_place_index_open(region_status.search_path,
                                                error,
                                                sizeof(error));
        if (place_index) {
            fprintf(stdout, "Offline place index loaded: %s\n", region_status.search_path);
        } else {
            fprintf(stderr,
                    "Place index unavailable (%s): %s\n",
                    region_status.search_path,
                    error[0] ? error : "unknown error");
        }
    } else {
        fprintf(stdout,
                "Offline search not installed. Run ./scripts/prepare_place_index.sh\n");
    }

    place_world = openride_place_world_create(&platform_paths,
                                                        error,
                                                        sizeof(error));
    if (place_world) {
        SDL_Log("PlaceWorld: %zu regional search index(es)",
                openride_place_world_region_count(place_world));
    } else {
        SDL_Log("PlaceWorld unavailable: %s",
                error[0] ? error : "unknown error");
        error[0] = '\0';
    }

    app_storage = openride_app_storage_open(platform_paths.app_storage_path,
                                               error,
                                               sizeof(error));
    if (app_storage) {
        if (active_region) {
            openride_app_storage_set_text(app_storage,
                                          "active_region_id",
                                          active_region->id,
                                          error,
                                          sizeof(error));
        }
        const int saved_style = openride_app_storage_get_int(app_storage,
                                                              "map_style",
                                                              (int)map_style);
        const int saved_profile = openride_app_storage_get_int(app_storage,
                                                                "routing_profile",
                                                                (int)routing_profile);
        const int saved_follow = openride_app_storage_get_int(app_storage,
                                                               "follow_gps",
                                                               follow_gps ? 1 : 0);
        const int saved_auto_reroute = openride_app_storage_get_int(app_storage,
                                                                     "auto_reroute",
                                                                     auto_reroute ? 1 : 0);
        const int saved_voice = openride_app_storage_get_int(app_storage,
                                                              "voice_enabled",
                                                              voice_enabled ? 1 : 0);
        if (saved_style >= (int)OPENRIDE_MAP_STYLE_ROAD
            && saved_style <= (int)OPENRIDE_MAP_STYLE_TOPO) {
            map_style = (OpenRideMapStyle)saved_style;
            if (ormap_map) openride_ormap_renderer_set_style(&ormap_renderer, map_style);
            else if (vector_map) openride_vector_map_renderer_set_style(&vector_renderer, map_style);
        }
        if (saved_profile >= (int)OPENRIDE_ROUTING_PROFILE_FASTEST
            && saved_profile <= (int)OPENRIDE_ROUTING_PROFILE_TRAIL) {
            routing_profile = (OpenRideRoutingProfile)saved_profile;
        }
        follow_gps = saved_follow != 0;
        auto_reroute = saved_auto_reroute != 0;
        voice_enabled = saved_voice != 0;
        openride_navigation_session_set_auto_reroute(&navigation_session, auto_reroute);
        openride_voice_guidance_set_enabled(&voice_guidance, voice_enabled);
        openride_app_search_refresh_stored_places(app_storage, true, favorite_places, &favorite_count);
        openride_app_search_refresh_stored_places(app_storage, false, history_places, &history_count);    } else {
        fprintf(stderr, "App storage unavailable: %s\n", error[0] ? error : "unknown error");
    }
#ifdef __ANDROID__
    if (voice_enabled) {
        if (!openride_android_voice_guidance_init()) {
            SDL_Log("Android TTS initialization request failed");
        }
    }
#endif
    OpenRideMapWorld *map_world = openride_map_world_create(renderer,
                                                           &platform_paths,
                                                           error,
                                                           sizeof(error));
    if (map_world) {
        const bool france_overview_available =
            openride_map_world_base_available(map_world);
        if (france_overview_available) {
            /*
             * The bundled France overview is zoomable even when the device has
             * no downloaded regional map yet. Event handling can therefore use
             * the MapWorld z6 floor instead of the fallback metadata z10 floor.
             */
            scalable_map = true;
        }
        SDL_Log(
            "MapWorld: France overview=%s, %zu installed region(s)",
            france_overview_available ? "ready" : "unavailable",
            openride_map_world_region_count(map_world));
    } else {
        SDL_Log("MapWorld overview unavailable: %s",
                error[0] ? error : "unknown error");
        error[0] = '\0';
    }
    if (openride_app_support_file_exists(gpx_import_path)) {
        gpx_loaded = openride_app_route_load_gpx_overlay(gpx_import_path,
                                      &gpx_overlay,
                                      route_status,
                                      sizeof(route_status));
        if (gpx_loaded) {
            int gpx_width = 0;
            int gpx_height = 0;
            SDL_GetCurrentRenderOutputSize(renderer, &gpx_width, &gpx_height);
            openride_app_route_fit_camera_to_gpx(&camera,
                              &gpx_overlay,
                              gpx_width,
                              gpx_height,
                              (double)metadata->min_zoom,
                              scalable_map ? 18.0 : (double)metadata->max_zoom);
        }
    }

    OpenRideAppEventContext event_context = {
        .window = window,
        .renderer = renderer,
        .running = &running,
        .platform_paths = &platform_paths,
        .camera = &camera,
        .map_zoom_test = &map_zoom_test,
        .selection = &selection,
        .route = &route,
        .routing_graph = &routing_graph,
        .graph_loaded = &graph_loaded,
        .routing_profile = &routing_profile,
        .map_style = &map_style,
        .scalable_map = &scalable_map,
        .ormap_map = &ormap_map,
        .vector_map = &vector_map,
        .ormap_renderer = &ormap_renderer,
        .vector_renderer = &vector_renderer,
        .metadata = &metadata,
        .map_world = map_world,
        .routing_world_cache = &routing_world_cache,
        .routing_world_context = &routing_world_context,
        .routing_world_thread = &routing_world_thread,
        .routing_world_pending_reroute = &routing_world_pending_reroute,
        .routing_world_pending_resume_simulator = &routing_world_pending_resume_simulator,
        .navigation = &navigation,
        .navigation_instructions = &navigation_instructions,
        .navigation_session = &navigation_session,
        .location_filter = &location_filter,
        .filtered_location = &filtered_location,
        .voice_guidance = &voice_guidance,
        .gps_simulator = &gps_simulator,
        .navigation_state = &navigation_state,
        .gps_sample = &gps_sample,
        .gps_sample_valid = &gps_sample_valid,
        .drive_mode = &drive_mode,
        .follow_gps = &follow_gps,
        .auto_reroute = &auto_reroute,
        .voice_enabled = &voice_enabled,
        .simulator_deviation = &simulator_deviation,
        .gpx_navigation_active = &gpx_navigation_active,
        .gpx_overlay = &gpx_overlay,
        .gpx_recording = &gpx_recording,
        .gpx_loaded = &gpx_loaded,
        .gpx_recording_active = &gpx_recording_active,
        .gpx_last_recorded_position_m = &gpx_last_recorded_position_m,
        .gpx_import_path = gpx_import_path,
        .gpx_route_export_path = gpx_route_export_path,
        .gpx_recording_export_path = gpx_recording_export_path,
        .planner_mode = &planner_mode,
        .planner_busy = &planner_busy,
        .planner_async_context = &planner_async_context,
        .planner_async_thread = &planner_async_thread,
        .loop_proposals = &loop_proposals,
        .route_choice = &route_choice,
        .loop_target_distance_m = &loop_target_distance_m,
        .loop_direction = &loop_direction,
        .loop_stats = &loop_stats,
        .loop_waypoints = loop_waypoints,
        .loop_waypoint_count = &loop_waypoint_count,
        .loop_seed = &loop_seed,
        .loop_active = &loop_active,
        .start_snap = &start_snap,
        .destination_snap = &destination_snap,
        .route_valid = &route_valid,
        .route_dirty = &route_dirty,
        .place_world = place_world,
        .app_storage = app_storage,
        .place_search_active = &place_search_active,
        .place_search_purpose = &place_search_purpose,
        .place_search_query = place_search_query,
        .place_search_query_size = sizeof(place_search_query),
        .place_search_results = place_search_results,
        .place_search_result_count = &place_search_result_count,
        .place_search_selected = &place_search_selected,
        .favorite_places = favorite_places,
        .history_places = history_places,
        .favorite_count = &favorite_count,
        .history_count = &history_count,
        .app_panel = &app_panel,
        .app_panel_selected = &app_panel_selected,
        .route_download_plan = &route_download_plan,
        .route_map_pick_marker = &route_map_pick_marker,
        .region = &region,
        .active_region = &active_region,
        .region_status = &region_status,
        .region_busy = &region_busy,
        .region_activation_requested = &region_activation_requested,
        .region_progress = &region_progress,
        .region_work_status = region_work_status,
        .region_work_status_size = sizeof(region_work_status),
        .dragging_map = &dragging_map,
        .map_drag_moved = &map_drag_moved,
        .mouse_down_x = &mouse_down_x,
        .mouse_down_y = &mouse_down_y,
        .dragging_marker = &dragging_marker,
        .touch_input = &touch_input,
        .pending_toolbar_action = &pending_toolbar_action,
        .pending_drive_action = &pending_drive_action,
        .route_status = route_status,
        .route_status_size = sizeof(route_status),
        .error = error,
        .error_size = sizeof(error),
#ifdef __ANDROID__
        .location_provider = &location_provider,
        .simulated_location_provider = &simulated_location_provider,
        .simulated_location_context = &simulated_location_context,
        .real_gps_active = &real_gps_active,
        .real_gps_requested = &real_gps_requested,
        .simulated_gps_active = &simulated_gps_active,
        .route_start_gps_pending = &route_start_gps_pending,
        .android_gps_sample_age_s = &android_gps_sample_age_s,
        .android_gps_accuracy_m = &android_gps_accuracy_m,
        .missed_turn_dev = &missed_turn_dev,
        .region_prepare_context = &region_prepare_context,
        .region_prepare_thread = &region_prepare_thread,
        .region_download_started = &region_download_started,
        .region_download_is_poly = &region_download_is_poly,
#endif
    };

    OpenRideAppAsyncContext async_context = {
        .events = &event_context,
        .map = &map,
        .ormap = &ormap,
        .metadata_storage = &metadata_storage,
        .raster_renderer = &raster_renderer,
        .place_index = &place_index,
#ifdef __ANDROID__
        .region_download_status = &region_download_status,
#endif
    };

    while (running) {
        /*
         * Adaptive foreground pacing:
         *   - 60 fps during gestures/zoom-test/recent visual changes;
         *   - 30 fps while GPS/navigation/async work is continuously active;
         *   - 15 fps on a static map or static panel.
         *
         * A static OpenRide screen was previously redrawn at 60 fps forever,
         * which kept the Pixel audit around one full CPU core even with VSync.
         */
        const Uint64 pacing_now_ns = SDL_GetTicksNS();
        bool runtime_continuous_activity =
            map_zoom_test.active
            || drive_mode.active
            || planner_busy != OPENRIDE_RIDE_PLANNER_IDLE
            || planner_async_thread != NULL
            || routing_world_thread != NULL
            || region_busy;
#ifdef __ANDROID__
        /*
         * A running GPS provider is not, by itself, a reason to redraw at
         * 30 fps. A location sample that actually changes the camera is caught
         * below by frame_camera_changed and temporarily boosts rendering.
         */
        runtime_continuous_activity =
            runtime_continuous_activity
            || region_prepare_thread != NULL
            || region_download_started;
#endif

        const Uint64 target_frame_ns =
            map_zoom_test.active || pacing_now_ns < interactive_until_ns
                ? OPENRIDE_INTERACTIVE_FRAME_NS
                : runtime_continuous_activity
                    ? OPENRIDE_ACTIVE_FRAME_NS
                    : OPENRIDE_IDLE_FRAME_NS;

#ifdef __ANDROID__
        if (target_frame_ns == OPENRIDE_INTERACTIVE_FRAME_NS) {
            ++audit_pacing_interactive_frames;
        } else if (target_frame_ns == OPENRIDE_ACTIVE_FRAME_NS) {
            ++audit_pacing_active_frames;
        } else {
            ++audit_pacing_idle_frames;
        }
#endif

        if (previous_frame_started_ns != 0U) {
            const Uint64 next_frame_ns =
                previous_frame_started_ns + target_frame_ns;
            if (pacing_now_ns < next_frame_ns) {
                /*
                 * This is an energy-saving frame wait, not a sub-millisecond
                 * timing benchmark. SDL_DelayNS can sleep; DelayPrecise may
                 * busy-wait near the deadline and defeat the CPU saving.
                 */
                SDL_DelayNS(next_frame_ns - pacing_now_ns);
            }
        }
        previous_frame_started_ns = SDL_GetTicksNS();

#ifdef __ANDROID__
        const bool audit_frame_profile_active =
            audit_frame_profile_remaining > 0U
            && !map_zoom_test.active;
        const Uint64 audit_frame_profile_started_ns =
            audit_frame_profile_active ? SDL_GetTicksNS() : 0U;
#endif

        const OpenRideMapCamera frame_camera_before_update = camera;
        const OpenRideAppPanel frame_panel_before_update = app_panel;
        const bool frame_search_before_update = place_search_active;
        const bool frame_drive_before_update = drive_mode.active;

        uint64_t map_zoom_loop_started_ns =
            map_zoom_test.active ? SDL_GetTicksNS() : 0U;


        const bool event_activity =
            openride_app_events_poll(&event_context,
                                     &map_zoom_loop_started_ns);
        if (event_activity) {
            render_dirty = true;
            render_followup_budget = OPENRIDE_RENDER_FOLLOWUP_BUDGET;
        }
        bool gps_visual_changed = false;

        const int lifecycle_signal = SDL_SetAtomicInt(&lifecycle_watch.pending_signal,
                                                       OPENRIDE_LIFECYCLE_SIGNAL_NONE);
        if (lifecycle_signal == OPENRIDE_LIFECYCLE_SIGNAL_BACKGROUND) {
#ifdef __ANDROID__
            openride_app_lifecycle_enter_background(&app_lifecycle,
                                                     real_gps_requested || real_gps_active,
                                                     drive_mode.active);
            if (real_gps_active) {
                openride_location_provider_stop(&location_provider);
                real_gps_active = false;
            }
#else
            openride_app_lifecycle_enter_background(&app_lifecycle,
                                                     gps_simulator.active,
                                                     drive_mode.active);
#endif
            render_suspended = true;
        } else if (lifecycle_signal == OPENRIDE_LIFECYCLE_SIGNAL_FOREGROUND) {
            openride_app_lifecycle_enter_foreground(&app_lifecycle);
            render_suspended = false;
            render_dirty = true;
            render_followup_budget = OPENRIDE_RENDER_FOLLOWUP_BUDGET;
            last_frame_ticks = SDL_GetTicks();
            openride_location_filter_reset(&location_filter);
            memset(&filtered_location, 0, sizeof(filtered_location));

            bool restart_gps = false;
            bool restore_drive = false;
            if (openride_app_lifecycle_take_resume(&app_lifecycle,
                                                    &restart_gps,
                                                    &restore_drive)) {
#ifdef __ANDROID__
                if (restart_gps) {
                    real_gps_requested = true;
                    if (openride_location_provider_start(&location_provider)) {
                        real_gps_active = true;
                        android_gps_sample_age_s = INFINITY;
                        snprintf(route_status, sizeof(route_status), "GPS repris apres retour application");
                    } else {
                        snprintf(route_status, sizeof(route_status), "GPS a relancer apres retour application");
                    }
                }
                if (restore_drive && route_valid && real_gps_active) {
                    openride_drive_mode_set_active(&drive_mode, true);
                    openride_drive_mode_set_auto_zoom(&drive_mode, true);
                    follow_gps = true;
                }
#else
                (void)restart_gps;
                (void)restore_drive;
#endif
            }
        } else if (lifecycle_signal == OPENRIDE_LIFECYCLE_SIGNAL_LOW_MEMORY) {
            snprintf(route_status, sizeof(route_status), "memoire faible: navigation conservee");
            render_dirty = true;
            render_followup_budget = OPENRIDE_RENDER_FOLLOWUP_BUDGET;
        } else if (lifecycle_signal == OPENRIDE_LIFECYCLE_SIGNAL_TERMINATING) {
            running = false;
        }

        if (!running) break;
        if (render_suspended || app_lifecycle.in_background) {
            SDL_Delay(50);
            continue;
        }



        openride_app_events_dispatch_pending(&event_context);

        openride_app_async_update(&async_context);

        const Uint64 current_ticks = SDL_GetTicks();
        double delta_seconds = (double)(current_ticks - last_frame_ticks) / 1000.0;
        last_frame_ticks = current_ticks;
        if (delta_seconds < 0.0) delta_seconds = 0.0;
        if (delta_seconds > 0.25) delta_seconds = 0.25;

        openride_map_zoom_test_update(&map_zoom_test, &camera, delta_seconds);

#ifdef __ANDROID__
        if (!simulated_gps_active
            && (missed_turn_dev.armed || missed_turn_dev.active)) {
            openride_android_missed_turn_dev_reset(
                &missed_turn_dev,
                &simulated_location_context,
                &gps_simulator);
        }

        if (simulated_gps_active
            && missed_turn_dev.armed
            && simulated_location_context.simulator == &gps_simulator
            && gps_simulator.position_m
                >= missed_turn_dev.plan.trigger_position_m) {
            openride_gps_simulator_restart(&missed_turn_dev.simulator);
            simulated_location_context.simulator =
                &missed_turn_dev.simulator;
            missed_turn_dev.armed = false;
            missed_turn_dev.active = true;
            openride_location_filter_reset(&location_filter);
            memset(&filtered_location, 0, sizeof(filtered_location));
            memset(&navigation_state, 0, sizeof(navigation_state));
            snprintf(route_status,
                     sizeof(route_status),
                     "VIRAGE RATE [DEV]: branche reelle suivie");
        }

        const bool android_location_active =
            simulated_gps_active || real_gps_active;
        OpenRideLocationProvider *android_location_provider =
            simulated_gps_active
                ? &simulated_location_provider
                : &location_provider;
        if (android_location_active) {
            if (isfinite(android_gps_sample_age_s)) {
                android_gps_sample_age_s += delta_seconds;
            }
            OpenRideLocationSample real_sample;
            if (openride_location_provider_poll(
                    android_location_provider,
                    delta_seconds,
                    &real_sample)) {
                gps_visual_changed = true;
                android_gps_sample_age_s = 0.0;
                android_gps_accuracy_m = real_sample.accuracy_m;
                gps_sample_valid = real_sample.valid;
                gps_sample.valid = real_sample.valid;
                gps_sample.finished = false;
                gps_sample.lat = real_sample.lat;
                gps_sample.lon = real_sample.lon;
                gps_sample.speed_mps = real_sample.speed_mps;
                gps_sample.heading_deg = real_sample.heading_deg;

                if (route_start_gps_pending
                    && gps_sample_valid
                    && !simulated_gps_active) {
                    openride_map_selection_set(&selection,
                                               OPENRIDE_MARKER_START,
                                               gps_sample.lat,
                                               gps_sample.lon);
                    start_snap.segment_id =
                        OPENRIDE_ROUTING_SEGMENT_NONE;
                    openride_route_destroy(&route);
                    route_valid = false;
                    route_dirty = false;
                    route_start_gps_pending = false;
                    snprintf(route_status,
                             sizeof(route_status),
                             "depart GPS acquis (precision %.0f m)",
                             android_gps_accuracy_m);
                }

                if (openride_location_filter_update(&location_filter,
                                                    gps_sample.lat,
                                                    gps_sample.lon,
                                                    gps_sample.speed_mps,
                                                    gps_sample.heading_deg,
                                                    delta_seconds,
                                                    &filtered_location)) {
                    if (route_valid && navigation.route != NULL) {
                        openride_navigation_engine_update(&navigation,
                                                          filtered_location.lat,
                                                          filtered_location.lon,
                                                          filtered_location.speed_mps,
                                                          filtered_location.heading_deg,
                                                          &navigation_state);
                        gps_sample.route_position_m = navigation_state.valid
                            ? navigation_state.traveled_m : 0.0;
                        openride_navigation_session_update(&navigation_session,
                                                           &navigation_state,
                                                           filtered_location.lat,
                                                           filtered_location.lon,
                                                           filtered_location.speed_mps,
                                                           delta_seconds);
                    }
                }

                if (gpx_recording_active) {
                    openride_app_route_record_gps_sample(&gpx_recording,
                                      &gps_sample,
                                      &gpx_last_recorded_position_m);
                }
                if (follow_gps && !map_zoom_test.active && !drive_mode.active
                    && !route_choice.active) {
                    camera.center_lat = filtered_location.valid
                        ? filtered_location.lat : gps_sample.lat;
                    camera.center_lon = filtered_location.valid
                        ? filtered_location.lon : gps_sample.lon;
                }
            }
        }
#endif

#ifndef __ANDROID__
        if (route_valid && gps_simulator.route
            && (gps_simulator.active || gps_sample_valid)) {
            const double navigation_delta_s = gps_simulator.active
                ? delta_seconds * OPENRIDE_GPS_SIMULATION_TIME_SCALE
                : delta_seconds;
            if (openride_gps_simulator_update(&gps_simulator,
                                              navigation_delta_s,
                                              &gps_sample)) {
                gps_visual_changed = true;
                gps_sample_valid = true;
                if (openride_location_filter_update(&location_filter,
                                                    gps_sample.lat,
                                                    gps_sample.lon,
                                                    gps_sample.speed_mps,
                                                    gps_sample.heading_deg,
                                                    navigation_delta_s,
                                                    &filtered_location)) {
                    openride_navigation_engine_update(&navigation,
                                                      filtered_location.lat,
                                                      filtered_location.lon,
                                                      filtered_location.speed_mps,
                                                      filtered_location.heading_deg,
                                                      &navigation_state);
                    openride_navigation_session_update(&navigation_session,
                                                       &navigation_state,
                                                       filtered_location.lat,
                                                       filtered_location.lon,
                                                       filtered_location.speed_mps,
                                                       navigation_delta_s);
                }
                if (gpx_recording_active) {
                    openride_app_route_record_gps_sample(&gpx_recording,
                                      &gps_sample,
                                      &gpx_last_recorded_position_m);
                }
                if (follow_gps && !map_zoom_test.active
                    && gps_simulator.active && !drive_mode.active
                    && !route_choice.active) {
                    camera.center_lat = filtered_location.valid ? filtered_location.lat : gps_sample.lat;
                    camera.center_lon = filtered_location.valid ? filtered_location.lon : gps_sample.lon;
                }
                if (navigation_state.status == OPENRIDE_NAVIGATION_ARRIVED
                    && gps_simulator.finished) {
                    snprintf(route_status, sizeof(route_status), "destination atteinte");
                }
            }
        }
#endif

        if (route_valid && gps_sample_valid && auto_reroute
            && !loop_active && !gpx_navigation_active
            && selection.has_destination
            && openride_navigation_session_take_reroute_request(&navigation_session)) {
#ifdef __ANDROID__
            const bool resume_simulator = false;
#else
            const bool resume_simulator = gps_simulator.active;
#endif
            const double reroute_lat = filtered_location.valid
                ? filtered_location.lat : gps_sample.lat;
            const double reroute_lon = filtered_location.valid
                ? filtered_location.lon : gps_sample.lon;
            route_valid = openride_app_route_reroute_from_position(
                &routing_graph,
                graph_loaded,
                &selection,
                routing_profile,
                reroute_lat,
                reroute_lon,
                &route,
                &start_snap,
                &destination_snap,
                &navigation,
                &gps_simulator,
                &navigation_instructions,
                &navigation_session,
                &location_filter,
                resume_simulator,
                route_status,
                sizeof(route_status));
            simulator_deviation = false;
#ifdef __ANDROID__
            if (missed_turn_dev.armed || missed_turn_dev.active) {
                openride_android_missed_turn_dev_reset(
                    &missed_turn_dev,
                    &simulated_location_context,
                    &gps_simulator);
            }
#endif
            memset(&navigation_state, 0, sizeof(navigation_state));
            memset(&filtered_location, 0, sizeof(filtered_location));
            if (route_valid) {
                openride_voice_guidance_reset(&voice_guidance);
                snprintf(route_status, sizeof(route_status), "recalcul automatique termine");
            } else if (openride_map_selection_complete(&selection)) {
                routing_world_pending_reroute = true;
                routing_world_pending_resume_simulator = resume_simulator;
                route_dirty = true;
            }
        }

        if (drive_mode.active) {
            double maneuver_distance_m = INFINITY;
            if (navigation_state.valid) {
                (void)openride_navigation_instructions_next(&navigation_instructions,
                                                            navigation_state.traveled_m,
                                                            &maneuver_distance_m);
            }
            const double drive_lat = filtered_location.valid
                ? filtered_location.lat : gps_sample.lat;
            const double drive_lon = filtered_location.valid
                ? filtered_location.lon : gps_sample.lon;
            const double drive_speed = filtered_location.valid
                ? filtered_location.speed_mps : gps_sample.speed_mps;
            const double drive_heading = filtered_location.valid
                ? filtered_location.heading_deg : gps_sample.heading_deg;
#ifdef __ANDROID__
            const bool drive_gps_active =
                real_gps_active || simulated_gps_active;
            const double drive_sample_age_s = android_gps_sample_age_s;
            const double drive_accuracy_m = android_gps_accuracy_m;
#else
            const bool drive_gps_active = gps_sample_valid;
            const double drive_sample_age_s = gps_sample_valid ? 0.0 : INFINITY;
            const double drive_accuracy_m = gps_sample_valid ? 5.0 : 0.0;
#endif
            openride_drive_mode_update(&drive_mode,
                                       drive_gps_active,
                                       gps_sample_valid,
                                       drive_sample_age_s,
                                       drive_accuracy_m,
                                       drive_lat,
                                       drive_lon,
                                       drive_speed,
                                       drive_heading,
                                       maneuver_distance_m,
                                       delta_seconds);
            if (drive_mode.gps_quality == OPENRIDE_GPS_LOST
                && last_drive_gps_quality != OPENRIDE_GPS_LOST) {
                snprintf(route_status, sizeof(route_status), "signal GPS perdu");
            } else if ((last_drive_gps_quality == OPENRIDE_GPS_LOST
                        || last_drive_gps_quality == OPENRIDE_GPS_UNAVAILABLE)
                       && drive_mode.gps_quality != OPENRIDE_GPS_LOST
                       && drive_mode.gps_quality != OPENRIDE_GPS_UNAVAILABLE) {
                snprintf(route_status, sizeof(route_status), "signal GPS retrouve");
            }
            last_drive_gps_quality = drive_mode.gps_quality;
            if (follow_gps && !map_zoom_test.active
                && drive_mode.initialized
                && drive_mode.gps_quality != OPENRIDE_GPS_LOST) {
                camera.center_lat = drive_mode.camera_lat;
                camera.center_lon = drive_mode.camera_lon;
                if (drive_mode.auto_zoom) {
                    const double max_drive_zoom = scalable_map
                        ? 18.0 : (double)metadata->max_zoom;
                    camera.zoom = openride_app_support_clampd(drive_mode.camera_zoom,
                                         (double)metadata->min_zoom,
                                         max_drive_zoom);
                }
                camera.bearing_deg = drive_mode.heading_up
                    ? drive_mode.camera_bearing_deg : 0.0;
            }
        }

        if (drive_mode.active != voice_drive_active) {
            openride_voice_guidance_reset(&voice_guidance);
            voice_drive_active = drive_mode.active;
        }
        if (drive_mode.active) {
            (void)openride_voice_guidance_update(&voice_guidance,
                                                 &navigation_instructions,
                                                 &navigation_state);
        }

        const bool frame_camera_changed =
            fabs(camera.center_lat - frame_camera_before_update.center_lat) > 1e-10
            || fabs(camera.center_lon - frame_camera_before_update.center_lon) > 1e-10
            || fabs(camera.zoom - frame_camera_before_update.zoom) > 1e-7
            || fabs(camera.bearing_deg - frame_camera_before_update.bearing_deg) > 1e-5;
        const bool frame_ui_changed =
            app_panel != frame_panel_before_update
            || place_search_active != frame_search_before_update
            || drive_mode.active != frame_drive_before_update;

        if (event_activity
            || frame_camera_changed
            || frame_ui_changed
            || gps_visual_changed) {
            render_dirty = true;
            render_followup_budget = OPENRIDE_RENDER_FOLLOWUP_BUDGET;
            interactive_until_ns =
                SDL_GetTicksNS() + OPENRIDE_INTERACTIVE_HOLD_NS;
        }

        if (map_zoom_test.active
            || drive_mode.active
            || runtime_continuous_activity) {
            render_dirty = true;
            render_followup_budget = OPENRIDE_RENDER_FOLLOWUP_BUDGET;
        }

        if (!render_dirty) {
#ifdef __ANDROID__
            openride_dirty_render_audit_tick(&dirty_render_audit, false);
#endif
            continue;
        }

        int width = 0;
        int height = 0;
        if (!SDL_GetCurrentRenderOutputSize(renderer, &width, &height)) {
            SDL_Log("SDL_GetCurrentRenderOutputSize failed: %s", SDL_GetError());
            break;
        }

        const bool world_base_available = map_world
            && openride_map_world_base_available(map_world);
        const bool world_available = map_world
            && openride_map_world_region_count(map_world) > 0U;
        OpenRideMapZoomFrameProfile map_zoom_profile;
        OpenRideMapWorldDebugStats map_zoom_world_debug;
        memset(&map_zoom_profile, 0, sizeof(map_zoom_profile));
        memset(&map_zoom_world_debug, 0, sizeof(map_zoom_world_debug));
        map_zoom_world_debug.road.prewarm_zoom = -1;

#ifdef __ANDROID__
        OpenRideMapWorldDebugStats audit_frame_world_debug;
        memset(&audit_frame_world_debug, 0, sizeof(audit_frame_world_debug));
        audit_frame_world_debug.road.prewarm_zoom = -1;
#endif

        bool map_debug_frame = map_zoom_test.active;
#ifdef __ANDROID__
        map_debug_frame = map_debug_frame || audit_frame_profile_active;
#endif
        if (map_debug_frame && map_world) {
            openride_map_world_debug_begin_frame(map_world);
        }

        const uint64_t map_zoom_map_started_ns =
            map_zoom_test.active ? SDL_GetTicksNS() : 0U;
#ifdef __ANDROID__
        const Uint64 audit_frame_profile_map_started_ns =
            audit_frame_profile_active ? SDL_GetTicksNS() : 0U;
#endif
        if (ormap_map) {
            /*
             * Installed regions are ORMap-owned at every application zoom.
             * MapWorld remains the multi-region coordinator, but its old
             * generalized overview renderer is no longer part of the installed
             * region render path. This removes the z10 engine handoff entirely.
             */
            if (world_available) {
                openride_map_world_draw_detail(map_world,
                                               &camera,
                                               map_style,
                                               width,
                                               height);
            } else {
                openride_ormap_renderer_draw(&ormap_renderer,
                                             &camera,
                                             width,
                                             height);
            }
        } else {
            if (vector_map) {
                openride_vector_map_renderer_draw(&vector_renderer, &camera, width, height);
            } else {
                SDL_SetRenderDrawColor(renderer, 28, 32, 38, SDL_ALPHA_OPAQUE);
                SDL_RenderClear(renderer);
                if (map) openride_map_renderer_draw(&raster_renderer, &camera, width, height);
            }

            if (!map && world_base_available) {
                /*
                 * No regional map installed: keep a real France-wide visual
                 * context instead of the empty background. Regional ORMaps,
                 * once installed, are composited by MapWorld's detail path.
                 */
                openride_map_world_draw_base_overview(
                    map_world,
                    &camera,
                    map_style,
                    width,
                    height);
            } else if (world_available
                       && camera.zoom
                           <= OPENRIDE_MAP_WORLD_MAX_OVERVIEW_ZOOM) {
                /* Legacy non-ORMap compatibility only. */
                openride_map_world_draw(map_world,
                                        &camera,
                                        map_style,
                                        NULL,
                                        width,
                                        height);
            }
        }
        const uint64_t map_zoom_map_finished_ns =
            map_zoom_test.active ? SDL_GetTicksNS() : 0U;
#ifdef __ANDROID__
        const Uint64 audit_frame_profile_map_finished_ns =
            audit_frame_profile_active ? SDL_GetTicksNS() : 0U;
        if (audit_frame_profile_active && map_world) {
            openride_map_world_get_debug_stats(
                map_world,
                &audit_frame_world_debug);
        }
#endif
        if (map_zoom_test.active) {
            if (map_zoom_loop_started_ns != 0U) map_zoom_profile.update_ms=(double)(map_zoom_map_started_ns-map_zoom_loop_started_ns)/1000000.0;
            map_zoom_profile.map_ms=(double)(map_zoom_map_finished_ns-map_zoom_map_started_ns)/1000000.0;
            if (map_world) {
                openride_map_world_get_debug_stats(map_world,&map_zoom_world_debug);
                map_zoom_profile.world_overview_ms=map_zoom_world_debug.overview_ms;
                map_zoom_profile.world_detail_ms=map_zoom_world_debug.detail_ms;
                map_zoom_profile.masks_ms=map_zoom_world_debug.masks_ms;
                map_zoom_profile.areas_layer_ms=map_zoom_world_debug.areas_ms;
                map_zoom_profile.waterways_ms=map_zoom_world_debug.waterways_ms;
                map_zoom_profile.roads_layer_ms=map_zoom_world_debug.roads_ms;
                map_zoom_profile.labels_ms=map_zoom_world_debug.labels_ms;
                map_zoom_profile.visible_detail_regions=map_zoom_world_debug.visible_detail_regions;
                map_zoom_profile.ormap_stats_valid=map_zoom_world_debug.ormap_stats_valid;
            }
        }

        if (gpx_loaded) {
            openride_app_render_gpx_document(renderer,
                              &camera,
                              &gpx_overlay,
                              width,
                              height);
        }

        const OpenRideRoute *preview_route =
            openride_route_choice_preview_route(&route_choice);
        const bool route_preview_active = preview_route != NULL;
        if (route_preview_active) {
            /*
             * Preview rendering is geometry-only, so regional node ids from
             * temporary graphs cannot leak into the active graph.
             */
            openride_app_render_route_preview(renderer,
                                              &camera,
                                              preview_route,
                                              width,
                                              height);
        } else if (route_valid) {
            openride_app_render_route(renderer,
                       &camera,
                       &routing_graph,
                       &route,
                       width,
                       height);
        }
        if (route_valid && !route_preview_active && !drive_mode.active) {
            openride_app_render_snap_connector(renderer,
                                &camera,
                                &selection,
                                &start_snap,
                                OPENRIDE_MARKER_START,
                                width,
                                height);
            if (!loop_active) {
                openride_app_render_snap_connector(renderer,
                                    &camera,
                                    &selection,
                                    &destination_snap,
                                    OPENRIDE_MARKER_DESTINATION,
                                    width,
                                    height);
            } else {
                openride_app_render_loop_waypoints(renderer,
                                    &camera,
                                    loop_waypoints,
                                    loop_waypoint_count,
                                    width,
                                    height);
            }
        }
        if (!drive_mode.active) {
            openride_app_render_selection(renderer,
                           &camera,
                           &selection,
                           !route_valid && !route_preview_active,
                           width,
                           height);
        }
        if (gps_sample_valid) {
            OpenRideGPSSample display_sample = gps_sample;
            if (filtered_location.valid) {
                display_sample.lat = filtered_location.lat;
                display_sample.lon = filtered_location.lon;
                display_sample.speed_mps = filtered_location.speed_mps;
                display_sample.heading_deg = filtered_location.heading_deg;
            }
            openride_app_render_navigation_position(renderer,
                                     &camera,
                                     &display_sample,
                                     &navigation_state,
                                     width,
                                     height);
        }
        if (!drive_mode.active && !route_preview_active) {
            openride_app_render_center_marker(renderer, width, height);
        }
        if (!drive_mode.active
            && app_panel == OPENRIDE_APP_PANEL_NONE
            && !place_search_active) {
            openride_app_ui_draw_map_status_overlay(renderer,
                                    &camera,
                                    &selection,
                                    metadata,
                                    scalable_map,
                                    graph_loaded,
                                    routing_profile,
                                    map_style,
                                    &route,
                                    route_valid,
                                    route_status,
                                    &start_snap,
                                    &destination_snap,
                                    loop_active,
                                    loop_target_distance_m,
                                    loop_direction,
                                    &loop_stats,
                                    &gpx_overlay,
                                    gpx_loaded,
                                    gpx_recording_active,
                                    gpx_navigation_active,
#ifdef __ANDROID__
                                    true,
#else
                                    false,
#endif
                                    width,
                                    height);
        }
        if (!drive_mode.active) {
            openride_app_ui_draw_navigation_overlay(renderer,
                                    &navigation_state,
                                    &navigation_instructions,
                                    &gps_simulator,
                                    &route,
                                    &navigation_session,
                                    gps_sample_valid,
                                    follow_gps,
                                    auto_reroute,
                                    simulator_deviation,
                                    gpx_navigation_active,
                                    height);
        }
#ifdef __ANDROID__
        if (drive_mode.active) {
            openride_app_ui_draw_drive_mode(renderer,
                               metadata,
                               &navigation_state,
                               &navigation_instructions,
                               &route,
                               &navigation_session,
                               &drive_mode,
                               auto_reroute,
                               simulated_gps_active,
                               simulator_deviation,
                               simulated_location_context.time_scale,
                               missed_turn_dev.armed,
                               missed_turn_dev.active,
                               width,
                               height);
        }
#endif
        openride_app_ui_draw_panel(renderer,
                       app_panel,
                       favorite_places,
                       favorite_count,
                       history_places,
                       history_count,
                       app_panel_selected,
                       map_style,
                       routing_profile,
                       follow_gps,
                       auto_reroute,
                       voice_enabled,
#ifdef __ANDROID__
                       simulated_gps_active,
                       simulator_deviation,
                       simulated_location_context.time_scale,
                       missed_turn_dev.armed,
                       missed_turn_dev.active,
#else
                       false,
                       false,
                       1.0,
                       false,
                       false,
#endif
                       region,
                       &region_status,
                       region == active_region,
                       region_busy,
                       region_progress,
                       region_work_status,
                       &selection,
                       gps_sample_valid,
#ifdef __ANDROID__
                       android_gps_accuracy_m,
#else
                       gps_sample_valid ? 5.0 : 0.0,
#endif
                       planner_mode,
                       planner_busy,
                       planner_async_context.status[0]
                           ? planner_async_context.status : NULL,
                       loop_target_distance_m,
                       loop_direction,
                       &route_choice,
                       &loop_proposals,
                       &route_download_plan,
                       width);
        const char *place_search_title =
            place_search_purpose == OPENRIDE_PLACE_SEARCH_ROUTE_START
                ? "RECHERCHER LE DEPART"
                : place_search_purpose == OPENRIDE_PLACE_SEARCH_ROUTE_DESTINATION
                    ? "RECHERCHER L'ARRIVEE"
                    : "RECHERCHER UN LIEU";
        openride_app_ui_draw_place_search_overlay(renderer,
                                  place_search_active,
                                  place_world != NULL,
                                  place_search_title,
                                  place_search_query,
                                  place_search_results,
                                  place_search_result_count,
                                  place_search_selected,
                                  width);
        if (!drive_mode.active
            && !place_search_active
            && app_panel == OPENRIDE_APP_PANEL_NONE) {
            openride_app_ui_draw_toolbar(renderer, width, height, route_valid);
        }
        if (map_zoom_test.active) {
            const SDL_Rect safe = openride_app_render_safe_area(renderer, width, height);
            const float ui_scale = openride_app_render_ui_scale(renderer);
            const float text_scale = ui_scale > 2.0f ? 2.0f : ui_scale;
            const float margin = 8.0f * ui_scale;
            const char *phase = map_zoom_test.direction > 0
                ? "ZOOM +"
                : map_zoom_test.direction < 0 ? "ZOOM -" : "FIN";
            char zoom_line[96];
            char gps_line[96];
            char road_line[160];
            char road_work_line[96];
            char area_line[160];
            OpenRideORMapRoadDebugStats road_debug;
            OpenRideORMapAreaDebugStats area_debug;
            memset(&road_debug, 0, sizeof(road_debug));
            memset(&area_debug, 0, sizeof(area_debug));
            road_debug.prewarm_zoom = -1;
            if (map_zoom_world_debug.ormap_stats_valid) {
                road_debug = map_zoom_world_debug.road;
                area_debug = map_zoom_world_debug.area;
            } else if (!world_available && ormap_map && renderer_initialized) {
                openride_ormap_renderer_get_road_debug_stats(&ormap_renderer,
                                                              &road_debug);
                openride_ormap_renderer_get_area_debug_stats(&ormap_renderer,
                                                              &area_debug);
            }
            snprintf(zoom_line, sizeof(zoom_line),
                     "TEST LOD  %s  z=%.3f", phase, map_zoom_test.zoom);
            snprintf(gps_line, sizeof(gps_line),
                     "CENTER %.6f, %.6f",
                     map_zoom_test.center_lat,
                     map_zoom_test.center_lon);
            snprintf(road_line,
                     sizeof(road_line),
                     "R %.1fms L%.1f H%u M%u P%u D%u X%u z%d",
                     road_debug.roads_ms,
                     road_debug.load_ms,
                     road_debug.cache_hits,
                     road_debug.cache_misses,
                     road_debug.prewarm_loads,
                     road_debug.draw_loads,
                     road_debug.deferred_loads,
                     road_debug.prewarm_zoom);
            snprintf(road_work_line,
                     sizeof(road_work_line),
                     "T%u S%u B%u",
                     road_debug.tiles_visited,
                     road_debug.segments_drawn,
                     road_debug.batches);
            snprintf(area_line,
                     sizeof(area_line),
                     "A%.1f L%.1f T%u G%u B%u P%u D%u X%u",
                     area_debug.areas_ms,
                     area_debug.load_ms,
                     area_debug.tiles_visited,
                     area_debug.triangles_drawn,
                     area_debug.batches,
                     area_debug.prewarm_loads,
                     area_debug.draw_loads,
                     area_debug.deferred_loads);
            float badge_width = 330.0f * ui_scale;
            const float max_badge_width = (float)safe.w - 2.0f * margin;
            if (badge_width > max_badge_width) badge_width = max_badge_width;
            SDL_FRect badge = {
                (float)safe.x + margin,
                (float)safe.y + margin,
                badge_width,
                98.0f * ui_scale
            };
            SDL_SetRenderDrawColor(renderer, 12, 16, 20, 224);
            SDL_RenderFillRect(renderer, &badge);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 180);
            SDL_RenderRect(renderer, &badge);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            openride_app_render_scaled_text(renderer,
                             badge.x + 8.0f * ui_scale,
                             badge.y + 7.0f * ui_scale,
                             text_scale,
                             zoom_line);
            openride_app_render_scaled_text(renderer,
                             badge.x + 8.0f * ui_scale,
                             badge.y + 25.0f * ui_scale,
                             text_scale,
                             gps_line);
            openride_app_render_scaled_text(renderer,
                             badge.x + 8.0f * ui_scale,
                             badge.y + 43.0f * ui_scale,
                             text_scale,
                             road_line);
            openride_app_render_scaled_text(renderer,
                             badge.x + 8.0f * ui_scale,
                             badge.y + 61.0f * ui_scale,
                             text_scale,
                             road_work_line);
            openride_app_render_scaled_text(renderer,
                             badge.x + 8.0f * ui_scale,
                             badge.y + 79.0f * ui_scale,
                             text_scale,
                             area_line);
        }

        const uint64_t map_zoom_ui_finished_ns =
            map_zoom_test.active ? SDL_GetTicksNS() : 0U;
        if (map_zoom_test.active) {
            map_zoom_profile.ui_ms =
                (double)(map_zoom_ui_finished_ns - map_zoom_map_finished_ns)
                / 1000000.0;
        }
#ifdef __ANDROID__
        const Uint64 audit_frame_profile_ui_finished_ns =
            audit_frame_profile_active ? SDL_GetTicksNS() : 0U;
#endif

        SDL_RenderPresent(renderer);

        bool map_followup_required = false;
        if (world_available && map_world) {
            map_followup_required =
                openride_map_world_needs_followup_frame(map_world);
        } else if (ormap_map && renderer_initialized) {
            map_followup_required =
                openride_ormap_renderer_needs_followup_frame(&ormap_renderer);
        }
        if (map_followup_required && render_followup_budget > 0U) {
            --render_followup_budget;
            render_dirty = true;
        } else {
#ifdef __ANDROID__
            if (map_followup_required && render_followup_budget == 0U) {
                SDL_Log(
                    "AUDIT_DIRTY_FOLLOWUP_CAPPED budget=%u zoom=%.3f",
                    OPENRIDE_RENDER_FOLLOWUP_BUDGET,
                    camera.zoom);
            }
#endif
            render_dirty = false;
        }

#ifdef __ANDROID__
        openride_dirty_render_audit_tick(&dirty_render_audit, true);
        if (audit_frame_profile_active) {
            const Uint64 audit_frame_profile_present_ns = SDL_GetTicksNS();
            const double audit_frame_ms =
                (double)(audit_frame_profile_present_ns
                         - audit_frame_profile_started_ns)
                / 1000000.0;
            const double audit_update_ms =
                (double)(audit_frame_profile_map_started_ns
                         - audit_frame_profile_started_ns)
                / 1000000.0;
            const double audit_map_ms =
                (double)(audit_frame_profile_map_finished_ns
                         - audit_frame_profile_map_started_ns)
                / 1000000.0;
            const double audit_ui_ms =
                (double)(audit_frame_profile_ui_finished_ns
                         - audit_frame_profile_map_finished_ns)
                / 1000000.0;
            const double audit_present_ms =
                (double)(audit_frame_profile_present_ns
                         - audit_frame_profile_ui_finished_ns)
                / 1000000.0;

            OpenRideORMapRoadDebugStats audit_road_debug;
            OpenRideORMapAreaDebugStats audit_area_debug;
            memset(&audit_road_debug, 0, sizeof(audit_road_debug));
            memset(&audit_area_debug, 0, sizeof(audit_area_debug));
            audit_road_debug.prewarm_zoom = -1;

            if (audit_frame_world_debug.ormap_stats_valid) {
                audit_road_debug = audit_frame_world_debug.road;
                audit_area_debug = audit_frame_world_debug.area;
            } else if (!world_available
                       && ormap_map
                       && renderer_initialized) {
                openride_ormap_renderer_get_road_debug_stats(
                    &ormap_renderer,
                    &audit_road_debug);
                openride_ormap_renderer_get_area_debug_stats(
                    &ormap_renderer,
                    &audit_area_debug);
            }

            SDL_Log(
                "AUDIT_FRAME_PROFILE idx=%u zoom=%.3f frame_ms=%.3f update_ms=%.3f map_ms=%.3f ui_ms=%.3f present_ms=%.3f world_detail_ms=%.3f masks_ms=%.3f areas_layer_ms=%.3f waterways_ms=%.3f roads_layer_ms=%.3f labels_ms=%.3f regions=%u stats=%d",
                audit_frame_profile_index,
                camera.zoom,
                audit_frame_ms,
                audit_update_ms,
                audit_map_ms,
                audit_ui_ms,
                audit_present_ms,
                audit_frame_world_debug.detail_ms,
                audit_frame_world_debug.masks_ms,
                audit_frame_world_debug.areas_ms,
                audit_frame_world_debug.waterways_ms,
                audit_frame_world_debug.roads_ms,
                audit_frame_world_debug.labels_ms,
                audit_frame_world_debug.visible_detail_regions,
                audit_frame_world_debug.ormap_stats_valid ? 1 : 0);

            SDL_Log(
                "AUDIT_FRAME_PROFILE_ROADS idx=%u roads_ms=%.3f load_ms=%.3f hits=%u misses=%u prewarm=%u draw=%u deferred=%u tiles=%u segments=%u batches=%u prewarm_z=%d",
                audit_frame_profile_index,
                audit_road_debug.roads_ms,
                audit_road_debug.load_ms,
                audit_road_debug.cache_hits,
                audit_road_debug.cache_misses,
                audit_road_debug.prewarm_loads,
                audit_road_debug.draw_loads,
                audit_road_debug.deferred_loads,
                audit_road_debug.tiles_visited,
                audit_road_debug.segments_drawn,
                audit_road_debug.batches,
                audit_road_debug.prewarm_zoom);

            SDL_Log(
                "AUDIT_FRAME_PROFILE_AREAS idx=%u areas_ms=%.3f load_ms=%.3f mask_compile_ms=%.3f tiles=%u triangles=%u batches=%u prewarm=%u draw=%u deferred=%u mask_tiles=%u mask_rects=%u mask_batches=%u mask_compile_rects=%u mask_hits=%u mask_misses=%u mask_fail=%u",
                audit_frame_profile_index,
                audit_area_debug.areas_ms,
                audit_area_debug.load_ms,
                audit_area_debug.mask_compile_ms,
                audit_area_debug.tiles_visited,
                audit_area_debug.triangles_drawn,
                audit_area_debug.batches,
                audit_area_debug.prewarm_loads,
                audit_area_debug.draw_loads,
                audit_area_debug.deferred_loads,
                audit_area_debug.mask_tiles,
                audit_area_debug.mask_rects,
                audit_area_debug.mask_batches,
                audit_area_debug.mask_compile_rects,
                audit_area_debug.mask_cache_hits,
                audit_area_debug.mask_cache_misses,
                audit_area_debug.mask_compile_failures);

            if (map_world) {
                openride_map_world_debug_end_frame(map_world);
            }

            ++audit_frame_profile_index;
            --audit_frame_profile_remaining;
            if (audit_frame_profile_remaining == 0U) {
                SDL_Log(
                    "AUDIT_FRAME_PROFILE_DONE frames=%u zoom=%.3f",
                    audit_frame_profile_index,
                    camera.zoom);
            }
        }

        if (!isfinite(audit_last_presented_zoom)
            || fabs(camera.zoom - audit_last_presented_zoom) > 1e-7) {
            SDL_Log("AUDIT_FRAME_PRESENT zoom=%.3f", camera.zoom);
            audit_last_presented_zoom = camera.zoom;
        }

        const Uint64 audit_pacing_now_ns = SDL_GetTicksNS();
        const Uint64 audit_pacing_elapsed_ns =
            audit_pacing_now_ns - audit_pacing_window_started_ns;
        if (audit_pacing_elapsed_ns >= 2000000000ULL) {
            const uint32_t audit_pacing_total_frames =
                audit_pacing_interactive_frames
                + audit_pacing_active_frames
                + audit_pacing_idle_frames;
            const double audit_pacing_window_s =
                (double)audit_pacing_elapsed_ns / 1000000000.0;
            const double audit_pacing_fps =
                audit_pacing_window_s > 0.0
                    ? (double)audit_pacing_total_frames / audit_pacing_window_s
                    : 0.0;
            const int audit_pacing_target_fps =
                target_frame_ns == OPENRIDE_INTERACTIVE_FRAME_NS ? 60
                : target_frame_ns == OPENRIDE_ACTIVE_FRAME_NS ? 30
                : 15;

            SDL_Log("AUDIT_FRAME_PACING window_s=%.3f fps=%.2f interactive=%u active=%u idle=%u target_fps=%d",
                    audit_pacing_window_s,
                    audit_pacing_fps,
                    audit_pacing_interactive_frames,
                    audit_pacing_active_frames,
                    audit_pacing_idle_frames,
                    audit_pacing_target_fps);

            audit_pacing_window_started_ns = audit_pacing_now_ns;
            audit_pacing_interactive_frames = 0U;
            audit_pacing_active_frames = 0U;
            audit_pacing_idle_frames = 0U;
        }
#endif

        if (!first_frame_logged) {
            const Uint64 first_frame_ns = SDL_GetTicksNS();
            const double first_frame_elapsed_ms =
                (double)(first_frame_ns - app_started_ns) / 1000000.0;
#ifdef __ANDROID__
            SDL_Log("AUDIT_FIRST_FRAME_READY pid=%ld elapsed_ms=%.3f width=%d height=%d zoom=%.3f vsync=%d",
                    (long)getpid(),
                    first_frame_elapsed_ms,
                    width,
                    height,
                    camera.zoom,
                    renderer_vsync_enabled ? 1 : 0);
#else
            SDL_Log("Runtime first frame ready elapsed_ms=%.3f width=%d height=%d zoom=%.3f vsync=%d",
                    first_frame_elapsed_ms,
                    width,
                    height,
                    camera.zoom,
                    renderer_vsync_enabled ? 1 : 0);
#endif
            first_frame_logged = true;
        }

        if (map_zoom_test.active) {
            const uint64_t map_zoom_present_ns=SDL_GetTicksNS();
            map_zoom_profile.present_ms=(double)(map_zoom_present_ns-map_zoom_ui_finished_ns)/1000000.0;
            OpenRideORMapRoadDebugStats map_zoom_road_debug;OpenRideORMapAreaDebugStats map_zoom_area_debug;
            memset(&map_zoom_road_debug,0,sizeof(map_zoom_road_debug));memset(&map_zoom_area_debug,0,sizeof(map_zoom_area_debug));map_zoom_road_debug.prewarm_zoom=-1;
            if(map_zoom_world_debug.ormap_stats_valid){map_zoom_road_debug=map_zoom_world_debug.road;map_zoom_area_debug=map_zoom_world_debug.area;}
            else if(!world_available&&ormap_map&&renderer_initialized){openride_ormap_renderer_get_road_debug_stats(&ormap_renderer,&map_zoom_road_debug);openride_ormap_renderer_get_area_debug_stats(&ormap_renderer,&map_zoom_area_debug);map_zoom_profile.ormap_stats_valid=true;}
            if(map_world)openride_map_world_debug_end_frame(map_world);
            (void)openride_map_zoom_test_record_present(&map_zoom_test,map_zoom_present_ns,&map_zoom_profile,&map_zoom_road_debug,&map_zoom_area_debug,route_status,sizeof(route_status));
        }
    }

    openride_map_zoom_test_destroy(&map_zoom_test);

#ifdef __ANDROID__
    if (region_download_started) {
        openride_android_region_download_cancel();
        region_download_started = false;
        region_download_is_poly = false;
    }
    if (region_prepare_thread) {
        SDL_WaitThread(region_prepare_thread, NULL);
        region_prepare_thread = NULL;
    }
    real_gps_requested = false;
    if (simulated_gps_active) {
        openride_location_provider_stop(&simulated_location_provider);
        simulated_gps_active = false;
    }
    simulator_deviation = false;
    openride_gps_simulator_set_lateral_offset_m(&gps_simulator, 0.0);
    openride_android_missed_turn_dev_destroy(
        &missed_turn_dev,
        &simulated_location_context,
        &gps_simulator);
    if (real_gps_active) {
        openride_location_provider_stop(&location_provider);
        real_gps_active = false;
    }
    openride_voice_guidance_reset(&voice_guidance);
    openride_android_voice_guidance_shutdown();
#endif

    if (planner_async_thread) {
        SDL_WaitThread(planner_async_thread, NULL);
        planner_async_thread = NULL;
    }
    openride_app_planner_async_reset(&planner_async_context);
    planner_busy = OPENRIDE_RIDE_PLANNER_IDLE;

    if (routing_world_thread) {
        SDL_WaitThread(routing_world_thread, NULL);
        routing_world_thread = NULL;
    }
    openride_route_destroy(&routing_world_context.route);
    openride_routing_world_cache_destroy(&routing_world_cache);

    if (lifecycle_watch_installed) {
        SDL_RemoveEventWatch(openride_app_support_lifecycle_event_watch, &lifecycle_watch);
        lifecycle_watch_installed = false;
    }

    openride_map_world_destroy(map_world);
    if (ormap_map) {
        openride_ormap_renderer_destroy(&ormap_renderer);
    } else if (vector_map) {
        openride_vector_map_renderer_destroy(&vector_renderer);
    } else if (map) {
        openride_map_renderer_destroy(&raster_renderer);
    }

    openride_navigation_instructions_destroy(&navigation_instructions);
    openride_gpx_document_destroy(&gpx_recording);
    openride_gpx_document_destroy(&gpx_overlay);
    openride_gps_simulator_destroy(&gps_simulator);
    openride_navigation_engine_destroy(&navigation);
    openride_loop_proposal_set_destroy(&loop_proposals);
    openride_route_destroy(&route);
    openride_routing_graph_destroy(&routing_graph);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    openride_place_world_destroy(place_world);
    openride_place_index_close(place_index);
    openride_app_storage_close(app_storage);
    openride_mbtiles_close(map);
    openride_ormap_close(ormap);
    SDL_Quit();

    return 0;
}
