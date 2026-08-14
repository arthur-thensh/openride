#include "openride/routing_gateway_index.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static OpenRideRoutingGraph build_left(void)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder = openride_routing_graph_builder_create();
    assert(builder != NULL);

    const OpenRideRoutingNodeId start =
        openride_routing_graph_builder_add_node(builder, 50.0000, 2.9900);
    const OpenRideRoutingNodeId gateway =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0000);

    OpenRideRoutingEdgeAttributes road = openride_routing_edge_attributes_default();
    road.length_m = 1000.0;
    road.road_class = OPENRIDE_ROAD_PRIMARY;
    road.surface = OPENRIDE_SURFACE_ASPHALT;
    road.max_speed_kph = 80U;
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, start, gateway, &road));

    char error[256] = {0};
    assert(openride_routing_graph_builder_build(builder, &graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);
    return graph;
}

static OpenRideRoutingGraph build_right(void)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder = openride_routing_graph_builder_create();
    assert(builder != NULL);

    const OpenRideRoutingNodeId gateway =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0000);
    const OpenRideRoutingNodeId destination =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0100);

    OpenRideRoutingEdgeAttributes road = openride_routing_edge_attributes_default();
    road.length_m = 1000.0;
    road.road_class = OPENRIDE_ROAD_PRIMARY;
    road.surface = OPENRIDE_SURFACE_ASPHALT;
    road.max_speed_kph = 80U;
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, gateway, destination, &road));

    char error[256] = {0};
    assert(openride_routing_graph_builder_build(builder, &graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);
    return graph;
}

int main(void)
{
    OpenRideRoutingGraph left = build_left();
    OpenRideRoutingGraph right = build_right();
    OpenRideRoutingGatewayIndex index;
    OpenRideRoutingGatewayIndex loaded;
    openride_routing_gateway_index_init(&index);
    openride_routing_gateway_index_init(&loaded);

    char error[256] = {0};
    assert(openride_routing_gateway_index_build(
        "left", &left, "right", &right, &index, error, sizeof(error)));
    assert(index.count == 1U);
    assert(index.records != NULL);
    assert(index.records[0].first_node == 1U);
    assert(index.records[0].second_node == 0U);
    assert(index.records[0].lat_e7 == 500000000);
    assert(index.records[0].lon_e7 == 30000000);

    const char *path = "test-routing-gateways.orgateway";
    remove(path);
    assert(openride_routing_gateway_index_save(&index, path, error, sizeof(error)));
    assert(openride_routing_gateway_index_load(&loaded, path, error, sizeof(error)));
    assert(loaded.count == 1U);

    bool reversed = true;
    assert(openride_routing_gateway_index_matches(
        &loaded, "left", &left, "right", &right, &reversed));
    assert(!reversed);

    reversed = false;
    assert(openride_routing_gateway_index_matches(
        &loaded, "right", &right, "left", &left, &reversed));
    assert(reversed);

    char path_a[256];
    char path_b[256];
    assert(openride_routing_gateway_index_pair_path(
        path_a, sizeof(path_a), "routing", "left", "right"));
    assert(openride_routing_gateway_index_pair_path(
        path_b, sizeof(path_b), "routing", "right", "left"));
    assert(strcmp(path_a, path_b) == 0);

    remove(path);
    openride_routing_gateway_index_destroy(&loaded);
    openride_routing_gateway_index_destroy(&index);
    openride_routing_graph_destroy(&left);
    openride_routing_graph_destroy(&right);

    puts("Routing gateway index tests: OK");
    return 0;
}
