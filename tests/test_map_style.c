#include "openride/map_style.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    openride_map_style_set_drive_mode_active(false);
    assert(!openride_map_style_drive_mode_active());

    assert(strcmp(openride_map_style_name(OPENRIDE_MAP_STYLE_TRAIL), "Trail") == 0);
    assert(openride_map_style_next(OPENRIDE_MAP_STYLE_ROAD) == OPENRIDE_MAP_STYLE_TRAIL);
    assert(openride_map_style_next(OPENRIDE_MAP_STYLE_TRAIL) == OPENRIDE_MAP_STYLE_TOPO);
    assert(openride_map_style_next(OPENRIDE_MAP_STYLE_TOPO) == OPENRIDE_MAP_STYLE_ROAD);

    const OpenRideMapPalette trail = openride_map_palette(OPENRIDE_MAP_STYLE_TRAIL);
    const OpenRideMapPalette road = openride_map_palette(OPENRIDE_MAP_STYLE_ROAD);
    assert(trail.background.r != road.background.r ||
           trail.background.g != road.background.g ||
           trail.background.b != road.background.b);

    assert(openride_map_place_label_visible("city", 100000, 7.0));
    assert(!openride_map_place_label_visible("village", 800, 12.0));
    assert(openride_map_place_label_visible("village", 6000, 12.0));
    assert(!openride_map_place_label_visible("hamlet", 300, 13.0));
    assert(openride_map_place_label_visible("hamlet", 300, 14.0));

    assert(openride_map_place_label_priority("city", 50000) >
           openride_map_place_label_priority("village", 50000));
    assert(openride_map_place_label_priority("town", 50000) >
           openride_map_place_label_priority("town", 500));

    assert(!openride_map_buildings_visible(OPENRIDE_MAP_STYLE_TRAIL, 15.9));
    assert(openride_map_buildings_visible(OPENRIDE_MAP_STYLE_TRAIL, 16.0));
    assert(openride_map_buildings_visible(OPENRIDE_MAP_STYLE_ROAD, 14.0));

    assert(openride_map_road_visible("motorway", 8.0));
    assert(!openride_map_road_visible_for_style(OPENRIDE_MAP_STYLE_ROAD,
                                                "track",
                                                12.0));
    assert(openride_map_road_visible_for_style(OPENRIDE_MAP_STYLE_TRAIL,
                                               "track",
                                               12.0));
    assert(!openride_map_road_visible_for_style(OPENRIDE_MAP_STYLE_TRAIL,
                                                "path",
                                                11.5));
    assert(openride_map_road_visible_for_style(OPENRIDE_MAP_STYLE_TRAIL,
                                               "path",
                                               12.0));

    OpenRideMapRoadPaint paint;
    assert(openride_map_road_paint(OPENRIDE_MAP_STYLE_TRAIL,
                                   "track",
                                   false,
                                   13.0,
                                   &paint));
    assert(paint.dashed);
    assert(paint.width >= 3);
    assert(paint.casing_width > paint.width);

    assert(openride_map_road_paint(OPENRIDE_MAP_STYLE_TRAIL,
                                   "residential",
                                   false,
                                   12.0,
                                   &paint));
    assert(paint.width >= 2);
    assert(paint.casing_width >= 4);

    assert(openride_map_road_paint(OPENRIDE_MAP_STYLE_TRAIL,
                                   "path",
                                   false,
                                   12.0,
                                   &paint));
    assert(paint.dashed);
    assert(paint.width >= 2);

    assert(openride_map_road_paint(OPENRIDE_MAP_STYLE_ROAD,
                                   "motorway",
                                   false,
                                   12.0,
                                   &paint));
    assert(!paint.dashed);
    assert(paint.casing_width > paint.width);

    /* Drive temporarily overrides paint/visibility without changing the
     * user's selected Road/Trail/Topo style value. */
    openride_map_style_set_drive_mode_active(true);
    assert(openride_map_style_drive_mode_active());

    const OpenRideMapPalette drive =
        openride_map_palette(OPENRIDE_MAP_STYLE_TRAIL);
    assert(drive.background.r >= 240U);
    assert(drive.background.g >= 240U);
    assert(drive.background.b >= 235U);
    assert(drive.building.r + 12U >= drive.background.r);
    assert(drive.building.g + 12U >= drive.background.g);
    assert(drive.building.b + 12U >= drive.background.b);
    assert(!openride_map_buildings_visible(OPENRIDE_MAP_STYLE_TRAIL, 18.0));

    assert(openride_map_place_label_visible("city", 0, 18.0));
    assert(openride_map_place_label_visible("village", 0, 18.0));
    assert(!openride_map_place_label_visible("hamlet", 0, 18.0));
    assert(!openride_map_place_label_visible("quarter", 0, 18.0));
    assert(!openride_map_place_label_visible("locality", 0, 18.0));

    OpenRideMapRoadPaint drive_primary;
    OpenRideMapRoadPaint drive_local;
    OpenRideMapRoadPaint drive_track;
    OpenRideMapRoadPaint drive_path;

    assert(openride_map_road_paint(OPENRIDE_MAP_STYLE_TRAIL,
                                   "primary",
                                   false,
                                   18.0,
                                   &drive_primary));
    assert(openride_map_road_paint(OPENRIDE_MAP_STYLE_TRAIL,
                                   "residential",
                                   false,
                                   18.0,
                                   &drive_local));
    assert(openride_map_road_paint(OPENRIDE_MAP_STYLE_TRAIL,
                                   "track",
                                   false,
                                   18.0,
                                   &drive_track));
    assert(openride_map_road_paint(OPENRIDE_MAP_STYLE_TRAIL,
                                   "path",
                                   false,
                                   18.0,
                                   &drive_path));

    assert(drive_primary.width >= drive_local.width);
    assert(drive_local.width == 1);
    assert(drive_local.casing_width == 0);
    assert(drive_track.dashed);
    assert(drive_track.width == 2);
    assert(drive_path.dashed);
    assert(drive_path.width == 1);
    assert(!openride_map_road_visible_for_style(OPENRIDE_MAP_STYLE_TRAIL,
                                                "track",
                                                14.25));
    assert(openride_map_road_visible_for_style(OPENRIDE_MAP_STYLE_TRAIL,
                                               "track",
                                               14.50));

    /* Normal cartography must be restored immediately after Drive. */
    openride_map_style_set_drive_mode_active(false);
    assert(!openride_map_style_drive_mode_active());
    assert(openride_map_buildings_visible(OPENRIDE_MAP_STYLE_TRAIL, 16.0));
    assert(openride_map_place_label_visible("hamlet", 300, 14.0));

    puts("Map style tests: OK");
    return 0;
}
