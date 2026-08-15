#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
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
#include "openride/ui_toolbar.h"
#include "openride/ui_main_menu.h"
#include "openride/ui_route_panel.h"
#include "openride/ui_settings_panel.h"
#include "openride/ui_regions_panel.h"
#include "openride/ui_places_panel.h"
#include "openride/ui_search_overlay.h"
#include "openride/ui_route_downloads_panel.h"
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
#define OPENRIDE_APP_LIST_MAX 12U
#define OPENRIDE_REAL_MAP_PATH "data/maps/nord-pas-de-calais.ormap"
#define OPENRIDE_ROUTING_GRAPH_PATH "data/routing/nord-pas-de-calais.orgraph"

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

static void openride_android_missed_turn_dev_init(
    OpenRideAndroidMissedTurnDev *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    openride_gps_simulator_init(&state->simulator);
    openride_dev_missed_turn_plan_init(&state->plan);
}

static void openride_android_missed_turn_dev_reset(
    OpenRideAndroidMissedTurnDev *state,
    OpenRideSimulatedLocationContext *location_context,
    OpenRideGPSSimulator *base_simulator)
{
    if (!state) return;
    if (location_context
        && location_context->simulator == &state->simulator) {
        location_context->simulator = base_simulator;
    }
    openride_gps_simulator_clear_route(&state->simulator);
    openride_dev_missed_turn_plan_destroy(&state->plan);
    state->armed = false;
    state->active = false;
}

static void openride_android_missed_turn_dev_destroy(
    OpenRideAndroidMissedTurnDev *state,
    OpenRideSimulatedLocationContext *location_context,
    OpenRideGPSSimulator *base_simulator)
{
    if (!state) return;
    openride_android_missed_turn_dev_reset(
        state, location_context, base_simulator);
    openride_gps_simulator_destroy(&state->simulator);
}

typedef struct OpenRideRegionPrepareThreadContext {
    OpenRidePlatformPaths paths;
    const OpenRideRegionDefinition *region;
    SDL_AtomicInt stage;
    SDL_AtomicInt done;
    SDL_AtomicInt success;
    OpenRideRegionPrepareStats stats;
    char error[256];
} OpenRideRegionPrepareThreadContext;

static void region_prepare_progress(OpenRideRegionPrepareStage stage,
                                    const char *message,
                                    void *userdata)
{
    (void)message;
    OpenRideRegionPrepareThreadContext *context = userdata;
    if (context) SDL_SetAtomicInt(&context->stage, (int)stage);
}

static int SDLCALL region_prepare_thread_main(void *userdata)
{
    OpenRideRegionPrepareThreadContext *context = userdata;
    if (!context) return 1;
    const bool ok = openride_region_prepare_from_pbf(&context->paths,
                                                      context->region,
                                                      false,
                                                      region_prepare_progress,
                                                      context,
                                                      &context->stats,
                                                      context->error,
                                                      sizeof(context->error));
    SDL_SetAtomicInt(&context->success, ok ? 1 : 0);
    SDL_SetAtomicInt(&context->done, 1);
    return ok ? 0 : 1;
}

static SDL_Thread *start_region_prepare_thread(
    OpenRideRegionPrepareThreadContext *context,
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *region)
{
    if (!context || !paths || !region) return NULL;
    memset(context, 0, sizeof(*context));
    context->paths = *paths;
    context->region = region;
    SDL_SetAtomicInt(&context->stage, OPENRIDE_REGION_PREPARE_ROUTING);
    return SDL_CreateThread(region_prepare_thread_main,
                            "OpenRide-region-prepare",
                            context);
}

static const char *region_prepare_stage_text(int stage)
{
    switch ((OpenRideRegionPrepareStage)stage) {
        case OPENRIDE_REGION_PREPARE_ROUTING: return "Preparation 1/3: routage";
        case OPENRIDE_REGION_PREPARE_SEARCH: return "Preparation 2/3: recherche";
        case OPENRIDE_REGION_PREPARE_MAP: return "Preparation 3/3: carte .ormap";
        case OPENRIDE_REGION_PREPARE_FINALIZING: return "Finalisation de la region";
        case OPENRIDE_REGION_PREPARE_COMPLETE: return "Region prete";
        case OPENRIDE_REGION_PREPARE_ERROR: return "Erreur de preparation";
        default: return "Preparation...";
    }
}

static double region_prepare_stage_progress(int stage)
{
    switch ((OpenRideRegionPrepareStage)stage) {
        case OPENRIDE_REGION_PREPARE_ROUTING: return 0.18;
        case OPENRIDE_REGION_PREPARE_SEARCH: return 0.50;
        case OPENRIDE_REGION_PREPARE_MAP: return 0.72;
        case OPENRIDE_REGION_PREPARE_FINALIZING: return 0.96;
        case OPENRIDE_REGION_PREPARE_COMPLETE: return 1.0;
        default: return -1.0;
    }
}

static bool start_android_region_file_download(
    const OpenRideRegionDefinition *region,
    bool poly,
    bool *download_started,
    bool *download_is_poly,
    bool *region_busy,
    double *region_progress,
    char *work_status,
    size_t work_status_size)
{
    if (!region || !download_started || !download_is_poly
        || !region_busy || !region_progress || !work_status) return false;
    const char *url = poly ? region->poly_url : region->pbf_url;
    const char *filename = poly ? region->poly_filename : region->pbf_filename;
    const char *directory = poly ? "data/maps" : "data/downloads";
    char relative_path[384];
    snprintf(relative_path, sizeof(relative_path), "%s/%s", directory, filename);
    if (!openride_android_region_download_start(url, relative_path)) {
        snprintf(work_status,
                 work_status_size,
                 "%s",
                 poly
                     ? "Impossible de lancer le telechargement du contour"
                     : "Impossible de lancer le telechargement OSM");
        return false;
    }
    *download_started = true;
    *download_is_poly = poly;
    *region_busy = true;
    *region_progress = 0.0;
    snprintf(work_status,
             work_status_size,
             "%s",
             poly ? "Telechargement du contour de region"
                  : "Telechargement des donnees OSM");
    return true;
}

static bool begin_android_region_install(
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *region,
    OpenRideRegionStatus *status,
    OpenRideRegionPrepareThreadContext *prepare_context,
    SDL_Thread **prepare_thread,
    bool *download_started,
    bool *download_is_poly,
    bool *region_busy,
    double *region_progress,
    char *work_status,
    size_t work_status_size,
    char *error,
    size_t error_size)
{
    if (!paths || !region || !status || !prepare_context || !prepare_thread
        || !download_started || !download_is_poly || !region_busy
        || !region_progress || !work_status) return false;
    if (*region_busy || *prepare_thread || *download_started) return false;
    if (!openride_region_get_status(paths, region, status, error, error_size)) return false;

    /* The Geofabrik .poly is tiny and provides the exact offline coverage outline. */
    if (!status->poly_present) {
        return start_android_region_file_download(region,
                                                  true,
                                                  download_started,
                                                  download_is_poly,
                                                  region_busy,
                                                  region_progress,
                                                  work_status,
                                                  work_status_size);
    }
    if (status->source_pbf_present) {
        *prepare_thread = start_region_prepare_thread(prepare_context, paths, region);
        if (!*prepare_thread) {
            snprintf(work_status, work_status_size, "Impossible de lancer la preparation");
            return false;
        }
        *region_busy = true;
        *region_progress = region_prepare_stage_progress(OPENRIDE_REGION_PREPARE_ROUTING);
        snprintf(work_status, work_status_size, "Preparation 1/3: routage");
        return true;
    }
    return start_android_region_file_download(region,
                                              false,
                                              download_started,
                                              download_is_poly,
                                              region_busy,
                                              region_progress,
                                              work_status,
                                              work_status_size);
}
#endif

static bool SDLCALL openride_lifecycle_event_watch(void *userdata, SDL_Event *event)
{
    OpenRideLifecycleWatch *watch = userdata;
    if (!watch || !event) return true;

    int signal = OPENRIDE_LIFECYCLE_SIGNAL_NONE;
    switch (event->type) {
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
        case SDL_EVENT_DID_ENTER_BACKGROUND:
            signal = OPENRIDE_LIFECYCLE_SIGNAL_BACKGROUND;
            break;
        case SDL_EVENT_WILL_ENTER_FOREGROUND:
        case SDL_EVENT_DID_ENTER_FOREGROUND:
            signal = OPENRIDE_LIFECYCLE_SIGNAL_FOREGROUND;
            break;
        case SDL_EVENT_LOW_MEMORY:
            signal = OPENRIDE_LIFECYCLE_SIGNAL_LOW_MEMORY;
            break;
        case SDL_EVENT_TERMINATING:
            signal = OPENRIDE_LIFECYCLE_SIGNAL_TERMINATING;
            break;
        default:
            break;
    }
    if (signal != OPENRIDE_LIFECYCLE_SIGNAL_NONE) {
        SDL_SetAtomicInt(&watch->pending_signal, signal);
    }
    return true;
}

static double clampd(double value, double min_value, double max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static bool file_exists(const char *path)
{
    return openride_platform_file_exists(path);
}

static bool is_vector_map(const OpenRideMBTilesMetadata *metadata)
{
    if (!metadata) return false;
    return strcmp(metadata->format, "pbf") == 0
        || strcmp(metadata->format, "mvt") == 0
        || strcmp(metadata->format, "application/x-protobuf") == 0;
}

static bool has_suffix(const char *text, const char *suffix)
{
    if (!text || !suffix) return false;
    const size_t text_len = strlen(text);
    const size_t suffix_len = strlen(suffix);
    return text_len >= suffix_len
        && strcmp(text + text_len - suffix_len, suffix) == 0;
}

static void metadata_from_ormap(OpenRideMBTilesMetadata *out,
                                const OpenRideORMapMetadata *source)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "%s", source ? source->name : "OpenRide");
    snprintf(out->format, sizeof(out->format), "ormap");
    snprintf(out->attribution,
             sizeof(out->attribution),
             "%s",
             source ? source->attribution : "OpenStreetMap contributors");
    out->min_zoom = source ? source->min_zoom : OPENRIDE_ORMAP_MIN_ROAD_ZOOM;
    out->max_zoom = source ? source->max_zoom : OPENRIDE_ORMAP_MAX_ROAD_ZOOM;
    if (source) {
        out->has_center = source->has_center;
        out->center_lat = source->center_lat;
        out->center_lon = source->center_lon;
        out->center_zoom = source->center_zoom;
        out->has_bounds = source->has_bounds;
        out->west = source->west;
        out->south = source->south;
        out->east = source->east;
        out->north = source->north;
    }
}

static const char *default_map_path(void)
{
    static const char *ormap = "data/maps/nord-pas-de-calais.ormap";
    static const char *legacy_map = "data/maps/nord-pas-de-calais-shortbread.mbtiles";
    static const char *demo_map = "data/maps/demo.mbtiles";

    if (file_exists(ormap)) return ormap;
    if (file_exists(legacy_map)) return legacy_map;
    return demo_map;
}

static const char *default_routing_graph_path(void)
{
    static const char *graph = "data/routing/nord-pas-de-calais.orgraph";
    return file_exists(graph) ? graph : NULL;
}

static void draw_filled_circle(SDL_Renderer *renderer, float cx, float cy, float radius)
{
    const int r = (int)ceilf(radius);

    for (int y = -r; y <= r; ++y) {
        const float fy = (float)y;
        const float inside = radius * radius - fy * fy;
        if (inside < 0.0f) continue;

        const float dx = sqrtf(inside);
        SDL_RenderLine(renderer, cx - dx, cy + fy, cx + dx, cy + fy);
    }
}

static void draw_circle_outline(SDL_Renderer *renderer, float cx, float cy, float radius)
{
    const int segments = 48;
    float previous_x = cx + radius;
    float previous_y = cy;

    for (int i = 1; i <= segments; ++i) {
        const float angle = (float)(6.28318530717958647692 * (double)i / (double)segments);
        const float x = cx + cosf(angle) * radius;
        const float y = cy + sinf(angle) * radius;
        SDL_RenderLine(renderer, previous_x, previous_y, x, y);
        previous_x = x;
        previous_y = y;
    }
}

static void draw_thick_line(SDL_Renderer *renderer,
                            float x1,
                            float y1,
                            float x2,
                            float y2,
                            int width)
{
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = sqrtf(dx * dx + dy * dy);

    if (length < 0.001f || width <= 1) {
        SDL_RenderLine(renderer, x1, y1, x2, y2);
        return;
    }

    const float nx = -dy / length;
    const float ny = dx / length;
    const float half = ((float)width - 1.0f) * 0.5f;

    for (int i = 0; i < width; ++i) {
        const float offset = (float)i - half;
        SDL_RenderLine(renderer,
                       x1 + nx * offset,
                       y1 + ny * offset,
                       x2 + nx * offset,
                       y2 + ny * offset);
    }
}

static void draw_scaled_text(SDL_Renderer *renderer,
                             float x,
                             float y,
                             float scale,
                             const char *text)
{
    float old_scale_x = 1.0f;
    float old_scale_y = 1.0f;

    if (!text || scale <= 0.0f) return;
    if (!SDL_GetRenderScale(renderer, &old_scale_x, &old_scale_y)) return;

    if (SDL_SetRenderScale(renderer, old_scale_x * scale, old_scale_y * scale)) {
        SDL_RenderDebugText(renderer,
                            x / scale,
                            y / scale,
                            text);
        SDL_SetRenderScale(renderer, old_scale_x, old_scale_y);
    }
}

static void draw_center_marker(SDL_Renderer *renderer, int width, int height)
{
    const float cx = (float)width * 0.5f;
    const float cy = (float)height * 0.5f;

    SDL_SetRenderDrawColor(renderer, 20, 20, 20, SDL_ALPHA_OPAQUE);
    SDL_RenderLine(renderer, cx - 11.0f, cy, cx + 11.0f, cy);
    SDL_RenderLine(renderer, cx, cy - 11.0f, cx, cy + 11.0f);

    SDL_SetRenderDrawColor(renderer, 245, 245, 245, SDL_ALPHA_OPAQUE);
    SDL_RenderLine(renderer, cx - 9.0f, cy, cx + 9.0f, cy);
    SDL_RenderLine(renderer, cx, cy - 9.0f, cx, cy + 9.0f);
}

static OpenRidePointD marker_screen_position(const OpenRideMapCamera *camera,
                                             const OpenRideMapSelection *selection,
                                             OpenRideSelectionMarker marker,
                                             int viewport_width,
                                             int viewport_height)
{
    OpenRideGeoPosition position = {0};

    if (marker == OPENRIDE_MARKER_START) {
        position = selection->start;
    } else if (marker == OPENRIDE_MARKER_DESTINATION) {
        position = selection->destination;
    }

    return openride_geo_to_screen(camera,
                                  position.lat,
                                  position.lon,
                                  viewport_width,
                                  viewport_height);
}

static OpenRideSelectionMarker marker_at_screen(const OpenRideMapCamera *camera,
                                                const OpenRideMapSelection *selection,
                                                double x,
                                                double y,
                                                int viewport_width,
                                                int viewport_height)
{
    const double radius_sq = OPENRIDE_MARKER_HIT_RADIUS * OPENRIDE_MARKER_HIT_RADIUS;

    if (selection->has_destination) {
        const OpenRidePointD p = marker_screen_position(camera,
                                                        selection,
                                                        OPENRIDE_MARKER_DESTINATION,
                                                        viewport_width,
                                                        viewport_height);
        const double dx = x - p.x;
        const double dy = y - (p.y - 25.0);
        if (dx * dx + dy * dy <= radius_sq) {
            return OPENRIDE_MARKER_DESTINATION;
        }
    }

    if (selection->has_start) {
        const OpenRidePointD p = marker_screen_position(camera,
                                                        selection,
                                                        OPENRIDE_MARKER_START,
                                                        viewport_width,
                                                        viewport_height);
        const double dx = x - p.x;
        const double dy = y - (p.y - 25.0);
        if (dx * dx + dy * dy <= radius_sq) {
            return OPENRIDE_MARKER_START;
        }
    }

    return OPENRIDE_MARKER_NONE;
}

static void draw_marker(SDL_Renderer *renderer,
                        OpenRidePointD p,
                        OpenRideSelectionMarker marker)
{
    Uint8 r = 42;
    Uint8 g = 157;
    Uint8 b = 84;
    const char *label = "D";
    const float center_x = (float)p.x;
    const float center_y = (float)p.y - 25.0f;

    if (marker == OPENRIDE_MARKER_DESTINATION) {
        r = 214;
        g = 66;
        b = 57;
        label = "A";
    }

    /* Pin shadow and stem. The selected coordinate is the tip of the pin. */
    SDL_SetRenderDrawColor(renderer, 18, 22, 26, 100);
    draw_filled_circle(renderer, center_x + 2.0f, center_y + 3.0f, 16.0f);
    draw_thick_line(renderer,
                    center_x + 2.0f,
                    center_y + 11.0f,
                    center_x + 2.0f,
                    (float)p.y + 2.0f,
                    5);

    SDL_SetRenderDrawColor(renderer, 248, 248, 246, SDL_ALPHA_OPAQUE);
    draw_thick_line(renderer,
                    center_x,
                    center_y + 10.0f,
                    center_x,
                    (float)p.y,
                    5);
    draw_filled_circle(renderer, center_x, center_y, 15.0f);

    SDL_SetRenderDrawColor(renderer, r, g, b, SDL_ALPHA_OPAQUE);
    draw_filled_circle(renderer, center_x, center_y, 12.0f);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
    draw_circle_outline(renderer, center_x, center_y, 12.0f);
    SDL_RenderDebugText(renderer, center_x - 4.0f, center_y - 4.0f, label);
}

static void draw_route(SDL_Renderer *renderer,
                       const OpenRideMapCamera *camera,
                       const OpenRideRoutingGraph *graph,
                       const OpenRideRoute *route,
                       int viewport_width,
                       int viewport_height)
{
    if (!route) return;

    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 0) {
            SDL_SetRenderDrawColor(renderer, 250, 250, 248, 225);
        } else {
            SDL_SetRenderDrawColor(renderer, 37, 101, 173, 245);
        }
        const int width = pass == 0 ? 8 : 4;

        if (route->geometry_count >= 2U && route->geometry) {
            for (uint32_t i = 1U; i < route->geometry_count; ++i) {
                const OpenRidePointD a = openride_geo_to_screen(
                    camera,
                    route->geometry[i - 1U].lat,
                    route->geometry[i - 1U].lon,
                    viewport_width,
                    viewport_height);
                const OpenRidePointD b = openride_geo_to_screen(
                    camera,
                    route->geometry[i].lat,
                    route->geometry[i].lon,
                    viewport_width,
                    viewport_height);
                draw_thick_line(renderer,
                                (float)a.x,
                                (float)a.y,
                                (float)b.x,
                                (float)b.y,
                                width);
            }
            continue;
        }

        if (!graph || route->node_count < 2U) continue;
        for (uint32_t i = 1U; i < route->node_count; ++i) {
            const OpenRideRoutingNodeId a_id = route->nodes[i - 1U];
            const OpenRideRoutingNodeId b_id = route->nodes[i];
            if (a_id >= graph->node_count || b_id >= graph->node_count) continue;

            double a_lat = 0.0;
            double a_lon = 0.0;
            double b_lat = 0.0;
            double b_lon = 0.0;
            openride_routing_node_geo(&graph->nodes[a_id], &a_lat, &a_lon);
            openride_routing_node_geo(&graph->nodes[b_id], &b_lat, &b_lon);

            const OpenRidePointD a = openride_geo_to_screen(camera,
                                                             a_lat,
                                                             a_lon,
                                                             viewport_width,
                                                             viewport_height);
            const OpenRidePointD b = openride_geo_to_screen(camera,
                                                             b_lat,
                                                             b_lon,
                                                             viewport_width,
                                                             viewport_height);
            draw_thick_line(renderer,
                            (float)a.x,
                            (float)a.y,
                            (float)b.x,
                            (float)b.y,
                            width);
        }
    }
}


static void draw_gpx_point_list(SDL_Renderer *renderer,
                                const OpenRideMapCamera *camera,
                                const OpenRideGPXPointList *list,
                                int viewport_width,
                                int viewport_height,
                                Uint8 r,
                                Uint8 g,
                                Uint8 b,
                                int width)
{
    if (!renderer || !camera || !list || list->count < 2U) return;

    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 0) {
            SDL_SetRenderDrawColor(renderer, 250, 250, 248, 210);
        } else {
            SDL_SetRenderDrawColor(renderer, r, g, b, 235);
        }
        const int draw_width = pass == 0 ? width + 3 : width;
        for (uint32_t i = 1U; i < list->count; ++i) {
            if (list->points[i].starts_new_segment) continue;
            const OpenRidePointD a = openride_geo_to_screen(camera,
                                                             list->points[i - 1U].lat,
                                                             list->points[i - 1U].lon,
                                                             viewport_width,
                                                             viewport_height);
            const OpenRidePointD c = openride_geo_to_screen(camera,
                                                             list->points[i].lat,
                                                             list->points[i].lon,
                                                             viewport_width,
                                                             viewport_height);
            draw_thick_line(renderer,
                            (float)a.x,
                            (float)a.y,
                            (float)c.x,
                            (float)c.y,
                            draw_width);
        }
    }
}

static void draw_gpx_document(SDL_Renderer *renderer,
                              const OpenRideMapCamera *camera,
                              const OpenRideGPXDocument *document,
                              int viewport_width,
                              int viewport_height)
{
    if (!document) return;

    draw_gpx_point_list(renderer,
                        camera,
                        &document->track_points,
                        viewport_width,
                        viewport_height,
                        148, 76, 183,
                        4);
    draw_gpx_point_list(renderer,
                        camera,
                        &document->route_points,
                        viewport_width,
                        viewport_height,
                        229, 126, 34,
                        3);

    for (uint32_t i = 0U; i < document->waypoints.count; ++i) {
        const OpenRideGPXPoint *point = &document->waypoints.points[i];
        const OpenRidePointD p = openride_geo_to_screen(camera,
                                                         point->lat,
                                                         point->lon,
                                                         viewport_width,
                                                         viewport_height);
        SDL_SetRenderDrawColor(renderer, 248, 248, 246, 245);
        draw_filled_circle(renderer, (float)p.x, (float)p.y, 7.0f);
        SDL_SetRenderDrawColor(renderer, 0, 142, 153, 245);
        draw_filled_circle(renderer, (float)p.x, (float)p.y, 4.5f);
    }
}

static void fit_camera_to_gpx(OpenRideMapCamera *camera,
                              const OpenRideGPXDocument *document,
                              int viewport_width,
                              int viewport_height,
                              double min_zoom,
                              double max_zoom)
{
    if (!camera || !document || viewport_width <= 100 || viewport_height <= 100) return;
    const OpenRideGPXBounds bounds = openride_gpx_document_bounds(document);
    if (!bounds.valid) return;

    const OpenRidePointD nw = openride_mercator_forward(bounds.max_lat, bounds.min_lon);
    const OpenRidePointD se = openride_mercator_forward(bounds.min_lat, bounds.max_lon);
    double dx = fabs(se.x - nw.x);
    if (dx > 0.5) dx = 1.0 - dx;
    const double dy = fabs(se.y - nw.y);
    const double usable_w = (double)viewport_width - 100.0;
    const double usable_h = (double)viewport_height - 100.0;
    double zoom = max_zoom;

    if (dx > 1e-12 && dy > 1e-12) {
        const double zoom_x = log2(usable_w / (256.0 * dx));
        const double zoom_y = log2(usable_h / (256.0 * dy));
        zoom = fmin(zoom_x, zoom_y);
    } else if (dx > 1e-12) {
        zoom = log2(usable_w / (256.0 * dx));
    } else if (dy > 1e-12) {
        zoom = log2(usable_h / (256.0 * dy));
    }

    camera->zoom = clampd(zoom, min_zoom, max_zoom);
    camera->center_lat = (bounds.min_lat + bounds.max_lat) * 0.5;
    camera->center_lon = (bounds.min_lon + bounds.max_lon) * 0.5;
}

static bool load_gpx_overlay(const char *path,
                             OpenRideGPXDocument *document,
                             char *status,
                             size_t status_size)
{
    char error[256] = {0};
    if (!path || !file_exists(path)) {
        snprintf(status,
                 status_size,
                 "GPX introuvable: %.180s",
                 path ? path : "-");
        return false;
    }

    if (!openride_gpx_load_file(path, document, error, sizeof(error))) {
        snprintf(status,
                 status_size,
                 "import GPX impossible: %.160s",
                 error[0] ? error : "erreur inconnue");
        return false;
    }

    snprintf(status,
             status_size,
             "GPX charge: %u track | %u route | %u waypoint",
             document->track_points.count,
             document->route_points.count,
             document->waypoints.count);
    return true;
}

static void record_gps_sample(OpenRideGPXDocument *recording,
                              const OpenRideGPSSample *sample,
                              double *last_recorded_position_m)
{
    if (!recording || !sample || !sample->valid || !last_recorded_position_m) return;
    if (*last_recorded_position_m >= 0.0
        && sample->route_position_m >= *last_recorded_position_m
        && sample->route_position_m - *last_recorded_position_m < OPENRIDE_GPX_RECORDING_MIN_STEP_M) {
        return;
    }

    OpenRideGPXPoint point;
    memset(&point, 0, sizeof(point));
    point.lat = sample->lat;
    point.lon = sample->lon;
    point.starts_new_segment = (recording->track_points.count == 0U);
    if (openride_gpx_document_append(recording, OPENRIDE_GPX_POINT_TRACK, &point)) {
        if (recording->track_points.count == 1U) recording->track_segment_count = 1U;
        *last_recorded_position_m = sample->route_position_m;
    }
}

static bool recalculate_route(const OpenRideRoutingGraph *graph,
                              bool graph_loaded,
                              const OpenRideMapSelection *selection,
                              OpenRideRoutingProfile profile,
                              OpenRideRoute *route,
                              OpenRideRoutingSnap *start_snap,
                              OpenRideRoutingSnap *destination_snap,
                              char *status,
                              size_t status_size)
{
    openride_route_destroy(route);
    if (start_snap) {
        memset(start_snap, 0, sizeof(*start_snap));
        start_snap->segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    }
    if (destination_snap) {
        memset(destination_snap, 0, sizeof(*destination_snap));
        destination_snap->segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    }

    if (!graph_loaded) {
        snprintf(status, status_size, "graphe routier non installe");
        return false;
    }
    if (!openride_map_selection_complete(selection)) {
        snprintf(status, status_size, "choisis un depart et une destination");
        return false;
    }

    OpenRideRoutingSnap local_start = {0};
    OpenRideRoutingSnap local_destination = {0};
    local_start.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    local_destination.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;

    if (!openride_routing_graph_snap_to_segment(graph,
                                                selection->start.lat,
                                                selection->start.lon,
                                                OPENRIDE_MAX_SNAP_DISTANCE_M,
                                                &local_start)
        || !openride_routing_graph_snap_to_segment(graph,
                                                   selection->destination.lat,
                                                   selection->destination.lon,
                                                   OPENRIDE_MAX_SNAP_DISTANCE_M,
                                                   &local_destination)) {
        snprintf(status, status_size, "point trop loin du reseau routier");
        return false;
    }

    if (start_snap) *start_snap = local_start;
    if (destination_snap) *destination_snap = local_destination;

    OpenRideSnappedRoutingRequest request = openride_snapped_routing_request_default();
    request.start = local_start;
    request.destination = local_destination;
    request.profile = profile;

    char route_error[256] = {0};
    if (!openride_routing_engine_calculate_snapped(graph,
                                                   &request,
                                                   route,
                                                   route_error,
                                                   sizeof(route_error))) {
        snprintf(status,
                 status_size,
                 "itineraire impossible: %.180s",
                 route_error[0] ? route_error : "erreur inconnue");
        return false;
    }

    snprintf(status, status_size, "itineraire calcule sur segments");
    return true;
}



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

static int SDLCALL routing_world_thread_main(void *userdata)
{
    OpenRideRoutingWorldThreadContext *context = userdata;
    if (!context) return 1;

    const bool ok = context->installed_alternative
        ? openride_routing_world_calculate_installed_alternative_cached(
              &context->paths,
              context->active_region,
              context->active_graph,
              context->cache,
              context->selection.start.lat,
              context->selection.start.lon,
              context->selection.destination.lat,
              context->selection.destination.lon,
              OPENRIDE_MAX_SNAP_DISTANCE_M,
              context->profile,
              &context->route,
              &context->result,
              context->error,
              sizeof(context->error))
        : openride_routing_world_calculate_selection_cached(
              &context->paths,
              context->active_region,
              context->active_graph,
              context->cache,
              &context->selection,
              OPENRIDE_MAX_SNAP_DISTANCE_M,
              context->profile,
              &context->route,
              &context->result,
              context->error,
              sizeof(context->error));

    SDL_SetAtomicInt(&context->success, ok ? 1 : 0);
    SDL_SetAtomicInt(&context->done, 1);
    return ok ? 0 : 1;
}

static SDL_Thread *start_routing_world_thread_mode(
    OpenRideRoutingWorldThreadContext *context,
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *active_region,
    const OpenRideRoutingGraph *active_graph,
    OpenRideRoutingWorldCache *cache,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile,
    bool installed_alternative,
    bool reroute,
    bool resume_simulator)
{
    if (!context || !paths || !selection
        || !openride_map_selection_complete(selection)) {
        return NULL;
    }

    openride_route_destroy(&context->route);
    memset(context, 0, sizeof(*context));
    context->paths = *paths;
    context->active_region = active_region;
    context->active_graph = active_graph;
    context->cache = cache;
    context->selection = *selection;
    context->profile = profile;
    context->installed_alternative = installed_alternative;
    context->reroute = reroute;
    context->resume_simulator = resume_simulator;
    SDL_SetAtomicInt(&context->done, 0);
    SDL_SetAtomicInt(&context->success, 0);

    return SDL_CreateThread(routing_world_thread_main,
                            "OpenRide-routing-world",
                            context);
}

static SDL_Thread *start_routing_world_thread(
    OpenRideRoutingWorldThreadContext *context,
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *active_region,
    const OpenRideRoutingGraph *active_graph,
    OpenRideRoutingWorldCache *cache,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile,
    bool reroute,
    bool resume_simulator)
{
    return start_routing_world_thread_mode(
        context,
        paths,
        active_region,
        active_graph,
        cache,
        selection,
        profile,
        false,
        reroute,
        resume_simulator);
}

static SDL_Thread *start_routing_world_installed_alternative_thread(
    OpenRideRoutingWorldThreadContext *context,
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *active_region,
    const OpenRideRoutingGraph *active_graph,
    OpenRideRoutingWorldCache *cache,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile)
{
    return start_routing_world_thread_mode(
        context,
        paths,
        active_region,
        active_graph,
        cache,
        selection,
        profile,
        true,
        false,
        false);
}

