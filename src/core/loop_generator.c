#include "openride/loop_generator.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENRIDE_EARTH_RADIUS_M 6371008.8
#define OPENRIDE_LOOP_MIN_DISTANCE_M 5000.0
#define OPENRIDE_LOOP_MAX_DISTANCE_M 500000.0
#define OPENRIDE_LOOP_MIN_CANDIDATES 1U
#define OPENRIDE_LOOP_MAX_CANDIDATES 16U
#define OPENRIDE_LOOP_DEFAULT_SNAP_M 2500.0
#define OPENRIDE_LOOP_PI 3.14159265358979323846264338327950288

typedef struct OpenRideTraversal {
    uint64_t key;
    double length_m;
} OpenRideTraversal;

typedef struct OpenRideTraversalList {
    OpenRideTraversal *items;
    size_t count;
    size_t capacity;
} OpenRideTraversalList;

typedef struct OpenRideGeometryBuilder {
    OpenRideRoutePoint *points;
    uint32_t count;
    uint32_t capacity;
} OpenRideGeometryBuilder;

typedef struct OpenRideLoopCandidate {
    OpenRideRoute route;
    OpenRideRoutePoint waypoints[OPENRIDE_LOOP_MAX_WAYPOINTS];
    double score;
    double distance_error_ratio;
    double overlap_ratio;
    double max_waypoint_snap_distance_m;
} OpenRideLoopCandidate;

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
}

