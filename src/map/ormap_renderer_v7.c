#include "map/ormap_renderer.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define V7_TILE_SIZE 256.0
#define V7_CACHE_ASSOCIATIVITY 4U
#define V7_BATCH_VERTEX_LIMIT 16384U
#define V7_BATCH_INDEX_LIMIT 24576U
#define V7_OVERVIEW_PREWARM_START 12.35
#define V7_OVERVIEW_DRAW_START 13.15
#define V7_DETAIL_PREWARM_START 13.90
#define V7_DETAIL_HANDOFF_START 14.70
#define V7_DETAIL_HANDOFF_END 15.20
#define V7_MASK_PREWARM_BUDGET 3U
#define V7_AREA_PREWARM_BUDGET 1U

typedef struct V7Batch {
    uint32_t vertices;
    uint32_t indices;
} V7Batch;

typedef struct V7View {
    int count;
    double tile_size;
    double center_x;
    double center_y;
    int first_x;
    int last_x;
    int first_y;
    int last_y;
    double c;
    double s;
    double cx;
    double cy;
} V7View;

void openride_ormap_renderer_draw_layer_v6(
    OpenRideORMapRenderer *renderer,
    const OpenRideMapCamera *camera,
    int viewport_width,
    int viewport_height,
    OpenRideORMapRenderLayer layer);

static double v7_smoothstep(double value, double start, double end)
{
    if (value <= start) return 0.0;
    if (value >= end) return 1.0;
    const double t = (value - start) / (end - start);
    return t * t * (3.0 - 2.0 * t);
}

static uint8_t v7_alpha(uint8_t alpha, double factor)
{
    if (factor <= 0.0) return 0U;
    if (factor >= 1.0) return alpha;
    return (uint8_t)lround((double)alpha * factor);
}

static int v7_wrap_x(int x, int count)
{
    int wrapped = x % count;
    if (wrapped < 0) wrapped += count;
    return wrapped;
}

static uint32_t v7_hash(int zoom, int x, int y)
{
    uint32_t value = (uint32_t)zoom * UINT32_C(0x9e3779b9);
    value ^= (uint32_t)x * UINT32_C(0x85ebca6b);
    value ^= (uint32_t)y * UINT32_C(0xc2b2ae35);
    value ^= value >> 16U;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15U;
    return value;
}

static size_t v7_cache_base(size_t capacity, int zoom, int x, int y)
{
    const size_t sets = capacity / V7_CACHE_ASSOCIATIVITY;
    return (size_t)(v7_hash(zoom, x, y) % (uint32_t)sets)
        * V7_CACHE_ASSOCIATIVITY;
}

static bool v7_view(const OpenRideMapCamera *camera,
                    int width,
                    int height,
                    int zoom,
                    double camera_zoom,
                    V7View *view)
{
    if (!camera || !view || width <= 0 || height <= 0
        || zoom < 0 || zoom > 30) {
        return false;
    }
    const int count = 1 << zoom;
    const double tile_size = V7_TILE_SIZE * pow(2.0, camera_zoom - zoom);
    if (!isfinite(tile_size) || tile_size <= 0.0) return false;
    const OpenRidePointD center =
        openride_mercator_forward(camera->center_lat, camera->center_lon);
    const double world_size = tile_size * count;
    const double center_x = center.x * world_size;
    const double center_y = center.y * world_size;
    const double angle = camera->bearing_deg
        * 3.14159265358979323846 / 180.0;
    const double c = cos(angle);
    const double s = sin(angle);
    const double half_w = fabs(c) * width * 0.5 + fabs(s) * height * 0.5;
    const double half_h = fabs(s) * width * 0.5 + fabs(c) * height * 0.5;
    *view = (V7View){
        .count = count,
        .tile_size = tile_size,
        .center_x = center_x,
        .center_y = center_y,
        .first_x = (int)floor((center_x - half_w) / tile_size),
        .last_x = (int)floor((center_x + half_w) / tile_size),
        .first_y = (int)floor((center_y - half_h) / tile_size),
        .last_y = (int)floor((center_y + half_h) / tile_size),
        .c = c,
        .s = s,
        .cx = width * 0.5,
        .cy = height * 0.5
    };
    return true;
}

