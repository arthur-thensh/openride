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

    assert(nearest == 1U);
    assert(distance_m < 5.0);

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
    test_binary_roundtrip();
    test_invalid_inputs();
    puts("Routing graph tests: OK");
    return 0;
}
