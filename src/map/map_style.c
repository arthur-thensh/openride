#include "openride/map_style.h"

#include <string.h>

static bool g_drive_mode_active = false;

static bool is_kind(const char *kind, const char *expected)
{
    return kind && expected && strcmp(kind, expected) == 0;
}

static bool road_runtime_fade_started(const char *kind, double zoom)
{
#ifdef __ANDROID__
    /*
     * The ORMap renderer applies the matching smooth fades to these classes.
     * Before each fade starts their final alpha is exactly zero, so admitting
     * them here only makes the renderer traverse and rebuild invisible road
     * geometry. Keep the style visibility gate aligned with the runtime fade
     * starts to skip that work without changing anything visible on screen.
     */
    if (is_kind(kind, "secondary")) return zoom >= 11.75;
    if (is_kind(kind, "tertiary")) return zoom >= 12.75;

    if (is_kind(kind, "unclassified") ||
        is_kind(kind, "residential") ||
        is_kind(kind, "service") ||
        is_kind(kind, "living_street")) {
        return zoom >= 13.75;
    }

    if (is_kind(kind, "track") ||
        is_kind(kind, "path") ||
        is_kind(kind, "footway") ||
        is_kind(kind, "cycleway") ||
        is_kind(kind, "steps")) {
        return zoom >= 14.50;
    }
#else
    (void)kind;
    (void)zoom;
#endif
    return true;
}

static OpenRideMapColor color(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    OpenRideMapColor value = {r, g, b, a};
    return value;
}

static int population_bonus(int64_t population)
{
    if (population <= 0) return 0;

    int bonus = 0;
    while (population >= 10 && bonus < 180) {
        population /= 10;
        bonus += 30;
    }
    return bonus;
}

void openride_map_style_set_drive_mode_active(bool active)
{
    g_drive_mode_active = active;
}

bool openride_map_style_drive_mode_active(void)
{
    return g_drive_mode_active;
}

const char *openride_map_style_name(OpenRideMapStyle style)
{
    switch (style) {
        case OPENRIDE_MAP_STYLE_ROAD: return "Road";
        case OPENRIDE_MAP_STYLE_TOPO: return "Topo";
        case OPENRIDE_MAP_STYLE_TRAIL:
        default: return "Trail";
    }
}

OpenRideMapStyle openride_map_style_next(OpenRideMapStyle style)
{
    switch (style) {
        case OPENRIDE_MAP_STYLE_ROAD: return OPENRIDE_MAP_STYLE_TRAIL;
        case OPENRIDE_MAP_STYLE_TRAIL: return OPENRIDE_MAP_STYLE_TOPO;
        case OPENRIDE_MAP_STYLE_TOPO:
        default: return OPENRIDE_MAP_STYLE_ROAD;
    }
}

OpenRideMapPalette openride_map_palette(OpenRideMapStyle style)
{
    OpenRideMapPalette palette;

    if (g_drive_mode_active) {
        /*
         * Drive is intentionally close to a modern navigation canvas: large
         * low-frequency context remains, while urban texture fades behind the
         * active route. Building RGB is deliberately near the background so
         * renderers that apply their own alpha still keep structures subtle.
         */
        palette.background = color(246, 247, 244, 255);
        palette.water = color(163, 196, 214, 220);
        palette.water_line = color(125, 176, 202, 205);
        palette.boundary = color(183, 184, 180, 55);
        palette.building = color(238, 240, 236, 35);
        palette.rail = color(146, 150, 148, 75);
        palette.label = color(61, 66, 64, 225);
        palette.label_halo = color(250, 251, 248, 245);
        return palette;
    }

    switch (style) {
        case OPENRIDE_MAP_STYLE_ROAD:
            palette.background = color(242, 243, 240, 255);
            palette.water = color(147, 190, 214, 255);
            palette.water_line = color(112, 171, 204, 255);
            palette.boundary = color(185, 176, 190, 170);
            palette.building = color(205, 202, 197, 180);
            palette.rail = color(115, 118, 122, 220);
            palette.label = color(43, 47, 50, 255);
            palette.label_halo = color(250, 250, 247, 245);
            break;

        case OPENRIDE_MAP_STYLE_TOPO:
            palette.background = color(232, 236, 224, 255);
            palette.water = color(104, 165, 194, 255);
            palette.water_line = color(76, 145, 180, 255);
            palette.boundary = color(151, 139, 157, 180);
            palette.building = color(174, 170, 160, 170);
            palette.rail = color(91, 94, 95, 220);
            palette.label = color(43, 50, 43, 255);
            palette.label_halo = color(245, 246, 238, 245);
            break;

        case OPENRIDE_MAP_STYLE_TRAIL:
        default:
            /* Neutral canvas: the transport network must dominate visually. */
            palette.background = color(241, 242, 236, 255);
            palette.water = color(110, 174, 204, 255);
            palette.water_line = color(67, 143, 181, 255);
            palette.boundary = color(163, 158, 166, 105);
            /* Buildings are hidden below z16 and remain intentionally faint. */
            palette.building = color(197, 198, 191, 55);
            palette.rail = color(122, 124, 120, 105);
            palette.label = color(42, 48, 42, 255);
            palette.label_halo = color(250, 250, 246, 250);
            break;
    }

    return palette;
}

