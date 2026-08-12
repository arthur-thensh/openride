#include "map/ormap_renderer.h"

#include "openride/place_search.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define ORMAP_TILE_SIZE 256.0
#define ORMAP_LABEL_BOX_MAX 64
#define ORMAP_CACHE_ASSOCIATIVITY 4U
#define ORMAP_GEOMETRY_BATCH_VERTEX_LIMIT 16384U
#define ORMAP_GEOMETRY_BATCH_INDEX_LIMIT 24576U

typedef struct GeometryBatch {
    uint32_t vertex_count;
    uint32_t index_count;
} GeometryBatch;

typedef struct LabelBox {
    float left;
    float top;
    float right;
    float bottom;
} LabelBox;

static int wrap_x(int x, int count)
{
    int wrapped = x % count;
    if (wrapped < 0) wrapped += count;
    return wrapped;
}

static void rotate_point(const OpenRideMapCamera *camera,
                         int width,
                         int height,
                         float *x,
                         float *y)
{
    if (!camera || !x || !y || fabs(camera->bearing_deg) < 1e-12) return;
    const double angle = camera->bearing_deg * 3.14159265358979323846 / 180.0;
    const double c = cos(angle);
    const double s = sin(angle);
    const double cx = width * 0.5;
    const double cy = height * 0.5;
    const double dx = *x - cx;
    const double dy = *y - cy;
    *x = (float)(cx + dx * c + dy * s);
    *y = (float)(cy - dx * s + dy * c);
}

static void set_color(SDL_Renderer *renderer, OpenRideMapColor color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

static bool ensure_geometry_scratch(OpenRideORMapRenderer *renderer,
                                    uint32_t vertex_count,
                                    uint32_t index_count)
{
    if (!renderer) return false;

    if (vertex_count > renderer->area_vertex_capacity) {
        uint32_t capacity = renderer->area_vertex_capacity == 0U
            ? 4096U : renderer->area_vertex_capacity;
        while (capacity < vertex_count) {
            if (capacity > UINT32_MAX / 2U) {
                capacity = vertex_count;
                break;
            }
            capacity *= 2U;
        }
        SDL_Vertex *vertices = realloc(renderer->area_vertices,
                                       (size_t)capacity * sizeof(*vertices));
        if (!vertices) return false;
        renderer->area_vertices = vertices;
        renderer->area_vertex_capacity = capacity;
    }

    if (index_count > renderer->area_index_capacity) {
        uint32_t capacity = renderer->area_index_capacity == 0U
            ? 6144U : renderer->area_index_capacity;
        while (capacity < index_count) {
            if (capacity > UINT32_MAX / 2U) {
                capacity = index_count;
                break;
            }
            capacity *= 2U;
        }
        int *indices = realloc(renderer->area_indices,
                               (size_t)capacity * sizeof(*indices));
        if (!indices) return false;
        renderer->area_indices = indices;
        renderer->area_index_capacity = capacity;
    }
    return true;
}

static void geometry_batch_flush(OpenRideORMapRenderer *renderer,
                                 GeometryBatch *batch)
{
    if (!renderer || !batch || batch->vertex_count == 0U || batch->index_count == 0U) {
        if (batch) {
            batch->vertex_count = 0U;
            batch->index_count = 0U;
        }
        return;
    }
    SDL_RenderGeometry(renderer->renderer,
                       NULL,
                       renderer->area_vertices,
                       (int)batch->vertex_count,
                       renderer->area_indices,
                       (int)batch->index_count);
    batch->vertex_count = 0U;
    batch->index_count = 0U;
}

static bool geometry_batch_reserve(OpenRideORMapRenderer *renderer,
                                   GeometryBatch *batch,
                                   uint32_t add_vertices,
                                   uint32_t add_indices)
{
    if (!renderer || !batch) return false;
    if (add_vertices > ORMAP_GEOMETRY_BATCH_VERTEX_LIMIT
        || add_indices > ORMAP_GEOMETRY_BATCH_INDEX_LIMIT) {
        return false;
    }
    if (batch->vertex_count > ORMAP_GEOMETRY_BATCH_VERTEX_LIMIT - add_vertices
        || batch->index_count > ORMAP_GEOMETRY_BATCH_INDEX_LIMIT - add_indices) {
        geometry_batch_flush(renderer, batch);
    }
    return ensure_geometry_scratch(renderer,
                                   batch->vertex_count + add_vertices,
                                   batch->index_count + add_indices);
}

static void set_geometry_vertex(SDL_Vertex *vertex,
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

static bool geometry_batch_quad(OpenRideORMapRenderer *renderer,
                                GeometryBatch *batch,
                                const float x[4],
                                const float y[4],
                                OpenRideMapColor color)
{
    if (!geometry_batch_reserve(renderer, batch, 4U, 6U)) return false;
    const uint32_t base_vertex = batch->vertex_count;
    const uint32_t base_index = batch->index_count;
    for (uint32_t i = 0U; i < 4U; ++i) {
        set_geometry_vertex(&renderer->area_vertices[base_vertex + i], x[i], y[i], color);
    }
    const int base = (int)base_vertex;
    renderer->area_indices[base_index + 0U] = base + 0;
    renderer->area_indices[base_index + 1U] = base + 1;
    renderer->area_indices[base_index + 2U] = base + 2;
    renderer->area_indices[base_index + 3U] = base + 0;
    renderer->area_indices[base_index + 4U] = base + 2;
    renderer->area_indices[base_index + 5U] = base + 3;
    batch->vertex_count += 4U;
    batch->index_count += 6U;
    return true;
}

static bool geometry_batch_triangle(OpenRideORMapRenderer *renderer,
                                    GeometryBatch *batch,
                                    const float x[3],
                                    const float y[3],
                                    OpenRideMapColor color)
{
    if (!geometry_batch_reserve(renderer, batch, 3U, 3U)) return false;
    const uint32_t base_vertex = batch->vertex_count;
    const uint32_t base_index = batch->index_count;
    for (uint32_t i = 0U; i < 3U; ++i) {
        set_geometry_vertex(&renderer->area_vertices[base_vertex + i], x[i], y[i], color);
        renderer->area_indices[base_index + i] = (int)(base_vertex + i);
    }
    batch->vertex_count += 3U;
    batch->index_count += 3U;
    return true;
}

static bool geometry_batch_line(OpenRideORMapRenderer *renderer,
                                GeometryBatch *batch,
                                float x1,
                                float y1,
                                float x2,
                                float y2,
                                int width,
                                OpenRideMapColor color)
{
    if (width <= 0) return true;
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.001f) return true;
    const float half = 0.5f * (float)width;
    const float nx = -dy / length * half;
    const float ny = dx / length * half;
    const float x[4] = {x1 + nx, x2 + nx, x2 - nx, x1 - nx};
    const float y[4] = {y1 + ny, y2 + ny, y2 - ny, y1 - ny};
    return geometry_batch_quad(renderer, batch, x, y, color);
}

static bool geometry_batch_dashed_line(OpenRideORMapRenderer *renderer,
                                       GeometryBatch *batch,
                                       float x1,
                                       float y1,
                                       float x2,
                                       float y2,
                                       int width,
                                       OpenRideMapColor color)
{
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.001f) return true;
    const float dash = 8.0f;
    const float gap = 5.0f;
    for (float position = 0.0f; position < length; position += dash + gap) {
        const float end = fminf(position + dash, length);
        const float t1 = position / length;
        const float t2 = end / length;
        if (!geometry_batch_line(renderer,
                                 batch,
                                 x1 + dx * t1,
                                 y1 + dy * t1,
                                 x1 + dx * t2,
                                 y1 + dy * t2,
                                 width,
                                 color)) {
            return false;
        }
    }
    return true;
}