static double clampd(double value, double low, double high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static double radians(double degrees)
{
    return degrees * (OPENRIDE_LOOP_PI / 180.0);
}

static double degrees(double radians_value)
{
    return radians_value * (180.0 / OPENRIDE_LOOP_PI);
}

static double normalize_bearing(double bearing)
{
    bearing = fmod(bearing, 360.0);
    if (bearing < 0.0) bearing += 360.0;
    return bearing;
}

static OpenRideRoutePoint destination_point(OpenRideRoutePoint start,
                                            double bearing_deg,
                                            double distance_m)
{
    const double angular = distance_m / OPENRIDE_EARTH_RADIUS_M;
    const double bearing = radians(bearing_deg);
    const double lat1 = radians(start.lat);
    const double lon1 = radians(start.lon);
    const double sin_lat1 = sin(lat1);
    const double cos_lat1 = cos(lat1);
    const double sin_angular = sin(angular);
    const double cos_angular = cos(angular);

    const double lat2 = asin(sin_lat1 * cos_angular
                           + cos_lat1 * sin_angular * cos(bearing));
    const double lon2 = lon1 + atan2(sin(bearing) * sin_angular * cos_lat1,
                                     cos_angular - sin_lat1 * sin(lat2));

    OpenRideRoutePoint point;
    point.lat = degrees(lat2);
    point.lon = degrees(lon2);
    while (point.lon > 180.0) point.lon -= 360.0;
    while (point.lon < -180.0) point.lon += 360.0;
    return point;
}

static uint32_t random_next(uint32_t *state)
{
    uint32_t x = *state;
    if (x == 0U) x = 0x6d2b79f5U;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static double random_unit(uint32_t *state)
{
    return (double)(random_next(state) & 0x00ffffffU) / 16777215.0;
}

static double direction_bearing(OpenRideLoopDirection direction)
{
    switch (direction) {
        case OPENRIDE_LOOP_DIRECTION_NORTH: return 0.0;
        case OPENRIDE_LOOP_DIRECTION_EAST:  return 90.0;
        case OPENRIDE_LOOP_DIRECTION_SOUTH: return 180.0;
        case OPENRIDE_LOOP_DIRECTION_WEST:  return 270.0;
        case OPENRIDE_LOOP_DIRECTION_ANY:
        default:                            return 0.0;
    }
}

static double candidate_bearing(const OpenRideLoopRequest *request,
                                uint32_t candidate_index,
                                uint32_t *random_state)
{
    const double jitter = (random_unit(random_state) - 0.5) * 16.0;

    if (request->direction == OPENRIDE_LOOP_DIRECTION_ANY) {
        const double step = 360.0 / (double)request->candidate_count;
        return normalize_bearing(step * (double)candidate_index + jitter);
    }

    const double base = direction_bearing(request->direction);
    if (request->candidate_count <= 1U) return normalize_bearing(base + jitter);

    const double t = (double)candidate_index / (double)(request->candidate_count - 1U);
    const double spread = -35.0 + t * 70.0;
    return normalize_bearing(base + spread + jitter);
}

static const OpenRideRoutingEdge *find_edge(const OpenRideRoutingGraph *graph,
                                            OpenRideRoutingNodeId from,
                                            OpenRideRoutingNodeId to)
{
    if (!graph || from >= graph->node_count || to >= graph->node_count) return NULL;
    const OpenRideRoutingNode *node = &graph->nodes[from];
    for (uint32_t i = 0U; i < node->edge_count; ++i) {
        const OpenRideRoutingEdge *edge = &graph->edges[node->first_edge + i];
        if (edge->target == to) return edge;
    }
    return NULL;
}

static bool edge_allowed_for_loop(const OpenRideRoutingEdge *edge,
                                  const OpenRideLoopRequest *request)
{
    if (!edge || !request) return false;
    if (request->avoid_tolls && (edge->flags & OPENRIDE_EDGE_FLAG_TOLL) != 0U) {
        return false;
    }
    if (request->avoid_ferries && (edge->flags & OPENRIDE_EDGE_FLAG_FERRY) != 0U) {
        return false;
    }
    return true;
}

static double default_speed_kph(OpenRideRoadClass road_class)
{
    switch (road_class) {
        case OPENRIDE_ROAD_MOTORWAY:      return 110.0;
        case OPENRIDE_ROAD_TRUNK:         return 90.0;
        case OPENRIDE_ROAD_PRIMARY:       return 80.0;
        case OPENRIDE_ROAD_SECONDARY:     return 80.0;
        case OPENRIDE_ROAD_TERTIARY:      return 70.0;
        case OPENRIDE_ROAD_UNCLASSIFIED:  return 60.0;
        case OPENRIDE_ROAD_RESIDENTIAL:   return 50.0;
        case OPENRIDE_ROAD_SERVICE:       return 30.0;
        case OPENRIDE_ROAD_LIVING_STREET: return 20.0;
        case OPENRIDE_ROAD_TRACK:         return 25.0;
        case OPENRIDE_ROAD_PATH:          return 15.0;
        case OPENRIDE_ROAD_OTHER:
        case OPENRIDE_ROAD_UNKNOWN:
        default:                          return 40.0;
    }
}

static double edge_speed_kph(const OpenRideRoutingEdge *edge)
{
    double speed = edge->max_speed_kph > 0U
        ? (double)edge->max_speed_kph
        : default_speed_kph((OpenRideRoadClass)edge->road_class);
    if (speed < 5.0) speed = 5.0;
    if (speed > 130.0) speed = 130.0;
    return speed;
}

static bool geometry_reserve(OpenRideGeometryBuilder *builder, uint32_t needed)
{
    if (needed <= builder->capacity) return true;
    uint32_t capacity = builder->capacity == 0U ? 256U : builder->capacity;
    while (capacity < needed) {
        if (capacity > UINT32_MAX / 2U) return false;
        capacity *= 2U;
    }
    OpenRideRoutePoint *points = realloc(builder->points,
                                         (size_t)capacity * sizeof(*points));
    if (!points) return false;
    builder->points = points;
    builder->capacity = capacity;
    return true;
}

static bool points_nearly_equal(OpenRideRoutePoint a, OpenRideRoutePoint b)
{
    return fabs(a.lat - b.lat) < 1e-10 && fabs(a.lon - b.lon) < 1e-10;
}

static bool geometry_append_point(OpenRideGeometryBuilder *builder,
                                  OpenRideRoutePoint point)
{
    if (builder->count > 0U
        && points_nearly_equal(builder->points[builder->count - 1U], point)) {
        return true;
    }
    if (!geometry_reserve(builder, builder->count + 1U)) return false;
    builder->points[builder->count++] = point;
    return true;
}

static bool geometry_append_route(OpenRideGeometryBuilder *builder,
                                  const OpenRideRoute *route)
{
    if (!route || !route->geometry) return route && route->geometry_count == 0U;
    for (uint32_t i = 0U; i < route->geometry_count; ++i) {
        if (!geometry_append_point(builder, route->geometry[i])) return false;
    }
    return true;
}

static bool traversal_reserve(OpenRideTraversalList *list, size_t needed)
{
    if (needed <= list->capacity) return true;
    size_t capacity = list->capacity == 0U ? 256U : list->capacity;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2U) return false;
        capacity *= 2U;
    }
    OpenRideTraversal *items = realloc(list->items, capacity * sizeof(*items));
    if (!items) return false;
    list->items = items;
    list->capacity = capacity;
    return true;
}

