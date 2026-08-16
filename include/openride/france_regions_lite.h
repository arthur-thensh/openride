#ifndef OPENRIDE_FRANCE_REGIONS_LITE_H
#define OPENRIDE_FRANCE_REGIONS_LITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Always-available lightweight regional coverage.
 *
 * The generated geometry is derived at development time from the same
 * Geofabrik .poly boundaries used by OpenRide's regional downloader.
 * Downloaded local .poly files remain the highest-precision source.
 */
typedef struct OpenRideFranceRegionsLiteRegionView {
    const char *id;
    uint32_t first_ring;
    uint32_t ring_count;
    double min_lon;
    double min_lat;
    double max_lon;
    double max_lat;
} OpenRideFranceRegionsLiteRegionView;

typedef struct OpenRideFranceRegionsLiteRingView {
    uint32_t point_offset;
    uint32_t point_count;
    bool hole;
} OpenRideFranceRegionsLiteRingView;

typedef struct OpenRideFranceRegionsLitePointView {
    double lon;
    double lat;
} OpenRideFranceRegionsLitePointView;

const char *openride_france_regions_lite_region_id(double lat, double lon);

bool openride_france_regions_lite_region_at(
    size_t index,
    OpenRideFranceRegionsLiteRegionView *region);
bool openride_france_regions_lite_ring_at(
    size_t index,
    OpenRideFranceRegionsLiteRingView *ring);
bool openride_france_regions_lite_point_at(
    size_t index,
    OpenRideFranceRegionsLitePointView *point);

size_t openride_france_regions_lite_region_count(void);
size_t openride_france_regions_lite_ring_count(void);
size_t openride_france_regions_lite_point_count(void);

#endif
