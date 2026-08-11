#ifndef OPENRIDE_MBTILES_H
#define OPENRIDE_MBTILES_H

#include <stdbool.h>
#include <stddef.h>

typedef struct OpenRideMBTiles OpenRideMBTiles;

typedef struct OpenRideMBTilesMetadata {
    char name[128];
    char format[32];
    char attribution[256];

    int min_zoom;
    int max_zoom;

    bool has_center;
    double center_lat;
    double center_lon;
    double center_zoom;

    bool has_bounds;
    double west;
    double south;
    double east;
    double north;
} OpenRideMBTilesMetadata;

typedef struct OpenRideTileData {
    unsigned char *bytes;
    size_t size;
} OpenRideTileData;

OpenRideMBTiles *openride_mbtiles_open(const char *path,
                                       char *error,
                                       size_t error_size);

void openride_mbtiles_close(OpenRideMBTiles *map);

const OpenRideMBTilesMetadata *openride_mbtiles_metadata(const OpenRideMBTiles *map);

/*
 * Load one tile using XYZ coordinates (origin at the north-west corner).
 * MBTiles stores tile_row in TMS order, so the conversion is handled here.
 * Returns true only when a tile exists and has been copied into out_tile.
 */
bool openride_mbtiles_load_tile(OpenRideMBTiles *map,
                                int zoom,
                                int x,
                                int y_xyz,
                                OpenRideTileData *out_tile,
                                char *error,
                                size_t error_size);

void openride_tile_data_free(OpenRideTileData *tile);

#endif
