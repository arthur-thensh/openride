#include "pathfinder.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct OpenRideHeapEntry {
    OpenRideRoutingNodeId node;
    double priority;
} OpenRideHeapEntry;

typedef struct OpenRideMinHeap {
    OpenRideHeapEntry *entries;
    size_t count;
    size_t capacity;
} OpenRideMinHeap;

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
}

static bool heap_grow(OpenRideMinHeap *heap)
{
    const size_t new_capacity = heap->capacity == 0U ? 256U : heap->capacity * 2U;
    if (new_capacity < heap->capacity) return false;

    OpenRideHeapEntry *new_entries = realloc(heap->entries,
                                             new_capacity * sizeof(*new_entries));
    if (!new_entries) return false;

    heap->entries = new_entries;
    heap->capacity = new_capacity;
    return true;
}

static bool heap_push(OpenRideMinHeap *heap,
                      OpenRideRoutingNodeId node,
                      double priority)
{
    if (heap->count == heap->capacity && !heap_grow(heap)) return false;

    size_t index = heap->count++;
    heap->entries[index].node = node;
    heap->entries[index].priority = priority;

    while (index > 0U) {
        const size_t parent = (index - 1U) / 2U;
        if (heap->entries[parent].priority <= heap->entries[index].priority) break;

        const OpenRideHeapEntry temp = heap->entries[parent];
        heap->entries[parent] = heap->entries[index];
        heap->entries[index] = temp;
        index = parent;
    }

    return true;
}

static bool heap_pop(OpenRideMinHeap *heap, OpenRideHeapEntry *entry)
{
    if (!heap || heap->count == 0U || !entry) return false;

    *entry = heap->entries[0];
    --heap->count;
    if (heap->count == 0U) return true;

    heap->entries[0] = heap->entries[heap->count];
    size_t index = 0U;

    for (;;) {
        const size_t left = index * 2U + 1U;
        const size_t right = left + 1U;
        size_t smallest = index;

        if (left < heap->count
            && heap->entries[left].priority < heap->entries[smallest].priority) {
            smallest = left;
        }
        if (right < heap->count
            && heap->entries[right].priority < heap->entries[smallest].priority) {
            smallest = right;
        }
        if (smallest == index) break;

        const OpenRideHeapEntry temp = heap->entries[index];
        heap->entries[index] = heap->entries[smallest];
        heap->entries[smallest] = temp;
        index = smallest;
    }

    return true;
}

static bool reconstruct_path(OpenRideRoutingNodeId start,
                             OpenRideRoutingNodeId destination,
                             const OpenRideRoutingNodeId *parent,
                             double total_cost,
                             OpenRidePathfinderResult *result,
                             char *error,
                             size_t error_size)
{
    uint32_t count = 1U;
    OpenRideRoutingNodeId cursor = destination;

    while (cursor != start) {
        cursor = parent[cursor];
        if (cursor == OPENRIDE_ROUTING_NODE_NONE) {
            set_error(error, error_size, "path reconstruction failed");
            return false;
        }
        if (count == UINT32_MAX) {
            set_error(error, error_size, "route contains too many nodes");
            return false;
        }
        ++count;
    }

    OpenRideRoutingNodeId *nodes = malloc((size_t)count * sizeof(*nodes));
    if (!nodes) {
        set_error(error, error_size, "unable to allocate route nodes");
        return false;
    }

    cursor = destination;
    for (uint32_t i = count; i > 0U; --i) {
        nodes[i - 1U] = cursor;
        if (cursor == start) break;
        cursor = parent[cursor];
    }

    openride_pathfinder_result_destroy(result);
    result->nodes = nodes;
    result->node_count = count;
    result->total_cost = total_cost;
    return true;
}

void openride_pathfinder_result_destroy(OpenRidePathfinderResult *result)
{
    if (!result) return;
    free(result->nodes);
    memset(result, 0, sizeof(*result));
}

typedef struct OpenRideFrontierHeuristicBall {
    bool enabled;
    OpenRideRoutingNodeId center;
    double radius;
} OpenRideFrontierHeuristicBall;

