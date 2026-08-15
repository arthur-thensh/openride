#include "navigation_scenario.h"
#include "openride/map_selection.h"
#include "openride/routing_world.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static OpenRideRoute make_reference_route(void)
{
    static const OpenRideRoutePoint points[] = {
        {50.0000, 3.0000},
        {50.0000, 3.0100},
        {50.0060, 3.0100},
        {50.0060, 3.0180}
    };

    OpenRideRoute route = {0};
    route.geometry_count = (uint32_t)(sizeof(points) / sizeof(points[0]));
    route.geometry = calloc(route.geometry_count, sizeof(*route.geometry));
    assert(route.geometry != NULL);

    for (uint32_t i = 0U; i < route.geometry_count; ++i) {
        route.geometry[i] = points[i];
        if (i > 0U) {
            route.distance_m += openride_geo_distance_m(
                route.geometry[i - 1U].lat,
                route.geometry[i - 1U].lon,
                route.geometry[i].lat,
                route.geometry[i].lon);
        }
    }
    return route;
}

static void run_nominal(const OpenRideRoute *route)
{
    const OpenRideNavigationScenarioWaypoint track[] = {
        {"depart",      50.0000, 3.0000, 50.0},
        {"premier axe", 50.0000, 3.0100, 50.0},
        {"virage nord", 50.0060, 3.0100, 40.0},
        {"arrivee",     50.0060, 3.0180, 20.0}
    };

    OpenRideNavigationScenarioResult result;
    char error[160] = {0};
    assert(openride_test_navigation_scenario_run(
        "trajet nominal", route, track,
        (uint32_t)(sizeof(track) / sizeof(track[0])),
        10.0, true, &result, error, sizeof(error)));

    assert(!result.saw_off_route);
    assert(!result.reroute_requested);
    assert(result.saw_arrival);
    assert(result.final_remaining_m < 1.0);
    assert(result.final_progress_ratio > 0.999);
    assert(result.max_distance_from_route_m < 1.0);
}

static void run_noisy_gps(const OpenRideRoute *route)
{
    const OpenRideNavigationScenarioWaypoint track[] = {
        {"depart",         50.00000, 3.00000, 50.0},
        {"bruit GPS 1",    50.00007, 3.00400, 50.0},
        {"bruit GPS 2",    49.99994, 3.00950, 45.0},
        {"branche nord",   50.00300, 3.01008, 45.0},
        {"avant virage",   50.00580, 3.00994, 40.0},
        {"dernier axe",    50.00603, 3.01400, 35.0},
        {"arrivee exacte", 50.00600, 3.01800, 20.0}
    };

    OpenRideNavigationScenarioResult result;
    char error[160] = {0};
    assert(openride_test_navigation_scenario_run(
        "GPS bruite mais acceptable", route, track,
        (uint32_t)(sizeof(track) / sizeof(track[0])),
        8.0, true, &result, error, sizeof(error)));

    assert(!result.saw_off_route);
    assert(!result.reroute_requested);
    assert(result.saw_arrival);
    assert(result.max_distance_from_route_m > 5.0);
    assert(result.max_distance_from_route_m < 40.0);
}

static void run_missed_turn_and_return(const OpenRideRoute *route)
{
    const OpenRideNavigationScenarioWaypoint track[] = {
        {"depart",              50.0000, 3.0000, 50.0},
        {"virage rate",         50.0000, 3.0100, 50.0},
        {"mauvaise direction",  50.0000, 3.0145, 45.0},
        {"retour au carrefour", 50.0000, 3.0100, 35.0},
        {"route retrouvee",     50.0060, 3.0100, 40.0},
        {"arrivee",             50.0060, 3.0180, 20.0}
    };

    OpenRideNavigationScenarioResult result;
    char error[160] = {0};
    assert(openride_test_navigation_scenario_run(
        "virage rate puis retour", route, track,
        (uint32_t)(sizeof(track) / sizeof(track[0])),
        10.0, true, &result, error, sizeof(error)));

    assert(result.saw_off_route);
    assert(result.saw_return_to_route);
    assert(result.reroute_requested);
    assert(result.saw_arrival);
    assert(result.max_distance_from_route_m > 200.0);
    assert(result.final_progress_ratio > 0.999);
}