static uint64_t traversal_key(OpenRideRoutingNodeId a, OpenRideRoutingNodeId b)
{
    const uint32_t low = a < b ? a : b;
    const uint32_t high = a < b ? b : a;
    return ((uint64_t)low << 32) | (uint64_t)high;
}

static bool traversal_append(OpenRideTraversalList *list,
                             OpenRideRoutingNodeId a,
                             OpenRideRoutingNodeId b,
                             double length_m)
{
    if (!traversal_reserve(list, list->count + 1U)) return false;
    list->items[list->count].key = traversal_key(a, b);
    list->items[list->count].length_m = length_m;
    ++list->count;
    return true;
}

static bool collect_route_traversals(const OpenRideRoutingGraph *graph,
                                     const OpenRideRoute *route,
                                     OpenRideTraversalList *list)
{
    if (!route || route->node_count < 2U) return true;
    for (uint32_t i = 1U; i < route->node_count; ++i) {
        const OpenRideRoutingEdge *edge = find_edge(graph,
                                                    route->nodes[i - 1U],
                                                    route->nodes[i]);
        if (!edge) continue;
        if (!traversal_append(list,
                              route->nodes[i - 1U],
                              route->nodes[i],
                              (double)edge->length_cm / 100.0)) {
            return false;
        }
    }
    return true;
}

static int traversal_compare(const void *lhs, const void *rhs)
{
    const OpenRideTraversal *a = lhs;
    const OpenRideTraversal *b = rhs;
    if (a->key < b->key) return -1;
    if (a->key > b->key) return 1;
    return 0;
}

static double repeated_distance_m(OpenRideTraversalList *list)
{
    if (!list || list->count < 2U) return 0.0;
    qsort(list->items, list->count, sizeof(list->items[0]), traversal_compare);

    double repeated = 0.0;
    size_t group_start = 0U;
    while (group_start < list->count) {
        size_t group_end = group_start + 1U;
        while (group_end < list->count
               && list->items[group_end].key == list->items[group_start].key) {
            ++group_end;
        }
        for (size_t i = group_start + 1U; i < group_end; ++i) {
            repeated += list->items[i].length_m;
        }
        group_start = group_end;
    }
    return repeated;
}

