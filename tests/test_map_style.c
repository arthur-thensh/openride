#include "openride/map_style.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(openride_map_place_label_visible("city", 100000, 7.0));
    assert(!openride_map_place_label_visible("village", 800, 12.0));
    assert(openride_map_place_label_visible("village", 6000, 12.0));
    assert(!openride_map_place_label_visible("hamlet", 300, 13.0));
    assert(openride_map_place_label_visible("hamlet", 300, 14.0));

    assert(openride_map_place_label_priority("city", 50000) >
           openride_map_place_label_priority("village", 50000));
    assert(openride_map_place_label_priority("town", 50000) >
           openride_map_place_label_priority("town", 500));

    assert(openride_map_road_visible("motorway", 8.0));
    assert(!openride_map_road_visible("residential", 11.0));
    assert(openride_map_road_visible("residential", 12.0));
    assert(!openride_map_road_visible("path", 13.0));
    assert(openride_map_road_visible("path", 14.0));

    puts("Map style tests: OK");
    return 0;
}