static OpenRideRoutingGraph build_instruction_region_left(void)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder =
        openride_routing_graph_builder_create();
    assert(builder != NULL);

    const OpenRideRoutingNodeId start =
        openride_routing_graph_builder_add_node(builder, 50.0000, 2.9980);
    const OpenRideRoutingNodeId corner =
        openride_routing_graph_builder_add_node(builder, 50.0010, 2.9980);
    const OpenRideRoutingNodeId gateway =
        openride_routing_graph_builder_add_node(builder, 50.0010, 3.0000);

    OpenRideRoutingEdgeAttributes road =
        openride_routing_edge_attributes_default();
    road.length_m = 100.0;
    road.road_class = OPENRIDE_ROAD_PRIMARY;
    road.surface = OPENRIDE_SURFACE_ASPHALT;
    road.max_speed_kph = 50U;

    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, start, corner, &road));
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, corner, gateway, &road));

    char error[160] = {0};
    assert(openride_routing_graph_builder_build(
        builder, &graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);
    return graph;
}

static OpenRideRoutingGraph build_instruction_region_right(void)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder =
        openride_routing_graph_builder_create();
    assert(builder != NULL);

    const OpenRideRoutingNodeId gateway =
        openride_routing_graph_builder_add_node(builder, 50.0010, 3.0000);
    const OpenRideRoutingNodeId entry =
        openride_routing_graph_builder_add_node(builder, 50.0010, 3.0010);
    const OpenRideRoutingNodeId roundabout_a =
        openride_routing_graph_builder_add_node(builder, 50.0015, 3.0015);
    const OpenRideRoutingNodeId roundabout_b =
        openride_routing_graph_builder_add_node(builder, 50.0010, 3.0020);
    const OpenRideRoutingNodeId destination =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0020);
    const OpenRideRoutingNodeId first_exit =
        openride_routing_graph_builder_add_node(builder, 50.0022, 3.0015);

    OpenRideRoutingEdgeAttributes normal =
        openride_routing_edge_attributes_default();
    normal.length_m = 100.0;
    normal.road_class = OPENRIDE_ROAD_PRIMARY;
    normal.surface = OPENRIDE_SURFACE_ASPHALT;
    normal.max_speed_kph = 50U;

    OpenRideRoutingEdgeAttributes roundabout = normal;
    roundabout.flags |= OPENRIDE_EDGE_FLAG_ROUNDABOUT;

    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, gateway, entry, &normal));
    assert(openride_routing_graph_builder_add_directed_edge(
        builder, entry, roundabout_a, &roundabout));
    assert(openride_routing_graph_builder_add_directed_edge(
        builder, roundabout_a, roundabout_b, &roundabout));
    assert(openride_routing_graph_builder_add_directed_edge(
        builder, roundabout_a, first_exit, &normal));
    assert(openride_routing_graph_builder_add_directed_edge(
        builder, roundabout_b, destination, &normal));

    char error[160] = {0};
    assert(openride_routing_graph_builder_build(
        builder, &graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);
    return graph;
}

