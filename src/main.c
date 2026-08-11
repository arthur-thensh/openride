#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "map/map_renderer.h"
#include "map/vector_map_renderer.h"
#include "openride/map_camera.h"
#include "openride/map_selection.h"
#include "openride/loop_generator.h"
#include "openride/mbtiles.h"
#include "openride/routing_engine.h"
#include "openride/routing_graph.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define OPENRIDE_CLICK_DRAG_THRESHOLD 5.0
#define OPENRIDE_MARKER_HIT_RADIUS 26.0
#define OPENRIDE_MAX_SNAP_DISTANCE_M 2000.0
#define OPENRIDE_LOOP_DISTANCE_STEP_M 25000.0
#define OPENRIDE_LOOP_DISTANCE_MIN_M 25000.0
#define OPENRIDE_LOOP_DISTANCE_MAX_M 300000.0

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
                         int viewport_width,
                         int viewport_height)
{
    const float panel_x = 10.0f;
    const float panel_y = 10.0f;
    const float panel_w = 500.0f;
    const float panel_h = 174.0f;
    SDL_FRect panel = {panel_x, panel_y, panel_w, panel_h};

    SDL_SetRenderDrawColor(renderer, 24, 28, 32, 218);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 75);
    SDL_RenderRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 247, 248, 249, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(renderer, panel_x + 12.0f, panel_y + 10.0f, "OpenRide v0.12");

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
                        "glisser: deplacer | clic droit: supprimer | C: effacer");

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
    char error[512] = {0};

    OpenRideMBTiles *map = openride_mbtiles_open(map_path, error, sizeof(error));
    if (!map) {
        fprintf(stderr, "Unable to open offline map: %s\n", map_path);
        fprintf(stderr, "Reason: %s\n", error[0] ? error : "unknown error");
        fprintf(stderr, "\nRun ./scripts/download_real_map.sh to install the real OSM map.\n");
        fprintf(stderr, "Usage: ./build/openride [path/to/map.mbtiles] [path/to/routing.orgraph]\n");
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
        openride_routing_graph_destroy(&routing_graph);
        openride_mbtiles_close(map);
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

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
        openride_routing_graph_destroy(&routing_graph);
        openride_mbtiles_close(map);
        SDL_Quit();
        return 1;
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
                    if (event.key.key == SDLK_ESCAPE) {
                        running = false;
                    } else if (event.key.key == SDLK_C) {
                        openride_map_selection_clear(&selection);
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
                    } else if (event.key.key == SDLK_O) {
                        loop_direction = openride_loop_direction_next(loop_direction);
                        if (loop_active) {
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
                            openride_route_destroy(&route);
                            route_valid = false;
                            loop_active = false;
                            loop_waypoint_count = 0U;
                        }
                        snprintf(route_status,
                                 sizeof(route_status),
                                 "boucle cible %.0f km | B pour generer",
                                 loop_target_distance_m / 1000.0);
                    } else if (event.key.key == SDLK_M && vector_map) {
                        map_style = openride_map_style_next(map_style);
                        openride_vector_map_renderer_set_style(&vector_renderer, map_style);
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
                        if (loop_active) {
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

                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
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
            route_valid = recalculate_route(&routing_graph,
                                            graph_loaded,
                                            &selection,
                                            routing_profile,
                                            &route,
                                            &start_snap,
                                            &destination_snap,
                                            route_status,
                                            sizeof(route_status));
            route_dirty = false;
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
                     width,
                     height);
        SDL_RenderPresent(renderer);
    }

    if (vector_map) {
        openride_vector_map_renderer_destroy(&vector_renderer);
    } else {
        openride_map_renderer_destroy(&raster_renderer);
    }

    openride_route_destroy(&route);
    openride_routing_graph_destroy(&routing_graph);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    openride_mbtiles_close(map);
    SDL_Quit();

    return 0;
}
