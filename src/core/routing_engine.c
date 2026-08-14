#include "openride/routing_engine.h"
#include "pathfinder.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENRIDE_EARTH_RADIUS_M 6371008.8
#define OPENRIDE_MAX_HEURISTIC_SPEED_KPH 130.0

typedef struct OpenRideEngineContext {
    const OpenRideRoutingGraph *graph;
    OpenRideRoutingRequest request;
} OpenRideEngineContext;

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
}

static double radians(double degrees)
{
    return degrees * 0.017453292519943295769236907684886;
}

static double geo_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    const double dlat = radians(lat2 - lat1);
    const double dlon = radians(lon2 - lon1);
    const double rlat1 = radians(lat1);
    const double rlat2 = radians(lat2);
    const double a = sin(dlat * 0.5) * sin(dlat * 0.5)
                   + cos(rlat1) * cos(rlat2)
                   * sin(dlon * 0.5) * sin(dlon * 0.5);
    const double clamped = a > 1.0 ? 1.0 : a;
    return 2.0 * OPENRIDE_EARTH_RADIUS_M * asin(sqrt(clamped));
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
        case OPENRIDE_ROAD_OTHER:         return 40.0;
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
    if (speed > OPENRIDE_MAX_HEURISTIC_SPEED_KPH) {
        speed = OPENRIDE_MAX_HEURISTIC_SPEED_KPH;
    }
    return speed;
}

static double profile_multiplier(OpenRideRoutingProfile profile,
                                 const OpenRideRoutingEdge *edge)
{
    const OpenRideRoadClass road_class = (OpenRideRoadClass)edge->road_class;
    double multiplier = 1.0;

    if (profile == OPENRIDE_ROUTING_PROFILE_TOURING) {
        switch (road_class) {
            case OPENRIDE_ROAD_MOTORWAY:      multiplier = 2.50; break;
            case OPENRIDE_ROAD_TRUNK:         multiplier = 1.80; break;
            case OPENRIDE_ROAD_PRIMARY:       multiplier = 1.35; break;
            case OPENRIDE_ROAD_SECONDARY:     multiplier = 1.10; break;
            case OPENRIDE_ROAD_TERTIARY:      multiplier = 1.00; break;
            case OPENRIDE_ROAD_UNCLASSIFIED:  multiplier = 1.00; break;
            case OPENRIDE_ROAD_RESIDENTIAL:   multiplier = 1.10; break;
            case OPENRIDE_ROAD_SERVICE:       multiplier = 1.30; break;
            case OPENRIDE_ROAD_LIVING_STREET: multiplier = 1.20; break;
            case OPENRIDE_ROAD_TRACK:         multiplier = 1.40; break;
            case OPENRIDE_ROAD_PATH:          multiplier = 1.70; break;
            default:                          multiplier = 1.25; break;
        }
        if ((edge->flags & OPENRIDE_EDGE_FLAG_UNPAVED) != 0U) multiplier *= 1.10;
    } else if (profile == OPENRIDE_ROUTING_PROFILE_TRAIL) {
        switch (road_class) {
            case OPENRIDE_ROAD_MOTORWAY:      multiplier = 5.00; break;
            case OPENRIDE_ROAD_TRUNK:         multiplier = 3.00; break;
            case OPENRIDE_ROAD_PRIMARY:       multiplier = 2.00; break;
            case OPENRIDE_ROAD_SECONDARY:     multiplier = 1.50; break;
            case OPENRIDE_ROAD_TERTIARY:      multiplier = 1.20; break;
            case OPENRIDE_ROAD_UNCLASSIFIED:  multiplier = 1.10; break;
            case OPENRIDE_ROAD_RESIDENTIAL:   multiplier = 1.30; break;
            case OPENRIDE_ROAD_SERVICE:       multiplier = 1.15; break;
            case OPENRIDE_ROAD_LIVING_STREET: multiplier = 1.20; break;
            case OPENRIDE_ROAD_TRACK:         multiplier = 1.00; break;
            case OPENRIDE_ROAD_PATH:          multiplier = 1.05; break;
            default:                          multiplier = 1.30; break;
        }
        if ((edge->flags & OPENRIDE_EDGE_FLAG_UNPAVED) == 0U
            && road_class != OPENRIDE_ROAD_TRACK
            && road_class != OPENRIDE_ROAD_PATH) {
            multiplier *= 1.20;
        }
    } else {
        if (road_class == OPENRIDE_ROAD_TRACK) multiplier *= 1.35;
        if (road_class == OPENRIDE_ROAD_PATH) multiplier *= 1.50;
        if ((edge->flags & OPENRIDE_EDGE_FLAG_UNPAVED) != 0U) multiplier *= 1.15;
    }

    return multiplier;
}