static uint32_t tile_cache_hash(int zoom, int x, int y)
{
    uint32_t value = (uint32_t)zoom * UINT32_C(0x9e3779b9);
    value ^= (uint32_t)x * UINT32_C(0x85ebca6b);
    value ^= (uint32_t)y * UINT32_C(0xc2b2ae35);
    value ^= value >> 16U;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15U;
    return value;
}

static size_t tile_cache_set_base(size_t capacity, int zoom, int x, int y)
{
    const size_t set_count = capacity / ORMAP_CACHE_ASSOCIATIVITY;
    return (size_t)(tile_cache_hash(zoom, x, y) % (uint32_t)set_count)
        * ORMAP_CACHE_ASSOCIATIVITY;
}

static const char *road_kind(uint8_t road_class)
{
    switch ((OpenRideRoadClass)road_class) {
        case OPENRIDE_ROAD_MOTORWAY: return "motorway";
        case OPENRIDE_ROAD_TRUNK: return "trunk";
        case OPENRIDE_ROAD_PRIMARY: return "primary";
        case OPENRIDE_ROAD_SECONDARY: return "secondary";
        case OPENRIDE_ROAD_TERTIARY: return "tertiary";
        case OPENRIDE_ROAD_UNCLASSIFIED: return "unclassified";
        case OPENRIDE_ROAD_RESIDENTIAL: return "residential";
        case OPENRIDE_ROAD_SERVICE: return "service";
        case OPENRIDE_ROAD_LIVING_STREET: return "living_street";
        case OPENRIDE_ROAD_TRACK: return "track";
        case OPENRIDE_ROAD_PATH: return "path";
        default: return "other";
    }
}

static OpenRideORMapRoadCacheEntry *road_cache_slot(OpenRideORMapRenderer *renderer,
                                                     int zoom,
                                                     int x,
                                                     int y)
{
    const size_t base = tile_cache_set_base(OPENRIDE_ORMAP_ROAD_CACHE_CAPACITY,
                                            zoom, x, y);
    OpenRideORMapRoadCacheEntry *victim = NULL;
    OpenRideORMapRoadCacheEntry *oldest = &renderer->roads[base];
    for (size_t i = 0U; i < ORMAP_CACHE_ASSOCIATIVITY; ++i) {
        OpenRideORMapRoadCacheEntry *entry = &renderer->roads[base + i];
        if (entry->occupied && entry->zoom == zoom && entry->x == x && entry->y == y) {
            entry->last_used = renderer->frame_counter;
            return entry;
        }
        if (!entry->occupied && !victim) victim = entry;
        if (entry->occupied && oldest->occupied && entry->last_used < oldest->last_used) {
            oldest = entry;
        }
    }
    if (!victim) victim = oldest;
    if (victim->occupied) openride_ormap_road_tile_destroy(&victim->tile);
    memset(victim, 0, sizeof(*victim));
    victim->occupied = true;
    victim->zoom = zoom;
    victim->x = x;
    victim->y = y;
    victim->last_used = renderer->frame_counter;
    char error[160] = {0};
    if (!openride_ormap_load_road_tile(renderer->map,
                                       zoom,
                                       x,
                                       y,
                                       &victim->tile,
                                       error,
                                       sizeof(error))) {
        /* Keep an occupied empty entry to cache absent tiles too. */
    }
    return victim;
}

