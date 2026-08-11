#include "openride/routing_graph.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static OpenRideRoutingGraph build_fixture(void)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder = openride_routing_graph_builder_create();
    assert(builder != NULL);

    const OpenRideRoutingNodeId a = openride_routing_graph_builder_add_node(builder, 50.3700, 3.0800);
    const OpenRideRoutingNodeId b = openride_routing_graph_builder_add_node(builder, 50.3710, 3.0810);
    const OpenRideRoutingNodeId c = openride_routing_graph_builder_add_node(builder, 50.3720, 3.0820);
    const OpenRideRoutingNodeId d = openride_routing_graph_builder_add_node(builder, 50.3715, 3.0840);

    assert(a == 0U && b == 1U && c == 2U && d == 3U);

    OpenRideRoutingEdgeAttributes road = openride_routing_edge_attributes_default();
    road.road_class = OPENRIDE_ROAD_RESIDENTIAL;
    road.surface = OPENRIDE_SURFACE_ASPHALT;
    road.max_speed_kph = 50U;

    assert(openride_routing_graph_builder_add_bidirectional_edge(builder, a, b, &road));
    assert(openride_routing_graph_builder_add_bidirectional_edge(builder, b, c, &road));

    OpenRideRoutingEdgeAttributes trail = openride_routing_edge_attributes_default();
    trail.road_class = OPENRIDE_ROAD_TRACK;
    trail.surface = OPENRIDE_SURFACE_GRAVEL;
    trail.flags = OPENRIDE_EDGE_FLAG_UNPAVED;
    trail.max_speed_kph = 30U;

    assert(openride_routing_graph_builder_add_directed_edge(builder, c, d, &trail));

    char error[256] = {0};
    assert(openride_routing_graph_builder_build(builder, &graph, error, sizeof(error)));
    assert(error[0] == '\0');
    openride_routing_graph_builder_destroy(builder);
    return graph;
}

static void test_builder_and_adjacency(void)
{
    OpenRideRoutingGraph graph = build_fixture();

    assert(graph.node_count == 4U);
    assert(graph.edge_count == 5U);
    assert(openride_routing_graph_has_spatial_index(&graph));
    assert(graph.spatial_index.cell_count > 0U);
    assert(graph.nodes[0].first_edge == 0U);
    assert(graph.nodes[0].edge_count == 1U);
    assert(graph.nodes[1].first_edge == 1U);
    assert(graph.nodes[1].edge_count == 2U);
    assert(graph.nodes[2].first_edge == 3U);
    assert(graph.nodes[2].edge_count == 2U);
    assert(graph.nodes[3].first_edge == 5U);
    assert(graph.nodes[3].edge_count == 0U);

    const OpenRideRoutingEdge *edge = &graph.edges[graph.nodes[2].first_edge + 1U];
    assert(edge->target == 3U);
    assert(edge->road_class == OPENRIDE_ROAD_TRACK);
    assert(edge->surface == OPENRIDE_SURFACE_GRAVEL);
    assert((edge->flags & OPENRIDE_EDGE_FLAG_UNPAVED) != 0U);
    assert(edge->max_speed_kph == 30U);
    assert(edge->length_cm > 0U);

    char error[256] = {0};
    assert(openride_routing_graph_validate(&graph, error, sizeof(error)));
    openride_routing_graph_destroy(&graph);
}

static void test_nearest_node(void)
{
    OpenRideRoutingGraph graph = build_fixture();
    double distance_m = 0.0;

    const OpenRideRoutingNodeId nearest = openride_routing_graph_nearest_node(
        &graph, 50.37102, 3.08102, &distance_m);
    double linear_distance_m = 0.0;
    const OpenRideRoutingNodeId linear = openride_routing_graph_nearest_node_linear(
        &graph, 50.37102, 3.08102, &linear_distance_m);

    assert(nearest == 1U);
    assert(linear == nearest);
    assert(fabs(linear_distance_m - distance_m) < 0.001);
    assert(distance_m < 5.0);

    openride_routing_graph_destroy(&graph);
}

