#ifndef OPENRIDE_PATHFINDER_H
#define OPENRIDE_PATHFINDER_H

#include "openride/routing_graph.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*OpenRidePathfinderEdgeAllowedFn)(const OpenRideRoutingEdge *edge,
                                                void *context);
typedef double (*OpenRidePathfinderEdgeCostFn)(const OpenRideRoutingEdge *edge,
                                               void *context);
typedef double (*OpenRidePathfinderHeuristicFn)(OpenRideRoutingNodeId node,
                                                OpenRideRoutingNodeId destination,
                                                void *context);

typedef struct OpenRidePathfinderCallbacks {
    OpenRidePathfinderEdgeAllowedFn edge_allowed;
    OpenRidePathfinderEdgeCostFn edge_cost;
    OpenRidePathfinderHeuristicFn heuristic;
    void *context;
} OpenRidePathfinderCallbacks;

typedef struct OpenRidePathfinderResult {
    OpenRideRoutingNodeId *nodes;
    uint32_t node_count;
    double total_cost;
} OpenRidePathfinderResult;

void openride_pathfinder_result_destroy(OpenRidePathfinderResult *result);

bool openride_pathfinder_find(const OpenRideRoutingGraph *graph,
                              OpenRideRoutingNodeId start,
                              OpenRideRoutingNodeId destination,
                              const OpenRidePathfinderCallbacks *callbacks,
                              OpenRidePathfinderResult *result,
                              char *error,
                              size_t error_size);

#endif
