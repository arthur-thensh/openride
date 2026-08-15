#ifndef OPENRIDE_ORMAP_H
#define OPENRIDE_ORMAP_H

#include "openride/routing_graph.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPENRIDE_ORMAP_FORMAT_VERSION 8U
/* v6 stores four semantic road LODs instead of regenerating every zoom. */
#define OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM 8
#define OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM 10
#define OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM 12
#define OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM 14
#define OPENRIDE_ORMAP_MIN_ROAD_ZOOM OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM
/* Road geometry is detailed enough at z14 and is scaled above that zoom. */
#define OPENRIDE_ORMAP_ROAD_DATA_MAX_ZOOM OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM
#define OPENRIDE_ORMAP_MAX_ZOOM 16
/*
 * v8 stores semantic masks at three LODs. z14 is the lightweight regional
 * source used when masks first appear, z15 takes over through medium zooms,
 * and z16 remains the detailed source for close views. Every level keeps the
 * compact 32x32 ORM1 tile encoding, so the runtime cache representation does
 * not grow when another LOD is added.
 */
#define OPENRIDE_ORMAP_MASK_REGIONAL_ZOOM 14
#define OPENRIDE_ORMAP_MASK_OVERVIEW_ZOOM 15
#define OPENRIDE_ORMAP_MASK_DETAIL_ZOOM 16
#define OPENRIDE_ORMAP_MASK_ZOOM OPENRIDE_ORMAP_MASK_DETAIL_ZOOM
#define OPENRIDE_ORMAP_MASK_GRID 32
/* Waterways are vector lines and can be scaled cleanly above z13. */
#define OPENRIDE_ORMAP_WATER_ZOOM 13
/* Generalized landcover is stored at three vector LODs. Relative to a
 * z11 footprint these correspond to effective 32/64/128-cell grids. */
#define OPENRIDE_ORMAP_AREA_REGIONAL_ZOOM 9
#define OPENRIDE_ORMAP_AREA_OVERVIEW_ZOOM 10
#define OPENRIDE_ORMAP_AREA_LOCAL_ZOOM 11
/* Compatibility metadata key used by v3/v4 readers: local is the former coarse LOD. */
#define OPENRIDE_ORMAP_AREA_COARSE_ZOOM OPENRIDE_ORMAP_AREA_LOCAL_ZOOM
#define OPENRIDE_ORMAP_AREA_DETAIL_ZOOM 14
/* Half a pixel of overlap at the area data zoom prevents tile-edge seams. */
#define OPENRIDE_ORMAP_AREA_BUFFER_FRACTION (0.5 / 256.0)

/* Compatibility alias used by older callers/tests. */
#define OPENRIDE_ORMAP_MAX_ROAD_ZOOM OPENRIDE_ORMAP_ROAD_DATA_MAX_ZOOM

typedef struct OpenRideORMap OpenRideORMap;

typedef struct OpenRideORMapMetadata {
    char name[128];
    char attribution[256];
    int format_version;
    int min_zoom;
    int max_zoom;
    int road_max_zoom;
    int mask_zoom;
    int water_zoom;
    int area_coarse_zoom;
    int area_detail_zoom;
    bool has_center;
    double center_lat;
    double center_lon;
    double center_zoom;
    bool has_bounds;
    double west;
    double south;
    double east;
    double north;
} OpenRideORMapMetadata;

typedef struct OpenRideORMapRoadRecord {
    uint16_t x1;
    uint16_t y1;
    uint16_t x2;
    uint16_t y2;
    uint8_t road_class;
    uint8_t surface;
    uint16_t flags;
} OpenRideORMapRoadRecord;

typedef struct OpenRideORMapRoadTile {
    OpenRideORMapRoadRecord *records;
    uint32_t count;
} OpenRideORMapRoadTile;

typedef enum OpenRideORMapWaterwayKind {
    OPENRIDE_ORMAP_WATERWAY_RIVER = 1,
    OPENRIDE_ORMAP_WATERWAY_CANAL = 2,
    OPENRIDE_ORMAP_WATERWAY_STREAM = 3,
    OPENRIDE_ORMAP_WATERWAY_DRAIN = 4
} OpenRideORMapWaterwayKind;

typedef struct OpenRideORMapWaterRecord {
    uint16_t x1;
    uint16_t y1;
    uint16_t x2;
    uint16_t y2;
    uint8_t kind;
    uint8_t reserved;
} OpenRideORMapWaterRecord;

typedef struct OpenRideORMapWaterTile {
    OpenRideORMapWaterRecord *records;
    uint32_t count;
} OpenRideORMapWaterTile;

typedef enum OpenRideORMapAreaKind {
    OPENRIDE_ORMAP_AREA_BUILTUP = 1,
    OPENRIDE_ORMAP_AREA_WATER = 2,
    OPENRIDE_ORMAP_AREA_GREEN = 3
} OpenRideORMapAreaKind;

/*
 * Filled areas are pre-triangulated by the builder. Coordinates are quantized
 * in a slightly buffered tile-local domain so adjacent tiles overlap by half
 * a data-zoom pixel instead of exposing hairline seams while rotating/scaling.
 * In v5 this representation is used for water and generalized overview landcover;
 * detailed built-up coverage stays in the semantic mask instead.
 */
typedef struct OpenRideORMapAreaTriangle {
    uint16_t x1;
    uint16_t y1;
    uint16_t x2;
    uint16_t y2;
    uint16_t x3;
    uint16_t y3;
    uint8_t kind;
    uint8_t reserved;
} OpenRideORMapAreaTriangle;