static OpenRideORMapMaskCacheEntry *mask_cache_slot(OpenRideORMapRenderer *renderer,
                                                     int zoom,
                                                     int x,
                                                     int y)
{
    const size_t base = tile_cache_set_base(OPENRIDE_ORMAP_MASK_CACHE_CAPACITY,
                                            zoom, x, y);
    OpenRideORMapMaskCacheEntry *victim = NULL;
    OpenRideORMapMaskCacheEntry *oldest = &renderer->masks[base];
    for (size_t i = 0U; i < ORMAP_CACHE_ASSOCIATIVITY; ++i) {
        OpenRideORMapMaskCacheEntry *entry = &renderer->masks[base + i];
        if (entry->occupied && entry->zoom == zoom && entry->x == x && entry->y == y) {
            entry->last_used = renderer->frame_counter;
            return entry;
        }
        if (!entry->occupied && !victim) victim = entry;
        if (entry->occupied && oldest->occupied && entry->last_used < oldest->last_used) {
            oldest = entry;
        }
    }
    if (!victim) victim = oldest;
    if (victim->occupied) openride_ormap_mask_tile_destroy(&victim->tile);
    memset(victim, 0, sizeof(*victim));
    victim->occupied = true;
    victim->zoom = zoom;
    victim->x = x;
    victim->y = y;
    victim->last_used = renderer->frame_counter;
    char error[160] = {0};
    if (!openride_ormap_load_mask_tile(renderer->map,
                                       zoom,
                                       x,
                                       y,
                                       &victim->tile,
                                       error,
                                       sizeof(error))) {
        /* Missing mask tile is expected in rural areas. */
    }
    return victim;
}

static OpenRideORMapWaterCacheEntry *water_cache_slot(OpenRideORMapRenderer *renderer,
                                                       int zoom,
                                                       int x,
                                                       int y)
{
    const size_t base = tile_cache_set_base(OPENRIDE_ORMAP_WATER_CACHE_CAPACITY,
                                            zoom, x, y);
    OpenRideORMapWaterCacheEntry *victim = NULL;
    OpenRideORMapWaterCacheEntry *oldest = &renderer->waters[base];
    for (size_t i = 0U; i < ORMAP_CACHE_ASSOCIATIVITY; ++i) {
        OpenRideORMapWaterCacheEntry *entry = &renderer->waters[base + i];
        if (entry->occupied && entry->zoom == zoom && entry->x == x && entry->y == y) {
            entry->last_used = renderer->frame_counter;
            return entry;
        }
        if (!entry->occupied && !victim) victim = entry;
        if (entry->occupied && oldest->occupied && entry->last_used < oldest->last_used) {
            oldest = entry;
        }
    }
    if (!victim) victim = oldest;
    if (victim->occupied) openride_ormap_water_tile_destroy(&victim->tile);
    memset(victim, 0, sizeof(*victim));
    victim->occupied = true;
    victim->zoom = zoom;
    victim->x = x;
    victim->y = y;
    victim->last_used = renderer->frame_counter;
    char error[160] = {0};
    if (!openride_ormap_load_water_tile(renderer->map,
                                        zoom,
                                        x,
                                        y,
                                        &victim->tile,
                                        error,
                                        sizeof(error))) {
        /* v1 maps and tiles without waterways intentionally stay empty. */
    }
    return victim;
}

static OpenRideORMapAreaCacheEntry *area_cache_slot(OpenRideORMapRenderer *renderer,
                                                     int zoom,
                                                     int x,
                                                     int y)
{
    const size_t base = tile_cache_set_base(OPENRIDE_ORMAP_AREA_CACHE_CAPACITY,
                                            zoom, x, y);
    OpenRideORMapAreaCacheEntry *victim = NULL;
    OpenRideORMapAreaCacheEntry *oldest = &renderer->areas[base];
    for (size_t i = 0U; i < ORMAP_CACHE_ASSOCIATIVITY; ++i) {
        OpenRideORMapAreaCacheEntry *entry = &renderer->areas[base + i];
        if (entry->occupied && entry->zoom == zoom && entry->x == x && entry->y == y) {
            entry->last_used = renderer->frame_counter;
            return entry;
        }
        if (!entry->occupied && !victim) victim = entry;
        if (entry->occupied && oldest->occupied && entry->last_used < oldest->last_used) {
            oldest = entry;
        }
    }
    if (!victim) victim = oldest;
    if (victim->occupied) openride_ormap_area_tile_destroy(&victim->tile);
    memset(victim, 0, sizeof(*victim));
    victim->occupied = true;
    victim->zoom = zoom;
    victim->x = x;
    victim->y = y;
    victim->last_used = renderer->frame_counter;
    char error[160] = {0};
    if (!openride_ormap_load_area_tile(renderer->map,
                                       zoom,
                                       x,
                                       y,
                                       &victim->tile,
                                       error,
                                       sizeof(error))) {
        /* Missing vector area tiles are common over rural/empty regions. */
    }
    return victim;
}

