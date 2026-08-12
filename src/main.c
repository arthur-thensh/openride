#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "map/map_renderer.h"
#include "map/vector_map_renderer.h"
#include "openride/map_camera.h"
#include "openride/map_selection.h"
#include "openride/loop_generator.h"
#include "openride/gps_simulator.h"
#include "openride/gpx.h"
#include "openride/navigation_engine.h"
#include "openride/navigation_instructions.h"
#include "openride/place_search.h"
#include "openride/app_storage.h"
#include "openride/mbtiles.h"
#include "openride/routing_engine.h"
#include "openride/routing_graph.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define OPENRIDE_CLICK_DRAG_THRESHOLD 5.0
#define OPENRIDE_MARKER_HIT_RADIUS 26.0
#define OPENRIDE_MAX_SNAP_DISTANCE_M 2000.0
#define OPENRIDE_LOOP_DISTANCE_STEP_M 25000.0
#define OPENRIDE_LOOP_DISTANCE_MIN_M 25000.0
#define OPENRIDE_LOOP_DISTANCE_MAX_M 300000.0
#define OPENRIDE_GPS_SIMULATION_TIME_SCALE 20.0
#define OPENRIDE_GPX_RECORDING_MIN_STEP_M 10.0
#define OPENRIDE_GPX_DEFAULT_IMPORT "data/gpx/import.gpx"
#define OPENRIDE_GPX_ROUTE_EXPORT "data/gpx/openride-route.gpx"
#define OPENRIDE_GPX_RECORDING_EXPORT "data/gpx/openride-recording.gpx"
#define OPENRIDE_PLACE_INDEX_PATH "data/search/nord-pas-de-calais.orplaces.sqlite"
#define OPENRIDE_SEARCH_MAX_RESULTS 8U
#define OPENRIDE_APP_STORAGE_PATH "data/openride-app.sqlite"
#define OPENRIDE_APP_LIST_MAX 12U
#define OPENRIDE_REAL_MAP_PATH "data/maps/nord-pas-de-calais-shortbread.mbtiles"
#define OPENRIDE_ROUTING_GRAPH_PATH "data/routing/nord-pas-de-calais.orgraph"

static double clampd(double value, double min_value, double max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static bool file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    fclose(file);
    return true;
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
    SDL_RenderDebugText(renderer, panel_x + 12.0f, panel_y + 10.0f, "OpenRide v0.16");

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
                        "S: GPS | F: suivi | X: ecart test | R: recalcul");

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
                        "I: importer GPX | E: exporter route | G: enregistrer trace");

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


typedef enum OpenRideAppPanel {
    OPENRIDE_APP_PANEL_NONE = 0,
    OPENRIDE_APP_PANEL_MAIN,
    OPENRIDE_APP_PANEL_FAVORITES,
    OPENRIDE_APP_PANEL_HISTORY,
    OPENRIDE_APP_PANEL_REGIONS,
    OPENRIDE_APP_PANEL_SETTINGS
} OpenRideAppPanel;