static bool routing_world_request_matches(
    const OpenRideRoutingWorldThreadContext *context,
    const OpenRideRegionDefinition *active_region,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile)
{
    if (!context || !selection) return false;
    if (context->active_region != active_region || context->profile != profile) return false;
    if (context->selection.has_start != selection->has_start
        || context->selection.has_destination != selection->has_destination) {
        return false;
    }
    if (strcmp(context->selection.start_region_id,
               selection->start_region_id) != 0
        || strcmp(context->selection.destination_region_id,
                  selection->destination_region_id) != 0) {
        return false;
    }
    if (selection->has_start
        && (context->selection.start.lat != selection->start.lat
            || context->selection.start.lon != selection->start.lon)) {
        return false;
    }
    if (selection->has_destination
        && (context->selection.destination.lat != selection->destination.lat
            || context->selection.destination.lon != selection->destination.lon)) {
        return false;
    }
    return true;
}

static bool generate_loop_route(const OpenRideRoutingGraph *graph,
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
                                size_t status_size)
{
    openride_route_destroy(route);
    if (stats) memset(stats, 0, sizeof(*stats));
    if (waypoint_count) *waypoint_count = 0U;
    if (start_snap) {
        memset(start_snap, 0, sizeof(*start_snap));
        start_snap->segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    }

    if (!graph_loaded) {
        snprintf(status, status_size, "graphe routier non installe");
        return false;
    }
    if (!selection->has_start) {
        snprintf(status, status_size, "choisis d'abord un point de depart");
        return false;
    }

    OpenRideRoutingSnap local_start = {0};
    local_start.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    if (!openride_routing_graph_snap_to_segment(graph,
                                                selection->start.lat,
                                                selection->start.lon,
                                                OPENRIDE_MAX_SNAP_DISTANCE_M,
                                                &local_start)) {
        snprintf(status, status_size, "depart trop loin du reseau routier");
        return false;
    }

    OpenRideLoopRequest request = openride_loop_request_default();
    request.start = local_start;
    request.profile = profile;
    request.direction = direction;
    request.target_distance_m = target_distance_m;
    request.candidate_count = 6U;
    request.seed = seed;

    OpenRideLoopResult generated = {0};
    char loop_error[256] = {0};
    if (!openride_loop_generator_generate(graph,
                                          &request,
                                          &generated,
                                          loop_error,
                                          sizeof(loop_error))) {
        snprintf(status,
                 status_size,
                 "boucle impossible: %.180s",
                 loop_error[0] ? loop_error : "erreur inconnue");
        return false;
    }

    if (start_snap) *start_snap = local_start;
    if (stats) *stats = generated.stats;
    if (waypoints && generated.waypoint_count > 0U) {
        memcpy(waypoints,
               generated.waypoints,
               sizeof(generated.waypoints));
    }
    if (waypoint_count) *waypoint_count = generated.waypoint_count;

    *route = generated.route;
    memset(&generated.route, 0, sizeof(generated.route));
    snprintf(status,
             status_size,
             "boucle %.1f km | score %.0f | %u/%u candidats",
             route->distance_m / 1000.0,
             generated.stats.score,
             generated.stats.successful_candidates,
             generated.stats.attempted_candidates);
    openride_loop_result_destroy(&generated);
    return true;
}

static void draw_loop_waypoints(SDL_Renderer *renderer,
                                const OpenRideMapCamera *camera,
                                const OpenRideRoutePoint *waypoints,
                                uint32_t waypoint_count,
                                int viewport_width,
                                int viewport_height)
{
    if (!waypoints) return;
    for (uint32_t i = 0U; i < waypoint_count; ++i) {
        const OpenRidePointD p = openride_geo_to_screen(camera,
                                                        waypoints[i].lat,
                                                        waypoints[i].lon,
                                                        viewport_width,
                                                        viewport_height);
        SDL_SetRenderDrawColor(renderer, 250, 250, 248, 235);
        draw_filled_circle(renderer, (float)p.x, (float)p.y, 6.0f);
        SDL_SetRenderDrawColor(renderer, 190, 112, 35, 245);
        draw_filled_circle(renderer, (float)p.x, (float)p.y, 4.0f);
    }
}

static void draw_snap_connector(SDL_Renderer *renderer,
                                const OpenRideMapCamera *camera,
                                const OpenRideMapSelection *selection,
                                const OpenRideRoutingSnap *snap,
                                OpenRideSelectionMarker marker,
                                int viewport_width,
                                int viewport_height)
{
    if (!snap || snap->segment_id == OPENRIDE_ROUTING_SEGMENT_NONE) return;

    const OpenRideGeoPosition raw = marker == OPENRIDE_MARKER_START
        ? selection->start : selection->destination;
    const OpenRidePointD raw_point = openride_geo_to_screen(camera,
                                                             raw.lat,
                                                             raw.lon,
                                                             viewport_width,
                                                             viewport_height);
    const OpenRidePointD snapped = openride_geo_to_screen(camera,
                                                           snap->lat,
                                                           snap->lon,
                                                           viewport_width,
                                                           viewport_height);

    SDL_SetRenderDrawColor(renderer, 53, 63, 72, 190);
    draw_thick_line(renderer,
                    (float)raw_point.x,
                    (float)raw_point.y,
                    (float)snapped.x,
                    (float)snapped.y,
                    2);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
    draw_filled_circle(renderer, (float)snapped.x, (float)snapped.y, 5.0f);
    SDL_SetRenderDrawColor(renderer, 37, 101, 173, SDL_ALPHA_OPAQUE);
    draw_filled_circle(renderer, (float)snapped.x, (float)snapped.y, 3.0f);
}

static void draw_selection(SDL_Renderer *renderer,
                           const OpenRideMapCamera *camera,
                           const OpenRideMapSelection *selection,
                           bool draw_direct_line,
                           int viewport_width,
                           int viewport_height)
{
    OpenRidePointD start = {0};
    OpenRidePointD destination = {0};

    if (selection->has_start) {
        start = marker_screen_position(camera,
                                       selection,
                                       OPENRIDE_MARKER_START,
                                       viewport_width,
                                       viewport_height);
    }

    if (selection->has_destination) {
        destination = marker_screen_position(camera,
                                             selection,
                                             OPENRIDE_MARKER_DESTINATION,
                                             viewport_width,
                                             viewport_height);
    }

    if (draw_direct_line && selection->has_start && selection->has_destination) {
        SDL_SetRenderDrawColor(renderer, 248, 248, 246, 205);
        draw_thick_line(renderer,
                        (float)start.x,
                        (float)start.y,
                        (float)destination.x,
                        (float)destination.y,
                        7);
        SDL_SetRenderDrawColor(renderer, 41, 91, 139, 225);
        draw_thick_line(renderer,
                        (float)start.x,
                        (float)start.y,
                        (float)destination.x,
                        (float)destination.y,
                        3);
    }

    if (selection->has_start) {
        draw_marker(renderer, start, OPENRIDE_MARKER_START);
    }

    if (selection->has_destination) {
        draw_marker(renderer, destination, OPENRIDE_MARKER_DESTINATION);
    }
}

static SDL_Rect openride_render_safe_area(SDL_Renderer *renderer,
                                              int viewport_width,
                                              int viewport_height)
{
    SDL_Rect safe = {0, 0, viewport_width, viewport_height};
    SDL_Rect queried = {0};
    if (renderer
        && SDL_GetRenderSafeArea(renderer, &queried)
        && queried.w > 0
        && queried.h > 0) {
        safe = queried;
    }
    return safe;
}

static float openride_ui_scale(SDL_Renderer *renderer)
{
    float scale = 1.0f;
    SDL_Window *window = renderer ? SDL_GetRenderWindow(renderer) : NULL;
    if (window) {
        const float queried = SDL_GetWindowDisplayScale(window);
        if (queried > 0.0f) scale = queried;
    }
    if (scale < 1.0f) scale = 1.0f;
    if (scale > 3.0f) scale = 3.0f;
    return scale;
}

static OpenRideToolbarAction mobile_toolbar_hit_test(SDL_Renderer *renderer,
                                                       double x,
                                                       double y,
                                                       int viewport_width,
                                                       int viewport_height)
{
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return OPENRIDE_TOOLBAR_NONE;
    }
    const OpenRideToolbarAction action =
        openride_ui_toolbar_hit_test(&ui, x, y);
    openride_ui_end(&ui);
    return action;
}

static void format_duration(double seconds, char *text, size_t text_size)
{
    if (!text || text_size == 0U) return;
    if (!isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const unsigned total_minutes = (unsigned)llround(seconds / 60.0);
    const unsigned hours = total_minutes / 60U;
    const unsigned minutes = total_minutes % 60U;
    if (hours > 0U) {
        snprintf(text, text_size, "%u h %02u", hours, minutes);
    } else {
        snprintf(text, text_size, "%u min", minutes);
    }
}

static void draw_overlay(SDL_Renderer *renderer,
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
                         int viewport_width,
                         int viewport_height)
{
    const float panel_x = 10.0f;
    const float panel_y = 10.0f;
    const float panel_w = 500.0f;
    const float panel_h = 238.0f;
    SDL_FRect panel = {panel_x, panel_y, panel_w, panel_h};

    SDL_SetRenderDrawColor(renderer, 24, 28, 32, 218);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 75);
    SDL_RenderRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 247, 248, 249, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(renderer, panel_x + 12.0f, panel_y + 10.0f, "OpenRide v0.23");

    SDL_SetRenderDrawColor(renderer, 174, 181, 188, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugTextFormat(renderer,
                              panel_x + 12.0f,
                              panel_y + 25.0f,
                              "centre %.5f %.5f  |  z %.1f  |  %s",
                              camera->center_lat,
                              camera->center_lon,
                              camera->zoom,
                              scalable_map ? openride_map_style_name(map_style) : "raster offline");

    if (selection->has_start) {
        SDL_FRect chip = {panel_x + 12.0f, panel_y + 44.0f, 10.0f, 10.0f};
        SDL_SetRenderDrawColor(renderer, 42, 157, 84, SDL_ALPHA_OPAQUE);
        SDL_RenderFillRect(renderer, &chip);
        SDL_SetRenderDrawColor(renderer, 238, 241, 243, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugTextFormat(renderer,
                                  panel_x + 30.0f,
                                  panel_y + 45.0f,
                                  "Depart       %.5f  %.5f",
                                  selection->start.lat,
                                  selection->start.lon);
    } else {
        SDL_SetRenderDrawColor(renderer, 238, 241, 243, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugText(renderer,
                            panel_x + 12.0f,
                            panel_y + 45.0f,
                            "Clique sur la carte pour choisir le depart");
    }

    if (selection->has_destination) {
        SDL_FRect chip = {panel_x + 12.0f, panel_y + 60.0f, 10.0f, 10.0f};
        SDL_SetRenderDrawColor(renderer, 214, 66, 57, SDL_ALPHA_OPAQUE);
        SDL_RenderFillRect(renderer, &chip);
        SDL_SetRenderDrawColor(renderer, 238, 241, 243, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugTextFormat(renderer,
                                  panel_x + 30.0f,
                                  panel_y + 61.0f,
                                  "Destination  %.5f  %.5f",
                                  selection->destination.lat,
                                  selection->destination.lon);
    } else if (selection->has_start) {
        SDL_SetRenderDrawColor(renderer, 238, 241, 243, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugText(renderer,
                            panel_x + 12.0f,
                            panel_y + 61.0f,
                            loop_active
                                ? "Boucle generee depuis ce depart"
                                : "Clique destination ou B pour generer une boucle");
    }

    SDL_SetRenderDrawColor(renderer,
                           graph_loaded ? 116 : 226,
                           graph_loaded ? 188 : 158,
                           graph_loaded ? 118 : 74,
                           SDL_ALPHA_OPAQUE);
    SDL_RenderDebugTextFormat(renderer,
                              panel_x + 12.0f,
                              panel_y + 79.0f,
                              "Routage: %s  |  profil: %s",
                              route_status ? route_status : "-",
                              openride_routing_profile_name(profile));

    if (route_valid && loop_active && loop_stats) {
        SDL_SetRenderDrawColor(renderer, 224, 177, 112, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugTextFormat(renderer,
                                  panel_x + 12.0f,
                                  panel_y + 94.0f,
                                  "Boucle: cible %.0f km | %s | score %.0f | repetition %.0f%%",
                                  loop_target_distance_m / 1000.0,
                                  openride_loop_direction_name(loop_direction),
                                  loop_stats->score,
                                  loop_stats->overlap_ratio * 100.0);
    } else if (route_valid && gpx_navigation) {
        SDL_SetRenderDrawColor(renderer, 190, 142, 214, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugTextFormat(renderer,
                                  panel_x + 12.0f,
                                  panel_y + 94.0f,
                                  "navigation sur trace GPX | %.1f km",
                                  route ? route->distance_m / 1000.0 : 0.0);
    } else if (route_valid) {
        SDL_SetRenderDrawColor(renderer, 150, 181, 210, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugTextFormat(renderer,
                                  panel_x + 12.0f,
                                  panel_y + 94.0f,
                                  "accroche segment: depart %.1f m | arrivee %.1f m",
                                  start_snap ? start_snap->distance_m : 0.0,
                                  destination_snap ? destination_snap->distance_m : 0.0);
    } else {
        SDL_SetRenderDrawColor(renderer, 157, 166, 174, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugText(renderer,
                            panel_x + 12.0f,
                            panel_y + 94.0f,
                            "1 rapide | 2 balade | 3 trail");
    }

    SDL_SetRenderDrawColor(renderer, 157, 166, 174, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugTextFormat(renderer,
                              panel_x + 12.0f,
                              panel_y + 110.0f,
                              "B: generer boucle | +/-: %.0f km | O: direction %s",
                              loop_target_distance_m / 1000.0,
                              openride_loop_direction_name(loop_direction));

    SDL_RenderDebugText(renderer,
                        panel_x + 12.0f,
                        panel_y + 126.0f,
                        "M: style carte | 1 rapide | 2 balade | 3 trail");

    SDL_RenderDebugText(renderer,
                        panel_x + 12.0f,
                        panel_y + 142.0f,
                        "S: GPS | F: suivi | A: recalcul auto | X: ecart test | R: manuel");

    SDL_RenderDebugText(renderer,
                        panel_x + 12.0f,
                        panel_y + 158.0f,
                        "glisser: deplacer | clic droit: supprimer | C: effacer");

    SDL_SetRenderDrawColor(renderer,
                           gpx_loaded ? 190 : 157,
                           gpx_loaded ? 142 : 166,
                           gpx_loaded ? 214 : 174,
                           SDL_ALPHA_OPAQUE);
    SDL_RenderDebugTextFormat(renderer,
                              panel_x + 12.0f,
                              panel_y + 174.0f,
                              "GPX: %s | track %u | route %u | wpt %u%s",
                              gpx_loaded ? "charge" : "aucun",
                              gpx_document ? gpx_document->track_points.count : 0U,
                              gpx_document ? gpx_document->route_points.count : 0U,
                              gpx_document ? gpx_document->waypoints.count : 0U,
                              gpx_recording ? " | ENREG" : "");

    SDL_SetRenderDrawColor(renderer, 157, 166, 174, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(renderer,
                        panel_x + 12.0f,
                        panel_y + 190.0f,
                        "I: importer GPX | N: naviguer GPX | E: exporter | G: enregistrer");

    SDL_RenderDebugText(renderer,
                        panel_x + 12.0f,
                        panel_y + 206.0f,
                        "/: recherche hors ligne");

    if (route_valid || (selection->has_start && selection->has_destination)) {
        double distance_m = selection->has_start && selection->has_destination
            ? openride_geo_distance_m(selection->start.lat,
                                      selection->start.lon,
                                      selection->destination.lat,
                                      selection->destination.lon)
            : 0.0;
        char distance_text[32];
        char duration_text[32] = {0};
        const char *title = "DISTANCE DIRECTE";

        if (route_valid && route) {
            distance_m = route->distance_m;
            title = loop_active ? "BOUCLE HORS LIGNE" : "ITINERAIRE HORS LIGNE";
            format_duration(route->estimated_time_s, duration_text, sizeof(duration_text));
        }

        if (distance_m >= 1000.0) {
            snprintf(distance_text, sizeof(distance_text), "%.1f km", distance_m / 1000.0);
        } else {
            snprintf(distance_text, sizeof(distance_text), "%.0f m", distance_m);
        }

        const float distance_w = 230.0f;
        const float distance_h = route_valid ? 82.0f : 66.0f;
        const float distance_x = viewport_width >= 760
            ? (float)viewport_width - distance_w - 10.0f
            : 10.0f;
        const float distance_y = viewport_width >= 760
            ? 10.0f
            : panel_y + panel_h + 8.0f;
        SDL_FRect distance_panel = {
            distance_x,
            distance_y,
            distance_w,
            distance_h
        };

        SDL_SetRenderDrawColor(renderer, 24, 28, 32, 218);
        SDL_RenderFillRect(renderer, &distance_panel);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 75);
        SDL_RenderRect(renderer, &distance_panel);

        SDL_SetRenderDrawColor(renderer, 174, 181, 188, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugText(renderer,
                            distance_x + 14.0f,
                            distance_y + 10.0f,
                            title);
        SDL_SetRenderDrawColor(renderer, 247, 248, 249, SDL_ALPHA_OPAQUE);
        draw_scaled_text(renderer,
                         distance_x + 14.0f,
                         distance_y + 30.0f,
                         1.75f,
                         distance_text);
        if (route_valid) {
            SDL_SetRenderDrawColor(renderer, 174, 181, 188, SDL_ALPHA_OPAQUE);
            SDL_RenderDebugText(renderer,
                                distance_x + 14.0f,
                                distance_y + 64.0f,
                                duration_text);
        }
    }

    if (metadata->attribution[0] != '\0' && viewport_height > 28) {
        size_t text_len = strlen(metadata->attribution);
        if (text_len > 90U) text_len = 90U;
        const float backing_width = (float)(text_len * 8U + 12U);
        SDL_FRect attribution_panel = {
            8.0f,
            (float)viewport_height - 24.0f,
            backing_width,
            16.0f
        };
        SDL_SetRenderDrawColor(renderer, 249, 249, 247, 210);
        SDL_RenderFillRect(renderer, &attribution_panel);
        SDL_SetRenderDrawColor(renderer, 65, 68, 70, 255);
        SDL_RenderDebugText(renderer,
                            14.0f,
                            (float)viewport_height - 20.0f,
                            metadata->attribution);
    }
}

static void utf8_backspace(char *text)
{
    if (!text || text[0] == '\0') return;
    size_t length = strlen(text);
    do {
        --length;
    } while (length > 0U && (((unsigned char)text[length] & 0xC0U) == 0x80U));
    text[length] = '\0';
}

static bool refresh_place_search(OpenRidePlaceWorld *world,
                                 const char *query,
                                 OpenRidePlaceSearchResult *results,
                                 uint32_t *result_count,
                                 uint32_t *selected_result,
                                 char *status,
                                 size_t status_size)
{
    char error[192] = {0};
    uint32_t count = 0U;
    const bool ok = openride_place_world_search(world,
                                                 query,
                                                 results,
                                                 OPENRIDE_SEARCH_MAX_RESULTS,
                                                 &count,
                                                 error,
                                                 sizeof(error));
    if (!ok) {
        if (status && status_size > 0U) {
            snprintf(status,
                     status_size,
                     "recherche impossible: %.150s",
                     error[0] ? error : "erreur inconnue");
        }
        return false;
    }
    if (result_count) *result_count = count;
    if (selected_result && (*selected_result >= count || count == 0U)) *selected_result = 0U;
    return true;
}

static void draw_place_search_overlay(SDL_Renderer *renderer,
                                      bool active,
                                      bool available,
                                      const char *title,
                                      const char *query,
                                      const OpenRidePlaceSearchResult *results,
                                      uint32_t result_count,
                                      uint32_t selected_result,
                                      int viewport_width)
{
    if (!active) return;

#ifdef __ANDROID__
    int viewport_height = 0;
    int queried_width = viewport_width;
    SDL_GetCurrentRenderOutputSize(renderer, &queried_width, &viewport_height);
    if (queried_width > 0) viewport_width = queried_width;
    if (viewport_height <= 0) return;

    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return;
    }

    uint32_t count = result_count;
    if (count > OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS) {
        count = OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS;
    }
    OpenRideUISearchOverlayState state = {
        .available = available,
        .title = title,
        .query = query,
        .count = count,
        .selected = selected_result
    };
    char secondary[OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS][96];
    for (uint32_t i = 0U; i < count; ++i) {
        const OpenRideRegionDefinition *result_region =
            results[i].region_id[0] != '\0'
                ? openride_region_find(results[i].region_id)
                : NULL;
        snprintf(secondary[i],
                 sizeof(secondary[i]),
                 "%s%s%s%s",
                 openride_place_kind_name(results[i].kind),
                 result_region ? " - " : "",
                 result_region ? result_region->name : "",
                 results[i].bundled_lite ? " - France" : "");
        state.items[i].name = results[i].name;
        state.items[i].secondary = secondary[i];
    }
    openride_ui_search_overlay_draw(&ui, &state);
    openride_ui_end(&ui);
    return;
#else
    const float w = 620.0f;
    const float x = viewport_width > (int)w ? ((float)viewport_width - w) * 0.5f : 8.0f;
    const float actual_w = viewport_width > (int)w ? w : (float)viewport_width - 16.0f;
    const float y = 16.0f;
    const float row_h = 24.0f;
    const float h = 72.0f + row_h * (float)(result_count > 0U ? result_count : 1U);
    SDL_FRect panel = {x, y, actual_w, h};

    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 242);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 90);
    SDL_RenderRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 247, 248, 249, 255);
    SDL_RenderDebugText(renderer,
                        x + 14.0f,
                        y + 12.0f,
                        title && title[0] ? title : "RECHERCHER UN LIEU");
    SDL_SetRenderDrawColor(renderer, 173, 183, 191, 255);
    SDL_RenderDebugText(renderer, x + 14.0f, y + 29.0f, "Entrer: centrer | Fleches: choisir | Esc: fermer");

    SDL_FRect query_box = {x + 12.0f, y + 46.0f, actual_w - 24.0f, 20.0f};
    SDL_SetRenderDrawColor(renderer, 38, 44, 50, 255);
    SDL_RenderFillRect(renderer, &query_box);
    SDL_SetRenderDrawColor(renderer, 235, 237, 239, 255);
    SDL_RenderDebugTextFormat(renderer,
                              x + 18.0f,
                              y + 52.0f,
                              "> %s%s",
                              query ? query : "",
                              active ? "_" : "");

    if (!available) {
        SDL_SetRenderDrawColor(renderer, 226, 158, 74, 255);
        SDL_RenderDebugText(renderer,
                            x + 14.0f,
                            y + 78.0f,
                            "Aucun index de recherche regional installe");
        return;
    }

    if (result_count == 0U) {
        SDL_SetRenderDrawColor(renderer, 157, 166, 174, 255);
        SDL_RenderDebugText(renderer,
                            x + 14.0f,
                            y + 78.0f,
                            (query && strlen(query) >= 2U)
                                ? "Aucun resultat"
                                : "Tape au moins 2 caracteres");
        return;
    }

    for (uint32_t i = 0U; i < result_count; ++i) {
        const float row_y = y + 73.0f + row_h * (float)i;
        if (i == selected_result) {
            SDL_FRect highlight = {x + 10.0f, row_y - 2.0f, actual_w - 20.0f, row_h - 2.0f};
            SDL_SetRenderDrawColor(renderer, 42, 82, 112, 220);
            SDL_RenderFillRect(renderer, &highlight);
        }
        const OpenRideRegionDefinition *result_region =
            results[i].region_id[0] != '\0'
                ? openride_region_find(results[i].region_id)
                : NULL;
        SDL_SetRenderDrawColor(renderer, 238, 241, 243, 255);
        SDL_RenderDebugTextFormat(renderer,
                                  x + 16.0f,
                                  row_y + 3.0f,
                                  "%c %-30.30s  [%s%s%s]",
                                  i == selected_result ? '>' : ' ',
                                  results[i].name,
                                  openride_place_kind_name(results[i].kind),
                                  result_region ? " - " : "",
                                  result_region ? result_region->name : "");
    }
#endif
}


static int place_search_result_at(SDL_Renderer *renderer,
                                  double x,
                                  double y,
                                  int viewport_width,
                                  int viewport_height,
                                  uint32_t result_count)
{
    if (result_count == 0U) return -1;

#ifdef __ANDROID__
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return -1;
    }
    const int result = openride_ui_search_overlay_result_at(
        &ui,
        result_count,
        x,
        y);
    openride_ui_end(&ui);
    return result;
#else
    (void)renderer;
    (void)viewport_height;
    const double panel_width = viewport_width > 620 ? 620.0 : (double)viewport_width - 16.0;
    const double panel_x = viewport_width > 620 ? ((double)viewport_width - panel_width) * 0.5 : 8.0;
    const double row_top = 87.0;
    const double row_height = 24.0;
    if (x < panel_x + 10.0 || x > panel_x + panel_width - 10.0 || y < row_top) return -1;
    const int index = (int)((y - row_top) / row_height);
    return index >= 0 && (uint32_t)index < result_count ? index : -1;
#endif
}

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
    OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS,
    OPENRIDE_APP_PANEL_FAVORITES,
    OPENRIDE_APP_PANEL_HISTORY,
    OPENRIDE_APP_PANEL_REGIONS,
    OPENRIDE_APP_PANEL_SETTINGS
} OpenRideAppPanel;

static OpenRideAppPanel app_panel_main_at(double x, double y, int viewport_width)
{
    const double w = 580.0;
    const double panel_x = viewport_width > (int)w ? ((double)viewport_width - w) * 0.5 : 8.0;
    const double panel_w = viewport_width > (int)w ? w : (double)viewport_width - 16.0;
    if (x < panel_x || x > panel_x + panel_w) return OPENRIDE_APP_PANEL_NONE;
    const double first_y = 18.0 + 42.0;
    const double row_h = 28.0;
    if (y < first_y || y >= first_y + row_h * 5.0) return OPENRIDE_APP_PANEL_NONE;
    const int row = (int)((y - first_y) / row_h);
    switch (row) {
        case 0: return OPENRIDE_APP_PANEL_NONE; /* search is handled specially */
        case 1: return OPENRIDE_APP_PANEL_FAVORITES;
        case 2: return OPENRIDE_APP_PANEL_HISTORY;
        case 3: return OPENRIDE_APP_PANEL_REGIONS;
        case 4: return OPENRIDE_APP_PANEL_SETTINGS;
        default: return OPENRIDE_APP_PANEL_NONE;
    }
}

static bool app_panel_main_search_at(double x, double y, int viewport_width)
{
    const double w = 580.0;
    const double panel_x = viewport_width > (int)w ? ((double)viewport_width - w) * 0.5 : 8.0;
    const double panel_w = viewport_width > (int)w ? w : (double)viewport_width - 16.0;
    return x >= panel_x && x <= panel_x + panel_w && y >= 60.0 && y < 88.0;
}

static int app_panel_place_at(double x,
                              double y,
                              int viewport_width,
                              uint32_t count)
{
    if (count == 0U) return -1;
    const double w = 580.0;
    const double panel_x = viewport_width > (int)w ? ((double)viewport_width - w) * 0.5 : 8.0;
    const double panel_w = viewport_width > (int)w ? w : (double)viewport_width - 16.0;
    const double row_top = 18.0 + 45.0;
    const double row_h = 24.0;
    if (x < panel_x + 10.0 || x > panel_x + panel_w - 10.0 || y < row_top) return -1;
    const int index = (int)((y - row_top) / row_h);
    return index >= 0 && (uint32_t)index < count ? index : -1;
}

static void set_destination_from_place(OpenRideMapSelection *selection,
                                       const OpenRideGPSSample *gps,
                                       bool gps_valid,
                                       double lat,
                                       double lon,
                                       const char *name,
                                       bool *route_dirty,
                                       char *status,
                                       size_t status_size)
{
    if (!selection) return;
#ifdef __ANDROID__
    if (gps_valid && gps) {
        openride_map_selection_set(selection,
                                   OPENRIDE_MARKER_START,
                                   gps->lat,
                                   gps->lon);
    }
#else
    (void)gps;
    (void)gps_valid;
#endif
    openride_map_selection_set(selection, OPENRIDE_MARKER_DESTINATION, lat, lon);
    if (route_dirty) *route_dirty = openride_map_selection_complete(selection);
    if (status && status_size > 0U) {
        snprintf(status,
                 status_size,
                 openride_map_selection_complete(selection)
                     ? "destination %.120s | calcul itineraire"
                     : "destination %.120s | choisis le depart",
                 name && name[0] ? name : "selectionnee");
    }
}

static void refresh_stored_places(OpenRideAppStorage *storage,
                                  bool favorites,
                                  OpenRideStoredPlace *places,
                                  uint32_t *count)
{
    char error[160] = {0};
    if (!storage || !places || !count) return;
    *count = 0U;
    if (favorites) {
        openride_app_storage_list_favorites(storage,
                                            places,
                                            OPENRIDE_APP_LIST_MAX,
                                            count,
                                            error,
                                            sizeof(error));
    } else {
        openride_app_storage_list_history(storage,
                                          places,
                                          OPENRIDE_APP_LIST_MAX,
                                          count,
                                          error,
                                          sizeof(error));
    }
}

static void open_place_search(SDL_Window *window,
                              OpenRidePlaceWorld *place_world,
                              bool *active,
                              char *query,
                              uint32_t *result_count,
                              uint32_t *selected,
                              char *status,
                              size_t status_size)
{
    if (!active || !query || !result_count || !selected) return;
    if (!place_world) {
        snprintf(status, status_size,
                 "recherche hors ligne indisponible");
        return;
    }
    *active = true;
    query[0] = '\0';
    *result_count = 0U;
    *selected = 0U;
    /* Text input is layout-aware: important for '/' on AZERTY keyboards. */
    SDL_StartTextInput(window);
}

#ifdef __ANDROID__
typedef enum OpenRideMobilePanelAction {
    OPENRIDE_MOBILE_PANEL_NONE = 0,
    OPENRIDE_MOBILE_PANEL_CLOSE,
    OPENRIDE_MOBILE_PANEL_BACK,
    OPENRIDE_MOBILE_PANEL_SEARCH,
    OPENRIDE_MOBILE_PANEL_ROUTE_GPS_START,
    OPENRIDE_MOBILE_PANEL_ROUTE_SEARCH_START,
    OPENRIDE_MOBILE_PANEL_ROUTE_MAP_START,
    OPENRIDE_MOBILE_PANEL_ROUTE_SEARCH_DESTINATION,
    OPENRIDE_MOBILE_PANEL_ROUTE_MAP_DESTINATION,
    OPENRIDE_MOBILE_PANEL_ROUTE_CALCULATE,
    OPENRIDE_MOBILE_PANEL_ROUTE_DOWNLOAD_REQUIRED,
    OPENRIDE_MOBILE_PANEL_ROUTE_USE_INSTALLED,
    OPENRIDE_MOBILE_PANEL_FAVORITES,
    OPENRIDE_MOBILE_PANEL_HISTORY,
    OPENRIDE_MOBILE_PANEL_REGIONS,
    OPENRIDE_MOBILE_PANEL_SETTINGS,
    OPENRIDE_MOBILE_PANEL_PLACE,
    OPENRIDE_MOBILE_PANEL_REGION_PREVIOUS,
    OPENRIDE_MOBILE_PANEL_REGION_NEXT,
    OPENRIDE_MOBILE_PANEL_REGION_INSTALL,
    OPENRIDE_MOBILE_PANEL_REGION_REMOVE,
    OPENRIDE_MOBILE_PANEL_SETTINGS_STYLE,
    OPENRIDE_MOBILE_PANEL_SETTINGS_PROFILE,
    OPENRIDE_MOBILE_PANEL_SETTINGS_FOLLOW,
    OPENRIDE_MOBILE_PANEL_SETTINGS_REROUTE,
    OPENRIDE_MOBILE_PANEL_SETTINGS_VOICE,
    OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_SIMULATION,
    OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_DEVIATION,
    OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_SPEED,
    OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_MISSED_TURN,
    OPENRIDE_MOBILE_PANEL_MAP_ZOOM_TEST
} OpenRideMobilePanelAction;

typedef struct OpenRideMobilePanelHit {
    OpenRideMobilePanelAction action;
    int index;
} OpenRideMobilePanelHit;

static OpenRideMobilePanelHit mobile_main_menu_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height)
{
    OpenRideMobilePanelHit hit = {OPENRIDE_MOBILE_PANEL_NONE, -1};
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return hit;
    }

    switch (openride_ui_main_menu_hit_test(&ui, x, y)) {
        case OPENRIDE_UI_MAIN_MENU_SEARCH:
            hit.action = OPENRIDE_MOBILE_PANEL_SEARCH;
            break;
        case OPENRIDE_UI_MAIN_MENU_FAVORITES:
            hit.action = OPENRIDE_MOBILE_PANEL_FAVORITES;
            break;
        case OPENRIDE_UI_MAIN_MENU_HISTORY:
            hit.action = OPENRIDE_MOBILE_PANEL_HISTORY;
            break;
        case OPENRIDE_UI_MAIN_MENU_REGIONS:
            hit.action = OPENRIDE_MOBILE_PANEL_REGIONS;
            break;
        case OPENRIDE_UI_MAIN_MENU_SETTINGS:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS;
            break;
        case OPENRIDE_UI_MAIN_MENU_MAP_ZOOM_TEST:
            hit.action = OPENRIDE_MOBILE_PANEL_MAP_ZOOM_TEST;
            break;
        case OPENRIDE_UI_MAIN_MENU_CLOSE:
            hit.action = OPENRIDE_MOBILE_PANEL_CLOSE;
            break;
        case OPENRIDE_UI_MAIN_MENU_NONE:
        default:
            break;
    }
    openride_ui_end(&ui);
    return hit;
}

static OpenRideMobilePanelHit mobile_route_panel_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height)
{
    OpenRideMobilePanelHit hit = {OPENRIDE_MOBILE_PANEL_NONE, -1};
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return hit;
    }

    switch (openride_ui_route_panel_hit_test(&ui, x, y)) {
        case OPENRIDE_UI_ROUTE_PANEL_GPS_START:
            hit.action = OPENRIDE_MOBILE_PANEL_ROUTE_GPS_START;
            break;
        case OPENRIDE_UI_ROUTE_PANEL_SEARCH_START:
            hit.action = OPENRIDE_MOBILE_PANEL_ROUTE_SEARCH_START;
            break;
        case OPENRIDE_UI_ROUTE_PANEL_MAP_START:
            hit.action = OPENRIDE_MOBILE_PANEL_ROUTE_MAP_START;
            break;
        case OPENRIDE_UI_ROUTE_PANEL_SEARCH_DESTINATION:
            hit.action = OPENRIDE_MOBILE_PANEL_ROUTE_SEARCH_DESTINATION;
            break;
        case OPENRIDE_UI_ROUTE_PANEL_MAP_DESTINATION:
            hit.action = OPENRIDE_MOBILE_PANEL_ROUTE_MAP_DESTINATION;
            break;
        case OPENRIDE_UI_ROUTE_PANEL_CALCULATE:
            hit.action = OPENRIDE_MOBILE_PANEL_ROUTE_CALCULATE;
            break;
        case OPENRIDE_UI_ROUTE_PANEL_BACK:
            hit.action = OPENRIDE_MOBILE_PANEL_BACK;
            break;
        case OPENRIDE_UI_ROUTE_PANEL_NONE:
        default:
            break;
    }
    openride_ui_end(&ui);
    return hit;
}

static OpenRideMobilePanelHit mobile_route_downloads_panel_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height)
{
    OpenRideMobilePanelHit hit = {OPENRIDE_MOBILE_PANEL_NONE, -1};
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return hit;
    }

    switch (openride_ui_route_downloads_panel_hit_test(&ui, x, y)) {
        case OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_DOWNLOAD:
            hit.action = OPENRIDE_MOBILE_PANEL_ROUTE_DOWNLOAD_REQUIRED;
            break;
        case OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_USE_INSTALLED:
            hit.action = OPENRIDE_MOBILE_PANEL_ROUTE_USE_INSTALLED;
            break;
        case OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_BACK:
            hit.action = OPENRIDE_MOBILE_PANEL_BACK;
            break;
        case OPENRIDE_UI_ROUTE_DOWNLOADS_PANEL_NONE:
        default:
            break;
    }
    openride_ui_end(&ui);
    return hit;
}