static bool geometry_batch_rotated_rect(OpenRideORMapRenderer *renderer,
                                        GeometryBatch *batch,
                                        const OpenRideMapCamera *camera,
                                        int width,
                                        int height,
                                        float left,
                                        float top,
                                        float right,
                                        float bottom,
                                        OpenRideMapColor color)
{
    float x[4] = {left, right, right, left};
    float y[4] = {top, top, bottom, bottom};
    for (int i = 0; i < 4; ++i) rotate_point(camera, width, height, &x[i], &y[i]);
    return geometry_batch_quad(renderer, batch, x, y, color);
}

static bool mask_bit(const unsigned char *bits, uint32_t index)
{
    return bits && (bits[index >> 3U] & (unsigned char)(1U << (index & 7U))) != 0U;
}

static OpenRideMapColor forest_color(OpenRideMapStyle style)
{
    if (style == OPENRIDE_MAP_STYLE_TOPO) return (OpenRideMapColor){180, 203, 170, 210};
    if (style == OPENRIDE_MAP_STYLE_TRAIL) return (OpenRideMapColor){194, 210, 184, 170};
    return (OpenRideMapColor){207, 216, 201, 145};
}

static bool draw_mask_layer(OpenRideORMapRenderer *renderer,
                            GeometryBatch *batch,
                            const OpenRideMapCamera *camera,
                            int width,
                            int height,
                            double tile_size,
                            double tile_left,
                            double tile_top,
                            const OpenRideORMapMaskTile *tile,
                            const unsigned char *bits,
                            OpenRideMapColor color)
{
    if (!tile || !bits || tile->grid_size == 0U) return true;
    const uint32_t grid = tile->grid_size;
    const double cell = tile_size / (double)grid;
    for (uint32_t y = 0U; y < grid; ++y) {
        uint32_t x = 0U;
        while (x < grid) {
            while (x < grid && !mask_bit(bits, y * grid + x)) ++x;
            if (x >= grid) break;
            const uint32_t start = x;
            while (x < grid && mask_bit(bits, y * grid + x)) ++x;
            if (!geometry_batch_rotated_rect(renderer,
                                             batch,
                                             camera,
                                             width,
                                             height,
                                             (float)(tile_left + start * cell),
                                             (float)(tile_top + y * cell),
                                             (float)(tile_left + x * cell + 0.5),
                                             (float)(tile_top + (y + 1U) * cell + 0.5),
                                             color)) {
                return false;
            }
        }
    }
    return true;
}

static void draw_masks(OpenRideORMapRenderer *renderer,
                       const OpenRideMapCamera *camera,
                       int width,
                       int height)
{
    if (camera->zoom < 14.0 || camera->zoom > 17.2) return;
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    const int zoom = metadata ? metadata->mask_zoom : OPENRIDE_ORMAP_MASK_ZOOM;
    const int count = 1 << zoom;
    const double scale = pow(2.0, camera->zoom - zoom);
    const double tile_size = ORMAP_TILE_SIZE * scale;
    const OpenRidePointD center = openride_mercator_forward(camera->center_lat,
                                                             camera->center_lon);
    const double world_size = tile_size * count;
    const double center_x = center.x * world_size;
    const double center_y = center.y * world_size;
    const double bearing = camera->bearing_deg * 3.14159265358979323846 / 180.0;
    const double half_w = fabs(cos(bearing)) * width * 0.5 + fabs(sin(bearing)) * height * 0.5;
    const double half_h = fabs(sin(bearing)) * width * 0.5 + fabs(cos(bearing)) * height * 0.5;
    const int first_x = (int)floor((center_x - half_w) / tile_size);
    const int last_x = (int)floor((center_x + half_w) / tile_size);
    const int first_y = (int)floor((center_y - half_h) / tile_size);
    const int last_y = (int)floor((center_y + half_h) / tile_size);
    const OpenRideMapPalette palette = openride_map_palette(renderer->style);
    OpenRideMapColor builtup = palette.building;
    builtup.a = renderer->style == OPENRIDE_MAP_STYLE_TRAIL ? 92 : 125;
    OpenRideMapColor water = palette.water;
    water.a = 210;
    const OpenRideMapColor forest = forest_color(renderer->style);
    GeometryBatch batch = {0};

    for (int ty = first_y; ty <= last_y; ++ty) {
        if (ty < 0 || ty >= count) continue;
        for (int tx = first_x; tx <= last_x; ++tx) {
            const int qx = wrap_x(tx, count);
            OpenRideORMapMaskCacheEntry *entry = mask_cache_slot(renderer, zoom, qx, ty);
            if (!entry || !entry->tile.builtup) continue;
            const double left = width * 0.5 + tx * tile_size - center_x;
            const double top = height * 0.5 + ty * tile_size - center_y;
            if (!draw_mask_layer(renderer, &batch, camera, width, height,
                                 tile_size, left, top, &entry->tile,
                                 entry->tile.forest, forest)) {
                geometry_batch_flush(renderer, &batch);
                return;
            }
            /* v1/v2 stored filled water/built-up areas as semantic cells.
             * v3 keeps these layers vector-only and retains masks for legacy
             * compatibility plus the still-coarse forest background. */
            if (!metadata || metadata->format_version < 3) {
                if (!draw_mask_layer(renderer, &batch, camera, width, height,
                                     tile_size, left, top, &entry->tile,
                                     entry->tile.builtup, builtup)
                    || !draw_mask_layer(renderer, &batch, camera, width, height,
                                        tile_size, left, top, &entry->tile,
                                        entry->tile.water, water)) {
                    geometry_batch_flush(renderer, &batch);
                    return;
                }
            }
        }
    }
    geometry_batch_flush(renderer, &batch);
}