static bool edge_allowed(const OpenRideRoutingEdge *edge, void *context)
{
    const OpenRideEngineContext *engine = context;

    if (engine->request.avoid_tolls
        && (edge->flags & OPENRIDE_EDGE_FLAG_TOLL) != 0U) {
        return false;
    }
    if (engine->request.avoid_ferries
        && (edge->flags & OPENRIDE_EDGE_FLAG_FERRY) != 0U) {
        return false;
    }
    return true;
}

static double edge_cost(const OpenRideRoutingEdge *edge, void *context)
{
    const OpenRideEngineContext *engine = context;
    const double length_m = (double)edge->length_cm / 100.0;
    const double speed_mps = edge_speed_kph(edge) / 3.6;
    const double travel_time_s = length_m / speed_mps;
    return travel_time_s * profile_multiplier(engine->request.profile, edge);
}

static double heuristic(OpenRideRoutingNodeId node_id,
                        OpenRideRoutingNodeId destination_id,
                        void *context)
{
    const OpenRideEngineContext *engine = context;
    const OpenRideRoutingNode *node = &engine->graph->nodes[node_id];
    const OpenRideRoutingNode *destination = &engine->graph->nodes[destination_id];
    double lat1 = 0.0;
    double lon1 = 0.0;
    double lat2 = 0.0;
    double lon2 = 0.0;

    openride_routing_node_geo(node, &lat1, &lon1);
    openride_routing_node_geo(destination, &lat2, &lon2);

    const double distance_m = geo_distance_m(lat1, lon1, lat2, lon2);
    const double max_speed_mps = OPENRIDE_MAX_HEURISTIC_SPEED_KPH / 3.6;
    return distance_m / max_speed_mps;
}

static const OpenRideRoutingEdge *find_edge(const OpenRideRoutingGraph *graph,
                                            OpenRideRoutingNodeId from,
                                            OpenRideRoutingNodeId to)
{
    const OpenRideRoutingNode *node = &graph->nodes[from];
    for (uint32_t i = 0U; i < node->edge_count; ++i) {
        const OpenRideRoutingEdge *edge = &graph->edges[node->first_edge + i];
        if (edge->target == to) return edge;
    }
    return NULL;
}