static OpenRideMobilePanelHit mobile_settings_panel_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height)
{
    OpenRideMobilePanelHit hit = {OPENRIDE_MOBILE_PANEL_NONE, -1};
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return hit;
    }

    switch (openride_ui_settings_panel_hit_test(&ui, x, y)) {
        case OPENRIDE_UI_SETTINGS_PANEL_STYLE:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_STYLE;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_PROFILE:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_PROFILE;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_FOLLOW:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_FOLLOW;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_REROUTE:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_REROUTE;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_VOICE:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_VOICE;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_GPS_SIMULATION:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_SIMULATION;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_GPS_DEVIATION:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_DEVIATION;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_GPS_SPEED:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_SPEED;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_GPS_MISSED_TURN:
            hit.action = OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_MISSED_TURN;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_BACK:
            hit.action = OPENRIDE_MOBILE_PANEL_BACK;
            break;
        case OPENRIDE_UI_SETTINGS_PANEL_NONE:
        default:
            break;
    }
    openride_ui_end(&ui);
    return hit;
}

static OpenRideMobilePanelHit mobile_regions_panel_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height)
{
    OpenRideMobilePanelHit hit = {OPENRIDE_MOBILE_PANEL_NONE, -1};
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return hit;
    }

    switch (openride_ui_regions_panel_hit_test(&ui, x, y)) {
        case OPENRIDE_UI_REGIONS_PANEL_PREVIOUS:
            hit.action = OPENRIDE_MOBILE_PANEL_REGION_PREVIOUS;
            break;
        case OPENRIDE_UI_REGIONS_PANEL_NEXT:
            hit.action = OPENRIDE_MOBILE_PANEL_REGION_NEXT;
            break;
        case OPENRIDE_UI_REGIONS_PANEL_INSTALL:
            hit.action = OPENRIDE_MOBILE_PANEL_REGION_INSTALL;
            break;
        case OPENRIDE_UI_REGIONS_PANEL_REMOVE:
            hit.action = OPENRIDE_MOBILE_PANEL_REGION_REMOVE;
            break;
        case OPENRIDE_UI_REGIONS_PANEL_BACK:
            hit.action = OPENRIDE_MOBILE_PANEL_BACK;
            break;
        case OPENRIDE_UI_REGIONS_PANEL_NONE:
        default:
            break;
    }
    openride_ui_end(&ui);
    return hit;
}

static OpenRideMobilePanelHit mobile_places_panel_hit_test(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height,
    uint32_t item_count)
{
    OpenRideMobilePanelHit hit = {OPENRIDE_MOBILE_PANEL_NONE, -1};
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return hit;
    }

    const OpenRideUIPlacesPanelHit places_hit =
        openride_ui_places_panel_hit_test(&ui, item_count, x, y);
    if (places_hit.action == OPENRIDE_UI_PLACES_PANEL_PLACE) {
        hit.action = OPENRIDE_MOBILE_PANEL_PLACE;
        hit.index = places_hit.index;
    } else if (places_hit.action == OPENRIDE_UI_PLACES_PANEL_BACK) {
        hit.action = OPENRIDE_MOBILE_PANEL_BACK;
    }

    openride_ui_end(&ui);
    return hit;
}

static void draw_mobile_app_panel(SDL_Renderer *renderer,
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
                                  const OpenRideRouteDownloadPlan *route_download_plan_state,
                                  int viewport_width)
{
    /*
     * The state itself is owned by main(). Rendering only needs a read-only
     * snapshot. Keeping the existing dot syntax below also makes the UI block
     * independent of the pointer lifetime.
     */
    const OpenRideRouteDownloadPlan route_download_plan =
        route_download_plan_state
            ? *route_download_plan_state
            : (OpenRideRouteDownloadPlan){0};

    int width = viewport_width;
    int height = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
    if (width <= 0 || height <= 0) return;

    if (panel == OPENRIDE_APP_PANEL_MAIN) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            (void)openride_ui_main_menu_draw(&ui);
            openride_ui_end(&ui);
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_ROUTE) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            const OpenRideUIRoutePanelState state = {
                .has_start = selection && selection->has_start,
                .has_destination = selection && selection->has_destination,
                .gps_valid = gps_valid,
                .gps_accuracy_m = gps_accuracy_m
            };
            (void)openride_ui_route_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            OpenRideUIRouteDownloadsPanelState state = {
                .downloading = route_download_plan.downloading,
                .has_installed_alternative =
                    route_download_plan.has_installed_alternative,
                .count = route_download_plan.count,
                .current_index = route_download_plan.index,
                .progress = region_progress,
                .work_status = region_work_status
            };
            uint32_t count = route_download_plan.count;
            if (count > OPENRIDE_UI_ROUTE_DOWNLOADS_MAX_REGIONS) {
                count = OPENRIDE_UI_ROUTE_DOWNLOADS_MAX_REGIONS;
            }
            state.count = count;
            for (uint32_t i = 0U; i < count; ++i) {
                const OpenRideRegionDefinition *required =
                    openride_region_find(route_download_plan.region_ids[i]);
                state.region_names[i] = required
                    ? required->name
                    : route_download_plan.region_ids[i];
            }
            (void)openride_ui_route_downloads_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_SETTINGS) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            const OpenRideUISettingsPanelState state = {
                .map_style_name = openride_map_style_name(map_style),
                .routing_profile_name = openride_routing_profile_name(profile),
                .follow_gps = follow_gps,
                .auto_reroute = auto_reroute,
                .voice_enabled = voice_enabled,
                .simulated_gps_active = simulated_gps_active,
                .simulated_gps_deviation = simulated_gps_deviation,
                .simulated_gps_time_scale = simulated_gps_time_scale,
                .simulated_missed_turn_armed = simulated_missed_turn_armed,
                .simulated_missed_turn_active = simulated_missed_turn_active
            };
            (void)openride_ui_settings_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_REGIONS) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            const OpenRideUIRegionsPanelState state = {
                .region_name = region ? region->name : "Region",
                .region_is_active = region_is_active,
                .ormap_installed = region_status && region_status->ormap_installed,
                .routing_installed = region_status && region_status->routing_installed,
                .search_installed = region_status && region_status->search_installed,
                .source_pbf_present = region_status && region_status->source_pbf_present,
                .poly_present = region_status && region_status->poly_present,
                .ready = openride_region_status_ready(region_status),
                .total_size_mb = region_status ? region_status->total_size_mb : 0.0,
                .busy = region_busy,
                .progress = region_progress,
                .work_status = region_work_status
            };
            (void)openride_ui_regions_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_FAVORITES
        || panel == OPENRIDE_APP_PANEL_HISTORY) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            const bool favorites_panel =
                panel == OPENRIDE_APP_PANEL_FAVORITES;
            const OpenRideStoredPlace *items =
                favorites_panel ? favorites : history;
            uint32_t count = favorites_panel ? favorite_count : history_count;
            if (count > OPENRIDE_UI_PLACES_PANEL_MAX_ITEMS) {
                count = OPENRIDE_UI_PLACES_PANEL_MAX_ITEMS;
            }

            OpenRideUIPlacesPanelState state = {
                .mode = favorites_panel
                    ? OPENRIDE_UI_PLACES_PANEL_FAVORITES
                    : OPENRIDE_UI_PLACES_PANEL_HISTORY,
                .count = count,
                .selected = selected
            };
            for (uint32_t i = 0U; i < count; ++i) {
                state.items[i] = items[i].name;
            }
            (void)openride_ui_places_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

}
#endif

static int app_panel_region_action_at(double x, double y, int viewport_width)
{
    const double panel_w = viewport_width > 580 ? 580.0 : (double)viewport_width - 16.0;
    const double panel_x = viewport_width > 580 ? ((double)viewport_width - 580.0) * 0.5 : 8.0;
    const double panel_y = 18.0;
    if (x < panel_x || x > panel_x + panel_w) return 0;
    if (y >= panel_y + 228.0 && y <= panel_y + 270.0) return 1;
    if (y >= panel_y + 278.0 && y <= panel_y + 320.0) return 2;
    return 0;
}

static void draw_app_panel(SDL_Renderer *renderer,
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
                           const OpenRideRouteDownloadPlan *route_download_plan_state,
                           int viewport_width)
{
    if (panel == OPENRIDE_APP_PANEL_NONE) return;
#ifdef __ANDROID__
    draw_mobile_app_panel(renderer,
                          panel,
                          favorites,
                          favorite_count,
                          history,
                          history_count,
                          selected,
                          map_style,
                          profile,
                          follow_gps,
                          auto_reroute,
                          voice_enabled,
                          simulated_gps_active,
                          simulated_gps_deviation,
                          simulated_gps_time_scale,
                          simulated_missed_turn_armed,
                          simulated_missed_turn_active,
                          region,
                          region_status,
                          region_is_active,
                          region_busy,
                          region_progress,
                          region_work_status,
                          selection,
                          gps_valid,
                          gps_accuracy_m,
                          route_download_plan_state,
                          viewport_width);
    return;
#endif
    (void)simulated_gps_active;
    (void)simulated_gps_deviation;
    (void)simulated_gps_time_scale;
    (void)simulated_missed_turn_armed;
    (void)simulated_missed_turn_active;
    const float w = 580.0f;
    const float x = viewport_width > (int)w ? ((float)viewport_width - w) * 0.5f : 8.0f;
    const float actual_w = viewport_width > (int)w ? w : (float)viewport_width - 16.0f;
    const float y = 18.0f;
    SDL_FRect box = {x, y, actual_w, 360.0f};
    SDL_SetRenderDrawColor(renderer, 18, 22, 26, 244);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 90);
    SDL_RenderRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 245, 247, 248, 255);

    if (panel == OPENRIDE_APP_PANEL_MAIN) {
        SDL_RenderDebugText(renderer, x + 18, y + 16, "OPENRIDE - MENU");
        SDL_RenderDebugText(renderer, x + 18, y + 52, "R  Recherche hors ligne");
        SDL_RenderDebugText(renderer, x + 18, y + 80, "F  Favoris");
        SDL_RenderDebugText(renderer, x + 18, y + 108, "H  Historique");
        SDL_RenderDebugText(renderer, x + 18, y + 136, "C  Cartes / donnees installees");
        SDL_RenderDebugText(renderer, x + 18, y + 164, "P  Parametres");
        SDL_RenderDebugText(renderer, x + 18, y + 192, "Z  Test zoom carte [DEV]");
        SDL_SetRenderDrawColor(renderer, 165, 174, 181, 255);
        SDL_RenderDebugText(renderer, x + 18, y + 322, "Tab/Esc: fermer | V: ajouter la position en favori");
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_ROUTE) {
        SDL_RenderDebugText(renderer, x + 18, y + 16, "ITINERAIRE");
        SDL_RenderDebugTextFormat(renderer,
                                  x + 18,
                                  y + 58,
                                  "Depart : %s",
                                  selection && selection->has_start
                                      ? "selectionne"
                                      : "a choisir");
        SDL_RenderDebugTextFormat(renderer,
                                  x + 18,
                                  y + 88,
                                  "Arrivee : %s",
                                  selection && selection->has_destination
                                      ? "selectionnee"
                                      : "a choisir");
        SDL_RenderDebugText(renderer,
                            x + 18,
                            y + 132,
                            "D : rechercher le depart");
        SDL_RenderDebugText(renderer,
                            x + 18,
                            y + 160,
                            "A : rechercher l'arrivee");
        SDL_RenderDebugText(renderer,
                            x + 18,
                            y + 188,
                            "Entree : calculer");
        SDL_SetRenderDrawColor(renderer, 165, 174, 181, 255);
        SDL_RenderDebugText(renderer,
                            x + 18,
                            y + 322,
                            "Esc: retour | placement sur carte toujours disponible");
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS) {
        SDL_RenderDebugText(renderer, x + 18, y + 16, "CARTES REQUISES");
        SDL_RenderDebugText(renderer,
                            x + 18,
                            y + 58,
                            "Installe les regions requises depuis Android.");
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_FAVORITES || panel == OPENRIDE_APP_PANEL_HISTORY) {
        const bool fav = panel == OPENRIDE_APP_PANEL_FAVORITES;
        const OpenRideStoredPlace *items = fav ? favorites : history;
        const uint32_t count = fav ? favorite_count : history_count;
        SDL_RenderDebugText(renderer, x + 18, y + 16, fav ? "FAVORIS" : "HISTORIQUE");
        if (count == 0U) {
            SDL_SetRenderDrawColor(renderer, 165, 174, 181, 255);
            SDL_RenderDebugText(renderer, x + 18, y + 54, fav ? "Aucun favori" : "Historique vide");
        }
        for (uint32_t i = 0U; i < count; ++i) {
            const float ry = y + 48.0f + 24.0f * (float)i;
            if (i == selected) {
                SDL_FRect hi = {x + 12, ry - 3, actual_w - 24, 21};
                SDL_SetRenderDrawColor(renderer, 42, 82, 112, 220);
                SDL_RenderFillRect(renderer, &hi);
            }
            SDL_SetRenderDrawColor(renderer, 238, 241, 243, 255);
            SDL_RenderDebugTextFormat(renderer, x + 18, ry,
                                      "%c %-38.38s  %.5f, %.5f",
                                      i == selected ? '>' : ' ', items[i].name,
                                      items[i].lat, items[i].lon);
        }
        SDL_SetRenderDrawColor(renderer, 165, 174, 181, 255);
        SDL_RenderDebugText(renderer, x + 18, y + 322,
                            fav ? "Entree: centrer | Suppr: retirer | Esc: retour"
                                : "Entree: centrer | Esc: retour");
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_REGIONS) {
        SDL_RenderDebugText(renderer, x + 18, y + 16, "CARTES / DONNEES HORS LIGNE");
        SDL_RenderDebugTextFormat(renderer,
                                  x + 18,
                                  y + 48,
                                  "%s%s",
                                  region ? region->name : "Region",
                                  region_is_active ? "  [ACTIVE]" : "");
        if (region_status) {
            SDL_RenderDebugTextFormat(renderer, x + 34, y + 78, "Carte .ormap : %s  %.1f Mo",
                                      region_status->ormap_installed ? "installee" : "absente",
                                      region_status->ormap_installed ? openride_platform_file_size_mb(region_status->ormap_path) : 0.0);
            SDL_RenderDebugTextFormat(renderer, x + 34, y + 102, "Routage      : %s  %.1f Mo",
                                      region_status->routing_installed ? "installe" : "absent",
                                      region_status->routing_installed ? region_status->routing_size_mb : 0.0);
            SDL_RenderDebugTextFormat(renderer, x + 34, y + 126, "Recherche    : %s  %.1f Mo",
                                      region_status->search_installed ? "installee" : "absente",
                                      region_status->search_installed ? region_status->search_size_mb : 0.0);
            SDL_RenderDebugTextFormat(renderer, x + 34, y + 150, "Source PBF   : %s  %.1f Mo",
                                      region_status->source_pbf_present ? "presente" : "absente",
                                      region_status->source_pbf_present ? region_status->source_pbf_size_mb : 0.0);
            if (region_status->legacy_map_installed && !region_status->ormap_installed) {
                SDL_SetRenderDrawColor(renderer, 190, 198, 202, 255);
                SDL_RenderDebugText(renderer, x + 34, y + 176, "Carte Shortbread actuelle: transition v0.22");
            }
        }
        if (region_busy) {
            SDL_SetRenderDrawColor(renderer, 235, 238, 240, 255);
            SDL_RenderDebugTextFormat(renderer, x + 18, y + 214, "%s",
                                      region_work_status && region_work_status[0] ? region_work_status : "Preparation en cours...");
            if (region_progress >= 0.0) {
                SDL_RenderDebugTextFormat(renderer, x + 18, y + 240, "Progression: %.0f %%", region_progress * 100.0);
            }
        } else {
            SDL_FRect install = {x + 16, y + 228, actual_w - 32, 42};
            SDL_SetRenderDrawColor(renderer, 40, 98, 62, 230);
            SDL_RenderFillRect(renderer, &install);
            SDL_SetRenderDrawColor(renderer, 250, 252, 250, 255);
            SDL_RenderDebugText(renderer,
                                x + 30,
                                y + 242,
                                openride_region_status_ready(region_status)
                                    ? (region_is_active ? "D  REGION ACTIVE" : "D  UTILISER CETTE REGION")
                                    : (region_status && region_status->source_pbf_present
                                        ? "D  PREPARER DEPUIS LE PBF LOCAL"
                                        : "D  TELECHARGER OSM ET PREPARER"));
            SDL_FRect remove_box = {x + 16, y + 278, actual_w - 32, 42};
            SDL_SetRenderDrawColor(renderer, 96, 54, 54, 210);
            SDL_RenderFillRect(renderer, &remove_box);
            SDL_SetRenderDrawColor(renderer, 250, 245, 245, 255);
            SDL_RenderDebugText(renderer, x + 30, y + 292, "S  SUPPRIMER LES DONNEES GENEREES");
        }
        SDL_SetRenderDrawColor(renderer, 165, 174, 181, 255);
        if (!region_busy && region_work_status && region_work_status[0]) {
            SDL_RenderDebugTextFormat(renderer, x + 18, y + 328, "%.76s", region_work_status);
        } else {
            SDL_RenderDebugText(renderer, x + 18, y + 328, "Fleches gauche/droite: changer de region | D/Entree: installer/activer");
            SDL_RenderDebugText(renderer, x + 18, y + 344, "S: supprimer la region selectionnee | Esc: retour");
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_SETTINGS) {
        SDL_RenderDebugText(renderer, x + 18, y + 16, "PARAMETRES");
        SDL_RenderDebugTextFormat(renderer, x + 18, y + 58, "M  Style carte       : %s", openride_map_style_name(map_style));
        SDL_RenderDebugTextFormat(renderer, x + 18, y + 88, "1/2/3 Profil routage : %s", openride_routing_profile_name(profile));
        SDL_RenderDebugTextFormat(renderer, x + 18, y + 118, "F  Suivi GPS camera  : %s", follow_gps ? "oui" : "non");
        SDL_RenderDebugTextFormat(renderer, x + 18, y + 148, "A  Recalcul auto      : %s", auto_reroute ? "oui" : "non");
        SDL_RenderDebugTextFormat(renderer, x + 18, y + 178, "V  Guidage vocal      : %s", voice_enabled ? "oui" : "non");
        SDL_SetRenderDrawColor(renderer, 165, 174, 181, 255);
        SDL_RenderDebugText(renderer, x + 18, y + 322, "Les reglages sont sauvegardes automatiquement | Esc: retour");
    }
}

static void clear_navigation_session(OpenRideNavigationEngine *navigation,
                                     OpenRideGPSSimulator *simulator,
                                     OpenRideNavigationState *navigation_state,
                                     OpenRideGPSSample *gps_sample,
                                     bool *gps_sample_valid)
{
    openride_navigation_engine_clear_route(navigation);
    openride_gps_simulator_clear_route(simulator);
    if (navigation_state) memset(navigation_state, 0, sizeof(*navigation_state));
    if (gps_sample) memset(gps_sample, 0, sizeof(*gps_sample));
    if (gps_sample_valid) *gps_sample_valid = false;
}

static bool prepare_navigation_session(OpenRideNavigationEngine *navigation,
                                       OpenRideGPSSimulator *simulator,
                                       OpenRideNavigationInstructionList *instructions,
                                       const OpenRideRoutingGraph *graph,
                                       const OpenRideRoute *route,
                                       char *status,
                                       size_t status_size)
{
    char error[192] = {0};
    if (!openride_navigation_engine_set_route(navigation,
                                              route,
                                              error,
                                              sizeof(error))) {
        snprintf(status,
                 status_size,
                 "navigation indisponible: %.140s",
                 error[0] ? error : "geometrie invalide");
        return false;
    }
    if (!openride_gps_simulator_set_route(simulator,
                                          route,
                                          60.0,
                                          error,
                                          sizeof(error))) {
        openride_navigation_engine_clear_route(navigation);
        snprintf(status,
                 status_size,
                 "simulateur GPS indisponible: %.130s",
                 error[0] ? error : "geometrie invalide");
        return false;
    }
    openride_navigation_instructions_destroy(instructions);
    const OpenRideRoutingGraph *instruction_graph =
        route->nodes && route->node_count > 0U ? graph : NULL;
    if (!openride_navigation_instructions_build(instruction_graph,
                                                route,
                                                instructions,
                                                error,
                                                sizeof(error))) {
        openride_gps_simulator_clear_route(simulator);
        openride_navigation_engine_clear_route(navigation);
        snprintf(status,
                 status_size,
                 "instructions indisponibles: %.125s",
                 error[0] ? error : "geometrie invalide");
        return false;
    }
    return true;
}


static bool reroute_navigation_from_position(
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
    size_t status_size)
{
    if (!selection || !selection->has_destination || !route || !navigation
        || !simulator || !instructions || !session || !location_filter) {
        if (status && status_size) snprintf(status, status_size, "recalcul impossible");
        return false;
    }

    openride_navigation_engine_clear_route(navigation);
    openride_gps_simulator_clear_route(simulator);
    openride_navigation_instructions_destroy(instructions);
    openride_map_selection_set(selection, OPENRIDE_MARKER_START, lat, lon);

    const bool ok = recalculate_route(graph,
                                      graph_loaded,
                                      selection,
                                      profile,
                                      route,
                                      start_snap,
                                      destination_snap,
                                      status,
                                      status_size);
    if (!ok) return false;

    if (!prepare_navigation_session(navigation,
                                    simulator,
                                    instructions,
                                    graph,
                                    route,
                                    status,
                                    status_size)) {
        openride_route_destroy(route);
        return false;
    }

    openride_navigation_session_mark_rerouted(session);
    openride_location_filter_reset(location_filter);
    if (resume_simulator) openride_gps_simulator_start(simulator);
    return true;
}

static bool prepare_gpx_navigation(const OpenRideGPXDocument *document,
                                   OpenRideRoutingGraph *graph,
                                   OpenRideMapSelection *selection,
                                   OpenRideRoute *route,
                                   OpenRideNavigationEngine *navigation,
                                   OpenRideGPSSimulator *simulator,
                                   OpenRideNavigationInstructionList *instructions,
                                   OpenRideNavigationSession *session,
                                   OpenRideLocationFilter *location_filter,
                                   char *status,
                                   size_t status_size)
{
    if (!document || !selection || !route || !navigation || !simulator
        || !instructions || !session || !location_filter) {
        return false;
    }

    char error[192] = {0};
    openride_navigation_engine_clear_route(navigation);
    openride_gps_simulator_clear_route(simulator);
    openride_navigation_instructions_destroy(instructions);

    if (!openride_gpx_build_navigation_route(document,
                                             OPENRIDE_GPX_NAVIGATION_SPEED_KPH,
                                             route,
                                             error,
                                             sizeof(error))) {
        snprintf(status,
                 status_size,
                 "navigation GPX impossible: %.145s",
                 error[0] ? error : "trace invalide");
        return false;
    }

    openride_map_selection_clear(selection);
    openride_map_selection_set(selection,
                               OPENRIDE_MARKER_START,
                               route->geometry[0].lat,
                               route->geometry[0].lon);
    openride_map_selection_set(selection,
                               OPENRIDE_MARKER_DESTINATION,
                               route->geometry[route->geometry_count - 1U].lat,
                               route->geometry[route->geometry_count - 1U].lon);

    if (!prepare_navigation_session(navigation,
                                    simulator,
                                    instructions,
                                    graph,
                                    route,
                                    status,
                                    status_size)) {
        openride_route_destroy(route);
        return false;
    }

    openride_navigation_session_reset(session);
    openride_location_filter_reset(location_filter);
    snprintf(status,
             status_size,
             "trace GPX prete: %.1f km | S pour simuler",
             route->distance_m / 1000.0);
    return true;
}

static void draw_navigation_position(SDL_Renderer *renderer,
                                     const OpenRideMapCamera *camera,
                                     const OpenRideGPSSample *gps,
                                     const OpenRideNavigationState *navigation,
                                     int viewport_width,
                                     int viewport_height)
{
    if (!gps || !gps->valid) return;

    const OpenRidePointD raw = openride_geo_to_screen(camera,
                                                       gps->lat,
                                                       gps->lon,
                                                       viewport_width,
                                                       viewport_height);

    if (navigation && navigation->valid) {
        const OpenRidePointD matched = openride_geo_to_screen(camera,
                                                               navigation->matched_lat,
                                                               navigation->matched_lon,
                                                               viewport_width,
                                                               viewport_height);
        if (navigation->distance_from_route_m > 1.0) {
            SDL_SetRenderDrawColor(renderer, 54, 65, 76, 180);
            draw_thick_line(renderer,
                            (float)raw.x,
                            (float)raw.y,
                            (float)matched.x,
                            (float)matched.y,
                            2);
        }
        SDL_SetRenderDrawColor(renderer, 248, 248, 246, 245);
        draw_filled_circle(renderer, (float)matched.x, (float)matched.y, 5.0f);
        SDL_SetRenderDrawColor(renderer, 35, 112, 190, 245);
        draw_filled_circle(renderer, (float)matched.x, (float)matched.y, 3.0f);
    }

    SDL_SetRenderDrawColor(renderer, 248, 248, 246, 245);
    draw_filled_circle(renderer, (float)raw.x, (float)raw.y, 11.0f);
    SDL_SetRenderDrawColor(renderer, 25, 118, 210, 255);
    draw_filled_circle(renderer, (float)raw.x, (float)raw.y, 8.0f);

    const double heading_rad = (gps->heading_deg - camera->bearing_deg)
        * 0.01745329251994329577;
    const float hx = (float)raw.x + (float)(sin(heading_rad) * 18.0);
    const float hy = (float)raw.y - (float)(cos(heading_rad) * 18.0);
    SDL_SetRenderDrawColor(renderer, 248, 248, 246, 255);
    draw_thick_line(renderer, (float)raw.x, (float)raw.y, hx, hy, 5);
    SDL_SetRenderDrawColor(renderer, 25, 118, 210, 255);
    draw_thick_line(renderer, (float)raw.x, (float)raw.y, hx, hy, 3);
}

