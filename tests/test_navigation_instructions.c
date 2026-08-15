#include "openride/navigation_instructions.h"
#include "openride/map_selection.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static OpenRideRoute route_from_points(const OpenRideRoutePoint *points,
                                       uint32_t count)
{
    OpenRideRoute route;
    memset(&route, 0, sizeof(route));
    route.geometry = calloc(count, sizeof(*route.geometry));
    assert(route.geometry != NULL);
    memcpy(route.geometry, points, count * sizeof(*points));
    route.geometry_count = count;
    double distance = 0.0;
    for (uint32_t i = 1U; i < count; ++i) {
        distance += openride_geo_distance_m(points[i - 1U].lat,
                                            points[i - 1U].lon,
                                            points[i].lat,
                                            points[i].lon);
    }
    route.distance_m = distance;
    return route;
}

static void test_right_turn(void)
{
    const OpenRideRoutePoint points[] = {
        {50.0000, 3.0000},
        {50.0010, 3.0000},
        {50.0010, 3.0010}
    };
    OpenRideRoute route = route_from_points(points, 3U);
    OpenRideNavigationInstructionList list = {0};
    char error[128] = {0};
    assert(openride_navigation_instructions_build(NULL,
                                                   &route,
                                                   &list,
                                                   error,
                                                   sizeof(error)));
    assert(list.count == 3U);
    assert(list.items[0].maneuver == OPENRIDE_MANEUVER_DEPART);
    assert(list.items[1].maneuver == OPENRIDE_MANEUVER_RIGHT);
    assert(list.items[2].maneuver == OPENRIDE_MANEUVER_ARRIVE);
    assert(list.items[1].turn_angle_deg > 80.0
           && list.items[1].turn_angle_deg < 100.0);

    double remaining = 0.0;
    const OpenRideNavigationInstruction *next =
        openride_navigation_instructions_next(&list, 0.0, &remaining);
    assert(next == &list.items[1]);
    assert(remaining > 50.0);

    char text[96];
    openride_navigation_instruction_text_fr(next, text, sizeof(text));
    assert(strcmp(text, "Tournez a droite") == 0);

    openride_navigation_instructions_destroy(&list);
    openride_route_destroy(&route);
}

static void test_topology_suppresses_curve_only_turn(void)
{
    const OpenRideRoutePoint points[] = {
        {50.0000, 3.0000},
        {50.0010, 3.0000},
        {50.0010, 3.0010}
    };
    OpenRideRoute route = route_from_points(points, 3U);
    route.navigation_context = calloc(
        route.geometry_count, sizeof(*route.navigation_context));
    assert(route.navigation_context != NULL);
    route.navigation_context_count = route.geometry_count;

    OpenRideNavigationInstructionList list = {0};
    char error[128] = {0};
    assert(openride_navigation_instructions_build(NULL,
                                                   &route,
                                                   &list,
                                                   error,
                                                   sizeof(error)));

    /* A 90 degree road bend without any alternative is not a decision. */
    assert(list.count == 2U);
    assert(list.items[0].maneuver == OPENRIDE_MANEUVER_DEPART);
    assert(list.items[1].maneuver == OPENRIDE_MANEUVER_ARRIVE);

    openride_navigation_instructions_destroy(&list);
    openride_route_destroy(&route);
}

static void test_slight_turn_at_real_choice(void)
{
    const OpenRideRoutePoint points[] = {
        {50.0000, 3.0000},
        {50.0010, 3.0000},
        {50.0016, 3.0010}
    };

    OpenRideRoute route = route_from_points(points, 3U);
    route.navigation_context = calloc(
        route.geometry_count, sizeof(*route.navigation_context));
    assert(route.navigation_context != NULL);
    route.navigation_context_count = route.geometry_count;
    route.navigation_context[1].flags = OPENRIDE_ROUTE_NAV_HAS_ALTERNATIVE;

    OpenRideNavigationInstructionList list = {0};
    char error[128] = {0};
    assert(openride_navigation_instructions_build(NULL,
                                                   &route,
                                                   &list,
                                                   error,
                                                   sizeof(error)));
    assert(list.count == 3U);
    assert(list.items[1].maneuver == OPENRIDE_MANEUVER_SLIGHT_RIGHT);
    assert(list.items[1].turn_angle_deg > 45.0
           && list.items[1].turn_angle_deg < 55.0);

    openride_navigation_instructions_destroy(&list);
    openride_route_destroy(&route);

    /* Geometry-only routes keep a useful fallback at the same angle. */
    route = route_from_points(points, 3U);
    memset(&list, 0, sizeof(list));
    memset(error, 0, sizeof(error));
    assert(openride_navigation_instructions_build(NULL,
                                                   &route,
                                                   &list,
                                                   error,
                                                   sizeof(error)));
    assert(list.count == 3U);
    assert(list.items[1].maneuver == OPENRIDE_MANEUVER_SLIGHT_RIGHT);

    openride_navigation_instructions_destroy(&list);
    openride_route_destroy(&route);
}

