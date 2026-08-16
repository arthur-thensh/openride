#include "openride/loop_generator.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENRIDE_EARTH_RADIUS_M 6371008.8
#define OPENRIDE_LOOP_MIN_DISTANCE_M 5000.0
#define OPENRIDE_LOOP_MAX_DISTANCE_M 500000.0
#define OPENRIDE_LOOP_MIN_CANDIDATES 1U
#define OPENRIDE_LOOP_DEFAULT_MAX_SNAP_M 2500.0
#define OPENRIDE_LOOP_DEFAULT_PREFERRED_SNAP_M 60.0
#define OPENRIDE_LOOP_PLACEMENT_VARIANTS 6U
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
    double shape_score;
    double waypoint_quality_score;
} OpenRideLoopCandidate;

typedef struct OpenRideWaypointPlacement {
    OpenRideRoutingSnap snaps[OPENRIDE_LOOP_MAX_WAYPOINTS];
    OpenRideRoutePoint points[OPENRIDE_LOOP_MAX_WAYPOINTS];
    double max_snap_distance_m;
    double mean_snap_distance_m;
    double quality_score;
} OpenRideWaypointPlacement;

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

static double route_shape_score(const OpenRideRoute *route)
{
    if (!route || !route->geometry || route->geometry_count < 4U
        || route->distance_m <= 1.0) {
        return 0.0;
    }

    const double lat0 = radians(route->geometry[0].lat);
    const double lon0 = radians(route->geometry[0].lon);
    const double cos_lat0 = cos(lat0);
    double twice_area = 0.0;

    for (uint32_t i = 0U; i < route->geometry_count; ++i) {
        const uint32_t j = (i + 1U) % route->geometry_count;
        const double xi = (radians(route->geometry[i].lon) - lon0)
                        * OPENRIDE_EARTH_RADIUS_M * cos_lat0;
        const double yi = (radians(route->geometry[i].lat) - lat0)
                        * OPENRIDE_EARTH_RADIUS_M;
        const double xj = (radians(route->geometry[j].lon) - lon0)
                        * OPENRIDE_EARTH_RADIUS_M * cos_lat0;
        const double yj = (radians(route->geometry[j].lat) - lat0)
                        * OPENRIDE_EARTH_RADIUS_M;
        twice_area += xi * yj - xj * yi;
    }

    const double area_m2 = fabs(twice_area) * 0.5;
    const double compactness = 4.0 * OPENRIDE_LOOP_PI * area_m2
                             / (route->distance_m * route->distance_m);
    return clampd(compactness, 0.0, 1.0);
}

static double waypoint_quality_score(double max_snap_m,
                                     double mean_snap_m,
                                     double preferred_snap_m)
{
    const double preferred = preferred_snap_m > 1.0 ? preferred_snap_m : 1.0;
    const double max_fit = clampd(1.0 - max_snap_m / (preferred * 3.0), 0.0, 1.0);
    const double mean_fit = clampd(1.0 - mean_snap_m / (preferred * 2.0), 0.0, 1.0);
    return 0.7 * max_fit + 0.3 * mean_fit;
}