#ifdef __ANDROID__
static void draw_android_status_overlay(SDL_Renderer *renderer,
                                        const OpenRideMBTilesMetadata *metadata,
                                        const OpenRideRoute *route,
                                        bool route_valid,
                                        const char *route_status,
                                        OpenRideRoutingProfile profile,
                                        int viewport_width,
                                        int viewport_height)
{
    const SDL_Rect safe = openride_render_safe_area(renderer, viewport_width, viewport_height);
    const float ui_scale = openride_ui_scale(renderer);
    const float text_scale = ui_scale > 2.0f ? 2.0f : ui_scale;
    const float attribution_scale = ui_scale > 1.5f ? 1.5f : ui_scale;
    const float margin = 8.0f * ui_scale;
    const float panel_x = (float)safe.x + margin;
    const float panel_y = (float)safe.y + margin;
    float panel_w = (float)safe.w - margin * 2.0f;
    if (panel_w > 390.0f * ui_scale) panel_w = 390.0f * ui_scale;
    if (panel_w < 180.0f * ui_scale) panel_w = 180.0f * ui_scale;
    const float panel_h = (route_valid ? 64.0f : 48.0f) * ui_scale;
    SDL_FRect panel = {panel_x, panel_y, panel_w, panel_h};

    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 205);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 55);
    SDL_RenderRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 247, 248, 249, 255);
    draw_scaled_text(renderer,
                     panel_x + 10.0f * ui_scale,
                     panel_y + 7.0f * ui_scale,
                     text_scale,
                     "OpenRide");

    SDL_SetRenderDrawColor(renderer, 185, 194, 202, 255);
    char status_line[96];
    if (route_valid && route) {
        snprintf(status_line,
                 sizeof(status_line),
                 "%.1f km | %.0f min | %s",
                 route->distance_m / 1000.0,
                 route->estimated_time_s / 60.0,
                 openride_routing_profile_name(profile));
    } else {
        snprintf(status_line,
                 sizeof(status_line),
                 "%.30s",
                 route_status && route_status[0] ? route_status : "pret");
    }
    draw_scaled_text(renderer,
                     panel_x + 10.0f * ui_scale,
                     panel_y + 26.0f * ui_scale,
                     text_scale,
                     status_line);

    if (route_valid) {
        SDL_SetRenderDrawColor(renderer, 255, 214, 83, 255);
        draw_scaled_text(renderer,
                         panel_x + 10.0f * ui_scale,
                         panel_y + 44.0f * ui_scale,
                         text_scale,
                         "TRAJET PRET - touche DEMARRER");
    }

    /* Keep OSM attribution visible, but above the gesture/navigation area and toolbar. */
    if (metadata && metadata->attribution[0] != '\0') {
        const float toolbar_clearance = 90.0f * ui_scale;
        const float attribution_y = (float)(safe.y + safe.h) - toolbar_clearance;
        if (attribution_y > panel_y + panel_h + 8.0f * ui_scale) {
            SDL_FRect backing = {(float)safe.x + 8.0f * ui_scale,
                                 attribution_y,
                                 250.0f * ui_scale,
                                 14.0f * ui_scale};
            SDL_SetRenderDrawColor(renderer, 249, 249, 247, 210);
            SDL_RenderFillRect(renderer, &backing);
            SDL_SetRenderDrawColor(renderer, 65, 68, 70, 255);
            draw_scaled_text(renderer,
                             backing.x + 5.0f * ui_scale,
                             backing.y + 3.0f * ui_scale,
                             attribution_scale,
                             "(c) OpenStreetMap contributors | ODbL");
        }
    }
}
#endif

static void draw_navigation_overlay(SDL_Renderer *renderer,
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
                                    int viewport_height)
{
#ifdef __ANDROID__
    (void)simulator;
    (void)deviation_enabled;
#endif
    if (!gps_sample_valid || !navigation || !navigation->valid) return;

    const float x = 10.0f;
    const float y = 242.0f;
    const float w = 540.0f;
    const float h = 172.0f;
    if (viewport_height < (int)(y + h + 20.0f)) return;

    SDL_FRect panel = {x, y, w, h};
    SDL_SetRenderDrawColor(renderer, 24, 28, 32, 226);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 75);
    SDL_RenderRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 247, 248, 249, 255);
#ifdef __ANDROID__
    SDL_RenderDebugTextFormat(renderer,
                              x + 12.0f,
                              y + 10.0f,
                              "NAVIGATION GPS REEL%s",
                              gpx_navigation ? " | GPX" : " | ROUTAGE");
#else
    SDL_RenderDebugTextFormat(renderer,
                              x + 12.0f,
                              y + 10.0f,
                              "NAVIGATION GPS SIMULEE%s",
                              gpx_navigation ? " | GPX" : " | ROUTAGE");
#endif

    if (navigation->status == OPENRIDE_NAVIGATION_OFF_ROUTE) {
        SDL_SetRenderDrawColor(renderer, 230, 98, 75, 255);
    } else if (navigation->status == OPENRIDE_NAVIGATION_ARRIVED) {
        SDL_SetRenderDrawColor(renderer, 86, 190, 118, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 100, 190, 126, 255);
    }
#ifdef __ANDROID__
    SDL_RenderDebugTextFormat(renderer,
                              x + 12.0f,
                              y + 27.0f,
                              "%s | GPS actif",
                              openride_navigation_status_name(navigation->status));
#else
    SDL_RenderDebugTextFormat(renderer,
                              x + 12.0f,
                              y + 27.0f,
                              "%s%s",
                              openride_navigation_status_name(navigation->status),
                              simulator && simulator->active ? " | lecture" : " | pause");
#endif

    double instruction_distance_m = 0.0;
    const OpenRideNavigationInstruction *next_instruction =
        openride_navigation_instructions_next(instructions,
                                              navigation->traveled_m,
                                              &instruction_distance_m);
    if (next_instruction) {
        char maneuver_text[128];
        char distance_text[32];
        openride_navigation_instruction_text_fr(next_instruction,
                                                maneuver_text,
                                                sizeof(maneuver_text));
        openride_navigation_distance_text_fr(instruction_distance_m,
                                             distance_text,
                                             sizeof(distance_text));
        SDL_SetRenderDrawColor(renderer, 255, 213, 92, 255);
        if (next_instruction->maneuver == OPENRIDE_MANEUVER_ARRIVE) {
            SDL_RenderDebugTextFormat(renderer,
                                      x + 12.0f,
                                      y + 47.0f,
                                      "ARRIVEE dans %s",
                                      distance_text);
        } else {
            SDL_RenderDebugTextFormat(renderer,
                                      x + 12.0f,
                                      y + 47.0f,
                                      "Dans %s | %s",
                                      distance_text,
                                      maneuver_text);
        }
    }

    char eta_text[32] = "--";
    if (route && route->distance_m > 0.0 && route->estimated_time_s > 0.0) {
        const double ratio = clampd(navigation->remaining_m / route->distance_m, 0.0, 1.0);
        format_duration(route->estimated_time_s * ratio, eta_text, sizeof(eta_text));
    }

    SDL_SetRenderDrawColor(renderer, 220, 225, 229, 255);
    SDL_RenderDebugTextFormat(renderer,
                              x + 12.0f,
                              y + 68.0f,
                              "reste %.1f km | ETA %s | progression %.1f%%",
                              navigation->remaining_m / 1000.0,
                              eta_text,
                              navigation->progress_ratio * 100.0);
    SDL_RenderDebugTextFormat(renderer,
                              x + 12.0f,
                              y + 84.0f,
                              "ecart %.1f m | vitesse %.0f km/h",
                              navigation->distance_from_route_m,
                              navigation->speed_mps * 3.6);

    const OpenRideNavigationTripStats *stats = openride_navigation_session_stats(session);
    if (stats) {
        char elapsed_text[32];
        format_duration(stats->elapsed_s, elapsed_text, sizeof(elapsed_text));
        SDL_RenderDebugTextFormat(renderer,
                                  x + 12.0f,
                                  y + 103.0f,
                                  "trajet %.1f km | %s | moy %.0f | max %.0f km/h",
                                  stats->gps_distance_m / 1000.0,
                                  elapsed_text,
                                  stats->average_speed_mps * 3.6,
                                  stats->max_speed_mps * 3.6);
        SDL_RenderDebugTextFormat(renderer,
                                  x + 12.0f,
                                  y + 119.0f,
                                  "recalcul auto %s | recalculs %u",
                                  auto_reroute ? "ON" : "OFF",
                                  stats->reroute_count);
    }

    SDL_SetRenderDrawColor(renderer, 158, 168, 176, 255);
#ifdef __ANDROID__
    SDL_RenderDebugTextFormat(renderer,
                              x + 12.0f,
                              y + 145.0f,
                              "GPS tactile | F suivi %s | A auto %s | R manuel",
                              follow_gps ? "ON" : "OFF",
                              auto_reroute ? "ON" : "OFF");
#else
    SDL_RenderDebugTextFormat(renderer,
                              x + 12.0f,
                              y + 145.0f,
                              "S lecture | F suivi %s | A auto %s | X deviation %s | R manuel",
                              follow_gps ? "ON" : "OFF",
                              auto_reroute ? "ON" : "OFF",
                              deviation_enabled ? "ON" : "OFF");
#endif
}

static void draw_mobile_toolbar(SDL_Renderer *renderer, int viewport_width, int viewport_height, bool route_ready)
{
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return;
    }
    (void)openride_ui_toolbar_draw(&ui, route_ready);
    openride_ui_end(&ui);
}


typedef enum OpenRideDriveAction {
    OPENRIDE_DRIVE_ACTION_NONE = 0,
    OPENRIDE_DRIVE_ACTION_EXIT,
    OPENRIDE_DRIVE_ACTION_RECENTER,
    OPENRIDE_DRIVE_ACTION_ORIENTATION,
    OPENRIDE_DRIVE_ACTION_GPS
} OpenRideDriveAction;

static SDL_FRect drive_controls_bounds(SDL_Renderer *renderer,
                                       int viewport_width,
                                       int viewport_height)
{
    const SDL_Rect safe = openride_render_safe_area(renderer, viewport_width, viewport_height);
    const float ui_scale = openride_ui_scale(renderer);
    const float margin = 6.0f * ui_scale;
    const float height = 62.0f * ui_scale;
    SDL_FRect result = {
        (float)safe.x + margin,
        (float)(safe.y + safe.h) - margin - height,
        (float)safe.w - margin * 2.0f,
        height
    };
    return result;
}

static OpenRideDriveAction drive_controls_hit_test(SDL_Renderer *renderer,
                                                   double x,
                                                   double y,
                                                   int viewport_width,
                                                   int viewport_height)
{
    const SDL_FRect bar = drive_controls_bounds(renderer, viewport_width, viewport_height);
    if (x < bar.x || y < bar.y || x > bar.x + bar.w || y > bar.y + bar.h) {
        return OPENRIDE_DRIVE_ACTION_NONE;
    }
    const double item_w = (double)bar.w / 4.0;
    int index = (int)((x - (double)bar.x) / item_w);
    if (index < 0) index = 0;
    if (index > 3) index = 3;
    return (OpenRideDriveAction)(OPENRIDE_DRIVE_ACTION_EXIT + index);
}

static void format_arrival_clock(double remaining_seconds, char *text, size_t text_size)
{
    if (!text || text_size == 0U) return;
    if (!isfinite(remaining_seconds) || remaining_seconds < 0.0) {
        snprintf(text, text_size, "--:--");
        return;
    }
    time_t now = time(NULL);
    time_t arrival = now + (time_t)llround(remaining_seconds);
    struct tm *local = localtime(&arrival);
    if (!local || strftime(text, text_size, "%H:%M", local) == 0U) {
        snprintf(text, text_size, "--:--");
    }
}

static void draw_drive_controls(SDL_Renderer *renderer,
                                const OpenRideDriveModeState *drive,
                                int viewport_width,
                                int viewport_height)
{
    const SDL_FRect bar = drive_controls_bounds(renderer, viewport_width, viewport_height);
    const float ui_scale = openride_ui_scale(renderer);
    SDL_SetRenderDrawColor(renderer, 13, 17, 21, 238);
    SDL_RenderFillRect(renderer, &bar);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 65);
    SDL_RenderRect(renderer, &bar);

    const char *labels[4] = {
        "CARTE",
        "CENTRER",
        drive && drive->heading_up ? "NORD" : "CAP",
        "GPS"
    };
    const float item_w = bar.w / 4.0f;
    for (int i = 0; i < 4; ++i) {
        const float x = bar.x + (float)i * item_w;
        if (i > 0) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 38);
            SDL_RenderLine(renderer, x, bar.y + 10.0f * ui_scale,
                           x, bar.y + bar.h - 10.0f * ui_scale);
        }
        const float scale = ui_scale > 2.4f ? 2.4f : ui_scale;
        const float label_w = (float)strlen(labels[i]) * 8.0f * scale;
        const float label_h = 8.0f * scale;
        SDL_SetRenderDrawColor(renderer, 243, 245, 247, 255);
        draw_scaled_text(renderer,
                         x + (item_w - label_w) * 0.5f,
                         bar.y + (bar.h - label_h) * 0.5f,
                         scale,
                         labels[i]);
    }
}

static void draw_drive_maneuver_icon(SDL_Renderer *renderer,
                                     OpenRideManeuverType maneuver,
                                     float x,
                                     float y,
                                     float size,
                                     float thickness)
{
    if (!renderer || size <= 0.0f) return;
    if (thickness < 1.0f) thickness = 1.0f;

    const float cx = x + size * 0.5f;
    const float top = y + size * 0.12f;
    const float bottom = y + size * 0.88f;
    const float left = x + size * 0.18f;
    const float right = x + size * 0.82f;
    const float mid = y + size * 0.48f;

    SDL_SetRenderDrawColor(renderer, 255, 214, 83, 255);

    /* Draw the same primitive several times with tiny offsets to obtain a
       glove-readable icon without depending on an image/font asset. */
    for (int pass = 0; pass < (int)ceilf(thickness); ++pass) {
        const float o = (float)pass - thickness * 0.5f;
        switch (maneuver) {
            case OPENRIDE_MANEUVER_LEFT:
            case OPENRIDE_MANEUVER_SHARP_LEFT:
            case OPENRIDE_MANEUVER_SLIGHT_LEFT:
                SDL_RenderLine(renderer, cx + o, bottom, cx + o, mid);
                SDL_RenderLine(renderer, cx + o, mid, left, mid);
                SDL_RenderLine(renderer, left, mid, left + size * 0.18f, mid - size * 0.18f);
                SDL_RenderLine(renderer, left, mid, left + size * 0.18f, mid + size * 0.18f);
                break;
            case OPENRIDE_MANEUVER_RIGHT:
            case OPENRIDE_MANEUVER_SHARP_RIGHT:
            case OPENRIDE_MANEUVER_SLIGHT_RIGHT:
                SDL_RenderLine(renderer, cx + o, bottom, cx + o, mid);
                SDL_RenderLine(renderer, cx + o, mid, right, mid);
                SDL_RenderLine(renderer, right, mid, right - size * 0.18f, mid - size * 0.18f);
                SDL_RenderLine(renderer, right, mid, right - size * 0.18f, mid + size * 0.18f);
                break;
            case OPENRIDE_MANEUVER_UTURN:
                SDL_RenderLine(renderer, cx + size * 0.18f + o, bottom,
                               cx + size * 0.18f + o, mid);
                SDL_RenderLine(renderer, cx + size * 0.18f + o, mid,
                               cx - size * 0.17f, mid);
                SDL_RenderLine(renderer, cx - size * 0.17f, mid,
                               cx - size * 0.17f, top + size * 0.18f);
                SDL_RenderLine(renderer, cx - size * 0.17f, top + size * 0.18f,
                               cx + size * 0.02f, top + size * 0.35f);
                SDL_RenderLine(renderer, cx - size * 0.17f, top + size * 0.18f,
                               cx - size * 0.34f, top + size * 0.35f);
                break;
            case OPENRIDE_MANEUVER_ROUNDABOUT: {
                const float radius = size * 0.23f;
                const int segments = 18;
                const float tau = 6.28318530717958647692f;
                for (int i = 0; i < segments; ++i) {
                    const float a0 = (float)i * (tau / (float)segments);
                    const float a1 = (float)(i + 1) * (tau / (float)segments);
                    SDL_RenderLine(renderer,
                                   cx + cosf(a0) * radius,
                                   mid + sinf(a0) * radius + o,
                                   cx + cosf(a1) * radius,
                                   mid + sinf(a1) * radius + o);
                }
                SDL_RenderLine(renderer, cx + o, bottom, cx + o, mid + radius);
                SDL_RenderLine(renderer, cx + radius, mid + o, right, mid + o);
                SDL_RenderLine(renderer, right, mid + o,
                               right - size * 0.15f, mid - size * 0.14f);
                SDL_RenderLine(renderer, right, mid + o,
                               right - size * 0.15f, mid + size * 0.14f);
                break;
            }
            case OPENRIDE_MANEUVER_ARRIVE:
                SDL_RenderLine(renderer, left + o, bottom, left + o, top);
                SDL_RenderLine(renderer, left + o, top, right, top + size * 0.12f);
                SDL_RenderLine(renderer, right, top + size * 0.12f,
                               left, top + size * 0.28f);
                break;
            case OPENRIDE_MANEUVER_DEPART:
            case OPENRIDE_MANEUVER_CONTINUE:
            default:
                SDL_RenderLine(renderer, cx + o, bottom, cx + o, top);
                SDL_RenderLine(renderer, cx + o, top,
                               cx - size * 0.18f, top + size * 0.20f);
                SDL_RenderLine(renderer, cx + o, top,
                               cx + size * 0.18f, top + size * 0.20f);
                break;
        }
    }
}

static void draw_drive_mode_ui(SDL_Renderer *renderer,
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
                               int viewport_height)
{
    if (!renderer || !drive || !drive->active) return;

    const SDL_Rect safe = openride_render_safe_area(renderer, viewport_width, viewport_height);
    const float ui_scale = openride_ui_scale(renderer);
    const float margin = 6.0f * ui_scale;
    const float top_h = 86.0f * ui_scale;
    SDL_FRect top = {
        (float)safe.x + margin,
        (float)safe.y + margin,
        (float)safe.w - margin * 2.0f,
        top_h
    };

    SDL_SetRenderDrawColor(renderer, 13, 17, 21, 232);
    SDL_RenderFillRect(renderer, &top);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 65);
    SDL_RenderRect(renderer, &top);

    double instruction_distance_m = INFINITY;
    const OpenRideNavigationInstruction *next_instruction = NULL;
    if (navigation && navigation->valid) {
        next_instruction = openride_navigation_instructions_next(instructions,
                                                                  navigation->traveled_m,
                                                                  &instruction_distance_m);
    }

    char distance_text[32] = "--";
    char maneuver_text[128] = "Suivre l'itineraire";
    if (next_instruction) {
        openride_navigation_distance_text_fr(instruction_distance_m,
                                             distance_text,
                                             sizeof(distance_text));
        openride_navigation_instruction_text_fr(next_instruction,
                                                maneuver_text,
                                                sizeof(maneuver_text));
    }

    double following_gap_m = INFINITY;
    const OpenRideNavigationInstruction *following_instruction = NULL;
    if (next_instruction
        && next_instruction->maneuver != OPENRIDE_MANEUVER_ARRIVE) {
        following_instruction =
            openride_navigation_instructions_after(
                instructions,
                next_instruction->distance_from_start_m,
                &following_gap_m);
    }
    const bool show_following_instruction =
        following_instruction
        && isfinite(following_gap_m)
        && following_gap_m <= 300.0
        && navigation
        && navigation->status != OPENRIDE_NAVIGATION_OFF_ROUTE
        && navigation->status != OPENRIDE_NAVIGATION_ARRIVED;

    const float big_scale = ui_scale > 2.2f ? 3.0f : ui_scale * 1.35f;
    const float normal_scale = ui_scale > 2.2f ? 2.2f : ui_scale;
    const float small_scale = ui_scale > 1.8f ? 1.8f : ui_scale;
    const float icon_size = 58.0f * ui_scale;
    const float content_x = top.x + 72.0f * ui_scale;

    OpenRideManeuverType icon_maneuver = OPENRIDE_MANEUVER_CONTINUE;
    if (next_instruction) icon_maneuver = next_instruction->maneuver;
    if (navigation && navigation->status == OPENRIDE_NAVIGATION_ARRIVED) {
        icon_maneuver = OPENRIDE_MANEUVER_ARRIVE;
    }
    if (!navigation || navigation->status != OPENRIDE_NAVIGATION_OFF_ROUTE) {
        draw_drive_maneuver_icon(renderer,
                                 icon_maneuver,
                                 top.x + 6.0f * ui_scale,
                                 top.y + 11.0f * ui_scale,
                                 icon_size,
                                 2.2f * ui_scale);
    }

    if (navigation && navigation->status == OPENRIDE_NAVIGATION_OFF_ROUTE) {
        SDL_SetRenderDrawColor(renderer, 242, 92, 72, 255);
        draw_scaled_text(renderer,
                         content_x,
                         top.y + 10.0f * ui_scale,
                         big_scale,
                         auto_reroute ? "HORS ITINERAIRE" : "HORS ROUTE");
        SDL_SetRenderDrawColor(renderer, 245, 225, 220, 255);
        draw_scaled_text(renderer,
                         content_x,
                         top.y + 48.0f * ui_scale,
                         normal_scale,
                         auto_reroute ? "Recalcul automatique..." : "Recalcul manuel disponible");
    } else if (navigation && navigation->status == OPENRIDE_NAVIGATION_ARRIVED) {
        SDL_SetRenderDrawColor(renderer, 92, 210, 126, 255);
        draw_scaled_text(renderer,
                         content_x,
                         top.y + 14.0f * ui_scale,
                         big_scale,
                         "ARRIVEE");
    } else {
        SDL_SetRenderDrawColor(renderer, 255, 214, 83, 255);
        char primary[64];
        if (next_instruction && next_instruction->maneuver == OPENRIDE_MANEUVER_ARRIVE) {
            snprintf(primary, sizeof(primary), "ARRIVEE %s", distance_text);
        } else {
            snprintf(primary, sizeof(primary), "DANS %s", distance_text);
        }
        draw_scaled_text(renderer,
                         content_x,
                         top.y + 8.0f * ui_scale,
                         big_scale,
                         primary);
        SDL_SetRenderDrawColor(renderer, 245, 247, 248, 255);
        draw_scaled_text(renderer,
                         content_x,
                         top.y + 47.0f * ui_scale,
                         normal_scale,
                         maneuver_text);
    }

    if (show_following_instruction) {
        const float preview_h = 28.0f * ui_scale;
        SDL_FRect preview = {
            top.x,
            top.y + top.h + margin,
            top.w,
            preview_h
        };
        SDL_SetRenderDrawColor(renderer, 20, 25, 30, 224);
        SDL_RenderFillRect(renderer, &preview);
        SDL_SetRenderDrawColor(renderer, 255, 214, 83, 85);
        SDL_RenderRect(renderer, &preview);

        char following_distance_text[32];
        char following_maneuver_text[128];
        char following_text[180];
        openride_navigation_distance_text_fr(
            following_gap_m,
            following_distance_text,
            sizeof(following_distance_text));
        openride_navigation_instruction_text_fr(
            following_instruction,
            following_maneuver_text,
            sizeof(following_maneuver_text));
        snprintf(following_text,
                 sizeof(following_text),
                 "PUIS %s | %.120s",
                 following_distance_text,
                 following_maneuver_text);

        const float preview_scale = ui_scale > 1.8f ? 1.8f : ui_scale;
        SDL_SetRenderDrawColor(renderer, 245, 223, 153, 255);
        draw_scaled_text(renderer,
                         preview.x + 10.0f * ui_scale,
                         preview.y + 8.0f * ui_scale,
                         preview_scale,
                         following_text);
    }

    char gps_text[80];
    char simulation_prefix[40] = {0};
    if (simulated_gps) {
        const char *format =
            simulated_missed_turn_active
                ? "SIM x%.0f RATE | "
                : simulated_missed_turn_armed
                    ? "SIM x%.0f ARME | "
                    : simulated_gps_deviation
                        ? "SIM x%.0f +80m | "
                        : "SIM x%.0f DEV | ";
        snprintf(simulation_prefix,
                 sizeof(simulation_prefix),
                 format,
                 simulated_gps_time_scale);
    }
    if (drive->gps_quality == OPENRIDE_GPS_GOOD || drive->gps_quality == OPENRIDE_GPS_FAIR) {
        snprintf(gps_text,
                 sizeof(gps_text),
                 "%s%s %.0f m",
                 simulation_prefix,
                 openride_drive_mode_gps_quality_name(drive->gps_quality),
                 drive->gps_accuracy_m);
    } else {
        snprintf(gps_text,
                 sizeof(gps_text),
                 "%s%s",
                 simulation_prefix,
                 openride_drive_mode_gps_quality_name(drive->gps_quality));
    }
    switch (drive->gps_quality) {
        case OPENRIDE_GPS_GOOD: SDL_SetRenderDrawColor(renderer, 98, 211, 128, 255); break;
        case OPENRIDE_GPS_FAIR: SDL_SetRenderDrawColor(renderer, 255, 207, 77, 255); break;
        default: SDL_SetRenderDrawColor(renderer, 240, 96, 76, 255); break;
    }
    const float gps_w = (float)strlen(gps_text) * 8.0f * small_scale;
    /*
     * In OFF_ROUTE state the large red title uses most of the first row.
     * Move GPS quality to the lower-right corner of the banner instead of
     * letting both labels compete for the same horizontal space.
     */
    const float gps_y =
        navigation && navigation->status == OPENRIDE_NAVIGATION_OFF_ROUTE
            ? top.y + 67.0f * ui_scale
            : top.y + 10.0f * ui_scale;
    draw_scaled_text(renderer,
                     top.x + top.w - gps_w - 10.0f * ui_scale,
                     gps_y,
                     small_scale,
                     gps_text);

    const SDL_FRect controls = drive_controls_bounds(renderer, viewport_width, viewport_height);
    const float stats_h = 54.0f * ui_scale;
    SDL_FRect stats = {
        controls.x,
        controls.y - stats_h - margin,
        controls.w,
        stats_h
    };
    SDL_SetRenderDrawColor(renderer, 13, 17, 21, 225);
    SDL_RenderFillRect(renderer, &stats);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 50);
    SDL_RenderRect(renderer, &stats);

    const double speed_kph = navigation && navigation->valid
        ? navigation->speed_mps * 3.6 : 0.0;
    const double remaining_m = navigation && navigation->valid
        ? navigation->remaining_m : (route ? route->distance_m : 0.0);
    double remaining_s = 0.0;
    if (route && route->distance_m > 0.0 && route->estimated_time_s > 0.0) {
        remaining_s = route->estimated_time_s * clampd(remaining_m / route->distance_m, 0.0, 1.0);
    }
    char arrival[16];
    format_arrival_clock(remaining_s, arrival, sizeof(arrival));

    const float col_w = stats.w / 3.0f;
    char value[48];
    const float value_scale = ui_scale > 2.2f ? 2.35f : ui_scale * 1.05f;
    const float label_scale = ui_scale > 1.65f ? 1.65f : ui_scale;
    SDL_SetRenderDrawColor(renderer, 245, 247, 248, 255);
    snprintf(value, sizeof(value), "%.0f km/h", speed_kph);
    draw_scaled_text(renderer, stats.x + 8.0f * ui_scale, stats.y + 8.0f * ui_scale,
                     value_scale, value);
    snprintf(value, sizeof(value), "%.1f km", remaining_m / 1000.0);
    draw_scaled_text(renderer, stats.x + col_w + 8.0f * ui_scale,
                     stats.y + 8.0f * ui_scale, value_scale, value);
    draw_scaled_text(renderer, stats.x + col_w * 2.0f + 8.0f * ui_scale,
                     stats.y + 8.0f * ui_scale, value_scale, arrival);

    SDL_SetRenderDrawColor(renderer, 160, 170, 179, 255);
    draw_scaled_text(renderer, stats.x + 8.0f * ui_scale, stats.y + 34.0f * ui_scale,
                     label_scale, "VITESSE");
    draw_scaled_text(renderer, stats.x + col_w + 8.0f * ui_scale,
                     stats.y + 34.0f * ui_scale, label_scale, "RESTANT");
    draw_scaled_text(renderer, stats.x + col_w * 2.0f + 8.0f * ui_scale,
                     stats.y + 34.0f * ui_scale, label_scale, "ARRIVEE");

    if (session) {
        const OpenRideNavigationTripStats *trip = openride_navigation_session_stats(session);
        if (trip && trip->reroute_count > 0U) {
            char reroutes[32];
            snprintf(reroutes, sizeof(reroutes), "recalcul %u", trip->reroute_count);
            SDL_SetRenderDrawColor(renderer, 170, 178, 185, 255);
            draw_scaled_text(renderer,
                             stats.x + stats.w - (float)strlen(reroutes) * 8.0f * label_scale - 6.0f * ui_scale,
                             stats.y - 14.0f * ui_scale,
                             label_scale,
                             reroutes);
        }
    }

    if (metadata && metadata->attribution[0] != '\0') {
        SDL_SetRenderDrawColor(renderer, 65, 68, 70, 255);
        draw_scaled_text(renderer,
                         controls.x + 3.0f * ui_scale,
                         stats.y - 13.0f * ui_scale,
                         ui_scale > 1.4f ? 1.4f : ui_scale,
                         "(c) OpenStreetMap contributors | ODbL");
    }

    draw_drive_controls(renderer, drive, viewport_width, viewport_height);
}