static double file_size_mb(const char *path)
{
    struct stat info;
    if (!path || stat(path, &info) != 0) return -1.0;
    return (double)info.st_size / (1024.0 * 1024.0);
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
        const double map_mb = file_size_mb(OPENRIDE_REAL_MAP_PATH);
        const double graph_mb = file_size_mb(OPENRIDE_ROUTING_GRAPH_PATH);
        const double search_mb = file_size_mb(OPENRIDE_PLACE_INDEX_PATH);
        SDL_RenderDebugText(renderer, x + 18, y + 16, "CARTES / DONNEES HORS LIGNE");
        SDL_RenderDebugText(renderer, x + 18, y + 52, "Nord-Pas-de-Calais");
        SDL_RenderDebugTextFormat(renderer, x + 34, y + 82, "Carte       : %s%s%.1f Mo",
                                  map_mb >= 0 ? "installee  " : "absente", map_mb >= 0 ? "" : "  ", map_mb >= 0 ? map_mb : 0.0);
        SDL_RenderDebugTextFormat(renderer, x + 34, y + 108, "Routage     : %s%s%.1f Mo",
                                  graph_mb >= 0 ? "installe   " : "absent", graph_mb >= 0 ? "" : "   ", graph_mb >= 0 ? graph_mb : 0.0);
        SDL_RenderDebugTextFormat(renderer, x + 34, y + 134, "Recherche   : %s%s%.1f Mo",
                                  search_mb >= 0 ? "installee  " : "absente", search_mb >= 0 ? "" : "  ", search_mb >= 0 ? search_mb : 0.0);
        SDL_SetRenderDrawColor(renderer, 165, 174, 181, 255);
        SDL_RenderDebugText(renderer, x + 18, y + 204, "Preparation depuis le terminal:");
        SDL_RenderDebugText(renderer, x + 34, y + 228, "./scripts/download_real_map.sh");
        SDL_RenderDebugText(renderer, x + 34, y + 250, "./scripts/prepare_routing_graph.sh");
        SDL_RenderDebugText(renderer, x + 34, y + 272, "./scripts/prepare_place_index.sh");
        SDL_RenderDebugText(renderer, x + 18, y + 322, "Esc: retour");
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_SETTINGS) {
        SDL_RenderDebugText(renderer, x + 18, y + 16, "PARAMETRES");
        SDL_RenderDebugTextFormat(renderer, x + 18, y + 58, "M  Style carte       : %s", openride_map_style_name(map_style));
        SDL_RenderDebugTextFormat(renderer, x + 18, y + 88, "1/2/3 Profil routage : %s", openride_routing_profile_name(profile));
        SDL_RenderDebugTextFormat(renderer, x + 18, y + 118, "F  Suivi GPS camera  : %s", follow_gps ? "oui" : "non");
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

    const double heading_rad = gps->heading_deg * 0.01745329251994329577;
    const float hx = (float)raw.x + (float)(sin(heading_rad) * 18.0);
    const float hy = (float)raw.y - (float)(cos(heading_rad) * 18.0);
    SDL_SetRenderDrawColor(renderer, 248, 248, 246, 255);
    draw_thick_line(renderer, (float)raw.x, (float)raw.y, hx, hy, 5);
    SDL_SetRenderDrawColor(renderer, 25, 118, 210, 255);
    draw_thick_line(renderer, (float)raw.x, (float)raw.y, hx, hy, 3);
}

