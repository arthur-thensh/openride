#ifndef OPENRIDE_MAP_WORLD_H
#define OPENRIDE_MAP_WORLD_H

#include <SDL3/SDL.h>

#include "map/ormap_renderer.h"
#include "openride/map_camera.h"
#include "openride/map_style.h"
#include "openride/platform_paths.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define OPENRIDE_MAP_WORLD_MIN_ZOOM 6.0
#define OPENRIDE_MAP_WORLD_DETAIL_ZOOM 10.0
#define OPENRIDE_MAP_WORLD_MAX_OVERVIEW_ZOOM 11.30

/*
 * Mask Geometry Cache V2
 * ----------------------
 * ORMap's v1 cache already compiles every 32x32 semantic bitmap into merged
 * rectangles. On a north-up map the old path still rebuilt four SDL vertices
 * and six indices for every cached rectangle on every frame. At z13-z14 this
 * can mean tens of thousands of quads.
 *
 * MapWorld can use the public renderer cache directly, so keep the original
 * ORMap renderer as the authoritative fallback and add a conservative fast
 * path here:
 *   - ORMap v6 only;
 *   - bearing must be exactly north-up;
 *   - every visible mask tile must already be cached and compiled;
 *   - every visible Area Detail tile must already be cached.
 *
 * If any condition is not met we call the original renderer unchanged. When
 * the fast path is valid, SDL_RenderFillRects receives the cached rectangles
 * directly, avoiding transient vertex/index generation. Forest is submitted
 * before built-up so cartographic layer order remains deterministic across
 * tile boundaries.
 */
#define OPENRIDE_MAP_WORLD_CACHE_ASSOCIATIVITY 4U
#define OPENRIDE_MAP_WORLD_MASK_TILE_SIZE 256.0
#define OPENRIDE_MAP_WORLD_BUILTUP_START 13.15
#define OPENRIDE_MAP_WORLD_BUILTUP_FULL 13.35
#define OPENRIDE_MAP_WORLD_BUILTUP_DETAIL_START 14.00
#define OPENRIDE_MAP_WORLD_BUILTUP_DETAIL_END 14.55
#define OPENRIDE_MAP_WORLD_FOREST_START 13.40
#define OPENRIDE_MAP_WORLD_FOREST_FULL 14.40
#define OPENRIDE_MAP_WORLD_MASK_MAX_ZOOM 17.20

typedef struct OpenRideMapWorldMaskViewport {
    int zoom;
    int tile_count;
    int first_x;
    int last_x;
    int first_y;
    int last_y;
    double tile_size;
    double center_x;
    double center_y;
} OpenRideMapWorldMaskViewport;

static inline double openride_map_world_mask_smoothstep(double zoom,
                                                         double start,
                                                         double end)
{
    if (zoom <= start) return 0.0;
    if (zoom >= end) return 1.0;
    const double t = (zoom - start) / (end - start);
    return t * t * (3.0 - 2.0 * t);
}

static inline uint8_t openride_map_world_mask_scaled_alpha(uint8_t alpha,
                                                            double factor)
{
    if (factor <= 0.0) return 0U;
    if (factor >= 1.0) return alpha;
    return (uint8_t)lround((double)alpha * factor);
}

static inline int openride_map_world_wrap_x(int x, int count)
{
    int wrapped = x % count;
    if (wrapped < 0) wrapped += count;
    return wrapped;
}

static inline uint32_t openride_map_world_cache_hash(int zoom, int x, int y)
{
    uint32_t value = (uint32_t)zoom * UINT32_C(0x9e3779b9);
    value ^= (uint32_t)x * UINT32_C(0x85ebca6b);
    value ^= (uint32_t)y * UINT32_C(0xc2b2ae35);
    value ^= value >> 16U;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15U;
    return value;
}

static inline size_t openride_map_world_cache_set_base(size_t capacity,
                                                        int zoom,
                                                        int x,
                                                        int y)
{
    const size_t set_count = capacity / OPENRIDE_MAP_WORLD_CACHE_ASSOCIATIVITY;
    return (size_t)(openride_map_world_cache_hash(zoom, x, y)
                    % (uint32_t)set_count)
        * OPENRIDE_MAP_WORLD_CACHE_ASSOCIATIVITY;
}