static bool add_selection_from_screen(OpenRideMapSelection *selection,
                                      const OpenRideMapCamera *camera,
                                      double screen_x,
                                      double screen_y,
                                      int viewport_width,
                                      int viewport_height,
                                      bool *route_dirty,
                                      bool *loop_active,
                                      uint32_t *loop_waypoint_count,
                                      char *status,
                                      size_t status_size)
{
    double lat = 0.0;
    double lon = 0.0;
    if (!selection || !camera) return false;
    openride_screen_to_geo(camera,
                           screen_x,
                           screen_y,
                           viewport_width,
                           viewport_height,
                           &lat,
                           &lon);
    const OpenRideSelectionMarker added = openride_map_selection_add(selection, lat, lon);
    if (added == OPENRIDE_MARKER_NONE) return false;
    if (loop_active) *loop_active = false;
    if (loop_waypoint_count) *loop_waypoint_count = 0U;
    if (route_dirty) *route_dirty = openride_map_selection_complete(selection);
    if (status && status_size > 0U && route_dirty && !*route_dirty) {
        snprintf(status, status_size, "choisis la destination");
    }
    return true;
}

static OpenRideMapCamera camera_from_metadata(const OpenRideMBTilesMetadata *metadata)
{
    OpenRideMapCamera camera = {
        .center_lat = 50.370800,
        .center_lon = 3.080200,
        .zoom = 12.0
    };

    if (!metadata) return camera;

    if (metadata->has_center) {
        camera.center_lat = metadata->center_lat;
        camera.center_lon = metadata->center_lon;
        camera.zoom = metadata->center_zoom;
    }
    camera.zoom = clampd(camera.zoom,
                         (double)metadata->min_zoom,
                         20.0);

    return camera;
}

static const OpenRideRegionDefinition *region_step(const OpenRideRegionDefinition *region,
                                                       int direction)
{
    const size_t count = openride_region_count();
    if (count == 0U) return NULL;
    size_t index = 0U;
    if (region) {
        for (size_t i = 0U; i < count; ++i) {
            const OpenRideRegionDefinition *candidate = openride_region_at(i);
            if (candidate && strcmp(candidate->id, region->id) == 0) {
                index = i;
                break;
            }
        }
    }
    if (direction < 0) {
        index = index == 0U ? count - 1U : index - 1U;
    } else if (direction > 0) {
        index = (index + 1U) % count;
    }
    return openride_region_at(index);
}

static const OpenRideRegionDefinition *select_initial_region(
    const OpenRidePlatformPaths *paths,
    const char *saved_region_id,
    OpenRideRegionStatus *status,
    char *error,
    size_t error_size)
{
    const OpenRideRegionDefinition *selected = openride_region_find(saved_region_id);
    if (!selected) selected = openride_region_default();
    if (selected
        && openride_region_get_status(paths, selected, status, error, error_size)
        && openride_region_status_ready(status)) {
        return selected;
    }
    for (size_t i = 0U; i < openride_region_count(); ++i) {
        const OpenRideRegionDefinition *candidate = openride_region_at(i);
        OpenRideRegionStatus candidate_status;
        if (!candidate) continue;
        if (openride_region_get_status(paths,
                                       candidate,
                                       &candidate_status,
                                       error,
                                       error_size)
            && openride_region_status_ready(&candidate_status)) {
            if (status) *status = candidate_status;
            return candidate;
        }
    }
    if (selected) {
        openride_region_get_status(paths, selected, status, error, error_size);
    }
    return selected;
}

static bool activate_region_runtime(
    SDL_Renderer *renderer,
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *region,
    OpenRideMapStyle map_style,
    OpenRideMBTiles **map,
    OpenRideORMap **ormap,
    bool *ormap_map,
    bool *vector_map,
    bool *scalable_map,
    OpenRideMBTilesMetadata *metadata_storage,
    const OpenRideMBTilesMetadata **metadata,
    OpenRideMapRenderer *raster_renderer,
    OpenRideVectorMapRenderer *vector_renderer,
    OpenRideORMapRenderer *ormap_renderer,
    OpenRideRoutingGraph *routing_graph,
    bool *graph_loaded,
    OpenRidePlaceIndex **place_index,
    OpenRideMapCamera *camera,
    OpenRideRegionStatus *status_out,
    char *error,
    size_t error_size)
{
    if (!renderer || !paths || !region || !map || !ormap || !ormap_map
        || !vector_map || !scalable_map || !metadata_storage || !metadata
        || !raster_renderer || !vector_renderer || !ormap_renderer
        || !routing_graph || !graph_loaded || !place_index || !camera) {
        if (error && error_size) snprintf(error, error_size, "invalid runtime region switch");
        return false;
    }

    OpenRideRegionStatus status;
    if (!openride_region_get_status(paths, region, &status, error, error_size)
        || !openride_region_status_ready(&status)) {
        if (error && error_size && error[0] == '\0') {
            snprintf(error, error_size, "region data are not complete");
        }
        return false;
    }

    OpenRideORMap *new_ormap = openride_ormap_open(status.ormap_path, error, error_size);
    if (!new_ormap) return false;

    OpenRideRoutingGraph new_graph = {0};
    if (!openride_routing_graph_load(&new_graph, status.routing_path, error, error_size)) {
        openride_ormap_close(new_ormap);
        return false;
    }

    OpenRidePlaceIndex *new_place_index = openride_place_index_open(status.search_path,
                                                                    error,
                                                                    error_size);
    if (!new_place_index) {
        openride_routing_graph_destroy(&new_graph);
        openride_ormap_close(new_ormap);
        return false;
    }

    OpenRideORMapRenderer *new_renderer = calloc(1U, sizeof(*new_renderer));
    if (!new_renderer) {
        if (error && error_size) snprintf(error, error_size, "unable to allocate map renderer");
        openride_place_index_close(new_place_index);
        openride_routing_graph_destroy(&new_graph);
        openride_ormap_close(new_ormap);
        return false;
    }
    if (!openride_ormap_renderer_init(new_renderer, renderer, new_ormap)) {
        if (error && error_size) snprintf(error, error_size, "unable to initialize .ormap renderer");
        free(new_renderer);
        openride_place_index_close(new_place_index);
        openride_routing_graph_destroy(&new_graph);
        openride_ormap_close(new_ormap);
        return false;
    }
    openride_ormap_renderer_set_style(new_renderer, map_style);

    if (*ormap_map) {
        openride_ormap_renderer_destroy(ormap_renderer);
    } else if (*vector_map) {
        openride_vector_map_renderer_destroy(vector_renderer);
    } else if (*map) {
        openride_map_renderer_destroy(raster_renderer);
    }

    openride_place_index_close(*place_index);
    openride_routing_graph_destroy(routing_graph);
    openride_mbtiles_close(*map);
    openride_ormap_close(*ormap);

    *map = NULL;
    *ormap = new_ormap;
    *ormap_renderer = *new_renderer;
    free(new_renderer);
    *routing_graph = new_graph;
    *graph_loaded = true;
    *place_index = new_place_index;
    *ormap_map = true;
    *vector_map = false;
    *scalable_map = true;
    metadata_from_ormap(metadata_storage, openride_ormap_metadata(new_ormap));
    *metadata = metadata_storage;
    *camera = camera_from_metadata(*metadata);
    if (status_out) *status_out = status;
    if (error && error_size) error[0] = '\0';
    return true;
}


static void refresh_map_world_overview(OpenRideMapWorld *map_world,
                                               const OpenRidePlatformPaths *paths)
{
    if (!map_world || !paths) return;
    char world_error[256] = {0};
    if (!openride_map_world_refresh(map_world,
                                    paths,
                                    world_error,
                                    sizeof(world_error))) {
        SDL_Log("MapWorld refresh failed: %s",
                world_error[0] ? world_error : "unknown error");
    }
}

int main(int argc, char **argv)
{
    char error[512] = {0};

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

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
    const OpenRideRegionDefinition *region = select_initial_region(&platform_paths,
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
            map_path = default_map_path();
#endif
        }
    }
    if (!routing_graph_path && region_status.routing_installed) {
        routing_graph_path = region_status.routing_path;
    }
#ifndef __ANDROID__
    if (!routing_graph_path) routing_graph_path = default_routing_graph_path();
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

    if (map_path && file_exists(map_path)) {
        ormap_map = has_suffix(map_path, ".ormap");
        if (ormap_map) {
            ormap = openride_ormap_open(map_path, error, sizeof(error));
            if (!ormap) {
                SDL_Log("Unable to open OpenRide map %s: %s",
                        map_path, error[0] ? error : "unknown error");
                SDL_Quit();
                return 1;
            }
            metadata_from_ormap(&metadata_storage, openride_ormap_metadata(ormap));
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
    bool vector_map = map && is_vector_map(metadata);
    bool scalable_map = vector_map || ormap_map;
    OpenRideMapCamera camera = camera_from_metadata(metadata);
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
    bool running = true;
    bool render_suspended = false;
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

    lifecycle_watch_installed = SDL_AddEventWatch(openride_lifecycle_event_watch,
                                                   &lifecycle_watch);
    if (!lifecycle_watch_installed) {
        SDL_Log("Unable to install lifecycle event watch: %s", SDL_GetError());
    }

    if (file_exists(region_status.search_path)) {
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
        refresh_stored_places(app_storage, true, favorite_places, &favorite_count);
        refresh_stored_places(app_storage, false, history_places, &history_count);    } else {
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
        SDL_Log("MapWorld overview: %zu installed region(s)",
                openride_map_world_region_count(map_world));
    } else {
        SDL_Log("MapWorld overview unavailable: %s",
                error[0] ? error : "unknown error");
        error[0] = '\0';
    }
    if (file_exists(gpx_import_path)) {
        gpx_loaded = load_gpx_overlay(gpx_import_path,
                                      &gpx_overlay,
                                      route_status,
                                      sizeof(route_status));
        if (gpx_loaded) {
            int gpx_width = 0;
            int gpx_height = 0;
            SDL_GetCurrentRenderOutputSize(renderer, &gpx_width, &gpx_height);
            fit_camera_to_gpx(&camera,
                              &gpx_overlay,
                              gpx_width,
                              gpx_height,
                              (double)metadata->min_zoom,
                              scalable_map ? 18.0 : (double)metadata->max_zoom);
        }
    }

    while (running) {
        uint64_t map_zoom_loop_started_ns =
            map_zoom_test.active ? SDL_GetTicksNS() : 0U;
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            SDL_ConvertEventToRenderCoordinates(renderer, &event);

            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;

                case SDL_EVENT_KEY_DOWN:
                    if (app_panel != OPENRIDE_APP_PANEL_NONE) {
                        if (event.key.key == SDLK_TAB) {
                            app_panel = OPENRIDE_APP_PANEL_NONE;
                        } else if (event.key.key == SDLK_ESCAPE) {
                            app_panel = app_panel == OPENRIDE_APP_PANEL_MAIN
                                ? OPENRIDE_APP_PANEL_NONE
                                : OPENRIDE_APP_PANEL_MAIN;
                            app_panel_selected = 0U;
                        } else if (app_panel == OPENRIDE_APP_PANEL_MAIN) {
                            if (event.key.key == SDLK_R) {
                                app_panel = OPENRIDE_APP_PANEL_NONE;
                                open_place_search(window,
                                                  place_world,
                                                  &place_search_active,
                                                  place_search_query,
                                                  &place_search_result_count,
                                                  &place_search_selected,
                                                  route_status,
                                                  sizeof(route_status));
                            } else if (event.key.key == SDLK_F) {
                                refresh_stored_places(app_storage, true, favorite_places, &favorite_count);
                                app_panel = OPENRIDE_APP_PANEL_FAVORITES;
                                app_panel_selected = 0U;
                            } else if (event.key.key == SDLK_H) {
                                refresh_stored_places(app_storage, false, history_places, &history_count);
                                app_panel = OPENRIDE_APP_PANEL_HISTORY;
                                app_panel_selected = 0U;
                            } else if (event.key.key == SDLK_C) {
                                app_panel = OPENRIDE_APP_PANEL_REGIONS;
                            } else if (event.key.key == SDLK_P) {
                                app_panel = OPENRIDE_APP_PANEL_SETTINGS;
                            } else if (event.key.key == SDLK_Z) {
                                if (map_zoom_test.active) {
                                    openride_map_zoom_test_cancel(&map_zoom_test);
                                    snprintf(route_status, sizeof(route_status),
                                             "test zoom carte annule");
                                } else {
                                    openride_map_zoom_test_start(&map_zoom_test, &camera, &platform_paths);
                                    map_zoom_loop_started_ns = SDL_GetTicksNS();
                                    app_panel = OPENRIDE_APP_PANEL_NONE;
                                    snprintf(route_status, sizeof(route_status),
                                             "test zoom 9.000 -> 17.000 -> 9.000 | log data/map-zoom-test.csv");
                                }
                            }
                        } else if (app_panel == OPENRIDE_APP_PANEL_ROUTE) {
                            if (event.key.key == SDLK_D) {
                                place_search_purpose =
                                    OPENRIDE_PLACE_SEARCH_ROUTE_START;
                                app_panel = OPENRIDE_APP_PANEL_NONE;
                                open_place_search(window,
                                                  place_world,
                                                  &place_search_active,
                                                  place_search_query,
                                                  &place_search_result_count,
                                                  &place_search_selected,
                                                  route_status,
                                                  sizeof(route_status));
                            } else if (event.key.key == SDLK_A) {
                                place_search_purpose =
                                    OPENRIDE_PLACE_SEARCH_ROUTE_DESTINATION;
                                app_panel = OPENRIDE_APP_PANEL_NONE;
                                open_place_search(window,
                                                  place_world,
                                                  &place_search_active,
                                                  place_search_query,
                                                  &place_search_result_count,
                                                  &place_search_selected,
                                                  route_status,
                                                  sizeof(route_status));
                            } else if ((event.key.key == SDLK_RETURN
                                        || event.key.key == SDLK_KP_ENTER)
                                       && openride_map_selection_complete(&selection)) {
                                app_panel = OPENRIDE_APP_PANEL_NONE;
                                route_dirty = true;
                            }
                        } else if (app_panel == OPENRIDE_APP_PANEL_FAVORITES
                                   || app_panel == OPENRIDE_APP_PANEL_HISTORY) {
                            const bool favorites_panel = app_panel == OPENRIDE_APP_PANEL_FAVORITES;
                            const uint32_t count = favorites_panel ? favorite_count : history_count;
                            OpenRideStoredPlace *items = favorites_panel ? favorite_places : history_places;
                            if (event.key.key == SDLK_UP && count > 0U) {
                                app_panel_selected = app_panel_selected == 0U ? count - 1U : app_panel_selected - 1U;
                            } else if (event.key.key == SDLK_DOWN && count > 0U) {
                                app_panel_selected = (app_panel_selected + 1U) % count;
                            } else if ((event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)
                                       && count > 0U) {
                                camera.center_lat = items[app_panel_selected].lat;
                                camera.center_lon = items[app_panel_selected].lon;
                                if (camera.zoom < 14.0) camera.zoom = 14.0;
                                app_panel = OPENRIDE_APP_PANEL_NONE;
                            } else if (favorites_panel
                                       && (event.key.key == SDLK_DELETE || event.key.key == SDLK_BACKSPACE)
                                       && count > 0U && app_storage) {
                                openride_app_storage_remove_favorite(app_storage,
                                                                     items[app_panel_selected].id,
                                                                     error,
                                                                     sizeof(error));
                                refresh_stored_places(app_storage, true, favorite_places, &favorite_count);
                                if (app_panel_selected >= favorite_count && app_panel_selected > 0U) {
                                    --app_panel_selected;
                                }
                            }
                        } else if (app_panel == OPENRIDE_APP_PANEL_REGIONS) {
                            if ((event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) && !region_busy) {
                                region = region_step(region, event.key.key == SDLK_LEFT ? -1 : 1);
                                openride_region_get_status(&platform_paths,
                                                           region,
                                                           &region_status,
                                                           error,
                                                           sizeof(error));
                                region_work_status[0] = '\0';
                            } else if (event.key.key == SDLK_D
                                       || event.key.key == SDLK_RETURN
                                       || event.key.key == SDLK_KP_ENTER) {
                                if (openride_region_status_ready(&region_status)
#ifdef __ANDROID__
                                    && region_status.poly_present
#endif
                                ) {
                                    if (region == active_region) {
                                        snprintf(region_work_status, sizeof(region_work_status),
                                                 "Cette region est deja active");
                                    } else {
                                        region_activation_requested = true;
                                    }
                                } else {
#ifdef __ANDROID__
                                    begin_android_region_install(&platform_paths,
                                                                 region,
                                                                 &region_status,
                                                                 &region_prepare_context,
                                                                 &region_prepare_thread,
                                                                 &region_download_started,
                                                                 &region_download_is_poly,
                                                                 &region_busy,
                                                                 &region_progress,
                                                                 region_work_status,
                                                                 sizeof(region_work_status),
                                                                 error,
                                                                 sizeof(error));
#else
                                    if (region_status.source_pbf_present) {
                                        snprintf(region_work_status, sizeof(region_work_status),
                                                 "Prepare cette region depuis le Terminal macOS");
                                    } else {
                                        snprintf(region_work_status, sizeof(region_work_status),
                                                 "PBF regional absent sur macOS");
                                    }
#endif
                                }
                            } else if (event.key.key == SDLK_S && !region_busy) {
                                if (region == active_region) {
                                    snprintf(region_work_status, sizeof(region_work_status),
                                             "Impossible de supprimer la region active");
                                } else if (openride_region_remove_generated(&platform_paths,
                                                                            region,
                                                                            error,
                                                                            sizeof(error))) {
                                    openride_region_get_status(&platform_paths, region,
                                                               &region_status, error, sizeof(error));
                                    refresh_map_world_overview(map_world, &platform_paths);
                                    snprintf(region_work_status, sizeof(region_work_status),
                                             "Donnees de la region supprimees");
                                } else {
                                    snprintf(region_work_status, sizeof(region_work_status),
                                             "Suppression impossible: %.120s", error);
                                }
                            }
                        } else if (app_panel == OPENRIDE_APP_PANEL_SETTINGS) {
                            if (event.key.key == SDLK_M && scalable_map) {
                                map_style = openride_map_style_next(map_style);
                                if (ormap_map) openride_ormap_renderer_set_style(&ormap_renderer, map_style);
                                else if (vector_map) openride_vector_map_renderer_set_style(&vector_renderer, map_style);
                                if (app_storage) openride_app_storage_set_int(app_storage, "map_style", (int)map_style, error, sizeof(error));
                            } else if (event.key.key == SDLK_1 || event.key.key == SDLK_2 || event.key.key == SDLK_3) {
                                routing_profile = event.key.key == SDLK_1 ? OPENRIDE_ROUTING_PROFILE_FASTEST
                                                  : event.key.key == SDLK_2 ? OPENRIDE_ROUTING_PROFILE_TOURING
                                                                           : OPENRIDE_ROUTING_PROFILE_TRAIL;
                                if (app_storage) openride_app_storage_set_int(app_storage, "routing_profile", (int)routing_profile, error, sizeof(error));
                            } else if (event.key.key == SDLK_F) {
                                follow_gps = !follow_gps;
                                if (app_storage) openride_app_storage_set_int(app_storage, "follow_gps", follow_gps ? 1 : 0, error, sizeof(error));
                            } else if (event.key.key == SDLK_A) {
                                auto_reroute = !auto_reroute;
                                openride_navigation_session_set_auto_reroute(&navigation_session, auto_reroute);
                                if (app_storage) openride_app_storage_set_int(app_storage, "auto_reroute", auto_reroute ? 1 : 0, error, sizeof(error));
                            } else if (event.key.key == SDLK_V) {
                                voice_enabled = !voice_enabled;
                                openride_voice_guidance_set_enabled(&voice_guidance, voice_enabled);
#ifdef __ANDROID__
                                if (voice_enabled) openride_android_voice_guidance_init();
#endif
                                if (app_storage) openride_app_storage_set_int(app_storage, "voice_enabled", voice_enabled ? 1 : 0, error, sizeof(error));
                            }
                        }
                        break;
                    }

                    if (place_search_active) {
                        if (event.key.key == SDLK_ESCAPE) {
                            place_search_active = false;
                            if (place_search_purpose
                                != OPENRIDE_PLACE_SEARCH_BROWSE) {
                                app_panel = OPENRIDE_APP_PANEL_ROUTE;
                            }
                            place_search_purpose =
                                OPENRIDE_PLACE_SEARCH_BROWSE;
                            SDL_StopTextInput(window);
                        } else if (event.key.key == SDLK_BACKSPACE) {
                            utf8_backspace(place_search_query);
                            refresh_place_search(place_world,
                                                 place_search_query,
                                                 place_search_results,
                                                 &place_search_result_count,
                                                 &place_search_selected,
                                                 route_status,
                                                 sizeof(route_status));
                        } else if (event.key.key == SDLK_UP && place_search_result_count > 0U) {
                            if (place_search_selected == 0U) {
                                place_search_selected = place_search_result_count - 1U;
                            } else {
                                --place_search_selected;
                            }
                        } else if (event.key.key == SDLK_DOWN && place_search_result_count > 0U) {
                            place_search_selected = (place_search_selected + 1U) % place_search_result_count;
                        } else if (event.key.key == SDLK_D
                                   && (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0
                                   && place_search_result_count > 0U && app_storage) {
                            const OpenRidePlaceSearchResult *chosen = &place_search_results[place_search_selected];
                            if (openride_app_storage_add_favorite(app_storage, chosen->name, chosen->lat, chosen->lon, (int)chosen->kind, error, sizeof(error))) {
                                refresh_stored_places(app_storage, true, favorite_places, &favorite_count);
                                snprintf(route_status, sizeof(route_status), "favori ajoute: %.120s", chosen->name);
                            }
                        } else if ((event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)
                                   && place_search_result_count > 0U) {
                            const OpenRidePlaceSearchResult *chosen =
                                &place_search_results[place_search_selected];
                            camera.center_lat = chosen->lat;
                            camera.center_lon = chosen->lon;
                            if (camera.zoom < 14.0) camera.zoom = 14.0;

                            if (place_search_purpose
                                == OPENRIDE_PLACE_SEARCH_ROUTE_START) {
                                openride_map_selection_set(&selection,
                                                           OPENRIDE_MARKER_START,
                                                           chosen->lat,
                                                           chosen->lon);
                                openride_map_selection_set_region_hint(
                                    &selection,
                                    OPENRIDE_MARKER_START,
                                    chosen->region_id);
                                start_snap.segment_id =
                                    OPENRIDE_ROUTING_SEGMENT_NONE;
                                openride_route_destroy(&route);
                                route_valid = false;
                                route_dirty = false;
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "depart: %.120s",
                                         chosen->name);
                                app_panel = OPENRIDE_APP_PANEL_ROUTE;
                            } else if (place_search_purpose
                                       == OPENRIDE_PLACE_SEARCH_ROUTE_DESTINATION) {
                                openride_map_selection_set(&selection,
                                                           OPENRIDE_MARKER_DESTINATION,
                                                           chosen->lat,
                                                           chosen->lon);
                                openride_map_selection_set_region_hint(
                                    &selection,
                                    OPENRIDE_MARKER_DESTINATION,
                                    chosen->region_id);
                                destination_snap.segment_id =
                                    OPENRIDE_ROUTING_SEGMENT_NONE;
                                openride_route_destroy(&route);
                                route_valid = false;
                                route_dirty = false;
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "arrivee: %.120s",
                                         chosen->name);
                                app_panel = OPENRIDE_APP_PANEL_ROUTE;
                            } else {
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "recherche: %.120s (%s)",
                                         chosen->name,
                                         openride_place_kind_name(chosen->kind));
                            }

                            if (app_storage) {
                                openride_app_storage_add_history(app_storage,
                                                                 chosen->name,
                                                                 chosen->lat,
                                                                 chosen->lon,
                                                                 (int)chosen->kind,
                                                                 error,
                                                                 sizeof(error));
                                refresh_stored_places(app_storage,
                                                      false,
                                                      history_places,
                                                      &history_count);
                            }
                            place_search_active = false;
                            place_search_purpose =
                                OPENRIDE_PLACE_SEARCH_BROWSE;
                            SDL_StopTextInput(window);
                        }
                        break;
                    }

                    if (event.key.key == SDLK_TAB) {
                        app_panel = OPENRIDE_APP_PANEL_MAIN;
                        app_panel_selected = 0U;
                    } else if (event.key.key == SDLK_SLASH
                               || (event.key.key == SDLK_COLON && (event.key.mod & SDL_KMOD_SHIFT) != 0)
                               || (event.key.key == SDLK_SEMICOLON && (event.key.mod & SDL_KMOD_SHIFT) != 0)
                               || (event.key.key == SDLK_F
                                   && (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0)) {
                        place_search_purpose =
                            OPENRIDE_PLACE_SEARCH_BROWSE;
                        open_place_search(window, place_world, &place_search_active,
                                          place_search_query, &place_search_result_count,
                                          &place_search_selected, route_status, sizeof(route_status));
                    } else if (event.key.key == SDLK_V) {
                        if (app_storage) {
                            const double lat = selection.has_destination ? selection.destination.lat : camera.center_lat;
                            const double lon = selection.has_destination ? selection.destination.lon : camera.center_lon;
                            const char *name = selection.has_destination ? "Destination" : "Position carte";
                            if (openride_app_storage_add_favorite(app_storage, name, lat, lon, 0, error, sizeof(error))) {
                                refresh_stored_places(app_storage, true, favorite_places, &favorite_count);
                                snprintf(route_status, sizeof(route_status), "favori ajoute");
                            }
                        }
                    } else if (event.key.key == SDLK_ESCAPE) {
                        running = false;
                    } else if (event.key.key == SDLK_C) {
                        openride_map_selection_clear(&selection);
                        clear_navigation_session(&navigation,
                                                 &gps_simulator,
                                                 &navigation_state,
                                                 &gps_sample,
                                                 &gps_sample_valid);
                        simulator_deviation = false;
                        gpx_navigation_active = false;
                        openride_navigation_session_reset(&navigation_session);
                        openride_location_filter_reset(&location_filter);
                        memset(&filtered_location, 0, sizeof(filtered_location));
                        gpx_recording_active = false;
                        openride_gpx_document_clear(&gpx_recording);
                        gpx_last_recorded_position_m = -1.0;
                        openride_route_destroy(&route);
                        route_valid = false;
                        route_dirty = false;
                        loop_active = false;
                        memset(&loop_stats, 0, sizeof(loop_stats));
                        loop_waypoint_count = 0U;
                        start_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                        destination_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                        snprintf(route_status,
                                 sizeof(route_status),
                                 "%s",
                                 graph_loaded ? "pret" : "graphe non installe");
                    } else if (event.key.key == SDLK_B) {
                        if (selection.has_destination) {
                            openride_map_selection_remove(&selection,
                                                          OPENRIDE_MARKER_DESTINATION);
                        }
                        destination_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                        route_dirty = false;
                        clear_navigation_session(&navigation,
                                                 &gps_simulator,
                                                 &navigation_state,
                                                 &gps_sample,
                                                 &gps_sample_valid);
                        simulator_deviation = false;
                        gpx_navigation_active = false;
                        openride_navigation_session_reset(&navigation_session);
                        openride_location_filter_reset(&location_filter);
                        memset(&filtered_location, 0, sizeof(filtered_location));
                        route_valid = generate_loop_route(&routing_graph,
                                                          graph_loaded,
                                                          &selection,
                                                          routing_profile,
                                                          loop_target_distance_m,
                                                          loop_direction,
                                                          loop_seed++,
                                                          &route,
                                                          &loop_stats,
                                                          loop_waypoints,
                                                          &loop_waypoint_count,
                                                          &start_snap,
                                                          route_status,
                                                          sizeof(route_status));
                        loop_active = route_valid;
                        if (route_valid) {
                            prepare_navigation_session(&navigation,
                                                       &gps_simulator,
                                                       &navigation_instructions,
                                                       &routing_graph,
                                                       &route,
                                                       route_status,
                                                       sizeof(route_status));
                        }
                    } else if (event.key.key == SDLK_S) {
#ifdef __ANDROID__
                        if (simulated_gps_active) {
                            openride_location_provider_stop(
                                &simulated_location_provider);
                            simulated_gps_active = false;
                            simulator_deviation = false;
                            openride_gps_simulator_set_lateral_offset_m(
                                &gps_simulator, 0.0);
                            openride_drive_mode_set_active(&drive_mode, false);
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "simulation GPS [DEV] arretee");
                        } else if (real_gps_active) {
                            openride_location_provider_stop(&location_provider);
                            real_gps_active = false;
                            snprintf(route_status, sizeof(route_status), "GPS reel arrete");
                        } else if (openride_location_provider_start(&location_provider)) {
                            real_gps_active = true;
                            snprintf(route_status, sizeof(route_status), "GPS reel actif");
                        } else {
                            snprintf(route_status, sizeof(route_status),
                                     "GPS indisponible: autorise la localisation puis reessaie");
                        }
#else
                        if (route_valid && gps_simulator.route) {
                            const bool active = openride_gps_simulator_toggle(&gps_simulator);
                            if (openride_gps_simulator_sample(&gps_simulator, &gps_sample)) {
                                gps_sample_valid = true;
                                openride_navigation_engine_update(&navigation,
                                                                  gps_sample.lat,
                                                                  gps_sample.lon,
                                                                  gps_sample.speed_mps,
                                                                  gps_sample.heading_deg,
                                                                  &navigation_state);
                            }
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "simulation GPS %s",
                                     active ? "en cours" : "en pause");
                        } else {
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "calcule un itineraire avant de lancer le GPS");
                        }
