#include "openride/navigation_engine.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static OpenRideRoute make_route(void)
{
    OpenRideRoute route = {0};
    route.geometry_count = 3U;
    route.geometry = calloc(route.geometry_count, sizeof(*route.geometry));
    assert(route.geometry != NULL);
    route.geometry[0] = (OpenRideRoutePoint){50.0000, 3.0000};
    route.geometry[1] = (OpenRideRoutePoint){50.0000, 3.0100};
    route.geometry[2] = (OpenRideRoutePoint){50.0100, 3.0100};
    route.distance_m = 1825.0;
    return route;
}

static OpenRideRoute make_closed_loop_route(void)
{
    OpenRideRoute route = {0};
    route.geometry_count = 5U;
    route.geometry = calloc(route.geometry_count, sizeof(*route.geometry));
    assert(route.geometry != NULL);

    /*
     * Final point ~22 m from the first point. The initial GPS sample below is
     * intentionally closer to the final segment than to the first segment.
     */
    route.geometry[0] = (OpenRideRoutePoint){50.0000, 3.0000};
    route.geometry[1] = (OpenRideRoutePoint){50.0000, 3.0100};
    route.geometry[2] = (OpenRideRoutePoint){50.0100, 3.0100};
    route.geometry[3] = (OpenRideRoutePoint){50.0100, 3.0000};
    route.geometry[4] = (OpenRideRoutePoint){50.0002, 3.0000};
    route.distance_m = 3630.0;
    return route;
}


static OpenRideRoute make_self_crossing_route(void)
{
    OpenRideRoute route = {0};
    route.geometry_count = 11U;
    route.geometry = calloc(route.geometry_count, sizeof(*route.geometry));
    assert(route.geometry != NULL);

    /*
     * Two passes through the same crossing at geometry points 3 and 7.
     * The route remains open so this test isolates crossing continuity from the
     * closed-loop start bootstrap tested separately.
     */
    route.geometry[0]  = (OpenRideRoutePoint){50.0000, 3.0000};
    route.geometry[1]  = (OpenRideRoutePoint){50.0020, 3.0010};
    route.geometry[2]  = (OpenRideRoutePoint){50.0020, 3.0030};
    route.geometry[3]  = (OpenRideRoutePoint){50.0000, 3.0040};
    route.geometry[4]  = (OpenRideRoutePoint){49.9980, 3.0050};
    route.geometry[5]  = (OpenRideRoutePoint){50.0000, 3.0080};
    route.geometry[6]  = (OpenRideRoutePoint){50.0020, 3.0050};
    route.geometry[7]  = (OpenRideRoutePoint){50.0000, 3.0040};
    route.geometry[8]  = (OpenRideRoutePoint){49.9980, 3.0030};
    route.geometry[9]  = (OpenRideRoutePoint){49.9980, 3.0010};
    route.geometry[10] = (OpenRideRoutePoint){49.9960, 3.0000};
    route.distance_m = 2600.0;
    return route;
}