static double decode_area_coord(uint16_t value)
{
    const double buffer = OPENRIDE_ORMAP_AREA_BUFFER_FRACTION;
    return ((double)value / 65535.0) * (1.0 + 2.0 * buffer) - buffer;
}

static OpenRideMapColor area_color(OpenRideORMapRenderer *renderer, uint8_t kind)
{
    const OpenRideMapPalette palette = openride_map_palette(renderer->style);
    if (kind == OPENRIDE_ORMAP_AREA_WATER) {
        OpenRideMapColor water = palette.water;
        water.a = 210;
        return water;
    }
    OpenRideMapColor builtup = palette.building;
    builtup.a = renderer->style == OPENRIDE_MAP_STYLE_TRAIL ? 92 : 125;
    return builtup;
}

static void draw_area_level(OpenRideORMapRenderer *renderer,
                            const OpenRideMapCamera *camera,
                            int width,
                            int height,
                            int zoom,
                            bool draw_builtup,
                            bool draw_water)
{
    const int count = 1 << zoom;
    const double scale = pow(2.0, camera->zoom - zoom);
    const double tile_size = ORMAP_TILE_SIZE * scale;
    const OpenRidePointD center = openride_mercator_forward(camera->center_lat,
                                                             camera->center_lon);
    const double world_size = tile_size * count;
    const double center_x = center.x * world_size;
    const double center_y = center.y * world_size;
    const double bearing = camera->bearing_deg * 3.14159265358979323846 / 180.0;
    const double half_w = fabs(cos(bearing)) * width * 0.5
        + fabs(sin(bearing)) * height * 0.5;
    const double half_h = fabs(sin(bearing)) * width * 0.5
        + fabs(cos(bearing)) * height * 0.5;
    const int first_x = (int)floor((center_x - half_w) / tile_size);
    const int last_x = (int)floor((center_x + half_w) / tile_size);
    const int first_y = (int)floor((center_y - half_h) / tile_size);
    const int last_y = (int)floor((center_y + half_h) / tile_size);
    const OpenRideMapColor builtup_color = area_color(renderer, OPENRIDE_ORMAP_AREA_BUILTUP);
    const OpenRideMapColor water_color = area_color(renderer, OPENRIDE_ORMAP_AREA_WATER);
    GeometryBatch batch = {0};

    for (int ty = first_y; ty <= last_y; ++ty) {
        if (ty < 0 || ty >= count) continue;
        for (int tx = first_x; tx <= last_x; ++tx) {
            const int qx = wrap_x(tx, count);
            OpenRideORMapAreaCacheEntry *entry = area_cache_slot(renderer, zoom, qx, ty);
            if (!entry || entry->tile.count == 0U) continue;
            const double left = width * 0.5 + tx * tile_size - center_x;
            const double top = height * 0.5 + ty * tile_size - center_y;
            for (uint32_t i = 0U; i < entry->tile.count; ++i) {
                const OpenRideORMapAreaTriangle *triangle = &entry->tile.triangles[i];
                if ((triangle->kind == OPENRIDE_ORMAP_AREA_BUILTUP && !draw_builtup)
                    || (triangle->kind == OPENRIDE_ORMAP_AREA_WATER && !draw_water)) {
                    continue;
                }
                if (triangle->kind != OPENRIDE_ORMAP_AREA_BUILTUP
                    && triangle->kind != OPENRIDE_ORMAP_AREA_WATER) {
                    continue;
                }
                const uint16_t xs[3] = {triangle->x1, triangle->x2, triangle->x3};
                const uint16_t ys[3] = {triangle->y1, triangle->y2, triangle->y3};
                float x[3];
                float y[3];
                for (uint32_t v = 0U; v < 3U; ++v) {
                    x[v] = (float)(left + decode_area_coord(xs[v]) * tile_size);
                    y[v] = (float)(top + decode_area_coord(ys[v]) * tile_size);
                    rotate_point(camera, width, height, &x[v], &y[v]);
                }
                const OpenRideMapColor color = triangle->kind == OPENRIDE_ORMAP_AREA_WATER
                    ? water_color : builtup_color;
                if (!geometry_batch_triangle(renderer,
                                             &batch,
                                             x,
                                             y,
                                             color)) {
                    geometry_batch_flush(renderer, &batch);
                    return;
                }
            }
        }
    }
    geometry_batch_flush(renderer, &batch);
}

