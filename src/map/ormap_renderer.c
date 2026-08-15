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
#define ORMAP_ROAD_PREWARM_TILE_BUDGET 3U
#define ORMAP_ROAD_DRAW_LOAD_BUDGET 2U
#define ORMAP_BUILTUP_MASK_PREWARM_TILE_BUDGET 6U
#define ORMAP_BUILTUP_PREWARM_START 12.20
#define ORMAP_BUILTUP_OVERVIEW_START 13.15
#define ORMAP_BUILTUP_OVERVIEW_FULL 13.35
#define ORMAP_BUILTUP_DETAIL_START 14.00
#define ORMAP_BUILTUP_DETAIL_END 14.55

static OpenRideORMapRoadDebugStats g_last_road_debug_stats;
static const OpenRideORMapRenderer *g_last_road_debug_renderer = NULL;
static OpenRideORMapAreaDebugStats g_last_area_debug_stats;
static const OpenRideORMapRenderer *g_last_area_debug_renderer = NULL;

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

static double ormap_zoom_smoothstep(double zoom,
                                   double start,
                                   double end)
{
    if (zoom <= start) return 0.0;
    if (zoom >= end) return 1.0;
    double t = (zoom - start) / (end - start);
    return t * t * (3.0 - 2.0 * t);
}

static uint8_t ormap_scaled_alpha(uint8_t alpha, double factor)
{
    if (factor <= 0.0) return 0U;
    if (factor >= 1.0) return alpha;
    return (uint8_t)lround((double)alpha * factor);
}

static double ormap_detail_handoff_fade(double zoom)
{
    return ormap_zoom_smoothstep(zoom, 10.0, 11.30);
}