bool openride_map_place_label_visible(const char *kind,
                                      int64_t population,
                                      double zoom)
{
    if (g_drive_mode_active) {
        /* Keep only orientation-scale place names while riding. */
        if (!kind || kind[0] == '\0') return false;
        if (is_kind(kind, "capital")) return zoom >= 4.0;
        if (is_kind(kind, "city")) return zoom >= 7.0;
        if (is_kind(kind, "town")) return zoom >= 9.5;
        if (is_kind(kind, "village")) return zoom >= 12.5;
        if (is_kind(kind, "suburb")) return zoom >= 16.5;
        return false;
    }

    if (!kind || kind[0] == '\0') return zoom >= 13.0;

    if (is_kind(kind, "capital")) return zoom >= 4.0;

    if (is_kind(kind, "city")) {
        if (population >= 100000) return zoom >= 6.0;
        if (population >= 50000) return zoom >= 7.0;
        return zoom >= 8.5;
    }

    if (is_kind(kind, "town")) {
        if (population >= 30000) return zoom >= 8.5;
        if (population >= 10000) return zoom >= 9.5;
        return zoom >= 10.5;
    }

    if (is_kind(kind, "village")) {
        if (population >= 5000) return zoom >= 11.0;
        if (population >= 1500) return zoom >= 12.0;
        return zoom >= 12.75;
    }

    if (is_kind(kind, "suburb")) return zoom >= 12.75;
    if (is_kind(kind, "borough")) return zoom >= 12.75;
    if (is_kind(kind, "hamlet")) return zoom >= 13.5;
    if (is_kind(kind, "quarter")) return zoom >= 13.75;
    if (is_kind(kind, "neighbourhood")) return zoom >= 13.75;
    if (is_kind(kind, "locality")) return zoom >= 14.0;
    if (is_kind(kind, "isolated_dwelling")) return zoom >= 14.0;

    return zoom >= 13.5;
}

int openride_map_place_label_priority(const char *kind,
                                      int64_t population)
{
    int base = 100;

    if (is_kind(kind, "capital")) base = 1200;
    else if (is_kind(kind, "city")) base = 1000;
    else if (is_kind(kind, "town")) base = 850;
    else if (is_kind(kind, "village")) base = 650;
    else if (is_kind(kind, "borough")) base = 520;
    else if (is_kind(kind, "suburb")) base = 500;
    else if (is_kind(kind, "hamlet")) base = 360;
    else if (is_kind(kind, "quarter")) base = 300;
    else if (is_kind(kind, "neighbourhood")) base = 280;
    else if (is_kind(kind, "locality")) base = 180;
    else if (is_kind(kind, "isolated_dwelling")) base = 160;

    return base + population_bonus(population);
}

bool openride_map_buildings_visible(OpenRideMapStyle style, double zoom)
{
    if (g_drive_mode_active) return false;
    if (style == OPENRIDE_MAP_STYLE_TRAIL) return zoom >= 16.0;
    if (style == OPENRIDE_MAP_STYLE_TOPO) return zoom >= 15.0;
    return zoom >= 13.75;
}

