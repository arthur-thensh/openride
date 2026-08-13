#include "openride/routing_world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENRIDE_PI 3.14159265358979323846
#define OPENRIDE_EARTH_RADIUS_M 6371008.8
#define OPENRIDE_GATEWAY_EXACT_DISTANCE_M 0.25

typedef struct OpenRideGatewayCandidate {
    OpenRideRoutingNodeId start_node;
    OpenRideRoutingNodeId destination_node;
    double score;
    double lat;
    double lon;
} OpenRideGatewayCandidate;

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
}

static double radians(double degrees)
{
    return degrees * OPENRIDE_PI / 180.0;
}

static double geo_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    const double phi1 = radians(lat1);
    const double phi2 = radians(lat2);
    const double dphi = radians(lat2 - lat1);
    const double dlambda = radians(lon2 - lon1);
    const double sin_dphi = sin(dphi * 0.5);
    const double sin_dlambda = sin(dlambda * 0.5);
    const double a = sin_dphi * sin_dphi
                   + cos(phi1) * cos(phi2) * sin_dlambda * sin_dlambda;
    const double clamped = a < 0.0 ? 0.0 : (a > 1.0 ? 1.0 : a);
    return OPENRIDE_EARTH_RADIUS_M
         * 2.0 * atan2(sqrt(clamped), sqrt(1.0 - clamped));
}

static uint8_t best_road_class(const OpenRideRoutingGraph *graph,
                               OpenRideRoutingNodeId node_id)
{
    if (!graph || node_id >= graph->node_count) return (uint8_t)OPENRIDE_ROAD_OTHER;
    const OpenRideRoutingNode *node = &graph->nodes[node_id];
    uint8_t best = (uint8_t)OPENRIDE_ROAD_OTHER;
    for (uint32_t i = 0U; i < node->edge_count; ++i) {
        const OpenRideRoutingEdge *edge = &graph->edges[node->first_edge + i];
        if (edge->road_class > 0U && edge->road_class < best) {
            best = edge->road_class;
        }
    }
    return best;
}

static double road_penalty(uint8_t road_class)
{
    if (road_class <= (uint8_t)OPENRIDE_ROAD_PRIMARY) return 0.0;
    if (road_class == (uint8_t)OPENRIDE_ROAD_SECONDARY) return 500.0;
    if (road_class == (uint8_t)OPENRIDE_ROAD_TERTIARY) return 1200.0;
    return 3000.0;
}

static bool inside_index_grid(const OpenRideRoutingGraph *graph,
                              const OpenRideRoutingNode *node)
{
    if (!graph || !node || !openride_routing_graph_has_spatial_index(graph)) return true;
    const OpenRideRoutingSpatialIndex *index = &graph->spatial_index;
    const int64_t max_lat = (int64_t)index->min_lat_e7
        + (int64_t)index->rows * (int64_t)index->cell_size_e7;
    const int64_t max_lon = (int64_t)index->min_lon_e7
        + (int64_t)index->columns * (int64_t)index->cell_size_e7;
    return (int64_t)node->lat_e7 >= index->min_lat_e7
        && (int64_t)node->lat_e7 <= max_lat
        && (int64_t)node->lon_e7 >= index->min_lon_e7
        && (int64_t)node->lon_e7 <= max_lon;
}

static void insert_candidate(
    OpenRideGatewayCandidate candidates[OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES],
    uint32_t *count,
    const OpenRideGatewayCandidate *candidate)
{
    if (!count || !candidate) return;

    for (uint32_t i = 0U; i < *count; ++i) {
        if (candidates[i].start_node == candidate->start_node
            && candidates[i].destination_node == candidate->destination_node) {
            return;
        }
    }

    uint32_t position = *count;
    if (position > OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES) {
        position = OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES;
    }
    while (position > 0U && candidates[position - 1U].score > candidate->score) {
        if (position < OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES) {
            candidates[position] = candidates[position - 1U];
        }
        --position;
    }
    if (position >= OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES) return;

    candidates[position] = *candidate;
    if (*count < OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES) ++(*count);
}

