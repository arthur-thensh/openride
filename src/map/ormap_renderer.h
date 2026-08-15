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
    /*
     * Runtime-only class index. record_order contains original record indexes
     * grouped by OpenRideRoadClass; class_offsets has one sentinel entry.
     * This avoids scanning hidden urban/path records every frame at low zoom.
     */
    uint32_t *record_order;
    uint32_t class_offsets[OPENRIDE_ROAD_OTHER + 2];
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

typedef struct OpenRideORMapRoadDebugStats {
    double roads_ms;
    double load_ms;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t prewarm_loads;
    uint32_t draw_loads;
    uint32_t deferred_loads;
    uint32_t tiles_visited;
    uint32_t segments_drawn;
    uint32_t batches;
    int prewarm_zoom;
} OpenRideORMapRoadDebugStats;

typedef struct OpenRideORMapAreaDebugStats {
    double areas_ms;
    double load_ms;
    uint32_t tiles_visited;
    uint32_t triangles_drawn;
    uint32_t batches;
    uint32_t mask_prewarm_loads;
} OpenRideORMapAreaDebugStats;

typedef struct OpenRideORMapRenderer {
    SDL_Renderer *renderer;
    OpenRideORMap *map;
    OpenRideMapStyle style;
    uint64_t frame_counter;
    OpenRideORMapRoadDebugStats road_debug;
    OpenRideORMapAreaDebugStats area_debug;
    uint32_t road_draw_load_budget_remaining;
    bool road_debug_active;
    bool area_debug_active;
    double road_previous_camera_zoom;
    bool road_has_previous_camera_zoom;
    int road_zoom_direction;
    OpenRideORMapRoadCacheEntry roads[OPENRIDE_ORMAP_ROAD_CACHE_CAPACITY];
    OpenRideORMapMaskCacheEntry masks[OPENRIDE_ORMAP_MASK_CACHE_CAPACITY];
    OpenRideORMapWaterCacheEntry waters[OPENRIDE_ORMAP_WATER_CACHE_CAPACITY];
    OpenRideORMapAreaCacheEntry areas[OPENRIDE_ORMAP_AREA_CACHE_CAPACITY];
    OpenRidePointD *label_world_positions;
    uint32_t label_world_position_count;
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
void openride_ormap_renderer_get_road_debug_stats(
    const OpenRideORMapRenderer *renderer,
    OpenRideORMapRoadDebugStats *stats);
void openride_ormap_renderer_get_area_debug_stats(
    const OpenRideORMapRenderer *renderer,
    OpenRideORMapAreaDebugStats *stats);
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