#endif
                    } else if (event.key.key == SDLK_X) {
#ifdef __ANDROID__
                        snprintf(route_status, sizeof(route_status),
                                 "test deviation X disponible sur macOS");
#else
                        simulator_deviation = !simulator_deviation;
                        openride_gps_simulator_set_lateral_offset_m(
                            &gps_simulator,
                            simulator_deviation ? 80.0 : 0.0);
                        snprintf(route_status,
                                 sizeof(route_status),
                                 "deviation GPS test: %s",
                                 simulator_deviation ? "80 m" : "desactivee");
#endif
                    } else if (event.key.key == SDLK_F) {
                        follow_gps = !follow_gps;
                        if (app_storage) openride_app_storage_set_int(app_storage, "follow_gps", follow_gps ? 1 : 0, error, sizeof(error));
                        snprintf(route_status,
                                 sizeof(route_status),
                                 "suivi camera GPS: %s",
                                 follow_gps ? "actif" : "inactif");
                    } else if (event.key.key == SDLK_A) {
                        auto_reroute = !auto_reroute;
                        openride_navigation_session_set_auto_reroute(&navigation_session, auto_reroute);
                        if (app_storage) openride_app_storage_set_int(app_storage, "auto_reroute", auto_reroute ? 1 : 0, error, sizeof(error));
                        snprintf(route_status,
                                 sizeof(route_status),
                                 "recalcul automatique: %s",
                                 auto_reroute ? "actif" : "inactif");
                    } else if (event.key.key == SDLK_R) {
                        if (gps_sample_valid && selection.has_destination && !loop_active
                            && !gpx_navigation_active) {
                            const bool resume_simulator = gps_simulator.active;
                            const double reroute_lat = filtered_location.valid
                                ? filtered_location.lat : gps_sample.lat;
                            const double reroute_lon = filtered_location.valid
                                ? filtered_location.lon : gps_sample.lon;
                            route_valid = reroute_navigation_from_position(
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
                            memset(&navigation_state, 0, sizeof(navigation_state));
                            memset(&filtered_location, 0, sizeof(filtered_location));
                            if (route_valid) {
                                snprintf(route_status, sizeof(route_status), "itineraire recalcule depuis GPS");
                            } else if (openride_map_selection_complete(&selection)) {
                                routing_world_pending_reroute = true;
                                routing_world_pending_resume_simulator = resume_simulator;
                                route_dirty = true;
                            }
                        } else {
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "R necessite une navigation routiere active");
                        }
                    } else if (event.key.key == SDLK_O) {
                        loop_direction = openride_loop_direction_next(loop_direction);
                        if (loop_active) {
                            clear_navigation_session(&navigation,
                                                     &gps_simulator,
                                                     &navigation_state,
                                                     &gps_sample,
                                                     &gps_sample_valid);
                            openride_route_destroy(&route);
                            route_valid = false;
                            loop_active = false;
                            loop_waypoint_count = 0U;
                        }
                        snprintf(route_status,
                                 sizeof(route_status),
                                 "direction boucle: %s | B pour generer",
                                 openride_loop_direction_name(loop_direction));
                    } else if (event.key.key == SDLK_PLUS
                               || event.key.key == SDLK_KP_PLUS
                               || event.key.key == SDLK_EQUALS) {
                        loop_target_distance_m = clampd(
                            loop_target_distance_m + OPENRIDE_LOOP_DISTANCE_STEP_M,
                            OPENRIDE_LOOP_DISTANCE_MIN_M,
                            OPENRIDE_LOOP_DISTANCE_MAX_M);
                        if (loop_active) {
                            clear_navigation_session(&navigation,
                                                     &gps_simulator,
                                                     &navigation_state,
                                                     &gps_sample,
                                                     &gps_sample_valid);
                            openride_route_destroy(&route);
                            route_valid = false;
                            loop_active = false;
                            loop_waypoint_count = 0U;
                        }
                        snprintf(route_status,
                                 sizeof(route_status),
                                 "boucle cible %.0f km | B pour generer",
                                 loop_target_distance_m / 1000.0);
                    } else if (event.key.key == SDLK_MINUS
                               || event.key.key == SDLK_KP_MINUS) {
                        loop_target_distance_m = clampd(
                            loop_target_distance_m - OPENRIDE_LOOP_DISTANCE_STEP_M,
                            OPENRIDE_LOOP_DISTANCE_MIN_M,
                            OPENRIDE_LOOP_DISTANCE_MAX_M);
                        if (loop_active) {
                            clear_navigation_session(&navigation,
                                                     &gps_simulator,
                                                     &navigation_state,
                                                     &gps_sample,
                                                     &gps_sample_valid);
                            openride_route_destroy(&route);
                            route_valid = false;
                            loop_active = false;
                            loop_waypoint_count = 0U;
                        }
                        snprintf(route_status,
                                 sizeof(route_status),
                                 "boucle cible %.0f km | B pour generer",
                                 loop_target_distance_m / 1000.0);
                    } else if (event.key.key == SDLK_I) {
                        gpx_loaded = load_gpx_overlay(gpx_import_path,
                                                      &gpx_overlay,
                                                      route_status,
                                                      sizeof(route_status));
                        if (gpx_loaded) {
                            int gpx_width = 0;
                            int gpx_height = 0;
                            SDL_GetCurrentRenderOutputSize(renderer, &gpx_width, &gpx_height);
                            fit_camera_to_gpx(&camera,
                                              &gpx_overlay,
                                              gpx_width,
                                              gpx_height,
                                              (double)metadata->min_zoom,
                                              scalable_map ? 18.0 : (double)metadata->max_zoom);
                        }
                    } else if (event.key.key == SDLK_N) {
                        if (!gpx_loaded) {
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "importe d'abord une trace GPX avec I");
                        } else {
                            memset(&navigation_state, 0, sizeof(navigation_state));
                            memset(&gps_sample, 0, sizeof(gps_sample));
                            gps_sample_valid = false;
                            simulator_deviation = false;
                            loop_active = false;
                            loop_waypoint_count = 0U;
                            route_dirty = false;
                            start_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                            destination_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                            route_valid = prepare_gpx_navigation(&gpx_overlay,
                                                                 &routing_graph,
                                                                 &selection,
                                                                 &route,
                                                                 &navigation,
                                                                 &gps_simulator,
                                                                 &navigation_instructions,
                                                                 &navigation_session,
                                                                 &location_filter,
                                                                 route_status,
                                                                 sizeof(route_status));
                            gpx_navigation_active = route_valid;
                        }
                    } else if (event.key.key == SDLK_E) {
                        if (!route_valid) {
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "aucun itineraire a exporter en GPX");
                        } else {
                            char gpx_error[192] = {0};
                            if (openride_gpx_save_route(gpx_route_export_path,
                                                        &route,
                                                        loop_active ? "OpenRide boucle" : "OpenRide itineraire",
                                                        gpx_error,
                                                        sizeof(gpx_error))) {
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "GPX exporte: %s",
                                         gpx_route_export_path);
                            } else {
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "export GPX impossible: %.145s",
                                         gpx_error[0] ? gpx_error : "erreur inconnue");
                            }
                        }
                    } else if (event.key.key == SDLK_G) {
                        if (!gpx_recording_active) {
                            if (!route_valid || !gps_simulator.route) {
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "G necessite un itineraire avec GPS simule");
                            } else {
                                openride_gpx_document_clear(&gpx_recording);
                                snprintf(gpx_recording.name,
                                         sizeof(gpx_recording.name),
                                         "OpenRide GPS recording");
                                gpx_recording_active = true;
                                gpx_last_recorded_position_m = -1.0;
                                if (gps_sample_valid) {
                                    record_gps_sample(&gpx_recording,
                                                      &gps_sample,
                                                      &gpx_last_recorded_position_m);
                                }
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "enregistrement GPX demarre");
                            }
                        } else {
                            gpx_recording_active = false;
                            if (gpx_recording.track_points.count >= 2U) {
                                char gpx_error[192] = {0};
                                if (openride_gpx_save_document(gpx_recording_export_path,
                                                               &gpx_recording,
                                                               "OpenRide",
                                                               gpx_error,
                                                               sizeof(gpx_error))) {
                                    snprintf(route_status,
                                             sizeof(route_status),
                                             "trace GPX enregistree: %s",
                                             gpx_recording_export_path);
                                } else {
                                    snprintf(route_status,
                                             sizeof(route_status),
                                             "trace GPX non ecrite: %.140s",
                                             gpx_error[0] ? gpx_error : "erreur inconnue");
                                }
                            } else {
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "trace GPX trop courte pour etre enregistree");
                            }
                        }
                    } else if (event.key.key == SDLK_M && scalable_map) {
                        map_style = openride_map_style_next(map_style);
                        if (ormap_map) openride_ormap_renderer_set_style(&ormap_renderer, map_style);
                        else if (vector_map) openride_vector_map_renderer_set_style(&vector_renderer, map_style);
                        if (app_storage) openride_app_storage_set_int(app_storage, "map_style", (int)map_style, error, sizeof(error));
                    } else if (event.key.key == SDLK_1
                               || event.key.key == SDLK_2
                               || event.key.key == SDLK_3) {
                        if (event.key.key == SDLK_1) {
                            routing_profile = OPENRIDE_ROUTING_PROFILE_FASTEST;
                        } else if (event.key.key == SDLK_2) {
                            routing_profile = OPENRIDE_ROUTING_PROFILE_TOURING;
                        } else {
                            routing_profile = OPENRIDE_ROUTING_PROFILE_TRAIL;
                        }
                        if (app_storage) openride_app_storage_set_int(app_storage, "routing_profile", (int)routing_profile, error, sizeof(error));
                        if (gpx_navigation_active) {
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "profil %s enregistre | la trace GPX reste active",
                                     openride_routing_profile_name(routing_profile));
                        } else if (loop_active) {
                            clear_navigation_session(&navigation,
                                                     &gps_simulator,
                                                     &navigation_state,
                                                     &gps_sample,
                                                     &gps_sample_valid);
                            openride_navigation_session_reset(&navigation_session);
                            openride_location_filter_reset(&location_filter);
                            openride_route_destroy(&route);
                            route_valid = false;
                            loop_active = false;
                            loop_waypoint_count = 0U;
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "profil %s | B pour regenerer la boucle",
                                     openride_routing_profile_name(routing_profile));
                        } else {
                            route_dirty = openride_map_selection_complete(&selection);
                        }
                    }
                    break;

                case SDL_EVENT_TEXT_INPUT:
                    if (place_search_active && place_world) {
                        const size_t current = strlen(place_search_query);
                        const size_t incoming = strlen(event.text.text);
                        if (current + incoming < sizeof(place_search_query)) {
                            memcpy(place_search_query + current,
                                   event.text.text,
                                   incoming + 1U);
                            refresh_place_search(place_world,
                                                 place_search_query,
                                                 place_search_results,
                                                 &place_search_result_count,
                                                 &place_search_selected,
                                                 route_status,
                                                 sizeof(route_status));
                        }
                    }
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    if (event.button.which == SDL_TOUCH_MOUSEID) break;
                    int width = 0;
                    int height = 0;
                    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
                    if (place_search_active || app_panel != OPENRIDE_APP_PANEL_NONE) break;

                    if (event.button.button == SDL_BUTTON_LEFT) {
                        const OpenRideToolbarAction toolbar_action = mobile_toolbar_hit_test(
                            renderer,
                            (double)event.button.x,
                            (double)event.button.y,
                            width,
                            height);
                        if (toolbar_action != OPENRIDE_TOOLBAR_NONE) {
                            pending_toolbar_action = toolbar_action;
                            break;
                        }
                        mouse_down_x = (double)event.button.x;
                        mouse_down_y = (double)event.button.y;
                        map_drag_moved = false;
                        dragging_marker = marker_at_screen(&camera,
                                                           &selection,
                                                           mouse_down_x,
                                                           mouse_down_y,
                                                           width,
                                                           height);
                        dragging_map = dragging_marker == OPENRIDE_MARKER_NONE;
                        if (dragging_marker != OPENRIDE_MARKER_NONE) {
                            clear_navigation_session(&navigation,
                                                     &gps_simulator,
                                                     &navigation_state,
                                                     &gps_sample,
                                                     &gps_sample_valid);
                            openride_route_destroy(&route);
                            route_valid = false;
                            loop_active = false;
                            gpx_navigation_active = false;
                            openride_navigation_session_reset(&navigation_session);
                            openride_location_filter_reset(&location_filter);
                            loop_waypoint_count = 0U;
                        }
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        const OpenRideSelectionMarker marker = marker_at_screen(
                            &camera,
                            &selection,
                            (double)event.button.x,
                            (double)event.button.y,
                            width,
                            height);
                        if (marker != OPENRIDE_MARKER_NONE) {
                            openride_map_selection_remove(&selection, marker);
                            clear_navigation_session(&navigation,
                                                     &gps_simulator,
                                                     &navigation_state,
                                                     &gps_sample,
                                                     &gps_sample_valid);
                            openride_route_destroy(&route);
                            route_valid = false;
                            loop_active = false;
                            gpx_navigation_active = false;
                            openride_navigation_session_reset(&navigation_session);
                            openride_location_filter_reset(&location_filter);
                            loop_waypoint_count = 0U;
                            route_dirty = openride_map_selection_complete(&selection);
                            if (!route_dirty) {
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "choisis un depart et une destination");
                            }
                        }
                    }
                    break;
                }

                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (event.button.which == SDL_TOUCH_MOUSEID) break;
                    if (place_search_active || app_panel != OPENRIDE_APP_PANEL_NONE) break;
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        if (dragging_marker != OPENRIDE_MARKER_NONE) {
                            dragging_marker = OPENRIDE_MARKER_NONE;
                            loop_active = false;
                            loop_waypoint_count = 0U;
                            route_dirty = openride_map_selection_complete(&selection);
                        } else if (dragging_map && !map_drag_moved) {
                            int width = 0;
                            int height = 0;
                            SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
                            add_selection_from_screen(&selection,
                                                      &camera,
                                                      (double)event.button.x,
                                                      (double)event.button.y,
                                                      width,
                                                      height,
                                                      &route_dirty,
                                                      &loop_active,
                                                      &loop_waypoint_count,
                                                      route_status,
                                                      sizeof(route_status));
                        }
                        dragging_map = false;
                        map_drag_moved = false;
                    }
                    break;

                case SDL_EVENT_MOUSE_MOTION:
                    if (event.motion.which == SDL_TOUCH_MOUSEID) break;
                    if (place_search_active || app_panel != OPENRIDE_APP_PANEL_NONE) break;
                    if (dragging_marker != OPENRIDE_MARKER_NONE) {
                        int width = 0;
                        int height = 0;
                        double lat = 0.0;
                        double lon = 0.0;
                        SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
                        openride_screen_to_geo(&camera,
                                               (double)event.motion.x,
                                               (double)event.motion.y,
                                               width,
                                               height,
                                               &lat,
                                               &lon);
                        openride_map_selection_set(&selection,
                                                   dragging_marker,
                                                   lat,
                                                   lon);
                    } else if (dragging_map) {
                        const double dx = (double)event.motion.x - mouse_down_x;
                        const double dy = (double)event.motion.y - mouse_down_y;
                        const double movement = sqrt(dx * dx + dy * dy);

                        if (movement >= OPENRIDE_CLICK_DRAG_THRESHOLD) {
                            map_drag_moved = true;
                        }

                        if (map_drag_moved) {
                            openride_camera_pan(&camera,
                                                (double)event.motion.xrel,
                                                (double)event.motion.yrel);
                        }
                    }
                    break;

                case SDL_EVENT_MOUSE_WHEEL: {
                    if (event.wheel.which == SDL_TOUCH_MOUSEID) break;
                    if (place_search_active || app_panel != OPENRIDE_APP_PANEL_NONE) break;
                    int width = 0;
                    int height = 0;
                    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);

                    const double requested_delta = (double)event.wheel.y * 0.5;
                    const double max_zoom = scalable_map ? 18.0 : (double)metadata->max_zoom;
                    const double min_zoom = map_world && scalable_map
                        ? OPENRIDE_MAP_WORLD_MIN_ZOOM
                        : (double)metadata->min_zoom;
                    const double target_zoom = clampd(
                        camera.zoom + requested_delta,
                        min_zoom,
                        max_zoom);

                    openride_camera_zoom_at(&camera,
                                            target_zoom - camera.zoom,
                                            (double)event.wheel.mouse_x,
                                            (double)event.wheel.mouse_y,
                                            width,
                                            height);
                    break;
                }

                case SDL_EVENT_FINGER_DOWN: {
                    int width = 0;
                    int height = 0;
                    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
                    const double x = (double)event.tfinger.x;
                    const double y = (double)event.tfinger.y;

#ifdef __ANDROID__
                    if (!place_search_active && app_panel != OPENRIDE_APP_PANEL_NONE) {
                        const uint32_t mobile_place_count =
                            app_panel == OPENRIDE_APP_PANEL_FAVORITES ? favorite_count
                            : app_panel == OPENRIDE_APP_PANEL_HISTORY ? history_count
                            : 0U;
                        const OpenRideMobilePanelHit mobile_hit =
                            app_panel == OPENRIDE_APP_PANEL_MAIN
                                ? mobile_main_menu_hit_test(renderer,
                                                            x,
                                                            y,
                                                            width,
                                                            height)
                            : app_panel == OPENRIDE_APP_PANEL_ROUTE
                                ? mobile_route_panel_hit_test(renderer,
                                                              x,
                                                              y,
                                                              width,
                                                              height)
                            : app_panel == OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS
                                ? mobile_route_downloads_panel_hit_test(renderer,
                                                                        x,
                                                                        y,
                                                                        width,
                                                                        height)
                            : app_panel == OPENRIDE_APP_PANEL_SETTINGS
                                ? mobile_settings_panel_hit_test(renderer,
                                                                 x,
                                                                 y,
                                                                 width,
                                                                 height)
                            : app_panel == OPENRIDE_APP_PANEL_REGIONS
                                ? mobile_regions_panel_hit_test(renderer,
                                                                x,
                                                                y,
                                                                width,
                                                                height)
                            : app_panel == OPENRIDE_APP_PANEL_FAVORITES
                              || app_panel == OPENRIDE_APP_PANEL_HISTORY
                                ? mobile_places_panel_hit_test(renderer,
                                                               x,
                                                               y,
                                                               width,
                                                               height,
                                                               mobile_place_count)
                                : (OpenRideMobilePanelHit){
                                      OPENRIDE_MOBILE_PANEL_NONE,
                                      -1
                                  };

                        if (mobile_hit.action == OPENRIDE_MOBILE_PANEL_CLOSE) {
                            app_panel = OPENRIDE_APP_PANEL_NONE;
                            app_panel_selected = 0U;
                        } else if (mobile_hit.action == OPENRIDE_MOBILE_PANEL_BACK) {
                            app_panel =
                                app_panel == OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS
                                    ? OPENRIDE_APP_PANEL_ROUTE
                                    : OPENRIDE_APP_PANEL_MAIN;
                            app_panel_selected = 0U;
                        } else if (mobile_hit.action == OPENRIDE_MOBILE_PANEL_SEARCH) {
                            app_panel = OPENRIDE_APP_PANEL_NONE;
                            place_search_purpose =
                                OPENRIDE_PLACE_SEARCH_BROWSE;
                            open_place_search(window,
                                              place_world,
                                              &place_search_active,
                                              place_search_query,
                                              &place_search_result_count,
                                              &place_search_selected,
                                              route_status,
                                              sizeof(route_status));
                        } else if (mobile_hit.action
                                   == OPENRIDE_MOBILE_PANEL_ROUTE_GPS_START) {
                            route_map_pick_marker = OPENRIDE_MARKER_NONE;
#ifdef __ANDROID__
                            if (gps_sample_valid) {
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
                                         "depart: position GPS actuelle");
                            } else {
                                real_gps_requested = true;
                                route_start_gps_pending = true;
                                if (!real_gps_active) {
                                    real_gps_active =
                                        openride_location_provider_start(
                                            &location_provider);
                                    android_gps_sample_age_s = INFINITY;
                                }
                                if (real_gps_active) {
                                    snprintf(route_status,
                                             sizeof(route_status),
                                             "recherche de la position GPS...");
                                } else {
                                    route_start_gps_pending = false;
                                    snprintf(route_status,
                                             sizeof(route_status),
                                             "GPS indisponible: autorise la localisation");
                                }
                            }
#endif
                        } else if (mobile_hit.action
                                   == OPENRIDE_MOBILE_PANEL_ROUTE_SEARCH_START) {
                            route_map_pick_marker = OPENRIDE_MARKER_NONE;
                            place_search_purpose =
                                OPENRIDE_PLACE_SEARCH_ROUTE_START;
                            app_panel = OPENRIDE_APP_PANEL_NONE;
                            open_place_search(window,
                                              place_world,
                                              &place_search_active,
                                              place_search_query,
                                              &place_search_result_count,
                                              &place_search_selected,
                                              route_status,
                                              sizeof(route_status));
                        } else if (mobile_hit.action
                                   == OPENRIDE_MOBILE_PANEL_ROUTE_MAP_START) {
                            route_map_pick_marker = OPENRIDE_MARKER_START;
                            app_panel = OPENRIDE_APP_PANEL_NONE;
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "Touchez la carte pour choisir le depart");
                        } else if (mobile_hit.action
                                   == OPENRIDE_MOBILE_PANEL_ROUTE_SEARCH_DESTINATION) {
                            route_map_pick_marker = OPENRIDE_MARKER_NONE;
                            place_search_purpose =
                                OPENRIDE_PLACE_SEARCH_ROUTE_DESTINATION;
                            app_panel = OPENRIDE_APP_PANEL_NONE;
                            open_place_search(window,
                                              place_world,
                                              &place_search_active,
                                              place_search_query,
                                              &place_search_result_count,
                                              &place_search_selected,
                                              route_status,
                                              sizeof(route_status));
                        } else if (mobile_hit.action
                                   == OPENRIDE_MOBILE_PANEL_ROUTE_MAP_DESTINATION) {
                            route_map_pick_marker =
                                OPENRIDE_MARKER_DESTINATION;
                            app_panel = OPENRIDE_APP_PANEL_NONE;
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "Touchez la carte pour choisir l'arrivee");
                        } else if (mobile_hit.action
                                   == OPENRIDE_MOBILE_PANEL_ROUTE_CALCULATE) {
                            if (openride_map_selection_complete(&selection)) {
                                app_panel = OPENRIDE_APP_PANEL_NONE;
                                route_dirty = true;
                            } else {
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "choisis un depart et une arrivee");
                            }
                        } else if (mobile_hit.action
                                   == OPENRIDE_MOBILE_PANEL_ROUTE_DOWNLOAD_REQUIRED) {
#ifdef __ANDROID__
                            if (route_download_plan.available
                                && !route_download_plan.downloading
                                && route_download_plan.count > 0U
                                && !region_busy) {
                                route_download_plan.downloading = true;
                                route_download_plan.index = 0U;
                                route_download_plan.selection = selection;
                                route_download_plan.profile = routing_profile;

                                const OpenRideRegionDefinition *required =
                                    openride_region_find(
                                        route_download_plan.region_ids[0]);
                                if (!required) {
                                    route_download_plan.downloading = false;
                                    snprintf(route_status,
                                             sizeof(route_status),
                                             "region requise inconnue");
                                } else {
                                    region = required;
                                    openride_region_get_status(
                                        &platform_paths,
                                        region,
                                        &region_status,
                                        error,
                                        sizeof(error));
                                    begin_android_region_install(
                                        &platform_paths,
                                        region,
                                        &region_status,
                                        &region_prepare_context,
                                        &region_prepare_thread,
                                        &region_download_started,
                                        &region_download_is_poly,
                                        &region_busy,
                                        &region_progress,
                                        region_work_status,
                                        sizeof(region_work_status),
                                        error,
                                        sizeof(error));
                                    if (!region_busy
                                        && !region_download_started
                                        && !region_prepare_thread) {
                                        route_download_plan.downloading = false;
                                    }
                                }
                            }
#endif
                        } else if (mobile_hit.action
                                   == OPENRIDE_MOBILE_PANEL_ROUTE_USE_INSTALLED) {
                            if (route_download_plan.available
                                && route_download_plan.has_installed_alternative
                                && !route_download_plan.downloading
                                && !region_busy
                                && !routing_world_thread) {
                                selection = route_download_plan.selection;
                                routing_profile = route_download_plan.profile;
                                app_panel = OPENRIDE_APP_PANEL_NONE;

                                routing_world_thread =
                                    start_routing_world_installed_alternative_thread(
                                        &routing_world_context,
                                        &platform_paths,
                                        active_region,
                                        graph_loaded ? &routing_graph : NULL,
                                        &routing_world_cache,
                                        &selection,
                                        routing_profile);

                                if (routing_world_thread) {
                                    snprintf(route_status,
                                             sizeof(route_status),
                                             "Calcul avec les cartes installees...");
                                } else {
                                    app_panel =
                                        OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS;
                                    snprintf(route_status,
                                             sizeof(route_status),
                                             "Impossible de lancer l'alternative");
                                }
                            }
                        } else if (mobile_hit.action == OPENRIDE_MOBILE_PANEL_FAVORITES) {
                            refresh_stored_places(app_storage, true, favorite_places, &favorite_count);
                            app_panel = OPENRIDE_APP_PANEL_FAVORITES;
                            app_panel_selected = 0U;
                        } else if (mobile_hit.action == OPENRIDE_MOBILE_PANEL_HISTORY) {
                            refresh_stored_places(app_storage, false, history_places, &history_count);
                            app_panel = OPENRIDE_APP_PANEL_HISTORY;
                            app_panel_selected = 0U;
                        } else if (mobile_hit.action == OPENRIDE_MOBILE_PANEL_REGIONS) {
                            openride_region_get_status(&platform_paths,
                                                       region,
                                                       &region_status,
                                                       error,
                                                       sizeof(error));
                            app_panel = OPENRIDE_APP_PANEL_REGIONS;
                            app_panel_selected = 0U;
                        } else if (mobile_hit.action == OPENRIDE_MOBILE_PANEL_SETTINGS) {
                            app_panel = OPENRIDE_APP_PANEL_SETTINGS;
                            app_panel_selected = 0U;
                        } else if (mobile_hit.action
                                   == OPENRIDE_MOBILE_PANEL_MAP_ZOOM_TEST) {
                            if (map_zoom_test.active) {
                                openride_map_zoom_test_cancel(&map_zoom_test);
                                snprintf(route_status, sizeof(route_status),
                                         "test zoom carte annule");
                            } else {
                                openride_map_zoom_test_start(&map_zoom_test, &camera, &platform_paths);
                                    map_zoom_loop_started_ns = SDL_GetTicksNS();
                                app_panel = OPENRIDE_APP_PANEL_NONE;
                                app_panel_selected = 0U;
                                snprintf(route_status, sizeof(route_status),
                                         "test zoom 9.000 -> 17.000 -> 9.000 | log data/map-zoom-test.csv");
                            }
                        } else if (mobile_hit.action == OPENRIDE_MOBILE_PANEL_PLACE
                                   && mobile_hit.index >= 0) {
                            const bool favorites_panel = app_panel == OPENRIDE_APP_PANEL_FAVORITES;
                            const uint32_t count = favorites_panel ? favorite_count : history_count;
                            OpenRideStoredPlace *items = favorites_panel ? favorite_places : history_places;
                            if ((uint32_t)mobile_hit.index < count) {
                                const OpenRideStoredPlace *chosen = &items[mobile_hit.index];
                                app_panel_selected = (uint32_t)mobile_hit.index;
                                camera.center_lat = chosen->lat;
                                camera.center_lon = chosen->lon;
                                if (camera.zoom < 14.0) camera.zoom = 14.0;
                                set_destination_from_place(&selection,
                                                           &gps_sample,
                                                           gps_sample_valid,
                                                           chosen->lat,
                                                           chosen->lon,
                                                           chosen->name,
                                                           &route_dirty,
                                                           route_status,
                                                           sizeof(route_status));
                                app_panel = OPENRIDE_APP_PANEL_NONE;
                            }
                        } else if (mobile_hit.action == OPENRIDE_MOBILE_PANEL_REGION_PREVIOUS
                                   || mobile_hit.action == OPENRIDE_MOBILE_PANEL_REGION_NEXT) {
                            if (!region_busy) {
                                region = region_step(region,
                                                     mobile_hit.action == OPENRIDE_MOBILE_PANEL_REGION_PREVIOUS ? -1 : 1);
                                openride_region_get_status(&platform_paths,
                                                           region,
                                                           &region_status,
                                                           error,
                                                           sizeof(error));
                                region_work_status[0] = '\0';
                            }
                        } else if (mobile_hit.action == OPENRIDE_MOBILE_PANEL_REGION_INSTALL) {
                            if (!region_busy) {
                                if (openride_region_status_ready(&region_status)
#ifdef __ANDROID__
                                    && region_status.poly_present
#endif
                                ) {
                                    if (region == active_region) {
                                        snprintf(region_work_status,
                                                 sizeof(region_work_status),
                                                 "Cette region est deja active");
                                    } else {
                                        region_activation_requested = true;
                                    }
                                } else {
                                    begin_android_region_install(&platform_paths,
                                                                 region,
                                                                 &region_status,
                                                                 &region_prepare_context,
                                                                 &region_prepare_thread,
                                                                 &region_download_started,
                                                                 &region_download_is_poly,
                                                                 &region_busy,
                                                                 &region_progress,
                                                                 region_work_status,
                                                                 sizeof(region_work_status),
                                                                 error,
                                                                 sizeof(error));
                                }
                            }
                        } else if (mobile_hit.action == OPENRIDE_MOBILE_PANEL_REGION_REMOVE) {
                            if (!region_busy) {
                                if (region == active_region) {
                                    snprintf(region_work_status,
                                             sizeof(region_work_status),
                                             "Impossible de supprimer la region active");
                                } else if (openride_region_remove_generated(&platform_paths,
                                                                            region,
                                                                            error,
                                                                            sizeof(error))) {
                                    openride_region_get_status(&platform_paths,
                                                               region,
                                                               &region_status,
                                                               error,
                                                               sizeof(error));
                                    refresh_map_world_overview(map_world, &platform_paths);
                                    snprintf(region_work_status,
                                             sizeof(region_work_status),
                                             "Donnees de la region supprimees");
                                } else {
                                    snprintf(region_work_status,
                                             sizeof(region_work_status),
                                             "Suppression impossible: %.120s",
                                             error);
                                }
                            }
                        } else if (mobile_hit.action == OPENRIDE_MOBILE_PANEL_SETTINGS_STYLE) {
                            if (scalable_map) {
                                map_style = openride_map_style_next(map_style);
                                if (ormap_map) openride_ormap_renderer_set_style(&ormap_renderer, map_style);
                                else if (vector_map) openride_vector_map_renderer_set_style(&vector_renderer, map_style);
                                if (app_storage) {
                                    openride_app_storage_set_int(app_storage,
                                                                 "map_style",
                                                                 (int)map_style,
                                                                 error,
                                                                 sizeof(error));
                                }
                            }
                        } else if (mobile_hit.action == OPENRIDE_MOBILE_PANEL_SETTINGS_PROFILE) {
                            routing_profile = routing_profile == OPENRIDE_ROUTING_PROFILE_FASTEST
                                ? OPENRIDE_ROUTING_PROFILE_TOURING
                                : routing_profile == OPENRIDE_ROUTING_PROFILE_TOURING
                                    ? OPENRIDE_ROUTING_PROFILE_TRAIL
                                    : OPENRIDE_ROUTING_PROFILE_FASTEST;
                            if (app_storage) {
                                openride_app_storage_set_int(app_storage,
                                                             "routing_profile",
                                                             (int)routing_profile,
                                                             error,
                                                             sizeof(error));
                            }
                            if (!gpx_navigation_active) {
                                if (loop_active) {
                                    clear_navigation_session(&navigation,
                                                             &gps_simulator,
                                                             &navigation_state,
                                                             &gps_sample,
                                                             &gps_sample_valid);
                                    openride_navigation_session_reset(&navigation_session);
                                    openride_location_filter_reset(&location_filter);
                                    openride_route_destroy(&route);
                                    route_valid = false;
                                    loop_active = false;
                                    loop_waypoint_count = 0U;
                                    snprintf(route_status,
                                             sizeof(route_status),
                                             "profil %s | regenere la boucle",
                                             openride_routing_profile_name(routing_profile));
                                } else if (openride_map_selection_complete(&selection)) {
                                    route_dirty = true;
                                }
                            }
                        } else if (mobile_hit.action == OPENRIDE_MOBILE_PANEL_SETTINGS_FOLLOW) {
                            follow_gps = !follow_gps;
                            if (app_storage) {
                                openride_app_storage_set_int(app_storage,
                                                             "follow_gps",
                                                             follow_gps ? 1 : 0,
                                                             error,
                                                             sizeof(error));
                            }
                        } else if (mobile_hit.action == OPENRIDE_MOBILE_PANEL_SETTINGS_REROUTE) {
                            auto_reroute = !auto_reroute;
                            openride_navigation_session_set_auto_reroute(&navigation_session, auto_reroute);
                            if (app_storage) {
                                openride_app_storage_set_int(app_storage,
                                                             "auto_reroute",
                                                             auto_reroute ? 1 : 0,
                                                             error,
                                                             sizeof(error));
                            }
                        } else if (mobile_hit.action == OPENRIDE_MOBILE_PANEL_SETTINGS_VOICE) {
                            voice_enabled = !voice_enabled;
                            openride_voice_guidance_set_enabled(&voice_guidance,
                                                                voice_enabled);
                            if (voice_enabled) {
                                openride_android_voice_guidance_init();
                            }
                            if (app_storage) {
                                openride_app_storage_set_int(app_storage,
                                                             "voice_enabled",
                                                             voice_enabled ? 1 : 0,
                                                             error,
                                                             sizeof(error));
                            }
                        } else if (mobile_hit.action
                                   == OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_SIMULATION) {
                            if (simulated_gps_active) {
                                openride_location_provider_stop(
                                    &simulated_location_provider);
                                simulated_gps_active = false;
                                simulator_deviation = false;
                                openride_gps_simulator_set_lateral_offset_m(
                                    &gps_simulator, 0.0);
                                openride_drive_mode_set_active(&drive_mode, false);
                                camera.bearing_deg = 0.0;
                                gps_sample_valid = false;
                                memset(&gps_sample, 0, sizeof(gps_sample));
                                memset(&navigation_state, 0, sizeof(navigation_state));
                                memset(&filtered_location, 0, sizeof(filtered_location));
                                openride_location_filter_reset(&location_filter);
                                openride_voice_guidance_reset(&voice_guidance);
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "simulation GPS [DEV] arretee");
                            } else if (!route_valid || !gps_simulator.route) {
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "calcule un itineraire avant la simulation GPS");
                            } else {
                                real_gps_requested = false;
                                if (real_gps_active) {
                                    openride_location_provider_stop(
                                        &location_provider);
                                    real_gps_active = false;
                                }

                                openride_navigation_session_reset(
                                    &navigation_session);
                                openride_navigation_session_set_auto_reroute(
                                    &navigation_session,
                                    auto_reroute);
                                openride_location_filter_reset(&location_filter);
                                memset(&filtered_location, 0, sizeof(filtered_location));
                                memset(&navigation_state, 0, sizeof(navigation_state));
                                memset(&gps_sample, 0, sizeof(gps_sample));
                                gps_sample_valid = false;

                                simulator_deviation = false;
                                openride_gps_simulator_set_lateral_offset_m(
                                    &gps_simulator, 0.0);
                                openride_android_missed_turn_dev_reset(
                                    &missed_turn_dev,
                                    &simulated_location_context,
                                    &gps_simulator);
                                simulated_gps_active =
                                    openride_location_provider_start(
                                        &simulated_location_provider);
                                if (simulated_gps_active) {
                                    android_gps_sample_age_s = INFINITY;
                                    android_gps_accuracy_m = 3.0;
                                    openride_drive_mode_set_active(
                                        &drive_mode, true);
                                    openride_drive_mode_set_auto_zoom(
                                        &drive_mode, true);
                                    follow_gps = true;
                                    openride_voice_guidance_reset(
                                        &voice_guidance);
                                    app_panel = OPENRIDE_APP_PANEL_NONE;
                                    snprintf(route_status,
                                             sizeof(route_status),
                                             "SIMULATION GPS [DEV] x%.0f",
                                             simulated_location_context.time_scale);
                                } else {
                                    snprintf(route_status,
                                             sizeof(route_status),
                                             "simulation GPS indisponible");
                                }
                            }
                        } else if (mobile_hit.action
                                   == OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_DEVIATION) {
                            if (missed_turn_dev.armed
                                || missed_turn_dev.active) {
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "annule d'abord Virage rate reel [DEV]");
                            } else if (!simulated_gps_active
                                || !route_valid
                                || !gps_simulator.route) {
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "active d'abord le GPS simule [DEV]");
                            } else if (!auto_reroute) {
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "active Recalcul auto avant le test DEV");
                            } else if (simulator_deviation) {
                                simulator_deviation = false;
                                openride_gps_simulator_set_lateral_offset_m(
                                    &gps_simulator, 0.0);
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "deviation GPS [DEV] annulee");
                            } else {
                                simulator_deviation = true;
                                openride_gps_simulator_set_lateral_offset_m(
                                    &gps_simulator, 80.0);
                                openride_drive_mode_set_active(&drive_mode, true);
                                openride_drive_mode_set_auto_zoom(&drive_mode, true);
                                follow_gps = true;
                                openride_voice_guidance_reset(&voice_guidance);
                                app_panel = OPENRIDE_APP_PANEL_NONE;
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "DEV +80 m: attente hors itineraire / recalcul");
                            }
                        } else if (mobile_hit.action
                                   == OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_SPEED) {
                            const double current =
                                simulated_location_context.time_scale;
                            const double next =
                                current < 1.5 ? 2.0
                                : current < 3.5 ? 5.0
                                : 1.0;
                            openride_simulated_location_provider_set_time_scale(
                                &simulated_location_context,
                                next);
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "vitesse simulation [DEV] x%.0f",
                                     simulated_location_context.time_scale);
                        } else if (mobile_hit.action
                                   == OPENRIDE_MOBILE_PANEL_SETTINGS_GPS_MISSED_TURN) {
                            if (missed_turn_dev.armed
                                || missed_turn_dev.active) {
                                openride_android_missed_turn_dev_reset(
                                    &missed_turn_dev,
                                    &simulated_location_context,
                                    &gps_simulator);
                                openride_location_filter_reset(&location_filter);
                                memset(&filtered_location,
                                       0,
                                       sizeof(filtered_location));
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "virage rate reel [DEV] annule");
                            } else if (!simulated_gps_active
                                       || !route_valid
                                       || !gps_simulator.route) {
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "active d'abord le GPS simule [DEV]");
                            } else if (!auto_reroute) {
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "active Recalcul auto avant le test DEV");
                            } else if (simulator_deviation) {
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "annule d'abord Deviation 80 m [DEV]");
                            } else if (!graph_loaded
                                       || !route.nodes
                                       || route.node_count < 3U) {
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "virage rate reel: itineraire mono-region requis");
                            } else if (!openride_dev_missed_turn_plan_build(
                                           &routing_graph,
                                           &route,
                                           gps_simulator.position_m,
                                           100.0,
                                           2500.0,
                                           80.0,
                                           &missed_turn_dev.plan,
                                           error,
                                           sizeof(error))) {
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "virage rate DEV impossible: %.140s",
                                         error[0] ? error : "aucune branche adaptee");
                            } else {
                                const double speed_kph =
                                    gps_simulator.speed_mps > 0.1
                                        ? gps_simulator.speed_mps * 3.6
                                        : 60.0;
                                if (!openride_gps_simulator_set_route(
                                        &missed_turn_dev.simulator,
                                        &missed_turn_dev.plan.branch_route,
                                        speed_kph,
                                        error,
                                        sizeof(error))) {
                                    openride_dev_missed_turn_plan_destroy(
                                        &missed_turn_dev.plan);
                                    snprintf(route_status,
                                             sizeof(route_status),
                                             "simulateur virage rate indisponible: %.120s",
                                             error[0] ? error : "erreur");
                                } else {
                                    missed_turn_dev.armed = true;
                                    missed_turn_dev.active = false;
                                    openride_drive_mode_set_active(
                                        &drive_mode, true);
                                    openride_drive_mode_set_auto_zoom(
                                        &drive_mode, true);
                                    follow_gps = true;
                                    app_panel = OPENRIDE_APP_PANEL_NONE;
                                    snprintf(
                                        route_status,
                                        sizeof(route_status),
                                        "virage rate [DEV] arme dans %.0f m",
                                        missed_turn_dev.plan.trigger_position_m
                                            - gps_simulator.position_m);
                                }
                            }
                        }
                        break;
                    }