static void v7_rotate(const V7View *view, float *x, float *y)
{
    if (!view || !x || !y || fabs(view->s) < 1e-15) return;
    const double dx = *x - view->cx;
    const double dy = *y - view->cy;
    *x = (float)(view->cx + dx * view->c + dy * view->s);
    *y = (float)(view->cy - dx * view->s + dy * view->c);
}

static bool v7_ensure_geometry(OpenRideORMapRenderer *renderer,
                               uint32_t vertices,
                               uint32_t indices)
{
    if (vertices > renderer->area_vertex_capacity) {
        uint32_t capacity = renderer->area_vertex_capacity
            ? renderer->area_vertex_capacity : 4096U;
        while (capacity < vertices) capacity *= 2U;
        SDL_Vertex *grown = realloc(renderer->area_vertices,
                                    (size_t)capacity * sizeof(*grown));
        if (!grown) return false;
        renderer->area_vertices = grown;
        renderer->area_vertex_capacity = capacity;
    }
    if (indices > renderer->area_index_capacity) {
        uint32_t capacity = renderer->area_index_capacity
            ? renderer->area_index_capacity : 6144U;
        while (capacity < indices) capacity *= 2U;
        int *grown = realloc(renderer->area_indices,
                             (size_t)capacity * sizeof(*grown));
        if (!grown) return false;
        renderer->area_indices = grown;
        renderer->area_index_capacity = capacity;
    }
    return true;
}

static void v7_flush(OpenRideORMapRenderer *renderer, V7Batch *batch)
{
    if (!renderer || !batch || batch->vertices == 0U || batch->indices == 0U) {
        return;
    }
    ++renderer->area_debug.batches;
    ++renderer->area_debug.mask_batches;
    SDL_RenderGeometry(renderer->renderer,
                       NULL,
                       renderer->area_vertices,
                       (int)batch->vertices,
                       renderer->area_indices,
                       (int)batch->indices);
    batch->vertices = 0U;
    batch->indices = 0U;
}

static bool v7_quad(OpenRideORMapRenderer *renderer,
                    V7Batch *batch,
                    const float x[4],
                    const float y[4],
                    OpenRideMapColor color)
{
    if (batch->vertices > V7_BATCH_VERTEX_LIMIT - 4U
        || batch->indices > V7_BATCH_INDEX_LIMIT - 6U) {
        v7_flush(renderer, batch);
    }
    if (!v7_ensure_geometry(renderer,
                            batch->vertices + 4U,
                            batch->indices + 6U)) {
        return false;
    }
    const uint32_t base = batch->vertices;
    for (uint32_t i = 0U; i < 4U; ++i) {
        SDL_Vertex *vertex = &renderer->area_vertices[base + i];
        vertex->position.x = x[i];
        vertex->position.y = y[i];
        vertex->color.r = color.r / 255.0f;
        vertex->color.g = color.g / 255.0f;
        vertex->color.b = color.b / 255.0f;
        vertex->color.a = color.a / 255.0f;
        vertex->tex_coord.x = 0.0f;
        vertex->tex_coord.y = 0.0f;
    }
    const uint32_t index = batch->indices;
    renderer->area_indices[index + 0U] = (int)base + 0;
    renderer->area_indices[index + 1U] = (int)base + 1;
    renderer->area_indices[index + 2U] = (int)base + 2;
    renderer->area_indices[index + 3U] = (int)base + 0;
    renderer->area_indices[index + 4U] = (int)base + 2;
    renderer->area_indices[index + 5U] = (int)base + 3;
    batch->vertices += 4U;
    batch->indices += 6U;
    return true;
}

