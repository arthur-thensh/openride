#ifndef OPENRIDE_TEST_NAVIGATION_SCENARIO_H
#define OPENRIDE_TEST_NAVIGATION_SCENARIO_H

#include "openride/navigation_engine.h"
#include "openride/navigation_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct OpenRideNavigationScenarioWaypoint {
    const char *label;
    double lat;
    double lon;
    double speed_kph;
} OpenRideNavigationScenarioWaypoint;

typedef struct OpenRideNavigationScenarioResult {
    uint32_t sample_count;
    uint32_t status_transition_count;
    uint32_t on_route_sample_count;
    uint32_t off_route_sample_count;
    uint32_t arrived_sample_count;
    bool saw_off_route;
    bool saw_return_to_route;
    bool saw_arrival;
    bool reroute_requested;
    double elapsed_s;
    double max_distance_from_route_m;
    double final_progress_ratio;
    double final_remaining_m;
} OpenRideNavigationScenarioResult;

bool openride_test_navigation_scenario_run(
    const char *name,
    const OpenRideRoute *route,
    const OpenRideNavigationScenarioWaypoint *waypoints,
    uint32_t waypoint_count,
    double sample_step_m,
    bool verbose,
    OpenRideNavigationScenarioResult *result,
    char *error,
    size_t error_size);

#endif
