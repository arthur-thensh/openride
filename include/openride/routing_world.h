#ifndef OPENRIDE_ROUTING_WORLD_H
#define OPENRIDE_ROUTING_WORLD_H

#include "openride/platform_paths.h"
#include "openride/region_manager.h"
#include "openride/routing_engine.h"
#include "openride/routing_graph.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES 8U

typedef struct OpenRideRoutingWorldResult {
    bool multi_region;
    char start_region_id[64];
    char destination_region_id[64];
    uint32_t shared_gateway_count;
    uint32_t attempted_gateways;
    double gateway_lat;
    double gateway_lon;
} OpenRideRoutingWorldResult;

typedef struct OpenRideRoutingWorldCache {
    OpenRideRoutingGraph graph;
    char region_id[64];
    bool loaded;
} OpenRideRoutingWorldCache;

void openride_routing_world_cache_init(OpenRideRoutingWorldCache *cache);
void openride_routing_world_cache_destroy(OpenRideRoutingWorldCache *cache);

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


/*
 * Resolve endpoints from installed .poly coverage, reuse the active graph when
 * possible, and load only the additional .orgraph needed by the request.
 *
 * v0.23 supports a single installed region or a direct hand-off between two
 * adjacent installed regions.
 */
bool openride_routing_world_calculate_installed(
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *active_region,
    const OpenRideRoutingGraph *active_graph,
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

bool openride_routing_world_calculate_installed_cached(
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *active_region,
    const OpenRideRoutingGraph *active_graph,
    OpenRideRoutingWorldCache *cache,
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
