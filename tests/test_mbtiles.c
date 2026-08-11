#include "openride/mbtiles.h"
#include "openride/map_camera.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    assert(argc == 2);

    char error[256] = {0};
    OpenRideMBTiles *map = openride_mbtiles_open(argv[1], error, sizeof(error));
    if (!map) {
        fprintf(stderr, "open failed: %s\n", error);
        return 1;
    }

    const OpenRideMBTilesMetadata *metadata = openride_mbtiles_metadata(map);
    assert(metadata != NULL);
    assert(metadata->min_zoom == 6);
    assert(metadata->max_zoom == 8);
    assert(metadata->has_center);

    const int zoom = 7;
    const int tile_count = 1 << zoom;
    const OpenRidePointD p = openride_mercator_forward(metadata->center_lat,
                                                        metadata->center_lon);
    const int x = (int)floor(p.x * (double)tile_count);
    const int y = (int)floor(p.y * (double)tile_count);

    OpenRideTileData tile = {0};
    const bool found = openride_mbtiles_load_tile(map,
                                                   zoom,
                                                   x,
                                                   y,
                                                   &tile,
                                                   error,
                                                   sizeof(error));
    assert(found);
    assert(tile.bytes != NULL);
    assert(tile.size > 8);

    /* PNG signature. */
    assert(tile.bytes[0] == 0x89);
    assert(tile.bytes[1] == 0x50);
    assert(tile.bytes[2] == 0x4E);
    assert(tile.bytes[3] == 0x47);

    openride_tile_data_free(&tile);
    openride_mbtiles_close(map);

    puts("test_mbtiles: OK");
    return 0;
}