static bool frontier_heuristic_ball_build(
    const OpenRideRoutingNodeId *targets,
    uint32_t target_count,
    const OpenRidePathfinderCallbacks *callbacks,
    OpenRideFrontierHeuristicBall *ball)
{
    if (!targets || target_count == 0U || !callbacks || !ball) return false;

    memset(ball, 0, sizeof(*ball));
    ball->center = OPENRIDE_ROUTING_NODE_NONE;

    /*
     * If no heuristic is available, retain exact multi-source Dijkstra.
     */
    if (!callbacks->heuristic) return true;

    /*
     * Build the smallest target-centered metric ball among the <= 8 targets.
     *
     * The callback heuristic used by RoutingEngine is straight-line travel
     * time at the global maximum speed. It is an admissible metric lower
     * bound. For a ball(center, radius) containing every target:
     *
     *   h_set(v) = max(0, h(v, center) - radius)
     *
     * is a lower bound to the distance from v to ANY target by triangle
     * inequality. It therefore keeps the frontier search exact while giving
     * it A*-style directionality.
     *
     * Choosing the best center among the target nodes costs at most 8*8
     * heuristic calls and avoids doing up to 8 heuristic calls per relaxed
     * graph node.
     */
    double best_radius = DBL_MAX;
    OpenRideRoutingNodeId best_center = OPENRIDE_ROUTING_NODE_NONE;

    for (uint32_t center_index = 0U;
         center_index < target_count;
         ++center_index) {
        const OpenRideRoutingNodeId center = targets[center_index];
        double radius = 0.0;
        bool valid = true;

        for (uint32_t target_index = 0U;
             target_index < target_count;
             ++target_index) {
            const double h = callbacks->heuristic(
                center,
                targets[target_index],
                callbacks->context);
            if (!isfinite(h) || h < 0.0) {
                valid = false;
                break;
            }
            if (h > radius) radius = h;
        }

        if (valid && radius < best_radius) {
            best_radius = radius;
            best_center = center;
        }
    }

    if (best_center == OPENRIDE_ROUTING_NODE_NONE
        || !isfinite(best_radius)
        || best_radius < 0.0) {
        return true;
    }

    /*
     * Tiny outward padding guarantees h(target)==0 despite floating-point
     * roundoff when the symmetric heuristic is evaluated in reverse order.
     */
    ball->enabled = true;
    ball->center = best_center;
    ball->radius = best_radius + 1e-9;
    return true;
}

