#ifndef OPENRIDE_VECTOR_MAP_RENDERER_H
#define OPENRIDE_VECTOR_MAP_RENDERER_H

#include <SDL3/SDL.h>

#include "openride/map_camera.h"
#include "openride/mbtiles.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPENRIDE_VECTOR_TILE_CACHE_CAPACITY 128

typedef struct OpenRideVectorTileCacheEntry {
    int zoom;
    int x;
    int y;
    unsigned char *bytes;
    size_t size;
    uint64_t last_used;
    bool occupied;
} OpenRideVectorTileCacheEntry;

typedef struct OpenRideVectorMapRenderer {
    SDL_Renderer *renderer;
    OpenRideMBTiles *map;
    uint64_t frame_counter;
    OpenRideVectorTileCacheEntry cache[OPENRIDE_VECTOR_TILE_CACHE_CAPACITY];
} OpenRideVectorMapRenderer;

bool openride_vector_map_renderer_init(OpenRideVectorMapRenderer *map_renderer,
                                       SDL_Renderer *renderer,
                                       OpenRideMBTiles *map);

void openride_vector_map_renderer_destroy(OpenRideVectorMapRenderer *map_renderer);

void openride_vector_map_renderer_draw(OpenRideVectorMapRenderer *map_renderer,
                                       const OpenRideMapCamera *camera,
                                       int viewport_width,
                                       int viewport_height);

#endif