static uint32_t find_gateways(
    const OpenRideRoutingGraph *start_graph,
    const OpenRideRoutingGraph *destination_graph,
    double start_lat,
    double start_lon,
    double destination_lat,
    double destination_lon,
    OpenRideGatewayCandidate candidates[OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES])
{
    const bool scan_start = start_graph->node_count <= destination_graph->node_count;
    const OpenRideRoutingGraph *scan_graph = scan_start ? start_graph : destination_graph;
    const OpenRideRoutingGraph *lookup_graph = scan_start ? destination_graph : start_graph;
    uint32_t count = 0U;

    for (uint32_t scan_id = 0U; scan_id < scan_graph->node_count; ++scan_id) {
        const OpenRideRoutingNode *node = &scan_graph->nodes[scan_id];
        if (node->edge_count == 0U || !inside_index_grid(lookup_graph, node)) continue;

        double lat = 0.0;
        double lon = 0.0;
        openride_routing_node_geo(node, &lat, &lon);

        double match_distance = INFINITY;
        const OpenRideRoutingNodeId match_id =
            openride_routing_graph_nearest_node(lookup_graph, lat, lon, &match_distance);
        if (match_id == OPENRIDE_ROUTING_NODE_NONE
            || match_distance > OPENRIDE_GATEWAY_EXACT_DISTANCE_M) {
            continue;
        }

        const OpenRideRoutingNode *match = &lookup_graph->nodes[match_id];
        if (match->edge_count == 0U
            || match->lat_e7 != node->lat_e7
            || match->lon_e7 != node->lon_e7) {
            continue;
        }

        OpenRideGatewayCandidate candidate;
        memset(&candidate, 0, sizeof(candidate));
        candidate.start_node = scan_start ? scan_id : match_id;
        candidate.destination_node = scan_start ? match_id : scan_id;
        candidate.lat = lat;
        candidate.lon = lon;
        candidate.score = geo_distance_m(start_lat, start_lon, lat, lon)
                        + geo_distance_m(lat, lon, destination_lat, destination_lon)
                        + road_penalty(best_road_class(start_graph,
                                                       candidate.start_node))
                        + road_penalty(best_road_class(destination_graph,
                                                       candidate.destination_node));
        insert_candidate(candidates, &count, &candidate);
    }
    return count;
}

static bool calculate_nodes(const OpenRideRoutingGraph *graph,
                            OpenRideRoutingNodeId start,
                            OpenRideRoutingNodeId destination,
                            OpenRideRoutingProfile profile,
                            OpenRideRoute *route)
{
    OpenRideRoutingRequest request = openride_routing_request_default();
    request.start = start;
    request.destination = destination;
    request.profile = profile;
    char error[128] = {0};
    return openride_routing_engine_calculate(graph,
                                             &request,
                                             route,
                                             error,
                                             sizeof(error));
}

static bool append_point(OpenRideRoutePoint *geometry,
                         uint32_t capacity,
                         uint32_t *count,
                         double lat,
                         double lon)
{
    if (!geometry || !count || *count >= capacity) return false;
    if (*count > 0U) {
        const OpenRideRoutePoint *last = &geometry[*count - 1U];
        if (fabs(last->lat - lat) < 1e-10 && fabs(last->lon - lon) < 1e-10) {
            return true;
        }
    }
    geometry[*count] = (OpenRideRoutePoint){lat, lon};
    ++(*count);
    return true;
}

static bool combine_routes(const OpenRideRoute *first,
                           const OpenRideRoute *second,
                           double start_lat,
                           double start_lon,
                           double destination_lat,
                           double destination_lon,
                           OpenRideRoute *route)
{
    if (!first || !second || !route
        || !first->geometry || first->geometry_count == 0U
        || !second->geometry || second->geometry_count == 0U) {
        return false;
    }

    const uint64_t capacity64 = (uint64_t)first->geometry_count
                              + (uint64_t)second->geometry_count + 2U;
    if (capacity64 > UINT32_MAX) return false;

    const uint32_t capacity = (uint32_t)capacity64;
    OpenRideRoutePoint *geometry = calloc(capacity, sizeof(*geometry));
    if (!geometry) return false;

    uint32_t count = 0U;
    if (!append_point(geometry, capacity, &count, start_lat, start_lon)) goto fail;
    for (uint32_t i = 0U; i < first->geometry_count; ++i) {
        if (!append_point(geometry, capacity, &count,
                          first->geometry[i].lat, first->geometry[i].lon)) goto fail;
    }
    for (uint32_t i = 0U; i < second->geometry_count; ++i) {
        if (!append_point(geometry, capacity, &count,
                          second->geometry[i].lat, second->geometry[i].lon)) goto fail;
    }
    if (!append_point(geometry, capacity, &count,
                      destination_lat, destination_lon)) goto fail;

    const double start_link_m = geo_distance_m(start_lat,
                                                start_lon,
                                                first->geometry[0].lat,
                                                first->geometry[0].lon);
    const OpenRideRoutePoint *last =
        &second->geometry[second->geometry_count - 1U];
    const double destination_link_m = geo_distance_m(last->lat,
                                                      last->lon,
                                                      destination_lat,
                                                      destination_lon);
    openride_route_destroy(route);
    route->geometry = geometry;
    route->geometry_count = count;
    route->nodes = NULL;
    route->node_count = 0U;
    route->distance_m = first->distance_m + second->distance_m
                      + start_link_m + destination_link_m;
    route->estimated_time_s = first->estimated_time_s + second->estimated_time_s
                            + (start_link_m + destination_link_m) / (50.0 / 3.6);
    route->weighted_cost_s = first->weighted_cost_s + second->weighted_cost_s
                           + (start_link_m + destination_link_m) / (50.0 / 3.6);
    return true;

fail:
    free(geometry);
    return false;
}

