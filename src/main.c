#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_atomic.h>

#include "map/map_renderer.h"
#include "map/vector_map_renderer.h"
#include "openride/map_camera.h"
#include "openride/map_selection.h"
#include "openride/loop_generator.h"
#include "openride/gps_simulator.h"
#include "openride/gpx.h"
#include "openride/navigation_engine.h"
#include "openride/navigation_instructions.h"
#include "openride/navigation_session.h"
#include "openride/location_filter.h"
#include "openride/location_provider.h"
#ifdef __ANDROID__
#include "openride/android_location_provider.h"
#include <SDL3/SDL_system.h>
#endif
#include "openride/place_search.h"
#include "openride/app_storage.h"
#include "openride/platform_paths.h"
#include "openride/region_manager.h"
#include "openride/touch_input.h"
#include "openride/app_toolbar.h"
#include "openride/drive_mode.h"
#include "openride/app_lifecycle.h"
#include "openride/mbtiles.h"
#include "openride/routing_engine.h"
#include "openride/routing_graph.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define OPENRIDE_CLICK_DRAG_THRESHOLD 5.0
#define OPENRIDE_MARKER_HIT_RADIUS 26.0
#define OPENRIDE_MAX_SNAP_DISTANCE_M 2000.0
#define OPENRIDE_LOOP_DISTANCE_STEP_M 25000.0
#define OPENRIDE_LOOP_DISTANCE_MIN_M 25000.0
#define OPENRIDE_LOOP_DISTANCE_MAX_M 300000.0
#define OPENRIDE_GPS_SIMULATION_TIME_SCALE 20.0
#define OPENRIDE_GPX_RECORDING_MIN_STEP_M 10.0
#define OPENRIDE_GPX_NAVIGATION_SPEED_KPH 50.0
#define OPENRIDE_SEARCH_MAX_RESULTS 8U
#define OPENRIDE_APP_LIST_MAX 12U
#define OPENRIDE_REAL_MAP_PATH "data/maps/nord-pas-de-calais-shortbread.mbtiles"
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

