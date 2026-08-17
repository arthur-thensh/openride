#include "map/ormap_pyramid_renderer.h"

#include "openride/ormap_pyramid_surface.h"
#include "openride/ormap_tile_pyramid.h"

#include <math.h>
#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define V11_SURFACE_CACHE_CAPACITY 384U
#define V11_SURFACE_TEXTURE_CACHE_CAPACITY 128U
#define V11_BUILDING_CACHE_CAPACITY 512U
#define V11_CACHE_ASSOCIATIVITY 4U
#define V11_SURFACE_LOAD_BUDGET 6U
#define V11_SURFACE_TEXTURE_COMPILE_BUDGET 2U
#define V11_BUILDING_LOAD_BUDGET 16U
#define V11_SURFACE_TEXTURE_PIXELS 257
#define V11_GEOMETRY_VERTEX_LIMIT 16384U
#define V11_GEOMETRY_INDEX_LIMIT 16384U
#define V11_BUILDING_READY_RAMP_MS 160U
#define V11_SURFACE_START_ZOOM 8.70
#define V11_SURFACE_FULL_ZOOM 9.40
#define V11_BUILDING_START_ZOOM 15.00
#define V11_BUILDING_FULL_ZOOM 16.00
#define V11_SURFACE_BACKEND_BLEND_START_ZOOM 14.10
#define V11_SURFACE_BACKEND_BLEND_END_ZOOM 14.40

typedef struct V11GeometryBatch {
    uint32_t vertex_count;
    uint32_t index_count;
} V11GeometryBatch;

typedef struct V11SurfaceCacheEntry {
    bool occupied;
    int zoom;
    int x;
    int y;
    uint64_t last_used;
    OpenRideORMapPyramidTileState state;
    OpenRideORMapPyramidSurfaceTile tile;
} V11SurfaceCacheEntry;

typedef struct V11SurfaceTextureEntry {
    bool occupied;
    int zoom;
    int x;
    int y;
    OpenRideMapStyle style;
    uint64_t last_used;
    SDL_Texture *base_texture;
    SDL_Texture *builtup_texture;
} V11SurfaceTextureEntry;

typedef struct V11BuildingTriangle {
    uint16_t x1, y1;
    uint16_t x2, y2;
    uint16_t x3, y3;
} V11BuildingTriangle;

typedef struct V11BuildingCacheEntry {
    bool occupied;
    bool empty;
    int x;
    int y;
    uint64_t last_used;
    uint64_t ready_since_ms;
    V11BuildingTriangle *triangles;
    uint32_t triangle_count;
} V11BuildingCacheEntry;

typedef struct V11VisibleRange {
    int first_x;
    int last_x;
    int first_y;
    int last_y;
    bool valid;
} V11VisibleRange;

typedef struct V11BuildingPoint {
    double x;
    double y;
} V11BuildingPoint;

struct OpenRideORMapPyramidRenderer {
    SDL_Renderer *renderer;
    OpenRideORMapPyramidSurfaceMap *surface_map;

    sqlite3 *building_db;
    sqlite3_stmt *building_load_stmt;
    bool building_available;

    OpenRideORMapTilePyramid pyramid;
    OpenRideORMapPyramidPlan surface_plan;

    OpenRideMapStyle style;
    uint64_t frame_counter;
    uint64_t now_ms;

    uint32_t surface_load_budget;
    uint32_t surface_texture_compile_budget;
    uint32_t building_load_budget;
    bool needs_followup;

    bool surface_texture_supported;
    bool surface_texture_compiling;
    SDL_BlendMode surface_add_blend;
    SDL_BlendMode surface_premult_blend;
    SDL_Texture *surface_compositor;
    int surface_compositor_width;
    int surface_compositor_height;
    SDL_Texture *surface_backend_compositor;
    bool surface_capture_gpu_compositor;
    bool surface_capture_backend_started;
    bool surface_capture_present_valid;
    bool surface_capture_failed;
    bool surface_capture_has_src;
    bool surface_capture_has_dst;
    double surface_capture_gpu_weight;
    SDL_FRect surface_capture_src;
    SDL_FRect surface_capture_dst;

    V11VisibleRange visible[
        OPENRIDE_ORMAP_PYRAMID_BUILDING_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM + 1];

    V11SurfaceCacheEntry surfaces[V11_SURFACE_CACHE_CAPACITY];
    V11SurfaceTextureEntry
        surface_textures[V11_SURFACE_TEXTURE_CACHE_CAPACITY];
    V11BuildingCacheEntry buildings[V11_BUILDING_CACHE_CAPACITY];

    SDL_Vertex *vertices;
    int *indices;
    uint32_t vertex_capacity;
    uint32_t index_capacity;

    OpenRideORMapAreaDebugStats debug;
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
}

static double clamp01(double value)
{
    if (value <= 0.0) return 0.0;
    if (value >= 1.0) return 1.0;
    return value;
}

static double smoothstep(double value, double start, double end)
{
    if (value <= start) return 0.0;
    if (value >= end) return 1.0;
    const double t = (value - start) / (end - start);
    return t * t * (3.0 - 2.0 * t);
}

static uint8_t scaled_alpha(uint8_t alpha, double factor)
{
    if (factor <= 0.0) return 0U;
    if (factor >= 1.0) return alpha;
    return (uint8_t)lround((double)alpha * factor);
}

static OpenRideMapColor green_color(OpenRideMapStyle style)
{
    if (style == OPENRIDE_MAP_STYLE_TOPO) {
        return (OpenRideMapColor){180, 203, 170, 205};
    }
    if (style == OPENRIDE_MAP_STYLE_TRAIL) {
        return (OpenRideMapColor){194, 210, 184, 165};
    }
    return (OpenRideMapColor){207, 216, 201, 145};
}

static void surface_base_colors(
    OpenRideMapStyle style,
    double camera_zoom,
    double surface_alpha,
    OpenRideMapColor colors[4])
{
    memset(colors, 0, 4U * sizeof(*colors));

    const OpenRideMapPalette palette =
        openride_map_palette(style);

    OpenRideMapColor water = palette.water;
    water.a =
        style == OPENRIDE_MAP_STYLE_TRAIL
            ? 218U
            : 228U;
    water.a =
        scaled_alpha(water.a, surface_alpha);

    OpenRideMapColor green =
        green_color(style);
    green.a =
        scaled_alpha(green.a, surface_alpha);

    OpenRideMapColor builtup = palette.building;
    builtup.a =
        style == OPENRIDE_MAP_STYLE_TRAIL
            ? 78U
            : style == OPENRIDE_MAP_STYLE_TOPO
                ? 88U
                : 96U;

    const double context_alpha =
        1.0
        - 0.78
            * smoothstep(
                camera_zoom,
                11.60,
                15.20);

    builtup.a =
        scaled_alpha(
            builtup.a,
            context_alpha * surface_alpha);

    colors[OPENRIDE_ORMAP_PYRAMID_SURFACE_BUILTUP] =
        builtup;
    colors[OPENRIDE_ORMAP_PYRAMID_SURFACE_WATER] =
        water;
    colors[OPENRIDE_ORMAP_PYRAMID_SURFACE_GREEN] =
        green;
}

static OpenRideMapColor building_color(
    OpenRideMapStyle style,
    double camera_zoom,
    double ready_alpha)
{
    OpenRideMapColor color = openride_map_palette(style).building;

    color.a = style == OPENRIDE_MAP_STYLE_TRAIL
        ? 112U
        : style == OPENRIDE_MAP_STYLE_TOPO ? 160U : 185U;

    color.a = scaled_alpha(
        color.a,
        smoothstep(
            camera_zoom,
            V11_BUILDING_START_ZOOM,
            V11_BUILDING_FULL_ZOOM)
            * ready_alpha);
    return color;
}

static bool ensure_geometry(
    OpenRideORMapPyramidRenderer *renderer,
    uint32_t vertex_count,
    uint32_t index_count)
{
    if (!renderer) return false;

    if (vertex_count > renderer->vertex_capacity) {
        uint32_t capacity =
            renderer->vertex_capacity ? renderer->vertex_capacity : 4096U;
        while (capacity < vertex_count) {
            if (capacity > UINT32_MAX / 2U) {
                capacity = vertex_count;
                break;
            }
            capacity *= 2U;
        }

        SDL_Vertex *grown = realloc(
            renderer->vertices,
            (size_t)capacity * sizeof(*grown));
        if (!grown) return false;
        renderer->vertices = grown;
        renderer->vertex_capacity = capacity;
    }

    if (index_count > renderer->index_capacity) {
        uint32_t capacity =
            renderer->index_capacity ? renderer->index_capacity : 4096U;
        while (capacity < index_count) {
            if (capacity > UINT32_MAX / 2U) {
                capacity = index_count;
                break;
            }
            capacity *= 2U;
        }

        int *grown = realloc(
            renderer->indices,
            (size_t)capacity * sizeof(*grown));
        if (!grown) return false;
        renderer->indices = grown;
        renderer->index_capacity = capacity;
    }

    return true;
}

static void flush_geometry(
    OpenRideORMapPyramidRenderer *renderer,
    V11GeometryBatch *batch)
{
    if (!renderer || !batch) return;

    if (batch->vertex_count > 0U && batch->index_count > 0U) {
        SDL_RenderGeometry(
            renderer->renderer,
            NULL,
            renderer->vertices,
            (int)batch->vertex_count,
            renderer->indices,
            (int)batch->index_count);

        if (!renderer->surface_texture_compiling) {
            ++renderer->debug.batches;
        }
    }

    batch->vertex_count = 0U;
    batch->index_count = 0U;
}

static bool reserve_geometry(
    OpenRideORMapPyramidRenderer *renderer,
    V11GeometryBatch *batch,
    uint32_t add_vertices,
    uint32_t add_indices)
{
    if (!renderer || !batch) return false;

    if (add_vertices > V11_GEOMETRY_VERTEX_LIMIT
        || add_indices > V11_GEOMETRY_INDEX_LIMIT) {
        return false;
    }

    if (batch->vertex_count > V11_GEOMETRY_VERTEX_LIMIT - add_vertices
        || batch->index_count > V11_GEOMETRY_INDEX_LIMIT - add_indices) {
        flush_geometry(renderer, batch);
    }

    return ensure_geometry(
        renderer,
        batch->vertex_count + add_vertices,
        batch->index_count + add_indices);
}

static void set_vertex(
    SDL_Vertex *vertex,
    float x,
    float y,
    OpenRideMapColor color)
{
    vertex->position.x = x;
    vertex->position.y = y;
    vertex->color.r = color.r / 255.0f;
    vertex->color.g = color.g / 255.0f;
    vertex->color.b = color.b / 255.0f;
    vertex->color.a = color.a / 255.0f;
    vertex->tex_coord.x = 0.0f;
    vertex->tex_coord.y = 0.0f;
}

static bool draw_triangle(
    OpenRideORMapPyramidRenderer *renderer,
    V11GeometryBatch *batch,
    const float x[3],
    const float y[3],
    OpenRideMapColor color)
{
    if (color.a == 0U) return true;
    if (!reserve_geometry(renderer, batch, 3U, 3U)) return false;

    const uint32_t base_v = batch->vertex_count;
    const uint32_t base_i = batch->index_count;

    for (uint32_t i = 0U; i < 3U; ++i) {
        set_vertex(&renderer->vertices[base_v + i], x[i], y[i], color);
        renderer->indices[base_i + i] = (int)(base_v + i);
    }

    batch->vertex_count += 3U;
    batch->index_count += 3U;
    return true;
}

static void quantized_vertex_to_screen(
    double origin_x,
    double origin_y,
    double quantized_scale,
    uint16_t qx,
    uint16_t qy,
    bool rotate,
    double bearing_cos,
    double bearing_sin,
    double viewport_cx,
    double viewport_cy,
    float *x_out,
    float *y_out)
{
    double x = origin_x + (double)qx * quantized_scale;
    double y = origin_y + (double)qy * quantized_scale;

    if (rotate) {
        const double dx = x - viewport_cx;
        const double dy = y - viewport_cy;

        x = viewport_cx
            + dx * bearing_cos
            + dy * bearing_sin;
        y = viewport_cy
            - dx * bearing_sin
            + dy * bearing_cos;
    }

    *x_out = (float)x;
    *y_out = (float)y;
}

