#include "openride/routing_graph.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define LINEAR_SAMPLES 3U
#define INDEXED_SAMPLES 5000U
#define MAX_SNAP_M 250.0

static double elapsed_ms(clock_t start, clock_t end)
{
    return (double)(end - start) * 1000.0 / (double)CLOCKS_PER_SEC;
}

static void query_for_segment(const OpenRideRoutingGraph *graph,
                              uint32_t sample,
                              double *lat,
                              double *lon)
{
    const uint32_t count = graph->segment_index.segment_count;
    const uint32_t segment_id = count > 1U
        ? (uint32_t)(((uint64_t)(sample + 1U) * 2654435761ULL) % count)
        : 0U;
    const OpenRideRoutingSegment *segment = &graph->segment_index.segments[segment_id];
    double a_lat = 0.0;
    double a_lon = 0.0;
    double b_lat = 0.0;
    double b_lon = 0.0;
    openride_routing_node_geo(&graph->nodes[segment->a], &a_lat, &a_lon);
    openride_routing_node_geo(&graph->nodes[segment->b], &b_lat, &b_lon);

    *lat = (a_lat + b_lat) * 0.5 + ((sample & 1U) ? 0.00002 : -0.00002);
    *lon = (a_lon + b_lon) * 0.5 + ((sample & 2U) ? 0.00002 : -0.00002);
}

int main(int argc, char **argv)
{
    const char *path = argc >= 2
        ? argv[1]
        : "data/routing/nord-pas-de-calais.orgraph";
    char error[512] = {0};
    OpenRideRoutingGraph graph = {0};

    if (!openride_routing_graph_load(&graph, path, error, sizeof(error))) {
        fprintf(stderr, "Impossible de charger %s: %s\n", path, error);
        return 1;
    }

    printf("OpenRide segment index benchmark\n");
    printf("  graphe       : %s\n", path);
    printf("  segments     : %u\n", graph.segment_index.segment_count);
    printf("  references   : %u\n", graph.segment_index.ref_count);
    printf("  cellules     : %u\n\n", graph.spatial_index.cell_count);

    if (graph.segment_index.segment_count == 0U) {
        openride_routing_graph_destroy(&graph);
        return 0;
    }

    double linear_total = 0.0;
    for (uint32_t i = 0U; i < LINEAR_SAMPLES; ++i) {
        double lat = 0.0;
        double lon = 0.0;
        OpenRideRoutingSnap linear = {0};
        OpenRideRoutingSnap indexed = {0};
        query_for_segment(&graph, i, &lat, &lon);

        const clock_t start = clock();
        const bool linear_ok = openride_routing_graph_snap_to_segment_linear(
            &graph, lat, lon, MAX_SNAP_M, &linear);
        const clock_t end = clock();
        linear_total += elapsed_ms(start, end);

        const bool indexed_ok = openride_routing_graph_snap_to_segment(
            &graph, lat, lon, MAX_SNAP_M, &indexed);

        if (linear_ok != indexed_ok
            || (linear_ok
                && (linear.segment_id != indexed.segment_id
                    || fabs(linear.distance_m - indexed.distance_m) > 0.05))) {
            fprintf(stderr,
                    "Erreur: l'index segment ne correspond pas a la recherche exhaustive (sample %u).\n",
                    i);
            openride_routing_graph_destroy(&graph);
            return 2;
        }
    }

    const clock_t indexed_start = clock();
    for (uint32_t i = 0U; i < INDEXED_SAMPLES; ++i) {
        double lat = 0.0;
        double lon = 0.0;
        OpenRideRoutingSnap snap = {0};
        query_for_segment(&graph, i, &lat, &lon);
        (void)openride_routing_graph_snap_to_segment(
            &graph, lat, lon, MAX_SNAP_M, &snap);
    }
    const clock_t indexed_end = clock();

    const double linear_avg = linear_total / LINEAR_SAMPLES;
    const double indexed_avg = elapsed_ms(indexed_start, indexed_end) / INDEXED_SAMPLES;

    printf("Recherche segments lineaire : %.3f ms / requete (%u essais)\n",
           linear_avg,
           LINEAR_SAMPLES);
    printf("Index segments              : %.3f ms / requete (%u essais)\n",
           indexed_avg,
           INDEXED_SAMPLES);
    if (indexed_avg > 0.0) {
        printf("Acceleration                : x%.1f\n", linear_avg / indexed_avg);
    }

    openride_routing_graph_destroy(&graph);
    return 0;
}