static void draw_areas(OpenRideORMapRenderer *renderer,
                       const OpenRideMapCamera *camera,
                       int width,
                       int height)
{
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    if (!metadata || metadata->format_version < 3 || camera->zoom < 10.0) return;

    if (camera->zoom < 12.5) {
        draw_area_level(renderer,
                        camera,
                        width,
                        height,
                        metadata->area_coarse_zoom,
                        false,
                        true);
    } else {
        draw_area_level(renderer,
                        camera,
                        width,
                        height,
                        metadata->area_detail_zoom,
                        camera->zoom >= 13.0,
                        true);
    }
}

static int waterway_width(uint8_t kind, double zoom)
{
    switch ((OpenRideORMapWaterwayKind)kind) {
        case OPENRIDE_ORMAP_WATERWAY_RIVER:
            return zoom >= 15.0 ? 5 : (zoom >= 13.0 ? 3 : 2);
        case OPENRIDE_ORMAP_WATERWAY_CANAL:
            return zoom >= 15.0 ? 4 : (zoom >= 12.0 ? 2 : 1);
        case OPENRIDE_ORMAP_WATERWAY_STREAM:
            return zoom >= 14.0 ? 2 : (zoom >= 12.5 ? 1 : 0);
        case OPENRIDE_ORMAP_WATERWAY_DRAIN:
            return zoom >= 14.5 ? 1 : 0;
        default:
            return 0;
    }
}

static void draw_waterways(OpenRideORMapRenderer *renderer,
                           const OpenRideMapCamera *camera,
                           int width,
                           int height)
{
    if (camera->zoom < 12.0) return;
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    if (!metadata || metadata->format_version < 2) return;
    const int zoom = metadata->water_zoom;
    const int count = 1 << zoom;
    const double scale = pow(2.0, camera->zoom - zoom);
    const double tile_size = ORMAP_TILE_SIZE * scale;
    const OpenRidePointD center = openride_mercator_forward(camera->center_lat,
                                                             camera->center_lon);
    const double world_size = tile_size * count;
    const double center_x = center.x * world_size;
    const double center_y = center.y * world_size;
    const double bearing = camera->bearing_deg * 3.14159265358979323846 / 180.0;
    const double half_w = fabs(cos(bearing)) * width * 0.5 + fabs(sin(bearing)) * height * 0.5;
    const double half_h = fabs(sin(bearing)) * width * 0.5 + fabs(cos(bearing)) * height * 0.5;
    const int first_x = (int)floor((center_x - half_w) / tile_size);
    const int last_x = (int)floor((center_x + half_w) / tile_size);
    const int first_y = (int)floor((center_y - half_h) / tile_size);
    const int last_y = (int)floor((center_y + half_h) / tile_size);
    OpenRideMapColor color = openride_map_palette(renderer->style).water;
    color.a = 235;
    GeometryBatch batch = {0};

    for (int ty = first_y; ty <= last_y; ++ty) {
        if (ty < 0 || ty >= count) continue;
        for (int tx = first_x; tx <= last_x; ++tx) {
            const int qx = wrap_x(tx, count);
            OpenRideORMapWaterCacheEntry *entry = water_cache_slot(renderer, zoom, qx, ty);
            if (!entry || entry->tile.count == 0U) continue;
            const double left = width * 0.5 + tx * tile_size - center_x;
            const double top = height * 0.5 + ty * tile_size - center_y;
            for (uint32_t i = 0U; i < entry->tile.count; ++i) {
                const OpenRideORMapWaterRecord *record = &entry->tile.records[i];
                const int line_width = waterway_width(record->kind, camera->zoom);
                if (line_width <= 0) continue;
                float x1 = (float)(left + ((double)record->x1 / 65535.0) * tile_size);
                float y1 = (float)(top + ((double)record->y1 / 65535.0) * tile_size);
                float x2 = (float)(left + ((double)record->x2 / 65535.0) * tile_size);
                float y2 = (float)(top + ((double)record->y2 / 65535.0) * tile_size);
                rotate_point(camera, width, height, &x1, &y1);
                rotate_point(camera, width, height, &x2, &y2);
                if (!geometry_batch_line(renderer,
                                         &batch,
                                         x1,
                                         y1,
                                         x2,
                                         y2,
                                         line_width,
                                         color)) {
                    geometry_batch_flush(renderer, &batch);
                    return;
                }
            }
        }
    }
    geometry_batch_flush(renderer, &batch);
}

typedef struct RoadPaintTable {
    OpenRideMapRoadPaint paints[OPENRIDE_ROAD_OTHER + 1];
    bool visible[OPENRIDE_ROAD_OTHER + 1];
} RoadPaintTable;

