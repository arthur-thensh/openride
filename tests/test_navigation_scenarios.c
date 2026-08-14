#include "navigation_scenario.h"
#include "openride/map_selection.h"

#include <assert.h>
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

int main(void)
{
    OpenRideRoute route = make_reference_route();
    run_nominal(&route);
    run_noisy_gps(&route);
    run_missed_turn_and_return(&route);
    openride_route_destroy(&route);

    puts("\nNavigation scenario tests: OK");
    return 0;
}