static inline OpenRideORMapMaskCacheEntry *openride_map_world_mask_cache_find(
    OpenRideORMapRenderer *renderer,
    int zoom,
    int x,
    int y)
{
    if (!renderer) return NULL;
    const size_t base = openride_map_world_cache_set_base(
        OPENRIDE_ORMAP_MASK_CACHE_CAPACITY, zoom, x, y);
    for (size_t i = 0U; i < OPENRIDE_MAP_WORLD_CACHE_ASSOCIATIVITY; ++i) {
        OpenRideORMapMaskCacheEntry *entry = &renderer->masks[base + i];
        if (entry->occupied && entry->zoom == zoom && entry->x == x && entry->y == y) {
            return entry;
        }
    }
    return NULL;
}

static inline bool openride_map_world_area_cache_contains(
    const OpenRideORMapRenderer *renderer,
    int zoom,
    int x,
    int y)
{
    if (!renderer) return false;
    const size_t base = openride_map_world_cache_set_base(
        OPENRIDE_ORMAP_AREA_CACHE_CAPACITY, zoom, x, y);
    for (size_t i = 0U; i < OPENRIDE_MAP_WORLD_CACHE_ASSOCIATIVITY; ++i) {
        const OpenRideORMapAreaCacheEntry *entry = &renderer->areas[base + i];
        if (entry->occupied && entry->zoom == zoom && entry->x == x && entry->y == y) {
            return true;
        }
    }
    return false;
}

static inline bool openride_map_world_mask_viewport(
    const OpenRideMapCamera *camera,
    int width,
    int height,
    int zoom,
    OpenRideMapWorldMaskViewport *viewport)
{
    if (!camera || !viewport || width <= 0 || height <= 0 || zoom < 0 || zoom > 30) {
        return false;
    }
    const int tile_count = 1 << zoom;
    const double scale = pow(2.0, camera->zoom - zoom);
    const double tile_size = OPENRIDE_MAP_WORLD_MASK_TILE_SIZE * scale;
    if (!isfinite(tile_size) || tile_size <= 0.0) return false;
    const OpenRidePointD center =
        openride_mercator_forward(camera->center_lat, camera->center_lon);
    const double world_size = tile_size * tile_count;
    const double center_x = center.x * world_size;
    const double center_y = center.y * world_size;
    viewport->zoom = zoom;
    viewport->tile_count = tile_count;
    viewport->tile_size = tile_size;
    viewport->center_x = center_x;
    viewport->center_y = center_y;
    viewport->first_x = (int)floor((center_x - width * 0.5) / tile_size);
    viewport->last_x = (int)floor((center_x + width * 0.5) / tile_size);
    viewport->first_y = (int)floor((center_y - height * 0.5) / tile_size);
    viewport->last_y = (int)floor((center_y + height * 0.5) / tile_size);
    return true;
}

static inline bool openride_map_world_area_detail_cache_ready(
    const OpenRideORMapRenderer *renderer,
    const OpenRideMapCamera *camera,
    int width,
    int height,
    int zoom)
{
    OpenRideMapWorldMaskViewport viewport;
    if (!openride_map_world_mask_viewport(camera, width, height, zoom, &viewport)) {
        return false;
    }
    for (int ty = viewport.first_y; ty <= viewport.last_y; ++ty) {
        if (ty < 0 || ty >= viewport.tile_count) continue;
        for (int tx = viewport.first_x; tx <= viewport.last_x; ++tx) {
            const int qx = openride_map_world_wrap_x(tx, viewport.tile_count);
            if (!openride_map_world_area_cache_contains(renderer, zoom, qx, ty)) {
                return false;
            }
        }
    }
    return true;
}

static inline bool openride_map_world_mask_cache_ready(
    OpenRideORMapRenderer *renderer,
    const OpenRideMapWorldMaskViewport *viewport,
    bool draw_forest,
    bool draw_builtup,
    uint32_t *tile_total,
    uint32_t *forest_rect_total,
    uint32_t *builtup_rect_total)
{
    if (!renderer || !viewport || !tile_total
        || !forest_rect_total || !builtup_rect_total) {
        return false;
    }
    uint64_t tiles = 0U;
    uint64_t forest_rects = 0U;
    uint64_t builtup_rects = 0U;
    for (int ty = viewport->first_y; ty <= viewport->last_y; ++ty) {
        if (ty < 0 || ty >= viewport->tile_count) continue;
        for (int tx = viewport->first_x; tx <= viewport->last_x; ++tx) {
            const int qx = openride_map_world_wrap_x(tx, viewport->tile_count);
            OpenRideORMapMaskCacheEntry *entry =
                openride_map_world_mask_cache_find(renderer, viewport->zoom, qx, ty);
            if (!entry || !entry->geometry_compiled) return false;
            entry->last_used = renderer->frame_counter;
            ++tiles;
            if (draw_forest) forest_rects += entry->forest_rect_count;
            if (draw_builtup) builtup_rects += entry->builtup_rect_count;
            if (tiles > UINT32_MAX
                || forest_rects > UINT32_MAX
                || builtup_rects > UINT32_MAX) {
                return false;
            }
        }
    }
    *tile_total = (uint32_t)tiles;
    *forest_rect_total = (uint32_t)forest_rects;
    *builtup_rect_total = (uint32_t)builtup_rects;
    return true;
}

