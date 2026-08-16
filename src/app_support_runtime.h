#ifndef OPENRIDE_APP_SUPPORT_RUNTIME_H
#define OPENRIDE_APP_SUPPORT_RUNTIME_H

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


typedef enum OpenRideLifecycleSignal {
    OPENRIDE_LIFECYCLE_SIGNAL_NONE = 0,
    OPENRIDE_LIFECYCLE_SIGNAL_BACKGROUND,
    OPENRIDE_LIFECYCLE_SIGNAL_FOREGROUND,
    OPENRIDE_LIFECYCLE_SIGNAL_LOW_MEMORY,
    OPENRIDE_LIFECYCLE_SIGNAL_TERMINATING
} OpenRideLifecycleSignal;

typedef struct OpenRideLifecycleWatch {
    SDL_AtomicInt pending_signal;
} OpenRideLifecycleWatch;

#ifdef __ANDROID__
typedef struct OpenRideAndroidMissedTurnDev {
    OpenRideGPSSimulator simulator;
    OpenRideDevMissedTurnPlan plan;
    bool armed;
    bool active;
} OpenRideAndroidMissedTurnDev;
#endif

bool SDLCALL openride_app_support_lifecycle_event_watch(void *userdata, SDL_Event *event);

double openride_app_support_clampd(double value, double min_value, double max_value);

bool openride_app_support_file_exists(const char *path);

bool openride_app_support_is_vector_map(const OpenRideMBTilesMetadata *metadata);

bool openride_app_support_has_suffix(const char *text, const char *suffix);

const char *openride_app_support_default_map_path(void);

const char *openride_app_support_default_routing_graph_path(void);

void openride_app_render_scaled_text(SDL_Renderer *renderer,
                             float x,
                             float y,
                             float scale,
                             const char *text);

void openride_app_render_center_marker(SDL_Renderer *renderer, int width, int height);

OpenRideSelectionMarker openride_app_render_marker_at_screen(const OpenRideMapCamera *camera,
                                                const OpenRideMapSelection *selection,
                                                double x,
                                                double y,
                                                int viewport_width,
                                                int viewport_height);

void openride_app_render_route(SDL_Renderer *renderer,
                       const OpenRideMapCamera *camera,
                       const OpenRideRoutingGraph *graph,
                       const OpenRideRoute *route,
                       int viewport_width,
                       int viewport_height);

void openride_app_render_gpx_document(SDL_Renderer *renderer,
                              const OpenRideMapCamera *camera,
                              const OpenRideGPXDocument *document,
                              int viewport_width,
                              int viewport_height);

void openride_app_render_loop_waypoints(SDL_Renderer *renderer,
                                const OpenRideMapCamera *camera,
                                const OpenRideRoutePoint *waypoints,
                                uint32_t waypoint_count,
                                int viewport_width,
                                int viewport_height);

void openride_app_render_snap_connector(SDL_Renderer *renderer,
                                const OpenRideMapCamera *camera,
                                const OpenRideMapSelection *selection,
                                const OpenRideRoutingSnap *snap,
                                OpenRideSelectionMarker marker,
                                int viewport_width,
                                int viewport_height);

void openride_app_render_selection(SDL_Renderer *renderer,
                           const OpenRideMapCamera *camera,
                           const OpenRideMapSelection *selection,
                           bool draw_direct_line,
                           int viewport_width,
                           int viewport_height);

SDL_Rect openride_app_render_safe_area(SDL_Renderer *renderer,
                                              int viewport_width,
                                              int viewport_height);

float openride_app_render_ui_scale(SDL_Renderer *renderer);

void openride_app_render_navigation_position(SDL_Renderer *renderer,
                                     const OpenRideMapCamera *camera,
                                     const OpenRideGPSSample *gps,
                                     const OpenRideNavigationState *navigation,
                                     int viewport_width,
                                     int viewport_height);

#ifdef __ANDROID__
void openride_android_missed_turn_dev_init(
    OpenRideAndroidMissedTurnDev *state);

void openride_android_missed_turn_dev_reset(
    OpenRideAndroidMissedTurnDev *state,
    OpenRideSimulatedLocationContext *location_context,
    OpenRideGPSSimulator *base_simulator);

void openride_android_missed_turn_dev_destroy(
    OpenRideAndroidMissedTurnDev *state,
    OpenRideSimulatedLocationContext *location_context,
    OpenRideGPSSimulator *base_simulator);
#endif

#endif