static void ormap_scale_color_alpha(OpenRideMapColor *color, double factor)
{
    if (!color) return;
    color->a = ormap_scaled_alpha(color->a, factor);
}

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
    if (renderer->road_debug_active) {
        ++renderer->road_debug.batches;
    }
    if (renderer->area_debug_active) {
        ++renderer->area_debug.batches;
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

static void road_cache_entry_destroy(OpenRideORMapRoadCacheEntry *entry)
{
    if (!entry) return;
    if (entry->occupied) {
        openride_ormap_road_tile_destroy(&entry->tile);
    }
    free(entry->record_order);
    memset(entry, 0, sizeof(*entry));
}

static bool road_cache_build_class_index(OpenRideORMapRoadCacheEntry *entry)
{
    if (!entry) return false;

    free(entry->record_order);
    entry->record_order = NULL;
    memset(entry->class_offsets, 0, sizeof(entry->class_offsets));

    if (entry->tile.count == 0U) return true;

    uint32_t counts[OPENRIDE_ROAD_OTHER + 1] = {0};
    for (uint32_t i = 0U; i < entry->tile.count; ++i) {
        const uint8_t raw_class = entry->tile.records[i].road_class;
        const uint8_t road_class =
            raw_class <= OPENRIDE_ROAD_OTHER
                ? raw_class
                : OPENRIDE_ROAD_OTHER;
        ++counts[road_class];
    }

    uint32_t cursor = 0U;
    for (int road_class = OPENRIDE_ROAD_UNKNOWN;
         road_class <= OPENRIDE_ROAD_OTHER;
         ++road_class) {
        entry->class_offsets[road_class] = cursor;
        cursor += counts[road_class];
    }
    entry->class_offsets[OPENRIDE_ROAD_OTHER + 1] = cursor;

    entry->record_order =
        malloc((size_t)entry->tile.count * sizeof(*entry->record_order));
    if (!entry->record_order) {
        memset(entry->class_offsets, 0, sizeof(entry->class_offsets));
        return false;
    }

    uint32_t write_cursor[OPENRIDE_ROAD_OTHER + 1];
    for (int road_class = OPENRIDE_ROAD_UNKNOWN;
         road_class <= OPENRIDE_ROAD_OTHER;
         ++road_class) {
        write_cursor[road_class] = entry->class_offsets[road_class];
    }

    for (uint32_t i = 0U; i < entry->tile.count; ++i) {
        const uint8_t raw_class = entry->tile.records[i].road_class;
        const uint8_t road_class =
            raw_class <= OPENRIDE_ROAD_OTHER
                ? raw_class
                : OPENRIDE_ROAD_OTHER;
        entry->record_order[write_cursor[road_class]++] = i;
    }

    return true;
}

static const uint8_t OPENRIDE_ROAD_DRAW_ORDER[] = {
    OPENRIDE_ROAD_OTHER,
    OPENRIDE_ROAD_PATH,
    OPENRIDE_ROAD_TRACK,
    OPENRIDE_ROAD_LIVING_STREET,
    OPENRIDE_ROAD_SERVICE,
    OPENRIDE_ROAD_RESIDENTIAL,
    OPENRIDE_ROAD_UNCLASSIFIED,
    OPENRIDE_ROAD_TERTIARY,
    OPENRIDE_ROAD_SECONDARY,
    OPENRIDE_ROAD_PRIMARY,
    OPENRIDE_ROAD_TRUNK,
    OPENRIDE_ROAD_MOTORWAY,
    OPENRIDE_ROAD_UNKNOWN
};

static bool road_cache_contains(const OpenRideORMapRenderer *renderer,
                                int zoom,
                                int x,
                                int y)
{
    if (!renderer) return false;
    const size_t base = tile_cache_set_base(OPENRIDE_ORMAP_ROAD_CACHE_CAPACITY,
                                            zoom, x, y);
    for (size_t i = 0U; i < ORMAP_CACHE_ASSOCIATIVITY; ++i) {
        const OpenRideORMapRoadCacheEntry *entry = &renderer->roads[base + i];
        if (entry->occupied && entry->zoom == zoom && entry->x == x && entry->y == y) {
            return true;
        }
    }
    return false;
}

static OpenRideORMapRoadCacheEntry *road_cache_slot(OpenRideORMapRenderer *renderer,
                                                     int zoom,
                                                     int x,
                                                     int y,
                                                     bool prewarm,
                                                     bool budgeted_draw_load)
{
    const size_t base = tile_cache_set_base(OPENRIDE_ORMAP_ROAD_CACHE_CAPACITY,
                                            zoom, x, y);
    OpenRideORMapRoadCacheEntry *victim = NULL;
    OpenRideORMapRoadCacheEntry *oldest = &renderer->roads[base];
    for (size_t i = 0U; i < ORMAP_CACHE_ASSOCIATIVITY; ++i) {
        OpenRideORMapRoadCacheEntry *entry = &renderer->roads[base + i];
        if (entry->occupied && entry->zoom == zoom && entry->x == x && entry->y == y) {
            entry->last_used = renderer->frame_counter;
            ++renderer->road_debug.cache_hits;
            return entry;
        }
        if (!entry->occupied && !victim) victim = entry;
        if (entry->occupied && oldest->occupied && entry->last_used < oldest->last_used) {
            oldest = entry;
        }
    }

    ++renderer->road_debug.cache_misses;
    if (budgeted_draw_load && !prewarm) {
        if (renderer->road_draw_load_budget_remaining == 0U) {
            ++renderer->road_debug.deferred_loads;
            return NULL;
        }
        --renderer->road_draw_load_budget_remaining;
    }

    if (!victim) {
        victim = oldest;
        /* Speculative prewarm must not evict a tile used by the immediately
         * previous rendered frame. The real draw path may still replace it. */
        if (prewarm && victim->occupied
            && victim->last_used + 1U >= renderer->frame_counter) {
            return NULL;
        }
    }
    road_cache_entry_destroy(victim);
    victim->occupied = true;
    victim->zoom = zoom;
    victim->x = x;
    victim->y = y;
    victim->last_used = renderer->frame_counter;

    const uint64_t load_started = SDL_GetTicksNS();
    char error[160] = {0};
    if (!openride_ormap_load_road_tile(renderer->map,
                                       zoom,
                                       x,
                                       y,
                                       &victim->tile,
                                       error,
                                       sizeof(error))) {
        /* Keep an occupied empty entry to cache absent tiles too. */
    } else {
        (void)road_cache_build_class_index(victim);
    }
    renderer->road_debug.load_ms +=
        (double)(SDL_GetTicksNS() - load_started) / 1000000.0;
    if (prewarm) {
        ++renderer->road_debug.prewarm_loads;
    } else {
        ++renderer->road_debug.draw_loads;
    }
    return victim;
}

static bool mask_cache_contains(const OpenRideORMapRenderer *renderer,
                                int zoom,
                                int x,
                                int y)
{
    if (!renderer) return false;
    const size_t base = tile_cache_set_base(OPENRIDE_ORMAP_MASK_CACHE_CAPACITY,
                                            zoom, x, y);
    for (size_t i = 0U; i < ORMAP_CACHE_ASSOCIATIVITY; ++i) {
        const OpenRideORMapMaskCacheEntry *entry = &renderer->masks[base + i];
        if (entry->occupied && entry->zoom == zoom && entry->x == x && entry->y == y) {
            return true;
        }
    }
    return false;
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
    const uint64_t load_started = SDL_GetTicksNS();
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
    if (renderer->area_debug_active) {
        renderer->area_debug.load_ms +=
            (double)(SDL_GetTicksNS() - load_started) / 1000000.0;
    }
    return victim;
}

static uint32_t prewarm_mask_level(OpenRideORMapRenderer *renderer,
                                   const OpenRideMapCamera *camera,
                                   int width,
                                   int height,
                                   int zoom,
                                   double viewport_zoom,
                                   uint32_t budget)
{
    if (!renderer || !camera || budget == 0U) return 0U;
    const int count = 1 << zoom;
    const double scale = pow(2.0, viewport_zoom - zoom);
    const double tile_size = ORMAP_TILE_SIZE * scale;
    const OpenRidePointD center =
        openride_mercator_forward(camera->center_lat, camera->center_lon);
    const double world_size = tile_size * count;
    const double center_x = center.x * world_size;
    const double center_y = center.y * world_size;
    const double bearing =
        camera->bearing_deg * 3.14159265358979323846 / 180.0;
    const double half_w = fabs(cos(bearing)) * width * 0.5
        + fabs(sin(bearing)) * height * 0.5;
    const double half_h = fabs(sin(bearing)) * width * 0.5
        + fabs(cos(bearing)) * height * 0.5;
    const int first_x = (int)floor((center_x - half_w) / tile_size);
    const int last_x = (int)floor((center_x + half_w) / tile_size);
    const int first_y = (int)floor((center_y - half_h) / tile_size);
    const int last_y = (int)floor((center_y + half_h) / tile_size);

    uint32_t loaded = 0U;
    for (int ty = first_y; ty <= last_y; ++ty) {
        if (ty < 0 || ty >= count) continue;
        for (int tx = first_x; tx <= last_x; ++tx) {
            const int qx = wrap_x(tx, count);
            if (mask_cache_contains(renderer, zoom, qx, ty)) continue;
            if (!mask_cache_slot(renderer, zoom, qx, ty)) continue;
            ++renderer->area_debug.mask_prewarm_loads;
            if (++loaded >= budget) return loaded;
        }
    }
    return loaded;
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
    const uint64_t load_started = SDL_GetTicksNS();
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
    if (renderer->area_debug_active) {
        renderer->area_debug.load_ms +=
            (double)(SDL_GetTicksNS() - load_started) / 1000000.0;
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
    const uint64_t masks_started = SDL_GetTicksNS();
    const bool previous_area_debug_active = renderer->area_debug_active;
    renderer->area_debug_active = true;

    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    const bool v4 = metadata && metadata->format_version >= 4;
    const bool v6 = metadata && metadata->format_version >= 6;
    const double forest_start = v4 ? 13.40 : 14.0;
    const double forest_end = v4 ? 14.40 : 14.50;
    const int zoom = metadata ? metadata->mask_zoom : OPENRIDE_ORMAP_MASK_ZOOM;

    if (v6
        && camera->zoom >= ORMAP_BUILTUP_PREWARM_START
        && camera->zoom < ORMAP_BUILTUP_OVERVIEW_FULL) {
        (void)prewarm_mask_level(renderer,
                                 camera,
                                 width,
                                 height,
                                 zoom,
                                 ORMAP_BUILTUP_OVERVIEW_START,
                                 ORMAP_BUILTUP_MASK_PREWARM_TILE_BUDGET);
    }

    const double builtup_overview_fade = v6
        ? ormap_zoom_smoothstep(camera->zoom,
                                ORMAP_BUILTUP_OVERVIEW_START,
                                ORMAP_BUILTUP_OVERVIEW_FULL)
            * (1.0 - ormap_zoom_smoothstep(camera->zoom,
                                           ORMAP_BUILTUP_DETAIL_START,
                                           ORMAP_BUILTUP_DETAIL_END))
        : 0.0;
    const bool draw_forest =
        camera->zoom >= forest_start && camera->zoom <= 17.2;
    const bool draw_builtup_overview = builtup_overview_fade > 0.001;
    const bool draw_legacy_masks =
        (!metadata || metadata->format_version < 3) && draw_forest;
    if (!draw_forest && !draw_builtup_overview && !draw_legacy_masks) {
        renderer->area_debug.areas_ms +=
            (double)(SDL_GetTicksNS() - masks_started) / 1000000.0;
        renderer->area_debug_active = previous_area_debug_active;
        return;
    }

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
    OpenRideMapColor forest = forest_color(renderer->style);

    const double forest_fade =
        ormap_zoom_smoothstep(camera->zoom, forest_start, forest_end);
    ormap_scale_color_alpha(&forest, forest_fade);
    ormap_scale_color_alpha(&builtup,
                            v6 ? builtup_overview_fade : forest_fade);
    ormap_scale_color_alpha(&water, forest_fade);

    GeometryBatch batch = {0};
    for (int ty = first_y; ty <= last_y; ++ty) {
        if (ty < 0 || ty >= count) continue;
        for (int tx = first_x; tx <= last_x; ++tx) {
            const int qx = wrap_x(tx, count);
            ++renderer->area_debug.tiles_visited;
            OpenRideORMapMaskCacheEntry *entry = mask_cache_slot(renderer, zoom, qx, ty);
            if (!entry) continue;
            const double left = width * 0.5 + tx * tile_size - center_x;
            const double top = height * 0.5 + ty * tile_size - center_y;

            if (draw_forest && entry->tile.forest && forest.a > 0U) {
                if (!draw_mask_layer(renderer, &batch, camera, width, height,
                                     tile_size, left, top, &entry->tile,
                                     entry->tile.forest, forest)) {
                    geometry_batch_flush(renderer, &batch);
                    goto masks_done;
                }
            }

            if (draw_builtup_overview && entry->tile.builtup && builtup.a > 0U) {
                if (!draw_mask_layer(renderer, &batch, camera, width, height,
                                     tile_size, left, top, &entry->tile,
                                     entry->tile.builtup, builtup)) {
                    geometry_batch_flush(renderer, &batch);
                    goto masks_done;
                }
            }

            /* v1/v2 stored filled water/built-up areas only as semantic cells. */
            if (draw_legacy_masks) {
                if (entry->tile.builtup && builtup.a > 0U
                    && !draw_mask_layer(renderer, &batch, camera, width, height,
                                        tile_size, left, top, &entry->tile,
                                        entry->tile.builtup, builtup)) {
                    geometry_batch_flush(renderer, &batch);
                    goto masks_done;
                }
                if (entry->tile.water && water.a > 0U
                    && !draw_mask_layer(renderer, &batch, camera, width, height,
                                        tile_size, left, top, &entry->tile,
                                        entry->tile.water, water)) {
                    geometry_batch_flush(renderer, &batch);
                    goto masks_done;
                }
            }
        }
    }
    geometry_batch_flush(renderer, &batch);

masks_done:
    renderer->area_debug.areas_ms +=
        (double)(SDL_GetTicksNS() - masks_started) / 1000000.0;
    renderer->area_debug_active = previous_area_debug_active;
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
                            bool draw_water,
                            double level_fade)
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
    OpenRideMapColor builtup_color =
        area_color(renderer, OPENRIDE_ORMAP_AREA_BUILTUP);
    OpenRideMapColor water_color =
        area_color(renderer, OPENRIDE_ORMAP_AREA_WATER);

    const double water_fade =
        ormap_detail_handoff_fade(camera->zoom) * level_fade;
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    const bool v6 = metadata && metadata->format_version >= 6;
    const double builtup_fade =
        ormap_zoom_smoothstep(camera->zoom,
                              v6 ? ORMAP_BUILTUP_DETAIL_START : 13.0,
                              v6 ? ORMAP_BUILTUP_DETAIL_END : 13.45)
        * level_fade;
    ormap_scale_color_alpha(&water_color, water_fade);
    ormap_scale_color_alpha(&builtup_color, builtup_fade);

    GeometryBatch batch = {0};

    for (int ty = first_y; ty <= last_y; ++ty) {
        if (ty < 0 || ty >= count) continue;
        for (int tx = first_x; tx <= last_x; ++tx) {
            const int qx = wrap_x(tx, count);
            ++renderer->area_debug.tiles_visited;
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
                if (color.a == 0U) continue;
                ++renderer->area_debug.triangles_drawn;
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
    const uint64_t areas_started = SDL_GetTicksNS();
    const bool previous_area_debug_active = renderer->area_debug_active;
    renderer->area_debug_active = true;

    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    if (metadata && metadata->format_version >= 3 && camera->zoom >= 10.0) {
        const double detail_mix =
            ormap_zoom_smoothstep(camera->zoom, 12.15, 12.85);
        const bool v6 = metadata->format_version >= 6;
        const bool draw_detail_builtup =
            camera->zoom >= (v6 ? ORMAP_BUILTUP_DETAIL_START : 13.0);

        if (detail_mix < 1.0) {
            draw_area_level(renderer,
                            camera,
                            width,
                            height,
                            metadata->area_coarse_zoom,
                            false,
                            true,
                            1.0 - detail_mix);
        }
        if (detail_mix > 0.0) {
            draw_area_level(renderer,
                            camera,
                            width,
                            height,
                            metadata->area_detail_zoom,
                            draw_detail_builtup,
                            true,
                            detail_mix);
        }
    }

    renderer->area_debug.areas_ms +=
        (double)(SDL_GetTicksNS() - areas_started) / 1000000.0;
    renderer->area_debug_active = previous_area_debug_active;
    g_last_area_debug_stats = renderer->area_debug;
    g_last_area_debug_renderer = renderer;
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

static double waterway_fade_factor(uint8_t kind, double zoom)
{
    switch ((OpenRideORMapWaterwayKind)kind) {
        case OPENRIDE_ORMAP_WATERWAY_RIVER:
        case OPENRIDE_ORMAP_WATERWAY_CANAL:
            return ormap_zoom_smoothstep(zoom, 12.0, 12.40);
        case OPENRIDE_ORMAP_WATERWAY_STREAM:
            return ormap_zoom_smoothstep(zoom, 12.5, 12.90);
        case OPENRIDE_ORMAP_WATERWAY_DRAIN:
            return ormap_zoom_smoothstep(zoom, 14.5, 14.90);
        default:
            return 0.0;
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
                const int line_width =
                    waterway_width(record->kind, camera->zoom);
                if (line_width <= 0) continue;

                OpenRideMapColor line_color = color;
                const double waterway_fade =
                    waterway_fade_factor(record->kind, camera->zoom);
                ormap_scale_color_alpha(&line_color, waterway_fade);
                if (line_color.a == 0U) continue;

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
                                         line_color)) {
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

static void apply_android_minor_road_lod(double zoom,
                                         int road_class,
                                         OpenRideMapRoadPaint *paint)
{
#ifdef __ANDROID__
    if (!paint || zoom >= 14.50) return;

    const bool local =
        road_class == OPENRIDE_ROAD_UNCLASSIFIED
        || road_class == OPENRIDE_ROAD_RESIDENTIAL
        || road_class == OPENRIDE_ROAD_SERVICE
        || road_class == OPENRIDE_ROAD_LIVING_STREET;
    const bool trail_detail =
        road_class == OPENRIDE_ROAD_TRACK
        || road_class == OPENRIDE_ROAD_PATH;

    /*
     * Below z14.5 the casing is more expensive than the visual information
     * it adds on a phone. Removing it makes the casing pass skip the entire
     * class before touching any road record.
     *
     * Restore the exact original style as the camera approaches z14.
     */
    if (zoom < 14.50) {
        paint->casing_width = 0;
        paint->casing.a = 0U;
    }

    if (local || trail_detail) {
        paint->dashed = false;
        if (local) {
            paint->width = 1;
        } else if (paint->width > 2) {
            paint->width = 2;
        }
    }

    /*
     * At regional/inter-city scales a 2 px major-road stroke is enough.
     * Geometry count is unchanged, but rasterization/submission is lighter
     * and the hierarchy remains readable without the casing.
     */
    if (zoom < 12.75
        && road_class >= OPENRIDE_ROAD_MOTORWAY
        && road_class <= OPENRIDE_ROAD_SECONDARY
        && paint->width > 2) {
        paint->width = 2;
    }
#else
    (void)zoom;
    (void)road_class;
    (void)paint;
#endif
}

static double android_road_class_fade(double zoom, int road_class)
{
#ifdef __ANDROID__
    switch ((OpenRideRoadClass)road_class) {
        case OPENRIDE_ROAD_SECONDARY:
            return ormap_zoom_smoothstep(zoom, 11.75, 12.15);

        case OPENRIDE_ROAD_TERTIARY:
            return ormap_zoom_smoothstep(zoom, 12.75, 13.15);

        case OPENRIDE_ROAD_UNCLASSIFIED:
        case OPENRIDE_ROAD_RESIDENTIAL:
        case OPENRIDE_ROAD_SERVICE:
        case OPENRIDE_ROAD_LIVING_STREET:
        case OPENRIDE_ROAD_OTHER:
            return ormap_zoom_smoothstep(zoom, 13.75, 14.20);

        case OPENRIDE_ROAD_TRACK:
        case OPENRIDE_ROAD_PATH:
            return ormap_zoom_smoothstep(zoom, 14.50, 14.95);

        default:
            return 1.0;
    }
#else
    (void)zoom;
    (void)road_class;
    return 1.0;
#endif
}

static void apply_road_fades(double zoom,
                             int road_class,
                             double level_fade,
                             OpenRideMapRoadPaint *paint)
{
    if (!paint) return;
    const double fade =
        ormap_detail_handoff_fade(zoom)
        * android_road_class_fade(zoom, road_class)
        * level_fade;
    ormap_scale_color_alpha(&paint->line, fade);
    ormap_scale_color_alpha(&paint->casing, fade);
}

static void build_road_paint_table(OpenRideORMapRenderer *renderer,
                                   double zoom,
                                   double level_fade,
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
        if (table->visible[road_class]) {
            apply_android_minor_road_lod(
                zoom,
                road_class,
                &table->paints[road_class]);
            apply_road_fades(
                zoom,
                road_class,
                level_fade,
                &table->paints[road_class]);
        }
    }
}

static bool road_paint_table_has_casing(const RoadPaintTable *table)
{
    if (!table) return false;
    for (int road_class = OPENRIDE_ROAD_UNKNOWN;
         road_class <= OPENRIDE_ROAD_OTHER;
         ++road_class) {
        if (!table->visible[road_class]) continue;
        const OpenRideMapRoadPaint *paint = &table->paints[road_class];
        if (paint->casing_width > paint->width && paint->casing.a > 0U) return true;
    }
    return false;
}

typedef enum RoadLODClassGroup {
    ROAD_LOD_GROUP_MAJOR = 0,
    ROAD_LOD_GROUP_PRIMARY,
    ROAD_LOD_GROUP_LOCAL,
    ROAD_LOD_GROUP_DETAIL
} RoadLODClassGroup;

static RoadLODClassGroup road_lod_class_group(int road_class)
{
    switch ((OpenRideRoadClass)road_class) {
        case OPENRIDE_ROAD_MOTORWAY:
        case OPENRIDE_ROAD_TRUNK:
            return ROAD_LOD_GROUP_MAJOR;
        case OPENRIDE_ROAD_PRIMARY:
            return ROAD_LOD_GROUP_PRIMARY;
        case OPENRIDE_ROAD_SECONDARY:
        case OPENRIDE_ROAD_TERTIARY:
            return ROAD_LOD_GROUP_LOCAL;
        default:
            return ROAD_LOD_GROUP_DETAIL;
    }
}

static int road_lod_group_source(RoadLODClassGroup group, double zoom)
{
    switch (group) {
        case ROAD_LOD_GROUP_MAJOR:
            if (zoom < 11.25) return 0;
            if (zoom < 12.80) return 1;
            if (zoom < 14.40) return 2;
            return 3;
        case ROAD_LOD_GROUP_PRIMARY:
            if (zoom < 12.80) return 1;
            if (zoom < 14.40) return 2;
            return 3;
        case ROAD_LOD_GROUP_LOCAL:
            return zoom < 14.40 ? 2 : 3;
        case ROAD_LOD_GROUP_DETAIL:
        default:
            return 3;
    }
}

static double road_lod_group_fade(RoadLODClassGroup group,
                                  double regional_to_overview,
                                  double overview_to_local,
                                  double local_to_detail)
{
    switch (group) {
        case ROAD_LOD_GROUP_MAJOR:
            return 1.0;
        case ROAD_LOD_GROUP_PRIMARY:
            return regional_to_overview;
        case ROAD_LOD_GROUP_LOCAL:
            return overview_to_local;
        case ROAD_LOD_GROUP_DETAIL:
        default:
            return local_to_detail;
    }
}

static void road_paint_table_configure_lod(RoadPaintTable *table,
                                           int lod_index,
                                           double zoom,
                                           double regional_to_overview,
                                           double overview_to_local,
                                           double local_to_detail)
{
    if (!table) return;
    for (int road_class = OPENRIDE_ROAD_UNKNOWN;
         road_class <= OPENRIDE_ROAD_OTHER;
         ++road_class) {
        if (!table->visible[road_class]) continue;
        const RoadLODClassGroup group = road_lod_class_group(road_class);
        if (road_lod_group_source(group, zoom) != lod_index) {
            table->visible[road_class] = false;
            continue;
        }
        const double fade = road_lod_group_fade(group,
                                                regional_to_overview,
                                                overview_to_local,
                                                local_to_detail);
        if (fade <= 0.001) {
            table->visible[road_class] = false;
            continue;
        }
        ormap_scale_color_alpha(&table->paints[road_class].line, fade);
        ormap_scale_color_alpha(&table->paints[road_class].casing, fade);
    }
}

static void draw_road_pass(OpenRideORMapRenderer *renderer,
                           const OpenRideMapCamera *camera,
                           int width,
                           int height,
                           int zoom,
                           const RoadPaintTable *paint_table,
                           bool casing_pass,
                           bool budgeted_draw_load)
{
    const bool previous_road_debug_active = renderer->road_debug_active;
    renderer->road_debug_active = true;
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
            ++renderer->road_debug.tiles_visited;
            OpenRideORMapRoadCacheEntry *entry =
                road_cache_slot(renderer, zoom, qx, ty, false, budgeted_draw_load);
            if (!entry || entry->tile.count == 0U) continue;
            const double left = width * 0.5 + tx * tile_size - center_x;
            const double top = height * 0.5 + ty * tile_size - center_y;
            if (entry->record_order) {
                for (size_t order_index = 0U;
                     order_index < sizeof(OPENRIDE_ROAD_DRAW_ORDER)
                         / sizeof(OPENRIDE_ROAD_DRAW_ORDER[0]);
                     ++order_index) {
                    const uint8_t road_class =
                        OPENRIDE_ROAD_DRAW_ORDER[order_index];
                    if (!paint_table->visible[road_class]) continue;

                    const OpenRideMapRoadPaint paint =
                        paint_table->paints[road_class];
                    if (casing_pass
                        && !(paint.casing_width > paint.width
                             && paint.casing.a > 0U)) {
                        continue;
                    }

                    const uint32_t first =
                        entry->class_offsets[road_class];
                    const uint32_t end =
                        entry->class_offsets[road_class + 1U];
                    for (uint32_t position = first;
                         position < end;
                         ++position) {
                        const OpenRideORMapRoadRecord *record =
                            &entry->tile.records[entry->record_order[position]];

                        float x1 = (float)(
                            left + ((double)record->x1 / 65535.0) * tile_size);
                        float y1 = (float)(
                            top + ((double)record->y1 / 65535.0) * tile_size);
                        float x2 = (float)(
                            left + ((double)record->x2 / 65535.0) * tile_size);
                        float y2 = (float)(
                            top + ((double)record->y2 / 65535.0) * tile_size);
                        rotate_point(camera, width, height, &x1, &y1);
                        rotate_point(camera, width, height, &x2, &y2);

                        const float clip_margin =
                            (float)(paint.casing_width > paint.width
                                        ? paint.casing_width
                                        : paint.width)
                            + 3.0f;
                        if ((x1 < -clip_margin && x2 < -clip_margin)
                            || (x1 > (float)width + clip_margin
                                && x2 > (float)width + clip_margin)
                            || (y1 < -clip_margin && y2 < -clip_margin)
                            || (y1 > (float)height + clip_margin
                                && y2 > (float)height + clip_margin)) {
                            continue;
                        }

                        ++renderer->road_debug.segments_drawn;

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
                            renderer->road_debug_active = previous_road_debug_active;
                            return;
                        }
                    }
                }
            } else {
                for (uint32_t r = 0U; r < entry->tile.count; ++r) {
                    const OpenRideORMapRoadRecord *record =
                        &entry->tile.records[r];
                    const uint8_t road_class =
                        record->road_class <= OPENRIDE_ROAD_OTHER
                            ? record->road_class
                            : OPENRIDE_ROAD_OTHER;
                    if (!paint_table->visible[road_class]) continue;

                    const OpenRideMapRoadPaint paint =
                        paint_table->paints[road_class];
                    if (casing_pass
                        && !(paint.casing_width > paint.width
                             && paint.casing.a > 0U)) {
                        continue;
                    }

                    float x1 = (float)(
                        left + ((double)record->x1 / 65535.0) * tile_size);
                    float y1 = (float)(
                        top + ((double)record->y1 / 65535.0) * tile_size);
                    float x2 = (float)(
                        left + ((double)record->x2 / 65535.0) * tile_size);
                    float y2 = (float)(
                        top + ((double)record->y2 / 65535.0) * tile_size);
                    rotate_point(camera, width, height, &x1, &y1);
                    rotate_point(camera, width, height, &x2, &y2);

                    ++renderer->road_debug.segments_drawn;

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
                        renderer->road_debug_active = previous_road_debug_active;
                        return;
                    }
                }
            }
        }
    }
    geometry_batch_flush(renderer, &batch);
    renderer->road_debug_active = previous_road_debug_active;
}