static double frontier_heuristic_ball_value(
    OpenRideRoutingNodeId node,
    const OpenRidePathfinderCallbacks *callbacks,
    const OpenRideFrontierHeuristicBall *ball)
{
    if (!callbacks || !ball || !ball->enabled || !callbacks->heuristic) {
        return 0.0;
    }

    const double h = callbacks->heuristic(
        node,
        ball->center,
        callbacks->context);
    if (!isfinite(h) || h < 0.0) return -1.0;
    return h > ball->radius ? h - ball->radius : 0.0;
}

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
    size_t error_size)
{
    if (!graph || !sources || !source_costs || source_count == 0U
        || !targets || target_count == 0U || !callbacks
        || !callbacks->edge_cost || !target_costs
        || !target_source_indices || !target_paths || !reachable) {
        set_error(error, error_size, "invalid frontier pathfinder arguments");
        return false;
    }

    for (uint32_t i = 0U; i < source_count; ++i) {
        if (sources[i] >= graph->node_count
            || !isfinite(source_costs[i])
            || source_costs[i] < 0.0) {
            set_error(error, error_size, "frontier source is invalid");
            return false;
        }
    }

    for (uint32_t i = 0U; i < target_count; ++i) {
        if (targets[i] >= graph->node_count) {
            set_error(error, error_size, "frontier target is out of bounds");
            return false;
        }
        target_costs[i] = DBL_MAX;
        target_source_indices[i] = UINT32_MAX;
        reachable[i] = false;
        memset(&target_paths[i], 0, sizeof(target_paths[i]));
    }

    OpenRideFrontierHeuristicBall heuristic_ball;
    if (!frontier_heuristic_ball_build(
            targets,
            target_count,
            callbacks,
            &heuristic_ball)) {
        set_error(error, error_size, "unable to initialize frontier heuristic");
        return false;
    }

    const size_t node_count = graph->node_count;
    double *g_score = malloc(node_count * sizeof(*g_score));
    uint32_t *origin = malloc(node_count * sizeof(*origin));
    OpenRideRoutingNodeId *parent = malloc(node_count * sizeof(*parent));
    unsigned char *closed = calloc(node_count, sizeof(*closed));
    OpenRideMinHeap open = {0};

    if (!g_score || !origin || !parent || !closed) {
        free(g_score);
        free(origin);
        free(parent);
        free(closed);
        set_error(error, error_size, "unable to allocate frontier pathfinder state");
        return false;
    }

    for (size_t i = 0U; i < node_count; ++i) {
        g_score[i] = DBL_MAX;
        origin[i] = UINT32_MAX;
        parent[i] = OPENRIDE_ROUTING_NODE_NONE;
    }

    for (uint32_t i = 0U; i < source_count; ++i) {
        const OpenRideRoutingNodeId node = sources[i];
        if (source_costs[i] >= g_score[node]) continue;

        const double heuristic = frontier_heuristic_ball_value(
            node,
            callbacks,
            &heuristic_ball);
        if (!isfinite(heuristic) || heuristic < 0.0) {
            free(g_score);
            free(origin);
            free(parent);
            free(closed);
            free(open.entries);
            set_error(error, error_size, "invalid frontier heuristic");
            return false;
        }

        g_score[node] = source_costs[i];
        origin[node] = i;
        parent[node] = OPENRIDE_ROUTING_NODE_NONE;
        if (!heap_push(&open, node, source_costs[i] + heuristic)) {
            free(g_score);
            free(origin);
            free(parent);
            free(closed);
            free(open.entries);
            set_error(error, error_size, "unable to initialize frontier queue");
            return false;
        }
    }

    uint32_t unresolved = target_count;
    OpenRideHeapEntry current;

    while (unresolved > 0U && heap_pop(&open, &current)) {
        const OpenRideRoutingNodeId node_id = current.node;
        if (closed[node_id]) continue;
        /*
         * With a fixed consistent heuristic, stale entries for the same node
         * always have a larger f-score than the newest entry. The best entry
         * therefore closes the node first and later stale entries are skipped
         * by the closed[] guard above.
         */
        closed[node_id] = 1U;

        for (uint32_t i = 0U; i < target_count; ++i) {
            if (reachable[i] || targets[i] != node_id) continue;

            const uint32_t source_index = origin[node_id];
            if (source_index >= source_count) {
                for (uint32_t j = 0U; j < target_count; ++j) {
                    openride_pathfinder_result_destroy(&target_paths[j]);
                }
                free(g_score);
                free(origin);
                free(parent);
                free(closed);
                free(open.entries);
                set_error(error, error_size, "frontier target has no source");
                return false;
            }

            if (!reconstruct_path(sources[source_index],
                                  node_id,
                                  parent,
                                  g_score[node_id],
                                  &target_paths[i],
                                  error,
                                  error_size)) {
                for (uint32_t j = 0U; j < target_count; ++j) {
                    openride_pathfinder_result_destroy(&target_paths[j]);
                }
                free(g_score);
                free(origin);
                free(parent);
                free(closed);
                free(open.entries);
                return false;
            }

            reachable[i] = true;
            target_costs[i] = g_score[node_id];
            target_source_indices[i] = source_index;
            --unresolved;
        }
        if (unresolved == 0U) break;

        const OpenRideRoutingNode *node = &graph->nodes[node_id];
        for (uint32_t i = 0U; i < node->edge_count; ++i) {
            const OpenRideRoutingEdge *edge = &graph->edges[node->first_edge + i];
            if (callbacks->edge_allowed
                && !callbacks->edge_allowed(edge, callbacks->context)) {
                continue;
            }

            const double edge_cost = callbacks->edge_cost(edge, callbacks->context);
            if (!isfinite(edge_cost) || edge_cost < 0.0) continue;

            const OpenRideRoutingNodeId neighbor = edge->target;
            if (closed[neighbor]) continue;

            const double tentative = g_score[node_id] + edge_cost;
            if (tentative >= g_score[neighbor]) continue;

            const double heuristic = frontier_heuristic_ball_value(
                neighbor,
                callbacks,
                &heuristic_ball);
            if (!isfinite(heuristic) || heuristic < 0.0) continue;

            g_score[neighbor] = tentative;
            origin[neighbor] = origin[node_id];
            parent[neighbor] = node_id;

            if (!heap_push(&open, neighbor, tentative + heuristic)) {
                for (uint32_t j = 0U; j < target_count; ++j) {
                    openride_pathfinder_result_destroy(&target_paths[j]);
                }
                free(g_score);
                free(origin);
                free(parent);
                free(closed);
                free(open.entries);
                set_error(error, error_size, "unable to grow frontier queue");
                return false;
            }
        }
    }

    free(g_score);
    free(origin);
    free(parent);
    free(closed);
    free(open.entries);
    set_error(error, error_size, "");
    return true;
}


