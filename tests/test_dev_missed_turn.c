#include "openride/dev_missed_turn.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static OpenRideRoutingGraph build_fixture(void)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder =
        openride_routing_graph_builder_create();
    assert(builder != NULL);

    /*
     *                         N ---- D
     *                         |
     * A ---- J                |
     *         \
     *          E ---- F ---- G
     *
     * Official route: A -> J -> N -> D
     * Missed turn:    J -> E -> F -> G
     */
    const OpenRideRoutingNodeId a =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0000);
    const OpenRideRoutingNodeId j =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0020);
    const OpenRideRoutingNodeId n =
        openride_routing_graph_builder_add_node(builder, 50.0020, 3.0020);
    const OpenRideRoutingNodeId d =
        openride_routing_graph_builder_add_node(builder, 50.0040, 3.0020);
    const OpenRideRoutingNodeId e =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0040);
    const OpenRideRoutingNodeId f =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0060);
    const OpenRideRoutingNodeId g =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0080);

    OpenRideRoutingEdgeAttributes road =
        openride_routing_edge_attributes_default();
    road.length_m = 150.0;
    road.road_class = OPENRIDE_ROAD_RESIDENTIAL;
    road.surface = OPENRIDE_SURFACE_ASPHALT;
    road.max_speed_kph = 50U;

    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, a, j, &road));
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, j, n, &road));
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, n, d, &road));
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, j, e, &road));
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, e, f, &road));
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, f, g, &road));

    char error[192] = {0};
    assert(openride_routing_graph_builder_build(
        builder, &graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);
    return graph;
}

int main(void)
{
    OpenRideRoutingGraph graph = build_fixture();

    OpenRideRoutingRequest request = openride_routing_request_default();
    request.start = 0U;
    request.destination = 3U;
    request.profile = OPENRIDE_ROUTING_PROFILE_FASTEST;

    OpenRideRoute planned = {0};
    char error[192] = {0};
    assert(openride_routing_engine_calculate(
        &graph, &request, &planned, error, sizeof(error)));
    assert(planned.node_count == 4U);
    assert(planned.nodes[0] == 0U);
    assert(planned.nodes[1] == 1U);
    assert(planned.nodes[2] == 2U);
    assert(planned.nodes[3] == 3U);

    OpenRideDevMissedTurnPlan plan;
    openride_dev_missed_turn_plan_init(&plan);

    assert(openride_dev_missed_turn_plan_build(
        &graph,
        &planned,
        0.0,
        50.0,
        1000.0,
        80.0,
        &plan,
        error,
        sizeof(error)));

    assert(plan.route_node_index == 1U);
    assert(plan.trigger_position_m > 100.0);

    /*
     * The planner deliberately stops extending the wrong branch as soon as
     * it has enough real-road geometry and enough separation from the planned
     * route for NavigationSession to confirm OFF_ROUTE. Do not require it to
     * consume every available fixture node.
     */
    assert(plan.branch_route.geometry_count >= 3U);
    assert(plan.branch_route.distance_m > 250.0);
    assert(plan.max_distance_from_route_m > 200.0);

    /* The DEV branch is geometry-only and cannot alter the planned route. */
    assert(plan.branch_route.nodes == NULL);
    assert(planned.nodes[1] == 1U);
    assert(planned.nodes[2] == 2U);

    openride_dev_missed_turn_plan_destroy(&plan);

    OpenRideRoute geometry_only = planned;
    geometry_only.nodes = NULL;
    geometry_only.node_count = 0U;
    assert(!openride_dev_missed_turn_plan_build(
        &graph,
        &geometry_only,
        0.0,
        50.0,
        1000.0,
        80.0,
        &plan,
        error,
        sizeof(error)));

    openride_route_destroy(&planned);
    openride_routing_graph_destroy(&graph);

    puts("Developer missed-turn tests: OK");
    return 0;
}