static inline bool openride_map_world_mask_scratch_reserve(
    SDL_FRect **scratch,
    uint32_t *capacity,
    uint32_t needed)
{
    if (!scratch || !capacity) return false;
    if (needed <= *capacity) return true;
    uint32_t next = *capacity == 0U ? 4096U : *capacity;
    while (next < needed) {
        if (next > UINT32_MAX / 2U) {
            next = needed;
            break;
        }
        next *= 2U;
    }
    SDL_FRect *grown = realloc(*scratch, (size_t)next * sizeof(*grown));
    if (!grown) return false;
    *scratch = grown;
    *capacity = next;
    return true;
}

static inline uint32_t openride_map_world_collect_mask_rects(
    OpenRideORMapRenderer *renderer,
    const OpenRideMapWorldMaskViewport *viewport,
    int width,
    int height,
    bool forest_layer,
    SDL_FRect *scratch)
{
    if (!renderer || !viewport || !scratch) return 0U;
    uint32_t written = 0U;
    for (int ty = viewport->first_y; ty <= viewport->last_y; ++ty) {
        if (ty < 0 || ty >= viewport->tile_count) continue;
        for (int tx = viewport->first_x; tx <= viewport->last_x; ++tx) {
            const int qx = openride_map_world_wrap_x(tx, viewport->tile_count);
            OpenRideORMapMaskCacheEntry *entry =
                openride_map_world_mask_cache_find(renderer, viewport->zoom, qx, ty);
            if (!entry || !entry->geometry_compiled || entry->tile.grid_size == 0U) {
                continue;
            }
            const OpenRideMaskRect *rects =
                forest_layer ? entry->forest_rects : entry->builtup_rects;
            const uint32_t rect_count =
                forest_layer ? entry->forest_rect_count : entry->builtup_rect_count;
            const double cell = viewport->tile_size / (double)entry->tile.grid_size;
            const double tile_left =
                width * 0.5 + tx * viewport->tile_size - viewport->center_x;
            const double tile_top =
                height * 0.5 + ty * viewport->tile_size - viewport->center_y;
            for (uint32_t i = 0U; i < rect_count; ++i) {
                const OpenRideMaskRect *rect = &rects[i];
                const float left = (float)(tile_left + rect->x0 * cell);
                const float top = (float)(tile_top + rect->y0 * cell);
                const float right = (float)(tile_left + rect->x1 * cell + 0.5);
                const float bottom = (float)(tile_top + rect->y1 * cell + 0.5);
                scratch[written++] = (SDL_FRect){
                    left,
                    top,
                    right - left,
                    bottom - top
                };
            }
        }
    }
    return written;
}

static inline OpenRideMapColor openride_map_world_forest_color(OpenRideMapStyle style)
{
    if (style == OPENRIDE_MAP_STYLE_TOPO) {
        return (OpenRideMapColor){180, 203, 170, 210};
    }
    if (style == OPENRIDE_MAP_STYLE_TRAIL) {
        return (OpenRideMapColor){194, 210, 184, 170};
    }
    return (OpenRideMapColor){207, 216, 201, 145};
}

