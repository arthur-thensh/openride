#ifndef OPENRIDE_ROUTING_WORLD_H
#define OPENRIDE_ROUTING_WORLD_H

#include "openride/map_selection.h"
#include "openride/platform_paths.h"
#include "openride/region_manager.h"
#include "openride/region_network.h"
#include "openride/routing_engine.h"
#include "openride/routing_graph.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES 8U
#define OPENRIDE_ROUTING_WORLD_MAX_CORRIDOR_REGIONS OPENRIDE_REGION_NETWORK_MAX_REGIONS
#define OPENRIDE_ROUTING_WORLD_REGION_ID_SIZE 64U

typedef struct OpenRideRoutingWorldCorridorSummary {
    char region_ids[OPENRIDE_ROUTING_WORLD_MAX_CORRIDOR_REGIONS]
                   [OPENRIDE_ROUTING_WORLD_REGION_ID_SIZE];
    uint32_t count;
    double estimated_distance_m;
} OpenRideRoutingWorldCorridorSummary;

typedef struct OpenRideRoutingWorldResult {
    bool multi_region;
    bool multi_hop;
    uint32_t routed_region_count;
    char start_region_id[OPENRIDE_ROUTING_WORLD_REGION_ID_SIZE];
    char destination_region_id[OPENRIDE_ROUTING_WORLD_REGION_ID_SIZE];

    bool corridor_planned;
    bool download_required;
    OpenRideRoutingWorldCorridorSummary recommended_corridor;
    char missing_region_ids[OPENRIDE_ROUTING_WORLD_MAX_CORRIDOR_REGIONS]
                           [OPENRIDE_ROUTING_WORLD_REGION_ID_SIZE];
    uint32_t missing_region_count;
    bool has_installed_alternative;
    OpenRideRoutingWorldCorridorSummary installed_alternative;
    bool used_installed_alternative;

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
 * Convert a RegionNetwork decision into the RoutingWorld public result.
 *
 * The recommended corridor is always computed without download penalties.
 * Installed-only routing, when available, is exposed only as a separate
 * fallback. This function does not load graphs or calculate a road route.
 */
bool openride_routing_world_plan_regions(
    const OpenRideRegionDefinition *start_region,
    double start_lat,
    double start_lon,
    const OpenRideRegionDefinition *destination_region,
    double destination_lat,
    double destination_lon,
    const bool *installed,
    size_t installed_count,
    OpenRideRoutingWorldResult *result,
    char *error,
    size_t error_size);

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
 * v0.23 supports single-region, direct adjacent-region and sequential
 * multi-hop routing across an installed RegionNetwork corridor. Multi-hop
 * resolves exact gateway transitions in one graph pass and retains only the
 * candidate route geometries needed for final backtracking.
 */
/*
 * Explicit user-selected fallback: calculate along RegionNetwork's
 * installed-only corridor while preserving the recommended corridor in result.
 * This function never auto-selects the fallback.
 */
bool openride_routing_world_calculate_installed_alternative_cached(
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

bool openride_routing_world_calculate_selection_cached(
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *active_region,
    const OpenRideRoutingGraph *active_graph,
    OpenRideRoutingWorldCache *cache,
    const OpenRideMapSelection *selection,
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
