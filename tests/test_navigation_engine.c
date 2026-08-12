#include "openride/navigation_engine.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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

    puts("Navigation engine tests: OK");
    return 0;
}