static inline bool openride_map_world_draw_cached_masks_fast(
    OpenRideORMapRenderer *renderer,
    const OpenRideMapCamera *camera,
    int width,
    int height)
{
    static SDL_FRect *scratch = NULL;
    static uint32_t scratch_capacity = 0U;

    if (!renderer || !camera || !renderer->map
        || width <= 0 || height <= 0
        || fabs(camera->bearing_deg) >= 1e-12) {
        return false;
    }
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    if (!metadata || metadata->format_version < 6
        || metadata->mask_zoom < 0 || metadata->area_detail_zoom < 0) {
        return false;
    }

    const double zoom = camera->zoom;
    if (zoom < OPENRIDE_MAP_WORLD_BUILTUP_START
        || zoom > OPENRIDE_MAP_WORLD_MASK_MAX_ZOOM) {
        return false;
    }

    /* draw_masks() owns Area Detail readiness. Fast rendering may only bypass
     * it once the exact current viewport is already warm. A cache miss falls
     * back to the original function, which keeps the one-tile/frame prewarm
     * and deferred-load policy intact. */
    if (!openride_map_world_area_detail_cache_ready(renderer,
                                                     camera,
                                                     width,
                                                     height,
                                                     metadata->area_detail_zoom)) {
        return false;
    }
    renderer->area_detail_ready = true;

    const double builtup_detail_handoff =
        openride_map_world_mask_smoothstep(
            zoom,
            OPENRIDE_MAP_WORLD_BUILTUP_DETAIL_START,
            OPENRIDE_MAP_WORLD_BUILTUP_DETAIL_END);
    const double builtup_fade =
        openride_map_world_mask_smoothstep(
            zoom,
            OPENRIDE_MAP_WORLD_BUILTUP_START,
            OPENRIDE_MAP_WORLD_BUILTUP_FULL)
        * (1.0 - builtup_detail_handoff);
    const double forest_fade =
        openride_map_world_mask_smoothstep(
            zoom,
            OPENRIDE_MAP_WORLD_FOREST_START,
            OPENRIDE_MAP_WORLD_FOREST_FULL);
    const bool draw_builtup = builtup_fade > 0.001;
    const bool draw_forest =
        zoom >= OPENRIDE_MAP_WORLD_FOREST_START
        && zoom <= OPENRIDE_MAP_WORLD_MASK_MAX_ZOOM
        && forest_fade > 0.0;
    if (!draw_builtup && !draw_forest) return false;

    OpenRideMapWorldMaskViewport viewport;
    if (!openride_map_world_mask_viewport(camera,
                                           width,
                                           height,
                                           metadata->mask_zoom,
                                           &viewport)) {
        return false;
    }

    uint32_t tile_total = 0U;
    uint32_t forest_rect_total = 0U;
    uint32_t builtup_rect_total = 0U;
    if (!openride_map_world_mask_cache_ready(renderer,
                                              &viewport,
                                              draw_forest,
                                              draw_builtup,
                                              &tile_total,
                                              &forest_rect_total,
                                              &builtup_rect_total)) {
        return false;
    }
    const uint32_t max_rects =
        forest_rect_total > builtup_rect_total
            ? forest_rect_total : builtup_rect_total;
    if (max_rects > (uint32_t)INT_MAX
        || !openride_map_world_mask_scratch_reserve(&scratch,
                                                     &scratch_capacity,
                                                     max_rects)) {
        return false;
    }

    Uint8 old_r = 0U, old_g = 0U, old_b = 0U, old_a = 0U;
    SDL_BlendMode old_blend = SDL_BLENDMODE_INVALID;
    if (!SDL_GetRenderDrawColor(renderer->renderer,
                                &old_r,
                                &old_g,
                                &old_b,
                                &old_a)
        || !SDL_GetRenderDrawBlendMode(renderer->renderer, &old_blend)) {
        return false;
    }

    const uint64_t started = SDL_GetTicksNS();
    (void)SDL_SetRenderDrawBlendMode(renderer->renderer, SDL_BLENDMODE_BLEND);

    uint32_t submitted_rects = 0U;
    uint32_t submitted_batches = 0U;
    if (draw_forest && forest_rect_total > 0U) {
        OpenRideMapColor forest = openride_map_world_forest_color(renderer->style);
        forest.a = openride_map_world_mask_scaled_alpha(forest.a, forest_fade);
        if (forest.a > 0U) {
            const uint32_t count = openride_map_world_collect_mask_rects(
                renderer, &viewport, width, height, true, scratch);
            (void)SDL_SetRenderDrawColor(renderer->renderer,
                                         forest.r,
                                         forest.g,
                                         forest.b,
                                         forest.a);
            (void)SDL_RenderFillRects(renderer->renderer, scratch, (int)count);
            submitted_rects += count;
            if (count > 0U) ++submitted_batches;
        }
    }

    if (draw_builtup && builtup_rect_total > 0U) {
        const OpenRideMapPalette palette = openride_map_palette(renderer->style);
        OpenRideMapColor builtup = palette.building;
        builtup.a = renderer->style == OPENRIDE_MAP_STYLE_TRAIL ? 92 : 125;
        builtup.a = openride_map_world_mask_scaled_alpha(builtup.a, builtup_fade);
        if (builtup.a > 0U) {
            const uint32_t count = openride_map_world_collect_mask_rects(
                renderer, &viewport, width, height, false, scratch);
            (void)SDL_SetRenderDrawColor(renderer->renderer,
                                         builtup.r,
                                         builtup.g,
                                         builtup.b,
                                         builtup.a);
            (void)SDL_RenderFillRects(renderer->renderer, scratch, (int)count);
            submitted_rects += count;
            if (count > 0U) ++submitted_batches;
        }
    }

    (void)SDL_SetRenderDrawColor(renderer->renderer,
                                 old_r,
                                 old_g,
                                 old_b,
                                 old_a);
    (void)SDL_SetRenderDrawBlendMode(renderer->renderer, old_blend);

    renderer->area_debug.tiles_visited += tile_total;
    renderer->area_debug.mask_tiles += tile_total;
    renderer->area_debug.mask_cache_hits += tile_total;
    renderer->area_debug.mask_rects += submitted_rects;
    renderer->area_debug.mask_batches += submitted_batches;
    renderer->area_debug.batches += submitted_batches;
    renderer->area_debug.areas_ms +=
        (double)(SDL_GetTicksNS() - started) / 1000000.0;
    return true;
}

