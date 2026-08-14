#ifndef OPENRIDE_FRANCE_REGIONS_LITE_H
#define OPENRIDE_FRANCE_REGIONS_LITE_H

#include <stddef.h>

/*
 * Always-available lightweight regional coverage.
 *
 * The generated geometry is derived at development time from the same
 * Geofabrik .poly boundaries used by OpenRide's regional downloader.
 * Downloaded local .poly files remain the highest-precision source.
 */
const char *openride_france_regions_lite_region_id(double lat, double lon);

size_t openride_france_regions_lite_region_count(void);
size_t openride_france_regions_lite_ring_count(void);
size_t openride_france_regions_lite_point_count(void);

#endif
