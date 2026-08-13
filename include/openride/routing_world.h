#ifndef OPENRIDE_ROUTING_WORLD_H
#define OPENRIDE_ROUTING_WORLD_H

#include "openride/routing_engine.h"
#include "openride/routing_graph.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES 8U

typedef struct OpenRideRoutingWorldResult {
    uint32_t shared_gateway_count;
    uint32_t attempted_gateways;
    double gateway_lat;
    double gateway_lon;
} OpenRideRoutingWorldResult;

/*
 * First RoutingWorld primitive: route directly between two regional graphs.
 *
 * Adjacent Geofabrik extracts generally share OSM nodes around their boundary.
 * Exact coordinate matches are treated as zero-distance hand-off gateways.
 *
 * The resulting route is geometry-first: route->nodes is NULL because node ids
 * belong to different regional graphs.
 */
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
    size_t error_size);

#endif