static bool prepare_waypoint_placement(const OpenRideRoutingGraph *graph,
                                       const OpenRideLoopRequest *request,
                                       uint32_t candidate_index,
                                       uint32_t *random_state,
                                       OpenRideWaypointPlacement *placement)
{
    if (!placement) return false;
    memset(placement, 0, sizeof(*placement));

    bool have_best = false;
    OpenRideWaypointPlacement best = {0};
    const double base_bearing = candidate_bearing(request, candidate_index, random_state);
    const OpenRideRoutePoint start = {request->start.lat, request->start.lon};

    for (uint32_t variant = 0U; variant < OPENRIDE_LOOP_PLACEMENT_VARIANTS; ++variant) {
        OpenRideWaypointPlacement current = {0};
        const double bearing_jitter = (random_unit(random_state) - 0.5) * 24.0;
        const double bearing = normalize_bearing(base_bearing + bearing_jitter);
        const double radius_variation = 0.82 + random_unit(random_state) * 0.32;
        const double radius_m = request->target_distance_m
                              / (2.0 * OPENRIDE_LOOP_PI)
                              * radius_variation;
        const OpenRideRoutePoint center = destination_point(start, bearing, radius_m);
        const double start_bearing_from_center = normalize_bearing(bearing + 180.0);
        const double orientation = ((candidate_index + variant) & 1U) == 0U ? 1.0 : -1.0;

        bool valid = true;
        double snap_sum = 0.0;
        for (uint32_t i = 0U; i < OPENRIDE_LOOP_MAX_WAYPOINTS; ++i) {
            const double angular_jitter = (random_unit(random_state) - 0.5) * 12.0;
            const double around = start_bearing_from_center
                                + orientation * 90.0 * (double)(i + 1U)
                                + angular_jitter;
            const OpenRideRoutePoint requested_waypoint = destination_point(
                center, around, radius_m);

            OpenRideRoutingSnap snap = {0};
            snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
            if (!openride_routing_graph_snap_to_segment(
                    graph,
                    requested_waypoint.lat,
                    requested_waypoint.lon,
                    request->max_waypoint_snap_distance_m,
                    &snap)) {
                valid = false;
                break;
            }

            current.snaps[i] = snap;
            current.points[i] = (OpenRideRoutePoint){snap.lat, snap.lon};
            snap_sum += snap.distance_m;
            if (snap.distance_m > current.max_snap_distance_m) {
                current.max_snap_distance_m = snap.distance_m;
            }
        }

        if (!valid) continue;
        current.mean_snap_distance_m = snap_sum / (double)OPENRIDE_LOOP_MAX_WAYPOINTS;
        current.quality_score = waypoint_quality_score(
            current.max_snap_distance_m,
            current.mean_snap_distance_m,
            request->preferred_waypoint_snap_distance_m);

        if (!have_best
            || current.quality_score > best.quality_score
            || (fabs(current.quality_score - best.quality_score) < 1e-12
                && current.max_snap_distance_m < best.max_snap_distance_m)) {
            best = current;
            have_best = true;
        }
    }

    if (!have_best) return false;
    *placement = best;
    return true;
}

static bool calculate_snapped_leg(const OpenRideRoutingGraph *graph,
                                  const OpenRideLoopRequest *request,
                                  OpenRideRoutingSnap start,
                                  OpenRideRoutingSnap destination,
                                  OpenRideRoute *route)
{
    OpenRideSnappedRoutingRequest routing = openride_snapped_routing_request_default();
    routing.start = start;
    routing.destination = destination;
    routing.profile = request->profile;
    routing.avoid_tolls = request->avoid_tolls;
    routing.avoid_ferries = request->avoid_ferries;
    char error[128] = {0};
    return openride_routing_engine_calculate_snapped(graph,
                                                     &routing,
                                                     route,
                                                     error,
                                                     sizeof(error));
}

