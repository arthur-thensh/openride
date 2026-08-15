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
    route.navigation_context = calloc(
        count, sizeof(*route.navigation_context));
    assert(route.geometry != NULL);
    assert(route.navigation_context != NULL);

    memcpy(route.geometry, points, count * sizeof(*points));
    route.geometry_count = count;
    route.navigation_context_count = count;

    for (uint32_t i = 1U; i < count; ++i) {
        route.distance_m += openride_geo_distance_m(
            points[i - 1U].lat,
            points[i - 1U].lon,
            points[i].lat,
            points[i].lon);
    }

    return route;
}

static void test_connector_geometry_uses_stable_angle(void)
{
    /*
     * The two tiny segments around the decision look like a 90 degree turn,
     * while the stable approach is roughly 45 degrees. The rider should get
     * "slightly right", not a full right-turn instruction.
     */
    const OpenRideRoutePoint points[] = {
        {49.99955, 2.99930},
        {49.99982, 3.00000},
        {50.00000, 3.00000},
        {50.00000, 3.00028},
        {50.00000, 3.00100}
    };

    OpenRideRoute route = route_from_points(points, 5U);
    route.navigation_context[2].flags =
        OPENRIDE_ROUTE_NAV_HAS_ALTERNATIVE;

    OpenRideNavigationInstructionList instructions = {0};
    char error[160] = {0};
    assert(openride_navigation_instructions_build(
        NULL, &route, &instructions, error, sizeof(error)));

    assert(instructions.count == 3U);
    assert(instructions.items[0].maneuver == OPENRIDE_MANEUVER_DEPART);
    assert(instructions.items[1].maneuver == OPENRIDE_MANEUVER_SLIGHT_RIGHT);
    assert(instructions.items[1].turn_angle_deg > 30.0);
    assert(instructions.items[1].turn_angle_deg < 55.0);
    assert(instructions.items[2].maneuver == OPENRIDE_MANEUVER_ARRIVE);

    openride_navigation_instructions_destroy(&instructions);
    openride_route_destroy(&route);
}

static OpenRideRoute build_roundabout_then_continue_route(bool turn_after_exit)
{
    OpenRideRoutePoint points[6] = {
        {50.00000, 3.00000},
        {50.00050, 3.00000},
        {50.00070, 3.00020},
        {50.00050, 3.00040},
        {50.00050, 3.00080},
        {50.00050, 3.00120}
    };

    if (turn_after_exit) {
        points[5].lat = 50.00010;
        points[5].lon = 3.00080;
    }

    OpenRideRoute route = route_from_points(points, 6U);

    route.navigation_context[1].flags =
        OPENRIDE_ROUTE_NAV_OUTGOING_ROUNDABOUT;
    route.navigation_context[2].flags =
        OPENRIDE_ROUTE_NAV_INCOMING_ROUNDABOUT
        | OPENRIDE_ROUTE_NAV_OUTGOING_ROUNDABOUT
        | OPENRIDE_ROUTE_NAV_HAS_ROUNDABOUT_EXIT;
    route.navigation_context[3].flags =
        OPENRIDE_ROUTE_NAV_INCOMING_ROUNDABOUT;
    route.navigation_context[4].flags =
        OPENRIDE_ROUTE_NAV_HAS_ALTERNATIVE;

    return route;
}

static void test_redundant_continue_after_roundabout_is_removed(void)
{
    OpenRideRoute route = build_roundabout_then_continue_route(false);
    OpenRideNavigationInstructionList instructions = {0};
    char error[160] = {0};

    assert(openride_navigation_instructions_build(
        NULL, &route, &instructions, error, sizeof(error)));

    assert(instructions.count == 3U);
    assert(instructions.items[0].maneuver == OPENRIDE_MANEUVER_DEPART);
    assert(instructions.items[1].maneuver == OPENRIDE_MANEUVER_ROUNDABOUT);
    assert(instructions.items[1].roundabout_exit_number == 2U);
    assert(instructions.items[2].maneuver == OPENRIDE_MANEUVER_ARRIVE);

    openride_navigation_instructions_destroy(&instructions);
    openride_route_destroy(&route);
}

static void test_real_turn_after_roundabout_is_kept(void)
{
    OpenRideRoute route = build_roundabout_then_continue_route(true);
    OpenRideNavigationInstructionList instructions = {0};
    char error[160] = {0};

    assert(openride_navigation_instructions_build(
        NULL, &route, &instructions, error, sizeof(error)));

    assert(instructions.count == 4U);
    assert(instructions.items[0].maneuver == OPENRIDE_MANEUVER_DEPART);
    assert(instructions.items[1].maneuver == OPENRIDE_MANEUVER_ROUNDABOUT);
    assert(instructions.items[1].roundabout_exit_number == 2U);
    assert(instructions.items[2].maneuver == OPENRIDE_MANEUVER_RIGHT
           || instructions.items[2].maneuver == OPENRIDE_MANEUVER_SHARP_RIGHT);
    assert(instructions.items[3].maneuver == OPENRIDE_MANEUVER_ARRIVE);

    openride_navigation_instructions_destroy(&instructions);
    openride_route_destroy(&route);
}

int main(void)
{
    test_connector_geometry_uses_stable_angle();
    test_redundant_continue_after_roundabout_is_removed();
    test_real_turn_after_roundabout_is_kept();
    puts("Navigation instruction refinement tests: OK");
    return 0;
}