static void build_road_paint_table(OpenRideORMapRenderer *renderer,
                                   double zoom,
                                   RoadPaintTable *table)
{
    memset(table, 0, sizeof(*table));
    for (int road_class = OPENRIDE_ROAD_UNKNOWN;
         road_class <= OPENRIDE_ROAD_OTHER;
         ++road_class) {
        table->visible[road_class] = openride_map_road_paint(
            renderer->style,
            road_kind((uint8_t)road_class),
            false,
            zoom,
            &table->paints[road_class]);
    }
}

static void draw_road_pass(OpenRideORMapRenderer *renderer,
                           const OpenRideMapCamera *camera,
                           int width,
                           int height,
                           int zoom,
                           const RoadPaintTable *paint_table,
                           bool casing_pass)
{
    const int count = 1 << zoom;
    const double scale = pow(2.0, camera->zoom - zoom);
    const double tile_size = ORMAP_TILE_SIZE * scale;
    const OpenRidePointD center = openride_mercator_forward(camera->center_lat,
                                                             camera->center_lon);
    const double world_size = tile_size * count;
    const double center_x = center.x * world_size;
    const double center_y = center.y * world_size;
    const double bearing = camera->bearing_deg * 3.14159265358979323846 / 180.0;
    const double half_w = fabs(cos(bearing)) * width * 0.5 + fabs(sin(bearing)) * height * 0.5;
    const double half_h = fabs(sin(bearing)) * width * 0.5 + fabs(cos(bearing)) * height * 0.5;
    const int first_x = (int)floor((center_x - half_w) / tile_size);
    const int last_x = (int)floor((center_x + half_w) / tile_size);
    const int first_y = (int)floor((center_y - half_h) / tile_size);
    const int last_y = (int)floor((center_y + half_h) / tile_size);
    GeometryBatch batch = {0};

    for (int ty = first_y; ty <= last_y; ++ty) {
        if (ty < 0 || ty >= count) continue;
        for (int tx = first_x; tx <= last_x; ++tx) {
            const int qx = wrap_x(tx, count);
            OpenRideORMapRoadCacheEntry *entry = road_cache_slot(renderer, zoom, qx, ty);
            if (!entry || entry->tile.count == 0U) continue;
            const double left = width * 0.5 + tx * tile_size - center_x;
            const double top = height * 0.5 + ty * tile_size - center_y;
            for (uint32_t r = 0U; r < entry->tile.count; ++r) {
                const OpenRideORMapRoadRecord *record = &entry->tile.records[r];
                const uint8_t road_class = record->road_class <= OPENRIDE_ROAD_OTHER
                    ? record->road_class : OPENRIDE_ROAD_OTHER;
                if (!paint_table->visible[road_class]) continue;
                const OpenRideMapRoadPaint paint = paint_table->paints[road_class];
                if (casing_pass
                    && !(paint.casing_width > paint.width && paint.casing.a > 0U)) {
                    continue;
                }
                float x1 = (float)(left + ((double)record->x1 / 65535.0) * tile_size);
                float y1 = (float)(top + ((double)record->y1 / 65535.0) * tile_size);
                float x2 = (float)(left + ((double)record->x2 / 65535.0) * tile_size);
                float y2 = (float)(top + ((double)record->y2 / 65535.0) * tile_size);
                rotate_point(camera, width, height, &x1, &y1);
                rotate_point(camera, width, height, &x2, &y2);

                bool ok = true;
                if (casing_pass) {
                    ok = geometry_batch_line(renderer,
                                             &batch,
                                             x1,
                                             y1,
                                             x2,
                                             y2,
                                             paint.casing_width,
                                             paint.casing);
                } else if (paint.dashed) {
                    ok = geometry_batch_dashed_line(renderer,
                                                    &batch,
                                                    x1,
                                                    y1,
                                                    x2,
                                                    y2,
                                                    paint.width,
                                                    paint.line);
                } else {
                    ok = geometry_batch_line(renderer,
                                             &batch,
                                             x1,
                                             y1,
                                             x2,
                                             y2,
                                             paint.width,
                                             paint.line);
                }
                if (!ok) {
                    geometry_batch_flush(renderer, &batch);
                    return;
                }
            }
        }
    }
    geometry_batch_flush(renderer, &batch);
}

static void draw_roads(OpenRideORMapRenderer *renderer,
                       const OpenRideMapCamera *camera,
                       int width,
                       int height)
{
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    if (!metadata) return;
    int zoom = (int)floor(camera->zoom);
    if (zoom < metadata->min_zoom) zoom = metadata->min_zoom;
    if (zoom > metadata->road_max_zoom) zoom = metadata->road_max_zoom;

    RoadPaintTable paint_table;
    build_road_paint_table(renderer, camera->zoom, &paint_table);

    /* Render all casings first, then all coloured road strokes. Besides being
     * visually cleaner at crossings, each pass is submitted in large geometry
     * batches instead of issuing several SDL line calls per OSM segment. */
    draw_road_pass(renderer, camera, width, height, zoom, &paint_table, true);
    draw_road_pass(renderer, camera, width, height, zoom, &paint_table, false);
}

static const char *label_kind_name(int kind)
{
    switch ((OpenRidePlaceKind)kind) {
        case OPENRIDE_PLACE_CITY: return "city";
        case OPENRIDE_PLACE_TOWN: return "town";
        case OPENRIDE_PLACE_VILLAGE: return "village";
        case OPENRIDE_PLACE_HAMLET: return "hamlet";
        case OPENRIDE_PLACE_SUBURB: return "suburb";
        case OPENRIDE_PLACE_QUARTER: return "quarter";
        default: return "locality";
    }
}