static int android_road_data_zoom(const OpenRideORMapMetadata *metadata,
                                  double camera_zoom)
{
    if (!metadata) return OPENRIDE_ORMAP_MIN_ROAD_ZOOM;

    int zoom = (int)floor(camera_zoom);
#ifdef __ANDROID__
    const int min_zoom = metadata->min_zoom;
    if (camera_zoom < 11.75) {
        zoom = min_zoom;
    } else if (camera_zoom < 12.75) {
        zoom = min_zoom + 1;
    } else if (camera_zoom < 13.75) {
        zoom = min_zoom + 2;
    } else if (camera_zoom < 14.50) {
        zoom = min_zoom + 3;
    } else {
        zoom = min_zoom + 4;
    }
#endif

    if (zoom < metadata->min_zoom) zoom = metadata->min_zoom;
    if (zoom > metadata->road_max_zoom) zoom = metadata->road_max_zoom;
    return zoom;
}

static uint32_t prewarm_road_level(OpenRideORMapRenderer *renderer,
                                   const OpenRideMapCamera *camera,
                                   int width,
                                   int height,
                                   int zoom,
                                   double viewport_zoom,
                                   uint32_t budget)
{
    if (!renderer || !camera || budget == 0U) return 0U;
    const int count = 1 << zoom;
    const double scale = pow(2.0, viewport_zoom - zoom);
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

    uint32_t loaded = 0U;
    for (int ty = first_y; ty <= last_y; ++ty) {
        if (ty < 0 || ty >= count) continue;
        for (int tx = first_x; tx <= last_x; ++tx) {
            const int qx = wrap_x(tx, count);
            if (road_cache_contains(renderer, zoom, qx, ty)) continue;
            OpenRideORMapRoadCacheEntry *entry =
                road_cache_slot(renderer, zoom, qx, ty, true, false);
            if (!entry) continue;
            if (++loaded >= budget) return loaded;
        }
    }
    return loaded;
}