static void v7_mask_entry_destroy(OpenRideORMapMaskCacheEntry *entry)
{
    if (!entry) return;
    if (entry->occupied) openride_ormap_mask_tile_destroy(&entry->tile);
    free(entry->builtup_rects);
    free(entry->water_rects);
    free(entry->forest_rects);
    memset(entry, 0, sizeof(*entry));
}

static OpenRideORMapMaskCacheEntry *v7_mask_find(OpenRideORMapRenderer *renderer,
                                                 int zoom,
                                                 int x,
                                                 int y)
{
    const size_t base = v7_cache_base(OPENRIDE_ORMAP_MASK_CACHE_CAPACITY,
                                      zoom, x, y);
    for (size_t i = 0U; i < V7_CACHE_ASSOCIATIVITY; ++i) {
        OpenRideORMapMaskCacheEntry *entry = &renderer->masks[base + i];
        if (entry->occupied && entry->zoom == zoom
            && entry->x == x && entry->y == y) {
            return entry;
        }
    }
    return NULL;
}

static OpenRideORMapMaskCacheEntry *v7_mask_load(OpenRideORMapRenderer *renderer,
                                                 int zoom,
                                                 int x,
                                                 int y,
                                                 bool prewarm)
{
    OpenRideORMapMaskCacheEntry *found = v7_mask_find(renderer, zoom, x, y);
    if (found) {
        found->last_used = renderer->frame_counter;
        ++renderer->area_debug.mask_cache_hits;
        return found;
    }
    ++renderer->area_debug.mask_cache_misses;

    const size_t base = v7_cache_base(OPENRIDE_ORMAP_MASK_CACHE_CAPACITY,
                                      zoom, x, y);
    OpenRideORMapMaskCacheEntry *victim = NULL;
    OpenRideORMapMaskCacheEntry *oldest = &renderer->masks[base];
    for (size_t i = 0U; i < V7_CACHE_ASSOCIATIVITY; ++i) {
        OpenRideORMapMaskCacheEntry *entry = &renderer->masks[base + i];
        if (!entry->occupied && !victim) victim = entry;
        if (entry->occupied && oldest->occupied
            && entry->last_used < oldest->last_used) {
            oldest = entry;
        }
    }
    if (!victim) {
        victim = oldest;
        if (prewarm && victim->occupied
            && victim->last_used + 1U >= renderer->frame_counter) {
            return NULL;
        }
    }
    v7_mask_entry_destroy(victim);
    victim->occupied = true;
    victim->zoom = zoom;
    victim->x = x;
    victim->y = y;
    victim->last_used = renderer->frame_counter;
    const uint64_t started = SDL_GetTicksNS();
    char error[160] = {0};
    (void)openride_ormap_load_mask_tile(renderer->map,
                                        zoom, x, y,
                                        &victim->tile,
                                        error, sizeof(error));
    renderer->area_debug.load_ms +=
        (double)(SDL_GetTicksNS() - started) / 1000000.0;
    if (prewarm) ++renderer->area_debug.prewarm_loads;
    else ++renderer->area_debug.draw_loads;
    return victim;
}

static uint32_t v7_prewarm_masks(OpenRideORMapRenderer *renderer,
                                 const OpenRideMapCamera *camera,
                                 int width,
                                 int height,
                                 int zoom,
                                 double view_zoom,
                                 uint32_t budget)
{
    V7View view;
    if (!v7_view(camera, width, height, zoom, view_zoom, &view)) return 0U;
    uint32_t loaded = 0U;
    for (int ty = view.first_y; ty <= view.last_y; ++ty) {
        if (ty < 0 || ty >= view.count) continue;
        for (int tx = view.first_x; tx <= view.last_x; ++tx) {
            const int qx = v7_wrap_x(tx, view.count);
            if (v7_mask_find(renderer, zoom, qx, ty)) continue;
            if (!v7_mask_load(renderer, zoom, qx, ty, true)) continue;
            if (++loaded >= budget) return loaded;
        }
    }
    return loaded;
}