static int visible_index(int zoom)
{
    if (zoom < OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM
        || zoom > OPENRIDE_ORMAP_PYRAMID_BUILDING_ZOOM) {
        return -1;
    }
    return zoom - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM;
}

static void compute_visible_ranges(
    OpenRideORMapPyramidRenderer *renderer,
    const OpenRideMapCamera *camera,
    int viewport_width,
    int viewport_height)
{
    if (!renderer || !camera) return;
    memset(renderer->visible, 0, sizeof(renderer->visible));

    const double margin = 96.0;
    const double sx[4] = {
        -margin, viewport_width + margin,
        viewport_width + margin, -margin
    };
    const double sy[4] = {
        -margin, -margin,
        viewport_height + margin, viewport_height + margin
    };

    double min_x = 1e30, min_y = 1e30;
    double max_x = -1e30, max_y = -1e30;

    for (int i = 0; i < 4; ++i) {
        double lat = 0.0, lon = 0.0;
        openride_screen_to_geo(
            camera,
            sx[i], sy[i],
            viewport_width, viewport_height,
            &lat, &lon);

        const OpenRidePointD world = openride_mercator_forward(lat, lon);
        if (world.x < min_x) min_x = world.x;
        if (world.x > max_x) max_x = world.x;
        if (world.y < min_y) min_y = world.y;
        if (world.y > max_y) max_y = world.y;
    }

    /* Current French extracts never cross the antimeridian. */
    if (max_x - min_x > 0.5) return;

    for (int zoom = OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM;
         zoom <= OPENRIDE_ORMAP_PYRAMID_BUILDING_ZOOM;
         ++zoom) {
        const int index = visible_index(zoom);
        const int count = 1 << zoom;

        int first_x = (int)floor(min_x * count) - 1;
        int last_x = (int)floor(max_x * count) + 1;
        int first_y = (int)floor(min_y * count) - 1;
        int last_y = (int)floor(max_y * count) + 1;

        if (first_x < 0) first_x = 0;
        if (first_y < 0) first_y = 0;
        if (last_x >= count) last_x = count - 1;
        if (last_y >= count) last_y = count - 1;

        renderer->visible[index] = (V11VisibleRange){
            .first_x = first_x,
            .last_x = last_x,
            .first_y = first_y,
            .last_y = last_y,
            .valid = first_x <= last_x && first_y <= last_y
        };
    }
}

static bool tile_is_visible(
    const OpenRideORMapPyramidRenderer *renderer,
    int zoom,
    int x,
    int y)
{
    const int index = visible_index(zoom);
    if (!renderer || index < 0) return false;

    const V11VisibleRange *range = &renderer->visible[index];
    return range->valid
        && x >= range->first_x && x <= range->last_x
        && y >= range->first_y && y <= range->last_y;
}

static void surface_entry_destroy(V11SurfaceCacheEntry *entry)
{
    if (!entry) return;
    if (entry->occupied) {
        openride_ormap_pyramid_surface_tile_destroy(&entry->tile);
    }
    memset(entry, 0, sizeof(*entry));
}

static uint32_t cache_hash(int zoom, int x, int y)
{
    uint32_t value =
        (uint32_t)zoom * UINT32_C(0x9e3779b9);
    value ^=
        (uint32_t)x * UINT32_C(0x85ebca6b);
    value ^=
        (uint32_t)y * UINT32_C(0xc2b2ae35);
    value ^= value >> 16U;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15U;
    return value;
}

static size_t cache_set_base(
    size_t capacity,
    int zoom,
    int x,
    int y)
{
    const size_t set_count =
        capacity / V11_CACHE_ASSOCIATIVITY;

    return
        (size_t)(
            cache_hash(zoom, x, y)
            % (uint32_t)set_count)
        * V11_CACHE_ASSOCIATIVITY;
}

static V11SurfaceCacheEntry *surface_find(
    OpenRideORMapPyramidRenderer *renderer,
    int zoom,
    int x,
    int y)
{
    if (!renderer) return NULL;

    for (size_t way = 0U;
         way < V11_SURFACE_CACHE_CAPACITY;
         ++way) {
        V11SurfaceCacheEntry *entry =
            &renderer->surfaces[way];

        if (entry->occupied
            && entry->zoom == zoom
            && entry->x == x
            && entry->y == y) {
            entry->last_used =
                renderer->frame_counter;
            return entry;
        }
    }

    return NULL;
}

static V11SurfaceCacheEntry *surface_victim(
    OpenRideORMapPyramidRenderer *renderer)
{
    if (!renderer) return NULL;

    V11SurfaceCacheEntry *victim = NULL;

    for (size_t way = 0U;
         way < V11_SURFACE_CACHE_CAPACITY;
         ++way) {
        V11SurfaceCacheEntry *entry =
            &renderer->surfaces[way];

        if (!entry->occupied) return entry;

        /*
         * An entry touched in this frame may already be part of the surface
         * plan. Evicting it while a deeper child is requested would leave a
         * stale draw key in the plan and produce a one-frame hole.
         */
        if (entry->last_used == renderer->frame_counter) {
            continue;
        }

        if (!victim
            || entry->last_used < victim->last_used) {
            victim = entry;
        }
    }

    if (victim) surface_entry_destroy(victim);
    return victim;
}


static void surface_texture_entry_destroy(
    V11SurfaceTextureEntry *entry)
{
    if (!entry) return;

    if (entry->base_texture) {
        SDL_DestroyTexture(entry->base_texture);
    }
    if (entry->builtup_texture) {
        SDL_DestroyTexture(entry->builtup_texture);
    }

    memset(entry, 0, sizeof(*entry));
}

static void surface_texture_cache_clear(
    OpenRideORMapPyramidRenderer *renderer)
{
    if (!renderer) return;

    for (uint32_t i = 0U;
         i < V11_SURFACE_TEXTURE_CACHE_CAPACITY;
         ++i) {
        surface_texture_entry_destroy(
            &renderer->surface_textures[i]);
    }
}

static V11SurfaceTextureEntry *surface_texture_find(
    OpenRideORMapPyramidRenderer *renderer,
    int zoom,
    int x,
    int y)
{
    if (!renderer || !renderer->surface_texture_supported) {
        return NULL;
    }

    for (size_t way = 0U;
         way < V11_SURFACE_TEXTURE_CACHE_CAPACITY;
         ++way) {
        V11SurfaceTextureEntry *entry =
            &renderer->surface_textures[way];

        if (entry->occupied
            && entry->zoom == zoom
            && entry->x == x
            && entry->y == y
            && entry->style == renderer->style) {
            entry->last_used =
                renderer->frame_counter;
            return entry;
        }
    }

    return NULL;
}

static void surface_pin_previous_plan(
    OpenRideORMapPyramidRenderer *renderer)
{
    if (!renderer) return;

    /*
     * Keep the last drawable fallback alive until the replacement plan is
     * complete. Tiles that left the buffered viewport remain immediately
     * evictable, so a pan can still make forward progress without growing the
     * caches.
     */
    for (uint32_t i = 0U;
         i < renderer->surface_plan.count;
         ++i) {
        OpenRideORMapPyramidTileKey key =
            renderer->surface_plan.tiles[i].key;

        /*
         * The planner must traverse every ancestor again before it can reach
         * this drawable leaf. Keep that complete ownership chain alive: if an
         * ancestor were evicted, a budget-deferred reload would omit its whole
         * already-cached descendant subtree from the next frame.
         */
        for (;;) {
            if (tile_is_visible(
                    renderer,
                    key.zoom,
                    key.x,
                    key.y)) {
                (void)surface_find(
                    renderer,
                    key.zoom,
                    key.x,
                    key.y);

                if (renderer->surface_texture_supported) {
                    (void)surface_texture_find(
                        renderer,
                        key.zoom,
                        key.x,
                        key.y);
                }
            }

            if (key.zoom <= OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM) {
                break;
            }

            --key.zoom;
            key.x /= 2;
            key.y /= 2;
        }
    }
}

static V11SurfaceTextureEntry *surface_texture_victim(
    OpenRideORMapPyramidRenderer *renderer)
{
    if (!renderer) return NULL;

    V11SurfaceTextureEntry *victim = NULL;

    for (size_t way = 0U;
         way < V11_SURFACE_TEXTURE_CACHE_CAPACITY;
         ++way) {
        V11SurfaceTextureEntry *entry =
            &renderer->surface_textures[way];

        if (!entry->occupied) return entry;

        if (entry->last_used == renderer->frame_counter) {
            continue;
        }

        if (!victim
            || entry->last_used < victim->last_used) {
            victim = entry;
        }
    }

    if (victim) {
        surface_texture_entry_destroy(victim);
    }

    return victim;
}

static uint32_t surface_cache_occupancy(
    const OpenRideORMapPyramidRenderer *renderer)
{
    if (!renderer) return 0U;

    uint32_t count = 0U;
    for (uint32_t i = 0U;
         i < V11_SURFACE_CACHE_CAPACITY;
         ++i) {
        if (renderer->surfaces[i].occupied) ++count;
    }
    return count;
}

static uint32_t surface_texture_cache_occupancy(
    const OpenRideORMapPyramidRenderer *renderer)
{
    if (!renderer) return 0U;

    uint32_t count = 0U;
    for (uint32_t i = 0U;
         i < V11_SURFACE_TEXTURE_CACHE_CAPACITY;
         ++i) {
        if (renderer->surface_textures[i].occupied) ++count;
    }
    return count;
}

static void surface_raster_colors(
    OpenRideMapStyle style,
    OpenRideMapColor colors[4])
{
    memset(colors, 0, 4U * sizeof(*colors));

    const OpenRideMapPalette palette =
        openride_map_palette(style);

    OpenRideMapColor water = palette.water;
    water.a =
        style == OPENRIDE_MAP_STYLE_TRAIL
            ? 218U
            : 228U;

    OpenRideMapColor green =
        green_color(style);

    OpenRideMapColor builtup = palette.building;
    builtup.a =
        style == OPENRIDE_MAP_STYLE_TRAIL
            ? 78U
            : style == OPENRIDE_MAP_STYLE_TOPO
                ? 88U
                : 96U;

    colors[OPENRIDE_ORMAP_PYRAMID_SURFACE_BUILTUP] =
        builtup;
    colors[OPENRIDE_ORMAP_PYRAMID_SURFACE_WATER] =
        water;
    colors[OPENRIDE_ORMAP_PYRAMID_SURFACE_GREEN] =
        green;
}

