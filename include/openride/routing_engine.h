#ifndef OPENRIDE_ROUTING_ENGINE_H
#define OPENRIDE_ROUTING_ENGINE_H

#include "openride/routing_graph.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum OpenRideRoutingProfile {
    OPENRIDE_ROUTING_PROFILE_FASTEST = 0,
    OPENRIDE_ROUTING_PROFILE_TOURING,
    OPENRIDE_ROUTING_PROFILE_TRAIL
} OpenRideRoutingProfile;

typedef struct OpenRideRoutingRequest {
    OpenRideRoutingNodeId start;
    OpenRideRoutingNodeId destination;
    OpenRideRoutingProfile profile;
    bool avoid_tolls;
    bool avoid_ferries;
} OpenRideRoutingRequest;

typedef struct OpenRideRoutePoint {
    double lat;
    double lon;
} OpenRideRoutePoint;

/*
 * Lightweight navigation metadata aligned one-to-one with route geometry.
 *
 * Regional node ids cannot survive RoutingWorld assembly because each graph has
 * its own id space. These flags preserve only the graph context required by
 * navigation instructions while the regional graph is still available.
 */
enum {
    OPENRIDE_ROUTE_NAV_HAS_ALTERNATIVE       = 1U << 0,
    OPENRIDE_ROUTE_NAV_INCOMING_ROUNDABOUT   = 1U << 1,
    OPENRIDE_ROUTE_NAV_OUTGOING_ROUNDABOUT   = 1U << 2,
    OPENRIDE_ROUTE_NAV_HAS_ROUNDABOUT_EXIT   = 1U << 3
};

typedef struct OpenRideRouteNavigationContext {
    uint8_t flags;
} OpenRideRouteNavigationContext;

typedef struct OpenRideRoute {
    OpenRideRoutingNodeId *nodes;
    uint32_t node_count;
    OpenRideRoutePoint *geometry;
    uint32_t geometry_count;
    OpenRideRouteNavigationContext *navigation_context;
    uint32_t navigation_context_count;
    double distance_m;
    double estimated_time_s;
    double weighted_cost_s;
} OpenRideRoute;

typedef struct OpenRideSnappedRoutingRequest {
    OpenRideRoutingSnap start;
    OpenRideRoutingSnap destination;
    OpenRideRoutingProfile profile;
    bool avoid_tolls;
    bool avoid_ferries;
} OpenRideSnappedRoutingRequest;

OpenRideRoutingRequest openride_routing_request_default(void);

void openride_route_destroy(OpenRideRoute *route);

bool openride_routing_engine_calculate(const OpenRideRoutingGraph *graph,
                                       const OpenRideRoutingRequest *request,
                                       OpenRideRoute *route,
                                       char *error,
                                       size_t error_size);

/*
 * Exact routes from an accumulated multi-source frontier to several targets.
 *
 * target_costs contain total accumulated weighted seconds. target_routes contain
 * only the regional source->target segment and are geometry-first (nodes freed
 * before return). target_source_indices identifies the winning source entry.
 */
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
    size_t error_size);

OpenRideSnappedRoutingRequest openride_snapped_routing_request_default(void);

bool openride_routing_engine_calculate_snapped(
    const OpenRideRoutingGraph *graph,
    const OpenRideSnappedRoutingRequest *request,
    OpenRideRoute *route,
    char *error,
    size_t error_size);

const char *openride_routing_profile_name(OpenRideRoutingProfile profile);

#endif