static bool v7_masks_ready(OpenRideORMapRenderer *renderer,
                           const OpenRideMapCamera *camera,
                           int width,
                           int height,
                           int zoom)
{
    V7View view;
    if (!v7_view(camera, width, height, zoom, camera->zoom, &view)) return false;
    for (int ty = view.first_y; ty <= view.last_y; ++ty) {
        if (ty < 0 || ty >= view.count) continue;
        for (int tx = view.first_x; tx <= view.last_x; ++tx) {
            if (!v7_mask_find(renderer,
                              zoom,
                              v7_wrap_x(tx, view.count),
                              ty)) {
                return false;
            }
        }
    }
    return true;
}

static bool v7_area_contains(const OpenRideORMapRenderer *renderer,
                             int zoom,
                             int x,
                             int y)
{
    const size_t base = v7_cache_base(OPENRIDE_ORMAP_AREA_CACHE_CAPACITY,
                                      zoom, x, y);
    for (size_t i = 0U; i < V7_CACHE_ASSOCIATIVITY; ++i) {
        const OpenRideORMapAreaCacheEntry *entry = &renderer->areas[base + i];
        if (entry->occupied && entry->zoom == zoom
            && entry->x == x && entry->y == y) {
            return true;
        }
    }
    return false;
}

static bool v7_area_load(OpenRideORMapRenderer *renderer,
                         int zoom,
                         int x,
                         int y)
{
    const size_t base = v7_cache_base(OPENRIDE_ORMAP_AREA_CACHE_CAPACITY,
                                      zoom, x, y);
    OpenRideORMapAreaCacheEntry *victim = NULL;
    OpenRideORMapAreaCacheEntry *oldest = &renderer->areas[base];
    for (size_t i = 0U; i < V7_CACHE_ASSOCIATIVITY; ++i) {
        OpenRideORMapAreaCacheEntry *entry = &renderer->areas[base + i];
        if (!entry->occupied && !victim) victim = entry;
        if (entry->occupied && oldest->occupied
            && entry->last_used < oldest->last_used) oldest = entry;
    }
    if (!victim) {
        victim = oldest;
        if (victim->occupied
            && victim->last_used + 1U >= renderer->frame_counter) return false;
    }
    if (victim->occupied) openride_ormap_area_tile_destroy(&victim->tile);
    memset(victim, 0, sizeof(*victim));
    victim->occupied = true;
    victim->zoom = zoom;
    victim->x = x;
    victim->y = y;
    victim->last_used = renderer->frame_counter;
    const uint64_t started = SDL_GetTicksNS();
    char error[160] = {0};
    (void)openride_ormap_load_area_tile(renderer->map,
                                        zoom, x, y,
                                        &victim->tile,
                                        error, sizeof(error));
    renderer->area_debug.load_ms +=
        (double)(SDL_GetTicksNS() - started) / 1000000.0;
    ++renderer->area_debug.prewarm_loads;
    return true;
}

static void v7_prepare_area_detail(OpenRideORMapRenderer *renderer,
                                   const OpenRideMapCamera *camera,
                                   int width,
                                   int height,
                                   int zoom)
{
    if (camera->zoom < 11.0) return;
    const double future_zoom = camera->zoom < 13.40 ? 13.40 : camera->zoom;
    V7View future;
    if (v7_view(camera, width, height, zoom, future_zoom, &future)) {
        uint32_t loaded = 0U;
        for (int ty = future.first_y;
             ty <= future.last_y && loaded < V7_AREA_PREWARM_BUDGET;
             ++ty) {
            if (ty < 0 || ty >= future.count) continue;
            for (int tx = future.first_x;
                 tx <= future.last_x && loaded < V7_AREA_PREWARM_BUDGET;
                 ++tx) {
                const int qx = v7_wrap_x(tx, future.count);
                if (v7_area_contains(renderer, zoom, qx, ty)) continue;
                if (v7_area_load(renderer, zoom, qx, ty)) ++loaded;
            }
        }
    }

    V7View current;
    if (!v7_view(camera, width, height, zoom, camera->zoom, &current)) return;
    renderer->area_detail_ready = true;
    for (int ty = current.first_y; ty <= current.last_y; ++ty) {
        if (ty < 0 || ty >= current.count) continue;
        for (int tx = current.first_x; tx <= current.last_x; ++tx) {
            if (!v7_area_contains(renderer,
                                  zoom,
                                  v7_wrap_x(tx, current.count),
                                  ty)) {
                renderer->area_detail_ready = false;
                return;
            }
        }
    }
}