bool openride_map_road_visible_for_style(OpenRideMapStyle style,
                                         const char *kind,
                                         double zoom)
{
    if (!kind || kind[0] == '\0') return zoom >= 12.0;
    if (!road_runtime_fade_started(kind, zoom)) return false;

    if (g_drive_mode_active) {
        if (is_kind(kind, "motorway") || is_kind(kind, "trunk")) return zoom >= 5.0;
        if (is_kind(kind, "primary")) return zoom >= 8.0;
        if (is_kind(kind, "secondary")) return zoom >= 11.75;
        if (is_kind(kind, "tertiary")) return zoom >= 12.75;
        if (is_kind(kind, "unclassified") ||
            is_kind(kind, "residential") ||
            is_kind(kind, "living_street")) {
            return zoom >= 13.75;
        }
        if (is_kind(kind, "service")) return zoom >= 14.5;
        if (is_kind(kind, "track")) return zoom >= 14.5;
        if (is_kind(kind, "path") ||
            is_kind(kind, "footway") ||
            is_kind(kind, "cycleway") ||
            is_kind(kind, "steps")) {
            return zoom >= 14.75;
        }
        return zoom >= 14.0;
    }

    if (is_kind(kind, "motorway") || is_kind(kind, "trunk")) return zoom >= 5.0;
    if (is_kind(kind, "primary")) return zoom >= 8.0;
    if (is_kind(kind, "secondary")) return zoom >= 9.0;
    if (is_kind(kind, "tertiary")) return zoom >= 10.5;

    if (is_kind(kind, "unclassified") ||
        is_kind(kind, "residential") ||
        is_kind(kind, "living_street")) {
        return zoom >= (style == OPENRIDE_MAP_STYLE_TRAIL ? 11.0 : 12.0);
    }

    if (is_kind(kind, "service")) {
        return zoom >= (style == OPENRIDE_MAP_STYLE_TRAIL ? 11.75 :
                        style == OPENRIDE_MAP_STYLE_ROAD ? 13.0 : 12.5);
    }

    if (is_kind(kind, "track")) {
        if (style == OPENRIDE_MAP_STYLE_TRAIL) return zoom >= 10.75;
        if (style == OPENRIDE_MAP_STYLE_TOPO) return zoom >= 11.75;
        return zoom >= 13.0;
    }

    if (is_kind(kind, "path") ||
        is_kind(kind, "footway") ||
        is_kind(kind, "cycleway") ||
        is_kind(kind, "steps")) {
        if (style == OPENRIDE_MAP_STYLE_TRAIL) return zoom >= 11.75;
        if (style == OPENRIDE_MAP_STYLE_TOPO) return zoom >= 13.0;
        return zoom >= 14.0;
    }

    return zoom >= 12.5;
}

bool openride_map_road_visible(const char *kind, double zoom)
{
    return openride_map_road_visible_for_style(OPENRIDE_MAP_STYLE_TRAIL,
                                               kind,
                                               zoom);
}

