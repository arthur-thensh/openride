#include "openride/location_provider.h"
#include "openride/simulated_location_provider.h"
#include "openride/map_selection.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

typedef struct FakeProvider {
    int starts;
    int stops;
    int polls;
} FakeProvider;

static bool fake_start(void *context)
{
    FakeProvider *fake = context;
    ++fake->starts;
    return true;
}

static void fake_stop(void *context)
{
    FakeProvider *fake = context;
    ++fake->stops;
}

static bool fake_poll(void *context, double delta_seconds, OpenRideLocationSample *sample)
{
    FakeProvider *fake = context;
    ++fake->polls;
    sample->valid = delta_seconds > 0.0;
    sample->lat = 50.37;
    sample->lon = 3.08;
    return true;
}

int main(void)
{
    FakeProvider fake = {0};
    OpenRideLocationProvider provider;
    OpenRideLocationSample sample;
    openride_location_provider_init(&provider, &fake, fake_start, fake_stop, fake_poll);
    assert(openride_location_provider_start(&provider));
    assert(fake.starts == 1);
    assert(openride_location_provider_poll(&provider, 0.1, &sample));
    assert(sample.valid);
    assert(fake.polls == 1);
    openride_location_provider_stop(&provider);
    assert(fake.stops == 1);

    OpenRideRoutePoint points[2] = {
        {50.0000, 3.0000},
        {50.0000, 3.0100}
    };
    OpenRideRoute route = {0};
    route.geometry = points;
    route.geometry_count = 2U;
    route.distance_m = openride_geo_distance_m(
        points[0].lat, points[0].lon,
        points[1].lat, points[1].lon);

    OpenRideGPSSimulator simulator;
    openride_gps_simulator_init(&simulator);
    char error[128] = {0};
    assert(openride_gps_simulator_set_route(
        &simulator, &route, 36.0, error, sizeof(error)));

    OpenRideLocationProvider simulated_provider;
    OpenRideSimulatedLocationContext simulated_context;
    openride_simulated_location_provider_init(
        &simulated_provider,
        &simulated_context,
        &simulator,
        2.0,
        4.0);

    assert(openride_location_provider_start(&simulated_provider));
    assert(simulator.active);
    assert(fabs(simulated_context.time_scale - 2.0) < 1e-9);

    OpenRideLocationSample simulated_sample = {0};
    assert(openride_location_provider_poll(
        &simulated_provider, 0.5, &simulated_sample));
    assert(simulated_sample.valid);
    assert(fabs(simulated_sample.accuracy_m - 4.0) < 1e-9);
    assert(fabs(simulated_sample.speed_mps - 10.0) < 1e-9);
    assert(simulated_sample.lon > points[0].lon);
    assert(simulator.position_m > 9.9 && simulator.position_m < 10.1);

    openride_simulated_location_provider_set_time_scale(
        &simulated_context, 5.0);
    assert(fabs(simulated_context.time_scale - 5.0) < 1e-9);
    assert(openride_location_provider_poll(
        &simulated_provider, 0.1, &simulated_sample));
    assert(simulator.position_m > 14.9 && simulator.position_m < 15.1);

    openride_location_provider_stop(&simulated_provider);
    assert(!simulator.active);
    openride_gps_simulator_destroy(&simulator);

    puts("Location provider tests: OK");
    return 0;
}
