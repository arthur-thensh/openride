#include "openride/map_selection.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void test_selection_flow(void)
{
    OpenRideMapSelection selection;
    openride_map_selection_init(&selection);

    assert(!selection.has_start);
    assert(!selection.has_destination);
    assert(!openride_map_selection_complete(&selection));

    assert(openride_map_selection_add(&selection, 50.37, 3.08)
           == OPENRIDE_MARKER_START);
    assert(selection.has_start);
    assert(!selection.has_destination);

    assert(openride_map_selection_add(&selection, 50.29, 2.78)
           == OPENRIDE_MARKER_DESTINATION);
    assert(openride_map_selection_complete(&selection));

    assert(openride_map_selection_add(&selection, 0.0, 0.0)
           == OPENRIDE_MARKER_NONE);

    openride_map_selection_remove(&selection, OPENRIDE_MARKER_DESTINATION);
    assert(selection.has_start);
    assert(!selection.has_destination);

    assert(openride_map_selection_add(&selection, 50.30, 2.79)
           == OPENRIDE_MARKER_DESTINATION);

    openride_map_selection_clear(&selection);
    assert(!selection.has_start);
    assert(!selection.has_destination);
}

static void test_geo_distance(void)
{
    assert(fabs(openride_geo_distance_m(50.0, 3.0, 50.0, 3.0)) < 0.001);

    /* One degree of longitude on the equator is about 111.195 km. */
    const double d = openride_geo_distance_m(0.0, 0.0, 0.0, 1.0);
    assert(d > 111000.0);
    assert(d < 111400.0);
}

int main(void)
{
    test_selection_flow();
    test_geo_distance();
    puts("Map selection tests: OK");
    return 0;
}
