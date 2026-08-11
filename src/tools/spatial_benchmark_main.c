#include "openride/routing_graph.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DEFAULT_LINEAR_SAMPLES 5U
#define DEFAULT_INDEXED_SAMPLES 5000U

static double elapsed_ms(clock_t start, clock_t end)
{
    return (double)(end - start) * 1000.0 / (double)CLOCKS_PER_SEC;
}

static void query_for_sample(const OpenRideRoutingGraph *graph,
                             uint32_t sample,
                             uint32_t sample_count,
                             double *lat,
                             double *lon)
{
    uint32_t node_id = 0U;
    if (sample_count > 1U && graph->node_count > 1U) {
        node_id = (uint32_t)(((uint64_t)sample * (graph->node_count - 1U))
                            / (sample_count - 1U));
    }

    openride_routing_node_geo(&graph->nodes[node_id], lat, lon);
    *lat += (sample & 1U) ? 0.00011 : -0.00009;
    *lon += (sample & 2U) ? 0.00013 : -0.00007;
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

    printf("OpenRide spatial index benchmark\n");
    printf("  graphe      : %s\n", path);
    printf("  noeuds      : %u\n", graph.node_count);
    printf("  cellules    : %u (%u x %u)\n",
           graph.spatial_index.cell_count,
           graph.spatial_index.rows,
           graph.spatial_index.columns);
    printf("  taille cellule: %.4f deg\n\n",
           (double)graph.spatial_index.cell_size_e7 / 10000000.0);

    if (graph.node_count == 0U) {
        openride_routing_graph_destroy(&graph);
        return 0;
    }

    double linear_total = 0.0;
    for (uint32_t i = 0U; i < DEFAULT_LINEAR_SAMPLES; ++i) {
        double lat = 0.0;
        double lon = 0.0;
        double linear_distance = INFINITY;
        double indexed_distance = INFINITY;
        query_for_sample(&graph, i, DEFAULT_LINEAR_SAMPLES, &lat, &lon);

        clock_t start = clock();
        const OpenRideRoutingNodeId linear = openride_routing_graph_nearest_node_linear(
            &graph, lat, lon, &linear_distance);
        clock_t end = clock();
        linear_total += elapsed_ms(start, end);

        const OpenRideRoutingNodeId indexed = openride_routing_graph_nearest_node(
            &graph, lat, lon, &indexed_distance);

        if (linear != indexed || fabs(linear_distance - indexed_distance) > 0.01) {
            fprintf(stderr,
                    "Erreur: l'index ne retourne pas le meme noeud (sample %u).\n",
                    i);
            openride_routing_graph_destroy(&graph);
            return 2;
        }
    }

    clock_t indexed_start = clock();
    for (uint32_t i = 0U; i < DEFAULT_INDEXED_SAMPLES; ++i) {
        double lat = 0.0;
        double lon = 0.0;
        query_for_sample(&graph, i % DEFAULT_LINEAR_SAMPLES,
                         DEFAULT_LINEAR_SAMPLES, &lat, &lon);
        (void)openride_routing_graph_nearest_node(&graph, lat, lon, NULL);
    }
    clock_t indexed_end = clock();

    const double linear_avg = linear_total / DEFAULT_LINEAR_SAMPLES;
    const double indexed_avg = elapsed_ms(indexed_start, indexed_end)
                             / DEFAULT_INDEXED_SAMPLES;

    printf("Recherche lineaire : %.3f ms / requete (%u essais)\n",
           linear_avg,
           DEFAULT_LINEAR_SAMPLES);
    printf("Index spatial       : %.3f ms / requete (%u essais)\n",
           indexed_avg,
           DEFAULT_INDEXED_SAMPLES);
    if (indexed_avg > 0.0) {
        printf("Acceleration        : x%.1f\n", linear_avg / indexed_avg);
    }

    openride_routing_graph_destroy(&graph);
    return 0;
}