static OpenRideMapColor v7_builtup(OpenRideORMapRenderer *renderer,
                                   double zoom,
                                   double weight)
{
    OpenRideMapColor color = openride_map_palette(renderer->style).building;
    const double mix = v7_smoothstep(zoom, 13.15, 13.75);
    const double coarse = renderer->style == OPENRIDE_MAP_STYLE_TRAIL ? 72.0 : 88.0;
    const double detail = renderer->style == OPENRIDE_MAP_STYLE_TRAIL ? 88.0 : 108.0;
    color.a = (uint8_t)lround(coarse + (detail - coarse) * mix);
    color.a = v7_alpha(color.a, weight);
    return color;
}

static OpenRideMapColor v7_forest(OpenRideORMapRenderer *renderer,
                                  double zoom,
                                  double weight)
{
    OpenRideMapColor color;
    double coarse = 98.0;
    double detail = 145.0;
    if (renderer->style == OPENRIDE_MAP_STYLE_TOPO) {
        color = (OpenRideMapColor){180U, 203U, 170U, 150U};
        coarse = 150.0;
        detail = 210.0;
    } else if (renderer->style == OPENRIDE_MAP_STYLE_TRAIL) {
        color = (OpenRideMapColor){194U, 210U, 184U, 118U};
        coarse = 118.0;
        detail = 170.0;
    } else {
        color = (OpenRideMapColor){207U, 216U, 201U, 98U};
    }
    const double mix = v7_smoothstep(zoom, 13.40, 14.40);
    color.a = (uint8_t)lround(coarse + (detail - coarse) * mix);
    color.a = v7_alpha(color.a, weight);
    return color;
}

static bool v7_draw_bits(OpenRideORMapRenderer *renderer,
                         V7Batch *batch,
                         const V7View *view,
                         int width,
                         int height,
                         int tx,
                         int ty,
                         const OpenRideORMapMaskTile *tile,
                         const unsigned char *bits,
                         OpenRideMapColor color)
{
    if (!tile || !bits || tile->grid_size == 0U || color.a == 0U) return true;
    const uint32_t grid = tile->grid_size;
    const double cell = view->tile_size / grid;
    const double left = width * 0.5 + tx * view->tile_size - view->center_x;
    const double top = height * 0.5 + ty * view->tile_size - view->center_y;
    for (uint32_t y = 0U; y < grid; ++y) {
        uint32_t x = 0U;
        while (x < grid) {
            while (x < grid && !(bits[(y * grid + x) >> 3U]
                    & (unsigned char)(1U << ((y * grid + x) & 7U)))) ++x;
            if (x >= grid) break;
            const uint32_t start = x;
            while (x < grid && (bits[(y * grid + x) >> 3U]
                    & (unsigned char)(1U << ((y * grid + x) & 7U)))) ++x;
            float px[4] = {
                (float)(left + start * cell),
                (float)(left + x * cell + 0.5),
                (float)(left + x * cell + 0.5),
                (float)(left + start * cell)
            };
            float py[4] = {
                (float)(top + y * cell),
                (float)(top + y * cell),
                (float)(top + (y + 1U) * cell + 0.5),
                (float)(top + (y + 1U) * cell + 0.5)
            };
            for (uint32_t v = 0U; v < 4U; ++v) v7_rotate(view, &px[v], &py[v]);
            float min_x = px[0], max_x = px[0], min_y = py[0], max_y = py[0];
            for (uint32_t v = 1U; v < 4U; ++v) {
                if (px[v] < min_x) min_x = px[v];
                if (px[v] > max_x) max_x = px[v];
                if (py[v] < min_y) min_y = py[v];
                if (py[v] > max_y) max_y = py[v];
            }
            if (max_x < -1.0f || min_x > width + 1.0f
                || max_y < -1.0f || min_y > height + 1.0f) continue;
            ++renderer->area_debug.mask_rects;
            if (!v7_quad(renderer, batch, px, py, color)) return false;
        }
    }
    return true;
}