static const char *default_map_path(void)
{
    static const char *real_map = "data/maps/nord-pas-de-calais-shortbread.mbtiles";
    static const char *demo_map = "data/maps/demo.mbtiles";

    return file_exists(real_map) ? real_map : demo_map;
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
    const SDL_Rect safe = openride_render_safe_area(renderer, viewport_width, viewport_height);
    const double ui_scale = (double)openride_ui_scale(renderer);
    const int logical_width = (int)((double)safe.w / ui_scale);
    const int logical_height = (int)((double)safe.h / ui_scale);
    return openride_toolbar_hit_test((x - (double)safe.x) / ui_scale,
                                     (y - (double)safe.y) / ui_scale,
                                     logical_width,
                                     logical_height);
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
                         bool vector_map,
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
    SDL_RenderDebugText(renderer, panel_x + 12.0f, panel_y + 10.0f, "OpenRide v0.21");

    SDL_SetRenderDrawColor(renderer, 174, 181, 188, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugTextFormat(renderer,
                              panel_x + 12.0f,
                              panel_y + 25.0f,
                              "centre %.5f %.5f  |  z %.1f  |  %s",
                              camera->center_lat,
                              camera->center_lon,
                              camera->zoom,
                              vector_map ? openride_map_style_name(map_style) : "raster offline");

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

static bool refresh_place_search(OpenRidePlaceIndex *index,
                                 const char *query,
                                 OpenRidePlaceSearchResult *results,
                                 uint32_t *result_count,
                                 uint32_t *selected_result,
                                 char *status,
                                 size_t status_size)
{
    char error[192] = {0};
    uint32_t count = 0U;
    const bool ok = openride_place_index_search(index,
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
                                      const char *query,
                                      const OpenRidePlaceSearchResult *results,
                                      uint32_t result_count,
                                      uint32_t selected_result,
                                      int viewport_width)
{
    if (!active) return;

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
    SDL_RenderDebugText(renderer, x + 14.0f, y + 12.0f, "RECHERCHE HORS LIGNE");
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
                            "Index absent: ./scripts/prepare_place_index.sh");
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
        SDL_SetRenderDrawColor(renderer, 238, 241, 243, 255);
        SDL_RenderDebugTextFormat(renderer,
                                  x + 16.0f,
                                  row_y + 3.0f,
                                  "%c %-34.34s  [%s]",
                                  i == selected_result ? '>' : ' ',
                                  results[i].name,
                                  openride_place_kind_name(results[i].kind));
    }
}


static int place_search_result_at(double x,
                                  double y,
                                  int viewport_width,
                                  uint32_t result_count)
{
    if (result_count == 0U) return -1;
    const double panel_width = viewport_width > 620 ? 620.0 : (double)viewport_width - 16.0;
    const double panel_x = viewport_width > 620 ? ((double)viewport_width - panel_width) * 0.5 : 8.0;
    const double row_top = 87.0;
    const double row_height = 24.0;
    if (x < panel_x + 10.0 || x > panel_x + panel_width - 10.0 || y < row_top) return -1;
    const int index = (int)((y - row_top) / row_height);
    return index >= 0 && (uint32_t)index < result_count ? index : -1;
}

typedef enum OpenRideAppPanel {
    OPENRIDE_APP_PANEL_NONE = 0,
    OPENRIDE_APP_PANEL_MAIN,
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
                              OpenRidePlaceIndex *place_index,
                              bool *active,
                              char *query,
                              uint32_t *result_count,
                              uint32_t *selected,
                              char *status,
                              size_t status_size)
{
    if (!active || !query || !result_count || !selected) return;
    if (!place_index) {
        snprintf(status, status_size,
                 "index recherche absent: ./scripts/prepare_place_index.sh");
        return;
    }
    *active = true;
    query[0] = '\0';
    *result_count = 0U;
    *selected = 0U;
    /* Text input is layout-aware: important for '/' on AZERTY keyboards. */
    SDL_StartTextInput(window);
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
                           const OpenRideRegionDefinition *region,
                           const OpenRideRegionStatus *region_status,
                           int viewport_width)
{
    if (panel == OPENRIDE_APP_PANEL_NONE) return;
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
        SDL_SetRenderDrawColor(renderer, 165, 174, 181, 255);
        SDL_RenderDebugText(renderer, x + 18, y + 322, "Tab/Esc: fermer | V: ajouter la position en favori");
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
        SDL_RenderDebugText(renderer, x + 18, y + 52, region ? region->name : "Region");
        if (region_status) {
            SDL_RenderDebugTextFormat(renderer, x + 34, y + 82, "Carte       : %s  %.1f Mo",
                                      region_status->map_installed ? "installee" : "absente",
                                      region_status->map_installed ? region_status->map_size_mb : 0.0);
            SDL_RenderDebugTextFormat(renderer, x + 34, y + 108, "Routage     : %s  %.1f Mo",
                                      region_status->routing_installed ? "installe" : "absent",
                                      region_status->routing_installed ? region_status->routing_size_mb : 0.0);
            SDL_RenderDebugTextFormat(renderer, x + 34, y + 134, "Recherche   : %s  %.1f Mo",
                                      region_status->search_installed ? "installee" : "absente",
                                      region_status->search_installed ? region_status->search_size_mb : 0.0);
            SDL_RenderDebugTextFormat(renderer, x + 34, y + 160, "Total local : %.1f Mo",
                                      region_status->total_size_mb);
        }
        SDL_SetRenderDrawColor(renderer, 165, 174, 181, 255);
        SDL_RenderDebugText(renderer, x + 18, y + 204, "Paquet regional pret pour le futur mobile:");
        SDL_RenderDebugText(renderer, x + 34, y + 228, "carte + routage + recherche");
        SDL_RenderDebugText(renderer, x + 18, y + 322, "Esc: retour");
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_SETTINGS) {
        SDL_RenderDebugText(renderer, x + 18, y + 16, "PARAMETRES");
        SDL_RenderDebugTextFormat(renderer, x + 18, y + 58, "M  Style carte       : %s", openride_map_style_name(map_style));
        SDL_RenderDebugTextFormat(renderer, x + 18, y + 88, "1/2/3 Profil routage : %s", openride_routing_profile_name(profile));
        SDL_RenderDebugTextFormat(renderer, x + 18, y + 118, "F  Suivi GPS camera  : %s", follow_gps ? "oui" : "non");
        SDL_RenderDebugTextFormat(renderer, x + 18, y + 148, "A  Recalcul auto      : %s", auto_reroute ? "oui" : "non");
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
    if (!openride_navigation_instructions_build(graph,
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
    const SDL_Rect safe = openride_render_safe_area(renderer, viewport_width, viewport_height);
    const double ui_scale = (double)openride_ui_scale(renderer);
    const int logical_width = (int)((double)safe.w / ui_scale);
    const int logical_height = (int)((double)safe.h / ui_scale);
    OpenRideToolbarRect bar = openride_toolbar_bounds(logical_width, logical_height);
    bar.x = (double)safe.x + bar.x * ui_scale;
    bar.y = (double)safe.y + bar.y * ui_scale;
    bar.w *= ui_scale;
    bar.h *= ui_scale;
    if (bar.w <= 0.0 || bar.h <= 0.0) return;

    SDL_FRect box = {(float)bar.x, (float)bar.y, (float)bar.w, (float)bar.h};
    SDL_SetRenderDrawColor(renderer, 16, 20, 24, 228);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 70);
    SDL_RenderRect(renderer, &box);

    for (OpenRideToolbarAction action = OPENRIDE_TOOLBAR_MENU;
         action <= OPENRIDE_TOOLBAR_GPS;
         action = (OpenRideToolbarAction)(action + 1)) {
        OpenRideToolbarRect item = openride_toolbar_item_bounds(action,
                                                                  logical_width,
                                                                  logical_height);
        item.x = (double)safe.x + item.x * ui_scale;
        item.y = (double)safe.y + item.y * ui_scale;
        item.w *= ui_scale;
        item.h *= ui_scale;
        SDL_FRect item_rect = {(float)item.x, (float)item.y, (float)item.w, (float)item.h};
        if (action != OPENRIDE_TOOLBAR_MENU) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 35);
            SDL_RenderLine(renderer,
                           (float)item.x,
                           (float)item.y + 10.0f,
                           (float)item.x,
                           (float)(item.y + item.h) - 10.0f);
        }
        SDL_SetRenderDrawColor(renderer, 238, 241, 243, 255);
        const char *label = openride_toolbar_action_label(action);
        if (action == OPENRIDE_TOOLBAR_ROUTE && route_ready) label = "Demarrer";
        const float label_scale = (float)ui_scale;
        const float label_w = (float)strlen(label) * 8.0f * label_scale;
        const float label_h = 8.0f * label_scale;
        const float label_x = item_rect.x + (item_rect.w - label_w) * 0.5f;
        const float label_y = item_rect.y + (item_rect.h - label_h) * 0.5f;
        draw_scaled_text(renderer, label_x, label_y, label_scale, label);
    }
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

    char gps_text[64];
    if (drive->gps_quality == OPENRIDE_GPS_GOOD || drive->gps_quality == OPENRIDE_GPS_FAIR) {
        snprintf(gps_text,
                 sizeof(gps_text),
                 "%s %.0f m",
                 openride_drive_mode_gps_quality_name(drive->gps_quality),
                 drive->gps_accuracy_m);
    } else {
        snprintf(gps_text,
                 sizeof(gps_text),
                 "%s",
                 openride_drive_mode_gps_quality_name(drive->gps_quality));
    }
    switch (drive->gps_quality) {
        case OPENRIDE_GPS_GOOD: SDL_SetRenderDrawColor(renderer, 98, 211, 128, 255); break;
        case OPENRIDE_GPS_FAIR: SDL_SetRenderDrawColor(renderer, 255, 207, 77, 255); break;
        default: SDL_SetRenderDrawColor(renderer, 240, 96, 76, 255); break;
    }
    const float gps_w = (float)strlen(gps_text) * 8.0f * small_scale;
    draw_scaled_text(renderer,
                     top.x + top.w - gps_w - 10.0f * ui_scale,
                     top.y + 10.0f * ui_scale,
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
    const OpenRideRegionDefinition *region = openride_region_default();
    memset(&region_status, 0, sizeof(region_status));
    openride_region_get_status(&platform_paths, region, &region_status, error, sizeof(error));

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

    if (!map_path || !file_exists(map_path)) {
#ifdef __ANDROID__
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                 "OpenRide - données hors ligne absentes",
                                 "Installe les données Nord-Pas-de-Calais avec ./scripts/android_push_data.sh puis relance OpenRide.",
                                 NULL);
        SDL_Log("Offline map missing from Android storage: %s", platform_paths.maps_dir);
#else
        SDL_Log("Offline map is missing: %s", map_path ? map_path : "(null)");
#endif
        SDL_Quit();
        return 1;
    }

    OpenRideMBTiles *map = openride_mbtiles_open(map_path, error, sizeof(error));
    if (!map) {
        SDL_Log("Unable to open offline map %s: %s",
                map_path,
                error[0] ? error : "unknown error");
        SDL_Quit();
        return 1;
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

    const OpenRideMBTilesMetadata *metadata = openride_mbtiles_metadata(map);
    const bool vector_map = is_vector_map(metadata);
    OpenRideMapCamera camera = camera_from_metadata(metadata);
    OpenRideMapSelection selection;
    openride_map_selection_init(&selection);
    OpenRideRoute route = {0};
    OpenRideNavigationEngine navigation;
    OpenRideNavigationInstructionList navigation_instructions = {0};
    OpenRideNavigationSession navigation_session;
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
    openride_android_location_provider_init(&location_provider, &android_location_context);
    bool real_gps_active = false;
    bool real_gps_requested = false;
    double real_gps_sample_age_s = INFINITY;
    double real_gps_accuracy_m = 0.0;
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
    bool simulator_deviation = false;
    bool gpx_navigation_active = false;
    Uint64 last_frame_ticks = 0;
    openride_navigation_engine_init(&navigation);
    openride_navigation_session_init(&navigation_session);
    openride_location_filter_init(&location_filter);
    openride_drive_mode_init(&drive_mode);
    openride_app_lifecycle_init(&app_lifecycle);
    openride_gps_simulator_init(&gps_simulator);
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
    bool place_search_active = false;
    char place_search_query[128] = {0};
    OpenRidePlaceSearchResult place_search_results[OPENRIDE_SEARCH_MAX_RESULTS];
    uint32_t place_search_result_count = 0U;
    uint32_t place_search_selected = 0U;

    OpenRideAppStorage *app_storage = NULL;
    OpenRideAppPanel app_panel = OPENRIDE_APP_PANEL_NONE;
    OpenRideStoredPlace favorite_places[OPENRIDE_APP_LIST_MAX];
    OpenRideStoredPlace history_places[OPENRIDE_APP_LIST_MAX];
    uint32_t favorite_count = 0U;
    uint32_t history_count = 0U;
    uint32_t app_panel_selected = 0U;

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    OpenRideMapRenderer raster_renderer;
    OpenRideVectorMapRenderer vector_renderer;
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

    if (vector_map) {
        renderer_initialized = openride_vector_map_renderer_init(&vector_renderer, renderer, map);
        if (renderer_initialized) {
            openride_vector_map_renderer_set_style(&vector_renderer, map_style);
        }
    } else {
        renderer_initialized = openride_map_renderer_init(&raster_renderer, renderer, map);
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

    app_storage = openride_app_storage_open(platform_paths.app_storage_path,
                                               error,
                                               sizeof(error));
    if (app_storage) {
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
        if (saved_style >= (int)OPENRIDE_MAP_STYLE_ROAD
            && saved_style <= (int)OPENRIDE_MAP_STYLE_TOPO) {
            map_style = (OpenRideMapStyle)saved_style;
            if (vector_map) openride_vector_map_renderer_set_style(&vector_renderer, map_style);
        }
        if (saved_profile >= (int)OPENRIDE_ROUTING_PROFILE_FASTEST
            && saved_profile <= (int)OPENRIDE_ROUTING_PROFILE_TRAIL) {
            routing_profile = (OpenRideRoutingProfile)saved_profile;
        }
        follow_gps = saved_follow != 0;
        auto_reroute = saved_auto_reroute != 0;
        openride_navigation_session_set_auto_reroute(&navigation_session, auto_reroute);
        refresh_stored_places(app_storage, true, favorite_places, &favorite_count);
        refresh_stored_places(app_storage, false, history_places, &history_count);
    } else {
        fprintf(stderr, "App storage unavailable: %s\n", error[0] ? error : "unknown error");
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
                              vector_map ? 18.0 : (double)metadata->max_zoom);
        }
    }

    while (running) {
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
                                                  place_index,
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
                        } else if (app_panel == OPENRIDE_APP_PANEL_SETTINGS) {
                            if (event.key.key == SDLK_M && vector_map) {
                                map_style = openride_map_style_next(map_style);
                                openride_vector_map_renderer_set_style(&vector_renderer, map_style);
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
                            }
                        }
                        break;
                    }

                    if (place_search_active) {
                        if (event.key.key == SDLK_ESCAPE) {
                            place_search_active = false;
                            SDL_StopTextInput(window);
                        } else if (event.key.key == SDLK_BACKSPACE) {
                            utf8_backspace(place_search_query);
                            refresh_place_search(place_index,
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
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "recherche: %.120s (%s)",
                                     chosen->name,
                                     openride_place_kind_name(chosen->kind));
                            if (app_storage) {
                                openride_app_storage_add_history(app_storage, chosen->name, chosen->lat, chosen->lon, (int)chosen->kind, error, sizeof(error));
                                refresh_stored_places(app_storage, false, history_places, &history_count);
                            }
                            place_search_active = false;
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
                        open_place_search(window, place_index, &place_search_active,
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
                        if (real_gps_active) {
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
                                              vector_map ? 18.0 : (double)metadata->max_zoom);
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
                    } else if (event.key.key == SDLK_M && vector_map) {
                        map_style = openride_map_style_next(map_style);
                        openride_vector_map_renderer_set_style(&vector_renderer, map_style);
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
                    if (place_search_active && place_index) {
                        const size_t current = strlen(place_search_query);
                        const size_t incoming = strlen(event.text.text);
                        if (current + incoming < sizeof(place_search_query)) {
                            memcpy(place_search_query + current,
                                   event.text.text,
                                   incoming + 1U);
                            refresh_place_search(place_index,
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
                    const double max_zoom = vector_map ? 18.0 : (double)metadata->max_zoom;
                    const double target_zoom = clampd(
                        camera.zoom + requested_delta,
                        (double)metadata->min_zoom,
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

                    if (place_search_active) {
                        const int result = place_search_result_at(x,
                                                                 y,
                                                                 width,
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
                            set_destination_from_place(&selection,
                                                       &gps_sample,
                                                       gps_sample_valid,
                                                       chosen->lat,
                                                       chosen->lon,
                                                       chosen->name,
                                                       &route_dirty,
                                                       route_status,
                                                       sizeof(route_status));
                            place_search_active = false;
                            SDL_StopTextInput(window);
                        }
                        break;
                    }

                    if (app_panel == OPENRIDE_APP_PANEL_MAIN) {
                        if (app_panel_main_search_at(x, y, width)) {
                            app_panel = OPENRIDE_APP_PANEL_NONE;
                            open_place_search(window,
                                              place_index,
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
                            pending_toolbar_action = toolbar_action;
                            openride_touch_input_cancel(&touch_input);
                            break;
                        }
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
                    if (action.type == OPENRIDE_TOUCH_ACTION_PAN) {
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
                    if (action.type == OPENRIDE_TOUCH_ACTION_TAP && !drive_mode.active) {
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
                    break;
                }

                case SDL_EVENT_FINGER_CANCELED:
                    openride_touch_input_cancel(&touch_input);
                    break;

                case SDL_EVENT_PINCH_BEGIN:
                    openride_touch_input_cancel(&touch_input);
                    break;

                case SDL_EVENT_PINCH_UPDATE: {
                    if (place_search_active || app_panel != OPENRIDE_APP_PANEL_NONE) break;
                    int width = 0;
                    int height = 0;
                    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
                    const double max_zoom = vector_map ? 18.0 : (double)metadata->max_zoom;
                    if (drive_mode.active) {
                        openride_drive_mode_set_auto_zoom(&drive_mode, false);
                    }
                    double zoom_delta = openride_touch_pinch_zoom_delta((double)event.pinch.scale);
                    zoom_delta = clampd(zoom_delta, -1.0, 1.0);
                    const double target_zoom = clampd(camera.zoom + zoom_delta,
                                                      (double)metadata->min_zoom,
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
                        real_gps_sample_age_s = INFINITY;
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
                open_place_search(window,
                                  place_index,
                                  &place_search_active,
                                  place_search_query,
                                  &place_search_result_count,
                                  &place_search_selected,
                                  route_status,
                                  sizeof(route_status));
            } else if (action == OPENRIDE_TOOLBAR_ROUTE) {
#ifdef __ANDROID__
                if (route_valid) {
                    real_gps_requested = true;
                    if (!real_gps_active) {
                        real_gps_active = openride_location_provider_start(&location_provider);
                        real_gps_sample_age_s = INFINITY;
                    }
                    if (real_gps_active) {
                        openride_drive_mode_set_active(&drive_mode, true);
                        openride_drive_mode_set_auto_zoom(&drive_mode, true);
                        follow_gps = true;
                        snprintf(route_status, sizeof(route_status), "navigation demarree");
                    } else {
                        snprintf(route_status, sizeof(route_status),
                                 "autorise la localisation Android puis retouche Demarrer");
                    }
                } else if (openride_map_selection_complete(&selection)) {
                    route_dirty = true;
                } else {
                    snprintf(route_status, sizeof(route_status), "touche la carte: depart puis destination");
                }
#else
                if (openride_map_selection_complete(&selection)) {
                    route_dirty = true;
                } else {
                    snprintf(route_status, sizeof(route_status), "touche la carte: depart puis destination");
                }
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
                if (real_gps_active) {
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
                        real_gps_sample_age_s = INFINITY;
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

        if (route_dirty) {
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
            if (route_valid) {
                prepare_navigation_session(&navigation,
                                           &gps_simulator,
                                           &navigation_instructions,
                                           &routing_graph,
                                           &route,
                                           route_status,
                                           sizeof(route_status));
#ifdef __ANDROID__
                if (real_gps_active) {
                    openride_drive_mode_set_active(&drive_mode, true);
                    openride_drive_mode_set_auto_zoom(&drive_mode, true);
                    follow_gps = true;
                }
#endif
                if (app_storage && selection.has_destination) {
                    openride_app_storage_add_history(app_storage,
                                                     "Destination",
                                                     selection.destination.lat,
                                                     selection.destination.lon,
                                                     0,
                                                     error,
                                                     sizeof(error));
                    refresh_stored_places(app_storage, false, history_places, &history_count);
                }
            }
            route_dirty = false;
        }

        const Uint64 current_ticks = SDL_GetTicks();
        double delta_seconds = (double)(current_ticks - last_frame_ticks) / 1000.0;
        last_frame_ticks = current_ticks;
        if (delta_seconds < 0.0) delta_seconds = 0.0;
        if (delta_seconds > 0.25) delta_seconds = 0.25;

#ifdef __ANDROID__
        if (real_gps_active) {
            if (isfinite(real_gps_sample_age_s)) {
                real_gps_sample_age_s += delta_seconds;
            }
            OpenRideLocationSample real_sample;
            if (openride_location_provider_poll(&location_provider, delta_seconds, &real_sample)) {
                real_gps_sample_age_s = 0.0;
                real_gps_accuracy_m = real_sample.accuracy_m;
                gps_sample_valid = real_sample.valid;
                gps_sample.valid = real_sample.valid;
                gps_sample.finished = false;
                gps_sample.lat = real_sample.lat;
                gps_sample.lon = real_sample.lon;
                gps_sample.speed_mps = real_sample.speed_mps;
                gps_sample.heading_deg = real_sample.heading_deg;

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
                if (follow_gps && !drive_mode.active) {
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
                if (follow_gps && gps_simulator.active && !drive_mode.active) {
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
            memset(&navigation_state, 0, sizeof(navigation_state));
            memset(&filtered_location, 0, sizeof(filtered_location));
            if (route_valid) {
                snprintf(route_status, sizeof(route_status), "recalcul automatique termine");
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
            const bool drive_gps_active = real_gps_active;
            const double drive_sample_age_s = real_gps_sample_age_s;
            const double drive_accuracy_m = real_gps_accuracy_m;
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
            if (follow_gps && drive_mode.initialized
                && drive_mode.gps_quality != OPENRIDE_GPS_LOST) {
                camera.center_lat = drive_mode.camera_lat;
                camera.center_lon = drive_mode.camera_lon;
                if (drive_mode.auto_zoom) {
                    const double max_drive_zoom = vector_map
                        ? 18.0 : (double)metadata->max_zoom;
                    camera.zoom = clampd(drive_mode.camera_zoom,
                                         (double)metadata->min_zoom,
                                         max_drive_zoom);
                }
                camera.bearing_deg = drive_mode.heading_up
                    ? drive_mode.camera_bearing_deg : 0.0;
            }
        }

        int width = 0;
        int height = 0;
        if (!SDL_GetCurrentRenderOutputSize(renderer, &width, &height)) {
            SDL_Log("SDL_GetCurrentRenderOutputSize failed: %s", SDL_GetError());
            break;
        }

        if (vector_map) {
            openride_vector_map_renderer_draw(&vector_renderer, &camera, width, height);
        } else {
            SDL_SetRenderDrawColor(renderer, 28, 32, 38, SDL_ALPHA_OPAQUE);
            SDL_RenderClear(renderer);
            openride_map_renderer_draw(&raster_renderer, &camera, width, height);
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
                     vector_map,
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
                       region,
                       &region_status,
                       width);
        draw_place_search_overlay(renderer,
                                  place_search_active,
                                  place_index != NULL,
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
        SDL_RenderPresent(renderer);
    }

#ifdef __ANDROID__
    real_gps_requested = false;
    if (real_gps_active) {
        openride_location_provider_stop(&location_provider);
        real_gps_active = false;
    }
#endif

    if (lifecycle_watch_installed) {
        SDL_RemoveEventWatch(openride_lifecycle_event_watch, &lifecycle_watch);
        lifecycle_watch_installed = false;
    }

    if (vector_map) {
        openride_vector_map_renderer_destroy(&vector_renderer);
    } else {
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
    openride_place_index_close(place_index);
    openride_app_storage_close(app_storage);
    openride_mbtiles_close(map);
    SDL_Quit();

    return 0;
}