static bool surface_rasterize_semantic_pass(
    OpenRideORMapPyramidRenderer *renderer,
    SDL_Texture *target,
    const OpenRideORMapPyramidSurfaceTile *tile,
    const uint8_t *kinds,
    uint32_t kind_count,
    const OpenRideMapColor colors[4])
{
    if (!renderer || !target || !tile || !kinds || kind_count == 0U) {
        return false;
    }

    SDL_Texture *previous_target =
        SDL_GetRenderTarget(renderer->renderer);

    SDL_BlendMode previous_blend =
        SDL_BLENDMODE_NONE;
    const bool have_previous_blend =
        SDL_GetRenderDrawBlendMode(
            renderer->renderer,
            &previous_blend);

    if (!SDL_SetRenderTarget(
            renderer->renderer,
            target)) {
        return false;
    }

    bool ok = true;

    if (!SDL_SetRenderDrawBlendMode(
            renderer->renderer,
            SDL_BLENDMODE_NONE)
        || !SDL_SetRenderDrawColor(
            renderer->renderer,
            0U,
            0U,
            0U,
            0U)
        || !SDL_RenderClear(renderer->renderer)
        || !SDL_SetRenderDrawBlendMode(
            renderer->renderer,
            SDL_BLENDMODE_BLEND)) {
        ok = false;
    }

    renderer->surface_texture_compiling = true;

    V11GeometryBatch batch = {0};

    /*
     * The v11 quantized coordinate domain exactly covers the buffered
     * [-0.5px, 256.5px] surface tile. A 257px render target therefore maps
     * q=0..65535 directly to x/y=0..257; the visible nominal tile is later
     * sampled from source rect 0.5..256.5.
     */
    const float quantized_scale =
        (float)V11_SURFACE_TEXTURE_PIXELS
        / 65535.0f;

    for (uint32_t kind_index = 0U;
         ok && kind_index < kind_count;
         ++kind_index) {
        const uint8_t kind = kinds[kind_index];
        const OpenRideMapColor color =
            colors[kind];

        if (color.a == 0U) continue;

        for (uint32_t t = 0U;
             t < tile->count;
             ++t) {
            const OpenRideORMapPyramidSurfaceTriangle *triangle =
                &tile->triangles[t];

            if (triangle->kind != kind) continue;

            const float x[3] = {
                triangle->x1 * quantized_scale,
                triangle->x2 * quantized_scale,
                triangle->x3 * quantized_scale
            };
            const float y[3] = {
                triangle->y1 * quantized_scale,
                triangle->y2 * quantized_scale,
                triangle->y3 * quantized_scale
            };

            if (!draw_triangle(
                    renderer,
                    &batch,
                    x,
                    y,
                    color)) {
                ok = false;
                break;
            }

            ++renderer->debug.triangles_drawn;
        }

        flush_geometry(renderer, &batch);
    }

    flush_geometry(renderer, &batch);
    renderer->surface_texture_compiling = false;

    if (!SDL_SetRenderTarget(
            renderer->renderer,
            previous_target)) {
        ok = false;
    }

    if (have_previous_blend) {
        (void)SDL_SetRenderDrawBlendMode(
            renderer->renderer,
            previous_blend);
    }

    return ok;
}

static SDL_Texture *surface_create_target(
    OpenRideORMapPyramidRenderer *renderer,
    int width,
    int height)
{
    if (!renderer
        || !renderer->surface_texture_supported
        || width <= 0
        || height <= 0) {
        return NULL;
    }

    SDL_Texture *texture =
        SDL_CreateTexture(
            renderer->renderer,
            SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_TARGET,
            width,
            height);

    if (!texture) return NULL;

    if (!SDL_SetTextureScaleMode(
            texture,
            SDL_SCALEMODE_LINEAR)) {
        SDL_DestroyTexture(texture);
        return NULL;
    }

    return texture;
}

static bool surface_texture_compile(
    OpenRideORMapPyramidRenderer *renderer,
    V11SurfaceCacheEntry *surface)
{
    if (!renderer
        || !surface
        || surface->state
            != OPENRIDE_ORMAP_PYRAMID_TILE_READY
        || surface->tile.count == 0U
        || !renderer->surface_texture_supported) {
        return false;
    }

    if (surface_texture_find(
            renderer,
            surface->zoom,
            surface->x,
            surface->y)) {
        return true;
    }

    if (renderer->surface_texture_compile_budget == 0U) {
        renderer->needs_followup = true;
        return false;
    }

    V11SurfaceTextureEntry *entry =
        surface_texture_victim(renderer);

    if (!entry) {
        /*
         * All four ways are already needed by this frame. Treat saturation as
         * a normal deferral, not as a GPU capability failure.
         */
        renderer->surface_texture_compile_budget = 0U;
        renderer->needs_followup = true;
        return false;
    }

    --renderer->surface_texture_compile_budget;

    const uint64_t started_ns =
        SDL_GetTicksNS();

    SDL_Texture *base =
        surface_create_target(
            renderer,
            V11_SURFACE_TEXTURE_PIXELS,
            V11_SURFACE_TEXTURE_PIXELS);
    SDL_Texture *builtup =
        surface_create_target(
            renderer,
            V11_SURFACE_TEXTURE_PIXELS,
            V11_SURFACE_TEXTURE_PIXELS);

    if (!base || !builtup) {
        if (base) SDL_DestroyTexture(base);
        if (builtup) SDL_DestroyTexture(builtup);
        return false;
    }

    OpenRideMapColor colors[4];
    surface_raster_colors(
        renderer->style,
        colors);

    /*
     * Base semantics get a deterministic order instead of relying on OSM
     * visitation order: GREEN first, WATER above it. BUILTUP remains
     * independent so its high-zoom attenuation stays continuous.
     */
    static const uint8_t base_kinds[] = {
        OPENRIDE_ORMAP_PYRAMID_SURFACE_GREEN,
        OPENRIDE_ORMAP_PYRAMID_SURFACE_WATER
    };
    static const uint8_t builtup_kinds[] = {
        OPENRIDE_ORMAP_PYRAMID_SURFACE_BUILTUP
    };

    bool ok =
        surface_rasterize_semantic_pass(
            renderer,
            base,
            &surface->tile,
            base_kinds,
            2U,
            colors)
        && surface_rasterize_semantic_pass(
            renderer,
            builtup,
            &surface->tile,
            builtup_kinds,
            1U,
            colors);

    if (ok) {
        ok =
            SDL_SetTextureBlendMode(
                base,
                renderer->surface_add_blend)
            && SDL_SetTextureBlendMode(
                builtup,
                renderer->surface_add_blend)
            && SDL_SetTextureColorModFloat(
                base,
                1.0f,
                1.0f,
                1.0f)
            && SDL_SetTextureColorModFloat(
                builtup,
                1.0f,
                1.0f,
                1.0f)
            && SDL_SetTextureAlphaModFloat(
                base,
                1.0f)
            && SDL_SetTextureAlphaModFloat(
                builtup,
                1.0f);
    }

    if (!ok) {
        SDL_DestroyTexture(base);
        SDL_DestroyTexture(builtup);
        return false;
    }

    *entry = (V11SurfaceTextureEntry){
        .occupied = true,
        .zoom = surface->zoom,
        .x = surface->x,
        .y = surface->y,
        .style = renderer->style,
        .last_used = renderer->frame_counter,
        .base_texture = base,
        .builtup_texture = builtup
    };

    renderer->debug.load_ms +=
        (double)(SDL_GetTicksNS() - started_ns)
        / 1000000.0;

    renderer->needs_followup = true;
    return true;
}

static void surface_disable_texture_path(
    OpenRideORMapPyramidRenderer *renderer)
{
    if (!renderer) return;

    surface_texture_cache_clear(renderer);

    if (renderer->surface_backend_compositor) {
        SDL_DestroyTexture(renderer->surface_backend_compositor);
        renderer->surface_backend_compositor = NULL;
    }
    renderer->surface_capture_gpu_compositor = false;
    renderer->surface_capture_backend_started = false;
    renderer->surface_capture_present_valid = false;
    renderer->surface_capture_failed = false;
    renderer->surface_capture_has_src = false;
    renderer->surface_capture_has_dst = false;
    renderer->surface_capture_gpu_weight = 0.0;

    if (renderer->surface_compositor) {
        SDL_DestroyTexture(
            renderer->surface_compositor);
        renderer->surface_compositor = NULL;
    }

    renderer->surface_compositor_width = 0;
    renderer->surface_compositor_height = 0;
    renderer->surface_texture_supported = false;

    SDL_Log(
        "OpenRide v11: surface GPU cache disabled; "
        "falling back to vector geometry");
}

typedef struct V11SurfaceBackendBlendState {
    SDL_Texture *previous_target;
    SDL_BlendMode previous_draw_blend;
    Uint8 previous_r;
    Uint8 previous_g;
    Uint8 previous_b;
    Uint8 previous_a;
    bool have_draw_blend;
    bool have_draw_color;
} V11SurfaceBackendBlendState;

static SDL_BlendMode surface_backend_add_blend_mode(void)
{
    return SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ONE,
        SDL_BLENDFACTOR_ONE,
        SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ONE,
        SDL_BLENDFACTOR_ONE,
        SDL_BLENDOPERATION_ADD);
}

static SDL_BlendMode surface_backend_over_blend_mode(void)
{
    return SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ONE,
        SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ONE,
        SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        SDL_BLENDOPERATION_ADD);
}

static double surface_backend_blend_mix(double camera_zoom)
{
    const double span =
        V11_SURFACE_BACKEND_BLEND_END_ZOOM
        - V11_SURFACE_BACKEND_BLEND_START_ZOOM;
    if (span <= 0.0) return 1.0;

    const double t = clamp01(
        (camera_zoom - V11_SURFACE_BACKEND_BLEND_START_ZOOM) / span);
    return t * t * (3.0 - 2.0 * t);
}

static bool surface_backend_compositor_ensure(
    OpenRideORMapPyramidRenderer *renderer,
    SDL_Texture *reference)
{
    if (!renderer || !reference || reference->w <= 0 || reference->h <= 0) {
        return false;
    }

    if (renderer->surface_backend_compositor
        && renderer->surface_backend_compositor->format == reference->format
        && renderer->surface_backend_compositor->w == reference->w
        && renderer->surface_backend_compositor->h == reference->h) {
        return true;
    }

    if (renderer->surface_backend_compositor) {
        SDL_DestroyTexture(renderer->surface_backend_compositor);
        renderer->surface_backend_compositor = NULL;
    }

    renderer->surface_backend_compositor = SDL_CreateTexture(
        renderer->renderer,
        reference->format,
        SDL_TEXTUREACCESS_TARGET,
        reference->w,
        reference->h);
    if (!renderer->surface_backend_compositor) return false;

    if (!SDL_SetTextureBlendMode(
            renderer->surface_backend_compositor,
            surface_backend_over_blend_mode())) {
        SDL_DestroyTexture(renderer->surface_backend_compositor);
        renderer->surface_backend_compositor = NULL;
        return false;
    }

    return true;
}

static bool surface_backend_weighted_copy(
    OpenRideORMapPyramidRenderer *renderer,
    SDL_Texture *source,
    double weight)
{
    if (!renderer || !source) return false;

    const float w = (float)clamp01(weight);
    bool ok = SDL_SetTextureBlendMode(
        source,
        surface_backend_add_blend_mode());

    if (ok) {
        ok = SDL_SetTextureColorModFloat(source, w, w, w);
    }
    if (ok) {
        ok = SDL_SetTextureAlphaModFloat(source, w);
    }
    if (ok) {
        ok = SDL_RenderTexture(
            renderer->renderer,
            source,
            NULL,
            NULL);
    }

    const bool color_reset = SDL_SetTextureColorModFloat(
        source, 1.0f, 1.0f, 1.0f);
    const bool alpha_reset = SDL_SetTextureAlphaModFloat(source, 1.0f);
    const bool blend_reset = SDL_SetTextureBlendMode(
        source,
        surface_backend_over_blend_mode());

    return ok && color_reset && alpha_reset && blend_reset;
}

static bool surface_backend_clear_target(
    OpenRideORMapPyramidRenderer *renderer,
    SDL_Texture *target)
{
    if (!renderer || !target) return false;

    SDL_Texture *previous_target = SDL_GetRenderTarget(renderer->renderer);
    SDL_BlendMode previous_blend = SDL_BLENDMODE_NONE;
    Uint8 previous_r = 0U;
    Uint8 previous_g = 0U;
    Uint8 previous_b = 0U;
    Uint8 previous_a = 0U;
    const bool have_blend = SDL_GetRenderDrawBlendMode(
        renderer->renderer, &previous_blend);
    const bool have_color = SDL_GetRenderDrawColor(
        renderer->renderer,
        &previous_r, &previous_g, &previous_b, &previous_a);

    bool ok = SDL_SetRenderTarget(renderer->renderer, target);
    if (ok) {
        ok = SDL_SetRenderDrawBlendMode(
            renderer->renderer, SDL_BLENDMODE_NONE);
    }
    if (ok) {
        ok = SDL_SetRenderDrawColor(
            renderer->renderer, 0U, 0U, 0U, 0U);
    }
    if (ok) {
        ok = SDL_RenderClear(renderer->renderer);
    }

    SDL_SetRenderTarget(renderer->renderer, previous_target);
    if (have_blend) {
        SDL_SetRenderDrawBlendMode(renderer->renderer, previous_blend);
    }
    if (have_color) {
        SDL_SetRenderDrawColor(
            renderer->renderer,
            previous_r, previous_g, previous_b, previous_a);
    }

    return ok;
}

