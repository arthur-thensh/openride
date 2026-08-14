#ifndef OPENRIDE_REGION_NETWORK_H
#define OPENRIDE_REGION_NETWORK_H

#include "openride/platform_paths.h"
#include "openride/region_manager.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPENRIDE_REGION_NETWORK_MAX_REGIONS 32U

typedef struct OpenRideRegionCorridor {
    const OpenRideRegionDefinition *regions[OPENRIDE_REGION_NETWORK_MAX_REGIONS];
    uint32_t count;
    double estimated_distance_m;
} OpenRideRegionCorridor;

typedef struct OpenRideRegionNetworkPlan {
    OpenRideRegionCorridor recommended;
    const OpenRideRegionDefinition *missing_regions[OPENRIDE_REGION_NETWORK_MAX_REGIONS];
    uint32_t missing_count;

    bool has_installed_alternative;
    OpenRideRegionCorridor installed_alternative;
} OpenRideRegionNetworkPlan;

/*
 * Return the small, built-in regional topology entry for a region.
 * The center is only a planning heuristic; it is not used as a routing point.
 */
bool openride_region_network_center(const OpenRideRegionDefinition *region,
                                    double *lat,
                                    double *lon);

bool openride_region_network_adjacent(const OpenRideRegionDefinition *first,
                                      const OpenRideRegionDefinition *second);

/*
 * Build an availability mask aligned with openride_region_at().
 * A region is considered installed when its generated offline package is ready.
 */
bool openride_region_network_installed_mask(
    const OpenRidePlatformPaths *paths,
    bool *installed,
    size_t installed_count,
    char *error,
    size_t error_size);

/*
 * Plan the preferred regional corridor without penalizing missing downloads.
 *
 * installed may be NULL. When supplied, the recommended corridor is STILL
 * computed without installation bias. Missing regions are then reported
 * separately. Only after that does the planner look for an optional route
 * restricted to already-installed regions.
 *
 * Endpoint coordinates refine the coarse regional estimate. The result is a
 * corridor-selection heuristic, not a substitute for .orgraph routing.
 */
bool openride_region_network_plan(
    const OpenRideRegionDefinition *start_region,
    double start_lat,
    double start_lon,
    const OpenRideRegionDefinition *destination_region,
    double destination_lat,
    double destination_lon,
    const bool *installed,
    size_t installed_count,
    OpenRideRegionNetworkPlan *plan,
    char *error,
    size_t error_size);

#endif