static bool route_build_navigation_context_from_nodes(
    const OpenRideRoutingGraph *graph,
    OpenRideRoute *route)
{
    if (!route) return false;

    free(route->navigation_context);
    route->navigation_context = NULL;
    route->navigation_context_count = 0U;

    if (!graph || !route->geometry || route->geometry_count == 0U
        || !route->nodes || route->node_count == 0U) {
        return true;
    }

    uint32_t geometry_offset = 0U;
    if (route->geometry_count == route->node_count) {
        geometry_offset = 0U;
    } else if (route->geometry_count == route->node_count + 2U) {
        geometry_offset = 1U;
    } else {
        return true;
    }

    OpenRideRouteNavigationContext *context =
        calloc(route->geometry_count, sizeof(*context));
    if (!context) return false;

    for (uint32_t node_index = 0U;
         node_index < route->node_count;
         ++node_index) {
        const OpenRideRoutingNodeId current = route->nodes[node_index];
        if (current >= graph->node_count) {
            free(context);
            return false;
        }

        uint8_t flags = 0U;
        OpenRideRoutingNodeId previous = OPENRIDE_ROUTING_NODE_NONE;
        OpenRideRoutingNodeId next = OPENRIDE_ROUTING_NODE_NONE;
        const OpenRideRoutingEdge *incoming = NULL;
        const OpenRideRoutingEdge *outgoing = NULL;

        if (node_index > 0U) {
            previous = route->nodes[node_index - 1U];
            incoming = find_edge(graph, previous, current);
            if (incoming
                && (incoming->flags & OPENRIDE_EDGE_FLAG_ROUNDABOUT) != 0U) {
                flags |= OPENRIDE_ROUTE_NAV_INCOMING_ROUNDABOUT;
            }
        }

        if (node_index + 1U < route->node_count) {
            next = route->nodes[node_index + 1U];
            outgoing = find_edge(graph, current, next);
            if (outgoing
                && (outgoing->flags & OPENRIDE_EDGE_FLAG_ROUNDABOUT) != 0U) {
                flags |= OPENRIDE_ROUTE_NAV_OUTGOING_ROUNDABOUT;
            }
        }

        const OpenRideRoutingNode *node = &graph->nodes[current];

        if (previous != OPENRIDE_ROUTING_NODE_NONE
            && next != OPENRIDE_ROUTING_NODE_NONE) {
            for (uint32_t edge_index = 0U;
                 edge_index < node->edge_count;
                 ++edge_index) {
                const OpenRideRoutingEdge *edge =
                    &graph->edges[node->first_edge + edge_index];
                if (edge->target != previous && edge->target != next) {
                    flags |= OPENRIDE_ROUTE_NAV_HAS_ALTERNATIVE;
                    break;
                }
            }
        }

        if ((flags & OPENRIDE_ROUTE_NAV_INCOMING_ROUNDABOUT) != 0U
            && previous != OPENRIDE_ROUTING_NODE_NONE) {
            for (uint32_t edge_index = 0U;
                 edge_index < node->edge_count;
                 ++edge_index) {
                const OpenRideRoutingEdge *edge =
                    &graph->edges[node->first_edge + edge_index];
                if (edge->target == previous) continue;
                if ((edge->flags & OPENRIDE_EDGE_FLAG_ROUNDABOUT) == 0U) {
                    flags |= OPENRIDE_ROUTE_NAV_HAS_ROUNDABOUT_EXIT;
                    break;
                }
            }
        }

        context[node_index + geometry_offset].flags = flags;
    }

    route->navigation_context = context;
    route->navigation_context_count = route->geometry_count;
    return true;
}

OpenRideRoutingRequest openride_routing_request_default(void)
{
    OpenRideRoutingRequest request;
    request.start = OPENRIDE_ROUTING_NODE_NONE;
    request.destination = OPENRIDE_ROUTING_NODE_NONE;
    request.profile = OPENRIDE_ROUTING_PROFILE_FASTEST;
    request.avoid_tolls = false;
    request.avoid_ferries = false;
    return request;
}

static bool route_build_geometry_from_nodes(const OpenRideRoutingGraph *graph,
                                            OpenRideRoute *route)
{
    free(route->geometry);
    route->geometry = NULL;
    route->geometry_count = 0U;
    free(route->navigation_context);
    route->navigation_context = NULL;
    route->navigation_context_count = 0U;
    if (route->node_count == 0U) return true;

    route->geometry = calloc(route->node_count, sizeof(*route->geometry));
    if (!route->geometry) return false;
    route->geometry_count = route->node_count;

    for (uint32_t i = 0U; i < route->node_count; ++i) {
        if (route->nodes[i] >= graph->node_count) return false;
        openride_routing_node_geo(&graph->nodes[route->nodes[i]],
                                  &route->geometry[i].lat,
                                  &route->geometry[i].lon);
    }
    return route_build_navigation_context_from_nodes(graph, route);
}

void openride_route_destroy(OpenRideRoute *route)
{
    if (!route) return;
    free(route->nodes);
    free(route->geometry);
    free(route->navigation_context);
    memset(route, 0, sizeof(*route));
}