#endif

                    if (place_search_active) {
                        const int result = place_search_result_at(renderer,
                                                                 x,
                                                                 y,
                                                                 width,
                                                                 height,
                                                                 place_search_result_count);
                        if (result >= 0) {
                            place_search_selected = (uint32_t)result;
                            const OpenRidePlaceSearchResult *chosen = &place_search_results[place_search_selected];
                            camera.center_lat = chosen->lat;
                            camera.center_lon = chosen->lon;
                            if (camera.zoom < 14.0) camera.zoom = 14.0;
                            if (app_storage) {
                                openride_app_storage_add_history(app_storage,
                                                                 chosen->name,
                                                                 chosen->lat,
                                                                 chosen->lon,
                                                                 (int)chosen->kind,
                                                                 error,
                                                                 sizeof(error));
                                refresh_stored_places(app_storage, false, history_places, &history_count);
                            }
                            if (place_search_purpose
                                == OPENRIDE_PLACE_SEARCH_ROUTE_START) {
                                openride_map_selection_set(&selection,
                                                           OPENRIDE_MARKER_START,
                                                           chosen->lat,
                                                           chosen->lon);
                                openride_map_selection_set_region_hint(
                                    &selection,
                                    OPENRIDE_MARKER_START,
                                    chosen->region_id);
                                start_snap.segment_id =
                                    OPENRIDE_ROUTING_SEGMENT_NONE;
                                openride_route_destroy(&route);
                                route_valid = false;
                                route_dirty = false;
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "depart: %.120s",
                                         chosen->name);
                                app_panel = OPENRIDE_APP_PANEL_ROUTE;
                            } else if (place_search_purpose
                                       == OPENRIDE_PLACE_SEARCH_ROUTE_DESTINATION) {
                                openride_map_selection_set(&selection,
                                                           OPENRIDE_MARKER_DESTINATION,
                                                           chosen->lat,
                                                           chosen->lon);
                                openride_map_selection_set_region_hint(
                                    &selection,
                                    OPENRIDE_MARKER_DESTINATION,
                                    chosen->region_id);
                                destination_snap.segment_id =
                                    OPENRIDE_ROUTING_SEGMENT_NONE;
                                openride_route_destroy(&route);
                                route_valid = false;
                                route_dirty = false;
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "arrivee: %.120s",
                                         chosen->name);
                                app_panel = OPENRIDE_APP_PANEL_ROUTE;
                            } else {
                                set_destination_from_place(&selection,
                                                           &gps_sample,
                                                           gps_sample_valid,
                                                           chosen->lat,
                                                           chosen->lon,
                                                           chosen->name,
                                                           &route_dirty,
                                                           route_status,
                                                           sizeof(route_status));
                            }
                            place_search_active = false;
                            place_search_purpose =
                                OPENRIDE_PLACE_SEARCH_BROWSE;
                            SDL_StopTextInput(window);
                        }
                        break;
                    }

                    if (app_panel == OPENRIDE_APP_PANEL_MAIN) {
                        if (app_panel_main_search_at(x, y, width)) {
                            app_panel = OPENRIDE_APP_PANEL_NONE;
                            open_place_search(window,
                                              place_world,
                                              &place_search_active,
                                              place_search_query,
                                              &place_search_result_count,
                                              &place_search_selected,
                                              route_status,
                                              sizeof(route_status));
                        } else {
                            const OpenRideAppPanel selected_panel = app_panel_main_at(x, y, width);
                            if (selected_panel != OPENRIDE_APP_PANEL_NONE) {
                                app_panel = selected_panel;
                                app_panel_selected = 0U;
                                if (selected_panel == OPENRIDE_APP_PANEL_FAVORITES) {
                                    refresh_stored_places(app_storage, true, favorite_places, &favorite_count);
                                } else if (selected_panel == OPENRIDE_APP_PANEL_HISTORY) {
                                    refresh_stored_places(app_storage, false, history_places, &history_count);
                                }
                            }
                        }
                        break;
                    }
                    if (app_panel == OPENRIDE_APP_PANEL_FAVORITES
                        || app_panel == OPENRIDE_APP_PANEL_HISTORY) {
                        const bool favorites_panel = app_panel == OPENRIDE_APP_PANEL_FAVORITES;
                        const uint32_t count = favorites_panel ? favorite_count : history_count;
                        OpenRideStoredPlace *items = favorites_panel ? favorite_places : history_places;
                        const int chosen_index = app_panel_place_at(x, y, width, count);
                        if (chosen_index >= 0) {
                            const OpenRideStoredPlace *chosen = &items[chosen_index];
                            app_panel_selected = (uint32_t)chosen_index;
                            camera.center_lat = chosen->lat;
                            camera.center_lon = chosen->lon;
                            if (camera.zoom < 14.0) camera.zoom = 14.0;
                            set_destination_from_place(&selection,
                                                       &gps_sample,
                                                       gps_sample_valid,
                                                       chosen->lat,
                                                       chosen->lon,
                                                       chosen->name,
                                                       &route_dirty,
                                                       route_status,
                                                       sizeof(route_status));
                            app_panel = OPENRIDE_APP_PANEL_NONE;
                        }
                        break;
                    }
                    if (app_panel == OPENRIDE_APP_PANEL_REGIONS) {
                        const int region_action = app_panel_region_action_at(x, y, width);
                        if (region_action == 1 && !region_busy) {
                            if (openride_region_status_ready(&region_status)
#ifdef __ANDROID__
                                    && region_status.poly_present
#endif
                                ) {
                                if (region == active_region) {
                                    snprintf(region_work_status, sizeof(region_work_status),
                                             "Cette region est deja active");
                                } else {
                                    region_activation_requested = true;
                                }
                            } else {
#ifdef __ANDROID__
                                begin_android_region_install(&platform_paths,
                                                             region,
                                                             &region_status,
                                                             &region_prepare_context,
                                                             &region_prepare_thread,
                                                             &region_download_started,
                                                             &region_download_is_poly,
                                                             &region_busy,
                                                             &region_progress,
                                                             region_work_status,
                                                             sizeof(region_work_status),
                                                             error,
                                                             sizeof(error));
#else
                                snprintf(region_work_status, sizeof(region_work_status),
                                         "Preparation depuis le Terminal sur macOS");
#endif
                            }
                        } else if (region_action == 2 && !region_busy) {
                            if (region == active_region) {
                                snprintf(region_work_status, sizeof(region_work_status),
                                         "Impossible de supprimer la region active");
                            } else if (openride_region_remove_generated(&platform_paths,
                                                                        region,
                                                                        error,
                                                                        sizeof(error))) {
                                openride_region_get_status(&platform_paths, region,
                                                           &region_status, error, sizeof(error));
                                refresh_map_world_overview(map_world, &platform_paths);
                                snprintf(region_work_status, sizeof(region_work_status),
                                         "Donnees de la region supprimees");
                            } else {
                                snprintf(region_work_status, sizeof(region_work_status),
                                         "Suppression impossible: %.120s", error);
                            }
                        }
                        break;
                    }
                    if (app_panel != OPENRIDE_APP_PANEL_NONE) break;

                    if (drive_mode.active) {
                        const OpenRideDriveAction drive_action = drive_controls_hit_test(
                            renderer, x, y, width, height);
                        if (drive_action != OPENRIDE_DRIVE_ACTION_NONE) {
                            pending_drive_action = drive_action;
                            openride_touch_input_cancel(&touch_input);
                            break;
                        }
                    } else {
                        const OpenRideToolbarAction toolbar_action = mobile_toolbar_hit_test(
                            renderer, x, y, width, height);
                        if (toolbar_action != OPENRIDE_TOOLBAR_NONE) {
                            route_map_pick_marker = OPENRIDE_MARKER_NONE;
                            pending_toolbar_action = toolbar_action;
                            openride_touch_input_cancel(&touch_input);
                            break;
                        }
                    }

                    /*
                     * Android route-point editing mirrors the desktop mouse
                     * interaction: touch a marker to edit it. A tap deletes
                     * it; a drag moves it. Route calculation is restarted only
                     * after the finger is released.
                     */
                    dragging_marker =
                        (route_map_pick_marker != OPENRIDE_MARKER_NONE
                         || drive_mode.active)
                            ? OPENRIDE_MARKER_NONE
                            : marker_at_screen(&camera,
                                           &selection,
                                           x,
                                           y,
                                           width,
                                           height);
                    if (dragging_marker != OPENRIDE_MARKER_NONE) {
                        clear_navigation_session(&navigation,
                                                 &gps_simulator,
                                                 &navigation_state,
                                                 &gps_sample,
                                                 &gps_sample_valid);
                        openride_route_destroy(&route);
                        route_valid = false;
                        route_dirty = false;
                        loop_active = false;
                        gpx_navigation_active = false;
                        openride_navigation_session_reset(&navigation_session);
                        openride_location_filter_reset(&location_filter);
                        loop_waypoint_count = 0U;
                    }

                    openride_touch_input_begin(&touch_input,
                                               (uint64_t)event.tfinger.fingerID,
                                               x,
                                               y);
                    break;
                }

                case SDL_EVENT_FINGER_MOTION: {
                    if (place_search_active || app_panel != OPENRIDE_APP_PANEL_NONE) break;
                    int width = 0;
                    int height = 0;
                    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
                    const double x = (double)event.tfinger.x;
                    const double y = (double)event.tfinger.y;
                    const OpenRideTouchAction action = openride_touch_input_motion(
                        &touch_input,
                        (uint64_t)event.tfinger.fingerID,
                        x,
                        y);
                    if (dragging_marker != OPENRIDE_MARKER_NONE) {
                        if (action.type == OPENRIDE_TOUCH_ACTION_PAN) {
                            double lat = 0.0;
                            double lon = 0.0;
                            openride_screen_to_geo(&camera,
                                                   x,
                                                   y,
                                                   width,
                                                   height,
                                                   &lat,
                                                   &lon);
                            openride_map_selection_set(&selection,
                                                       dragging_marker,
                                                       lat,
                                                       lon);
                        }
                    } else if (action.type == OPENRIDE_TOUCH_ACTION_PAN) {
                        openride_camera_pan(&camera, action.dx, action.dy);
                        if (drive_mode.active) {
                            follow_gps = false;
                            openride_drive_mode_set_auto_zoom(&drive_mode, false);
                        }
                    }
                    break;
                }

                case SDL_EVENT_FINGER_UP: {
                    if (place_search_active || app_panel != OPENRIDE_APP_PANEL_NONE) {
                        openride_touch_input_cancel(&touch_input);
                        break;
                    }
                    int width = 0;
                    int height = 0;
                    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
                    const double x = (double)event.tfinger.x;
                    const double y = (double)event.tfinger.y;
                    const OpenRideTouchAction action = openride_touch_input_end(
                        &touch_input,
                        (uint64_t)event.tfinger.fingerID,
                        x,
                        y);

                    if (dragging_marker != OPENRIDE_MARKER_NONE) {
                        const OpenRideSelectionMarker edited_marker = dragging_marker;
                        dragging_marker = OPENRIDE_MARKER_NONE;

                        if (action.type == OPENRIDE_TOUCH_ACTION_TAP) {
                            openride_map_selection_remove(&selection, edited_marker);
                            if (edited_marker == OPENRIDE_MARKER_START) {
                                start_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                            } else {
                                destination_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                            }
                            route_dirty = openride_map_selection_complete(&selection);
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "%s supprime - touche la carte pour le replacer",
                                     edited_marker == OPENRIDE_MARKER_START
                                         ? "Depart"
                                         : "Destination");
                        } else {
                            double lat = 0.0;
                            double lon = 0.0;
                            openride_screen_to_geo(&camera,
                                                   x,
                                                   y,
                                                   width,
                                                   height,
                                                   &lat,
                                                   &lon);
                            openride_map_selection_set(&selection,
                                                       edited_marker,
                                                       lat,
                                                       lon);
                            if (edited_marker == OPENRIDE_MARKER_START) {
                                start_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                            } else {
                                destination_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                            }
                            route_dirty = openride_map_selection_complete(&selection);
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "%s deplace%s",
                                     edited_marker == OPENRIDE_MARKER_START
                                         ? "Depart"
                                         : "Destination",
                                     route_dirty ? " - recalcul..." : "");
                        }

                        loop_active = false;
                        loop_waypoint_count = 0U;
                    } else if (action.type == OPENRIDE_TOUCH_ACTION_TAP
                               && !drive_mode.active) {
                        if (route_map_pick_marker != OPENRIDE_MARKER_NONE) {
                            double picked_lat = 0.0;
                            double picked_lon = 0.0;
                            openride_screen_to_geo(&camera,
                                                   action.x,
                                                   action.y,
                                                   width,
                                                   height,
                                                   &picked_lat,
                                                   &picked_lon);

                            const OpenRideSelectionMarker picked_marker =
                                route_map_pick_marker;
                            route_map_pick_marker = OPENRIDE_MARKER_NONE;

                            openride_map_selection_set(&selection,
                                                       picked_marker,
                                                       picked_lat,
                                                       picked_lon);
                            if (picked_marker == OPENRIDE_MARKER_START) {
                                start_snap.segment_id =
                                    OPENRIDE_ROUTING_SEGMENT_NONE;
                            } else {
                                destination_snap.segment_id =
                                    OPENRIDE_ROUTING_SEGMENT_NONE;
                            }

                            clear_navigation_session(&navigation,
                                                     &gps_simulator,
                                                     &navigation_state,
                                                     &gps_sample,
                                                     &gps_sample_valid);
                            openride_navigation_session_reset(
                                &navigation_session);
                            openride_location_filter_reset(&location_filter);
                            openride_route_destroy(&route);
                            route_valid = false;
                            route_dirty = false;
                            loop_active = false;
                            gpx_navigation_active = false;
                            loop_waypoint_count = 0U;

                            snprintf(route_status,
                                     sizeof(route_status),
                                     picked_marker == OPENRIDE_MARKER_START
                                         ? "Depart choisi sur la carte"
                                         : "Arrivee choisie sur la carte");
                            app_panel = OPENRIDE_APP_PANEL_ROUTE;
                            app_panel_selected = 0U;
                        } else {
                            add_selection_from_screen(&selection,
                                                      &camera,
                                                      action.x,
                                                      action.y,
                                                      width,
                                                      height,
                                                      &route_dirty,
                                                      &loop_active,
                                                      &loop_waypoint_count,
                                                      route_status,
                                                      sizeof(route_status));
                        }
                    }
                    break;
                }

                case SDL_EVENT_FINGER_CANCELED:
                    if (dragging_marker != OPENRIDE_MARKER_NONE) {
                        dragging_marker = OPENRIDE_MARKER_NONE;
                        route_dirty = openride_map_selection_complete(&selection);
                    }
                    openride_touch_input_cancel(&touch_input);
                    break;

                case SDL_EVENT_PINCH_BEGIN:
                    if (dragging_marker != OPENRIDE_MARKER_NONE) {
                        dragging_marker = OPENRIDE_MARKER_NONE;
                        route_dirty = openride_map_selection_complete(&selection);
                    }
                    openride_touch_input_cancel(&touch_input);
                    break;

                case SDL_EVENT_PINCH_UPDATE: {
                    if (place_search_active || app_panel != OPENRIDE_APP_PANEL_NONE) break;
                    int width = 0;
                    int height = 0;
                    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);                    const double max_zoom = scalable_map ? 18.0 : (double)metadata->max_zoom;
                    const double min_zoom = map_world && scalable_map
                        ? OPENRIDE_MAP_WORLD_MIN_ZOOM
                        : (double)metadata->min_zoom;
                    if (drive_mode.active) {
                        openride_drive_mode_set_auto_zoom(&drive_mode, false);
                    }
                    double zoom_delta = openride_touch_pinch_zoom_delta((double)event.pinch.scale);
                    zoom_delta = clampd(zoom_delta, -1.0, 1.0);
                    const double target_zoom = clampd(camera.zoom + zoom_delta,
                                                       min_zoom,
                                                       max_zoom);
                    const double focus_x = (double)width * 0.5;
                    const double focus_y = (double)height * 0.5;
                    openride_camera_zoom_at(&camera,
                                            target_zoom - camera.zoom,
                                            focus_x,
                                            focus_y,
                                            width,
                                            height);
                    break;
                }

                default:
                    break;
            }
        }

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
        } else if (lifecycle_signal == OPENRIDE_LIFECYCLE_SIGNAL_TERMINATING) {
            running = false;
        }

        if (!running) break;
        if (render_suspended || app_lifecycle.in_background) {
            SDL_Delay(50);
            continue;
        }

        if (pending_drive_action != OPENRIDE_DRIVE_ACTION_NONE) {
            const OpenRideDriveAction action = pending_drive_action;
            pending_drive_action = OPENRIDE_DRIVE_ACTION_NONE;
            if (action == OPENRIDE_DRIVE_ACTION_EXIT) {
                openride_drive_mode_set_active(&drive_mode, false);
                camera.bearing_deg = 0.0;
                follow_gps = false;
                snprintf(route_status, sizeof(route_status), "mode conduite ferme");
            } else if (action == OPENRIDE_DRIVE_ACTION_RECENTER) {
                follow_gps = true;
                openride_drive_mode_set_auto_zoom(&drive_mode, true);
                snprintf(route_status, sizeof(route_status), "suivi GPS actif");
            } else if (action == OPENRIDE_DRIVE_ACTION_ORIENTATION) {
                openride_drive_mode_set_heading_up(&drive_mode, !drive_mode.heading_up);
                if (!drive_mode.heading_up) camera.bearing_deg = 0.0;
                snprintf(route_status,
                         sizeof(route_status),
                         "orientation %s",
                         drive_mode.heading_up ? "cap en haut" : "nord en haut");
            } else if (action == OPENRIDE_DRIVE_ACTION_GPS) {
#ifdef __ANDROID__
                real_gps_requested = false;
                if (simulated_gps_active) {
                    openride_location_provider_stop(
                        &simulated_location_provider);
                    simulated_gps_active = false;
                    simulator_deviation = false;
                    openride_gps_simulator_set_lateral_offset_m(
                        &gps_simulator, 0.0);
                }
                if (real_gps_active) {
                    openride_location_provider_stop(&location_provider);
                    real_gps_active = false;
                }
#else
                openride_gps_simulator_stop(&gps_simulator);
#endif
                openride_drive_mode_set_active(&drive_mode, false);
                camera.bearing_deg = 0.0;
                snprintf(route_status, sizeof(route_status), "GPS arrete");
            }
        }

        if (pending_toolbar_action != OPENRIDE_TOOLBAR_NONE) {
            const OpenRideToolbarAction action = pending_toolbar_action;
            pending_toolbar_action = OPENRIDE_TOOLBAR_NONE;
            if (action == OPENRIDE_TOOLBAR_MENU) {
                app_panel = OPENRIDE_APP_PANEL_MAIN;
                app_panel_selected = 0U;
            } else if (action == OPENRIDE_TOOLBAR_SEARCH) {
                place_search_purpose =
                    OPENRIDE_PLACE_SEARCH_BROWSE;
                open_place_search(window,
                                  place_world,
                                  &place_search_active,
                                  place_search_query,
                                  &place_search_result_count,
                                  &place_search_selected,
                                  route_status,
                                  sizeof(route_status));
            } else if (action == OPENRIDE_TOOLBAR_ROUTE) {
#ifdef __ANDROID__
                if (route_valid) {
                    if (simulated_gps_active) {
                        openride_drive_mode_set_active(&drive_mode, true);
                        openride_drive_mode_set_auto_zoom(&drive_mode, true);
                        follow_gps = true;
                        snprintf(route_status,
                                 sizeof(route_status),
                                 "navigation GPS simulee [DEV]");
                    } else {
                        real_gps_requested = true;
                        if (!real_gps_active) {
                            real_gps_active =
                                openride_location_provider_start(
                                    &location_provider);
                            android_gps_sample_age_s = INFINITY;
                        }
                        if (real_gps_active) {
                            openride_drive_mode_set_active(&drive_mode, true);
                            openride_drive_mode_set_auto_zoom(&drive_mode, true);
                            follow_gps = true;
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "navigation demarree");
                        } else {
                            snprintf(route_status, sizeof(route_status),
                                     "autorise la localisation Android puis retouche Demarrer");
                        }
                    }
                } else {
                    app_panel = OPENRIDE_APP_PANEL_ROUTE;
                    app_panel_selected = 0U;
                }
#else
                app_panel = OPENRIDE_APP_PANEL_ROUTE;
                app_panel_selected = 0U;
#endif
            } else if (action == OPENRIDE_TOOLBAR_LOOP) {
                if (!selection.has_start) {
                    snprintf(route_status, sizeof(route_status), "choisis d'abord le depart de la boucle");
                } else {
                    if (selection.has_destination) {
                        openride_map_selection_remove(&selection, OPENRIDE_MARKER_DESTINATION);
                    }
                    destination_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                    route_dirty = false;
                    clear_navigation_session(&navigation,
                                             &gps_simulator,
                                             &navigation_state,
                                             &gps_sample,
                                             &gps_sample_valid);
                    simulator_deviation = false;
                    gpx_navigation_active = false;
                    openride_navigation_session_reset(&navigation_session);
                    openride_location_filter_reset(&location_filter);
                    memset(&filtered_location, 0, sizeof(filtered_location));
                    route_valid = generate_loop_route(&routing_graph,
                                                      graph_loaded,
                                                      &selection,
                                                      routing_profile,
                                                      loop_target_distance_m,
                                                      loop_direction,
                                                      loop_seed++,
                                                      &route,
                                                      &loop_stats,
                                                      loop_waypoints,
                                                      &loop_waypoint_count,
                                                      &start_snap,
                                                      route_status,
                                                      sizeof(route_status));
                    loop_active = route_valid;
                    if (route_valid) {
                        prepare_navigation_session(&navigation,
                                                   &gps_simulator,
                                                   &navigation_instructions,
                                                   &routing_graph,
                                                   &route,
                                                   route_status,
                                                   sizeof(route_status));
                    }
                }
            } else if (action == OPENRIDE_TOOLBAR_GPS) {
#ifdef __ANDROID__
                if (simulated_gps_active) {
                    openride_location_provider_stop(
                        &simulated_location_provider);
                    simulated_gps_active = false;
                    simulator_deviation = false;
                    openride_gps_simulator_set_lateral_offset_m(
                        &gps_simulator, 0.0);
                    openride_drive_mode_set_active(&drive_mode, false);
                    camera.bearing_deg = 0.0;
                    gps_sample_valid = false;
                    memset(&filtered_location, 0, sizeof(filtered_location));
                    snprintf(route_status,
                             sizeof(route_status),
                             "simulation GPS [DEV] arretee");
                } else if (real_gps_active) {
                    if (route_valid) {
                        openride_drive_mode_set_active(&drive_mode, true);
                        openride_drive_mode_set_auto_zoom(&drive_mode, true);
                        follow_gps = true;
                        snprintf(route_status, sizeof(route_status), "mode conduite actif");
                    } else {
                        real_gps_requested = false;
                        openride_location_provider_stop(&location_provider);
                        real_gps_active = false;
                        openride_drive_mode_set_active(&drive_mode, false);
                        camera.bearing_deg = 0.0;
                        snprintf(route_status, sizeof(route_status), "GPS reel arrete");
                    }
                } else {
                    real_gps_requested = true;
                    if (openride_location_provider_start(&location_provider)) {
                        real_gps_active = true;
                        android_gps_sample_age_s = INFINITY;
                        if (route_valid) {
                            openride_drive_mode_set_active(&drive_mode, true);
                            openride_drive_mode_set_auto_zoom(&drive_mode, true);
                            follow_gps = true;
                        }
                        snprintf(route_status,
                                 sizeof(route_status),
                                 route_valid ? "GPS reel + mode conduite" : "GPS reel actif");
                    } else {
                        snprintf(route_status, sizeof(route_status),
                                 "autorise la localisation Android puis retouche GPS");
                    }
                }
#else
                if (route_valid && gps_simulator.route) {
                    const bool active = openride_gps_simulator_toggle(&gps_simulator);
                    snprintf(route_status,
                             sizeof(route_status),
                             "simulation GPS %s",
                             active ? "en cours" : "en pause");
                } else {
                    snprintf(route_status, sizeof(route_status), "calcule un itineraire avant le GPS");
                }
#endif
            }
        }