static bool choose_start_endpoint(const OpenRideRoutingGraph *graph,
                                  const OpenRideLoopRequest *request,
                                  bool departure,
                                  OpenRideRoutingNodeId *node,
                                  const OpenRideRoutingEdge **partial_edge,
                                  double *partial_fraction)
{
    const OpenRideRoutingSnap *snap = &request->start;
    const double eps = 1e-9;
    if (snap->fraction <= eps) {
        *node = snap->a;
        *partial_edge = NULL;
        *partial_fraction = 0.0;
        return true;
    }
    if (1.0 - snap->fraction <= eps) {
        *node = snap->b;
        *partial_edge = NULL;
        *partial_fraction = 0.0;
        return true;
    }

    const OpenRideRoutingEdge *a_to_b = find_edge(graph, snap->a, snap->b);
    const OpenRideRoutingEdge *b_to_a = find_edge(graph, snap->b, snap->a);
    bool have = false;
    double best_fraction = 2.0;
    OpenRideRoutingNodeId best_node = OPENRIDE_ROUTING_NODE_NONE;
    const OpenRideRoutingEdge *best_edge = NULL;

    if (departure) {
        if (b_to_a && edge_allowed_for_loop(b_to_a, request)
            && snap->fraction < best_fraction) {
            have = true;
            best_fraction = snap->fraction;
            best_node = snap->a;
            best_edge = b_to_a;
        }
        const double to_b = 1.0 - snap->fraction;
        if (a_to_b && edge_allowed_for_loop(a_to_b, request) && to_b < best_fraction) {
            have = true;
            best_fraction = to_b;
            best_node = snap->b;
            best_edge = a_to_b;
        }
    } else {
        if (a_to_b && edge_allowed_for_loop(a_to_b, request)
            && snap->fraction < best_fraction) {
            have = true;
            best_fraction = snap->fraction;
            best_node = snap->a;
            best_edge = a_to_b;
        }
        const double from_b = 1.0 - snap->fraction;
        if (b_to_a && edge_allowed_for_loop(b_to_a, request) && from_b < best_fraction) {
            have = true;
            best_fraction = from_b;
            best_node = snap->b;
            best_edge = b_to_a;
        }
    }

    if (!have) return false;
    *node = best_node;
    *partial_edge = best_edge;
    *partial_fraction = best_fraction;
    return true;
}

static bool snap_waypoint_to_node(const OpenRideRoutingGraph *graph,
                                  OpenRideRoutePoint requested,
                                  double max_distance_m,
                                  OpenRideRoutePoint *snapped,
                                  OpenRideRoutingNodeId *node,
                                  double *snap_distance_m)
{
    double distance = 0.0;
    const OpenRideRoutingNodeId id = openride_routing_graph_nearest_node(
        graph, requested.lat, requested.lon, &distance);
    if (id == OPENRIDE_ROUTING_NODE_NONE || distance > max_distance_m) return false;

    double lat = 0.0;
    double lon = 0.0;
    openride_routing_node_geo(&graph->nodes[id], &lat, &lon);
    *snapped = (OpenRideRoutePoint){lat, lon};
    *node = id;
    if (snap_distance_m) *snap_distance_m = distance;
    return true;
}

static bool calculate_leg(const OpenRideRoutingGraph *graph,
                          const OpenRideLoopRequest *request,
                          OpenRideRoutingNodeId start,
                          OpenRideRoutingNodeId destination,
                          OpenRideRoute *route)
{
    OpenRideRoutingRequest routing = openride_routing_request_default();
    routing.start = start;
    routing.destination = destination;
    routing.profile = request->profile;
    routing.avoid_tolls = request->avoid_tolls;
    routing.avoid_ferries = request->avoid_ferries;
    char error[128] = {0};
    return openride_routing_engine_calculate(graph,
                                             &routing,
                                             route,
                                             error,
                                             sizeof(error));
}

static void add_partial_metrics(const OpenRideRoutingEdge *edge,
                                double fraction,
                                OpenRideRoute *route)
{
    if (!edge || fraction <= 0.0 || !route) return;
    const double length_m = ((double)edge->length_cm / 100.0) * fraction;
    route->distance_m += length_m;
    route->estimated_time_s += length_m / (edge_speed_kph(edge) / 3.6);
    route->weighted_cost_s += length_m / (edge_speed_kph(edge) / 3.6);
}

