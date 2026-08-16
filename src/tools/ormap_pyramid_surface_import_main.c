#include "openride/ormap_pyramid_surface.h"

#include <inttypes.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(
            stderr,
            "Usage: %s source.osm.pbf output.ormap11\n",
            argv[0]);
        return 2;
    }

    OpenRideORMapPyramidSurfaceBuildStats stats = {0};
    char error[512] = {0};

    printf("OpenRide ORMap v11 surface-pyramid builder\n");
    printf("  PBF    : %s\n", argv[1]);
    printf("  output : %s\n\n", argv[2]);

    if (!openride_ormap_pyramid_surface_build(
            argv[1],
            argv[2],
            "OpenRide v11 experimental region",
            &stats,
            error,
            sizeof(error))) {
        fprintf(stderr, "Build failed: %s\n", error);
        return 1;
    }

    printf("Surface pyramid complete.\n");
    printf("  OSM features     : %" PRIu64 "\n", stats.osm_features_seen);
    printf("  surface polygons : %" PRIu64 "\n", stats.surface_polygons_seen);
    printf("  built-up         : %" PRIu64 "\n", stats.builtup_polygons);
    printf("  water            : %" PRIu64 "\n", stats.water_polygons);
    printf("  green            : %" PRIu64 "\n", stats.green_polygons);
    printf("  building points ignored: %" PRIu64 "\n",
           stats.representative_building_points_ignored);
    printf("  triangulation skips: %" PRIu64 "\n",
           stats.triangulation_failures);
    printf("  partial triangles discarded: %" PRIu64 "\n",
           stats.triangulation_partial_triangles_discarded);

    for (int zoom = OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM;
         zoom <= OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM;
         ++zoom) {
        const int level =
            zoom - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM;

        printf(
            "  z%d: %" PRIu64 " triangles / %" PRIu64 " tiles\n",
            zoom,
            stats.triangles_by_zoom[level],
            stats.tiles_by_zoom[level]);
    }

    printf("  total triangles  : %" PRIu64 "\n", stats.triangles_total);
    printf("  total tiles      : %" PRIu64 "\n", stats.tiles_total);
    printf("  raw tile bytes   : %" PRIu64 "\n", stats.raw_bytes);
    printf("  compressed bytes : %" PRIu64 "\n", stats.compressed_bytes);

    printf("\nBuilding footprints z%d...\n",
           OPENRIDE_ORMAP_PYRAMID_BUILDING_ZOOM);

    OpenRideORMapPyramidBuildingBuildStats building = {0};

    if (!openride_ormap_pyramid_buildings_append(
            argv[1],
            argv[2],
            &building,
            error,
            sizeof(error))) {
        fprintf(
            stderr,
            "Building layer failed: %s\n",
            error);
        return 1;
    }

    printf("Building layer complete.\n");
    printf(
        "  OSM ways seen     : %" PRIu64 "\n",
        building.osm_ways_seen);
    printf(
        "  footprints seen   : %" PRIu64 "\n",
        building.footprints_seen);
    printf(
        "  footprints stored : %" PRIu64 "\n",
        building.footprints_stored);
    printf(
        "  tile polygons     : %" PRIu64 "\n",
        building.tile_polygons_stored);
    printf(
        "  vertices stored   : %" PRIu64 "\n",
        building.vertices_stored);
    printf(
        "  building tiles    : %" PRIu64 "\n",
        building.tiles_total);
    printf(
        "  tiny skipped      : %" PRIu64 "\n",
        building.skipped_too_small);
    printf(
        "  invalid polygons  : %" PRIu64 "\n",
        building.invalid_polygons);
    printf(
        "  raw bytes         : %" PRIu64 "\n",
        building.raw_bytes);
    printf(
        "  compressed bytes  : %" PRIu64 "\n",
        building.compressed_bytes);

    return 0;
}