bool openride_pathfinder_find(const OpenRideRoutingGraph *graph,
                              OpenRideRoutingNodeId start,
                              OpenRideRoutingNodeId destination,
                              const OpenRidePathfinderCallbacks *callbacks,
                              OpenRidePathfinderResult *result,
                              char *error,
                              size_t error_size)
{
    if (!graph || !result || !callbacks || !callbacks->edge_cost
        || !callbacks->heuristic) {
        set_error(error, error_size, "invalid pathfinder arguments");
        return false;
    }
    if (start >= graph->node_count || destination >= graph->node_count) {
        set_error(error, error_size, "routing node is out of bounds");
        return false;
    }

    openride_pathfinder_result_destroy(result);

    if (start == destination) {
        result->nodes = malloc(sizeof(*result->nodes));
        if (!result->nodes) {
            set_error(error, error_size, "unable to allocate route nodes");
            return false;
        }
        result->nodes[0] = start;
        result->node_count = 1U;
        result->total_cost = 0.0;
        set_error(error, error_size, "");
        return true;
    }

    const size_t node_count = graph->node_count;
    double *g_score = malloc(node_count * sizeof(*g_score));
    OpenRideRoutingNodeId *parent = malloc(node_count * sizeof(*parent));
    unsigned char *closed = calloc(node_count, sizeof(*closed));
    OpenRideMinHeap open = {0};

    if (!g_score || !parent || !closed) {
        free(g_score);
        free(parent);
        free(closed);
        set_error(error, error_size, "unable to allocate pathfinder state");
        return false;
    }

    for (size_t i = 0U; i < node_count; ++i) {
        g_score[i] = DBL_MAX;
        parent[i] = OPENRIDE_ROUTING_NODE_NONE;
    }

    g_score[start] = 0.0;
    const double initial_h = callbacks->heuristic(start, destination,
                                                   callbacks->context);
    if (!isfinite(initial_h) || initial_h < 0.0
        || !heap_push(&open, start, initial_h)) {
        free(g_score);
        free(parent);
        free(closed);
        free(open.entries);
        set_error(error, error_size, "unable to initialize pathfinder");
        return false;
    }

    bool found = false;
    OpenRideHeapEntry current;

    while (heap_pop(&open, &current)) {
        const OpenRideRoutingNodeId node_id = current.node;
        if (closed[node_id]) continue;
        closed[node_id] = 1U;

        if (node_id == destination) {
            found = true;
            break;
        }

        const OpenRideRoutingNode *node = &graph->nodes[node_id];
        for (uint32_t i = 0U; i < node->edge_count; ++i) {
            const OpenRideRoutingEdge *edge = &graph->edges[node->first_edge + i];
            if (callbacks->edge_allowed
                && !callbacks->edge_allowed(edge, callbacks->context)) {
                continue;
            }

            const double edge_cost = callbacks->edge_cost(edge, callbacks->context);
            if (!isfinite(edge_cost) || edge_cost < 0.0) continue;

            const OpenRideRoutingNodeId neighbor = edge->target;
            if (closed[neighbor]) continue;

            const double tentative = g_score[node_id] + edge_cost;
            if (tentative >= g_score[neighbor]) continue;

            const double heuristic = callbacks->heuristic(neighbor,
                                                          destination,
                                                          callbacks->context);
            if (!isfinite(heuristic) || heuristic < 0.0) continue;

            g_score[neighbor] = tentative;
            parent[neighbor] = node_id;

            if (!heap_push(&open, neighbor, tentative + heuristic)) {
                free(g_score);
                free(parent);
                free(closed);
                free(open.entries);
                set_error(error, error_size, "unable to grow pathfinder queue");
                return false;
            }
        }
    }

    bool ok = false;
    if (found) {
        ok = reconstruct_path(start,
                              destination,
                              parent,
                              g_score[destination],
                              result,
                              error,
                              error_size);
    } else {
        set_error(error, error_size, "no route found");
    }

    free(g_score);
    free(parent);
    free(closed);
    free(open.entries);
    return ok;
}
