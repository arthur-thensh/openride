#include "app_support_runtime.h"

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

#ifdef __ANDROID__
void openride_android_missed_turn_dev_init(
    OpenRideAndroidMissedTurnDev *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    openride_gps_simulator_init(&state->simulator);
    openride_dev_missed_turn_plan_init(&state->plan);
}

void openride_android_missed_turn_dev_reset(
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

void openride_android_missed_turn_dev_destroy(
    OpenRideAndroidMissedTurnDev *state,
    OpenRideSimulatedLocationContext *location_context,
    OpenRideGPSSimulator *base_simulator)
{
    if (!state) return;
    openride_android_missed_turn_dev_reset(
        state, location_context, base_simulator);
    openride_gps_simulator_destroy(&state->simulator);
}

#endif

bool SDLCALL openride_app_support_lifecycle_event_watch(void *userdata, SDL_Event *event)
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

double openride_app_support_clampd(double value, double min_value, double max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

bool openride_app_support_file_exists(const char *path)
{
    return openride_platform_file_exists(path);
}

bool openride_app_support_is_vector_map(const OpenRideMBTilesMetadata *metadata)
{
    if (!metadata) return false;
    return strcmp(metadata->format, "pbf") == 0
        || strcmp(metadata->format, "mvt") == 0
        || strcmp(metadata->format, "application/x-protobuf") == 0;
}

bool openride_app_support_has_suffix(const char *text, const char *suffix)
{
    if (!text || !suffix) return false;
    const size_t text_len = strlen(text);
    const size_t suffix_len = strlen(suffix);
    return text_len >= suffix_len
        && strcmp(text + text_len - suffix_len, suffix) == 0;
}

const char *openride_app_support_default_map_path(void)
{
    static const char *ormap = "data/maps/nord-pas-de-calais.ormap";
    static const char *legacy_map = "data/maps/nord-pas-de-calais-shortbread.mbtiles";
    static const char *demo_map = "data/maps/demo.mbtiles";

    if (openride_app_support_file_exists(ormap)) return ormap;
    if (openride_app_support_file_exists(legacy_map)) return legacy_map;
    return demo_map;
}

const char *openride_app_support_default_routing_graph_path(void)
{
    static const char *graph = "data/routing/nord-pas-de-calais.orgraph";
    return openride_app_support_file_exists(graph) ? graph : NULL;
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

static void navigation_marker_point(float cx,
                                    float cy,
                                    float angle_rad,
                                    float local_x,
                                    float local_y,
                                    float *out_x,
                                    float *out_y)
{
    const float c = cosf(angle_rad);
    const float s = sinf(angle_rad);
    if (out_x) *out_x = cx + local_x * c - local_y * s;
    if (out_y) *out_y = cy + local_x * s + local_y * c;
}

static void draw_filled_triangle(SDL_Renderer *renderer,
                                 float x1,
                                 float y1,
                                 float x2,
                                 float y2,
                                 float x3,
                                 float y3,
                                 Uint8 r,
                                 Uint8 g,
                                 Uint8 b,
                                 Uint8 a)
{
    SDL_Vertex vertices[3] = {
        {{x1, y1}, {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f}, {0.0f, 0.0f}},
        {{x2, y2}, {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f}, {0.0f, 0.0f}},
        {{x3, y3}, {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f}, {0.0f, 0.0f}}
    };
    SDL_RenderGeometry(renderer, NULL, vertices, 3, NULL, 0);
}

static void draw_navigation_motorcycle(SDL_Renderer *renderer,
                                       float cx,
                                       float cy,
                                       double heading_rad)
{
    if (!renderer) return;

    /*
     * Vector-only navigation marker. The geometry is expressed in logical
     * pixels then scaled by the display density, so the motorcycle stays sharp
     * and physically readable without any bitmap asset or platform SVG stack.
     */
    const float scale = openride_app_render_ui_scale(renderer);
    const float angle = (float)heading_rad;
    const int outer_width = (int)lroundf(5.4f * scale);
    const int inner_width = (int)lroundf(3.1f * scale);
    const int handle_outer_width = (int)lroundf(3.2f * scale);
    const int handle_inner_width = (int)lroundf(1.7f * scale);

    float front_x = 0.0f, front_y = 0.0f;
    float rear_x = 0.0f, rear_y = 0.0f;
    float bar_left_x = 0.0f, bar_left_y = 0.0f;
    float bar_right_x = 0.0f, bar_right_y = 0.0f;
    float nose_x = 0.0f, nose_y = 0.0f;
    float nose_left_x = 0.0f, nose_left_y = 0.0f;
    float nose_right_x = 0.0f, nose_right_y = 0.0f;

    navigation_marker_point(cx, cy, angle,
                            0.0f, -8.0f * scale,
                            &front_x, &front_y);
    navigation_marker_point(cx, cy, angle,
                            0.0f, 8.5f * scale,
                            &rear_x, &rear_y);
    navigation_marker_point(cx, cy, angle,
                            -5.7f * scale, -4.6f * scale,
                            &bar_left_x, &bar_left_y);
    navigation_marker_point(cx, cy, angle,
                            5.7f * scale, -4.6f * scale,
                            &bar_right_x, &bar_right_y);
    navigation_marker_point(cx, cy, angle,
                            0.0f, -14.5f * scale,
                            &nose_x, &nose_y);
    navigation_marker_point(cx, cy, angle,
                            -4.8f * scale, -7.0f * scale,
                            &nose_left_x, &nose_left_y);
    navigation_marker_point(cx, cy, angle,
                            4.8f * scale, -7.0f * scale,
                            &nose_right_x, &nose_right_y);

    /* Compact shadow separates the icon from dense ORMap line work. */
    SDL_SetRenderDrawColor(renderer, 15, 26, 36, 145);
    draw_filled_circle(renderer,
                       cx + 1.3f * scale,
                       cy + 1.8f * scale,
                       7.2f * scale);

    /* White casing, then OpenRide blue body/wheels. */
    SDL_SetRenderDrawColor(renderer, 250, 252, 253, 255);
    draw_thick_line(renderer,
                    rear_x, rear_y, front_x, front_y,
                    outer_width);
    draw_filled_circle(renderer, front_x, front_y, 3.6f * scale);
    draw_filled_circle(renderer, rear_x, rear_y, 3.6f * scale);
    draw_thick_line(renderer,
                    bar_left_x, bar_left_y,
                    bar_right_x, bar_right_y,
                    handle_outer_width);
    draw_filled_triangle(renderer,
                         nose_x, nose_y,
                         nose_left_x, nose_left_y,
                         nose_right_x, nose_right_y,
                         250, 252, 253, 255);

    SDL_SetRenderDrawColor(renderer, 25, 118, 210, 255);
    draw_thick_line(renderer,
                    rear_x, rear_y, front_x, front_y,
                    inner_width);
    draw_filled_circle(renderer, front_x, front_y, 2.2f * scale);
    draw_filled_circle(renderer, rear_x, rear_y, 2.2f * scale);
    draw_thick_line(renderer,
                    bar_left_x, bar_left_y,
                    bar_right_x, bar_right_y,
                    handle_inner_width);

    float inner_tip_x = 0.0f, inner_tip_y = 0.0f;
    float inner_left_x = 0.0f, inner_left_y = 0.0f;
    float inner_right_x = 0.0f, inner_right_y = 0.0f;
    navigation_marker_point(cx, cy, angle,
                            0.0f, -12.7f * scale,
                            &inner_tip_x, &inner_tip_y);
    navigation_marker_point(cx, cy, angle,
                            -3.0f * scale, -7.5f * scale,
                            &inner_left_x, &inner_left_y);
    navigation_marker_point(cx, cy, angle,
                            3.0f * scale, -7.5f * scale,
                            &inner_right_x, &inner_right_y);
    draw_filled_triangle(renderer,
                         inner_tip_x, inner_tip_y,
                         inner_left_x, inner_left_y,
                         inner_right_x, inner_right_y,
                         25, 118, 210, 255);
}

void openride_app_render_scaled_text(SDL_Renderer *renderer,
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

void openride_app_render_center_marker(SDL_Renderer *renderer, int width, int height)
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

OpenRideSelectionMarker openride_app_render_marker_at_screen(const OpenRideMapCamera *camera,
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

static void app_render_route_styled(SDL_Renderer *renderer,
                                    const OpenRideMapCamera *camera,
                                    const OpenRideRoutingGraph *graph,
                                    const OpenRideRoute *route,
                                    int viewport_width,
                                    int viewport_height,
                                    Uint8 route_r,
                                    Uint8 route_g,
                                    Uint8 route_b,
                                    bool navigation_emphasis)
{
    if (!renderer || !camera || !route) return;

    const float ui_scale = navigation_emphasis
        ? openride_app_render_ui_scale(renderer) : 1.0f;
    int outer_width = navigation_emphasis
        ? (int)lroundf(6.5f * ui_scale) : 10;
    int inner_width = navigation_emphasis
        ? (int)lroundf(3.5f * ui_scale) : 5;
    if (outer_width < inner_width + 4) outer_width = inner_width + 4;

    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 0) {
            if (navigation_emphasis) {
                SDL_SetRenderDrawColor(renderer, 16, 35, 52, 235);
            } else {
                SDL_SetRenderDrawColor(renderer, 250, 250, 248, 225);
            }
        } else {
            SDL_SetRenderDrawColor(renderer, route_r, route_g, route_b, 250);
        }
        const int width = pass == 0 ? outer_width : inner_width;

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
                draw_thick_line(renderer, (float)a.x, (float)a.y,
                                (float)b.x, (float)b.y, width);
            }
            continue;
        }

        if (!graph || route->node_count < 2U) continue;
        for (uint32_t i = 1U; i < route->node_count; ++i) {
            const OpenRideRoutingNodeId a_id = route->nodes[i - 1U];
            const OpenRideRoutingNodeId b_id = route->nodes[i];
            if (a_id >= graph->node_count || b_id >= graph->node_count) continue;

            double a_lat = 0.0, a_lon = 0.0, b_lat = 0.0, b_lon = 0.0;
            openride_routing_node_geo(&graph->nodes[a_id], &a_lat, &a_lon);
            openride_routing_node_geo(&graph->nodes[b_id], &b_lat, &b_lon);
            const OpenRidePointD a = openride_geo_to_screen(
                camera, a_lat, a_lon, viewport_width, viewport_height);
            const OpenRidePointD b = openride_geo_to_screen(
                camera, b_lat, b_lon, viewport_width, viewport_height);
            draw_thick_line(renderer, (float)a.x, (float)a.y,
                            (float)b.x, (float)b.y, width);
        }
    }
}

void openride_app_render_route(SDL_Renderer *renderer,
                       const OpenRideMapCamera *camera,
                       const OpenRideRoutingGraph *graph,
                       const OpenRideRoute *route,
                       int viewport_width,
                       int viewport_height)
{
    app_render_route_styled(renderer, camera, graph, route,
                            viewport_width, viewport_height,
                            25, 118, 210, true);
}

void openride_app_render_route_preview(SDL_Renderer *renderer,
                               const OpenRideMapCamera *camera,
                               const OpenRideRoute *route,
                               int viewport_width,
                               int viewport_height)
{
    /*
     * Preview is intentionally geometry-only: regional node ids from a
     * temporary graph must never be resolved through the active graph.
     */
    app_render_route_styled(renderer, camera, NULL, route,
                            viewport_width, viewport_height,
                            184, 32, 255, false);
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

void openride_app_render_gpx_document(SDL_Renderer *renderer,
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

void openride_app_render_loop_waypoints(SDL_Renderer *renderer,
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

void openride_app_render_snap_connector(SDL_Renderer *renderer,
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

void openride_app_render_selection(SDL_Renderer *renderer,
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

SDL_Rect openride_app_render_safe_area(SDL_Renderer *renderer,
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

float openride_app_render_ui_scale(SDL_Renderer *renderer)
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

void openride_app_render_navigation_position(SDL_Renderer *renderer,
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
    OpenRidePointD rider = raw;

    if (navigation && navigation->valid) {
        const OpenRidePointD matched = openride_geo_to_screen(camera,
                                                               navigation->matched_lat,
                                                               navigation->matched_lon,
                                                               viewport_width,
                                                               viewport_height);
        if (navigation->status == OPENRIDE_NAVIGATION_OFF_ROUTE) {
            /*
             * Off-route keeps the motorcycle at the real GPS position and
             * shows where the matcher still projects onto the planned route.
             */
            SDL_SetRenderDrawColor(renderer, 54, 65, 76, 180);
            draw_thick_line(renderer,
                            (float)raw.x,
                            (float)raw.y,
                            (float)matched.x,
                            (float)matched.y,
                            2);
            SDL_SetRenderDrawColor(renderer, 248, 248, 246, 245);
            draw_filled_circle(renderer, (float)matched.x, (float)matched.y, 5.0f);
            SDL_SetRenderDrawColor(renderer, 35, 112, 190, 245);
            draw_filled_circle(renderer, (float)matched.x, (float)matched.y, 3.0f);
        } else {
            /* Normal navigation exposes one unambiguous rider marker. */
            rider = matched;
        }
    }

    const double heading_rad = (gps->heading_deg - camera->bearing_deg)
        * 0.01745329251994329577;
    draw_navigation_motorcycle(renderer,
                               (float)rider.x,
                               (float)rider.y,
                               heading_rad);
}
