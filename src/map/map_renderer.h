#ifndef OPENRIDE_MAP_RENDERER_H
#define OPENRIDE_MAP_RENDERER_H

#include <SDL3/SDL.h>

#include "openride/map_camera.h"
#include "openride/mbtiles.h"

#include <stdbool.h>
#include <stdint.h>

#define OPENRIDE_TILE_CACHE_CAPACITY 256

typedef struct OpenRideTileCacheEntry {
    bool occupied;
    int zoom;
    int x;
    int y;
    SDL_Texture *texture;
    uint64_t last_used;
} OpenRideTileCacheEntry;

typedef struct OpenRideMapRenderer {
    SDL_Renderer *renderer;
    OpenRideMBTiles *map;
    OpenRideTileCacheEntry cache[OPENRIDE_TILE_CACHE_CAPACITY];
    uint64_t frame_counter;
} OpenRideMapRenderer;

bool openride_map_renderer_init(OpenRideMapRenderer *map_renderer,
                                SDL_Renderer *renderer,
                                OpenRideMBTiles *map);

void openride_map_renderer_destroy(OpenRideMapRenderer *map_renderer);

void openride_map_renderer_draw(OpenRideMapRenderer *map_renderer,
                                const OpenRideMapCamera *camera,
                                int viewport_width,
                                int viewport_height);

#endif
