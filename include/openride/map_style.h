#ifndef OPENRIDE_MAP_STYLE_H
#define OPENRIDE_MAP_STYLE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum OpenRideMapStyle {
    OPENRIDE_MAP_STYLE_ROAD = 0,
    OPENRIDE_MAP_STYLE_TRAIL = 1,
    OPENRIDE_MAP_STYLE_TOPO = 2
} OpenRideMapStyle;

typedef struct OpenRideMapColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} OpenRideMapColor;

typedef struct OpenRideMapPalette {
    OpenRideMapColor background;
    OpenRideMapColor water;
    OpenRideMapColor water_line;
    OpenRideMapColor boundary;
    OpenRideMapColor building;
    OpenRideMapColor rail;
    OpenRideMapColor label;
    OpenRideMapColor label_halo;
} OpenRideMapPalette;

typedef struct OpenRideMapRoadPaint {
    OpenRideMapColor line;
    OpenRideMapColor casing;
    int width;
    int casing_width;
    bool dashed;
} OpenRideMapRoadPaint;

const char *openride_map_style_name(OpenRideMapStyle style);
OpenRideMapStyle openride_map_style_next(OpenRideMapStyle style);
OpenRideMapPalette openride_map_palette(OpenRideMapStyle style);

/*
 * Drive Mode keeps the user's selected map style intact while applying a
 * temporary navigation-first presentation. The flag is owned by the Drive
 * controller and affects only cartographic paint/visibility decisions.
 */
void openride_map_style_set_drive_mode_active(bool active);
bool openride_map_style_drive_mode_active(void);

/*
 * Pure-C map styling policy. No SDL dependency.
 *
 * Keeping visibility and paint decisions here makes the cartographic rules
 * testable and reusable later on Android/iOS without coupling them to the
 * renderer.
 */
bool openride_map_place_label_visible(const char *kind,
                                      int64_t population,
                                      double zoom);

int openride_map_place_label_priority(const char *kind,
                                      int64_t population);

/* Visibility policy for high-detail building polygons. */
bool openride_map_buildings_visible(OpenRideMapStyle style, double zoom);

bool openride_map_road_visible(const char *kind, double zoom);
bool openride_map_road_visible_for_style(OpenRideMapStyle style,
                                         const char *kind,
                                         double zoom);

bool openride_map_road_paint(OpenRideMapStyle style,
                             const char *kind,
                             bool rail,
                             double zoom,
                             OpenRideMapRoadPaint *paint);

#endif