static int road_prewarm_target_zoom(OpenRideORMapRenderer *renderer,
                                    double camera_zoom,
                                    double *viewport_zoom)
{
    if (viewport_zoom) *viewport_zoom = -1.0;
    if (!renderer) return -1;
    if (!renderer->road_has_previous_camera_zoom) {
        renderer->road_previous_camera_zoom = camera_zoom;
        renderer->road_has_previous_camera_zoom = true;
        return -1;
    }

    const double delta = camera_zoom - renderer->road_previous_camera_zoom;
    renderer->road_previous_camera_zoom = camera_zoom;
    if (delta > 0.0001) {
        renderer->road_zoom_direction = 1;
    } else if (delta < -0.0001) {
        renderer->road_zoom_direction = -1;
    }

    if (renderer->road_zoom_direction > 0) {
        if (camera_zoom >= 12.80 && camera_zoom < 14.40) {
            if (viewport_zoom) *viewport_zoom = 13.40;
            return OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM;
        }
        if (camera_zoom >= 11.15 && camera_zoom < 12.80) {
            if (viewport_zoom) *viewport_zoom = 11.85;
            return OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM;
        }
        if (camera_zoom >= 9.95 && camera_zoom < 11.25) {
            if (viewport_zoom) *viewport_zoom = 10.55;
            return OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM;
        }
    } else if (renderer->road_zoom_direction < 0) {
        if (camera_zoom <= 15.00 && camera_zoom > 13.40) {
            if (viewport_zoom) *viewport_zoom = 14.40;
            return OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM;
        }
        if (camera_zoom <= 13.40 && camera_zoom > 11.85) {
            if (viewport_zoom) *viewport_zoom = 12.80;
            return OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM;
        }
        if (camera_zoom <= 11.85 && camera_zoom > 10.55) {
            if (viewport_zoom) *viewport_zoom = 11.25;
            return OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM;
        }
    }
    return -1;
}

