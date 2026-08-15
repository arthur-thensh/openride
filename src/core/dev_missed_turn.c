#include "openride/dev_missed_turn.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENRIDE_DEV_BRANCH_MAX_NODES 32U
#define OPENRIDE_EARTH_RADIUS_M 6371008.8
#define OPENRIDE_DEG_TO_RAD 0.017453292519943295769236907684886

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
}

static double geo_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    const double dlat = (lat2 - lat1) * OPENRIDE_DEG_TO_RAD;
    const double dlon = (lon2 - lon1) * OPENRIDE_DEG_TO_RAD;
    const double rlat1 = lat1 * OPENRIDE_DEG_TO_RAD;
    const double rlat2 = lat2 * OPENRIDE_DEG_TO_RAD;
    const double a =
        sin(dlat * 0.5) * sin(dlat * 0.5)
        + cos(rlat1) * cos(rlat2)
        * sin(dlon * 0.5) * sin(dlon * 0.5);
    const double clamped = a > 1.0 ? 1.0 : a;
    return 2.0 * OPENRIDE_EARTH_RADIUS_M * asin(sqrt(clamped));
}

static double point_segment_distance_m(double lat,
                                       double lon,
                                       const OpenRideRoutePoint *a,
                                       const OpenRideRoutePoint *b)
{
    if (!a || !b) return INFINITY;

    const double cos_lat = cos(lat * OPENRIDE_DEG_TO_RAD);
    const double meters_per_degree_lat =
        OPENRIDE_EARTH_RADIUS_M * OPENRIDE_DEG_TO_RAD;
    const double meters_per_degree_lon =
        meters_per_degree_lat * cos_lat;

    const double ax = (a->lon - lon) * meters_per_degree_lon;
    const double ay = (a->lat - lat) * meters_per_degree_lat;
    const double bx = (b->lon - lon) * meters_per_degree_lon;
    const double by = (b->lat - lat) * meters_per_degree_lat;
    const double dx = bx - ax;
    const double dy = by - ay;
    const double length_sq = dx * dx + dy * dy;

    double t = 0.0;
    if (length_sq > 1e-9) {
        t = -(ax * dx + ay * dy) / length_sq;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
    }

    const double px = ax + dx * t;
    const double py = ay + dy * t;
    return sqrt(px * px + py * py);
}

static double point_route_distance_m(double lat,
                                     double lon,
                                     const OpenRideRoute *route)
{
    if (!route || !route->geometry || route->geometry_count == 0U) {
        return INFINITY;
    }
    if (route->geometry_count == 1U) {
        return geo_distance_m(lat,
                              lon,
                              route->geometry[0].lat,
                              route->geometry[0].lon);
    }

    double best = INFINITY;
    for (uint32_t i = 1U; i < route->geometry_count; ++i) {
        const double distance =
            point_segment_distance_m(lat,
                                     lon,
                                     &route->geometry[i - 1U],
                                     &route->geometry[i]);
        if (distance < best) best = distance;
    }
    return best;
}

static bool route_contains_node(const OpenRideRoute *route,
                                OpenRideRoutingNodeId node)
{
    if (!route || !route->nodes) return false;
    for (uint32_t i = 0U; i < route->node_count; ++i) {
        if (route->nodes[i] == node) return true;
    }
    return false;
}

static bool node_in_small_path(const OpenRideRoutingNodeId *nodes,
                               uint32_t count,
                               OpenRideRoutingNodeId node)
{
    for (uint32_t i = 0U; i < count; ++i) {
        if (nodes[i] == node) return true;
    }
    return false;
}

static bool route_geometry_offset(const OpenRideRoute *route,
                                  uint32_t *offset)
{
    if (!route || !offset || !route->nodes || route->node_count == 0U
        || !route->geometry || route->geometry_count == 0U) {
        return false;
    }
    if (route->geometry_count == route->node_count) {
        *offset = 0U;
        return true;
    }
    if (route->geometry_count == route->node_count + 2U) {
        *offset = 1U;
        return true;
    }
    return false;
}