static void test_spatial_index_matches_linear(void)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder = openride_routing_graph_builder_create();
    assert(builder != NULL);

    for (uint32_t row = 0U; row < 35U; ++row) {
        for (uint32_t col = 0U; col < 45U; ++col) {
            const double lat = 50.0 + (double)row * 0.0031;
            const double lon = 2.5 + (double)col * 0.0037;
            assert(openride_routing_graph_builder_add_node(builder, lat, lon)
                   != OPENRIDE_ROUTING_NODE_NONE);
        }
    }

    char error[256] = {0};
    assert(openride_routing_graph_builder_build(builder, &graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);

    const double queries[][2] = {
        {50.0004, 2.5002},
        {50.0372, 2.5581},
        {50.0819, 2.6427},
        {49.9950, 2.4800},
        {50.1200, 2.6900}
    };

    for (size_t i = 0U; i < sizeof(queries) / sizeof(queries[0]); ++i) {
        double indexed_distance = 0.0;
        double linear_distance = 0.0;
        const OpenRideRoutingNodeId indexed = openride_routing_graph_nearest_node(
            &graph, queries[i][0], queries[i][1], &indexed_distance);
        const OpenRideRoutingNodeId linear = openride_routing_graph_nearest_node_linear(
            &graph, queries[i][0], queries[i][1], &linear_distance);
        assert(indexed == linear);
        assert(fabs(indexed_distance - linear_distance) < 0.001);
    }

    openride_routing_graph_destroy(&graph);
}

static void test_binary_roundtrip(void)
{
    const char *path = "openride-test-routing.orgraph";
    OpenRideRoutingGraph graph = build_fixture();
    OpenRideRoutingGraph loaded = {0};
    char error[256] = {0};

    assert(openride_routing_graph_save(&graph, path, error, sizeof(error)));
    assert(openride_routing_graph_load(&loaded, path, error, sizeof(error)));

    assert(loaded.node_count == graph.node_count);
    assert(loaded.edge_count == graph.edge_count);
    assert(memcmp(loaded.nodes,
                  graph.nodes,
                  (size_t)graph.node_count * sizeof(graph.nodes[0])) == 0);
    assert(memcmp(loaded.edges,
                  graph.edges,
                  (size_t)graph.edge_count * sizeof(graph.edges[0])) == 0);
    assert(loaded.spatial_index.cell_size_e7 == graph.spatial_index.cell_size_e7);
    assert(loaded.spatial_index.rows == graph.spatial_index.rows);
    assert(loaded.spatial_index.columns == graph.spatial_index.columns);
    assert(loaded.spatial_index.cell_count == graph.spatial_index.cell_count);
    assert(memcmp(loaded.spatial_index.cell_offsets,
                  graph.spatial_index.cell_offsets,
                  ((size_t)graph.spatial_index.cell_count + 1U)
                      * sizeof(graph.spatial_index.cell_offsets[0])) == 0);
    assert(memcmp(loaded.spatial_index.node_ids,
                  graph.spatial_index.node_ids,
                  (size_t)graph.node_count * sizeof(graph.spatial_index.node_ids[0])) == 0);

    remove(path);
    openride_routing_graph_destroy(&loaded);
    openride_routing_graph_destroy(&graph);
}

static void test_invalid_inputs(void)
{
    OpenRideRoutingGraphBuilder *builder = openride_routing_graph_builder_create();
    assert(builder != NULL);
    assert(openride_routing_graph_builder_add_node(builder, 100.0, 3.0)
           == OPENRIDE_ROUTING_NODE_NONE);

    const OpenRideRoutingNodeId node = openride_routing_graph_builder_add_node(builder, 50.0, 3.0);
    assert(node == 0U);
    assert(!openride_routing_graph_builder_add_directed_edge(
        builder, node, 99U, NULL));

    openride_routing_graph_builder_destroy(builder);
}

int main(void)
{
    test_builder_and_adjacency();
    test_nearest_node();
    test_spatial_index_matches_linear();
    test_binary_roundtrip();
    test_invalid_inputs();
    puts("Routing graph tests: OK");
    return 0;
}
