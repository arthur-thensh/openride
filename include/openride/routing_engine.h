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

typedef struct OpenRideRoute {
    OpenRideRoutingNodeId *nodes;
    uint32_t node_count;
    double distance_m;
    double estimated_time_s;
    double weighted_cost_s;
} OpenRideRoute;

OpenRideRoutingRequest openride_routing_request_default(void);

void openride_route_destroy(OpenRideRoute *route);

bool openride_routing_engine_calculate(const OpenRideRoutingGraph *graph,
                                       const OpenRideRoutingRequest *request,
                                       OpenRideRoute *route,
                                       char *error,
                                       size_t error_size);

const char *openride_routing_profile_name(OpenRideRoutingProfile profile);

#endif