bool openride_routing_world_calculate_graph_pair(
    const OpenRideRoutingGraph *start_graph,
    const OpenRideRoutingGraph *destination_graph,
    double start_lat,
    double start_lon,
    double destination_lat,
    double destination_lon,
    double max_snap_distance_m,
    OpenRideRoutingProfile profile,
    OpenRideRoute *route,
    OpenRideRoutingWorldResult *result,
    char *error,
    size_t error_size)
{
    if (result) memset(result, 0, sizeof(*result));
    if (!start_graph || !destination_graph || !route
        || start_graph == destination_graph
        || !isfinite(max_snap_distance_m)
        || max_snap_distance_m <= 0.0) {
        set_error(error, error_size, "invalid routing-world graph pair");
        return false;
    }

    double start_distance = INFINITY;
    double destination_distance = INFINITY;
    const OpenRideRoutingNodeId start_node =
        openride_routing_graph_nearest_node(start_graph,
                                            start_lat,
                                            start_lon,
                                            &start_distance);
    const OpenRideRoutingNodeId destination_node =
        openride_routing_graph_nearest_node(destination_graph,
                                            destination_lat,
                                            destination_lon,
                                            &destination_distance);
    if (start_node == OPENRIDE_ROUTING_NODE_NONE
        || destination_node == OPENRIDE_ROUTING_NODE_NONE
        || start_distance > max_snap_distance_m
        || destination_distance > max_snap_distance_m) {
        set_error(error, error_size, "routing-world endpoint is too far from a road");
        return false;
    }

    OpenRideGatewayCandidate candidates[
        OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES];
    memset(candidates, 0, sizeof(candidates));
    const uint32_t gateway_count = find_gateways(start_graph,
                                                  destination_graph,
                                                  start_lat,
                                                  start_lon,
                                                  destination_lat,
                                                  destination_lon,
                                                  candidates);
    if (result) result->shared_gateway_count = gateway_count;
    if (gateway_count == 0U) {
        set_error(error, error_size, "no shared routing gateway between regions");
        return false;
    }

    for (uint32_t i = 0U; i < gateway_count; ++i) {
        if (result) result->attempted_gateways = i + 1U;

        OpenRideRoute first = {0};
        OpenRideRoute second = {0};
        const bool first_ok = calculate_nodes(start_graph,
                                              start_node,
                                              candidates[i].start_node,
                                              profile,
                                              &first);
        const bool second_ok = first_ok
            && calculate_nodes(destination_graph,
                               candidates[i].destination_node,
                               destination_node,
                               profile,
                               &second);

        if (first_ok && second_ok
            && combine_routes(&first,
                              &second,
                              start_lat,
                              start_lon,
                              destination_lat,
                              destination_lon,
                              route)) {
            if (result) {
                result->gateway_lat = candidates[i].lat;
                result->gateway_lon = candidates[i].lon;
            }
            openride_route_destroy(&first);
            openride_route_destroy(&second);
            set_error(error, error_size, "");
            return true;
        }

        openride_route_destroy(&first);
        openride_route_destroy(&second);
    }

    set_error(error, error_size, "shared gateways exist but none is routable");
    return false;
}