static void draw_roads_legacy(OpenRideORMapRenderer *renderer,
                              const OpenRideMapCamera *camera,
                              int width,
                              int height,
                              const OpenRideORMapMetadata *metadata)
{
    const int zoom = android_road_data_zoom(metadata, camera->zoom);
    RoadPaintTable paint_table;
    build_road_paint_table(renderer, camera->zoom, 1.0, &paint_table);
    if (road_paint_table_has_casing(&paint_table)) {
        draw_road_pass(renderer,
                       camera,
                       width,
                       height,
                       zoom,
                       &paint_table,
                       true,
                       false);
    }
    draw_road_pass(renderer,
                   camera,
                   width,
                   height,
                   zoom,
                   &paint_table,
                   false,
                   false);
}

static void draw_roads(OpenRideORMapRenderer *renderer,
                       const OpenRideMapCamera *camera,
                       int width,
                       int height)
{
    if (!renderer) return;
    const uint64_t roads_started = SDL_GetTicksNS();
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    if (!metadata) {
        renderer->road_debug.roads_ms =
            (double)(SDL_GetTicksNS() - roads_started) / 1000000.0;
        return;
    }
    if (metadata->format_version < 6) {
        draw_roads_legacy(renderer, camera, width, height, metadata);
        renderer->road_debug.roads_ms =
            (double)(SDL_GetTicksNS() - roads_started) / 1000000.0;
        return;
    }

    double prewarm_view_zoom = -1.0;
    const int prewarm_zoom =
        road_prewarm_target_zoom(renderer, camera->zoom, &prewarm_view_zoom);
    renderer->road_debug.prewarm_zoom = prewarm_zoom;
    if (prewarm_zoom >= 0) {
        (void)prewarm_road_level(renderer,
                                 camera,
                                 width,
                                 height,
                                 prewarm_zoom,
                                 prewarm_view_zoom,
                                 ORMAP_ROAD_PREWARM_TILE_BUDGET);
    }

    const double regional_to_overview =
        ormap_zoom_smoothstep(camera->zoom, 10.55, 11.25);
    const double overview_to_local =
        ormap_zoom_smoothstep(camera->zoom, 11.85, 12.80);
    const double local_to_detail =
        ormap_zoom_smoothstep(camera->zoom, 13.40, 14.40);

    const int zooms[4] = {
        OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM,
        OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM,
        OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM,
        OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM
    };
    RoadPaintTable tables[4];
    bool active[4] = {false, false, false, false};
    for (int i = 0; i < 4; ++i) {
        build_road_paint_table(renderer, camera->zoom, 1.0, &tables[i]);
        road_paint_table_configure_lod(&tables[i],
                                       i,
                                       camera->zoom,
                                       regional_to_overview,
                                       overview_to_local,
                                       local_to_detail);
        for (int road_class = OPENRIDE_ROAD_UNKNOWN;
             road_class <= OPENRIDE_ROAD_OTHER;
             ++road_class) {
            if (tables[i].visible[road_class]) {
                active[i] = true;
                break;
            }
        }
    }

    /*
     * Each road class has exactly one geometry owner at any camera zoom.
     * A handoff therefore draws the stable/common hierarchy once and only
     * fades in classes newly introduced by the next semantic LOD. The source
     * for shared classes changes only after that handoff is complete; road
     * LOD V1 does no geometric simplification, so this avoids duplicate
     * rasterization without introducing a visible opacity dip.
     */
    for (int pass = 0; pass < 2; ++pass) {
        const bool casing = pass == 0;
        for (int i = 0; i < 4; ++i) {
            if (!active[i]) continue;
            if (casing && !road_paint_table_has_casing(&tables[i])) continue;
            draw_road_pass(renderer,
                           camera,
                           width,
                           height,
                           zooms[i],
                           &tables[i],
                           casing,
                           true);
        }
    }
    renderer->road_debug.roads_ms =
        (double)(SDL_GetTicksNS() - roads_started) / 1000000.0;
    g_last_road_debug_stats = renderer->road_debug;
    g_last_road_debug_renderer = renderer;
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

static bool label_is_region_reference(const OpenRideORMapLabel *labels,
                                      uint32_t count,
                                      uint32_t label_index)
{
    if (!labels || label_index >= count) return false;

    uint32_t selected = 0U;
    for (int pass = 0; pass < 3 && selected < 3U; ++pass) {
        const uint8_t wanted_kind =
            pass == 0
                ? OPENRIDE_PLACE_CITY
                : (pass == 1 ? OPENRIDE_PLACE_TOWN
                             : OPENRIDE_PLACE_VILLAGE);

        for (uint32_t i = 0U; i < count && selected < 3U; ++i) {
            if (labels[i].kind != wanted_kind || labels[i].name[0] == '\0') {
                continue;
            }
            if (i == label_index) return true;
            ++selected;
        }
    }

    return false;
}

static double label_fade_start_zoom(const char *kind)
{
    if (!kind || kind[0] == '\0') return 13.0;
    if (strcmp(kind, "capital") == 0) return 4.0;
    if (strcmp(kind, "city") == 0) return 8.5;
    if (strcmp(kind, "town") == 0) return 10.5;
    if (strcmp(kind, "village") == 0) return 12.75;
    if (strcmp(kind, "suburb") == 0) return 12.75;
    if (strcmp(kind, "borough") == 0) return 12.75;
    if (strcmp(kind, "hamlet") == 0) return 13.5;
    if (strcmp(kind, "quarter") == 0) return 13.75;
    if (strcmp(kind, "neighbourhood") == 0) return 13.75;
    if (strcmp(kind, "locality") == 0) return 14.0;
    if (strcmp(kind, "isolated_dwelling") == 0) return 14.0;
    return 13.5;
}

static double label_fade_factor(const char *kind, double zoom)
{
    const double start = label_fade_start_zoom(kind);
    return ormap_zoom_smoothstep(zoom, start, start + 0.65);
}

static double label_lod_fade(const OpenRideORMapLabel *label, double zoom)
{
    if (!label) return 0.0;
    switch ((OpenRideORMapLabelLOD)label->lod) {
        case OPENRIDE_ORMAP_LABEL_LOD_REGIONAL:
            return ormap_zoom_smoothstep(zoom, 10.0, 10.55);
        case OPENRIDE_ORMAP_LABEL_LOD_OVERVIEW:
            return ormap_zoom_smoothstep(zoom, 10.55, 11.25);
        case OPENRIDE_ORMAP_LABEL_LOD_LOCAL:
            return ormap_zoom_smoothstep(zoom, 11.85, 12.80);
        case OPENRIDE_ORMAP_LABEL_LOD_DETAIL:
        default:
            return ormap_zoom_smoothstep(zoom, 13.40, 14.10);
    }
}

static OpenRidePointD label_world_to_screen(OpenRidePointD world,
                                                OpenRidePointD center,
                                                double world_size,
                                                double bearing_cos,
                                                double bearing_sin,
                                                int width,
                                                int height)
{
    double dx = world.x - center.x;
    if (dx > 0.5) dx -= 1.0;
    if (dx < -0.5) dx += 1.0;

    const double dy = world.y - center.y;
    const double world_dx = dx * world_size;
    const double world_dy = dy * world_size;

    const double screen_dx =
        world_dx * bearing_cos + world_dy * bearing_sin;
    const double screen_dy =
        -world_dx * bearing_sin + world_dy * bearing_cos;

    return (OpenRidePointD){
        (double)width * 0.5 + screen_dx,
        (double)height * 0.5 + screen_dy
    };
}

static void draw_labels(OpenRideORMapRenderer *renderer,
                        const OpenRideMapCamera *camera,
                        int width,
                        int height)
{
    uint32_t count = 0U;
    const OpenRideORMapLabel *labels = openride_ormap_labels(renderer->map, &count);
    if (!labels || count == 0U) return;
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);

    const OpenRidePointD center =
        openride_mercator_forward(camera->center_lat, camera->center_lon);
    const double world_size = openride_world_size_pixels(camera->zoom);
    const double angle =
        camera->bearing_deg * 3.14159265358979323846 / 180.0;
    const double bearing_cos = cos(angle);
    const double bearing_sin = sin(angle);
    const bool cached_positions =
        renderer->label_world_positions
        && renderer->label_world_position_count == count;

    LabelBox boxes[ORMAP_LABEL_BOX_MAX];
    uint32_t box_count = 0U;
    const OpenRideMapPalette palette = openride_map_palette(renderer->style);

    SDL_BlendMode previous_blend_mode = SDL_BLENDMODE_NONE;
    const bool have_previous_blend_mode =
        SDL_GetRenderDrawBlendMode(renderer->renderer, &previous_blend_mode);
    SDL_SetRenderDrawBlendMode(renderer->renderer, SDL_BLENDMODE_BLEND);

    /*
     * Some labels (notably city/town) have a semantic visibility threshold
     * below the Android detail-renderer handoff. Without a global handoff
     * fade, those labels arrive already at full opacity the first frame the
     * detailed renderer becomes active.
     */
    const double detail_label_fade =
        ormap_detail_handoff_fade(camera->zoom);

    for (uint32_t i = 0U; i < count && box_count < ORMAP_LABEL_BOX_MAX; ++i) {
        const OpenRideORMapLabel *label = &labels[i];
        const char *kind = label_kind_name(label->kind);
        if (!openride_map_place_label_visible(kind, 0, camera->zoom)) continue;

        const OpenRidePointD world = cached_positions
            ? renderer->label_world_positions[i]
            : openride_mercator_forward(label->lat_e7 / 10000000.0,
                                        label->lon_e7 / 10000000.0);
        const OpenRidePointD p = label_world_to_screen(world,
                                                       center,
                                                       world_size,
                                                       bearing_cos,
                                                       bearing_sin,
                                                       width,
                                                       height);

        if (p.x < -100.0
            || p.x > width + 100.0
            || p.y < -40.0
            || p.y > height + 40.0) {
            continue;
        }

        const float text_w = (float)strlen(label->name) * 8.0f;
        LabelBox box = {
            .left = (float)p.x - text_w * 0.5f - 4.0f,
            .top = (float)p.y - 7.0f,
            .right = (float)p.x + text_w * 0.5f + 4.0f,
            .bottom = (float)p.y + 10.0f
        };

        bool collision = false;
        for (uint32_t b = 0U; b < box_count; ++b) {
            if (boxes_overlap(box, boxes[b])) {
                collision = true;
                break;
            }
        }
        if (collision) continue;

        const bool persistent_region_reference =
            label_is_region_reference(labels, count, i);
        const double label_fade = metadata && metadata->format_version >= 6
            ? detail_label_fade * label_lod_fade(label, camera->zoom)
            : (persistent_region_reference
                ? detail_label_fade
                : detail_label_fade * label_fade_factor(kind, camera->zoom));
        if (label_fade <= 0.0) continue;

        OpenRideMapColor label_halo = palette.label_halo;
        OpenRideMapColor label_color = palette.label;
        ormap_scale_color_alpha(&label_halo, label_fade);
        ormap_scale_color_alpha(&label_color, label_fade);

        boxes[box_count++] = box;
        set_color(renderer->renderer, label_halo);
        SDL_RenderDebugText(renderer->renderer,
                            (float)p.x - text_w * 0.5f + 1.0f,
                            (float)p.y + 1.0f,
                            label->name);
        set_color(renderer->renderer, label_color);
        SDL_RenderDebugText(renderer->renderer,
                            (float)p.x - text_w * 0.5f,
                            (float)p.y,
                            label->name);
    }

    SDL_SetRenderDrawBlendMode(
        renderer->renderer,
        have_previous_blend_mode
            ? previous_blend_mode
            : SDL_BLENDMODE_NONE);
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

    uint32_t label_count = 0U;
    const OpenRideORMapLabel *labels =
        openride_ormap_labels(map, &label_count);
    if (labels && label_count > 0U) {
        renderer->label_world_positions =
            malloc((size_t)label_count
                   * sizeof(*renderer->label_world_positions));
        if (renderer->label_world_positions) {
            renderer->label_world_position_count = label_count;
            for (uint32_t i = 0U; i < label_count; ++i) {
                renderer->label_world_positions[i] =
                    openride_mercator_forward(
                        labels[i].lat_e7 / 10000000.0,
                        labels[i].lon_e7 / 10000000.0);
            }
        }
    }

    return true;
}