static bool build_branch_from_edge(
    const OpenRideRoutingGraph *graph,
    const OpenRideRoute *planned_route,
    OpenRideRoutingNodeId junction,
    const OpenRideRoutingEdge *first_edge,
    double minimum_off_route_m,
    OpenRideRoute *branch_route,
    double *max_distance_from_route_m)
{
    if (!graph || !planned_route || !first_edge || !branch_route
        || first_edge->target >= graph->node_count) {
        return false;
    }

    OpenRideRoutingNodeId branch_nodes[OPENRIDE_DEV_BRANCH_MAX_NODES];
    uint32_t count = 2U;
    branch_nodes[0] = junction;
    branch_nodes[1] = first_edge->target;

    double branch_length_m = 0.0;
    double max_off_route_m = 0.0;

    for (uint32_t guard = 0U; guard < OPENRIDE_DEV_BRANCH_MAX_NODES; ++guard) {
        double prev_lat = 0.0;
        double prev_lon = 0.0;
        double current_lat = 0.0;
        double current_lon = 0.0;
        openride_routing_node_geo(
            &graph->nodes[branch_nodes[count - 2U]], &prev_lat, &prev_lon);
        openride_routing_node_geo(
            &graph->nodes[branch_nodes[count - 1U]], &current_lat, &current_lon);

        branch_length_m += geo_distance_m(prev_lat,
                                          prev_lon,
                                          current_lat,
                                          current_lon);
        const double current_off_route =
            point_route_distance_m(current_lat, current_lon, planned_route);
        if (current_off_route > max_off_route_m) {
            max_off_route_m = current_off_route;
        }

        /*
         * At x5 a 60 km/h simulation advances roughly 83 m per real second.
         * Keep enough real-road geometry after the turn that the normal
         * two-second off-route confirmation can happen naturally.
         */
        if (branch_length_m >= 220.0
            && max_off_route_m >= minimum_off_route_m) {
            break;
        }

        if (count >= OPENRIDE_DEV_BRANCH_MAX_NODES) break;

        const OpenRideRoutingNodeId previous = branch_nodes[count - 2U];
        const OpenRideRoutingNodeId current = branch_nodes[count - 1U];
        const OpenRideRoutingNode *node = &graph->nodes[current];

        const OpenRideRoutingEdge *best = NULL;
        double best_score = -1.0;

        for (uint32_t edge_index = 0U;
             edge_index < node->edge_count;
             ++edge_index) {
            const OpenRideRoutingEdge *edge =
                &graph->edges[node->first_edge + edge_index];
            if (edge->target >= graph->node_count
                || edge->target == previous
                || node_in_small_path(branch_nodes, count, edge->target)
                || route_contains_node(planned_route, edge->target)
                || (edge->flags & OPENRIDE_EDGE_FLAG_FERRY) != 0U) {
                continue;
            }

            double candidate_lat = 0.0;
            double candidate_lon = 0.0;
            openride_routing_node_geo(&graph->nodes[edge->target],
                                      &candidate_lat,
                                      &candidate_lon);
            const double away =
                point_route_distance_m(candidate_lat,
                                       candidate_lon,
                                       planned_route);
            /*
             * Prefer the edge that moves furthest away from the official
             * route. A tiny length term makes equal-distance choices stable
             * without overriding the geometric intent.
             */
            const double score =
                away + (double)edge->length_cm / 100000.0;
            if (score > best_score) {
                best = edge;
                best_score = score;
            }
        }

        if (!best) break;
        branch_nodes[count++] = best->target;
    }

    if (count < 2U
        || branch_length_m < 120.0
        || max_off_route_m < minimum_off_route_m) {
        return false;
    }

    OpenRideRoute result = {0};
    result.geometry = calloc(count, sizeof(*result.geometry));
    if (!result.geometry) return false;
    result.geometry_count = count;

    result.distance_m = 0.0;
    for (uint32_t i = 0U; i < count; ++i) {
        openride_routing_node_geo(&graph->nodes[branch_nodes[i]],
                                  &result.geometry[i].lat,
                                  &result.geometry[i].lon);
        if (i > 0U) {
            result.distance_m += geo_distance_m(
                result.geometry[i - 1U].lat,
                result.geometry[i - 1U].lon,
                result.geometry[i].lat,
                result.geometry[i].lon);
        }
    }
    result.estimated_time_s = result.distance_m / (60.0 / 3.6);
    result.weighted_cost_s = result.estimated_time_s;

    *branch_route = result;
    if (max_distance_from_route_m) {
        *max_distance_from_route_m = max_off_route_m;
    }
    return true;
}

void openride_dev_missed_turn_plan_init(OpenRideDevMissedTurnPlan *plan)
{
    if (!plan) return;
    memset(plan, 0, sizeof(*plan));
}