bool openride_map_road_paint(OpenRideMapStyle style,
                             const char *kind,
                             bool rail,
                             double zoom,
                             OpenRideMapRoadPaint *paint)
{
    if (!paint) return false;

    memset(paint, 0, sizeof(*paint));
    paint->line = color(160, 163, 160, 255);
    paint->casing = color(0, 0, 0, 0);
    paint->width = 1;
    paint->casing_width = 0;
    paint->dashed = false;

    if (rail) {
        const OpenRideMapPalette palette = openride_map_palette(style);
        paint->line = palette.rail;
        paint->width = 1;
        paint->dashed = true;
        return true;
    }

    if (!openride_map_road_visible_for_style(style, kind, zoom)) return false;

    const bool motorway = is_kind(kind, "motorway") || is_kind(kind, "trunk");
    const bool primary = is_kind(kind, "primary");
    const bool secondary = is_kind(kind, "secondary");
    const bool tertiary = is_kind(kind, "tertiary");
    const bool local = is_kind(kind, "residential") ||
                       is_kind(kind, "unclassified") ||
                       is_kind(kind, "living_street") ||
                       is_kind(kind, "service");
    const bool track = is_kind(kind, "track");
    const bool path = is_kind(kind, "path") ||
                      is_kind(kind, "footway") ||
                      is_kind(kind, "cycleway") ||
                      is_kind(kind, "steps");

    if (g_drive_mode_active) {
        /*
         * Navigation hierarchy: the active route overlay owns saturated blue.
         * The base network becomes neutral and progressively thinner, while
         * tracks/paths remain distinct for OpenRide's trail use case.
         */
        if (motorway) {
            paint->line = color(221, 224, 220, 205);
            paint->casing = color(174, 180, 176, 105);
            paint->width = 3;
            paint->casing_width = 4;
        } else if (primary) {
            paint->line = color(236, 237, 232, 230);
            paint->casing = color(181, 186, 181, 120);
            paint->width = 3;
            paint->casing_width = 4;
        } else if (secondary) {
            paint->line = color(240, 241, 237, 220);
            paint->casing = color(191, 196, 191, 90);
            paint->width = 2;
            paint->casing_width = 3;
        } else if (tertiary) {
            paint->line = color(224, 228, 223, 190);
            paint->width = 2;
        } else if (local) {
            paint->line = color(207, 212, 207, 135);
            paint->width = 1;
        } else if (track) {
            paint->line = color(143, 116, 78, 190);
            paint->width = 2;
            paint->dashed = true;
        } else if (path) {
            paint->line = color(95, 126, 88, 175);
            paint->width = 1;
            paint->dashed = true;
        } else {
            paint->line = color(198, 203, 198, 120);
            paint->width = 1;
        }
        return true;
    }

    if (style == OPENRIDE_MAP_STYLE_ROAD) {
        if (motorway) {
            paint->line = color(231, 168, 102, 255);
            paint->casing = color(173, 126, 81, 255);
            paint->width = 4;
            paint->casing_width = 6;
        } else if (primary) {
            paint->line = color(241, 211, 137, 255);
            paint->casing = color(187, 166, 113, 255);
            paint->width = 3;
            paint->casing_width = 5;
        } else if (secondary) {
            paint->line = color(249, 239, 194, 255);
            paint->casing = color(193, 189, 158, 255);
            paint->width = 3;
            paint->casing_width = 4;
        } else if (tertiary) {
            paint->line = color(250, 250, 245, 255);
            paint->casing = color(190, 193, 190, 255);
            paint->width = 2;
            paint->casing_width = 3;
        } else if (local) {
            paint->line = color(246, 246, 242, 255);
            paint->casing = color(205, 207, 204, 255);
            paint->width = zoom >= 13.0 ? 2 : 1;
            paint->casing_width = zoom >= 13.0 ? 3 : 0;
        } else if (track || path) {
            paint->line = color(151, 144, 132, 220);
            paint->width = 1;
            paint->dashed = true;
        }
        return true;
    }

    if (style == OPENRIDE_MAP_STYLE_TOPO) {
        if (motorway) {
            paint->line = color(215, 155, 93, 255);
            paint->casing = color(156, 112, 72, 255);
            paint->width = 4;
            paint->casing_width = 6;
        } else if (primary) {
            paint->line = color(230, 193, 119, 255);
            paint->casing = color(168, 145, 96, 255);
            paint->width = 3;
            paint->casing_width = 5;
        } else if (secondary) {
            paint->line = color(238, 222, 166, 255);
            paint->casing = color(178, 173, 139, 255);
            paint->width = 3;
            paint->casing_width = 4;
        } else if (tertiary || local) {
            paint->line = color(245, 244, 233, 255);
            paint->casing = color(180, 183, 172, 255);
            paint->width = zoom >= 13.0 ? 2 : 1;
            paint->casing_width = zoom >= 13.0 ? 3 : 0;
        } else if (track) {
            paint->line = color(125, 96, 58, 255);
            paint->width = 2;
            paint->dashed = true;
        } else if (path) {
            paint->line = color(103, 119, 76, 245);
            paint->width = 1;
            paint->dashed = true;
        }
        return true;
    }

    /*
     * Trail is deliberately road-first. Urban detail is muted elsewhere so
     * local roads, tracks and paths remain readable on a motorcycle screen.
     */
    if (motorway) {
        paint->line = color(218, 197, 166, 205);
        paint->casing = color(170, 158, 140, 160);
        paint->width = 2;
        paint->casing_width = 3;
    } else if (primary) {
        paint->line = color(244, 216, 158, 255);
        paint->casing = color(168, 142, 101, 235);
        paint->width = 4;
        paint->casing_width = 6;
    } else if (secondary) {
        paint->line = color(255, 237, 178, 255);
        paint->casing = color(171, 151, 94, 245);
        paint->width = 4;
        paint->casing_width = 6;
    } else if (tertiary) {
        paint->line = color(255, 254, 245, 255);
        paint->casing = color(132, 146, 124, 245);
        paint->width = 3;
        paint->casing_width = 5;
    } else if (local) {
        paint->line = color(255, 255, 251, 255);
        paint->casing = color(145, 155, 139, 235);
        paint->width = zoom >= 11.75 ? 2 : 1;
        paint->casing_width = zoom >= 11.75 ? 4 : 2;
    } else if (track) {
        paint->line = color(126, 76, 29, 255);
        paint->casing = color(240, 226, 196, 245);
        paint->width = zoom >= 12.0 ? 3 : 2;
        paint->casing_width = zoom >= 12.0 ? 5 : 4;
        paint->dashed = true;
    } else if (path) {
        paint->line = color(55, 103, 52, 255);
        paint->casing = color(238, 243, 232, 220);
        paint->width = 2;
        paint->casing_width = 3;
        paint->dashed = true;
    }

    return true;
}