void openride_ormap_renderer_destroy(OpenRideORMapRenderer *renderer)
{
    if (!renderer) return;
    for (size_t i = 0U; i < OPENRIDE_ORMAP_ROAD_CACHE_CAPACITY; ++i) {
        road_cache_entry_destroy(&renderer->roads[i]);
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
    free(renderer->label_world_positions);
    free(renderer->area_vertices);
    free(renderer->area_indices);
    memset(renderer, 0, sizeof(*renderer));
}

void openride_ormap_renderer_set_style(OpenRideORMapRenderer *renderer,
                                       OpenRideMapStyle style)
{
    if (renderer) renderer->style = style;
}

void openride_ormap_renderer_begin_frame(OpenRideORMapRenderer *renderer)
{
    if (!renderer) return;
    ++renderer->frame_counter;
    memset(&renderer->road_debug, 0, sizeof(renderer->road_debug));
    memset(&renderer->area_debug, 0, sizeof(renderer->area_debug));
    renderer->road_debug.prewarm_zoom = -1;
    renderer->road_draw_load_budget_remaining = ORMAP_ROAD_DRAW_LOAD_BUDGET;
    renderer->road_debug_active = false;
}

void openride_ormap_renderer_get_road_debug_stats(
    const OpenRideORMapRenderer *renderer,
    OpenRideORMapRoadDebugStats *stats)
{
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
    stats->prewarm_zoom = -1;
    if (renderer && renderer == g_last_road_debug_renderer) {
        *stats = renderer->road_debug;
    } else if (g_last_road_debug_renderer) {
        /* MapWorld owns the renderer actually used for installed regions.
         * The DEV HUD asks the standalone renderer, so expose the most
         * recently drawn ORMap road pass as a diagnostic fallback. */
        *stats = g_last_road_debug_stats;
    } else if (renderer) {
        *stats = renderer->road_debug;
    }
}

void openride_ormap_renderer_get_area_debug_stats(
    const OpenRideORMapRenderer *renderer,
    OpenRideORMapAreaDebugStats *stats)
{
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
    if (renderer && renderer == g_last_area_debug_renderer) {
        *stats = renderer->area_debug;
    } else if (g_last_area_debug_renderer) {
        *stats = g_last_area_debug_stats;
    } else if (renderer) {
        *stats = renderer->area_debug;
    }
}

void openride_ormap_renderer_draw_layer(OpenRideORMapRenderer *renderer,
                                        const OpenRideMapCamera *camera,
                                        int viewport_width,
                                        int viewport_height,
                                        OpenRideORMapRenderLayer layer)
{
    if (!renderer || !camera || viewport_width <= 0 || viewport_height <= 0) return;

    switch (layer) {
        case OPENRIDE_ORMAP_RENDER_LAYER_MASKS:
            draw_masks(renderer, camera, viewport_width, viewport_height);
            break;
        case OPENRIDE_ORMAP_RENDER_LAYER_AREAS:
            draw_areas(renderer, camera, viewport_width, viewport_height);
            break;
        case OPENRIDE_ORMAP_RENDER_LAYER_WATERWAYS:
            draw_waterways(renderer, camera, viewport_width, viewport_height);
            break;
        case OPENRIDE_ORMAP_RENDER_LAYER_ROADS:
            draw_roads(renderer, camera, viewport_width, viewport_height);
            break;
        case OPENRIDE_ORMAP_RENDER_LAYER_LABELS:
            draw_labels(renderer, camera, viewport_width, viewport_height);
            break;
        default:
            break;
    }
}

void openride_ormap_renderer_draw(OpenRideORMapRenderer *renderer,
                                  const OpenRideMapCamera *camera,
                                  int viewport_width,
                                  int viewport_height)
{
    if (!renderer || !camera || viewport_width <= 0 || viewport_height <= 0) return;

    openride_ormap_renderer_begin_frame(renderer);

    const OpenRideMapPalette palette = openride_map_palette(renderer->style);
    set_color(renderer->renderer, palette.background);
    SDL_RenderClear(renderer->renderer);

    for (int layer = OPENRIDE_ORMAP_RENDER_LAYER_MASKS;
         layer <= OPENRIDE_ORMAP_RENDER_LAYER_LABELS;
         ++layer) {
        openride_ormap_renderer_draw_layer(renderer,
                                           camera,
                                           viewport_width,
                                           viewport_height,
                                           (OpenRideORMapRenderLayer)layer);
    }
}