bool openride_routing_engine_calculate(const OpenRideRoutingGraph *graph,
                                       const OpenRideRoutingRequest *request,
                                       OpenRideRoute *route,
                                       char *error,
                                       size_t error_size)
{
    if (!graph || !request || !route) {
        set_error(error, error_size, "invalid routing engine arguments");
        return false;
    }
    if (request->start >= graph->node_count
        || request->destination >= graph->node_count) {
        set_error(error, error_size, "routing request node is out of bounds");
        return false;
    }
    if (request->profile < OPENRIDE_ROUTING_PROFILE_FASTEST
        || request->profile > OPENRIDE_ROUTING_PROFILE_TRAIL) {
        set_error(error, error_size, "unknown routing profile");
        return false;
    }

    OpenRideEngineContext context;
    context.graph = graph;
    context.request = *request;

    OpenRidePathfinderCallbacks callbacks;
    callbacks.edge_allowed = edge_allowed;
    callbacks.edge_cost = edge_cost;
    callbacks.heuristic = heuristic;
    callbacks.context = &context;

    OpenRidePathfinderResult path = {0};
    if (!openride_pathfinder_find(graph,
                                  request->start,
                                  request->destination,
                                  &callbacks,
                                  &path,
                                  error,
                                  error_size)) {
        return false;
    }

    double distance_m = 0.0;
    double estimated_time_s = 0.0;

    for (uint32_t i = 1U; i < path.node_count; ++i) {
        const OpenRideRoutingEdge *edge = find_edge(graph,
                                                    path.nodes[i - 1U],
                                                    path.nodes[i]);
        if (!edge) {
            openride_pathfinder_result_destroy(&path);
            set_error(error, error_size, "route references a missing graph edge");
            return false;
        }
        const double length_m = (double)edge->length_cm / 100.0;
        distance_m += length_m;
        estimated_time_s += length_m / (edge_speed_kph(edge) / 3.6);
    }

    openride_route_destroy(route);
    route->nodes = path.nodes;
    route->node_count = path.node_count;
    route->distance_m = distance_m;
    route->estimated_time_s = estimated_time_s;
    route->weighted_cost_s = path.total_cost;
    path.nodes = NULL;
    openride_pathfinder_result_destroy(&path);

    if (!route_build_geometry_from_nodes(graph, route)) {
        openride_route_destroy(route);
        set_error(error, error_size, "unable to allocate route geometry");
        return false;
    }

    set_error(error, error_size, "");
    return true;
}

bool openride_routing_engine_calculate_frontier_routes(
    const OpenRideRoutingGraph *graph,
    const OpenRideRoutingNodeId *sources,
    const double *source_costs,
    uint32_t source_count,
    const OpenRideRoutingNodeId *targets,
    uint32_t target_count,
    OpenRideRoutingProfile profile,
    double *target_costs,
    uint32_t *target_source_indices,
    OpenRideRoute *target_routes,
    bool *reachable,
    char *error,
    size_t error_size)
{
    if (!graph || !sources || !source_costs || source_count == 0U
        || !targets || target_count == 0U || !target_costs
        || !target_source_indices || !target_routes || !reachable) {
        set_error(error, error_size, "invalid routing frontier arguments");
        return false;
    }
    if (profile < OPENRIDE_ROUTING_PROFILE_FASTEST
        || profile > OPENRIDE_ROUTING_PROFILE_TRAIL) {
        set_error(error, error_size, "unknown routing profile");
        return false;
    }

    for (uint32_t i = 0U; i < target_count; ++i) {
        openride_route_destroy(&target_routes[i]);
    }

    OpenRideEngineContext context;
    memset(&context, 0, sizeof(context));
    context.graph = graph;
    context.request = openride_routing_request_default();
    context.request.profile = profile;

    OpenRidePathfinderCallbacks callbacks;
    callbacks.edge_allowed = edge_allowed;
    callbacks.edge_cost = edge_cost;
    callbacks.heuristic = heuristic;
    callbacks.context = &context;

    OpenRidePathfinderResult *paths =
        calloc(target_count, sizeof(*paths));
    if (!paths) {
        set_error(error, error_size, "unable to allocate frontier routes");
        return false;
    }

    const bool ok = openride_pathfinder_find_frontier_paths(
        graph,
        sources,
        source_costs,
        source_count,
        targets,
        target_count,
        &callbacks,
        target_costs,
        target_source_indices,
        paths,
        reachable,
        error,
        error_size);
    if (!ok) {
        for (uint32_t i = 0U; i < target_count; ++i) {
            openride_pathfinder_result_destroy(&paths[i]);
        }
        free(paths);
        return false;
    }

    for (uint32_t i = 0U; i < target_count; ++i) {
        if (!reachable[i]) continue;
        if (target_source_indices[i] >= source_count
            || !paths[i].nodes
            || paths[i].node_count == 0U) {
            for (uint32_t j = 0U; j < target_count; ++j) {
                openride_pathfinder_result_destroy(&paths[j]);
                openride_route_destroy(&target_routes[j]);
            }
            free(paths);
            set_error(error, error_size, "invalid reconstructed frontier route");
            return false;
        }

        double distance_m = 0.0;
        double estimated_time_s = 0.0;
        for (uint32_t node_index = 1U;
             node_index < paths[i].node_count;
             ++node_index) {
            const OpenRideRoutingEdge *edge =
                find_edge(graph,
                          paths[i].nodes[node_index - 1U],
                          paths[i].nodes[node_index]);
            if (!edge) {
                for (uint32_t j = 0U; j < target_count; ++j) {
                    openride_pathfinder_result_destroy(&paths[j]);
                    openride_route_destroy(&target_routes[j]);
                }
                free(paths);
                set_error(error, error_size,
                          "frontier route references a missing graph edge");
                return false;
            }
            const double length_m = (double)edge->length_cm / 100.0;
            distance_m += length_m;
            estimated_time_s += length_m / (edge_speed_kph(edge) / 3.6);
        }

        OpenRideRoute *route = &target_routes[i];
        route->nodes = paths[i].nodes;
        route->node_count = paths[i].node_count;
        route->distance_m = distance_m;
        route->estimated_time_s = estimated_time_s;
        route->weighted_cost_s =
            target_costs[i] - source_costs[target_source_indices[i]];
        paths[i].nodes = NULL;
        paths[i].node_count = 0U;

        if (!route_build_geometry_from_nodes(graph, route)) {
            for (uint32_t j = 0U; j < target_count; ++j) {
                openride_pathfinder_result_destroy(&paths[j]);
                openride_route_destroy(&target_routes[j]);
            }
            free(paths);
            set_error(error, error_size,
                      "unable to allocate frontier route geometry");
            return false;
        }

        free(route->nodes);
        route->nodes = NULL;
        route->node_count = 0U;
    }

    for (uint32_t i = 0U; i < target_count; ++i) {
        openride_pathfinder_result_destroy(&paths[i]);
    }
    free(paths);
    set_error(error, error_size, "");
    return true;
}