static void run_multi_region_instruction_progression(void)
{
    OpenRideRoutingGraph left = build_instruction_region_left();
    OpenRideRoutingGraph right = build_instruction_region_right();
    OpenRideRoute route = {0};
    OpenRideRoutingWorldResult world = {0};
    char error[160] = {0};

    assert(openride_routing_world_calculate_graph_pair(
        &left,
        &right,
        50.0000,
        2.9980,
        50.0000,
        3.0020,
        50.0,
        OPENRIDE_ROUTING_PROFILE_FASTEST,
        &route,
        &world,
        error,
        sizeof(error)));

    assert(route.nodes == NULL);
    assert(route.navigation_context != NULL);
    assert(route.navigation_context_count == route.geometry_count);

    uint32_t gateway_geometry_index = UINT32_MAX;
    for (uint32_t i = 0U; i < route.geometry_count; ++i) {
        if (fabs(route.geometry[i].lat - 50.0010) < 1e-8
            && fabs(route.geometry[i].lon - 3.0000) < 1e-8) {
            gateway_geometry_index = i;
            break;
        }
    }
    assert(gateway_geometry_index != UINT32_MAX);

    OpenRideNavigationScenarioWaypoint *track =
        calloc(route.geometry_count, sizeof(*track));
    assert(track != NULL);

    for (uint32_t i = 0U; i < route.geometry_count; ++i) {
        track[i].label = NULL;
        track[i].lat = route.geometry[i].lat;
        track[i].lon = route.geometry[i].lon;
        track[i].speed_kph = 35.0;
    }

    OpenRideNavigationScenarioResult result;
    assert(openride_test_navigation_scenario_run(
        "instructions multi-region en mouvement",
        &route,
        track,
        route.geometry_count,
        5.0,
        true,
        &result,
        error,
        sizeof(error)));

    assert(!result.instruction_event_overflow);
    assert(result.instruction_event_count == 2U);
    assert(result.instruction_events[0].maneuver == OPENRIDE_MANEUVER_ROUNDABOUT);
    assert(result.instruction_events[0].roundabout_exit_number == 2U);
    assert(result.instruction_events[1].maneuver == OPENRIDE_MANEUVER_ARRIVE);
    assert(result.saw_arrival);

    bool saw_turn_before_boundary = false;
    bool saw_roundabout_after_boundary = false;
    bool saw_arrive_instruction = false;
    uint32_t previous_geometry_index = 0U;

    for (uint32_t i = 0U; i < result.instruction_event_count; ++i) {
        const OpenRideNavigationScenarioInstructionEvent *event =
            &result.instruction_events[i];

        if (i > 0U) {
            assert(event->geometry_index >= previous_geometry_index);
        }
        previous_geometry_index = event->geometry_index;

        if ((event->maneuver == OPENRIDE_MANEUVER_SLIGHT_RIGHT
             || event->maneuver == OPENRIDE_MANEUVER_RIGHT
             || event->maneuver == OPENRIDE_MANEUVER_SHARP_RIGHT
             || event->maneuver == OPENRIDE_MANEUVER_SLIGHT_LEFT
             || event->maneuver == OPENRIDE_MANEUVER_LEFT
             || event->maneuver == OPENRIDE_MANEUVER_SHARP_LEFT)
            && event->geometry_index < gateway_geometry_index) {
            saw_turn_before_boundary = true;
        }

        if (event->maneuver == OPENRIDE_MANEUVER_ROUNDABOUT) {
            assert(event->geometry_index > gateway_geometry_index);
            assert(event->roundabout_exit_number == 2U);
            saw_roundabout_after_boundary = true;
        }

        if (event->maneuver == OPENRIDE_MANEUVER_ARRIVE) {
            saw_arrive_instruction = true;
        }
    }

    assert(!saw_turn_before_boundary);
    assert(saw_roundabout_after_boundary);
    assert(saw_arrive_instruction);

    free(track);
    openride_route_destroy(&route);
    openride_routing_graph_destroy(&left);
    openride_routing_graph_destroy(&right);
}


typedef struct RealRerouteFixture {
    const OpenRideRoutingGraph *graph;
    OpenRideRoutingNodeId destination;
    uint32_t callback_count;
} RealRerouteFixture;

static OpenRideRoutingGraph build_real_reroute_graph(
    OpenRideRoutingNodeId *start_out,
    OpenRideRoutingNodeId *destination_out)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder =
        openride_routing_graph_builder_create();
    assert(builder != NULL);

    const OpenRideRoutingNodeId start =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0000);
    const OpenRideRoutingNodeId junction =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0060);
    const OpenRideRoutingNodeId north =
        openride_routing_graph_builder_add_node(builder, 50.0060, 3.0060);
    const OpenRideRoutingNodeId destination =
        openride_routing_graph_builder_add_node(builder, 50.0060, 3.0120);

    const OpenRideRoutingNodeId wrong0 =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0072);
    const OpenRideRoutingNodeId wrong1 =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0100);
    const OpenRideRoutingNodeId wrong2 =
        openride_routing_graph_builder_add_node(builder, 50.0020, 3.0120);
    const OpenRideRoutingNodeId alternate =
        openride_routing_graph_builder_add_node(builder, 50.0040, 3.0120);

    OpenRideRoutingEdgeAttributes road =
        openride_routing_edge_attributes_default();
    road.road_class = OPENRIDE_ROAD_PRIMARY;
    road.surface = OPENRIDE_SURFACE_ASPHALT;
    road.max_speed_kph = 50U;

    road.length_m = 300.0;
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, start, junction, &road));

    road.length_m = 350.0;
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, junction, north, &road));
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, north, destination, &road));

    /*
     * Deterministic costs for the reroute fixture:
     *
     *   initial normal branch from junction = 350 + 350 = 700 m
     *   initial wrong branch from junction  = 250 + 4 * 150 = 850 m
     *
     * Therefore the original route selects the normal branch.
     *
     * Once the rider has reached wrong0:
     *
     *   continue forward to destination = 4 * 150 = 600 m
     *   backtrack via junction          = 250 + 700 = 950 m
     *
     * Therefore the real reroute must follow the branch actually taken by the
     * simulated GPS instead of asking the rider to turn around.
     */
    road.length_m = 250.0;
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, junction, wrong0, &road));

    road.length_m = 150.0;
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, wrong0, wrong1, &road));
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, wrong1, wrong2, &road));
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, wrong2, alternate, &road));
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, alternate, destination, &road));

    char error[160] = {0};
    assert(openride_routing_graph_builder_build(
        builder, &graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);

    *start_out = start;
    *destination_out = destination;
    return graph;
}