static OpenRideRoutingGraph build_roundabout_graph(void)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder = openride_routing_graph_builder_create();
    assert(builder != NULL);

    const OpenRideRoutingNodeId n0 = openride_routing_graph_builder_add_node(builder, 50.0000, 3.0000);
    const OpenRideRoutingNodeId n1 = openride_routing_graph_builder_add_node(builder, 50.0010, 3.0000);
    const OpenRideRoutingNodeId n2 = openride_routing_graph_builder_add_node(builder, 50.0015, 3.0005);
    const OpenRideRoutingNodeId n3 = openride_routing_graph_builder_add_node(builder, 50.0010, 3.0010);
    const OpenRideRoutingNodeId n4 = openride_routing_graph_builder_add_node(builder, 50.0000, 3.0010);
    const OpenRideRoutingNodeId exit1 = openride_routing_graph_builder_add_node(builder, 50.0022, 3.0005);
    assert(n0 == 0U && n1 == 1U && n2 == 2U && n3 == 3U && n4 == 4U && exit1 == 5U);

    OpenRideRoutingEdgeAttributes normal = openride_routing_edge_attributes_default();
    OpenRideRoutingEdgeAttributes roundabout = normal;
    roundabout.flags |= OPENRIDE_EDGE_FLAG_ROUNDABOUT;

    assert(openride_routing_graph_builder_add_directed_edge(builder, n0, n1, &normal));
    assert(openride_routing_graph_builder_add_directed_edge(builder, n1, n2, &roundabout));
    assert(openride_routing_graph_builder_add_directed_edge(builder, n2, n3, &roundabout));
    assert(openride_routing_graph_builder_add_directed_edge(builder, n2, exit1, &normal));
    assert(openride_routing_graph_builder_add_directed_edge(builder, n3, n4, &normal));

    char error[128] = {0};
    assert(openride_routing_graph_builder_build(builder, &graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);
    return graph;
}

static void test_roundabout_exit_number(void)
{
    OpenRideRoutingGraph graph = build_roundabout_graph();
    OpenRideRoute route;
    memset(&route, 0, sizeof(route));
    route.node_count = 5U;
    route.nodes = calloc(route.node_count, sizeof(*route.nodes));
    route.geometry_count = route.node_count;
    route.geometry = calloc(route.geometry_count, sizeof(*route.geometry));
    assert(route.nodes && route.geometry);

    for (uint32_t i = 0U; i < route.node_count; ++i) {
        route.nodes[i] = i;
        openride_routing_node_geo(&graph.nodes[i],
                                  &route.geometry[i].lat,
                                  &route.geometry[i].lon);
        if (i > 0U) {
            route.distance_m += openride_geo_distance_m(route.geometry[i - 1U].lat,
                                                        route.geometry[i - 1U].lon,
                                                        route.geometry[i].lat,
                                                        route.geometry[i].lon);
        }
    }

    OpenRideNavigationInstructionList list = {0};
    char error[128] = {0};
    assert(openride_navigation_instructions_build(&graph,
                                                   &route,
                                                   &list,
                                                   error,
                                                   sizeof(error)));

    assert(list.count == 3U);
    assert(list.items[0].maneuver == OPENRIDE_MANEUVER_DEPART);
    assert(list.items[1].maneuver == OPENRIDE_MANEUVER_ROUNDABOUT);
    assert(list.items[1].roundabout_exit_number == 2U);
    assert(list.items[2].maneuver == OPENRIDE_MANEUVER_ARRIVE);

    openride_navigation_instructions_destroy(&list);
    openride_route_destroy(&route);
    openride_routing_graph_destroy(&graph);
}