OpenRideSnappedRoutingRequest openride_snapped_routing_request_default(void)
{
    OpenRideSnappedRoutingRequest request;
    memset(&request, 0, sizeof(request));
    request.start.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    request.destination.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    request.start.a = OPENRIDE_ROUTING_NODE_NONE;
    request.start.b = OPENRIDE_ROUTING_NODE_NONE;
    request.destination.a = OPENRIDE_ROUTING_NODE_NONE;
    request.destination.b = OPENRIDE_ROUTING_NODE_NONE;
    request.profile = OPENRIDE_ROUTING_PROFILE_FASTEST;
    request.avoid_tolls = false;
    request.avoid_ferries = false;
    return request;
}

typedef struct OpenRideSnapEndpointOption {
    OpenRideRoutingNodeId node;
    const OpenRideRoutingEdge *edge;
    double fraction;
} OpenRideSnapEndpointOption;

static bool snap_valid_for_graph(const OpenRideRoutingGraph *graph,
                                 const OpenRideRoutingSnap *snap)
{
    if (!snap
        || snap->segment_id == OPENRIDE_ROUTING_SEGMENT_NONE
        || snap->segment_id >= graph->segment_index.segment_count
        || snap->a >= graph->node_count
        || snap->b >= graph->node_count
        || snap->fraction < 0.0
        || snap->fraction > 1.0
        || !isfinite(snap->lat)
        || !isfinite(snap->lon)) {
        return false;
    }
    const OpenRideRoutingSegment *segment =
        &graph->segment_index.segments[snap->segment_id];
    return snap->a == segment->a && snap->b == segment->b;
}