static void v7_draw_level(OpenRideORMapRenderer *renderer,
                          const OpenRideMapCamera *camera,
                          int width,
                          int height,
                          int zoom,
                          double weight)
{
    if (weight <= 0.001) return;
    V7View view;
    if (!v7_view(camera, width, height, zoom, camera->zoom, &view)) return;
    const OpenRideMapColor forest = v7_forest(renderer, camera->zoom, weight);
    const OpenRideMapColor builtup = v7_builtup(renderer, camera->zoom, weight);
    V7Batch batch = {0};
    for (int ty = view.first_y; ty <= view.last_y; ++ty) {
        if (ty < 0 || ty >= view.count) continue;
        for (int tx = view.first_x; tx <= view.last_x; ++tx) {
            const int qx = v7_wrap_x(tx, view.count);
            OpenRideORMapMaskCacheEntry *entry = v7_mask_find(renderer, zoom, qx, ty);
            if (!entry) continue;
            entry->last_used = renderer->frame_counter;
            ++renderer->area_debug.mask_cache_hits;
            ++renderer->area_debug.mask_tiles;
            ++renderer->area_debug.tiles_visited;
            if (!v7_draw_bits(renderer, &batch, &view, width, height,
                              tx, ty, &entry->tile, entry->tile.forest, forest)
                || !v7_draw_bits(renderer, &batch, &view, width, height,
                                 tx, ty, &entry->tile, entry->tile.builtup, builtup)) {
                v7_flush(renderer, &batch);
                return;
            }
        }
    }
    v7_flush(renderer, &batch);
}

static bool v7_draw_masks(OpenRideORMapRenderer *renderer,
                          const OpenRideMapCamera *camera,
                          int width,
                          int height)
{
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    if (!metadata || metadata->format_version < 7) return false;
    const uint64_t started = SDL_GetTicksNS();
    const bool old_area_debug = renderer->area_debug_active;
    const bool old_mask_debug = renderer->mask_debug_active;
    renderer->area_debug_active = true;
    renderer->mask_debug_active = true;

    v7_prepare_area_detail(renderer, camera, width, height, metadata->area_detail_zoom);
    if (camera->zoom >= V7_OVERVIEW_PREWARM_START) {
        const double view_zoom = camera->zoom < V7_OVERVIEW_DRAW_START
            ? V7_OVERVIEW_DRAW_START : camera->zoom;
        (void)v7_prewarm_masks(renderer, camera, width, height,
                               OPENRIDE_ORMAP_MASK_OVERVIEW_ZOOM,
                               view_zoom, V7_MASK_PREWARM_BUDGET);
    }
    if (camera->zoom >= V7_DETAIL_PREWARM_START) {
        const double view_zoom = camera->zoom < V7_DETAIL_HANDOFF_START
            ? V7_DETAIL_HANDOFF_START : camera->zoom;
        (void)v7_prewarm_masks(renderer, camera, width, height,
                               metadata->mask_zoom,
                               view_zoom, V7_MASK_PREWARM_BUDGET);
    }

    if (camera->zoom < V7_OVERVIEW_DRAW_START
        || !v7_masks_ready(renderer, camera, width, height,
                           OPENRIDE_ORMAP_MASK_OVERVIEW_ZOOM)) {
        renderer->area_debug.areas_ms +=
            (double)(SDL_GetTicksNS() - started) / 1000000.0;
        renderer->mask_debug_active = old_mask_debug;
        renderer->area_debug_active = old_area_debug;
        return false;
    }

    const bool detail_ready = camera->zoom >= V7_DETAIL_HANDOFF_START
        && v7_masks_ready(renderer, camera, width, height, metadata->mask_zoom);
    const double detail_mix = detail_ready
        ? v7_smoothstep(camera->zoom,
                        V7_DETAIL_HANDOFF_START,
                        V7_DETAIL_HANDOFF_END)
        : 0.0;
    v7_draw_level(renderer, camera, width, height,
                  OPENRIDE_ORMAP_MASK_OVERVIEW_ZOOM,
                  1.0 - detail_mix);
    if (detail_mix > 0.001) {
        v7_draw_level(renderer, camera, width, height,
                      metadata->mask_zoom, detail_mix);
    }

    renderer->area_debug.areas_ms +=
        (double)(SDL_GetTicksNS() - started) / 1000000.0;
    renderer->mask_debug_active = old_mask_debug;
    renderer->area_debug_active = old_area_debug;
    return true;
}

