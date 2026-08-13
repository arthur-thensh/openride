#ifndef OPENRIDE_ORMAP_RENDERER_H
#define OPENRIDE_ORMAP_RENDERER_H

#include <SDL3/SDL.h>

#include "openride/map_camera.h"
#include "openride/map_style.h"
#include "openride/ormap.h"

#include <stdbool.h>
#include <stdint.h>

#define OPENRIDE_ORMAP_ROAD_CACHE_CAPACITY 192
#define OPENRIDE_ORMAP_MASK_CACHE_CAPACITY 2048
#define OPENRIDE_ORMAP_WATER_CACHE_CAPACITY 512
#define OPENRIDE_ORMAP_AREA_CACHE_CAPACITY 512

typedef enum OpenRideORMapRenderLayer {
    OPENRIDE_ORMAP_RENDER_LAYER_MASKS = 0,
    OPENRIDE_ORMAP_RENDER_LAYER_AREAS,
    OPENRIDE_ORMAP_RENDER_LAYER_WATERWAYS,
    OPENRIDE_ORMAP_RENDER_LAYER_ROADS,
    OPENRIDE_ORMAP_RENDER_LAYER_LABELS
} OpenRideORMapRenderLayer;

typedef struct OpenRideORMapRoadCacheEntry {
    bool occupied;
    int zoom;
    int x;
    int y;
    uint64_t last_used;
    OpenRideORMapRoadTile tile;
} OpenRideORMapRoadCacheEntry;

typedef struct OpenRideORMapMaskCacheEntry {
    bool occupied;
    int zoom;
    int x;
    int y;
    uint64_t last_used;
    OpenRideORMapMaskTile tile;
} OpenRideORMapMaskCacheEntry;

typedef struct OpenRideORMapWaterCacheEntry {
    bool occupied;
    int zoom;
    int x;
    int y;
    uint64_t last_used;
    OpenRideORMapWaterTile tile;
} OpenRideORMapWaterCacheEntry;

typedef struct OpenRideORMapAreaCacheEntry {
    bool occupied;
    int zoom;
    int x;
    int y;
    uint64_t last_used;
    OpenRideORMapAreaTile tile;
} OpenRideORMapAreaCacheEntry;

typedef struct OpenRideORMapRenderer {
    SDL_Renderer *renderer;
    OpenRideORMap *map;
    OpenRideMapStyle style;
    uint64_t frame_counter;
    OpenRideORMapRoadCacheEntry roads[OPENRIDE_ORMAP_ROAD_CACHE_CAPACITY];
    OpenRideORMapMaskCacheEntry masks[OPENRIDE_ORMAP_MASK_CACHE_CAPACITY];
    OpenRideORMapWaterCacheEntry waters[OPENRIDE_ORMAP_WATER_CACHE_CAPACITY];
    OpenRideORMapAreaCacheEntry areas[OPENRIDE_ORMAP_AREA_CACHE_CAPACITY];
    SDL_Vertex *area_vertices;
    int *area_indices;
    uint32_t area_vertex_capacity;
    uint32_t area_index_capacity;
} OpenRideORMapRenderer;

bool openride_ormap_renderer_init(OpenRideORMapRenderer *renderer,
                                  SDL_Renderer *sdl_renderer,
                                  OpenRideORMap *map);
void openride_ormap_renderer_destroy(OpenRideORMapRenderer *renderer);
void openride_ormap_renderer_set_style(OpenRideORMapRenderer *renderer,
                                       OpenRideMapStyle style);
void openride_ormap_renderer_begin_frame(OpenRideORMapRenderer *renderer);
void openride_ormap_renderer_draw_layer(OpenRideORMapRenderer *renderer,
                                        const OpenRideMapCamera *camera,
                                        int viewport_width,
                                        int viewport_height,
                                        OpenRideORMapRenderLayer layer);
void openride_ormap_renderer_draw(OpenRideORMapRenderer *renderer,
                                  const OpenRideMapCamera *camera,
                                  int viewport_width,
                                  int viewport_height);

#endif