static bool generate_candidate(const OpenRideRoutingGraph *graph,
                               const OpenRideLoopRequest *request,
                               uint32_t candidate_index,
                               uint32_t *random_state,
                               OpenRideLoopCandidate *candidate)
{
    memset(candidate, 0, sizeof(*candidate));

    OpenRideRoutingNodeId departure_node = OPENRIDE_ROUTING_NODE_NONE;
    OpenRideRoutingNodeId arrival_node = OPENRIDE_ROUTING_NODE_NONE;
    const OpenRideRoutingEdge *departure_edge = NULL;
    const OpenRideRoutingEdge *arrival_edge = NULL;
    double departure_fraction = 0.0;
    double arrival_fraction = 0.0;

    if (!choose_start_endpoint(graph,
                               request,
                               true,
                               &departure_node,
                               &departure_edge,
                               &departure_fraction)
        || !choose_start_endpoint(graph,
                                  request,
                                  false,
                                  &arrival_node,
                                  &arrival_edge,
                                  &arrival_fraction)) {
        return false;
    }

    const double bearing = candidate_bearing(request, candidate_index, random_state);
    const double radius_variation = 0.85 + random_unit(random_state) * 0.25;
    const double radius_m = request->target_distance_m
                          / (2.0 * OPENRIDE_LOOP_PI)
                          * radius_variation;
    const OpenRideRoutePoint start = {request->start.lat, request->start.lon};
    const OpenRideRoutePoint center = destination_point(start, bearing, radius_m);
    const double start_bearing_from_center = normalize_bearing(bearing + 180.0);
    const double orientation = (candidate_index & 1U) == 0U ? 1.0 : -1.0;

    OpenRideRoutingNodeId waypoint_nodes[OPENRIDE_LOOP_MAX_WAYPOINTS];
    double max_snap = 0.0;
    for (uint32_t i = 0U; i < OPENRIDE_LOOP_MAX_WAYPOINTS; ++i) {
        const double around = start_bearing_from_center
                            + orientation * 90.0 * (double)(i + 1U);
        const OpenRideRoutePoint requested_waypoint = destination_point(
            center, around, radius_m);
        double snap_distance = 0.0;
        if (!snap_waypoint_to_node(graph,
                                   requested_waypoint,
                                   request->max_waypoint_snap_distance_m,
                                   &candidate->waypoints[i],
                                   &waypoint_nodes[i],
                                   &snap_distance)) {
            return false;
        }
        if (snap_distance > max_snap) max_snap = snap_distance;
    }

    OpenRideRoutingNodeId leg_starts[4] = {
        departure_node,
        waypoint_nodes[0],
        waypoint_nodes[1],
        waypoint_nodes[2]
    };
    OpenRideRoutingNodeId leg_ends[4] = {
        waypoint_nodes[0],
        waypoint_nodes[1],
        waypoint_nodes[2],
        arrival_node
    };

    OpenRideGeometryBuilder geometry = {0};
    OpenRideTraversalList traversals = {0};
    bool ok = geometry_append_point(&geometry, start);
    double distance_m = 0.0;
    double estimated_time_s = 0.0;
    double weighted_cost_s = 0.0;

    if (ok && departure_fraction > 0.0) {
        double lat = 0.0;
        double lon = 0.0;
        openride_routing_node_geo(&graph->nodes[departure_node], &lat, &lon);
        ok = geometry_append_point(&geometry, (OpenRideRoutePoint){lat, lon});
        const double partial_length = ((double)departure_edge->length_cm / 100.0)
                                    * departure_fraction;
        ok = ok && traversal_append(&traversals,
                                    request->start.a,
                                    request->start.b,
                                    partial_length);
    }

    for (uint32_t leg_index = 0U; ok && leg_index < 4U; ++leg_index) {
        OpenRideRoute leg = {0};
        if (!calculate_leg(graph,
                           request,
                           leg_starts[leg_index],
                           leg_ends[leg_index],
                           &leg)) {
            ok = false;
            openride_route_destroy(&leg);
            break;
        }
        ok = geometry_append_route(&geometry, &leg)
          && collect_route_traversals(graph, &leg, &traversals);
        distance_m += leg.distance_m;
        estimated_time_s += leg.estimated_time_s;
        weighted_cost_s += leg.weighted_cost_s;
        openride_route_destroy(&leg);
    }

    if (ok && arrival_fraction > 0.0) {
        const double partial_length = ((double)arrival_edge->length_cm / 100.0)
                                    * arrival_fraction;
        ok = traversal_append(&traversals,
                              request->start.a,
                              request->start.b,
                              partial_length)
          && geometry_append_point(&geometry, start);
    } else if (ok) {
        ok = geometry_append_point(&geometry, start);
    }

    if (!ok) {
        free(geometry.points);
        free(traversals.items);
        return false;
    }

    OpenRideRoute combined = {0};
    combined.geometry = geometry.points;
    combined.geometry_count = geometry.count;
    combined.distance_m = distance_m;
    combined.estimated_time_s = estimated_time_s;
    combined.weighted_cost_s = weighted_cost_s;
    add_partial_metrics(departure_edge, departure_fraction, &combined);
    add_partial_metrics(arrival_edge, arrival_fraction, &combined);

    const double repeated_m = repeated_distance_m(&traversals);
    free(traversals.items);

    const double distance_error = fabs(combined.distance_m - request->target_distance_m)
                                / request->target_distance_m;
    const double overlap_ratio = combined.distance_m > 1.0
        ? clampd(repeated_m / combined.distance_m, 0.0, 1.0)
        : 1.0;
    const double distance_fit = clampd(1.0 - distance_error / 0.40, 0.0, 1.0);
    const double overlap_fit = clampd(1.0 - overlap_ratio / 0.30, 0.0, 1.0);
    const double score = 100.0 * (0.72 * distance_fit + 0.28 * overlap_fit);

    candidate->route = combined;
    candidate->score = score;
    candidate->distance_error_ratio = distance_error;
    candidate->overlap_ratio = overlap_ratio;
    candidate->max_waypoint_snap_distance_m = max_snap;
    return true;
}