static void draw_navigation_overlay(SDL_Renderer *renderer,
                                    const OpenRideNavigationState *navigation,
                                    const OpenRideNavigationInstructionList *instructions,
                                    const OpenRideGPSSimulator *simulator,
                                    bool gps_sample_valid,
                                    bool follow_gps,
                                    bool deviation_enabled,
                                    int viewport_height)
{
    if (!gps_sample_valid || !navigation || !navigation->valid) return;

    const float x = 10.0f;
    const float y = 242.0f;
    const float w = 500.0f;
    const float h = 132.0f;
    if (viewport_height < (int)(y + h + 30.0f)) return;

    SDL_FRect panel = {x, y, w, h};
    SDL_SetRenderDrawColor(renderer, 24, 28, 32, 222);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 75);
    SDL_RenderRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 247, 248, 249, 255);
    SDL_RenderDebugText(renderer, x + 12.0f, y + 10.0f, "NAVIGATION GPS SIMULEE");

    if (navigation->status == OPENRIDE_NAVIGATION_OFF_ROUTE) {
        SDL_SetRenderDrawColor(renderer, 230, 98, 75, 255);
    } else if (navigation->status == OPENRIDE_NAVIGATION_ARRIVED) {
        SDL_SetRenderDrawColor(renderer, 86, 190, 118, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 100, 190, 126, 255);
    }
    SDL_RenderDebugTextFormat(renderer,
                              x + 12.0f,
                              y + 27.0f,
                              "%s%s",
                              openride_navigation_status_name(navigation->status),
                              simulator && simulator->active ? " | lecture" : " | pause");

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
                                      y + 46.0f,
                                      "ARRIVEE dans %s",
                                      distance_text);
        } else {
            SDL_RenderDebugTextFormat(renderer,
                                      x + 12.0f,
                                      y + 46.0f,
                                      "Dans %s | %s",
                                      distance_text,
                                      maneuver_text);
        }
    }

    SDL_SetRenderDrawColor(renderer, 220, 225, 229, 255);
    SDL_RenderDebugTextFormat(renderer,
                              x + 12.0f,
                              y + 66.0f,
                              "reste %.1f km | progression %.1f%%",
                              navigation->remaining_m / 1000.0,
                              navigation->progress_ratio * 100.0);
    SDL_RenderDebugTextFormat(renderer,
                              x + 12.0f,
                              y + 81.0f,
                              "ecart %.1f m | vitesse %.0f km/h",
                              navigation->distance_from_route_m,
                              navigation->speed_mps * 3.6);

    SDL_SetRenderDrawColor(renderer, 158, 168, 176, 255);
    SDL_RenderDebugTextFormat(renderer,
                              x + 12.0f,
                              y + 103.0f,
                              "S lecture/pause | F suivi %s | X deviation %s | R recalcul",
                              follow_gps ? "ON" : "OFF",
                              deviation_enabled ? "ON" : "OFF");
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
    const char *map_path = argc >= 2 ? argv[1] : default_map_path();
    const char *routing_graph_path = argc >= 3 ? argv[2] : default_routing_graph_path();
    const char *gpx_import_path = argc >= 4 ? argv[3] : OPENRIDE_GPX_DEFAULT_IMPORT;
    char error[512] = {0};

    OpenRideMBTiles *map = openride_mbtiles_open(map_path, error, sizeof(error));
    if (!map) {
        fprintf(stderr, "Unable to open offline map: %s\n", map_path);
        fprintf(stderr, "Reason: %s\n", error[0] ? error : "unknown error");
        fprintf(stderr, "\nRun ./scripts/download_real_map.sh to install the real OSM map.\n");
        fprintf(stderr, "Usage: ./build/openride [path/to/map.mbtiles] [path/to/routing.orgraph] [path/to/import.gpx]\n");
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
    OpenRideGPSSimulator gps_simulator;
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
    bool simulator_deviation = false;
    Uint64 last_frame_ticks = 0;
    openride_navigation_engine_init(&navigation);
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
    bool dragging_map = false;
    bool map_drag_moved = false;
    double mouse_down_x = 0.0;
    double mouse_down_y = 0.0;
    OpenRideSelectionMarker dragging_marker = OPENRIDE_MARKER_NONE;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        openride_gpx_document_destroy(&gpx_recording);
        openride_gpx_document_destroy(&gpx_overlay);
        openride_gps_simulator_destroy(&gps_simulator);
        openride_navigation_engine_destroy(&navigation);
        openride_routing_graph_destroy(&routing_graph);
        openride_mbtiles_close(map);
        return 1;
    }

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

    if (file_exists(OPENRIDE_PLACE_INDEX_PATH)) {
        place_index = openride_place_index_open(OPENRIDE_PLACE_INDEX_PATH,
                                                error,
                                                sizeof(error));
        if (place_index) {
            fprintf(stdout, "Offline place index loaded: %s\n", OPENRIDE_PLACE_INDEX_PATH);
        } else {
            fprintf(stderr,
                    "Place index unavailable (%s): %s\n",
                    OPENRIDE_PLACE_INDEX_PATH,
                    error[0] ? error : "unknown error");
        }
    } else {
        fprintf(stdout,
                "Offline search not installed. Run ./scripts/prepare_place_index.sh\n");
    }

    app_storage = openride_app_storage_open(OPENRIDE_APP_STORAGE_PATH,
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
                    } else if (event.key.key == SDLK_X) {
                        simulator_deviation = !simulator_deviation;
                        openride_gps_simulator_set_lateral_offset_m(
                            &gps_simulator,
                            simulator_deviation ? 80.0 : 0.0);
                        snprintf(route_status,
                                 sizeof(route_status),
                                 "deviation GPS test: %s",
                                 simulator_deviation ? "80 m" : "desactivee");
                    } else if (event.key.key == SDLK_F) {
                        follow_gps = !follow_gps;
                        if (app_storage) openride_app_storage_set_int(app_storage, "follow_gps", follow_gps ? 1 : 0, error, sizeof(error));
                        snprintf(route_status,
                                 sizeof(route_status),
                                 "suivi camera GPS: %s",
                                 follow_gps ? "actif" : "inactif");
                    } else if (event.key.key == SDLK_R) {
                        if (gps_sample_valid && selection.has_destination && !loop_active) {
                            const double reroute_lat = gps_sample.lat;
                            const double reroute_lon = gps_sample.lon;
                            clear_navigation_session(&navigation,
                                                     &gps_simulator,
                                                     &navigation_state,
                                                     &gps_sample,
                                                     &gps_sample_valid);
                            openride_route_destroy(&route);
                            openride_map_selection_set(&selection,
                                                       OPENRIDE_MARKER_START,
                                                       reroute_lat,
                                                       reroute_lon);
                            route_valid = false;
                            route_dirty = true;
                            loop_active = false;
                            simulator_deviation = false;
                            snprintf(route_status, sizeof(route_status), "recalcul depuis GPS...");
                        } else {
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "R necessite un GPS actif et une destination");
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
                    } else if (event.key.key == SDLK_E) {
                        if (!route_valid) {
                            snprintf(route_status,
                                     sizeof(route_status),
                                     "aucun itineraire a exporter en GPX");
                        } else {
                            char gpx_error[192] = {0};
                            if (openride_gpx_save_route(OPENRIDE_GPX_ROUTE_EXPORT,
                                                        &route,
                                                        loop_active ? "OpenRide boucle" : "OpenRide itineraire",
                                                        gpx_error,
                                                        sizeof(gpx_error))) {
                                snprintf(route_status,
                                         sizeof(route_status),
                                         "GPX exporte: %s",
                                         OPENRIDE_GPX_ROUTE_EXPORT);
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
                                if (openride_gpx_save_document(OPENRIDE_GPX_RECORDING_EXPORT,
                                                               &gpx_recording,
                                                               "OpenRide",
                                                               gpx_error,
                                                               sizeof(gpx_error))) {
                                    snprintf(route_status,
                                             sizeof(route_status),
                                             "trace GPX enregistree: %s",
                                             OPENRIDE_GPX_RECORDING_EXPORT);
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
                    if (place_search_active || app_panel != OPENRIDE_APP_PANEL_NONE) break;
                    int width = 0;
                    int height = 0;
                    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);

                    if (event.button.button == SDL_BUTTON_LEFT) {
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
                            double lat = 0.0;
                            double lon = 0.0;
                            SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
                            openride_screen_to_geo(&camera,
                                                   (double)event.button.x,
                                                   (double)event.button.y,
                                                   width,
                                                   height,
                                                   &lat,
                                                   &lon);
                            const OpenRideSelectionMarker added =
                                openride_map_selection_add(&selection, lat, lon);
                            if (added != OPENRIDE_MARKER_NONE) {
                                loop_active = false;
                                loop_waypoint_count = 0U;
                                route_dirty = openride_map_selection_complete(&selection);
                                if (!route_dirty) {
                                    snprintf(route_status,
                                             sizeof(route_status),
                                             "choisis la destination");
                                }
                            }
                        }
                        dragging_map = false;
                        map_drag_moved = false;
                    }
                    break;

                case SDL_EVENT_MOUSE_MOTION:
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

                default:
                    break;
            }
        }

        if (route_dirty) {
            loop_active = false;
            loop_waypoint_count = 0U;
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

        if (route_valid && gps_simulator.route
            && (gps_simulator.active || gps_sample_valid)) {
            if (openride_gps_simulator_update(&gps_simulator,
                                              delta_seconds * OPENRIDE_GPS_SIMULATION_TIME_SCALE,
                                              &gps_sample)) {
                gps_sample_valid = true;
                openride_navigation_engine_update(&navigation,
                                                  gps_sample.lat,
                                                  gps_sample.lon,
                                                  gps_sample.speed_mps,
                                                  gps_sample.heading_deg,
                                                  &navigation_state);
                if (gpx_recording_active) {
                    record_gps_sample(&gpx_recording,
                                      &gps_sample,
                                      &gpx_last_recorded_position_m);
                }
                if (follow_gps && gps_simulator.active) {
                    camera.center_lat = gps_sample.lat;
                    camera.center_lon = gps_sample.lon;
                }
                if (navigation_state.status == OPENRIDE_NAVIGATION_ARRIVED
                    && gps_simulator.finished) {
                    snprintf(route_status, sizeof(route_status), "destination atteinte");
                }
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
        if (route_valid) {
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
        draw_selection(renderer,
                       &camera,
                       &selection,
                       !route_valid,
                       width,
                       height);
        if (gps_sample_valid) {
            draw_navigation_position(renderer,
                                     &camera,
                                     &gps_sample,
                                     &navigation_state,
                                     width,
                                     height);
        }
        draw_center_marker(renderer, width, height);
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
                     width,
                     height);
        draw_navigation_overlay(renderer,
                                &navigation_state,
                                &navigation_instructions,
                                &gps_simulator,
                                gps_sample_valid,
                                follow_gps,
                                simulator_deviation,
                                height);
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
                       width);
        draw_place_search_overlay(renderer,
                                  place_search_active,
                                  place_index != NULL,
                                  place_search_query,
                                  place_search_results,
                                  place_search_result_count,
                                  place_search_selected,
                                  width);
        SDL_RenderPresent(renderer);
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