static void surface_backend_capture_begin(
    OpenRideORMapPyramidRenderer *renderer,
    double gpu_weight)
{
    if (!renderer) return;

    renderer->surface_capture_gpu_compositor = true;
    renderer->surface_capture_backend_started = false;
    renderer->surface_capture_present_valid = false;
    renderer->surface_capture_failed = false;
    renderer->surface_capture_has_src = false;
    renderer->surface_capture_has_dst = false;
    renderer->surface_capture_gpu_weight = clamp01(gpu_weight);
}

static bool surface_backend_capture_end(
    OpenRideORMapPyramidRenderer *renderer)
{
    if (!renderer) return false;

    renderer->surface_capture_gpu_compositor = false;
    return renderer->surface_capture_backend_started
        && renderer->surface_capture_present_valid
        && !renderer->surface_capture_failed
        && renderer->surface_backend_compositor;
}

static bool surface_backend_present_or_capture(
    OpenRideORMapPyramidRenderer *renderer,
    SDL_Texture *texture,
    const SDL_FRect *srcrect,
    const SDL_FRect *dstrect)
{
    if (!renderer || !texture) return false;

    if (!renderer->surface_capture_gpu_compositor) {
        return SDL_RenderTexture(
            renderer->renderer,
            texture,
            srcrect,
            dstrect);
    }

    if (!renderer->surface_capture_backend_started) {
        if (!surface_backend_compositor_ensure(renderer, texture)) {
            renderer->surface_capture_failed = true;
            return false;
        }
        if (!surface_backend_clear_target(
                renderer,
                renderer->surface_backend_compositor)) {
            renderer->surface_capture_failed = true;
            return false;
        }
        renderer->surface_capture_backend_started = true;
    }

    renderer->surface_capture_has_src = srcrect != NULL;
    renderer->surface_capture_has_dst = dstrect != NULL;
    if (srcrect) renderer->surface_capture_src = *srcrect;
    if (dstrect) renderer->surface_capture_dst = *dstrect;
    renderer->surface_capture_present_valid = true;

    SDL_Texture *previous_target = SDL_GetRenderTarget(renderer->renderer);
    bool ok = SDL_SetRenderTarget(
        renderer->renderer,
        renderer->surface_backend_compositor);
    if (ok) {
        ok = surface_backend_weighted_copy(
            renderer,
            texture,
            renderer->surface_capture_gpu_weight);
    }
    SDL_SetRenderTarget(renderer->renderer, previous_target);

    if (!ok) renderer->surface_capture_failed = true;
    return ok;
}

static void surface_backend_restore_state(
    OpenRideORMapPyramidRenderer *renderer,
    const V11SurfaceBackendBlendState *state)
{
    if (!renderer || !state) return;

    SDL_SetRenderTarget(renderer->renderer, state->previous_target);

    if (state->have_draw_blend) {
        SDL_SetRenderDrawBlendMode(
            renderer->renderer,
            state->previous_draw_blend);
    }
    if (state->have_draw_color) {
        SDL_SetRenderDrawColor(
            renderer->renderer,
            state->previous_r,
            state->previous_g,
            state->previous_b,
            state->previous_a);
    }
}

static void surface_backend_capture_state(
    OpenRideORMapPyramidRenderer *renderer,
    V11SurfaceBackendBlendState *state)
{
    if (!state) return;

    memset(state, 0, sizeof(*state));
    if (!renderer) return;

    state->previous_target = SDL_GetRenderTarget(renderer->renderer);
    state->have_draw_blend = SDL_GetRenderDrawBlendMode(
        renderer->renderer,
        &state->previous_draw_blend);
    state->have_draw_color = SDL_GetRenderDrawColor(
        renderer->renderer,
        &state->previous_r,
        &state->previous_g,
        &state->previous_b,
        &state->previous_a);
}

static bool surface_backend_vector_start(
    OpenRideORMapPyramidRenderer *renderer,
    V11SurfaceBackendBlendState *state)
{
    if (!renderer || !renderer->surface_compositor
        || !renderer->surface_backend_compositor || !state) {
        return false;
    }

    surface_backend_capture_state(renderer, state);

    bool ok = SDL_SetRenderTarget(
        renderer->renderer,
        renderer->surface_compositor);
    if (ok) {
        ok = SDL_SetRenderDrawBlendMode(
            renderer->renderer,
            SDL_BLENDMODE_NONE);
    }
    if (ok) {
        ok = SDL_SetRenderDrawColor(
            renderer->renderer, 0U, 0U, 0U, 0U);
    }
    if (ok) {
        ok = SDL_RenderClear(renderer->renderer);
    }
    if (ok) {
        ok = SDL_SetRenderDrawBlendMode(
            renderer->renderer,
            SDL_BLENDMODE_BLEND);
    }

    if (!ok) {
        surface_backend_restore_state(renderer, state);
    }
    return ok;
}

static bool surface_backend_blend_finish(
    OpenRideORMapPyramidRenderer *renderer,
    double vector_weight,
    const V11SurfaceBackendBlendState *state)
{
    if (!renderer || !renderer->surface_compositor
        || !renderer->surface_backend_compositor || !state) {
        if (renderer && state) {
            surface_backend_restore_state(renderer, state);
        }
        return false;
    }

    bool ok = SDL_SetRenderTarget(
        renderer->renderer,
        renderer->surface_backend_compositor);
    if (ok) {
        ok = surface_backend_weighted_copy(
            renderer,
            renderer->surface_compositor,
            vector_weight);
    }

    surface_backend_restore_state(renderer, state);

    if (ok) {
        ok = SDL_SetTextureBlendMode(
            renderer->surface_backend_compositor,
            surface_backend_over_blend_mode());
    }
    if (ok) {
        ok = SDL_SetTextureColorModFloat(
            renderer->surface_backend_compositor,
            1.0f, 1.0f, 1.0f);
    }
    if (ok) {
        ok = SDL_SetTextureAlphaModFloat(
            renderer->surface_backend_compositor,
            1.0f);
    }

    const SDL_FRect *src =
        renderer->surface_capture_has_src
            ? &renderer->surface_capture_src
            : NULL;
    const SDL_FRect *dst =
        renderer->surface_capture_has_dst
            ? &renderer->surface_capture_dst
            : NULL;

    if (ok) {
        ok = SDL_RenderTexture(
            renderer->renderer,
            renderer->surface_backend_compositor,
            src,
            dst);
    }

    return ok;
}


static void surface_prepare_visible_textures(
    OpenRideORMapPyramidRenderer *renderer,
    const OpenRideMapCamera *camera)
{
    if (!renderer
        || !camera
        || !renderer->surface_texture_supported
        || renderer->surface_texture_compile_budget == 0U) {
        return;
    }

    int desired_zoom =
        (int)floor(camera->zoom) + 1;

    if (desired_zoom
        < OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM) {
        desired_zoom =
            OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM;
    }
    if (desired_zoom
        > OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM) {
        desired_zoom =
            OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM;
    }

    /*
     * Parent-first warmup guarantees there is always a drawable fallback,
     * while the second pass prioritizes the camera's next refinement level.
     */
    for (int pass = 0;
         pass < 2
         && renderer->surface_texture_compile_budget > 0U;
         ++pass) {
        const int begin_zoom =
            pass == 0
                ? OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM
                : desired_zoom;
        const int end_zoom =
            pass == 0
                ? desired_zoom
                : OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM;
        const int step =
            pass == 0 ? 1 : -1;

        for (int zoom = begin_zoom;
             renderer->surface_texture_compile_budget > 0U;
             zoom += step) {
            if ((step > 0 && zoom > end_zoom)
                || (step < 0 && zoom < end_zoom)) {
                break;
            }

            for (uint32_t i = 0U;
                 i < V11_SURFACE_CACHE_CAPACITY
                 && renderer->surface_texture_compile_budget > 0U;
                 ++i) {
                V11SurfaceCacheEntry *surface =
                    &renderer->surfaces[i];

                if (!surface->occupied
                    || surface->state
                        != OPENRIDE_ORMAP_PYRAMID_TILE_READY
                    || surface->zoom != zoom
                    || !tile_is_visible(
                        renderer,
                        surface->zoom,
                        surface->x,
                        surface->y)
                    || surface_texture_find(
                        renderer,
                        surface->zoom,
                        surface->x,
                        surface->y)) {
                    continue;
                }

                if (!surface_texture_compile(
                        renderer,
                        surface)) {
                    if (renderer->surface_texture_compile_budget > 0U) {
                        surface_disable_texture_path(
                            renderer);
                    }
                    return;
                }
            }
        }
    }
}

static bool surface_probe_texture_path(
    OpenRideORMapPyramidRenderer *renderer)
{
    if (!renderer || !renderer->renderer) {
        return false;
    }

    renderer->surface_add_blend =
        SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ONE,
            SDL_BLENDFACTOR_ONE,
            SDL_BLENDOPERATION_ADD,
            SDL_BLENDFACTOR_ONE,
            SDL_BLENDFACTOR_ONE,
            SDL_BLENDOPERATION_ADD);

    renderer->surface_premult_blend =
        SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ONE,
            SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            SDL_BLENDOPERATION_ADD,
            SDL_BLENDFACTOR_ONE,
            SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            SDL_BLENDOPERATION_ADD);

    SDL_Texture *probe =
        SDL_CreateTexture(
            renderer->renderer,
            SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_TARGET,
            4,
            4);

    if (!probe) return false;

    SDL_Texture *previous_target =
        SDL_GetRenderTarget(renderer->renderer);

    bool ok =
        SDL_SetTextureBlendMode(
            probe,
            renderer->surface_add_blend)
        && SDL_SetTextureBlendMode(
            probe,
            renderer->surface_premult_blend)
        && SDL_SetTextureColorModFloat(
            probe,
            0.5f,
            0.5f,
            0.5f)
        && SDL_SetTextureAlphaModFloat(
            probe,
            0.5f)
        && SDL_SetTextureScaleMode(
            probe,
            SDL_SCALEMODE_LINEAR)
        && SDL_SetRenderTarget(
            renderer->renderer,
            probe)
        && SDL_SetRenderTarget(
            renderer->renderer,
            previous_target);

    SDL_DestroyTexture(probe);
    return ok;
}

static bool surface_ensure_compositor(
    OpenRideORMapPyramidRenderer *renderer,
    int viewport_width,
    int viewport_height)
{
    if (!renderer
        || !renderer->surface_texture_supported
        || viewport_width <= 0
        || viewport_height <= 0) {
        return false;
    }

    if (renderer->surface_compositor
        && renderer->surface_compositor_width
            == viewport_width
        && renderer->surface_compositor_height
            == viewport_height) {
        return true;
    }

    if (renderer->surface_compositor) {
        SDL_DestroyTexture(
            renderer->surface_compositor);
        renderer->surface_compositor = NULL;
    }

    renderer->surface_compositor =
        surface_create_target(
            renderer,
            viewport_width,
            viewport_height);

    if (!renderer->surface_compositor) {
        return false;
    }

    if (!SDL_SetTextureBlendMode(
            renderer->surface_compositor,
            renderer->surface_premult_blend)
        || !SDL_SetTextureColorModFloat(
            renderer->surface_compositor,
            1.0f,
            1.0f,
            1.0f)
        || !SDL_SetTextureAlphaModFloat(
            renderer->surface_compositor,
            1.0f)) {
        SDL_DestroyTexture(
            renderer->surface_compositor);
        renderer->surface_compositor = NULL;
        return false;
    }

    renderer->surface_compositor_width =
        viewport_width;
    renderer->surface_compositor_height =
        viewport_height;

    return true;
}