static inline void openride_map_world_ormap_renderer_draw_layer(
    OpenRideORMapRenderer *renderer,
    const OpenRideMapCamera *camera,
    int viewport_width,
    int viewport_height,
    OpenRideORMapRenderLayer layer)
{
    if (layer == OPENRIDE_ORMAP_RENDER_LAYER_MASKS
        && openride_map_world_draw_cached_masks_fast(renderer,
                                                      camera,
                                                      viewport_width,
                                                      viewport_height)) {
        return;
    }
    openride_ormap_renderer_draw_layer(renderer,
                                       camera,
                                       viewport_width,
                                       viewport_height,
                                       layer);
}

typedef struct OpenRideMapWorldDebugStats {
    bool overview_drawn;
    bool detail_drawn;
    bool ormap_stats_valid;
    uint32_t visible_detail_regions;
    double overview_ms;
    double detail_ms;
    double masks_ms;
    double areas_ms;
    double waterways_ms;
    double roads_ms;
    double labels_ms;
    OpenRideORMapRoadDebugStats road;
    OpenRideORMapAreaDebugStats area;
} OpenRideMapWorldDebugStats;

typedef struct OpenRideMapWorld OpenRideMapWorld;

OpenRideMapWorld *openride_map_world_create(SDL_Renderer *renderer,
                                             const OpenRidePlatformPaths *paths,
                                             char *error,
                                             size_t error_size);

bool openride_map_world_refresh(OpenRideMapWorld *world,
                                const OpenRidePlatformPaths *paths,
                                char *error,
                                size_t error_size);

void openride_map_world_destroy(OpenRideMapWorld *world);

void openride_map_world_draw(OpenRideMapWorld *world,
                             const OpenRideMapCamera *camera,
                             OpenRideMapStyle style,
                             const char *skip_region_id,
                             int viewport_width,
                             int viewport_height);
void openride_map_world_draw_detail(OpenRideMapWorld *world,
                                    const OpenRideMapCamera *camera,
                                    OpenRideMapStyle style,
                                    int viewport_width,
                                    int viewport_height);

void openride_map_world_debug_begin_frame(OpenRideMapWorld *world);
void openride_map_world_debug_end_frame(OpenRideMapWorld *world);
void openride_map_world_get_debug_stats(
    const OpenRideMapWorld *world,
    OpenRideMapWorldDebugStats *stats);

size_t openride_map_world_region_count(const OpenRideMapWorld *world);

/*
 * Route MapWorld's ORMap layer submissions through the cache-aware dispatcher.
 * The original symbol remains the fallback inside the inline function above;
 * renderer.c does not include this header, so its own definition is untouched.
 */
#define openride_ormap_renderer_draw_layer openride_map_world_ormap_renderer_draw_layer

#endif