int main(void)
{
    OpenRideRoute route = make_route();
    OpenRideNavigationEngine navigation;
    openride_navigation_engine_init(&navigation);

    char error[128] = {0};
    assert(openride_navigation_engine_set_route(&navigation,
                                                &route,
                                                error,
                                                sizeof(error)));

    OpenRideNavigationState state = {0};
    assert(openride_navigation_engine_update(&navigation,
                                             50.0000,
                                             3.0050,
                                             15.0,
                                             90.0,
                                             &state));
    assert(state.valid);
    assert(state.status == OPENRIDE_NAVIGATION_ON_ROUTE);
    assert(state.distance_from_route_m < 1.0);
    assert(state.progress_ratio > 0.15 && state.progress_ratio < 0.35);
    assert(state.remaining_m > 1000.0);

    /* About 111 m north of the first leg: definitely off route. */
    assert(openride_navigation_engine_update(&navigation,
                                             50.0010,
                                             3.0050,
                                             12.0,
                                             90.0,
                                             &state));
    assert(state.status == OPENRIDE_NAVIGATION_OFF_ROUTE);
    assert(state.distance_from_route_m > 90.0);

    /* Hysteresis: ~33 m remains off-route until we are closer than 25 m. */
    assert(openride_navigation_engine_update(&navigation,
                                             50.00030,
                                             3.0050,
                                             12.0,
                                             90.0,
                                             &state));
    assert(state.status == OPENRIDE_NAVIGATION_OFF_ROUTE);

    assert(openride_navigation_engine_update(&navigation,
                                             50.00010,
                                             3.0050,
                                             12.0,
                                             90.0,
                                             &state));
    assert(state.status == OPENRIDE_NAVIGATION_ON_ROUTE);

    assert(openride_navigation_engine_update(&navigation,
                                             50.0100,
                                             3.0100,
                                             0.0,
                                             0.0,
                                             &state));
    assert(state.status == OPENRIDE_NAVIGATION_ARRIVED);
    assert(state.remaining_m < 1.0);
    assert(state.progress_ratio > 0.999);

    openride_navigation_engine_destroy(&navigation);
    openride_route_destroy(&route);

    /*
     * Regression: on a closed loop, GPS noise near the start must not snap
     * navigation to the final segment and immediately report ARRIVED.
     */
    OpenRideRoute loop = make_closed_loop_route();
    OpenRideNavigationEngine loop_navigation;
    openride_navigation_engine_init(&loop_navigation);

    assert(openride_navigation_engine_set_route(&loop_navigation,
                                                &loop,
                                                error,
                                                sizeof(error)));

    memset(&state, 0, sizeof(state));
    assert(openride_navigation_engine_update(&loop_navigation,
                                             50.00018,
                                             3.00005,
                                             2.0,
                                             90.0,
                                             &state));
    assert(state.valid);
    assert(state.status == OPENRIDE_NAVIGATION_ON_ROUTE);
    assert(state.progress_ratio < 0.10);
    assert(state.remaining_m > 3000.0);
    assert(state.route_segment_index == 0U);

    /* Move around the loop so the start bootstrap is released. */
    assert(openride_navigation_engine_update(&loop_navigation,
                                             50.0000,
                                             3.0050,
                                             10.0,
                                             90.0,
                                             &state));
    assert(state.status == OPENRIDE_NAVIGATION_ON_ROUTE);

    assert(openride_navigation_engine_update(&loop_navigation,
                                             50.0050,
                                             3.0100,
                                             10.0,
                                             0.0,
                                             &state));
    assert(state.progress_ratio > 0.20);

    assert(openride_navigation_engine_update(&loop_navigation,
                                             50.0100,
                                             3.0050,
                                             10.0,
                                             270.0,
                                             &state));
    assert(state.progress_ratio > 0.45);

    assert(openride_navigation_engine_update(&loop_navigation,
                                             50.0050,
                                             3.0000,
                                             10.0,
                                             180.0,
                                             &state));
    assert(state.progress_ratio > 0.70);

    assert(openride_navigation_engine_update(&loop_navigation,
                                             50.0002,
                                             3.0000,
                                             0.0,
                                             180.0,
                                             &state));
    assert(state.status == OPENRIDE_NAVIGATION_ARRIVED);
    assert(state.remaining_m < 1.0);
    assert(state.progress_ratio > 0.999);

    openride_navigation_engine_destroy(&loop_navigation);
    openride_route_destroy(&loop);


    /*
     * Regression: when the route crosses itself, GPS noise must not jump the
     * navigation to the later pass through the crossing.
     */
    OpenRideRoute crossing = make_self_crossing_route();
    OpenRideNavigationEngine crossing_navigation;
    openride_navigation_engine_init(&crossing_navigation);

    assert(openride_navigation_engine_set_route(&crossing_navigation,
                                                &crossing,
                                                error,
                                                sizeof(error)));

    memset(&state, 0, sizeof(state));

    /* Establish progress on the first arm. */
    assert(openride_navigation_engine_update(&crossing_navigation,
                                             50.0010,
                                             3.0005,
                                             10.0,
                                             30.0,
                                             &state));
    assert(state.route_segment_index == 0U);

    assert(openride_navigation_engine_update(&crossing_navigation,
                                             50.0020,
                                             3.0020,
                                             10.0,
                                             90.0,
                                             &state));
    assert(state.route_segment_index == 1U);

    /*
     * This noisy point lies essentially on the later south-west branch
     * (segment 7), while still being only about 23 m from the current first
     * pass. Pure nearest-segment matching would jump forward to segment 7.
     */
    assert(openride_navigation_engine_update(&crossing_navigation,
                                             49.9998,
                                             3.0039,
                                             10.0,
                                             150.0,
                                             &state));
    assert(state.status == OPENRIDE_NAVIGATION_ON_ROUTE);
    assert(state.route_segment_index <= 3U);
    const double first_crossing_progress = state.progress_ratio;
    assert(first_crossing_progress < 0.45);

    /* Continue around the middle of the figure eight. */
    assert(openride_navigation_engine_update(&crossing_navigation,
                                             49.9990,
                                             3.0065,
                                             10.0,
                                             60.0,
                                             &state));
    assert(state.route_segment_index >= 4U);
    assert(state.progress_ratio > first_crossing_progress);

    assert(openride_navigation_engine_update(&crossing_navigation,
                                             50.0010,
                                             3.0065,
                                             10.0,
                                             300.0,
                                             &state));
    assert(state.route_segment_index >= 5U);

    /*
     * On the second visit, invert the GPS noise: this point lies on the earlier
     * branch, but continuity must now keep us on the later pass.
     */
    assert(openride_navigation_engine_update(&crossing_navigation,
                                             50.0001,
                                             3.00395,
                                             10.0,
                                             210.0,
                                             &state));
    assert(state.status == OPENRIDE_NAVIGATION_ON_ROUTE);
    assert(state.route_segment_index >= 6U);
    assert(state.progress_ratio > 0.50);

    openride_navigation_engine_destroy(&crossing_navigation);
    openride_route_destroy(&crossing);

    puts("Navigation engine tests: OK");
    return 0;
}
