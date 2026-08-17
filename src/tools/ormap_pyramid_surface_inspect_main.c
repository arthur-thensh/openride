#include "openride/ormap_pyramid_surface.h"
#include "openride/ormap_pyramid_overlay.h"

#include <inttypes.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(
            stderr,
            "Usage: %s map.ormap11\n",
            argv[0]);
        return 2;
    }

    OpenRideORMapPyramidSurfaceInspectStats stats = {0};
    char error[512] = {0};

    if (!openride_ormap_pyramid_surface_inspect(
            argv[1],
            &stats,
            error,
            sizeof(error))) {
        fprintf(stderr, "Inspection failed: %s\n", error);
        return 1;
    }

    printf("OpenRide ORMap v11 surface-pyramid inspector\n");
    printf("  file: %s\n\n", argv[1]);

    for (int zoom = OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM;
         zoom <= OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM;
         ++zoom) {
        const int level =
            zoom - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM;

        const uint64_t tiles = stats.tiles_by_zoom[level];
        const uint64_t triangles =
            stats.triangles_by_zoom[level];

        const double avg =
            tiles ? (double)triangles / (double)tiles : 0.0;

        const double mib =
            (double)stats.compressed_bytes_by_zoom[level]
            / (1024.0 * 1024.0);

        printf(
            "z%d\n"
            "  tiles            : %" PRIu64 "\n"
            "  triangles        : %" PRIu64 "\n"
            "  triangles/tile   : %.1f avg / %u max\n"
            "  built-up         : %" PRIu64 "\n"
            "  water            : %" PRIu64 "\n"
            "  green            : %" PRIu64 "\n"
            "  compressed       : %.2f MiB\n",
            zoom,
            tiles,
            triangles,
            avg,
            stats.max_triangles_per_tile_by_zoom[level],
            stats.builtup_triangles_by_zoom[level],
            stats.water_triangles_by_zoom[level],
            stats.green_triangles_by_zoom[level],
            mib);
    }

    printf("\nTotals\n");
    printf("  tiles            : %" PRIu64 "\n", stats.tile_count);
    printf("  triangles        : %" PRIu64 "\n", stats.triangle_count);
    printf(
        "  compressed       : %.2f MiB\n",
        (double)stats.compressed_bytes
            / (1024.0 * 1024.0));
    printf("  malformed tiles  : %" PRIu64 "\n", stats.malformed_tiles);
    printf("  invalid kinds    : %" PRIu64 "\n", stats.invalid_kinds);
    printf("  invalid payloads : %" PRIu64 "\n", stats.invalid_payloads);

    printf("\nBuildings z%d\n",
           OPENRIDE_ORMAP_PYRAMID_BUILDING_ZOOM);
    printf(
        "  tiles            : %" PRIu64 "\n",
        stats.building_tiles);
    printf(
        "  tile polygons    : %" PRIu64 "\n",
        stats.building_tile_polygons);
    printf(
        "  vertices         : %" PRIu64 "\n",
        stats.building_vertices);
    printf(
        "  max polygons/tile: %u\n",
        stats.max_buildings_per_tile);
    printf(
        "  compressed       : %.2f MiB\n",
        (double)stats.building_compressed_bytes
            / (1024.0 * 1024.0));
    printf(
        "  malformed tiles  : %" PRIu64 "\n",
        stats.malformed_building_tiles);

    OpenRideORMapPyramidOverlayInspectStats overlay = {0};
    char overlay_error[256] = {0};
    if (openride_ormap_pyramid_overlay_inspect(
            argv[1],
            &overlay,
            overlay_error,
            sizeof(overlay_error))) {
        printf("\nV3.9 overlay roads\n");
        for (int zoom = OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM;
             zoom <= OPENRIDE_ORMAP_PYRAMID_ROAD_MAX_ZOOM;
             ++zoom) {
            const int i = zoom - OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM;
            printf(
                "  z%d: %" PRIu64 " records / %" PRIu64 " tiles\n",
                zoom,
                overlay.road_records_by_zoom[i],
                overlay.road_tiles_by_zoom[i]);
        }

        printf("\nV3.9 overlay waterways\n");
        for (int zoom = OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM;
             zoom <= OPENRIDE_ORMAP_PYRAMID_WATER_MAX_ZOOM;
             ++zoom) {
            const int i = zoom - OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM;
            printf(
                "  z%d: %" PRIu64 " records / %" PRIu64 " tiles\n",
                zoom,
                overlay.water_records_by_zoom[i],
                overlay.water_tiles_by_zoom[i]);
        }
        printf("  labels           : %" PRIu64 "\n", overlay.labels);
        printf(
            "  compressed       : %.2f MiB\n",
            (double)overlay.compressed_bytes / (1024.0 * 1024.0));
        printf(
            "  malformed tiles  : %" PRIu64 "\n",
            overlay.malformed_tiles);
        printf(
            "  invalid records  : %" PRIu64 "\n",
            overlay.invalid_records);
    } else {
        printf(
            "\nV3.9 overlay: not present (%s)\n",
            overlay_error[0] ? overlay_error : "unknown");
    }

    return 0;
}