static void test_continue_grouping(void)
{
    const OpenRideRoutePoint points[] = {
        {50.0000, 3.0000},
        {50.0005, 3.0000},
        {50.0010, 3.0000},
        {50.0015, 3.0000},
        {50.0060, 3.0000},
        {50.0065, 3.0000}
    };
    OpenRideRoute route = route_from_points(points, 6U);
    route.navigation_context = calloc(
        route.geometry_count, sizeof(*route.navigation_context));
    assert(route.navigation_context != NULL);
    route.navigation_context_count = route.geometry_count;

    route.navigation_context[1].flags = OPENRIDE_ROUTE_NAV_HAS_ALTERNATIVE;
    route.navigation_context[2].flags = OPENRIDE_ROUTE_NAV_HAS_ALTERNATIVE;
    route.navigation_context[3].flags = OPENRIDE_ROUTE_NAV_HAS_ALTERNATIVE;
    route.navigation_context[4].flags = OPENRIDE_ROUTE_NAV_HAS_ALTERNATIVE;

    OpenRideNavigationInstructionList list = {0};
    char error[128] = {0};
    assert(openride_navigation_instructions_build(NULL,
                                                   &route,
                                                   &list,
                                                   error,
                                                   sizeof(error)));

    assert(list.count == 4U);
    assert(list.items[0].maneuver == OPENRIDE_MANEUVER_DEPART);
    assert(list.items[1].maneuver == OPENRIDE_MANEUVER_CONTINUE);
    assert(list.items[1].geometry_index == 3U);
    assert(list.items[2].maneuver == OPENRIDE_MANEUVER_CONTINUE);
    assert(list.items[2].geometry_index == 4U);
    assert(list.items[2].distance_from_start_m
           - list.items[1].distance_from_start_m > 300.0);
    assert(list.items[3].maneuver == OPENRIDE_MANEUVER_ARRIVE);

    openride_navigation_instructions_destroy(&list);
    openride_route_destroy(&route);
}

static void test_close_maneuver_handoff(void)
{
    OpenRideNavigationInstruction items[4];
    memset(items, 0, sizeof(items));

    items[0].maneuver = OPENRIDE_MANEUVER_DEPART;
    items[0].distance_from_start_m = 0.0;
    items[1].maneuver = OPENRIDE_MANEUVER_RIGHT;
    items[1].distance_from_start_m = 100.0;
    items[2].maneuver = OPENRIDE_MANEUVER_LEFT;
    items[2].distance_from_start_m = 135.0;
    items[3].maneuver = OPENRIDE_MANEUVER_ARRIVE;
    items[3].distance_from_start_m = 1000.0;

    OpenRideNavigationInstructionList list = {
        .items = items,
        .count = 4U,
        .route_distance_m = 1000.0
    };

    double distance_m = 0.0;
    const OpenRideNavigationInstruction *next =
        openride_navigation_instructions_next(&list, 102.0, &distance_m);
    assert(next == &items[1]);
    assert(fabs(distance_m) < 1e-9);

    next = openride_navigation_instructions_next(&list, 103.0, &distance_m);
    assert(next == &items[2]);
    assert(distance_m > 30.0 && distance_m < 35.0);
}

static void test_following_instruction(void)
{
    OpenRideNavigationInstruction items[4];
    memset(items, 0, sizeof(items));

    items[0].maneuver = OPENRIDE_MANEUVER_DEPART;
    items[0].distance_from_start_m = 0.0;
    items[1].maneuver = OPENRIDE_MANEUVER_RIGHT;
    items[1].distance_from_start_m = 100.0;
    items[2].maneuver = OPENRIDE_MANEUVER_LEFT;
    items[2].distance_from_start_m = 250.0;
    items[3].maneuver = OPENRIDE_MANEUVER_ARRIVE;
    items[3].distance_from_start_m = 1000.0;

    OpenRideNavigationInstructionList list = {
        .items = items,
        .count = 4U,
        .route_distance_m = 1000.0
    };

    double gap = 0.0;
    const OpenRideNavigationInstruction *following =
        openride_navigation_instructions_after(&list, 100.0, &gap);
    assert(following == &items[2]);
    assert(fabs(gap - 150.0) < 1e-9);

    following = openride_navigation_instructions_after(&list, 250.0, &gap);
    assert(following == &items[3]);
    assert(fabs(gap - 750.0) < 1e-9);

    following = openride_navigation_instructions_after(&list, 1000.0, &gap);
    assert(following == NULL);
}

static void test_distance_format(void)
{
    char text[32];
    openride_navigation_distance_text_fr(347.0, text, sizeof(text));
    assert(strcmp(text, "350 m") == 0);
    openride_navigation_distance_text_fr(2350.0, text, sizeof(text));
    assert(strcmp(text, "2.4 km") == 0);
}

int main(void)
{
    test_right_turn();
    test_topology_suppresses_curve_only_turn();
    test_slight_turn_at_real_choice();
    test_roundabout_exit_number();
    test_continue_grouping();
    test_close_maneuver_handoff();
    test_following_instruction();
    test_distance_format();
    puts("Navigation instructions tests: OK");
    return 0;
}