static uint32_t start_options(const OpenRideRoutingGraph *graph,
                              const OpenRideRoutingSnap *snap,
                              OpenRideEngineContext *context,
                              OpenRideSnapEndpointOption options[2])
{
    const double eps = 1e-9;
    uint32_t count = 0U;

    if (snap->fraction <= eps) {
        options[count++] = (OpenRideSnapEndpointOption){snap->a, NULL, 0.0};
    } else {
        const OpenRideRoutingEdge *edge = find_edge(graph, snap->b, snap->a);
        if (edge && edge_allowed(edge, context)) {
            options[count++] = (OpenRideSnapEndpointOption){snap->a, edge, snap->fraction};
        }
    }

    if (1.0 - snap->fraction <= eps) {
        options[count++] = (OpenRideSnapEndpointOption){snap->b, NULL, 0.0};
    } else {
        const OpenRideRoutingEdge *edge = find_edge(graph, snap->a, snap->b);
        if (edge && edge_allowed(edge, context)) {
            options[count++] = (OpenRideSnapEndpointOption){snap->b, edge, 1.0 - snap->fraction};
        }
    }
    return count;
}

static uint32_t destination_options(const OpenRideRoutingGraph *graph,
                                    const OpenRideRoutingSnap *snap,
                                    OpenRideEngineContext *context,
                                    OpenRideSnapEndpointOption options[2])
{
    const double eps = 1e-9;
    uint32_t count = 0U;

    if (snap->fraction <= eps) {
        options[count++] = (OpenRideSnapEndpointOption){snap->a, NULL, 0.0};
    } else {
        const OpenRideRoutingEdge *edge = find_edge(graph, snap->a, snap->b);
        if (edge && edge_allowed(edge, context)) {
            options[count++] = (OpenRideSnapEndpointOption){snap->a, edge, snap->fraction};
        }
    }

    if (1.0 - snap->fraction <= eps) {
        options[count++] = (OpenRideSnapEndpointOption){snap->b, NULL, 0.0};
    } else {
        const OpenRideRoutingEdge *edge = find_edge(graph, snap->b, snap->a);
        if (edge && edge_allowed(edge, context)) {
            options[count++] = (OpenRideSnapEndpointOption){snap->b, edge, 1.0 - snap->fraction};
        }
    }
    return count;
}

static void add_partial_metrics(const OpenRideRoutingEdge *edge,
                                double fraction,
                                OpenRideEngineContext *context,
                                double *distance_m,
                                double *time_s,
                                double *weighted_cost_s)
{
    if (!edge || fraction <= 0.0) return;
    const double length = ((double)edge->length_cm / 100.0) * fraction;
    *distance_m += length;
    *time_s += length / (edge_speed_kph(edge) / 3.6);
    *weighted_cost_s += edge_cost(edge, context) * fraction;
}

static bool build_snapped_geometry(const OpenRideRoutingGraph *graph,
                                   const OpenRideRoutingSnap *start,
                                   const OpenRideRoutingSnap *destination,
                                   OpenRideRoute *route)
{
    const uint32_t count = route->node_count + 2U;
    OpenRideRoutePoint *geometry = calloc(count, sizeof(*geometry));
    if (!geometry) return false;

    geometry[0].lat = start->lat;
    geometry[0].lon = start->lon;
    for (uint32_t i = 0U; i < route->node_count; ++i) {
        openride_routing_node_geo(&graph->nodes[route->nodes[i]],
                                  &geometry[i + 1U].lat,
                                  &geometry[i + 1U].lon);
    }
    geometry[count - 1U].lat = destination->lat;
    geometry[count - 1U].lon = destination->lon;

    free(route->geometry);
    route->geometry = geometry;
    route->geometry_count = count;
    return route_build_navigation_context_from_nodes(graph, route);
}

static bool direct_same_segment_route(const OpenRideRoutingGraph *graph,
                                      const OpenRideSnappedRoutingRequest *request,
                                      OpenRideEngineContext *context,
                                      OpenRideRoute *route)
{
    if (request->start.segment_id != request->destination.segment_id) return false;

    const double delta = request->destination.fraction - request->start.fraction;
    if (fabs(delta) < 1e-12) {
        openride_route_destroy(route);
        route->geometry = calloc(2U, sizeof(*route->geometry));
        if (!route->geometry) return false;
        route->geometry_count = 2U;
        route->geometry[0] = (OpenRideRoutePoint){request->start.lat, request->start.lon};
        route->geometry[1] = (OpenRideRoutePoint){request->destination.lat, request->destination.lon};
        return true;
    }

    const OpenRideRoutingEdge *edge = delta > 0.0
        ? find_edge(graph, request->start.a, request->start.b)
        : find_edge(graph, request->start.b, request->start.a);
    if (!edge || !edge_allowed(edge, context)) return false;

    openride_route_destroy(route);
    route->geometry = calloc(2U, sizeof(*route->geometry));
    if (!route->geometry) return false;
    route->geometry_count = 2U;
    route->geometry[0] = (OpenRideRoutePoint){request->start.lat, request->start.lon};
    route->geometry[1] = (OpenRideRoutePoint){request->destination.lat, request->destination.lon};
    add_partial_metrics(edge,
                        fabs(delta),
                        context,
                        &route->distance_m,
                        &route->estimated_time_s,
                        &route->weighted_cost_s);
    return true;
}

