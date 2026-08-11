#include "openride/routing_engine.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static OpenRideRoutingGraph build_profile_fixture(void)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder = openride_routing_graph_builder_create();
    assert(builder != NULL);

    /*
     *  A -- motorway -- B -- motorway -- D
     *   \                                /
     *    \-- gravel track -- C --------/
     *
     * The upper route is faster. The lower route is preferred by TRAIL.
     */
    const OpenRideRoutingNodeId a = openride_routing_graph_builder_add_node(builder, 50.3700, 3.0800);
    const OpenRideRoutingNodeId b = openride_routing_graph_builder_add_node(builder, 50.3700, 3.0900);
    const OpenRideRoutingNodeId c = openride_routing_graph_builder_add_node(builder, 50.3650, 3.0850);
    const OpenRideRoutingNodeId d = openride_routing_graph_builder_add_node(builder, 50.3700, 3.1000);
    assert(a == 0U && b == 1U && c == 2U && d == 3U);

    OpenRideRoutingEdgeAttributes motorway = openride_routing_edge_attributes_default();
    motorway.length_m = 1000.0;
    motorway.road_class = OPENRIDE_ROAD_MOTORWAY;
    motorway.surface = OPENRIDE_SURFACE_ASPHALT;
    motorway.max_speed_kph = 100U;

    OpenRideRoutingEdgeAttributes track = openride_routing_edge_attributes_default();
    track.length_m = 1200.0;
    track.road_class = OPENRIDE_ROAD_TRACK;
    track.surface = OPENRIDE_SURFACE_GRAVEL;
    track.flags = OPENRIDE_EDGE_FLAG_UNPAVED;
    track.max_speed_kph = 50U;

    assert(openride_routing_graph_builder_add_bidirectional_edge(builder, a, b, &motorway));
    assert(openride_routing_graph_builder_add_bidirectional_edge(builder, b, d, &motorway));
    assert(openride_routing_graph_builder_add_bidirectional_edge(builder, a, c, &track));
    assert(openride_routing_graph_builder_add_bidirectional_edge(builder, c, d, &track));

    char error[256] = {0};
    assert(openride_routing_graph_builder_build(builder, &graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);
    return graph;
}

static void test_fastest_profile(void)
{
    OpenRideRoutingGraph graph = build_profile_fixture();
    OpenRideRoutingRequest request = openride_routing_request_default();
    OpenRideRoute route = {0};
    char error[256] = {0};

    request.start = 0U;
    request.destination = 3U;
    request.profile = OPENRIDE_ROUTING_PROFILE_FASTEST;

    assert(openride_routing_engine_calculate(&graph, &request, &route, error, sizeof(error)));
    assert(route.node_count == 3U);
    assert(route.nodes[0] == 0U);
    assert(route.nodes[1] == 1U);
    assert(route.nodes[2] == 3U);
    assert(fabs(route.distance_m - 2000.0) < 0.1);
    assert(route.estimated_time_s > 70.0 && route.estimated_time_s < 73.0);

    openride_route_destroy(&route);
    openride_routing_graph_destroy(&graph);
}

static void test_trail_profile(void)
{
    OpenRideRoutingGraph graph = build_profile_fixture();
    OpenRideRoutingRequest request = openride_routing_request_default();
    OpenRideRoute route = {0};
    char error[256] = {0};

    request.start = 0U;
    request.destination = 3U;
    request.profile = OPENRIDE_ROUTING_PROFILE_TRAIL;

    assert(openride_routing_engine_calculate(&graph, &request, &route, error, sizeof(error)));
    assert(route.node_count == 3U);
    assert(route.nodes[0] == 0U);
    assert(route.nodes[1] == 2U);
    assert(route.nodes[2] == 3U);
    assert(fabs(route.distance_m - 2400.0) < 0.1);

    openride_route_destroy(&route);
    openride_routing_graph_destroy(&graph);
}

static void test_toll_avoidance(void)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder = openride_routing_graph_builder_create();
    assert(builder != NULL);

    const OpenRideRoutingNodeId a = openride_routing_graph_builder_add_node(builder, 50.0, 3.0);
    const OpenRideRoutingNodeId b = openride_routing_graph_builder_add_node(builder, 50.0, 3.01);
    const OpenRideRoutingNodeId c = openride_routing_graph_builder_add_node(builder, 50.01, 3.0);
    const OpenRideRoutingNodeId d = openride_routing_graph_builder_add_node(builder, 50.01, 3.01);

    OpenRideRoutingEdgeAttributes toll = openride_routing_edge_attributes_default();
    toll.length_m = 500.0;
    toll.road_class = OPENRIDE_ROAD_PRIMARY;
    toll.surface = OPENRIDE_SURFACE_ASPHALT;
    toll.max_speed_kph = 80U;
    toll.flags = OPENRIDE_EDGE_FLAG_TOLL;

    OpenRideRoutingEdgeAttributes free_road = toll;
    free_road.length_m = 800.0;
    free_road.flags = OPENRIDE_EDGE_FLAG_NONE;

    assert(openride_routing_graph_builder_add_directed_edge(builder, a, b, &toll));
    assert(openride_routing_graph_builder_add_directed_edge(builder, b, d, &toll));
    assert(openride_routing_graph_builder_add_directed_edge(builder, a, c, &free_road));
    assert(openride_routing_graph_builder_add_directed_edge(builder, c, d, &free_road));

    char error[256] = {0};
    assert(openride_routing_graph_builder_build(builder, &graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);

    OpenRideRoutingRequest request = openride_routing_request_default();
    OpenRideRoute route = {0};
    request.start = a;
    request.destination = d;
    request.avoid_tolls = true;

    assert(openride_routing_engine_calculate(&graph, &request, &route, error, sizeof(error)));
    assert(route.node_count == 3U);
    assert(route.nodes[1] == c);

    openride_route_destroy(&route);
    openride_routing_graph_destroy(&graph);
}


