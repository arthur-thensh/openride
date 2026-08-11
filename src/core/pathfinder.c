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