static bool surface_render_tile_texture(
    OpenRideORMapPyramidRenderer *renderer,
    const OpenRideMapCamera *camera,
    OpenRideORMapPyramidTileKey key,
    SDL_Texture *texture,
    double factor,
    int viewport_width,
    int viewport_height)
{
    if (!renderer
        || !camera
        || !texture
        || factor <= 0.0005) {
        return true;
    }

    const OpenRidePointD center =
        openride_mercator_forward(
            camera->center_lat,
            camera->center_lon);

    const double world_size =
        openride_world_size_pixels(
            camera->zoom);

    const double tile_size =
        world_size
        / (double)(1 << key.zoom);

    const double center_x =
        center.x * world_size;
    const double center_y =
        center.y * world_size;

    const double viewport_cx =
        viewport_width * 0.5;
    const double viewport_cy =
        viewport_height * 0.5;

    SDL_FRect source = {
        .x = 0.5f,
        .y = 0.5f,
        .w = 256.0f,
        .h = 256.0f
    };

    SDL_FRect destination = {
        .x =
            (float)(
                viewport_cx
                + (double)key.x * tile_size
                - center_x),
        .y =
            (float)(
                viewport_cy
                + (double)key.y * tile_size
                - center_y),
        .w = (float)tile_size,
        .h = (float)tile_size
    };

    const float weight =
        (float)clamp01(factor);

    if (!SDL_SetTextureColorModFloat(
            texture,
            weight,
            weight,
            weight)
        || !SDL_SetTextureAlphaModFloat(
            texture,
            weight)) {
        return false;
    }

    bool ok = true;

    if (fabs(camera->bearing_deg) < 1e-12) {
        ok =
            SDL_RenderTexture(
                renderer->renderer,
                texture,
                &source,
                &destination);
    } else {
        const SDL_FPoint rotation_center = {
            .x =
                (float)viewport_cx
                - destination.x,
            .y =
                (float)viewport_cy
                - destination.y
        };

        /*
         * Camera bearing rotates map geometry opposite the camera. SDL's
         * texture rotation is clockwise, hence the negative bearing.
         */
        ok =
            SDL_RenderTextureRotated(
                renderer->renderer,
                texture,
                &source,
                &destination,
                -camera->bearing_deg,
                &rotation_center,
                SDL_FLIP_NONE);
    }

    (void)SDL_SetTextureColorModFloat(
        texture,
        1.0f,
        1.0f,
        1.0f);
    (void)SDL_SetTextureAlphaModFloat(
        texture,
        1.0f);

    return ok;
}

typedef enum V11SurfaceTexturePass {
    V11_SURFACE_TEXTURE_PASS_BASE = 0,
    V11_SURFACE_TEXTURE_PASS_BUILTUP = 1
} V11SurfaceTexturePass;

static bool surface_composite_pass(
    OpenRideORMapPyramidRenderer *renderer,
    const OpenRideMapCamera *camera,
    int viewport_width,
    int viewport_height,
    V11SurfaceTexturePass pass,
    double global_factor)
{
    if (!renderer
        || !camera
        || !renderer->surface_compositor
        || global_factor <= 0.0005) {
        return true;
    }

    SDL_Texture *previous_target =
        SDL_GetRenderTarget(renderer->renderer);

    SDL_BlendMode previous_blend =
        SDL_BLENDMODE_NONE;
    const bool have_previous_blend =
        SDL_GetRenderDrawBlendMode(
            renderer->renderer,
            &previous_blend);

    if (!SDL_SetRenderTarget(
            renderer->renderer,
            renderer->surface_compositor)
        || !SDL_SetRenderDrawBlendMode(
            renderer->renderer,
            SDL_BLENDMODE_NONE)
        || !SDL_SetRenderDrawColor(
            renderer->renderer,
            0U,
            0U,
            0U,
            0U)
        || !SDL_RenderClear(renderer->renderer)) {
        (void)SDL_SetRenderTarget(
            renderer->renderer,
            previous_target);
        return false;
    }

    bool ok = true;

    /*
     * Every cached tile texture stores premultiplied RGB/A because it was
     * rasterized with source-over onto transparency. Parent and child
     * textures are therefore weighted in both RGB and A, then accumulated
     * with ONE + ONE. Where both LODs describe the same coverage:
     *
     *   parent*(1-t) + child*t
     *
     * preserves the original premultiplied pixel exactly.
     */
    for (uint32_t p = 0U;
         p < renderer->surface_plan.count;
         ++p) {
        const OpenRideORMapPyramidDrawTile *draw =
            &renderer->surface_plan.tiles[p];

        V11SurfaceTextureEntry *entry =
            surface_texture_find(
                renderer,
                draw->key.zoom,
                draw->key.x,
                draw->key.y);

        if (!entry) {
            ++renderer->debug.surface_missing_textures;
            ok = false;
            break;
        }

        SDL_Texture *texture =
            pass == V11_SURFACE_TEXTURE_PASS_BASE
                ? entry->base_texture
                : entry->builtup_texture;

        if (!surface_render_tile_texture(
                renderer,
                camera,
                draw->key,
                texture,
                draw->alpha * global_factor,
                viewport_width,
                viewport_height)) {
            ok = false;
            break;
        }

        if (pass == V11_SURFACE_TEXTURE_PASS_BASE) {
            ++renderer->debug.surface_draw_tiles;
            renderer->debug.surface_draw_alpha +=
                draw->alpha * global_factor;
        }
    }

    if (!SDL_SetRenderTarget(
            renderer->renderer,
            previous_target)) {
        ok = false;
    }

    if (ok) {
        SDL_FRect destination = {
            .x = 0.0f,
            .y = 0.0f,
            .w = (float)viewport_width,
            .h = (float)viewport_height
        };

        ok =
            surface_backend_present_or_capture(
            renderer,
            renderer->surface_compositor,
            NULL,
            &destination);

        if (ok) {
            ++renderer->debug.batches;
        }
    }

    if (have_previous_blend) {
        (void)SDL_SetRenderDrawBlendMode(
            renderer->renderer,
            previous_blend);
    }

    return ok;
}

static bool draw_surface_plan_textured(
    OpenRideORMapPyramidRenderer *renderer,
    const OpenRideMapCamera *camera,
    int viewport_width,
    int viewport_height,
    double surface_alpha)
{
    if (!renderer
        || !camera
        || !renderer->surface_texture_supported) {
        return false;
    }

    if (!surface_ensure_compositor(
            renderer,
            viewport_width,
            viewport_height)) {
        return false;
    }

    if (!surface_composite_pass(
            renderer,
            camera,
            viewport_width,
            viewport_height,
            V11_SURFACE_TEXTURE_PASS_BASE,
            surface_alpha)) {
        return false;
    }

    const double builtup_context =
        1.0
        - 0.78
            * smoothstep(
                camera->zoom,
                11.60,
                15.20);

    if (!surface_composite_pass(
            renderer,
            camera,
            viewport_width,
            viewport_height,
            V11_SURFACE_TEXTURE_PASS_BUILTUP,
            surface_alpha * builtup_context)) {
        return false;
    }

    return true;
}

static OpenRideORMapPyramidTileState surface_state(
    void *userdata,
    OpenRideORMapPyramidTileKey key)
{
    OpenRideORMapPyramidRenderer *renderer = userdata;
    if (!renderer) return OPENRIDE_ORMAP_PYRAMID_TILE_EMPTY;

    /*
     * The planner is viewport-agnostic. Off-screen children are frame-local
     * EMPTY so a visible z9 root never loads its whole z9->z14 subtree.
     */
    if (!tile_is_visible(renderer, key.zoom, key.x, key.y)) {
        return OPENRIDE_ORMAP_PYRAMID_TILE_EMPTY;
    }

    V11SurfaceCacheEntry *entry =
        surface_find(
            renderer,
            key.zoom,
            key.x,
            key.y);

    if (!entry) {
        return OPENRIDE_ORMAP_PYRAMID_TILE_UNKNOWN;
    }

    if (!renderer->surface_texture_supported) {
        return entry->state;
    }

    if (entry->state
        == OPENRIDE_ORMAP_PYRAMID_TILE_EMPTY) {
        return OPENRIDE_ORMAP_PYRAMID_TILE_EMPTY;
    }

    if (entry->state
            == OPENRIDE_ORMAP_PYRAMID_TILE_READY
        && surface_texture_find(
            renderer,
            key.zoom,
            key.x,
            key.y)) {
        return OPENRIDE_ORMAP_PYRAMID_TILE_READY;
    }

    /*
     * Data is available but its GPU tile is still being rasterized. Conversely,
     * a surviving texture without its vector data was reported UNKNOWN above
     * so the backend blend cannot expose a vector-side hole. REQUESTED keeps
     * the nearest ready parent fully alive; availability ramp timing only
     * starts once all four child textures genuinely exist.
     */
    if (entry->state
        == OPENRIDE_ORMAP_PYRAMID_TILE_READY) {
        return OPENRIDE_ORMAP_PYRAMID_TILE_REQUESTED;
    }

    return entry->state;
}

static void surface_request(
    void *userdata,
    OpenRideORMapPyramidTileKey key)
{
    OpenRideORMapPyramidRenderer *renderer = userdata;

    if (!renderer
        || !tile_is_visible(renderer, key.zoom, key.x, key.y)
        || surface_find(renderer, key.zoom, key.x, key.y)) {
        return;
    }

    if (renderer->surface_load_budget == 0U) {
        ++renderer->debug.deferred_loads;
        renderer->needs_followup = true;
        return;
    }

    V11SurfaceCacheEntry *entry =
        surface_victim(renderer);
    if (!entry) {
        ++renderer->debug.deferred_loads;
        renderer->needs_followup = true;
        return;
    }

    OpenRideORMapPyramidSurfaceTile tile = {0};
    char error[160] = {0};

    const uint64_t started = SDL_GetTicksNS();
    const bool ok = openride_ormap_pyramid_surface_load_tile(
        renderer->surface_map,
        key.zoom, key.x, key.y,
        &tile,
        error, sizeof(error));

    renderer->debug.load_ms +=
        (double)(SDL_GetTicksNS() - started) / 1000000.0;
    --renderer->surface_load_budget;
    ++renderer->debug.draw_loads;

    if (!ok) {
        SDL_Log(
            "OpenRide v11 surface z%d/%d/%d: %s",
            key.zoom, key.x, key.y,
            error[0] ? error : "load failed");
        openride_ormap_pyramid_surface_tile_destroy(&tile);
        *entry = (V11SurfaceCacheEntry){
            .occupied = true,
            .zoom = key.zoom,
            .x = key.x,
            .y = key.y,
            .last_used = renderer->frame_counter,
            .state = OPENRIDE_ORMAP_PYRAMID_TILE_EMPTY
        };
        return;
    }

    *entry = (V11SurfaceCacheEntry){
        .occupied = true,
        .zoom = key.zoom,
        .x = key.x,
        .y = key.y,
        .last_used = renderer->frame_counter,
        .state = tile.count > 0U
            ? OPENRIDE_ORMAP_PYRAMID_TILE_READY
            : OPENRIDE_ORMAP_PYRAMID_TILE_EMPTY,
        .tile = tile
    };

    if (renderer->surface_texture_supported
        && tile.count > 0U
        && renderer->surface_texture_compile_budget > 0U
        && !surface_texture_compile(
            renderer,
            entry)
        && renderer->surface_texture_compile_budget > 0U) {
        surface_disable_texture_path(renderer);
    }

    renderer->needs_followup = true;
}