OpenRideLoopRequest openride_loop_request_default(void)
{
    OpenRideLoopRequest request;
    memset(&request, 0, sizeof(request));
    request.start.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    request.start.a = OPENRIDE_ROUTING_NODE_NONE;
    request.start.b = OPENRIDE_ROUTING_NODE_NONE;
    request.profile = OPENRIDE_ROUTING_PROFILE_TOURING;
    request.direction = OPENRIDE_LOOP_DIRECTION_ANY;
    request.target_distance_m = 100000.0;
    request.max_waypoint_snap_distance_m = OPENRIDE_LOOP_DEFAULT_SNAP_M;
    request.candidate_count = 6U;
    request.seed = 0x4f70656eU;
    request.avoid_tolls = false;
    request.avoid_ferries = false;
    return request;
}

void openride_loop_result_destroy(OpenRideLoopResult *result)
{
    if (!result) return;
    openride_route_destroy(&result->route);
    memset(result, 0, sizeof(*result));
}

static bool validate_request(const OpenRideRoutingGraph *graph,
                             const OpenRideLoopRequest *request,
                             char *error,
                             size_t error_size)
{
    if (!graph || !request) {
        set_error(error, error_size, "invalid loop generator arguments");
        return false;
    }
    if (request->start.segment_id == OPENRIDE_ROUTING_SEGMENT_NONE
        || request->start.segment_id >= graph->segment_index.segment_count
        || request->start.a >= graph->node_count
        || request->start.b >= graph->node_count) {
        set_error(error, error_size, "invalid loop start position");
        return false;
    }
    if (!isfinite(request->target_distance_m)
        || request->target_distance_m < OPENRIDE_LOOP_MIN_DISTANCE_M
        || request->target_distance_m > OPENRIDE_LOOP_MAX_DISTANCE_M) {
        set_error(error, error_size, "loop target distance must be between 5 and 500 km");
        return false;
    }
    if (request->candidate_count < OPENRIDE_LOOP_MIN_CANDIDATES
        || request->candidate_count > OPENRIDE_LOOP_MAX_CANDIDATES) {
        set_error(error, error_size, "loop candidate count must be between 1 and 16");
        return false;
    }
    if (!isfinite(request->max_waypoint_snap_distance_m)
        || request->max_waypoint_snap_distance_m <= 0.0) {
        set_error(error, error_size, "invalid waypoint snap distance");
        return false;
    }
    if (request->profile < OPENRIDE_ROUTING_PROFILE_FASTEST
        || request->profile > OPENRIDE_ROUTING_PROFILE_TRAIL) {
        set_error(error, error_size, "invalid loop routing profile");
        return false;
    }
    if (request->direction < OPENRIDE_LOOP_DIRECTION_ANY
        || request->direction > OPENRIDE_LOOP_DIRECTION_WEST) {
        set_error(error, error_size, "invalid loop direction");
        return false;
    }
    return true;
}

