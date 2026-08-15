#include "openride/gps_simulator.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static OpenRideRoute make_route(void)
{
    OpenRideRoute route = {0};
    route.geometry_count = 2U;
    route.geometry = calloc(route.geometry_count, sizeof(*route.geometry));
    assert(route.geometry != NULL);
    route.geometry[0] = (OpenRideRoutePoint){50.0000, 3.0000};
    route.geometry[1] = (OpenRideRoutePoint){50.0000, 3.0200};
    route.distance_m = 1430.0;
    return route;
}

int main(void)
{
    OpenRideRoute route = make_route();
    OpenRideGPSSimulator simulator;
    openride_gps_simulator_init(&simulator);

    char error[128] = {0};
    assert(openride_gps_simulator_set_route(&simulator,
                                            &route,
                                            36.0,
                                            error,
                                            sizeof(error)));

    OpenRideGPSSample sample = {0};
    assert(openride_gps_simulator_sample(&simulator, &sample));
    assert(sample.valid);
    assert(fabs(sample.lat - 50.0) < 1e-7);
    assert(fabs(sample.lon - 3.0) < 1e-7);

    openride_gps_simulator_start(&simulator);
    assert(openride_gps_simulator_update(&simulator, 10.0, &sample));
    /* Delta is deliberately clamped to one second to make frame stalls harmless. */
    assert(sample.route_position_m > 9.0 && sample.route_position_m < 11.0);
    assert(sample.lon > 3.0);
    assert(sample.speed_mps > 9.9 && sample.speed_mps < 10.1);

    const double no_offset_lat = sample.lat;
    openride_gps_simulator_set_lateral_offset_m(&simulator, 80.0);
    assert(openride_gps_simulator_sample(&simulator, &sample));
    assert(fabs(sample.lat - no_offset_lat) > 0.0005);

    /*
     * Installing a rerouted geometry must clear a temporary DEV deviation.
     * v0.26-B relies on this to resume on the recalculated route.
     */
    assert(openride_gps_simulator_set_route(&simulator,
                                            &route,
                                            36.0,
                                            error,
                                            sizeof(error)));
    assert(fabs(simulator.lateral_offset_m) < 1e-9);
    assert(openride_gps_simulator_sample(&simulator, &sample));
    assert(fabs(sample.lat - 50.0) < 1e-7);
    assert(fabs(sample.lon - 3.0) < 1e-7);

    openride_gps_simulator_start(&simulator);
    openride_gps_simulator_stop(&simulator);
    assert(openride_gps_simulator_update(&simulator, 0.5, &sample));
    assert(sample.speed_mps == 0.0);

    openride_gps_simulator_destroy(&simulator);
    openride_route_destroy(&route);
    puts("GPS simulator tests: OK");
    return 0;
}
