#include "openride/ormap_pyramid_overlay.h"

#include <inttypes.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s stable.ormap target.ormap11\n", argv[0]);
        return 2;
    }

    OpenRideORMapPyramidOverlayBuildStats stats = {0};
    char error[512] = {0};
    printf("OpenRide ORMap v11 complete overlay append\n");
    printf("  source v8 : %s\n", argv[1]);
    printf("  target v11: %s\n\n", argv[2]);

    if (!openride_ormap_pyramid_overlay_append(
            argv[1], argv[2], &stats, error, sizeof(error))) {
        fprintf(stderr, "Overlay append failed: %s\n", error);
        return 1;
    }

    printf("Road pyramid\n");
    for (int zoom = OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM;
         zoom <= OPENRIDE_ORMAP_PYRAMID_ROAD_MAX_ZOOM;
         ++zoom) {
        const int i = zoom - OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM;
        printf("  z%d: %" PRIu64 " records / %" PRIu64 " tiles\n",
               zoom,
               stats.road_records_by_zoom[i],
               stats.road_tiles_by_zoom[i]);
    }

    printf("\nWaterway pyramid\n");
    for (int zoom = OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM;
         zoom <= OPENRIDE_ORMAP_PYRAMID_WATER_MAX_ZOOM;
         ++zoom) {
        const int i = zoom - OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM;
        printf("  z%d: %" PRIu64 " records / %" PRIu64 " tiles\n",
               zoom,
               stats.water_records_by_zoom[i],
               stats.water_tiles_by_zoom[i]);
    }

    printf("\nLabels             : %" PRIu64 "\n", stats.labels);
    printf("Raw line bytes      : %" PRIu64 "\n", stats.raw_bytes);
    printf("Compressed bytes    : %" PRIu64 "\n", stats.compressed_bytes);
    return 0;
}