bool openride_loop_generator_generate(const OpenRideRoutingGraph *graph,
                                      const OpenRideLoopRequest *request,
                                      OpenRideLoopResult *result,
                                      char *error,
                                      size_t error_size)
{
    if (!result || !validate_request(graph, request, error, error_size)) return false;

    OpenRideLoopResult generated;
    memset(&generated, 0, sizeof(generated));
    generated.stats.attempted_candidates = request->candidate_count;

    bool have_best = false;
    uint32_t random_state = request->seed == 0U ? 0x4f70656eU : request->seed;

    for (uint32_t i = 0U; i < request->candidate_count; ++i) {
        OpenRideLoopCandidate candidate;
        if (!generate_candidate(graph, request, i, &random_state, &candidate)) {
            continue;
        }
        ++generated.stats.successful_candidates;

        if (!have_best || candidate.score > generated.stats.score) {
            openride_route_destroy(&generated.route);
            generated.route = candidate.route;
            memset(&candidate.route, 0, sizeof(candidate.route));
            memcpy(generated.waypoints,
                   candidate.waypoints,
                   sizeof(generated.waypoints));
            generated.waypoint_count = OPENRIDE_LOOP_MAX_WAYPOINTS;
            generated.stats.score = candidate.score;
            generated.stats.distance_error_ratio = candidate.distance_error_ratio;
            generated.stats.overlap_ratio = candidate.overlap_ratio;
            generated.stats.max_waypoint_snap_distance_m =
                candidate.max_waypoint_snap_distance_m;
            have_best = true;
        }
        openride_route_destroy(&candidate.route);
    }

    if (!have_best) {
        openride_loop_result_destroy(&generated);
        set_error(error, error_size, "no loop candidate could be routed");
        return false;
    }

    openride_loop_result_destroy(result);
    *result = generated;
    set_error(error, error_size, "");
    return true;
}

const char *openride_loop_direction_name(OpenRideLoopDirection direction)
{
    switch (direction) {
        case OPENRIDE_LOOP_DIRECTION_ANY:   return "libre";
        case OPENRIDE_LOOP_DIRECTION_NORTH: return "nord";
        case OPENRIDE_LOOP_DIRECTION_EAST:  return "est";
        case OPENRIDE_LOOP_DIRECTION_SOUTH: return "sud";
        case OPENRIDE_LOOP_DIRECTION_WEST:  return "ouest";
        default:                            return "?";
    }
}

OpenRideLoopDirection openride_loop_direction_next(OpenRideLoopDirection direction)
{
    switch (direction) {
        case OPENRIDE_LOOP_DIRECTION_ANY:   return OPENRIDE_LOOP_DIRECTION_NORTH;
        case OPENRIDE_LOOP_DIRECTION_NORTH: return OPENRIDE_LOOP_DIRECTION_EAST;
        case OPENRIDE_LOOP_DIRECTION_EAST:  return OPENRIDE_LOOP_DIRECTION_SOUTH;
        case OPENRIDE_LOOP_DIRECTION_SOUTH: return OPENRIDE_LOOP_DIRECTION_WEST;
        case OPENRIDE_LOOP_DIRECTION_WEST:
        default:                            return OPENRIDE_LOOP_DIRECTION_ANY;
    }
}
