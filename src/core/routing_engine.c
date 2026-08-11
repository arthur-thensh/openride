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

void openride_route_destroy(OpenRideRoute *route)
{
    if (!route) return;
    free(route->nodes);
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