static bool build_surface_roots(
    OpenRideORMapPyramidRenderer *renderer,
    OpenRideORMapPyramidTileKey **roots_out,
    uint32_t *root_count_out)
{
    if (!renderer || !roots_out || !root_count_out) return false;

    *roots_out = NULL;
    *root_count_out = 0U;

    const int index = visible_index(OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM);
    if (index < 0 || !renderer->visible[index].valid) return true;

    const V11VisibleRange range = renderer->visible[index];
    const uint64_t width = (uint64_t)(range.last_x - range.first_x + 1);
    const uint64_t height = (uint64_t)(range.last_y - range.first_y + 1);
    const uint64_t count64 = width * height;

    if (count64 == 0U || count64 > UINT32_MAX
        || count64 > SIZE_MAX / sizeof(OpenRideORMapPyramidTileKey)) {
        return false;
    }

    OpenRideORMapPyramidTileKey *roots =
        malloc((size_t)count64 * sizeof(*roots));
    if (!roots) return false;

    uint32_t count = 0U;
    for (int y = range.first_y; y <= range.last_y; ++y) {
        for (int x = range.first_x; x <= range.last_x; ++x) {
            roots[count++] = (OpenRideORMapPyramidTileKey){
                .zoom = OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM,
                .x = x,
                .y = y
            };
        }
    }

    *roots_out = roots;
    *root_count_out = count;
    return true;
}

static void draw_surface_plan(
    OpenRideORMapPyramidRenderer *renderer,
    const OpenRideMapCamera *camera,
    int viewport_width,
    int viewport_height,
    double surface_alpha)
{
    const OpenRidePointD center =
        openride_mercator_forward(
            camera->center_lat,
            camera->center_lon);

    const double world_size =
        openride_world_size_pixels(camera->zoom);
    const double center_x =
        center.x * world_size;
    const double center_y =
        center.y * world_size;

    const double viewport_cx =
        viewport_width * 0.5;
    const double viewport_cy =
        viewport_height * 0.5;

    const bool rotate =
        fabs(camera->bearing_deg) >= 1e-12;
    const double angle =
        camera->bearing_deg
        * 3.14159265358979323846 / 180.0;
    const double bearing_cos =
        rotate ? cos(angle) : 1.0;
    const double bearing_sin =
        rotate ? sin(angle) : 0.0;

    OpenRideMapColor base_colors[4];
    surface_base_colors(
        renderer->style,
        camera->zoom,
        surface_alpha,
        base_colors);

    V11GeometryBatch batch = {0};

    for (uint32_t p = 0U;
         p < renderer->surface_plan.count;
         ++p) {
        const OpenRideORMapPyramidDrawTile *draw =
            &renderer->surface_plan.tiles[p];

        V11SurfaceCacheEntry *entry =
            surface_find(
                renderer,
                draw->key.zoom,
                draw->key.x,
                draw->key.y);

        if (!entry
            || entry->state
                != OPENRIDE_ORMAP_PYRAMID_TILE_READY) {
            ++renderer->debug.surface_missing_data;
            continue;
        }

        ++renderer->debug.tiles_visited;

        const double tile_size =
            world_size
            / (double)(1 << draw->key.zoom);

        const double left =
            viewport_cx
            + (double)draw->key.x * tile_size
            - center_x;
        const double top =
            viewport_cy
            + (double)draw->key.y * tile_size
            - center_y;

        const double buffer =
            OPENRIDE_ORMAP_PYRAMID_SURFACE_BUFFER_FRACTION;
        const double origin_x =
            left - buffer * tile_size;
        const double origin_y =
            top - buffer * tile_size;
        const double quantized_scale =
            tile_size
            * (1.0 + 2.0 * buffer)
            / 65535.0;

        OpenRideMapColor draw_colors[4] = {
            base_colors[0],
            base_colors[1],
            base_colors[2],
            base_colors[3]
        };

        for (int kind =
                 OPENRIDE_ORMAP_PYRAMID_SURFACE_BUILTUP;
             kind <=
                 OPENRIDE_ORMAP_PYRAMID_SURFACE_GREEN;
             ++kind) {
            draw_colors[kind].a =
                scaled_alpha(
                    draw_colors[kind].a,
                    draw->alpha);
        }

        for (uint32_t t = 0U;
             t < entry->tile.count;
             ++t) {
            const OpenRideORMapPyramidSurfaceTriangle *triangle =
                &entry->tile.triangles[t];

            if (triangle->kind
                    < OPENRIDE_ORMAP_PYRAMID_SURFACE_BUILTUP
                || triangle->kind
                    > OPENRIDE_ORMAP_PYRAMID_SURFACE_GREEN) {
                continue;
            }

            const OpenRideMapColor color =
                draw_colors[triangle->kind];

            if (color.a == 0U) continue;

            const uint16_t qx[3] = {
                triangle->x1,
                triangle->x2,
                triangle->x3
            };
            const uint16_t qy[3] = {
                triangle->y1,
                triangle->y2,
                triangle->y3
            };

            float x[3];
            float y[3];

            for (int i = 0; i < 3; ++i) {
                quantized_vertex_to_screen(
                    origin_x,
                    origin_y,
                    quantized_scale,
                    qx[i],
                    qy[i],
                    rotate,
                    bearing_cos,
                    bearing_sin,
                    viewport_cx,
                    viewport_cy,
                    &x[i],
                    &y[i]);
            }

            if (!draw_triangle(
                    renderer,
                    &batch,
                    x,
                    y,
                    color)) {
                flush_geometry(renderer, &batch);
                return;
            }

            ++renderer->debug.triangles_drawn;
        }
    }

    flush_geometry(renderer, &batch);
}

static void building_entry_destroy(V11BuildingCacheEntry *entry)
{
    if (!entry) return;
    free(entry->triangles);
    memset(entry, 0, sizeof(*entry));
}

static V11BuildingCacheEntry *building_find(
    OpenRideORMapPyramidRenderer *renderer,
    int x,
    int y)
{
    if (!renderer) return NULL;

    const size_t base =
        cache_set_base(
            V11_BUILDING_CACHE_CAPACITY,
            OPENRIDE_ORMAP_PYRAMID_BUILDING_ZOOM,
            x,
            y);

    for (size_t way = 0U;
         way < V11_CACHE_ASSOCIATIVITY;
         ++way) {
        V11BuildingCacheEntry *entry =
            &renderer->buildings[base + way];

        if (entry->occupied
            && entry->x == x
            && entry->y == y) {
            entry->last_used =
                renderer->frame_counter;
            return entry;
        }
    }

    return NULL;
}

static V11BuildingCacheEntry *building_victim(
    OpenRideORMapPyramidRenderer *renderer,
    int x,
    int y)
{
    if (!renderer) return NULL;

    const size_t base =
        cache_set_base(
            V11_BUILDING_CACHE_CAPACITY,
            OPENRIDE_ORMAP_PYRAMID_BUILDING_ZOOM,
            x,
            y);

    V11BuildingCacheEntry *victim = NULL;

    for (size_t way = 0U;
         way < V11_CACHE_ASSOCIATIVITY;
         ++way) {
        V11BuildingCacheEntry *entry =
            &renderer->buildings[base + way];

        if (!entry->occupied) return entry;

        if (!victim
            || entry->last_used < victim->last_used) {
            victim = entry;
        }
    }

    if (victim) building_entry_destroy(victim);
    return victim;
}

static uint16_t read_u16_le(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t read_u32_le(const unsigned char *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8U)
        | ((uint32_t)p[2] << 16U)
        | ((uint32_t)p[3] << 24U);
}

static double building_cross(
    V11BuildingPoint a,
    V11BuildingPoint b,
    V11BuildingPoint c)
{
    return (b.x - a.x) * (c.y - a.y)
        - (b.y - a.y) * (c.x - a.x);
}

