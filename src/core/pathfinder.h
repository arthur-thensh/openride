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

/*
 * Exact multi-source frontier with path reconstruction.
 *
 * The implementation uses an admissible target-set A* lower bound when a
 * heuristic callback is available, and falls back to Dijkstra otherwise.
 * Each source starts with its own accumulated cost. For every target, return
 * the minimum accumulated cost, the winning source index and the exact path
 * from that source to the target. This lets RoutingWorld keep the selected
 * candidate geometries while the regional graph is already resident, avoiding
 * a second graph-loading / A* reconstruction pass.
 *
 * target_paths entries must be zero-initialized by the caller.
 * Unreachable targets are not an API error: reachable[i] remains false.
 */
bool openride_pathfinder_find_frontier_paths(
    const OpenRideRoutingGraph *graph,
    const OpenRideRoutingNodeId *sources,
    const double *source_costs,
    uint32_t source_count,
    const OpenRideRoutingNodeId *targets,
    uint32_t target_count,
    const OpenRidePathfinderCallbacks *callbacks,
    double *target_costs,
    uint32_t *target_source_indices,
    OpenRidePathfinderResult *target_paths,
    bool *reachable,
    char *error,
    size_t error_size);

#endif