#ifdef __ANDROID__
        if (region_download_started) {
            if (!openride_android_region_download_poll(&region_download_status)) {
                region_download_started = false;
                region_download_is_poly = false;
                region_busy = false;
                region_progress = -1.0;
                if (route_download_plan.downloading) {
                    route_download_plan.downloading = false;
                }
                snprintf(region_work_status, sizeof(region_work_status),
                         "Etat du telechargement indisponible");
            } else if (region_download_status.state == OPENRIDE_ANDROID_DOWNLOAD_RUNNING) {
                region_busy = true;
                if (region_download_status.total_bytes > 0U) {
                    region_progress = (double)region_download_status.bytes_downloaded
                        / (double)region_download_status.total_bytes;
                } else {
                    region_progress = -1.0;
                }
                if (region_download_is_poly) {
                    snprintf(region_work_status, sizeof(region_work_status),
                             "Telechargement contour: %.0f / %.0f Ko",
                             (double)region_download_status.bytes_downloaded / 1024.0,
                             (double)region_download_status.total_bytes / 1024.0);
                } else {
                    snprintf(region_work_status, sizeof(region_work_status),
                             "Telechargement OSM: %.1f / %.1f Mo",
                             (double)region_download_status.bytes_downloaded / (1024.0 * 1024.0),
                             (double)region_download_status.total_bytes / (1024.0 * 1024.0));
                }
            } else if (region_download_status.state == OPENRIDE_ANDROID_DOWNLOAD_COMPLETE) {
                const bool completed_poly = region_download_is_poly;
                region_download_started = false;
                region_download_is_poly = false;
                openride_region_get_status(&platform_paths, region,
                                           &region_status, error, sizeof(error));
                if (completed_poly) {
                    refresh_map_world_overview(map_world, &platform_paths);
                    if (openride_region_status_ready(&region_status)) {
                        region_busy = false;
                        region_progress = 1.0;
                            if (route_download_plan.downloading) {
                                refresh_map_world_overview(map_world,
                                                           &platform_paths);
                                if (place_world) {
                                    openride_place_world_refresh(place_world,
                                                                 error,
                                                                 sizeof(error));
                                }

                                ++route_download_plan.index;
                                if (route_download_plan.index
                                    < route_download_plan.count) {
                                    const OpenRideRegionDefinition *next_required =
                                        openride_region_find(
                                            route_download_plan.region_ids[
                                                route_download_plan.index]);
                                    if (!next_required) {
                                        route_download_plan.downloading = false;
                                        snprintf(region_work_status,
                                                 sizeof(region_work_status),
                                                 "Region requise suivante inconnue");
                                    } else {
                                        region = next_required;
                                        openride_region_get_status(
                                            &platform_paths,
                                            region,
                                            &region_status,
                                            error,
                                            sizeof(error));
                                        begin_android_region_install(
                                            &platform_paths,
                                            region,
                                            &region_status,
                                            &region_prepare_context,
                                            &region_prepare_thread,
                                            &region_download_started,
                                            &region_download_is_poly,
                                            &region_busy,
                                            &region_progress,
                                            region_work_status,
                                            sizeof(region_work_status),
                                            error,
                                            sizeof(error));
                                        if (!region_busy
                                            && !region_download_started
                                            && !region_prepare_thread) {
                                            route_download_plan.downloading =
                                                false;
                                        }
                                    }
                                } else {
                                    route_download_plan.downloading = false;
                                    route_download_plan.available = false;
                                    selection = route_download_plan.selection;
                                    routing_profile =
                                        route_download_plan.profile;
                                    app_panel = OPENRIDE_APP_PANEL_NONE;
                                    route_dirty =
                                        openride_map_selection_complete(
                                            &selection);
                                    snprintf(route_status,
                                             sizeof(route_status),
                                             "cartes pretes - recalcul itineraire...");
                                    snprintf(region_work_status,
                                             sizeof(region_work_status),
                                             "Cartes requises installees");
                                }
                            } else if (region != active_region) {
                                snprintf(region_work_status,
                                         sizeof(region_work_status),
                                         "Contour pret - activation en cours");
                                region_activation_requested = true;
                            } else {
                                snprintf(region_work_status,
                                         sizeof(region_work_status),
                                         "Contour de region ajoute a MapWorld");
                            }
                    } else if (region_status.source_pbf_present) {
                        region_prepare_thread = start_region_prepare_thread(&region_prepare_context,
                                                                             &platform_paths,
                                                                             region);
                        if (region_prepare_thread) {
                            region_busy = true;
                            region_progress = region_prepare_stage_progress(
                                OPENRIDE_REGION_PREPARE_ROUTING);
                            snprintf(region_work_status, sizeof(region_work_status),
                                     "Contour pret - preparation 1/3: routage");
                        } else {
                            region_busy = false;
                            region_progress = -1.0;
                            snprintf(region_work_status, sizeof(region_work_status),
                                     "Impossible de lancer la preparation");
                        }
                    } else if (!start_android_region_file_download(region,
                                                                   false,
                                                                   &region_download_started,
                                                                   &region_download_is_poly,
                                                                   &region_busy,
                                                                   &region_progress,
                                                                   region_work_status,
                                                                   sizeof(region_work_status))) {
                        region_busy = false;
                        region_progress = -1.0;
                    }
                } else {
                    region_prepare_thread = start_region_prepare_thread(&region_prepare_context,
                                                                         &platform_paths,
                                                                         region);
                    if (region_prepare_thread) {
                        region_busy = true;
                        region_progress = region_prepare_stage_progress(
                            OPENRIDE_REGION_PREPARE_ROUTING);
                        snprintf(region_work_status, sizeof(region_work_status),
                                 "Telechargement termine - preparation 1/3: routage");
                    } else {
                        region_busy = false;
                        region_progress = -1.0;
                        snprintf(region_work_status, sizeof(region_work_status),
                                 "Impossible de lancer la preparation");
                    }
                }
            } else if (region_download_status.state == OPENRIDE_ANDROID_DOWNLOAD_ERROR
                       || region_download_status.state == OPENRIDE_ANDROID_DOWNLOAD_CANCELLED) {
                const bool failed_poly = region_download_is_poly;
                region_download_started = false;
                region_download_is_poly = false;
                region_busy = false;
                region_progress = -1.0;
                if (route_download_plan.downloading) {
                    route_download_plan.downloading = false;
                }
                snprintf(region_work_status, sizeof(region_work_status),
                         "%s%s%s",
                         region_download_status.state == OPENRIDE_ANDROID_DOWNLOAD_CANCELLED
                             ? "Telechargement annule"
                             : (failed_poly ? "Erreur contour" : "Erreur telechargement OSM"),
                         region_download_status.error[0] ? ": " : "",
                         region_download_status.error);
            }
        }
        if (region_prepare_thread) {
            const int stage = SDL_GetAtomicInt(&region_prepare_context.stage);
            region_busy = true;
            region_progress = region_prepare_stage_progress(stage);
            snprintf(region_work_status, sizeof(region_work_status), "%s",
                     region_prepare_stage_text(stage));
            if (SDL_GetAtomicInt(&region_prepare_context.done)) {
                SDL_WaitThread(region_prepare_thread, NULL);
                region_prepare_thread = NULL;
                const bool prepared = SDL_GetAtomicInt(&region_prepare_context.success) != 0;
                region_busy = false;
                region_progress = prepared ? 1.0 : -1.0;
                openride_region_get_status(&platform_paths, region,
                                           &region_status, error, sizeof(error));
                if (prepared) {
                    if (route_download_plan.downloading) {
                        refresh_map_world_overview(map_world, &platform_paths);
                        if (place_world) {
                            openride_place_world_refresh(place_world,
                                                         error,
                                                         sizeof(error));
                        }

                        ++route_download_plan.index;
                        if (route_download_plan.index
                            < route_download_plan.count) {
                            const OpenRideRegionDefinition *next_required =
                                openride_region_find(
                                    route_download_plan.region_ids[
                                        route_download_plan.index]);
                            if (!next_required) {
                                route_download_plan.downloading = false;
                                snprintf(region_work_status,
                                         sizeof(region_work_status),
                                         "Region requise suivante inconnue");
                            } else {
                                region = next_required;
                                openride_region_get_status(&platform_paths,
                                                           region,
                                                           &region_status,
                                                           error,
                                                           sizeof(error));
                                begin_android_region_install(
                                    &platform_paths,
                                    region,
                                    &region_status,
                                    &region_prepare_context,
                                    &region_prepare_thread,
                                    &region_download_started,
                                    &region_download_is_poly,
                                    &region_busy,
                                    &region_progress,
                                    region_work_status,
                                    sizeof(region_work_status),
                                    error,
                                    sizeof(error));
                                if (!region_busy
                                    && !region_download_started
                                    && !region_prepare_thread) {
                                    route_download_plan.downloading = false;
                                }
                            }
                        } else {
                            route_download_plan.downloading = false;
                            route_download_plan.available = false;
                            selection = route_download_plan.selection;
                            routing_profile = route_download_plan.profile;
                            app_panel = OPENRIDE_APP_PANEL_NONE;
                            route_dirty =
                                openride_map_selection_complete(&selection);
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "cartes pretes - recalcul itineraire...");
                            snprintf(region_work_status,
                                     sizeof(region_work_status),
                                     "Cartes requises installees");
                        }
                    } else {
                        snprintf(region_work_status,
                                 sizeof(region_work_status),
                                 "Region prete - activation en cours");
                        region_activation_requested = true;
                    }
                } else {
                    if (route_download_plan.downloading) {
                        route_download_plan.downloading = false;
                    }
                    snprintf(region_work_status, sizeof(region_work_status),
                             "Preparation impossible: %.150s",
                             region_prepare_context.error[0]
                                 ? region_prepare_context.error : "erreur inconnue");
                }
            }
        }
#endif
        /*
         * RoutingWorld borrows the currently active routing graph read-only.
         * Region activation can destroy/reload that graph, so defer activation
         * until the worker has joined on the main thread.
         */
        if (region_activation_requested && !region_busy && !routing_world_thread) {
            region_activation_requested = false;
            if (region == active_region) {
                snprintf(region_work_status, sizeof(region_work_status),
                         "Cette region est deja active");
            } else if (activate_region_runtime(renderer,
                                               &platform_paths,
                                               region,
                                               map_style,
                                               &map,
                                               &ormap,
                                               &ormap_map,
                                               &vector_map,
                                               &scalable_map,
                                               &metadata_storage,
                                               &metadata,
                                               &raster_renderer,
                                               &vector_renderer,
                                               &ormap_renderer,
                                               &routing_graph,
                                               &graph_loaded,
                                               &place_index,
                                               &camera,
                                               &region_status,
                                               error,
                                               sizeof(error))) {
                active_region = region;
                refresh_map_world_overview(map_world, &platform_paths);
                if (place_world) {
                    openride_place_world_refresh(place_world,
                                                 error,
                                                 sizeof(error));
                }
                if (app_storage) {
                    openride_app_storage_set_text(app_storage,
                                                  "active_region_id",
                                                  active_region->id,
                                                  error,
                                                  sizeof(error));
                }
                openride_route_destroy(&route);
                clear_navigation_session(&navigation,
                                         &gps_simulator,
                                         &navigation_state,
                                         &gps_sample,
                                         &gps_sample_valid);
                openride_navigation_instructions_destroy(&navigation_instructions);
                openride_navigation_session_reset(&navigation_session);
                openride_location_filter_reset(&location_filter);
                openride_map_selection_clear(&selection);
                memset(&filtered_location, 0, sizeof(filtered_location));
                route_valid = false;
                route_dirty = false;
                loop_active = false;
                loop_waypoint_count = 0U;
                gpx_navigation_active = false;
                simulator_deviation = false;
                start_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                destination_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                openride_drive_mode_set_active(&drive_mode, false);
                camera.bearing_deg = 0.0;
                snprintf(route_status, sizeof(route_status),
                         "region active: %s", active_region->name);
                snprintf(region_work_status, sizeof(region_work_status),
                         "%s active - carte, routage et recherche recharges",
                         active_region->name);
            } else {
                snprintf(region_work_status,
                         sizeof(region_work_status),
                         "Activation impossible: %.145s",
                         error[0] ? error : "erreur inconnue");
            }
        }

        if (routing_world_thread
            && SDL_GetAtomicInt(&routing_world_context.done)) {
            SDL_WaitThread(routing_world_thread, NULL);
            routing_world_thread = NULL;

            const bool request_current = routing_world_request_matches(
                &routing_world_context,
                active_region,
                &selection,
                routing_profile);
            const bool calculation_ok =
                SDL_GetAtomicInt(&routing_world_context.success) != 0;

            if (!request_current) {
                openride_route_destroy(&routing_world_context.route);
                route_dirty = openride_map_selection_complete(&selection);
                routing_world_pending_reroute = routing_world_context.reroute;
                routing_world_pending_resume_simulator =
                    routing_world_context.resume_simulator;
            } else if (!calculation_ok) {
                openride_route_destroy(&routing_world_context.route);
                route_valid = false;

                if (routing_world_context.result.download_required
                    && routing_world_context.result.missing_region_count > 0U) {
                    memset(&route_download_plan,
                           0,
                           sizeof(route_download_plan));
                    route_download_plan.available = true;
                    route_download_plan.count =
                        routing_world_context.result.missing_region_count;
                    if (route_download_plan.count
                        > OPENRIDE_ROUTING_WORLD_MAX_CORRIDOR_REGIONS) {
                        route_download_plan.count =
                            OPENRIDE_ROUTING_WORLD_MAX_CORRIDOR_REGIONS;
                    }
                    route_download_plan.has_installed_alternative =
                        routing_world_context.result.has_installed_alternative;
                    route_download_plan.selection = selection;
                    route_download_plan.profile = routing_profile;
                    for (uint32_t i = 0U;
                         i < route_download_plan.count;
                         ++i) {
                        snprintf(route_download_plan.region_ids[i],
                                 sizeof(route_download_plan.region_ids[i]),
                                 "%s",
                                 routing_world_context.result.missing_region_ids[i]);
                    }
#ifdef __ANDROID__
                    app_panel = OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS;
                    app_panel_selected = 0U;
#endif

                    const char *missing_id =
                        routing_world_context.result.missing_region_ids[0];
                    const OpenRideRegionDefinition *missing_region =
                        openride_region_find(missing_id);
                    const char *missing_name =
                        missing_region ? missing_region->name : missing_id;

                    if (routing_world_context.result.missing_region_count == 1U) {
                        snprintf(route_status,
                                 sizeof(route_status),
                                 "carte requise: %.150s%s",
                                 missing_name,
                                 routing_world_context.result.has_installed_alternative
                                     ? " | alternative dispo"
                                     : "");
                    } else {
                        snprintf(route_status,
                                 sizeof(route_status),
                                 "%u cartes requises, dont %.120s%s",
                                 routing_world_context.result.missing_region_count,
                                 missing_name,
                                 routing_world_context.result.has_installed_alternative
                                     ? " | alternative dispo"
                                     : "");
                    }

                    SDL_Log("RoutingWorld plan: %s -> %s | corridor=%u regions | "
                            "missing=%u | first_missing=%s | installed_alternative=%s",
                            routing_world_context.result.start_region_id,
                            routing_world_context.result.destination_region_id,
                            routing_world_context.result.recommended_corridor.count,
                            routing_world_context.result.missing_region_count,
                            missing_name,
                            routing_world_context.result.has_installed_alternative
                                ? "yes"
                                : "no");
                } else if (routing_world_context.result.corridor_planned
                           && routing_world_context.result.recommended_corridor.count > 2U
                           && strcmp(routing_world_context.error,
                                     "multi-hop regional corridor ready") == 0) {
                    snprintf(route_status,
                             sizeof(route_status),
                             "corridor multi-region pret: %u regions",
                             routing_world_context.result.recommended_corridor.count);
                    SDL_Log("RoutingWorld multi-hop corridor ready: %s -> %s | %u regions",
                            routing_world_context.result.start_region_id,
                            routing_world_context.result.destination_region_id,
                            routing_world_context.result.recommended_corridor.count);
                } else {
                    snprintf(route_status,
                             sizeof(route_status),
                             "itineraire impossible: %.180s",
                             routing_world_context.error[0]
                                 ? routing_world_context.error
                                 : "aucune continuite inter-region");
                }
            } else {
                if (routing_world_context.result.used_installed_alternative) {
                    SDL_Log("RoutingWorld installed alternative: %s -> %s | corridor=%u regions",
                            routing_world_context.result.start_region_id,
                            routing_world_context.result.destination_region_id,
                            routing_world_context.result.installed_alternative.count);
                }
                memset(&route_download_plan, 0, sizeof(route_download_plan));
                openride_route_destroy(&route);
                route = routing_world_context.route;
                memset(&routing_world_context.route, 0, sizeof(routing_world_context.route));

                memset(&start_snap, 0, sizeof(start_snap));
                memset(&destination_snap, 0, sizeof(destination_snap));
                start_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                destination_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;

                route_valid = prepare_navigation_session(&navigation,
                                                         &gps_simulator,
                                                         &navigation_instructions,
                                                         &routing_graph,
                                                         &route,
                                                         route_status,
                                                         sizeof(route_status));
                if (route_valid) {
                    if (routing_world_context.reroute) {
                        openride_navigation_session_mark_rerouted(&navigation_session);
                        openride_location_filter_reset(&location_filter);
                        openride_voice_guidance_reset(&voice_guidance);
                        if (routing_world_context.resume_simulator) {
                            openride_gps_simulator_start(&gps_simulator);
                        }
                    }

                    if (routing_world_context.result.used_installed_alternative) {
                        snprintf(route_status,
                                 sizeof(route_status),
                                 "alternative avec cartes installees | %.1f km",
                                 route.distance_m / 1000.0);
                    } else if (routing_world_context.result.multi_region) {
                        snprintf(route_status,
                                 sizeof(route_status),
                                 "itineraire multi-region %s -> %s | %.1f km",
                                 routing_world_context.result.start_region_id,
                                 routing_world_context.result.destination_region_id,
                                 route.distance_m / 1000.0);
                    } else {
                        snprintf(route_status,
                                 sizeof(route_status),
                                 "itineraire sur region installee %s | %.1f km",
                                 routing_world_context.result.start_region_id,
                                 route.distance_m / 1000.0);
                    }

#ifdef __ANDROID__
                    if (real_gps_active || simulated_gps_active) {
                        openride_drive_mode_set_active(&drive_mode, true);
                        openride_drive_mode_set_auto_zoom(&drive_mode, true);
                        follow_gps = true;
                    }
#endif
                    if (!routing_world_context.reroute
                        && app_storage
                        && selection.has_destination) {
                        openride_app_storage_add_history(app_storage,
                                                         "Destination",
                                                         selection.destination.lat,
                                                         selection.destination.lon,
                                                         0,
                                                         error,
                                                         sizeof(error));
                        refresh_stored_places(app_storage,
                                              false,
                                              history_places,
                                              &history_count);
                    }
                }
            }

            memset(&routing_world_context.result, 0, sizeof(routing_world_context.result));
            routing_world_context.error[0] = '\0';
        }

        if (route_dirty && !routing_world_thread) {
            loop_active = false;
            gpx_navigation_active = false;
            loop_waypoint_count = 0U;
            openride_navigation_session_reset(&navigation_session);
            openride_location_filter_reset(&location_filter);
            memset(&filtered_location, 0, sizeof(filtered_location));
            clear_navigation_session(&navigation,
                                     &gps_simulator,
                                     &navigation_state,
                                     &gps_sample,
                                     &gps_sample_valid);
            simulator_deviation = false;

            route_valid = recalculate_route(&routing_graph,
                                            graph_loaded,
                                            &selection,
                                            routing_profile,
                                            &route,
                                            &start_snap,
                                            &destination_snap,
                                            route_status,
                                            sizeof(route_status));

            bool world_route_started = false;
            if (!route_valid && openride_map_selection_complete(&selection)) {
                routing_world_thread = start_routing_world_thread(
                    &routing_world_context,
                    &platform_paths,
                    active_region,
                    graph_loaded ? &routing_graph : NULL,
                    &routing_world_cache,
                    &selection,
                    routing_profile,
                    routing_world_pending_reroute,
                    routing_world_pending_resume_simulator);
                world_route_started = routing_world_thread != NULL;
                if (world_route_started) {
                    snprintf(route_status,
                             sizeof(route_status),
                             "%s",
                             routing_world_pending_reroute
                                 ? "Recalcul inter-region en cours..."
                                 : "Calcul de l'itineraire inter-region...");
                } else {
                    snprintf(route_status,
                             sizeof(route_status),
                             "Impossible de lancer le calcul inter-region");
                }
            }

            if (route_valid) {
                prepare_navigation_session(&navigation,
                                           &gps_simulator,
                                           &navigation_instructions,
                                           &routing_graph,
                                           &route,
                                           route_status,
                                           sizeof(route_status));
                if (routing_world_pending_reroute) {
                    openride_navigation_session_mark_rerouted(&navigation_session);
                    openride_voice_guidance_reset(&voice_guidance);
                    if (routing_world_pending_resume_simulator) {
                        openride_gps_simulator_start(&gps_simulator);
                    }
                }
#ifdef __ANDROID__
                if (real_gps_active || simulated_gps_active) {
                    openride_drive_mode_set_active(&drive_mode, true);
                    openride_drive_mode_set_auto_zoom(&drive_mode, true);
                    follow_gps = true;
                }
#endif
                if (!routing_world_pending_reroute
                    && app_storage
                    && selection.has_destination) {
                    openride_app_storage_add_history(app_storage,
                                                     "Destination",
                                                     selection.destination.lat,
                                                     selection.destination.lon,
                                                     0,
                                                     error,
                                                     sizeof(error));
                    refresh_stored_places(app_storage,
                                          false,
                                          history_places,
                                          &history_count);
                }
            }

            routing_world_pending_reroute = false;
            routing_world_pending_resume_simulator = false;
            route_dirty = false;
            (void)world_route_started;
        }
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
                    record_gps_sample(&gpx_recording,
                                      &gps_sample,
                                      &gpx_last_recorded_position_m);
                }
                if (follow_gps && !map_zoom_test.active && !drive_mode.active) {
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
                    record_gps_sample(&gpx_recording,
                                      &gps_sample,
                                      &gpx_last_recorded_position_m);
                }
                if (follow_gps && !map_zoom_test.active
                    && gps_simulator.active && !drive_mode.active) {
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
            route_valid = reroute_navigation_from_position(
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
                    camera.zoom = clampd(drive_mode.camera_zoom,
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

        int width = 0;
        int height = 0;
        if (!SDL_GetCurrentRenderOutputSize(renderer, &width, &height)) {
            SDL_Log("SDL_GetCurrentRenderOutputSize failed: %s", SDL_GetError());
            break;
        }

        const bool world_available = map_world
            && openride_map_world_region_count(map_world) > 0U;
        const bool world_overview_only = world_available
            && camera.zoom < OPENRIDE_MAP_WORLD_DETAIL_ZOOM;
        OpenRideMapZoomFrameProfile map_zoom_profile;
        OpenRideMapWorldDebugStats map_zoom_world_debug;
        memset(&map_zoom_profile, 0, sizeof(map_zoom_profile));
        memset(&map_zoom_world_debug, 0, sizeof(map_zoom_world_debug));
        map_zoom_world_debug.road.prewarm_zoom = -1;
        if (map_zoom_test.active && map_world) openride_map_world_debug_begin_frame(map_world);
        const uint64_t map_zoom_map_started_ns = map_zoom_test.active ? SDL_GetTicksNS() : 0U;
        if (world_overview_only) {
            const OpenRideMapPalette palette = openride_map_palette(map_style);
            SDL_SetRenderDrawColor(renderer,
                                   palette.background.r,
                                   palette.background.g,
                                   palette.background.b,
                                   SDL_ALPHA_OPAQUE);
            SDL_RenderClear(renderer);
            openride_map_world_draw(map_world,
                                    &camera,
                                    map_style,
                                    NULL,
                                    width,
                                    height);
        } else if (ormap_map) {
            if (world_available) {
                openride_map_world_draw_detail(map_world,
                                               &camera,
                                               map_style,
                                               width,
                                               height);
            } else {
                openride_ormap_renderer_draw(&ormap_renderer, &camera, width, height);
            }
        } else {
            if (vector_map) {
                openride_vector_map_renderer_draw(&vector_renderer, &camera, width, height);
            } else {
                SDL_SetRenderDrawColor(renderer, 28, 32, 38, SDL_ALPHA_OPAQUE);
                SDL_RenderClear(renderer);
                if (map) openride_map_renderer_draw(&raster_renderer, &camera, width, height);
            }

            if (world_available
                && camera.zoom <= OPENRIDE_MAP_WORLD_MAX_OVERVIEW_ZOOM) {
                /* During the handoff, keep the active region's
                 * generalized overview visible as the detailed ORMap fades in. */
                openride_map_world_draw(map_world,
                                        &camera,
                                        map_style,
                                        NULL,
                                        width,
                                        height);
            }
        }
        const uint64_t map_zoom_map_finished_ns = map_zoom_test.active ? SDL_GetTicksNS() : 0U;
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
            draw_gpx_document(renderer,
                              &camera,
                              &gpx_overlay,
                              width,
                              height);
        }
        if (route_valid) {
            draw_route(renderer,
                       &camera,
                       &routing_graph,
                       &route,
                       width,
                       height);
        }
        if (route_valid && !drive_mode.active) {
            draw_snap_connector(renderer,
                                &camera,
                                &selection,
                                &start_snap,
                                OPENRIDE_MARKER_START,
                                width,
                                height);
            if (!loop_active) {
                draw_snap_connector(renderer,
                                    &camera,
                                    &selection,
                                    &destination_snap,
                                    OPENRIDE_MARKER_DESTINATION,
                                    width,
                                    height);
            } else {
                draw_loop_waypoints(renderer,
                                    &camera,
                                    loop_waypoints,
                                    loop_waypoint_count,
                                    width,
                                    height);
            }
        }
        if (!drive_mode.active) {
            draw_selection(renderer,
                           &camera,
                           &selection,
                           !route_valid,
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
            draw_navigation_position(renderer,
                                     &camera,
                                     &display_sample,
                                     &navigation_state,
                                     width,
                                     height);
        }
        if (!drive_mode.active) {
            draw_center_marker(renderer, width, height);
        }
#ifdef __ANDROID__
        if (!drive_mode.active) {
            draw_android_status_overlay(renderer,
                                    metadata,
                                    &route,
                                    route_valid,
                                    route_status,
                                    routing_profile,
                                    width,
                                    height);
        }
#else
        draw_overlay(renderer,
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
                     width,
                     height);
#endif
        if (!drive_mode.active) {
            draw_navigation_overlay(renderer,
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
            draw_drive_mode_ui(renderer,
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
        draw_app_panel(renderer,
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
                       &route_download_plan,
                       width);
        const char *place_search_title =
            place_search_purpose == OPENRIDE_PLACE_SEARCH_ROUTE_START
                ? "RECHERCHER LE DEPART"
                : place_search_purpose == OPENRIDE_PLACE_SEARCH_ROUTE_DESTINATION
                    ? "RECHERCHER L'ARRIVEE"
                    : "RECHERCHER UN LIEU";
        draw_place_search_overlay(renderer,
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
            draw_mobile_toolbar(renderer, width, height, route_valid);
        }
        if (map_zoom_test.active) {
            const SDL_Rect safe = openride_render_safe_area(renderer, width, height);
            const float ui_scale = openride_ui_scale(renderer);
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
                     "GPS %.6f, %.6f",
                     OPENRIDE_MAP_ZOOM_TEST_LAT,
                     OPENRIDE_MAP_ZOOM_TEST_LON);
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
            draw_scaled_text(renderer,
                             badge.x + 8.0f * ui_scale,
                             badge.y + 7.0f * ui_scale,
                             text_scale,
                             zoom_line);
            draw_scaled_text(renderer,
                             badge.x + 8.0f * ui_scale,
                             badge.y + 25.0f * ui_scale,
                             text_scale,
                             gps_line);
            draw_scaled_text(renderer,
                             badge.x + 8.0f * ui_scale,
                             badge.y + 43.0f * ui_scale,
                             text_scale,
                             road_line);
            draw_scaled_text(renderer,
                             badge.x + 8.0f * ui_scale,
                             badge.y + 61.0f * ui_scale,
                             text_scale,
                             road_work_line);
            draw_scaled_text(renderer,
                             badge.x + 8.0f * ui_scale,
                             badge.y + 79.0f * ui_scale,
                             text_scale,
                             area_line);
        }

        const uint64_t map_zoom_ui_finished_ns = map_zoom_test.active ? SDL_GetTicksNS() : 0U;
        if (map_zoom_test.active) map_zoom_profile.ui_ms=(double)(map_zoom_ui_finished_ns-map_zoom_map_finished_ns)/1000000.0;
        SDL_RenderPresent(renderer);
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

    if (routing_world_thread) {
        SDL_WaitThread(routing_world_thread, NULL);
        routing_world_thread = NULL;
    }
    openride_route_destroy(&routing_world_context.route);
    openride_routing_world_cache_destroy(&routing_world_cache);

    if (lifecycle_watch_installed) {
        SDL_RemoveEventWatch(openride_lifecycle_event_watch, &lifecycle_watch);
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