static double building_signed_area(
    const V11BuildingPoint *points,
    uint32_t count)
{
    double area = 0.0;
    for (uint32_t i = 0U; i < count; ++i) {
        const V11BuildingPoint a = points[i];
        const V11BuildingPoint b = points[(i + 1U) % count];
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5;
}

static bool point_inside_building_triangle(
    V11BuildingPoint p,
    V11BuildingPoint a,
    V11BuildingPoint b,
    V11BuildingPoint c,
    double orientation)
{
    const double epsilon = 1e-9;
    return orientation * building_cross(a, b, p) > epsilon
        && orientation * building_cross(b, c, p) > epsilon
        && orientation * building_cross(c, a, p) > epsilon;
}

static bool building_triangle_push(
    V11BuildingTriangle **triangles,
    uint32_t *count,
    uint32_t *capacity,
    const uint16_t *coordinates,
    uint32_t a,
    uint32_t b,
    uint32_t c)
{
    if (*count >= *capacity) {
        uint32_t next = *capacity ? *capacity * 2U : 256U;
        if (next < *count + 1U) next = *count + 1U;

        V11BuildingTriangle *grown = realloc(
            *triangles,
            (size_t)next * sizeof(*grown));
        if (!grown) return false;

        *triangles = grown;
        *capacity = next;
    }

    (*triangles)[(*count)++] = (V11BuildingTriangle){
        .x1 = coordinates[a * 2U],
        .y1 = coordinates[a * 2U + 1U],
        .x2 = coordinates[b * 2U],
        .y2 = coordinates[b * 2U + 1U],
        .x3 = coordinates[c * 2U],
        .y3 = coordinates[c * 2U + 1U]
    };
    return true;
}

static bool triangulate_building(
    const uint16_t *coordinates,
    uint32_t point_count,
    V11BuildingTriangle **triangles,
    uint32_t *triangle_count,
    uint32_t *triangle_capacity)
{
    V11BuildingPoint *points =
        malloc((size_t)point_count * sizeof(*points));
    uint32_t *indices =
        malloc((size_t)point_count * sizeof(*indices));

    if (!points || !indices) {
        free(points);
        free(indices);
        return false;
    }

    for (uint32_t i = 0U; i < point_count; ++i) {
        points[i] = (V11BuildingPoint){
            .x = coordinates[i * 2U],
            .y = coordinates[i * 2U + 1U]
        };
        indices[i] = i;
    }

    const double area = building_signed_area(points, point_count);
    if (fabs(area) < 1e-6) {
        free(points);
        free(indices);
        return true;
    }

    const double orientation = area > 0.0 ? 1.0 : -1.0;
    uint32_t remaining = point_count;
    uint64_t guard = (uint64_t)point_count * point_count + 1U;

    while (remaining > 3U && guard-- > 0U) {
        bool clipped = false;

        for (uint32_t i = 0U; i < remaining; ++i) {
            const uint32_t prev =
                indices[(i + remaining - 1U) % remaining];
            const uint32_t curr = indices[i];
            const uint32_t next = indices[(i + 1U) % remaining];

            const V11BuildingPoint a = points[prev];
            const V11BuildingPoint b = points[curr];
            const V11BuildingPoint c = points[next];

            if (orientation * building_cross(a, b, c) <= 1e-6) {
                continue;
            }

            bool contains = false;
            for (uint32_t j = 0U; j < remaining; ++j) {
                const uint32_t candidate = indices[j];
                if (candidate == prev || candidate == curr || candidate == next) {
                    continue;
                }

                if (point_inside_building_triangle(
                        points[candidate], a, b, c, orientation)) {
                    contains = true;
                    break;
                }
            }
            if (contains) continue;

            if (!building_triangle_push(
                    triangles,
                    triangle_count,
                    triangle_capacity,
                    coordinates,
                    prev, curr, next)) {
                free(points);
                free(indices);
                return false;
            }

            memmove(
                indices + i,
                indices + i + 1U,
                (size_t)(remaining - i - 1U) * sizeof(*indices));
            --remaining;
            clipped = true;
            break;
        }

        if (!clipped) {
            free(points);
            free(indices);
            return true;
        }
    }

    if (remaining == 3U) {
        if (!building_triangle_push(
                triangles,
                triangle_count,
                triangle_capacity,
                coordinates,
                indices[0], indices[1], indices[2])) {
            free(points);
            free(indices);
            return false;
        }
    }

    free(points);
    free(indices);
    return true;
}

static bool decode_building_blob(
    const void *blob,
    int blob_size,
    V11BuildingTriangle **triangles_out,
    uint32_t *triangle_count_out)
{
    *triangles_out = NULL;
    *triangle_count_out = 0U;

    uLongf raw_capacity = (uLongf)blob_size * 5U + 64U;
    unsigned char *raw = NULL;
    int zrc = Z_BUF_ERROR;

    for (int attempt = 0; attempt < 12 && zrc == Z_BUF_ERROR; ++attempt) {
        unsigned char *grown = realloc(raw, (size_t)raw_capacity);
        if (!grown) {
            free(raw);
            return false;
        }
        raw = grown;

        uLongf decoded_size = raw_capacity;
        zrc = uncompress(
            raw,
            &decoded_size,
            (const Bytef *)blob,
            (uLong)blob_size);

        if (zrc == Z_OK) {
            raw_capacity = decoded_size;
            break;
        }
        if (raw_capacity > UINT32_MAX / 2U) break;
        raw_capacity *= 2U;
    }

    if (zrc != Z_OK
        || raw_capacity < 16U
        || memcmp(raw, "ORB1", 4U) != 0
        || read_u16_le(raw + 4U)
            != OPENRIDE_ORMAP_PYRAMID_BUILDING_BLOB_VERSION
        || read_u16_le(raw + 6U) != 0U) {
        free(raw);
        return false;
    }

    const uint32_t polygon_count = read_u32_le(raw + 8U);
    const uint32_t expected_vertices = read_u32_le(raw + 12U);

    size_t offset = 16U;
    uint64_t parsed_vertices = 0U;
    V11BuildingTriangle *triangles = NULL;
    uint32_t triangle_count = 0U;
    uint32_t triangle_capacity = 0U;

    for (uint32_t polygon = 0U; polygon < polygon_count; ++polygon) {
        if (offset + 4U > raw_capacity) {
            free(triangles);
            free(raw);
            return false;
        }

        const uint16_t count = read_u16_le(raw + offset);
        const uint16_t flags = read_u16_le(raw + offset + 2U);
        offset += 4U;

        if (count < 3U || flags != 0U
            || (uint64_t)count * 4U > raw_capacity - offset) {
            free(triangles);
            free(raw);
            return false;
        }

        uint16_t *coordinates =
            malloc((size_t)count * 2U * sizeof(*coordinates));
        if (!coordinates) {
            free(triangles);
            free(raw);
            return false;
        }

        for (uint32_t i = 0U; i < count; ++i) {
            coordinates[i * 2U] =
                read_u16_le(raw + offset + i * 4U);
            coordinates[i * 2U + 1U] =
                read_u16_le(raw + offset + i * 4U + 2U);
        }

        if (!triangulate_building(
                coordinates,
                count,
                &triangles,
                &triangle_count,
                &triangle_capacity)) {
            free(coordinates);
            free(triangles);
            free(raw);
            return false;
        }

        free(coordinates);
        offset += (size_t)count * 4U;
        parsed_vertices += count;
    }

    const bool valid =
        offset == raw_capacity && parsed_vertices == expected_vertices;
    free(raw);

    if (!valid) {
        free(triangles);
        return false;
    }

    *triangles_out = triangles;
    *triangle_count_out = triangle_count;
    return true;
}

static V11BuildingCacheEntry *building_load(
    OpenRideORMapPyramidRenderer *renderer,
    int x,
    int y)
{
    V11BuildingCacheEntry *existing = building_find(renderer, x, y);
    if (existing) return existing;

    if (renderer->building_load_budget == 0U) {
        ++renderer->debug.deferred_loads;
        renderer->needs_followup = true;
        return NULL;
    }

    V11BuildingCacheEntry *entry =
        building_victim(renderer, x, y);
    if (!entry) return NULL;

    const uint64_t started = SDL_GetTicksNS();

    sqlite3_reset(renderer->building_load_stmt);
    sqlite3_clear_bindings(renderer->building_load_stmt);
    sqlite3_bind_int(
        renderer->building_load_stmt, 1,
        OPENRIDE_ORMAP_PYRAMID_BUILDING_ZOOM);
    sqlite3_bind_int(renderer->building_load_stmt, 2, x);
    sqlite3_bind_int(renderer->building_load_stmt, 3, y);

    const int rc = sqlite3_step(renderer->building_load_stmt);

    bool empty = true;
    V11BuildingTriangle *triangles = NULL;
    uint32_t triangle_count = 0U;

    if (rc == SQLITE_ROW) {
        const void *blob =
            sqlite3_column_blob(renderer->building_load_stmt, 0);
        const int blob_size =
            sqlite3_column_bytes(renderer->building_load_stmt, 0);

        if (decode_building_blob(
                blob, blob_size,
                &triangles, &triangle_count)) {
            empty = triangle_count == 0U;
        } else {
            SDL_Log("OpenRide v11 invalid building tile z16/%d/%d", x, y);
        }
    } else if (rc != SQLITE_DONE) {
        SDL_Log(
            "OpenRide v11 building SQLite z16/%d/%d: %s",
            x, y, sqlite3_errmsg(renderer->building_db));
    }

    sqlite3_reset(renderer->building_load_stmt);

    renderer->debug.load_ms +=
        (double)(SDL_GetTicksNS() - started) / 1000000.0;
    --renderer->building_load_budget;
    ++renderer->debug.draw_loads;

    *entry = (V11BuildingCacheEntry){
        .occupied = true,
        .empty = empty,
        .x = x,
        .y = y,
        .last_used = renderer->frame_counter,
        .ready_since_ms = renderer->now_ms,
        .triangles = triangles,
        .triangle_count = triangle_count
    };

    renderer->needs_followup = true;
    return entry;
}

OpenRideORMapPyramidRenderer *
openride_ormap_pyramid_renderer_create(
    SDL_Renderer *sdl_renderer,
    const char *ormap11_path,
    char *error,
    size_t error_size)
{
    if (!sdl_renderer || !ormap11_path) {
        set_error(error, error_size, "invalid v11 renderer arguments");
        return NULL;
    }

    OpenRideORMapPyramidRenderer *renderer =
        calloc(1U, sizeof(*renderer));
    if (!renderer) {
        set_error(error, error_size, "out of memory creating v11 renderer");
        return NULL;
    }

    renderer->renderer = sdl_renderer;
    renderer->style = OPENRIDE_MAP_STYLE_TRAIL;

    /*
     * SDL3 render targets are common on accelerated backends, but custom blend
     * support is backend-dependent. Probe everything we need once; failure is
     * non-fatal because the vector V3.8.5 path remains available.
     */
    renderer->surface_texture_supported =
        surface_probe_texture_path(renderer);

    renderer->surface_map =
        openride_ormap_pyramid_surface_open(
            ormap11_path,
            error,
            error_size);
    if (!renderer->surface_map) {
        openride_ormap_pyramid_renderer_destroy(renderer);
        return NULL;
    }

    OpenRideORMapTilePyramidConfig config =
        openride_ormap_tile_pyramid_default_config();
    config.min_zoom = OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM;
    config.max_zoom = OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM;

    if (!openride_ormap_tile_pyramid_init(&renderer->pyramid, &config)) {
        set_error(error, error_size, "unable to initialize v11 tile pyramid");
        openride_ormap_pyramid_renderer_destroy(renderer);
        return NULL;
    }

    if (sqlite3_open_v2(
            ormap11_path,
            &renderer->building_db,
            SQLITE_OPEN_READONLY,
            NULL) == SQLITE_OK
        && sqlite3_prepare_v2(
            renderer->building_db,
            "SELECT tile_data FROM building_tiles "
            "WHERE zoom=?1 AND tile_column=?2 AND tile_row=?3",
            -1,
            &renderer->building_load_stmt,
            NULL) == SQLITE_OK) {
        renderer->building_available = true;
    } else {
        if (renderer->building_load_stmt) {
            sqlite3_finalize(renderer->building_load_stmt);
            renderer->building_load_stmt = NULL;
        }
        if (renderer->building_db) {
            sqlite3_close(renderer->building_db);
            renderer->building_db = NULL;
        }
    }

    SDL_Log(
        "AUDIT_ORMAP_V11_ACTIVE path=%s surfaces=z9..z14 buildings=%s hotpath=bounded-lru surface_gpu=%s",
        ormap11_path,
        renderer->building_available ? "z16" : "off",
        renderer->surface_texture_supported
            ? "premult-target-cache"
            : "vector-fallback");

    set_error(error, error_size, "");
    return renderer;
}

void openride_ormap_pyramid_renderer_destroy(
    OpenRideORMapPyramidRenderer *renderer)
{
    if (!renderer) return;

    surface_texture_cache_clear(renderer);

    if (renderer->surface_compositor) {
        SDL_DestroyTexture(renderer->surface_compositor);
        renderer->surface_compositor = NULL;
    }

    for (uint32_t i = 0U; i < V11_SURFACE_CACHE_CAPACITY; ++i) {
        surface_entry_destroy(&renderer->surfaces[i]);
    }
    for (uint32_t i = 0U; i < V11_BUILDING_CACHE_CAPACITY; ++i) {
        building_entry_destroy(&renderer->buildings[i]);
    }

    openride_ormap_pyramid_plan_destroy(&renderer->surface_plan);
    openride_ormap_tile_pyramid_destroy(&renderer->pyramid);

    if (renderer->surface_map) {
        openride_ormap_pyramid_surface_close(renderer->surface_map);
    }
    if (renderer->building_load_stmt) {
        sqlite3_finalize(renderer->building_load_stmt);
    }
    if (renderer->building_db) {
        sqlite3_close(renderer->building_db);
    }

    free(renderer->vertices);
    free(renderer->indices);
    free(renderer);
}

void openride_ormap_pyramid_renderer_set_style(
    OpenRideORMapPyramidRenderer *renderer,
    OpenRideMapStyle style)
{
    if (!renderer) return;

    if (renderer->style != style) {
        renderer->style = style;

        /*
         * Surface colors are baked into GPU tiles. Keep decompressed v11 data
         * and invalidate only the small GPU cache on style changes.
         */
        surface_texture_cache_clear(renderer);
        renderer->needs_followup = true;
    }
}

void openride_ormap_pyramid_renderer_begin_frame(
    OpenRideORMapPyramidRenderer *renderer)
{
    if (!renderer) return;

    ++renderer->frame_counter;
    renderer->now_ms = SDL_GetTicks();
    renderer->surface_load_budget = V11_SURFACE_LOAD_BUDGET;
    renderer->surface_texture_compile_budget =
        V11_SURFACE_TEXTURE_COMPILE_BUDGET;
    renderer->building_load_budget = V11_BUILDING_LOAD_BUDGET;
    renderer->needs_followup = false;
    memset(&renderer->debug, 0, sizeof(renderer->debug));
}

void openride_ormap_pyramid_renderer_draw_surfaces(
    OpenRideORMapPyramidRenderer *renderer,
    const OpenRideMapCamera *camera,
    int viewport_width,
    int viewport_height)
{
    if (!renderer || !camera
        || viewport_width <= 0 || viewport_height <= 0
        || camera->zoom < V11_SURFACE_START_ZOOM) {
        return;
    }

    const uint64_t started = SDL_GetTicksNS();

    compute_visible_ranges(
        renderer,
        camera,
        viewport_width,
        viewport_height);

    surface_pin_previous_plan(renderer);

    if (renderer->surface_texture_supported) {
        surface_prepare_visible_textures(
            renderer,
            camera);
    }

    OpenRideORMapPyramidTileKey *roots = NULL;
    uint32_t root_count = 0U;

    if (!build_surface_roots(
            renderer,
            &roots,
            &root_count)) {
        renderer->needs_followup = true;
        renderer->debug.areas_ms +=
            (double)(SDL_GetTicksNS() - started)
            / 1000000.0;
        return;
    }

    if (root_count > 0U) {
        const bool planned =
            openride_ormap_tile_pyramid_plan(
                &renderer->pyramid,
                camera->zoom,
                roots,
                root_count,
                renderer->now_ms,
                surface_state,
                surface_request,
                renderer,
                &renderer->surface_plan);

        if (!planned) {
            renderer->needs_followup = true;
        } else {
            renderer->debug.surface_plan_tiles +=
                renderer->surface_plan.count;
            renderer->debug.surface_plan_requests +=
                renderer->surface_plan.requests_issued;
            renderer->debug.surface_plan_pending +=
                renderer->surface_plan.pending_tiles;
            renderer->debug.surface_plan_blending +=
                renderer->surface_plan.blending_families;
            renderer->debug.surface_cache_entries +=
                surface_cache_occupancy(renderer);
            renderer->debug.surface_texture_entries +=
                surface_texture_cache_occupancy(renderer);

            for (uint32_t i = 0U;
                 i < renderer->surface_plan.count;
                 ++i) {
                renderer->debug.surface_plan_alpha +=
                    renderer->surface_plan.tiles[i].alpha;
            }

            if (root_count > 0U
                && renderer->surface_plan.count == 0U) {
                ++renderer->debug.surface_empty_plans;
            }

            if (renderer->surface_plan.needs_followup_frame
                || renderer->surface_plan.pending_tiles > 0U) {
                renderer->needs_followup = true;
            }

            const double alpha =
                smoothstep(
                    camera->zoom,
                    V11_SURFACE_START_ZOOM,
                    V11_SURFACE_FULL_ZOOM);

            /*
             * V3.8.11: blend the two surface backends over a short camera-zoom
             * interval. TilePyramid ownership, readiness and DATA LOD remain
             * unchanged. The GPU and vector images are each composited first,
             * then combined as premultiplied images before one final source-over
             * presentation. This prevents the binary sharpness snap without
             * reintroducing a region-wide geometry LOD transition.
             */
            const double surface_backend_mix =
                surface_backend_blend_mix(camera->zoom);

            if (surface_backend_mix >= 1.0) {
                    SDL_BlendMode previous =
                        SDL_BLENDMODE_NONE;
                    const bool have_previous =
                        SDL_GetRenderDrawBlendMode(
                            renderer->renderer,
                            &previous);

                    SDL_SetRenderDrawBlendMode(
                        renderer->renderer,
                        SDL_BLENDMODE_BLEND);

                    draw_surface_plan(
                        renderer,
                        camera,
                        viewport_width,
                        viewport_height,
                        alpha);

                    SDL_SetRenderDrawBlendMode(
                        renderer->renderer,
                        have_previous
                            ? previous
                            : SDL_BLENDMODE_NONE);
            } else if (renderer->surface_texture_supported) {
                if (surface_backend_mix <= 0.0) {
                        /*
                         * The planner only marks textured tiles READY, so this draw is
                         * hole-free. A runtime target/blend failure disables the GPU
                         * path and the next frame falls back to the exact V3.8.5
                         * vector renderer.
                         */
                        if (!draw_surface_plan_textured(
                                renderer,
                                camera,
                                viewport_width,
                                viewport_height,
                                alpha)) {
                            ++renderer->debug.surface_draw_failures;
                            surface_disable_texture_path(
                                renderer);
                            renderer->needs_followup = true;
                        }
                } else {
                    surface_backend_capture_begin(
                        renderer, 1.0 - surface_backend_mix);
                        /*
                         * The planner only marks textured tiles READY, so this draw is
                         * hole-free. A runtime target/blend failure disables the GPU
                         * path and the next frame falls back to the exact V3.8.5
                         * vector renderer.
                         */
                        if (!draw_surface_plan_textured(
                                renderer,
                                camera,
                                viewport_width,
                                viewport_height,
                                alpha)) {
                            ++renderer->debug.surface_draw_failures;
                            surface_disable_texture_path(
                                renderer);
                            renderer->needs_followup = true;
                        }
                    const bool have_gpu_capture =
                        surface_backend_capture_end(renderer);

                    V11SurfaceBackendBlendState backend_state;
                    if (have_gpu_capture
                        && surface_backend_vector_start(
                            renderer, &backend_state)) {
                            SDL_BlendMode previous =
                                SDL_BLENDMODE_NONE;
                            const bool have_previous =
                                SDL_GetRenderDrawBlendMode(
                                    renderer->renderer,
                                    &previous);

                            SDL_SetRenderDrawBlendMode(
                                renderer->renderer,
                                SDL_BLENDMODE_BLEND);

                            draw_surface_plan(
                                renderer,
                                camera,
                                viewport_width,
                                viewport_height,
                                alpha);

                            SDL_SetRenderDrawBlendMode(
                                renderer->renderer,
                                have_previous
                                    ? previous
                                    : SDL_BLENDMODE_NONE);
                        if (!surface_backend_blend_finish(
                                renderer,
                                surface_backend_mix,
                                &backend_state)) {
                            SDL_BlendMode previous =
                                SDL_BLENDMODE_NONE;
                            const bool have_previous =
                                SDL_GetRenderDrawBlendMode(
                                    renderer->renderer,
                                    &previous);

                            SDL_SetRenderDrawBlendMode(
                                renderer->renderer,
                                SDL_BLENDMODE_BLEND);

                            draw_surface_plan(
                                renderer,
                                camera,
                                viewport_width,
                                viewport_height,
                                alpha);

                            SDL_SetRenderDrawBlendMode(
                                renderer->renderer,
                                have_previous
                                    ? previous
                                    : SDL_BLENDMODE_NONE);
                        }
                    } else {
                        SDL_BlendMode previous =
                            SDL_BLENDMODE_NONE;
                        const bool have_previous =
                            SDL_GetRenderDrawBlendMode(
                                renderer->renderer,
                                &previous);

                        SDL_SetRenderDrawBlendMode(
                            renderer->renderer,
                            SDL_BLENDMODE_BLEND);

                        draw_surface_plan(
                            renderer,
                            camera,
                            viewport_width,
                            viewport_height,
                            alpha);

                        SDL_SetRenderDrawBlendMode(
                            renderer->renderer,
                            have_previous
                                ? previous
                                : SDL_BLENDMODE_NONE);
                    }
                }
            } else {
                    SDL_BlendMode previous =
                        SDL_BLENDMODE_NONE;
                    const bool have_previous =
                        SDL_GetRenderDrawBlendMode(
                            renderer->renderer,
                            &previous);

                    SDL_SetRenderDrawBlendMode(
                        renderer->renderer,
                        SDL_BLENDMODE_BLEND);

                    draw_surface_plan(
                        renderer,
                        camera,
                        viewport_width,
                        viewport_height,
                        alpha);

                    SDL_SetRenderDrawBlendMode(
                        renderer->renderer,
                        have_previous
                            ? previous
                            : SDL_BLENDMODE_NONE);
            }
        }
    }

    free(roots);

    renderer->debug.areas_ms +=
        (double)(SDL_GetTicksNS() - started)
        / 1000000.0;
}


void openride_ormap_pyramid_renderer_draw_buildings(
    OpenRideORMapPyramidRenderer *renderer,
    const OpenRideMapCamera *camera,
    int viewport_width,
    int viewport_height)
{
    if (!renderer || !renderer->building_available || !camera
        || viewport_width <= 0 || viewport_height <= 0
        || camera->zoom < V11_BUILDING_START_ZOOM) {
        return;
    }

    const uint64_t started = SDL_GetTicksNS();

    compute_visible_ranges(renderer, camera, viewport_width, viewport_height);
    const int index = visible_index(OPENRIDE_ORMAP_PYRAMID_BUILDING_ZOOM);
    if (index < 0 || !renderer->visible[index].valid) return;

    const V11VisibleRange range = renderer->visible[index];

    const OpenRidePointD center =
        openride_mercator_forward(
            camera->center_lat,
            camera->center_lon);
    const double world_size =
        openride_world_size_pixels(camera->zoom);
    const double center_x =
        center.x * world_size;
    const double center_y =
        center.y * world_size;

    const double viewport_cx =
        viewport_width * 0.5;
    const double viewport_cy =
        viewport_height * 0.5;

    const bool rotate =
        fabs(camera->bearing_deg) >= 1e-12;
    const double angle =
        camera->bearing_deg
        * 3.14159265358979323846 / 180.0;
    const double bearing_cos =
        rotate ? cos(angle) : 1.0;
    const double bearing_sin =
        rotate ? sin(angle) : 0.0;

    const double tile_size =
        world_size
        / (double)(
            1 << OPENRIDE_ORMAP_PYRAMID_BUILDING_ZOOM);
    const double buffer =
        OPENRIDE_ORMAP_PYRAMID_BUILDING_BUFFER_FRACTION;
    const double quantized_scale =
        tile_size
        * (1.0 + 2.0 * buffer)
        / 65535.0;

    SDL_BlendMode previous = SDL_BLENDMODE_NONE;
    const bool have_previous =
        SDL_GetRenderDrawBlendMode(renderer->renderer, &previous);
    SDL_SetRenderDrawBlendMode(renderer->renderer, SDL_BLENDMODE_BLEND);

    V11GeometryBatch batch = {0};

    for (int y_tile = range.first_y; y_tile <= range.last_y; ++y_tile) {
        for (int x_tile = range.first_x; x_tile <= range.last_x; ++x_tile) {
            V11BuildingCacheEntry *entry =
                building_find(renderer, x_tile, y_tile);

            if (!entry) {
                entry = building_load(renderer, x_tile, y_tile);
                if (!entry) {
                    renderer->needs_followup = true;
                    continue;
                }
            }

            if (entry->empty || entry->triangle_count == 0U) continue;

            const uint64_t elapsed =
                renderer->now_ms >= entry->ready_since_ms
                    ? renderer->now_ms - entry->ready_since_ms
                    : 0U;
            const double ready_alpha = clamp01(
                (double)elapsed / (double)V11_BUILDING_READY_RAMP_MS);

            if (ready_alpha < 0.999) {
                renderer->needs_followup = true;
            }

            const OpenRideMapColor color =
                building_color(renderer->style, camera->zoom, ready_alpha);
            if (color.a == 0U) continue;

            ++renderer->debug.tiles_visited;

            const double left =
                viewport_cx
                + (double)entry->x * tile_size
                - center_x;
            const double top =
                viewport_cy
                + (double)entry->y * tile_size
                - center_y;
            const double origin_x =
                left - buffer * tile_size;
            const double origin_y =
                top - buffer * tile_size;

            for (uint32_t t = 0U; t < entry->triangle_count; ++t) {
                const V11BuildingTriangle *triangle = &entry->triangles[t];
                const uint16_t qx[3] = {
                    triangle->x1, triangle->x2, triangle->x3
                };
                const uint16_t qy[3] = {
                    triangle->y1, triangle->y2, triangle->y3
                };
                float x[3], y[3];

                for (int i = 0; i < 3; ++i) {
                    quantized_vertex_to_screen(
                        origin_x,
                        origin_y,
                        quantized_scale,
                        qx[i],
                        qy[i],
                        rotate,
                        bearing_cos,
                        bearing_sin,
                        viewport_cx,
                        viewport_cy,
                        &x[i],
                        &y[i]);
                }

                if (!draw_triangle(renderer, &batch, x, y, color)) {
                    flush_geometry(renderer, &batch);
                    goto buildings_done;
                }
                ++renderer->debug.triangles_drawn;
            }
        }
    }

buildings_done:
    flush_geometry(renderer, &batch);
    SDL_SetRenderDrawBlendMode(
        renderer->renderer,
        have_previous ? previous : SDL_BLENDMODE_NONE);

    renderer->debug.areas_ms +=
        (double)(SDL_GetTicksNS() - started) / 1000000.0;
}

bool openride_ormap_pyramid_renderer_needs_followup_frame(
    const OpenRideORMapPyramidRenderer *renderer)
{
    return renderer && renderer->needs_followup;
}

void openride_ormap_pyramid_renderer_get_area_debug_stats(
    const OpenRideORMapPyramidRenderer *renderer,
    OpenRideORMapAreaDebugStats *stats)
{
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
    if (renderer) *stats = renderer->debug;
}
