#ifndef OPENRIDE_ORMAP_PYRAMID_OVERLAY_H
#define OPENRIDE_ORMAP_PYRAMID_OVERLAY_H

#include "openride/ormap.h"
#include "openride/ormap_tile_pyramid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPENRIDE_ORMAP_PYRAMID_OVERLAY_VERSION 1U
#define OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM 9
#define OPENRIDE_ORMAP_PYRAMID_ROAD_MAX_ZOOM 14
#define OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM 9
#define OPENRIDE_ORMAP_PYRAMID_WATER_MAX_ZOOM 13

typedef enum OpenRideORMapPyramidOverlayLayer {
    OPENRIDE_ORMAP_PYRAMID_OVERLAY_ROAD = 1,
    OPENRIDE_ORMAP_PYRAMID_OVERLAY_WATERWAY = 2
} OpenRideORMapPyramidOverlayLayer;

typedef struct OpenRideORMapPyramidOverlayLineRecord {
    uint16_t x1;
    uint16_t y1;
    uint16_t x2;
    uint16_t y2;
    uint8_t kind;
    uint8_t aux;
    uint16_t flags;
} OpenRideORMapPyramidOverlayLineRecord;

typedef struct OpenRideORMapPyramidOverlayLineTile {
    OpenRideORMapPyramidOverlayLineRecord *records;
    uint32_t count;
} OpenRideORMapPyramidOverlayLineTile;

typedef struct OpenRideORMapPyramidOverlayBuildStats {
    uint64_t road_tiles_by_zoom[
        OPENRIDE_ORMAP_PYRAMID_ROAD_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM + 1];
    uint64_t road_records_by_zoom[
        OPENRIDE_ORMAP_PYRAMID_ROAD_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM + 1];
    uint64_t water_tiles_by_zoom[
        OPENRIDE_ORMAP_PYRAMID_WATER_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM + 1];
    uint64_t water_records_by_zoom[
        OPENRIDE_ORMAP_PYRAMID_WATER_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM + 1];
    uint64_t labels;
    uint64_t raw_bytes;
    uint64_t compressed_bytes;
} OpenRideORMapPyramidOverlayBuildStats;

typedef struct OpenRideORMapPyramidOverlayInspectStats {
    uint64_t road_tiles_by_zoom[
        OPENRIDE_ORMAP_PYRAMID_ROAD_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM + 1];
    uint64_t road_records_by_zoom[
        OPENRIDE_ORMAP_PYRAMID_ROAD_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM + 1];
    uint64_t water_tiles_by_zoom[
        OPENRIDE_ORMAP_PYRAMID_WATER_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM + 1];
    uint64_t water_records_by_zoom[
        OPENRIDE_ORMAP_PYRAMID_WATER_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM + 1];
    uint64_t labels;
    uint64_t compressed_bytes;
    uint64_t malformed_tiles;
    uint64_t invalid_records;
} OpenRideORMapPyramidOverlayInspectStats;

typedef struct OpenRideORMapPyramidOverlayMap
    OpenRideORMapPyramidOverlayMap;

bool openride_ormap_pyramid_overlay_append(
    const char *ormap8_path,
    const char *ormap11_path,
    OpenRideORMapPyramidOverlayBuildStats *stats,
    char *error,
    size_t error_size);

OpenRideORMapPyramidOverlayMap *
openride_ormap_pyramid_overlay_open(
    const char *ormap11_path,
    char *error,
    size_t error_size);

void openride_ormap_pyramid_overlay_close(
    OpenRideORMapPyramidOverlayMap *map);

bool openride_ormap_pyramid_overlay_layer_available(
    const OpenRideORMapPyramidOverlayMap *map,
    OpenRideORMapPyramidOverlayLayer layer);

bool openride_ormap_pyramid_overlay_load_tile(
    OpenRideORMapPyramidOverlayMap *map,
    OpenRideORMapPyramidOverlayLayer layer,
    int zoom,
    int x,
    int y,
    OpenRideORMapPyramidOverlayLineTile *tile,
    char *error,
    size_t error_size);

void openride_ormap_pyramid_overlay_tile_destroy(
    OpenRideORMapPyramidOverlayLineTile *tile);

const OpenRideORMapLabel *
openride_ormap_pyramid_overlay_labels(
    const OpenRideORMapPyramidOverlayMap *map,
    uint32_t *count);

bool openride_ormap_pyramid_overlay_inspect(
    const char *ormap11_path,
    OpenRideORMapPyramidOverlayInspectStats *stats,
    char *error,
    size_t error_size);

#endif