static bool generate_candidate(const OpenRideRoutingGraph *graph,
                               const OpenRideLoopRequest *request,
                               uint32_t candidate_index,
                               uint32_t *random_state,
                               OpenRideLoopCandidate *candidate)
{
    memset(candidate, 0, sizeof(*candidate));

    OpenRideWaypointPlacement placement = {0};
    if (!prepare_waypoint_placement(graph,
                                    request,
                                    candidate_index,
                                    random_state,
                                    &placement)) {
        return false;
    }
    memcpy(candidate->waypoints, placement.points, sizeof(candidate->waypoints));

    OpenRideRoutingSnap leg_starts[4] = {
        request->start,
        placement.snaps[0],
        placement.snaps[1],
        placement.snaps[2]
    };
    OpenRideRoutingSnap leg_ends[4] = {
        placement.snaps[0],
        placement.snaps[1],
        placement.snaps[2],
        request->start
    };

    OpenRideGeometryBuilder geometry = {0};
    OpenRideTraversalList traversals = {0};
    bool ok = true;
    double distance_m = 0.0;
    double estimated_time_s = 0.0;
    double weighted_cost_s = 0.0;

    for (uint32_t leg_index = 0U; ok && leg_index < 4U; ++leg_index) {
        OpenRideRoute leg = {0};
        if (!calculate_snapped_leg(graph,
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

    const double repeated_m = repeated_distance_m(&traversals);
    free(traversals.items);

    const double distance_error = fabs(combined.distance_m - request->target_distance_m)
                                / request->target_distance_m;
    const double overlap_ratio = combined.distance_m > 1.0
        ? clampd(repeated_m / combined.distance_m, 0.0, 1.0)
        : 1.0;
    const double shape_score = route_shape_score(&combined);
    const double distance_fit = clampd(1.0 - distance_error / 0.40, 0.0, 1.0);
    const double overlap_fit = clampd(1.0 - overlap_ratio / 0.30, 0.0, 1.0);
    const double score = 100.0 * (0.55 * distance_fit
                                + 0.25 * overlap_fit
                                + 0.10 * shape_score
                                + 0.10 * placement.quality_score);

    candidate->route = combined;
    candidate->score = score;
    candidate->distance_error_ratio = distance_error;
    candidate->overlap_ratio = overlap_ratio;
    candidate->max_waypoint_snap_distance_m = placement.max_snap_distance_m;
    candidate->shape_score = shape_score;
    candidate->waypoint_quality_score = placement.quality_score;
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
    request.max_waypoint_snap_distance_m = OPENRIDE_LOOP_DEFAULT_MAX_SNAP_M;
    request.preferred_waypoint_snap_distance_m = OPENRIDE_LOOP_DEFAULT_PREFERRED_SNAP_M;
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

void openride_loop_proposal_set_destroy(OpenRideLoopProposalSet *proposals)
{
    if (!proposals) return;
    for (uint32_t i = 0U; i < OPENRIDE_LOOP_MAX_PROPOSALS; ++i) {
        openride_route_destroy(&proposals->items[i].route);
    }
    memset(proposals, 0, sizeof(*proposals));
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
    if (!isfinite(request->preferred_waypoint_snap_distance_m)
        || request->preferred_waypoint_snap_distance_m <= 0.0
        || request->preferred_waypoint_snap_distance_m
           > request->max_waypoint_snap_distance_m) {
        set_error(error, error_size, "invalid preferred waypoint snap distance");
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

static void proposal_fill_stats(OpenRideLoopCandidateStats *stats,
                                const OpenRideLoopCandidate *candidate)
{
    stats->successful = true;
    stats->distance_m = candidate->route.distance_m;
    stats->score = candidate->score;
    stats->distance_error_ratio = candidate->distance_error_ratio;
    stats->overlap_ratio = candidate->overlap_ratio;
    stats->max_waypoint_snap_distance_m = candidate->max_waypoint_snap_distance_m;
    stats->shape_score = candidate->shape_score;
    stats->waypoint_quality_score = candidate->waypoint_quality_score;
}

static void proposal_move_candidate(OpenRideLoopProposal *proposal,
                                    OpenRideLoopCandidate *candidate,
                                    uint32_t source_index)
{
    memset(proposal, 0, sizeof(*proposal));
    proposal_fill_stats(&proposal->stats, candidate);
    proposal->route = candidate->route;
    memset(&candidate->route, 0, sizeof(candidate->route));
    memcpy(proposal->waypoints, candidate->waypoints, sizeof(proposal->waypoints));
    proposal->waypoint_count = OPENRIDE_LOOP_MAX_WAYPOINTS;
    proposal->source_candidate_index = source_index;
}

static void proposal_insert_ranked(OpenRideLoopProposalSet *proposals,
                                   OpenRideLoopCandidate *candidate,
                                   uint32_t source_index)
{
    uint32_t position = proposals->count;
    if (position > OPENRIDE_LOOP_MAX_PROPOSALS) position = OPENRIDE_LOOP_MAX_PROPOSALS;
    for (uint32_t i = 0U; i < proposals->count; ++i) {
        if (candidate->score > proposals->items[i].stats.score) {
            position = i;
            break;
        }
    }

    if (proposals->count >= OPENRIDE_LOOP_MAX_PROPOSALS
        && position >= OPENRIDE_LOOP_MAX_PROPOSALS) {
        return;
    }

    uint32_t new_count = proposals->count;
    if (new_count < OPENRIDE_LOOP_MAX_PROPOSALS) {
        ++new_count;
    } else {
        openride_route_destroy(&proposals->items[OPENRIDE_LOOP_MAX_PROPOSALS - 1U].route);
        memset(&proposals->items[OPENRIDE_LOOP_MAX_PROPOSALS - 1U],
               0,
               sizeof(proposals->items[0]));
    }

    for (uint32_t i = new_count - 1U; i > position; --i) {
        proposals->items[i] = proposals->items[i - 1U];
        memset(&proposals->items[i - 1U], 0, sizeof(proposals->items[i - 1U]));
    }
    proposal_move_candidate(&proposals->items[position], candidate, source_index);
    proposals->count = new_count;
}

bool openride_loop_generator_generate_proposals(
    const OpenRideRoutingGraph *graph,
    const OpenRideLoopRequest *request,
    OpenRideLoopProposalSet *proposals,
    char *error,
    size_t error_size)
{
    if (!proposals || !validate_request(graph, request, error, error_size)) return false;

    OpenRideLoopProposalSet generated;
    memset(&generated, 0, sizeof(generated));
    generated.generation_stats.attempted_candidates = request->candidate_count;
    generated.generation_stats.selected_candidate_index = UINT32_MAX;
    generated.generation_stats.candidate_stat_count = request->candidate_count;

    uint32_t random_state = request->seed == 0U ? 0x4f70656eU : request->seed;
    for (uint32_t i = 0U; i < request->candidate_count; ++i) {
        OpenRideLoopCandidate candidate;
        memset(&candidate, 0, sizeof(candidate));
        OpenRideLoopCandidateStats *candidate_stats =
            &generated.generation_stats.candidates[i];
        if (!generate_candidate(graph, request, i, &random_state, &candidate)) {
            candidate_stats->successful = false;
            continue;
        }
        ++generated.generation_stats.successful_candidates;
        proposal_fill_stats(candidate_stats, &candidate);
        proposal_insert_ranked(&generated, &candidate, i);
        openride_route_destroy(&candidate.route);
    }

    if (generated.count == 0U) {
        openride_loop_proposal_set_destroy(&generated);
        set_error(error, error_size, "no loop candidate could be routed");
        return false;
    }

    const OpenRideLoopProposal *best = &generated.items[0];
    generated.generation_stats.score = best->stats.score;
    generated.generation_stats.distance_error_ratio = best->stats.distance_error_ratio;
    generated.generation_stats.overlap_ratio = best->stats.overlap_ratio;
    generated.generation_stats.max_waypoint_snap_distance_m =
        best->stats.max_waypoint_snap_distance_m;
    generated.generation_stats.shape_score = best->stats.shape_score;
    generated.generation_stats.waypoint_quality_score = best->stats.waypoint_quality_score;
    generated.generation_stats.selected_candidate_index = best->source_candidate_index;

    openride_loop_proposal_set_destroy(proposals);
    *proposals = generated;
    set_error(error, error_size, "");
    return true;
}

bool openride_loop_proposal_set_take(OpenRideLoopProposalSet *proposals,
                                     uint32_t index,
                                     OpenRideRoute *route,
                                     OpenRideRoutePoint waypoints[OPENRIDE_LOOP_MAX_WAYPOINTS],
                                     uint32_t *waypoint_count,
                                     OpenRideLoopCandidateStats *stats,
                                     uint32_t *source_candidate_index)
{
    if (!proposals || !route || index >= proposals->count) return false;
    OpenRideLoopProposal *chosen = &proposals->items[index];
    openride_route_destroy(route);
    *route = chosen->route;
    memset(&chosen->route, 0, sizeof(chosen->route));
    if (waypoints) memcpy(waypoints, chosen->waypoints, sizeof(chosen->waypoints));
    if (waypoint_count) *waypoint_count = chosen->waypoint_count;
    if (stats) *stats = chosen->stats;
    if (source_candidate_index) *source_candidate_index = chosen->source_candidate_index;
    openride_loop_proposal_set_destroy(proposals);
    return true;
}

bool openride_loop_generator_generate(const OpenRideRoutingGraph *graph,
                                      const OpenRideLoopRequest *request,
                                      OpenRideLoopResult *result,
                                      char *error,
                                      size_t error_size)
{
    if (!result) return false;
    OpenRideLoopProposalSet proposals = {0};
    if (!openride_loop_generator_generate_proposals(graph,
                                                    request,
                                                    &proposals,
                                                    error,
                                                    error_size)) {
        return false;
    }

    OpenRideLoopStats generation_stats = proposals.generation_stats;
    OpenRideLoopCandidateStats selected_stats = {0};
    uint32_t source_index = UINT32_MAX;
    OpenRideRoute route = {0};
    OpenRideRoutePoint waypoints[OPENRIDE_LOOP_MAX_WAYPOINTS] = {{0}};
    uint32_t waypoint_count = 0U;
    if (!openride_loop_proposal_set_take(&proposals,
                                         0U,
                                         &route,
                                         waypoints,
                                         &waypoint_count,
                                         &selected_stats,
                                         &source_index)) {
        openride_loop_proposal_set_destroy(&proposals);
        set_error(error, error_size, "unable to select generated loop");
        return false;
    }

    openride_loop_result_destroy(result);
    result->route = route;
    memcpy(result->waypoints, waypoints, sizeof(result->waypoints));
    result->waypoint_count = waypoint_count;
    result->stats = generation_stats;
    result->stats.score = selected_stats.score;
    result->stats.distance_error_ratio = selected_stats.distance_error_ratio;
    result->stats.overlap_ratio = selected_stats.overlap_ratio;
    result->stats.max_waypoint_snap_distance_m = selected_stats.max_waypoint_snap_distance_m;
    result->stats.shape_score = selected_stats.shape_score;
    result->stats.waypoint_quality_score = selected_stats.waypoint_quality_score;
    result->stats.selected_candidate_index = source_index;
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
