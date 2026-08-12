#include "openride/location_filter.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    OpenRideLocationFilter filter;
    OpenRideFilteredLocation location;
    openride_location_filter_init(&filter);

    assert(openride_location_filter_update(&filter, 50.0, 3.0, 10.0, 350.0, 0.1, &location));
    assert(location.valid);
    assert(fabs(location.lat - 50.0) < 1e-12);

    assert(openride_location_filter_update(&filter, 50.00001, 3.00001, 12.0, 10.0, 0.1, &location));
    assert(location.lat > 50.0 && location.lat < 50.00001);
    assert(location.heading_deg > 350.0 || location.heading_deg < 10.0);
    assert(location.speed_mps > 10.0 && location.speed_mps < 12.0);

    filter.config.reset_jump_distance_m = 50.0;
    assert(openride_location_filter_update(&filter, 50.01, 3.01, 5.0, 90.0, 0.1, &location));
    assert(fabs(location.lat - 50.01) < 1e-12);
    assert(fabs(location.lon - 3.01) < 1e-12);
    assert(fabs(location.heading_deg - 90.0) < 1e-12);

    printf("Location filter tests: OK\n");
    return 0;
}
