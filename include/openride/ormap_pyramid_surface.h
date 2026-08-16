#ifndef OPENRIDE_ORMAP_PYRAMID_SURFACE_H
#define OPENRIDE_ORMAP_PYRAMID_SURFACE_H

#include "openride/ormap_tile_pyramid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPENRIDE_ORMAP_PYRAMID_SURFACE_BLOB_VERSION 1U
#define OPENRIDE_ORMAP_PYRAMID_SURFACE_BUFFER_FRACTION (0.5 / 256.0)

/*
 * True building footprints are a separate close-view layer. One detailed z16
 * data level is overzoomed through z18; visibility remains a style/runtime
 * decision and can be disabled entirely while navigating.
 */
#define OPENRIDE_ORMAP_PYRAMID_BUILDING_ZOOM 16
#define OPENRIDE_ORMAP_PYRAMID_BUILDING_BLOB_VERSION 1U
#define OPENRIDE_ORMAP_PYRAMID_BUILDING_BUFFER_FRACTION (0.5 / 256.0)

typedef enum OpenRideORMapPyramidSurfaceKind {
    OPENRIDE_ORMAP_PYRAMID_SURFACE_BUILTUP = 1,
    OPENRIDE_ORMAP_PYRAMID_SURFACE_WATER = 2,
    OPENRIDE_ORMAP_PYRAMID_SURFACE_GREEN = 3
} OpenRideORMapPyramidSurfaceKind;

typedef struct OpenRideORMapPyramidSurfaceTriangle {
    uint16_t x1;
    uint16_t y1;
    uint16_t x2;
    uint16_t y2;
    uint16_t x3;
    uint16_t y3;
    uint8_t kind;
    uint8_t reserved;
} OpenRideORMapPyramidSurfaceTriangle;

typedef struct OpenRideORMapPyramidSurfaceBuildStats {
    uint64_t osm_features_seen;
    uint64_t surface_polygons_seen;
    uint64_t builtup_polygons;
    uint64_t water_polygons;
    uint64_t green_polygons;
    uint64_t representative_building_points_ignored;
    uint64_t invalid_or_small_polygons;
    uint64_t triangulation_failures;
    uint64_t triangulation_partial_triangles_discarded;

    uint64_t triangles_by_zoom[
        OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM
        + 1];
    uint64_t tiles_by_zoom[
        OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM
        + 1];

    uint64_t triangles_total;
    uint64_t tiles_total;
    uint64_t raw_bytes;
    uint64_t compressed_bytes;
} OpenRideORMapPyramidSurfaceBuildStats;

typedef struct OpenRideORMapPyramidBuildingBuildStats {
    uint64_t osm_ways_seen;
    uint64_t footprints_seen;
    uint64_t footprints_stored;
    uint64_t tile_polygons_stored;
    uint64_t vertices_stored;
    uint64_t tiles_total;
    uint64_t skipped_too_small;
    uint64_t invalid_polygons;
    uint64_t raw_bytes;
    uint64_t compressed_bytes;
} OpenRideORMapPyramidBuildingBuildStats;

/*
 * Build the experimental ORMap v11 surface pyramid.
 *
 * The output is a separate .ormap11 SQLite file. The stable v8 runtime does
 * not open it. WATER/GREEN/BUILTUP are emitted at every integer zoom z9..z14
 * from the same canonical OSM polygon source.
 */
bool openride_ormap_pyramid_surface_build(
    const char *pbf_path,
    const char *output_path,
    const char *region_name,
    OpenRideORMapPyramidSurfaceBuildStats *stats,
    char *error,
    size_t error_size);

/*
 * Append/replace the close-view building_tiles layer in an existing v11
 * .ormap11 surface pyramid. The input file must already be format_version=11.
 */
bool openride_ormap_pyramid_buildings_append(
    const char *pbf_path,
    const char *ormap11_path,
    OpenRideORMapPyramidBuildingBuildStats *stats,
    char *error,
    size_t error_size);


typedef struct OpenRideORMapPyramidSurfaceMetadata {
    char name[128];
    int format_version;
    int min_zoom;
    int max_zoom;
} OpenRideORMapPyramidSurfaceMetadata;

typedef struct OpenRideORMapPyramidSurfaceTile {
    OpenRideORMapPyramidSurfaceTriangle *triangles;
    uint32_t count;
} OpenRideORMapPyramidSurfaceTile;

typedef struct OpenRideORMapPyramidSurfaceInspectStats {
    uint64_t tiles_by_zoom[
        OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM
        + 1];
    uint64_t triangles_by_zoom[
        OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM
        + 1];
    uint64_t compressed_bytes_by_zoom[
        OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM
        + 1];
    uint64_t builtup_triangles_by_zoom[
        OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM
        + 1];
    uint64_t water_triangles_by_zoom[
        OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM
        + 1];
    uint64_t green_triangles_by_zoom[
        OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM
        + 1];
    uint32_t max_triangles_per_tile_by_zoom[
        OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM
        + 1];

    uint64_t tile_count;
    uint64_t triangle_count;
    uint64_t compressed_bytes;
    uint64_t malformed_tiles;
    uint64_t invalid_kinds;
    uint64_t invalid_payloads;

    uint64_t building_tiles;
    uint64_t building_tile_polygons;
    uint64_t building_vertices;
    uint64_t building_compressed_bytes;
    uint32_t max_buildings_per_tile;
    uint64_t malformed_building_tiles;
} OpenRideORMapPyramidSurfaceInspectStats;

typedef struct OpenRideORMapPyramidSurfaceMap
    OpenRideORMapPyramidSurfaceMap;

OpenRideORMapPyramidSurfaceMap *
openride_ormap_pyramid_surface_open(
    const char *path,
    char *error,
    size_t error_size);

void openride_ormap_pyramid_surface_close(
    OpenRideORMapPyramidSurfaceMap *map);

const OpenRideORMapPyramidSurfaceMetadata *
openride_ormap_pyramid_surface_metadata(
    const OpenRideORMapPyramidSurfaceMap *map);

bool openride_ormap_pyramid_surface_tile_exists(
    OpenRideORMapPyramidSurfaceMap *map,
    int zoom,
    int x,
    int y,
    bool *exists,
    char *error,
    size_t error_size);

bool openride_ormap_pyramid_surface_load_tile(
    OpenRideORMapPyramidSurfaceMap *map,
    int zoom,
    int x,
    int y,
    OpenRideORMapPyramidSurfaceTile *tile,
    char *error,
    size_t error_size);

void openride_ormap_pyramid_surface_tile_destroy(
    OpenRideORMapPyramidSurfaceTile *tile);

bool openride_ormap_pyramid_surface_inspect(
    const char *path,
    OpenRideORMapPyramidSurfaceInspectStats *stats,
    char *error,
    size_t error_size);

/* Internal-facing public helper used by the combined v11 inspector. */
bool openride_ormap_pyramid_buildings_inspect(
    const char *path,
    OpenRideORMapPyramidSurfaceInspectStats *stats,
    char *error,
    size_t error_size);

#endif