typedef struct OpenRideORMapAreaTile {
    OpenRideORMapAreaTriangle *triangles;
    uint32_t count;
} OpenRideORMapAreaTile;

typedef struct OpenRideORMapMaskTile {
    uint8_t grid_size;
    unsigned char *builtup;
    unsigned char *water;
    unsigned char *forest;
    size_t layer_bytes;
} OpenRideORMapMaskTile;

typedef enum OpenRideORMapLabelLOD {
    OPENRIDE_ORMAP_LABEL_LOD_REGIONAL = 0,
    OPENRIDE_ORMAP_LABEL_LOD_OVERVIEW = 1,
    OPENRIDE_ORMAP_LABEL_LOD_LOCAL = 2,
    OPENRIDE_ORMAP_LABEL_LOD_DETAIL = 3
} OpenRideORMapLabelLOD;

typedef struct OpenRideORMapLabel {
    int32_t lat_e7;
    int32_t lon_e7;
    int kind;
    int rank;
    uint8_t lod;
    char name[96];
} OpenRideORMapLabel;

typedef struct OpenRideORMapTileCoord {
    int x;
    int y;
} OpenRideORMapTileCoord;

typedef enum OpenRideORMapTileLayer {
    OPENRIDE_ORMAP_TILE_LAYER_ROAD = 0,
    OPENRIDE_ORMAP_TILE_LAYER_WATER,
    OPENRIDE_ORMAP_TILE_LAYER_AREA,
    OPENRIDE_ORMAP_TILE_LAYER_MASK
} OpenRideORMapTileLayer;

typedef struct OpenRideORMapBuildStats {
    uint64_t routing_segments_seen;
    uint64_t road_records_written;
    uint64_t road_tiles_written;
    uint64_t road_regional_records;
    uint64_t road_overview_records;
    uint64_t road_local_records;
    uint64_t road_detail_records;
    uint64_t map_features_seen;
    uint64_t map_relations_seen;
    uint64_t multipolygon_relations;
    uint64_t multipolygon_outer_rings;
    uint64_t incomplete_multipolygons;
    uint64_t multipolygon_inner_members_ignored;
    uint64_t builtup_polygons;
    uint64_t builtup_contours;
    uint64_t water_polygons;
    uint64_t forest_polygons;
    uint64_t waterway_features;
    uint64_t water_records_written;
    uint64_t water_tiles_written;
    uint64_t area_triangles_written;
    uint64_t area_tiles_written;
    uint64_t landcover_regional_triangles;
    uint64_t landcover_overview_triangles;
    uint64_t landcover_local_triangles;
    uint64_t area_polygons_skipped;
    uint64_t mask_tiles_written;
    uint64_t labels_written;
    uint64_t label_regional_count;
    uint64_t label_overview_count;
    uint64_t label_local_count;
    uint64_t label_detail_count;
} OpenRideORMapBuildStats;

OpenRideORMap *openride_ormap_open(const char *path,
                                   char *error,
                                   size_t error_size);
void openride_ormap_close(OpenRideORMap *map);
const OpenRideORMapMetadata *openride_ormap_metadata(const OpenRideORMap *map);
bool openride_ormap_list_tiles(OpenRideORMap *map,
                               OpenRideORMapTileLayer layer,
                               int zoom,
                               OpenRideORMapTileCoord **coords,
                               uint32_t *count,
                               char *error,
                               size_t error_size);
void openride_ormap_tile_coords_destroy(OpenRideORMapTileCoord *coords);
bool openride_ormap_load_road_tile(OpenRideORMap *map,
                                   int zoom,
                                   int x,
                                   int y,
                                   OpenRideORMapRoadTile *tile,
                                   char *error,
                                   size_t error_size);
void openride_ormap_road_tile_destroy(OpenRideORMapRoadTile *tile);

bool openride_ormap_load_water_tile(OpenRideORMap *map,
                                    int zoom,
                                    int x,
                                    int y,
                                    OpenRideORMapWaterTile *tile,
                                    char *error,
                                    size_t error_size);
void openride_ormap_water_tile_destroy(OpenRideORMapWaterTile *tile);

bool openride_ormap_load_area_tile(OpenRideORMap *map,
                                   int zoom,
                                   int x,
                                   int y,
                                   OpenRideORMapAreaTile *tile,
                                   char *error,
                                   size_t error_size);
void openride_ormap_area_tile_destroy(OpenRideORMapAreaTile *tile);

bool openride_ormap_load_mask_tile(OpenRideORMap *map,
                                   int zoom,
                                   int x,
                                   int y,
                                   OpenRideORMapMaskTile *tile,
                                   char *error,
                                   size_t error_size);
void openride_ormap_mask_tile_destroy(OpenRideORMapMaskTile *tile);

const OpenRideORMapLabel *openride_ormap_labels(const OpenRideORMap *map,
                                                 uint32_t *count);

/*
 * Build OpenRide's compact map from the same regional inputs used by routing
 * and search. Roads and waterways remain vector lines. Water surfaces remain
 * compact vector triangles. v8 stores generalized landcover plus three
 * semantic mask LODs: z14 regional, z15 overview and z16 detail. Individual
 * building footprints are still not stored.
 */
bool openride_ormap_build(const char *pbf_path,
                          const char *routing_graph_path,
                          const char *places_database_path,
                          const char *output_path,
                          const char *region_name,
                          OpenRideORMapBuildStats *stats,
                          char *error,
                          size_t error_size);

#endif