static bool boxes_overlap(LabelBox a, LabelBox b)
{
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

static void draw_labels(OpenRideORMapRenderer *renderer,
                        const OpenRideMapCamera *camera,
                        int width,
                        int height)
{
    uint32_t count = 0U;
    const OpenRideORMapLabel *labels = openride_ormap_labels(renderer->map, &count);
    if (!labels || count == 0U) return;
    LabelBox boxes[ORMAP_LABEL_BOX_MAX];
    uint32_t box_count = 0U;
    const OpenRideMapPalette palette = openride_map_palette(renderer->style);
    for (uint32_t i = 0U; i < count && box_count < ORMAP_LABEL_BOX_MAX; ++i) {
        const OpenRideORMapLabel *label = &labels[i];
        const char *kind = label_kind_name(label->kind);
        if (!openride_map_place_label_visible(kind, 0, camera->zoom)) continue;
        const OpenRidePointD p = openride_geo_to_screen(camera,
                                                         label->lat_e7 / 10000000.0,
                                                         label->lon_e7 / 10000000.0,
                                                         width,
                                                         height);
        if (p.x < -100.0 || p.x > width + 100.0 || p.y < -40.0 || p.y > height + 40.0) continue;
        const float text_w = (float)strlen(label->name) * 8.0f;
        LabelBox box = {
            .left = (float)p.x - text_w * 0.5f - 4.0f,
            .top = (float)p.y - 7.0f,
            .right = (float)p.x + text_w * 0.5f + 4.0f,
            .bottom = (float)p.y + 10.0f
        };
        bool collision = false;
        for (uint32_t b = 0U; b < box_count; ++b) {
            if (boxes_overlap(box, boxes[b])) { collision = true; break; }
        }
        if (collision) continue;
        boxes[box_count++] = box;
        set_color(renderer->renderer, palette.label_halo);
        SDL_RenderDebugText(renderer->renderer,
                            (float)p.x - text_w * 0.5f + 1.0f,
                            (float)p.y + 1.0f,
                            label->name);
        set_color(renderer->renderer, palette.label);
        SDL_RenderDebugText(renderer->renderer,
                            (float)p.x - text_w * 0.5f,
                            (float)p.y,
                            label->name);
    }
}

bool openride_ormap_renderer_init(OpenRideORMapRenderer *renderer,
                                  SDL_Renderer *sdl_renderer,
                                  OpenRideORMap *map)
{
    if (!renderer || !sdl_renderer || !map) return false;
    memset(renderer, 0, sizeof(*renderer));
    renderer->renderer = sdl_renderer;
    renderer->map = map;
    renderer->style = OPENRIDE_MAP_STYLE_TRAIL;
    return true;
}

void openride_ormap_renderer_destroy(OpenRideORMapRenderer *renderer)
{
    if (!renderer) return;
    for (size_t i = 0U; i < OPENRIDE_ORMAP_ROAD_CACHE_CAPACITY; ++i) {
        if (renderer->roads[i].occupied) openride_ormap_road_tile_destroy(&renderer->roads[i].tile);
    }
    for (size_t i = 0U; i < OPENRIDE_ORMAP_MASK_CACHE_CAPACITY; ++i) {
        if (renderer->masks[i].occupied) openride_ormap_mask_tile_destroy(&renderer->masks[i].tile);
    }
    for (size_t i = 0U; i < OPENRIDE_ORMAP_WATER_CACHE_CAPACITY; ++i) {
        if (renderer->waters[i].occupied) openride_ormap_water_tile_destroy(&renderer->waters[i].tile);
    }
    for (size_t i = 0U; i < OPENRIDE_ORMAP_AREA_CACHE_CAPACITY; ++i) {
        if (renderer->areas[i].occupied) openride_ormap_area_tile_destroy(&renderer->areas[i].tile);
    }
    free(renderer->area_vertices);
    free(renderer->area_indices);
    memset(renderer, 0, sizeof(*renderer));
}

void openride_ormap_renderer_set_style(OpenRideORMapRenderer *renderer,
                                       OpenRideMapStyle style)
{
    if (renderer) renderer->style = style;
}

void openride_ormap_renderer_draw(OpenRideORMapRenderer *renderer,
                                  const OpenRideMapCamera *camera,
                                  int viewport_width,
                                  int viewport_height)
{
    if (!renderer || !camera || viewport_width <= 0 || viewport_height <= 0) return;
    ++renderer->frame_counter;
    const OpenRideMapPalette palette = openride_map_palette(renderer->style);
    set_color(renderer->renderer, palette.background);
    SDL_RenderClear(renderer->renderer);
    draw_masks(renderer, camera, viewport_width, viewport_height);
    draw_areas(renderer, camera, viewport_width, viewport_height);
    draw_waterways(renderer, camera, viewport_width, viewport_height);
    draw_roads(renderer, camera, viewport_width, viewport_height);
    draw_labels(renderer, camera, viewport_width, viewport_height);
}