void openride_dev_missed_turn_plan_destroy(OpenRideDevMissedTurnPlan *plan)
{
    if (!plan) return;
    openride_route_destroy(&plan->branch_route);
    memset(plan, 0, sizeof(*plan));
}

bool openride_dev_missed_turn_plan_build(
    const OpenRideRoutingGraph *graph,
    const OpenRideRoute *planned_route,
    double current_position_m,
    double minimum_ahead_m,
    double maximum_ahead_m,
    double minimum_off_route_m,
    OpenRideDevMissedTurnPlan *plan,
    char *error,
    size_t error_size)
{
    if (!graph || !planned_route || !plan) {
        set_error(error, error_size, "invalid missed-turn arguments");
        return false;
    }
    if (!planned_route->nodes || planned_route->node_count < 3U) {
        set_error(error,
                  error_size,
                  "missed-turn DEV requires a single-region route with nodes");
        return false;
    }
    if (!planned_route->geometry || planned_route->geometry_count < 3U) {
        set_error(error, error_size, "planned route has no usable geometry");
        return false;
    }

    uint32_t geometry_offset = 0U;
    if (!route_geometry_offset(planned_route, &geometry_offset)) {
        set_error(error,
                  error_size,
                  "route node/geometry alignment is unsupported");
        return false;
    }

    if (!isfinite(current_position_m) || current_position_m < 0.0) {
        current_position_m = 0.0;
    }
    if (!isfinite(minimum_ahead_m) || minimum_ahead_m < 0.0) {
        minimum_ahead_m = 80.0;
    }
    if (!isfinite(maximum_ahead_m) || maximum_ahead_m <= minimum_ahead_m) {
        maximum_ahead_m = minimum_ahead_m + 2500.0;
    }
    if (!isfinite(minimum_off_route_m) || minimum_off_route_m < 45.0) {
        minimum_off_route_m = 80.0;
    }

    openride_dev_missed_turn_plan_destroy(plan);

    double *cumulative =
        calloc(planned_route->geometry_count, sizeof(*cumulative));
    if (!cumulative) {
        set_error(error, error_size, "unable to allocate route distances");
        return false;
    }

    for (uint32_t i = 1U; i < planned_route->geometry_count; ++i) {
        cumulative[i] = cumulative[i - 1U]
            + geo_distance_m(planned_route->geometry[i - 1U].lat,
                             planned_route->geometry[i - 1U].lon,
                             planned_route->geometry[i].lat,
                             planned_route->geometry[i].lon);
    }

    const double earliest = current_position_m + minimum_ahead_m;
    const double latest = current_position_m + maximum_ahead_m;

    for (uint32_t route_index = 1U;
         route_index + 1U < planned_route->node_count;
         ++route_index) {
        const uint32_t geometry_index = route_index + geometry_offset;
        if (geometry_index >= planned_route->geometry_count) break;

        const double trigger = cumulative[geometry_index];
        if (trigger < earliest) continue;
        if (trigger > latest) break;

        const OpenRideRoutingNodeId previous =
            planned_route->nodes[route_index - 1U];
        const OpenRideRoutingNodeId junction =
            planned_route->nodes[route_index];
        const OpenRideRoutingNodeId next =
            planned_route->nodes[route_index + 1U];

        if (junction >= graph->node_count) continue;
        const OpenRideRoutingNode *node = &graph->nodes[junction];

        for (uint32_t edge_index = 0U;
             edge_index < node->edge_count;
             ++edge_index) {
            const OpenRideRoutingEdge *edge =
                &graph->edges[node->first_edge + edge_index];

            if (edge->target >= graph->node_count
                || edge->target == previous
                || edge->target == next
                || route_contains_node(planned_route, edge->target)
                || (edge->flags & OPENRIDE_EDGE_FLAG_FERRY) != 0U) {
                continue;
            }

            OpenRideRoute branch = {0};
            double max_off_route = 0.0;
            if (!build_branch_from_edge(graph,
                                        planned_route,
                                        junction,
                                        edge,
                                        minimum_off_route_m,
                                        &branch,
                                        &max_off_route)) {
                continue;
            }

            plan->branch_route = branch;
            plan->trigger_position_m = trigger;
            plan->route_node_index = route_index;
            plan->max_distance_from_route_m = max_off_route;
            free(cumulative);
            if (error && error_size) error[0] = '\0';
            return true;
        }
    }

    free(cumulative);
    set_error(error,
              error_size,
              "no suitable real branch found in the next 2.5 km");
    return false;
}