bool openride_routing_engine_calculate_snapped(
    const OpenRideRoutingGraph *graph,
    const OpenRideSnappedRoutingRequest *request,
    OpenRideRoute *route,
    char *error,
    size_t error_size)
{
    if (!graph || !request || !route) {
        set_error(error, error_size, "invalid snapped routing arguments");
        return false;
    }
    if (!snap_valid_for_graph(graph, &request->start)
        || !snap_valid_for_graph(graph, &request->destination)) {
        set_error(error, error_size, "invalid snapped routing position");
        return false;
    }
    if (request->profile < OPENRIDE_ROUTING_PROFILE_FASTEST
        || request->profile > OPENRIDE_ROUTING_PROFILE_TRAIL) {
        set_error(error, error_size, "unknown routing profile");
        return false;
    }

    OpenRideRoutingRequest base = openride_routing_request_default();
    base.profile = request->profile;
    base.avoid_tolls = request->avoid_tolls;
    base.avoid_ferries = request->avoid_ferries;
    OpenRideEngineContext context = {graph, base};

    OpenRideRoute best = {0};
    bool have_best = false;
    if (direct_same_segment_route(graph, request, &context, &best)) {
        have_best = true;
    }

    OpenRideSnapEndpointOption starts[2];
    OpenRideSnapEndpointOption destinations[2];
    const uint32_t start_count = start_options(graph, &request->start, &context, starts);
    const uint32_t destination_count = destination_options(
        graph, &request->destination, &context, destinations);

    for (uint32_t si = 0U; si < start_count; ++si) {
        for (uint32_t di = 0U; di < destination_count; ++di) {
            OpenRideRoutingRequest node_request = base;
            node_request.start = starts[si].node;
            node_request.destination = destinations[di].node;

            OpenRideRoute candidate = {0};
            char candidate_error[128] = {0};
            if (!openride_routing_engine_calculate(graph,
                                                   &node_request,
                                                   &candidate,
                                                   candidate_error,
                                                   sizeof(candidate_error))) {
                continue;
            }

            add_partial_metrics(starts[si].edge,
                                starts[si].fraction,
                                &context,
                                &candidate.distance_m,
                                &candidate.estimated_time_s,
                                &candidate.weighted_cost_s);
            add_partial_metrics(destinations[di].edge,
                                destinations[di].fraction,
                                &context,
                                &candidate.distance_m,
                                &candidate.estimated_time_s,
                                &candidate.weighted_cost_s);

            if (!build_snapped_geometry(graph,
                                        &request->start,
                                        &request->destination,
                                        &candidate)) {
                openride_route_destroy(&candidate);
                openride_route_destroy(&best);
                set_error(error, error_size, "unable to allocate snapped route geometry");
                return false;
            }

            if (!have_best || candidate.weighted_cost_s < best.weighted_cost_s) {
                openride_route_destroy(&best);
                best = candidate;
                memset(&candidate, 0, sizeof(candidate));
                have_best = true;
            }
            openride_route_destroy(&candidate);
        }
    }

    if (!have_best) {
        set_error(error, error_size, "no route between snapped positions");
        return false;
    }

    openride_route_destroy(route);
    *route = best;
    set_error(error, error_size, "");
    return true;
}

const char *openride_routing_profile_name(OpenRideRoutingProfile profile)
{
    switch (profile) {
        case OPENRIDE_ROUTING_PROFILE_FASTEST: return "fastest";
        case OPENRIDE_ROUTING_PROFILE_TOURING: return "touring";
        case OPENRIDE_ROUTING_PROFILE_TRAIL:   return "trail";
        default:                               return "unknown";
    }
}
