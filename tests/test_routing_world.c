#include "openride/routing_world.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

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
    OpenRideRoute route = {0};
    OpenRideRoutingWorldResult result = {0};
    char error[256] = {0};

    assert(openride_routing_world_calculate_graph_pair(
        &left,
        &right,
        50.0000,
        2.9900,
        50.0000,
        3.0100,
        50.0,
        OPENRIDE_ROUTING_PROFILE_FASTEST,
        &route,
        &result,
        error,
        sizeof(error)));

    assert(result.shared_gateway_count >= 1U);
    assert(result.attempted_gateways >= 1U);
    assert(fabs(result.gateway_lon - 3.0000) < 1e-8);
    assert(route.nodes == NULL);
    assert(route.node_count == 0U);
    assert(route.geometry != NULL);
    assert(route.geometry_count >= 3U);
    assert(fabs(route.geometry[0].lon - 2.9900) < 1e-8);
    assert(fabs(route.geometry[route.geometry_count - 1U].lon - 3.0100) < 1e-8);
    assert(route.distance_m > 1999.0 && route.distance_m < 2001.0);

    openride_route_destroy(&route);
    openride_routing_graph_destroy(&left);
    openride_routing_graph_destroy(&right);
    puts("Routing world tests: OK");
    return 0;
}