static bool calculate_real_reroute(void *userdata,
                                   double lat,
                                   double lon,
                                   OpenRideRoute *route,
                                   char *error,
                                   size_t error_size)
{
    RealRerouteFixture *fixture = userdata;
    assert(fixture != NULL);
    assert(fixture->graph != NULL);
    ++fixture->callback_count;

    double snap_distance_m = INFINITY;
    const OpenRideRoutingNodeId start =
        openride_routing_graph_nearest_node(
            fixture->graph, lat, lon, &snap_distance_m);
    if (start == OPENRIDE_ROUTING_NODE_NONE || snap_distance_m > 150.0) {
        snprintf(error,
                 error_size,
                 "reroute GPS is too far from synthetic graph");
        return false;
    }

    OpenRideRoutingRequest request = openride_routing_request_default();
    request.start = start;
    request.destination = fixture->destination;
    request.profile = OPENRIDE_ROUTING_PROFILE_FASTEST;
    return openride_routing_engine_calculate(
        fixture->graph, &request, route, error, error_size);
}

static void run_real_reroute_after_missed_turn(void)
{
    OpenRideRoutingNodeId start = OPENRIDE_ROUTING_NODE_NONE;
    OpenRideRoutingNodeId destination = OPENRIDE_ROUTING_NODE_NONE;
    OpenRideRoutingGraph graph =
        build_real_reroute_graph(&start, &destination);

    OpenRideRoutingRequest request = openride_routing_request_default();
    request.start = start;
    request.destination = destination;
    request.profile = OPENRIDE_ROUTING_PROFILE_FASTEST;

    OpenRideRoute initial_route = {0};
    char error[160] = {0};
    assert(openride_routing_engine_calculate(
        &graph, &request, &initial_route, error, sizeof(error)));

    /*
     * Initial shortest route must be:
     * start -> junction -> north -> destination.
     */
    assert(initial_route.node_count == 4U);
    assert(initial_route.nodes[0] == start);
    assert(initial_route.nodes[1] == 1U);
    assert(initial_route.nodes[2] == 2U);
    assert(initial_route.nodes[3] == destination);

    const OpenRideNavigationScenarioWaypoint track[] = {
        {"depart",                 50.0000, 3.0000, 45.0},
        {"carrefour",              50.0000, 3.0060, 45.0},
        {"mauvaise branche",       50.0000, 3.0072, 40.0},
        {"nouvelle route",         50.0000, 3.0100, 40.0},
        {"bifurcation",            50.0020, 3.0120, 35.0},
        {"approche destination",   50.0040, 3.0120, 35.0},
        {"arrivee",                50.0060, 3.0120, 20.0}
    };

    RealRerouteFixture fixture = {
        .graph = &graph,
        .destination = destination,
        .callback_count = 0U
    };

    OpenRideNavigationScenarioResult result;
    assert(openride_test_navigation_scenario_run_with_reroute(
        "virage rate avec recalcul reel",
        &initial_route,
        track,
        (uint32_t)(sizeof(track) / sizeof(track[0])),
        5.0,
        calculate_real_reroute,
        &fixture,
        true,
        &result,
        error,
        sizeof(error)));

    assert(result.saw_off_route);
    assert(result.reroute_requested);
    assert(!result.reroute_install_failed);
    assert(result.real_reroute_count == 1U);
    assert(result.session_reroute_count == 1U);
    assert(fixture.callback_count == 1U);
    assert(result.saw_return_to_route);
    assert(result.saw_arrival);
    assert(result.final_progress_ratio > 0.999);

    bool saw_generation_one_instruction = false;
    bool saw_generation_one_arrival = false;
    for (uint32_t i = 0U; i < result.instruction_event_count; ++i) {
        const OpenRideNavigationScenarioInstructionEvent *event =
            &result.instruction_events[i];
        if (event->route_generation != 1U) continue;
        saw_generation_one_instruction = true;
        if (event->maneuver == OPENRIDE_MANEUVER_ARRIVE) {
            saw_generation_one_arrival = true;
        }
    }
    assert(saw_generation_one_instruction);
    assert(saw_generation_one_arrival);

    openride_route_destroy(&initial_route);
    openride_routing_graph_destroy(&graph);
}

int main(void)
{
    OpenRideRoute route = make_reference_route();
    run_nominal(&route);
    run_noisy_gps(&route);
    run_missed_turn_and_return(&route);
    openride_route_destroy(&route);

    run_multi_region_instruction_progression();
    run_real_reroute_after_missed_turn();

    puts("\nNavigation scenario tests: OK");
    return 0;
}