void openride_ormap_renderer_draw_layer(OpenRideORMapRenderer *renderer,
                                        const OpenRideMapCamera *camera,
                                        int viewport_width,
                                        int viewport_height,
                                        OpenRideORMapRenderLayer layer)
{
    if (!renderer || !camera || viewport_width <= 0 || viewport_height <= 0) return;
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    if (metadata && metadata->format_version >= 7
        && layer == OPENRIDE_ORMAP_RENDER_LAYER_MASKS) {
        if (camera->zoom < V7_OVERVIEW_DRAW_START) {
            if (camera->zoom >= V7_OVERVIEW_PREWARM_START) {
                const bool old_debug = renderer->area_debug_active;
                renderer->area_debug_active = true;
                (void)v7_prewarm_masks(renderer,
                                       camera,
                                       viewport_width,
                                       viewport_height,
                                       OPENRIDE_ORMAP_MASK_OVERVIEW_ZOOM,
                                       V7_OVERVIEW_DRAW_START,
                                       V7_MASK_PREWARM_BUDGET);
                renderer->area_debug_active = old_debug;
            }
            openride_ormap_renderer_draw_layer_v6(renderer, camera,
                                                   viewport_width, viewport_height,
                                                   layer);
            return;
        }
        if (v7_draw_masks(renderer, camera,
                          viewport_width, viewport_height)) return;
    }
    openride_ormap_renderer_draw_layer_v6(renderer, camera,
                                           viewport_width, viewport_height,
                                           layer);
}

void openride_ormap_renderer_draw(OpenRideORMapRenderer *renderer,
                                  const OpenRideMapCamera *camera,
                                  int viewport_width,
                                  int viewport_height)
{
    if (!renderer || !camera || viewport_width <= 0 || viewport_height <= 0) return;
    openride_ormap_renderer_begin_frame(renderer);
    const OpenRideMapPalette palette = openride_map_palette(renderer->style);
    SDL_SetRenderDrawColor(renderer->renderer,
                           palette.background.r,
                           palette.background.g,
                           palette.background.b,
                           palette.background.a);
    SDL_RenderClear(renderer->renderer);
    for (int layer = OPENRIDE_ORMAP_RENDER_LAYER_MASKS;
         layer <= OPENRIDE_ORMAP_RENDER_LAYER_LABELS;
         ++layer) {
        openride_ormap_renderer_draw_layer(renderer, camera,
                                           viewport_width, viewport_height,
                                           (OpenRideORMapRenderLayer)layer);
    }
}
