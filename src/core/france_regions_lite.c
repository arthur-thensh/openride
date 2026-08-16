#include "openride/france_regions_lite.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPENRIDE_FRANCE_REGIONS_LITE_SCALE 100000.0

typedef struct OpenRideFranceRegionsLitePoint {
    int32_t lon_e5;
    int32_t lat_e5;
} OpenRideFranceRegionsLitePoint;

typedef struct OpenRideFranceRegionsLiteRing {
    uint32_t point_offset;
    uint32_t point_count;
    uint8_t hole;
} OpenRideFranceRegionsLiteRing;

typedef struct OpenRideFranceRegionsLiteRegion {
    const char *id;
    uint32_t first_ring;
    uint32_t ring_count;
    int32_t min_lon_e5;
    int32_t min_lat_e5;
    int32_t max_lon_e5;
    int32_t max_lat_e5;
} OpenRideFranceRegionsLiteRegion;

#include "france_regions_lite_data.inc"

static bool point_in_ring(const OpenRideFranceRegionsLiteRing *ring,
                          double lat_e5,
                          double lon_e5)
{
    if (!ring || ring->point_count < 3U) return false;

    bool inside = false;
    const uint32_t first = ring->point_offset;
    uint32_t previous = first + ring->point_count - 1U;

    for (uint32_t local = 0U; local < ring->point_count; ++local) {
        const uint32_t current = first + local;
        const OpenRideFranceRegionsLitePoint *a =
            &OPENRIDE_FRANCE_REGIONS_LITE_POINTS[current];
        const OpenRideFranceRegionsLitePoint *b =
            &OPENRIDE_FRANCE_REGIONS_LITE_POINTS[previous];

        const double ay = (double)a->lat_e5;
        const double by = (double)b->lat_e5;
        const double ax = (double)a->lon_e5;
        const double bx = (double)b->lon_e5;

        if ((ay > lat_e5) != (by > lat_e5)) {
            const double crossing =
                ax + (bx - ax) * (lat_e5 - ay) / (by - ay);
            if (lon_e5 < crossing) inside = !inside;
        }
        previous = current;
    }

    return inside;
}

static bool region_contains(const OpenRideFranceRegionsLiteRegion *region,
                            double lat_e5,
                            double lon_e5)
{
    if (!region) return false;
    if (lon_e5 < (double)region->min_lon_e5
        || lon_e5 > (double)region->max_lon_e5
        || lat_e5 < (double)region->min_lat_e5
        || lat_e5 > (double)region->max_lat_e5) {
        return false;
    }

    bool inside_outer = false;
    const uint32_t end = region->first_ring + region->ring_count;
    for (uint32_t i = region->first_ring; i < end; ++i) {
        const OpenRideFranceRegionsLiteRing *ring =
            &OPENRIDE_FRANCE_REGIONS_LITE_RINGS[i];
        if (!point_in_ring(ring, lat_e5, lon_e5)) continue;

        if (ring->hole) {
            return false;
        }
        inside_outer = true;
    }

    return inside_outer;
}

const char *openride_france_regions_lite_region_id(double lat, double lon)
{
    const double lat_e5 = lat * OPENRIDE_FRANCE_REGIONS_LITE_SCALE;
    const double lon_e5 = lon * OPENRIDE_FRANCE_REGIONS_LITE_SCALE;

    for (size_t i = 0U;
         i < sizeof(OPENRIDE_FRANCE_REGIONS_LITE_REGIONS)
             / sizeof(OPENRIDE_FRANCE_REGIONS_LITE_REGIONS[0]);
         ++i) {
        const OpenRideFranceRegionsLiteRegion *region =
            &OPENRIDE_FRANCE_REGIONS_LITE_REGIONS[i];
        if (region_contains(region, lat_e5, lon_e5)) {
            return region->id;
        }
    }
    return NULL;
}

bool openride_france_regions_lite_region_at(
    size_t index,
    OpenRideFranceRegionsLiteRegionView *region)
{
    if (!region || index >= openride_france_regions_lite_region_count()) {
        return false;
    }

    const OpenRideFranceRegionsLiteRegion *source =
        &OPENRIDE_FRANCE_REGIONS_LITE_REGIONS[index];
    region->id = source->id;
    region->first_ring = source->first_ring;
    region->ring_count = source->ring_count;
    region->min_lon =
        (double)source->min_lon_e5 / OPENRIDE_FRANCE_REGIONS_LITE_SCALE;
    region->min_lat =
        (double)source->min_lat_e5 / OPENRIDE_FRANCE_REGIONS_LITE_SCALE;
    region->max_lon =
        (double)source->max_lon_e5 / OPENRIDE_FRANCE_REGIONS_LITE_SCALE;
    region->max_lat =
        (double)source->max_lat_e5 / OPENRIDE_FRANCE_REGIONS_LITE_SCALE;
    return true;
}

bool openride_france_regions_lite_ring_at(
    size_t index,
    OpenRideFranceRegionsLiteRingView *ring)
{
    if (!ring || index >= openride_france_regions_lite_ring_count()) {
        return false;
    }

    const OpenRideFranceRegionsLiteRing *source =
        &OPENRIDE_FRANCE_REGIONS_LITE_RINGS[index];
    ring->point_offset = source->point_offset;
    ring->point_count = source->point_count;
    ring->hole = source->hole != 0U;
    return true;
}

bool openride_france_regions_lite_point_at(
    size_t index,
    OpenRideFranceRegionsLitePointView *point)
{
    if (!point || index >= openride_france_regions_lite_point_count()) {
        return false;
    }

    const OpenRideFranceRegionsLitePoint *source =
        &OPENRIDE_FRANCE_REGIONS_LITE_POINTS[index];
    point->lon =
        (double)source->lon_e5 / OPENRIDE_FRANCE_REGIONS_LITE_SCALE;
    point->lat =
        (double)source->lat_e5 / OPENRIDE_FRANCE_REGIONS_LITE_SCALE;
    return true;
}

size_t openride_france_regions_lite_region_count(void)
{
    return sizeof(OPENRIDE_FRANCE_REGIONS_LITE_REGIONS)
         / sizeof(OPENRIDE_FRANCE_REGIONS_LITE_REGIONS[0]);
}

size_t openride_france_regions_lite_ring_count(void)
{
    return sizeof(OPENRIDE_FRANCE_REGIONS_LITE_RINGS)
         / sizeof(OPENRIDE_FRANCE_REGIONS_LITE_RINGS[0]);
}

size_t openride_france_regions_lite_point_count(void)
{
    return sizeof(OPENRIDE_FRANCE_REGIONS_LITE_POINTS)
         / sizeof(OPENRIDE_FRANCE_REGIONS_LITE_POINTS[0]);
}