static void test_snapped_routing(void)
{
    OpenRideRoutingGraph graph = build_profile_fixture();
    OpenRideRoutingSnap start = {0};
    OpenRideRoutingSnap destination = {0};
    OpenRideRoute route = {0};
    char error[256] = {0};

    assert(openride_routing_graph_snap_to_segment(
        &graph, 50.3700, 3.0825, 20.0, &start));
    assert(openride_routing_graph_snap_to_segment(
        &graph, 50.3700, 3.0950, 20.0, &destination));

    OpenRideSnappedRoutingRequest request = openride_snapped_routing_request_default();
    request.start = start;
    request.destination = destination;
    request.profile = OPENRIDE_ROUTING_PROFILE_FASTEST;

    assert(openride_routing_engine_calculate_snapped(
        &graph, &request, &route, error, sizeof(error)));
    assert(route.geometry_count >= 2U);
    assert(fabs(route.geometry[0].lon - start.lon) < 1e-8);
    assert(fabs(route.geometry[route.geometry_count - 1U].lon - destination.lon) < 1e-8);
    assert(route.distance_m > 1200.0 && route.distance_m < 1300.0);

    openride_route_destroy(&route);
    openride_routing_graph_destroy(&graph);
}

static void test_snapped_one_way(void)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder = openride_routing_graph_builder_create();
    assert(builder != NULL);
    const OpenRideRoutingNodeId a = openride_routing_graph_builder_add_node(builder, 50.0, 3.0);
    const OpenRideRoutingNodeId b = openride_routing_graph_builder_add_node(builder, 50.0, 3.01);
    OpenRideRoutingEdgeAttributes road = openride_routing_edge_attributes_default();
    road.length_m = 1000.0;
    road.max_speed_kph = 50U;
    assert(openride_routing_graph_builder_add_directed_edge(builder, a, b, &road));
    char error[256] = {0};
    assert(openride_routing_graph_builder_build(builder, &graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);

    OpenRideRoutingSnap s1 = {0};
    OpenRideRoutingSnap s2 = {0};
    assert(openride_routing_graph_snap_to_segment(&graph, 50.0, 3.002, 20.0, &s1));
    assert(openride_routing_graph_snap_to_segment(&graph, 50.0, 3.008, 20.0, &s2));

    OpenRideSnappedRoutingRequest request = openride_snapped_routing_request_default();
    OpenRideRoute route = {0};
    request.start = s1;
    request.destination = s2;
    assert(openride_routing_engine_calculate_snapped(
        &graph, &request, &route, error, sizeof(error)));
    assert(route.distance_m > 590.0 && route.distance_m < 610.0);
    openride_route_destroy(&route);

    request.start = s2;
    request.destination = s1;
    assert(!openride_routing_engine_calculate_snapped(
        &graph, &request, &route, error, sizeof(error)));

    openride_routing_graph_destroy(&graph);
}

static void test_no_route_and_same_node(void)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder = openride_routing_graph_builder_create();
    assert(builder != NULL);
    const OpenRideRoutingNodeId a = openride_routing_graph_builder_add_node(builder, 50.0, 3.0);
    const OpenRideRoutingNodeId b = openride_routing_graph_builder_add_node(builder, 50.1, 3.1);
    char error[256] = {0};
    assert(openride_routing_graph_builder_build(builder, &graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);

    OpenRideRoutingRequest request = openride_routing_request_default();
    OpenRideRoute route = {0};
    request.start = a;
    request.destination = a;
    assert(openride_routing_engine_calculate(&graph, &request, &route, error, sizeof(error)));
    assert(route.node_count == 1U && route.nodes[0] == a);
    openride_route_destroy(&route);

    request.destination = b;
    assert(!openride_routing_engine_calculate(&graph, &request, &route, error, sizeof(error)));
    assert(strstr(error, "no route") != NULL);

    openride_routing_graph_destroy(&graph);
}

int main(void)
{
    test_fastest_profile();
    test_trail_profile();
    test_toll_avoidance();
    test_snapped_routing();
    test_snapped_one_way();
    test_no_route_and_same_node();
    puts("Routing engine tests: OK");
    return 0;
}
