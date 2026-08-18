#include "map/ormap_pyramid_overlay_renderer.h"

#include "map/dashed_line.h"

#include "openride/ormap_pyramid_overlay.h"
#include "openride/place_search.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OVERLAY_TILE_SIZE 256.0
#define OVERLAY_CACHE_CAPACITY 768U
#define OVERLAY_CACHE_ASSOCIATIVITY 32U
#define OVERLAY_ROAD_LOAD_BUDGET 10U
#define OVERLAY_WATER_LOAD_BUDGET 6U
#define OVERLAY_GEOMETRY_VERTEX_LIMIT 16384U
#define OVERLAY_GEOMETRY_INDEX_LIMIT 24576U
#define OVERLAY_LABEL_BOX_MAX 64U

typedef struct OverlayBatch {
    uint32_t vertex_count;
    uint32_t index_count;
} OverlayBatch;

typedef struct OverlayDashSegment {
    float x1;
    float y1;
    float x2;
    float y2;
    float length;
    uint64_t endpoint_key[2];
    uint32_t node_index[2];
    int32_t next_endpoint[2];
    bool visible;
    bool drawn;
} OverlayDashSegment;

typedef struct OverlayDashNode {
    uint64_t key;
    int32_t first_endpoint;
    uint32_t degree;
    bool occupied;
} OverlayDashNode;

typedef struct OverlayVisibleRange {
    int first_x;
    int last_x;
    int first_y;
    int last_y;
    bool valid;
} OverlayVisibleRange;

typedef struct OverlayLevelTransform {
    double tile_size;
    double quantized_scale;
    double center_x;
    double center_y;
    double viewport_center_x;
    double viewport_center_y;
    double rotation_cos;
    double rotation_sin;
    bool rotated;
} OverlayLevelTransform;

typedef struct OverlayCacheEntry {
    bool occupied;
    OpenRideORMapPyramidOverlayLayer layer;
    int zoom;
    int x;
    int y;
    uint64_t last_used;
    OpenRideORMapPyramidOverlayLineTile tile;
    uint32_t class_offsets[OPENRIDE_ROAD_OTHER + 2U];
} OverlayCacheEntry;

typedef struct OverlayTargetState {
    SDL_Texture *previous_target;
    SDL_BlendMode previous_blend;
    Uint8 previous_r;
    Uint8 previous_g;
    Uint8 previous_b;
    Uint8 previous_a;
    bool have_previous_blend;
    bool have_previous_color;
} OverlayTargetState;

typedef struct OverlayLabelBox {
    float left;
    float top;
    float right;
    float bottom;
} OverlayLabelBox;

struct OpenRideORMapPyramidOverlayRenderer {
    SDL_Renderer *renderer;
    OpenRideORMapPyramidOverlayMap *map;
    OpenRideMapStyle style;

    OverlayCacheEntry cache[OVERLAY_CACHE_CAPACITY];

    SDL_Vertex *vertices;
    int *indices;
    uint32_t vertex_capacity;
    uint32_t index_capacity;

    OverlayDashSegment *dash_segments;
    uint32_t dash_segment_count;
    uint32_t dash_segment_capacity;
    OverlayDashNode *dash_nodes;
    uint32_t dash_node_capacity;

    SDL_Texture *layer_compositor;
    int layer_compositor_width;
    int layer_compositor_height;
    SDL_BlendMode premult_blend;

    OpenRidePointD *label_world_positions;
    uint32_t label_world_position_count;

    uint64_t frame_counter;
    uint32_t road_load_budget;
    uint32_t water_load_budget;
    bool needs_followup;
    bool healthy;
    bool road_debug_active;

    OpenRideORMapRoadDebugStats road_debug;
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
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

static void scale_color_alpha(OpenRideMapColor *color, double factor)
{
    if (!color) return;
    color->a = scaled_alpha(color->a, factor);
}

static OverlayVisibleRange compute_visible_range(
    const OpenRideMapCamera *camera,
    int width,
    int height,
    int zoom)
{
    OverlayVisibleRange range = {0};
    if (!camera || zoom < 0 || zoom >= 30) return range;

    const double margin = 96.0;
    const double sx[4] = {-margin, width + margin, width + margin, -margin};
    const double sy[4] = {-margin, -margin, height + margin, height + margin};
    double min_x = 1e30, min_y = 1e30;
    double max_x = -1e30, max_y = -1e30;

    for (int i = 0; i < 4; ++i) {
        double lat = 0.0, lon = 0.0;
        openride_screen_to_geo(camera,
                               sx[i], sy[i],
                               width, height,
                               &lat, &lon);
        const OpenRidePointD world = openride_mercator_forward(lat, lon);
        if (world.x < min_x) min_x = world.x;
        if (world.x > max_x) max_x = world.x;
        if (world.y < min_y) min_y = world.y;
        if (world.y > max_y) max_y = world.y;
    }

    if (max_x - min_x > 0.5) return range;

    const int count = 1 << zoom;
    int first_x = (int)floor(min_x * count) - 1;
    int last_x = (int)floor(max_x * count) + 1;
    int first_y = (int)floor(min_y * count) - 1;
    int last_y = (int)floor(max_y * count) + 1;
    if (first_x < 0) first_x = 0;
    if (first_y < 0) first_y = 0;
    if (last_x >= count) last_x = count - 1;
    if (last_y >= count) last_y = count - 1;
    range = (OverlayVisibleRange){
        .first_x = first_x,
        .last_x = last_x,
        .first_y = first_y,
        .last_y = last_y,
        .valid = first_x <= last_x && first_y <= last_y
    };
    return range;
}

static uint32_t cache_hash(OpenRideORMapPyramidOverlayLayer layer,
                           int zoom,
                           int x,
                           int y)
{
    uint32_t value = (uint32_t)layer * UINT32_C(0x27d4eb2d);
    value ^= (uint32_t)zoom * UINT32_C(0x9e3779b9);
    value ^= (uint32_t)x * UINT32_C(0x85ebca6b);
    value ^= (uint32_t)y * UINT32_C(0xc2b2ae35);
    value ^= value >> 16U;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15U;
    return value;
}

static size_t cache_set_base(OpenRideORMapPyramidOverlayLayer layer,
                             int zoom,
                             int x,
                             int y)
{
    const size_t sets = OVERLAY_CACHE_CAPACITY / OVERLAY_CACHE_ASSOCIATIVITY;
    return (size_t)(cache_hash(layer, zoom, x, y) % (uint32_t)sets)
        * OVERLAY_CACHE_ASSOCIATIVITY;
}

static void cache_entry_destroy(OverlayCacheEntry *entry)
{
    if (!entry) return;
    if (entry->occupied) openride_ormap_pyramid_overlay_tile_destroy(&entry->tile);
    memset(entry, 0, sizeof(*entry));
}

static OverlayCacheEntry *cache_lookup(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    OpenRideORMapPyramidOverlayLayer layer,
    int zoom,
    int x,
    int y,
    bool count_debug)
{
    const size_t base = cache_set_base(layer, zoom, x, y);
    for (size_t way = 0U; way < OVERLAY_CACHE_ASSOCIATIVITY; ++way) {
        OverlayCacheEntry *entry = &renderer->cache[base + way];
        if (entry->occupied
            && entry->layer == layer
            && entry->zoom == zoom
            && entry->x == x
            && entry->y == y) {
            entry->last_used = renderer->frame_counter;
            if (count_debug) ++renderer->road_debug.cache_hits;
            return entry;
        }
    }
    if (count_debug) ++renderer->road_debug.cache_misses;
    return NULL;
}

static OverlayCacheEntry *cache_victim(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    OpenRideORMapPyramidOverlayLayer layer,
    int zoom,
    int x,
    int y)
{
    const size_t base = cache_set_base(layer, zoom, x, y);
    OverlayCacheEntry *victim = NULL;
    for (size_t way = 0U; way < OVERLAY_CACHE_ASSOCIATIVITY; ++way) {
        OverlayCacheEntry *entry = &renderer->cache[base + way];
        if (!entry->occupied) return entry;
        if (entry->last_used == renderer->frame_counter) continue;
        if (!victim || entry->last_used < victim->last_used) victim = entry;
    }
    if (victim) cache_entry_destroy(victim);
    return victim;
}

static void build_class_offsets(OverlayCacheEntry *entry)
{
    memset(entry->class_offsets, 0, sizeof(entry->class_offsets));
    if (entry->layer != OPENRIDE_ORMAP_PYRAMID_OVERLAY_ROAD
        || entry->tile.count == 0U) return;

    uint32_t position = 0U;
    for (uint32_t road_class = 0U;
         road_class <= OPENRIDE_ROAD_OTHER;
         ++road_class) {
        entry->class_offsets[road_class] = position;
        while (position < entry->tile.count
               && entry->tile.records[position].kind == road_class) {
            ++position;
        }
    }
    entry->class_offsets[OPENRIDE_ROAD_OTHER + 1U] = position;
}

static bool cache_load(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    OpenRideORMapPyramidOverlayLayer layer,
    int zoom,
    int x,
    int y)
{
    const bool road_debug =
        layer == OPENRIDE_ORMAP_PYRAMID_OVERLAY_ROAD;
    uint32_t *budget = layer == OPENRIDE_ORMAP_PYRAMID_OVERLAY_ROAD
        ? &renderer->road_load_budget
        : &renderer->water_load_budget;
    if (*budget == 0U) {
        renderer->needs_followup = true;
        if (road_debug) ++renderer->road_debug.deferred_loads;
        return false;
    }
    OverlayCacheEntry *entry = cache_victim(renderer, layer, zoom, x, y);
    if (!entry) {
        renderer->needs_followup = true;
        if (road_debug) ++renderer->road_debug.deferred_loads;
        return false;
    }
    --*budget;

    *entry = (OverlayCacheEntry){
        .occupied = true,
        .layer = layer,
        .zoom = zoom,
        .x = x,
        .y = y,
        .last_used = renderer->frame_counter
    };

    const uint64_t started = SDL_GetTicksNS();
    char error[192] = {0};
    if (!openride_ormap_pyramid_overlay_load_tile(
            renderer->map,
            layer,
            zoom,
            x,
            y,
            &entry->tile,
            error,
            sizeof(error))) {
        SDL_Log("OpenRide v11 overlay tile failure z%d/%d/%d: %s",
                zoom, x, y,
                error[0] ? error : "unknown");
        cache_entry_destroy(entry);
        renderer->healthy = false;
        return false;
    }

    if (road_debug) {
        renderer->road_debug.load_ms +=
            (double)(SDL_GetTicksNS() - started) / 1000000.0;
        ++renderer->road_debug.draw_loads;
    }
    build_class_offsets(entry);
    renderer->needs_followup = true;
    return true;
}

static bool prepare_visible_level(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    const OpenRideMapCamera *camera,
    int width,
    int height,
    OpenRideORMapPyramidOverlayLayer layer,
    int zoom,
    OverlayVisibleRange *range_out)
{
    if (!renderer || !camera || !range_out) return false;
    *range_out = compute_visible_range(camera, width, height, zoom);
    if (!range_out->valid) return false;

    const bool count_debug =
        layer == OPENRIDE_ORMAP_PYRAMID_OVERLAY_ROAD;
    for (int y = range_out->first_y; y <= range_out->last_y; ++y) {
        for (int x = range_out->first_x; x <= range_out->last_x; ++x) {
            OverlayCacheEntry *entry = cache_lookup(
                renderer, layer, zoom, x, y, count_debug);
            if (!entry && !cache_load(renderer, layer, zoom, x, y)) {
                return false;
            }
        }
    }

    for (int y = range_out->first_y; y <= range_out->last_y; ++y) {
        for (int x = range_out->first_x; x <= range_out->last_x; ++x) {
            if (!cache_lookup(renderer, layer, zoom, x, y, false)) {
                renderer->needs_followup = true;
                return false;
            }
        }
    }
    return true;
}

static bool ensure_geometry(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    uint32_t vertices,
    uint32_t indices)
{
    if (vertices > renderer->vertex_capacity) {
        uint32_t capacity = renderer->vertex_capacity ? renderer->vertex_capacity : 4096U;
        while (capacity < vertices) {
            if (capacity > UINT32_MAX / 2U) {
                capacity = vertices;
                break;
            }
            capacity *= 2U;
        }
        SDL_Vertex *grown = realloc(
            renderer->vertices, (size_t)capacity * sizeof(*grown));
        if (!grown) return false;
        renderer->vertices = grown;
        renderer->vertex_capacity = capacity;
    }

    if (indices > renderer->index_capacity) {
        uint32_t capacity = renderer->index_capacity ? renderer->index_capacity : 6144U;
        while (capacity < indices) {
            if (capacity > UINT32_MAX / 2U) {
                capacity = indices;
                break;
            }
            capacity *= 2U;
        }
        int *grown = realloc(renderer->indices, (size_t)capacity * sizeof(*grown));
        if (!grown) return false;
        renderer->indices = grown;
        renderer->index_capacity = capacity;
    }
    return true;
}

static bool flush_geometry(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    OverlayBatch *batch)
{
    if (!renderer || !batch
        || batch->vertex_count == 0U
        || batch->index_count == 0U) return true;

    const bool ok = SDL_RenderGeometry(renderer->renderer,
                                       NULL,
                                       renderer->vertices,
                                       (int)batch->vertex_count,
                                       renderer->indices,
                                       (int)batch->index_count);
    if (ok && renderer->road_debug_active) ++renderer->road_debug.batches;
    batch->vertex_count = 0U;
    batch->index_count = 0U;
    return ok;
}

static bool reserve_geometry(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    OverlayBatch *batch,
    uint32_t add_vertices,
    uint32_t add_indices)
{
    if (batch->vertex_count > OVERLAY_GEOMETRY_VERTEX_LIMIT - add_vertices
        || batch->index_count > OVERLAY_GEOMETRY_INDEX_LIMIT - add_indices) {
        if (!flush_geometry(renderer, batch)) return false;
    }
    return ensure_geometry(renderer,
                           batch->vertex_count + add_vertices,
                           batch->index_count + add_indices);
}

static void set_vertex(SDL_Vertex *vertex,
                       float x,
                       float y,
                       OpenRideMapColor color)
{
    vertex->position.x = x;
    vertex->position.y = y;
    vertex->color.r = (float)color.r / 255.0f;
    vertex->color.g = (float)color.g / 255.0f;
    vertex->color.b = (float)color.b / 255.0f;
    vertex->color.a = (float)color.a / 255.0f;
    vertex->tex_coord.x = 0.0f;
    vertex->tex_coord.y = 0.0f;
}

static bool draw_line(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    OverlayBatch *batch,
    float x1,
    float y1,
    float x2,
    float y2,
    int width,
    OpenRideMapColor color)
{
    if (width <= 0 || color.a == 0U) return true;
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.001f) return true;
    if (!reserve_geometry(renderer, batch, 4U, 6U)) return false;

    const float half = 0.5f * (float)width;
    const float nx = -dy / length * half;
    const float ny = dx / length * half;
    const float xs[4] = {x1 + nx, x2 + nx, x2 - nx, x1 - nx};
    const float ys[4] = {y1 + ny, y2 + ny, y2 - ny, y1 - ny};
    const uint32_t base_v = batch->vertex_count;
    const uint32_t base_i = batch->index_count;

    for (uint32_t i = 0U; i < 4U; ++i) {
        set_vertex(&renderer->vertices[base_v + i], xs[i], ys[i], color);
    }
    const int base = (int)base_v;
    renderer->indices[base_i + 0U] = base + 0;
    renderer->indices[base_i + 1U] = base + 1;
    renderer->indices[base_i + 2U] = base + 2;
    renderer->indices[base_i + 3U] = base + 0;
    renderer->indices[base_i + 4U] = base + 2;
    renderer->indices[base_i + 5U] = base + 3;
    batch->vertex_count += 4U;
    batch->index_count += 6U;
    return true;
}

static bool draw_dashed_line(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    OverlayBatch *batch,
    float x1,
    float y1,
    float x2,
    float y2,
    int width,
    OpenRideMapColor color,
    float phase)
{
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.001f) return true;
    const float dash = 8.0f;
    const float gap = 5.0f;
    const float period = dash + gap;
    phase = openride_dashed_line_normalize_phase(phase, period);
    for (float position = -phase; position < length; position += period) {
        const float start = fmaxf(position, 0.0f);
        const float end = fminf(position + dash, length);
        if (end <= start) continue;
        const float t1 = start / length;
        const float t2 = end / length;
        if (!draw_line(renderer, batch,
                       x1 + dx * t1, y1 + dy * t1,
                       x1 + dx * t2, y1 + dy * t2,
                       width, color)) return false;
    }
    return true;
}

static bool ensure_dash_segment_capacity(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    uint32_t needed)
{
    if (needed <= renderer->dash_segment_capacity) return true;
    uint32_t capacity = renderer->dash_segment_capacity
        ? renderer->dash_segment_capacity
        : 1024U;
    while (capacity < needed) {
        if (capacity > UINT32_MAX / 2U) return false;
        capacity *= 2U;
    }
    OverlayDashSegment *grown = realloc(
        renderer->dash_segments,
        (size_t)capacity * sizeof(*grown));
    if (!grown) return false;
    renderer->dash_segments = grown;
    renderer->dash_segment_capacity = capacity;
    return true;
}

static uint32_t dash_node_hash(uint64_t key)
{
    key ^= key >> 33U;
    key *= UINT64_C(0xff51afd7ed558ccd);
    key ^= key >> 33U;
    return (uint32_t)key ^ (uint32_t)(key >> 32U);
}

static bool prepare_dash_nodes(
    OpenRideORMapPyramidOverlayRenderer *renderer)
{
    if (renderer->dash_segment_count > (uint32_t)INT32_MAX / 2U) return false;
    uint32_t needed = 1024U;
    while ((uint64_t)needed
           < (uint64_t)renderer->dash_segment_count * 4U) {
        if (needed > UINT32_MAX / 2U) return false;
        needed *= 2U;
    }
    if (needed > renderer->dash_node_capacity) {
        OverlayDashNode *grown = realloc(
            renderer->dash_nodes,
            (size_t)needed * sizeof(*grown));
        if (!grown) return false;
        renderer->dash_nodes = grown;
        renderer->dash_node_capacity = needed;
    }
    memset(renderer->dash_nodes,
           0,
           (size_t)renderer->dash_node_capacity * sizeof(*renderer->dash_nodes));
    return true;
}

static uint32_t dash_node_get(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    uint64_t key)
{
    const uint32_t mask = renderer->dash_node_capacity - 1U;
    uint32_t index = dash_node_hash(key) & mask;
    for (;;) {
        OverlayDashNode *node = &renderer->dash_nodes[index];
        if (!node->occupied) {
            *node = (OverlayDashNode){
                .key = key,
                .first_endpoint = -1,
                .occupied = true
            };
            return index;
        }
        if (node->key == key) return index;
        index = (index + 1U) & mask;
    }
}

static bool ensure_layer_compositor(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    int width,
    int height)
{
    if (renderer->layer_compositor
        && renderer->layer_compositor_width == width
        && renderer->layer_compositor_height == height) return true;

    if (renderer->layer_compositor) {
        SDL_DestroyTexture(renderer->layer_compositor);
        renderer->layer_compositor = NULL;
    }

    renderer->layer_compositor = SDL_CreateTexture(
        renderer->renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        width,
        height);
    if (!renderer->layer_compositor) return false;
    if (!SDL_SetTextureBlendMode(
            renderer->layer_compositor,
            renderer->premult_blend)) {
        SDL_DestroyTexture(renderer->layer_compositor);
        renderer->layer_compositor = NULL;
        return false;
    }
    renderer->layer_compositor_width = width;
    renderer->layer_compositor_height = height;
    return true;
}

static bool layer_begin(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    int width,
    int height,
    OverlayTargetState *state)
{
    if (!ensure_layer_compositor(renderer, width, height)) return false;
    memset(state, 0, sizeof(*state));
    state->previous_target = SDL_GetRenderTarget(renderer->renderer);
    state->have_previous_blend = SDL_GetRenderDrawBlendMode(
        renderer->renderer, &state->previous_blend);
    state->have_previous_color = SDL_GetRenderDrawColor(
        renderer->renderer,
        &state->previous_r, &state->previous_g,
        &state->previous_b, &state->previous_a);

    bool ok = SDL_SetRenderTarget(renderer->renderer, renderer->layer_compositor);
    if (ok) ok = SDL_SetRenderDrawBlendMode(renderer->renderer, SDL_BLENDMODE_NONE);
    if (ok) ok = SDL_SetRenderDrawColor(renderer->renderer, 0U, 0U, 0U, 0U);
    if (ok) ok = SDL_RenderClear(renderer->renderer);
    if (ok) ok = SDL_SetRenderDrawBlendMode(renderer->renderer, SDL_BLENDMODE_BLEND);

    if (!ok) {
        SDL_SetRenderTarget(renderer->renderer, state->previous_target);
        if (state->have_previous_blend) {
            SDL_SetRenderDrawBlendMode(renderer->renderer, state->previous_blend);
        }
        if (state->have_previous_color) {
            SDL_SetRenderDrawColor(renderer->renderer,
                                   state->previous_r, state->previous_g,
                                   state->previous_b, state->previous_a);
        }
    }
    return ok;
}

static bool layer_end(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    const OverlayTargetState *state,
    bool present)
{
    bool ok = SDL_SetRenderTarget(renderer->renderer, state->previous_target);
    if (state->have_previous_blend) {
        ok = SDL_SetRenderDrawBlendMode(renderer->renderer, state->previous_blend) && ok;
    }
    if (state->have_previous_color) {
        ok = SDL_SetRenderDrawColor(renderer->renderer,
                                    state->previous_r, state->previous_g,
                                    state->previous_b, state->previous_a) && ok;
    }
    if (ok && present) {
        ok = SDL_SetTextureBlendMode(
            renderer->layer_compositor,
            renderer->premult_blend);
    }
    if (ok && present) {
        ok = SDL_RenderTexture(
            renderer->renderer,
            renderer->layer_compositor,
            NULL,
            NULL);
    }
    return ok;
}

static void rotate_point(const OverlayLevelTransform *transform,
                         float *x,
                         float *y)
{
    if (!transform->rotated) return;
    const double dx = *x - transform->viewport_center_x;
    const double dy = *y - transform->viewport_center_y;
    *x = (float)(transform->viewport_center_x
        + dx * transform->rotation_cos
        + dy * transform->rotation_sin);
    *y = (float)(transform->viewport_center_y
        - dx * transform->rotation_sin
        + dy * transform->rotation_cos);
}

static OverlayLevelTransform make_level_transform(
    const OpenRideMapCamera *camera,
    int width,
    int height,
    int zoom)
{
    const int count = 1 << zoom;
    const double tile_size = OVERLAY_TILE_SIZE * pow(2.0, camera->zoom - zoom);
    const OpenRidePointD center = openride_mercator_forward(
        camera->center_lat, camera->center_lon);
    const double angle = camera->bearing_deg
        * 3.14159265358979323846 / 180.0;
    const bool rotated = fabs(camera->bearing_deg) >= 1e-12;
    return (OverlayLevelTransform){
        .tile_size = tile_size,
        .quantized_scale = tile_size / 65535.0,
        .center_x = center.x * tile_size * count,
        .center_y = center.y * tile_size * count,
        .viewport_center_x = width * 0.5,
        .viewport_center_y = height * 0.5,
        .rotation_cos = rotated ? cos(angle) : 1.0,
        .rotation_sin = rotated ? sin(angle) : 0.0,
        .rotated = rotated
    };
}

static void record_to_screen(
    const OverlayLevelTransform *transform,
    int tile_x,
    int tile_y,
    const OpenRideORMapPyramidOverlayLineRecord *record,
    float *x1,
    float *y1,
    float *x2,
    float *y2)
{
    const double left = transform->viewport_center_x
        + tile_x * transform->tile_size - transform->center_x;
    const double top = transform->viewport_center_y
        + tile_y * transform->tile_size - transform->center_y;

    *x1 = (float)(left + record->x1 * transform->quantized_scale);
    *y1 = (float)(top + record->y1 * transform->quantized_scale);
    *x2 = (float)(left + record->x2 * transform->quantized_scale);
    *y2 = (float)(top + record->y2 * transform->quantized_scale);
    rotate_point(transform, x1, y1);
    rotate_point(transform, x2, y2);
}

static const uint8_t ROAD_DRAW_ORDER[] = {
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
    OPENRIDE_ROAD_MOTORWAY
};

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

    paint->casing_width = 0;
    paint->casing.a = 0U;
    if (local || trail_detail) {
        paint->dashed = false;
        if (local) paint->width = 1;
        else if (paint->width > 2) paint->width = 2;
    }
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
            return smoothstep(zoom, 11.75, 12.15);
        case OPENRIDE_ROAD_TERTIARY:
            return smoothstep(zoom, 12.75, 13.15);
        case OPENRIDE_ROAD_UNCLASSIFIED:
        case OPENRIDE_ROAD_RESIDENTIAL:
        case OPENRIDE_ROAD_SERVICE:
        case OPENRIDE_ROAD_LIVING_STREET:
        case OPENRIDE_ROAD_OTHER:
            return smoothstep(zoom, 13.75, 14.20);
        case OPENRIDE_ROAD_TRACK:
        case OPENRIDE_ROAD_PATH:
            return smoothstep(zoom, 14.50, 14.95);
        default:
            return 1.0;
    }
#else
    (void)zoom;
    (void)road_class;
    return 1.0;
#endif
}

static int road_owner_zoom(uint8_t road_class)
{
    switch ((OpenRideRoadClass)road_class) {
        case OPENRIDE_ROAD_MOTORWAY:
        case OPENRIDE_ROAD_TRUNK:
        case OPENRIDE_ROAD_PRIMARY:
            return OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM;
        case OPENRIDE_ROAD_SECONDARY:
        case OPENRIDE_ROAD_TERTIARY:
            return OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM;
        default:
            return OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM;
    }
}

static bool collect_dashed_road_segments(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    const OverlayLevelTransform *transform,
    int width,
    int height,
    int zoom,
    const OverlayVisibleRange *range,
    uint8_t road_class,
    int stroke_width)
{
    renderer->dash_segment_count = 0U;
    for (int y = range->first_y; y <= range->last_y; ++y) {
        for (int x = range->first_x; x <= range->last_x; ++x) {
            OverlayCacheEntry *entry = cache_lookup(
                renderer,
                OPENRIDE_ORMAP_PYRAMID_OVERLAY_ROAD,
                zoom,
                x,
                y,
                false);
            if (!entry) return false;

            ++renderer->road_debug.tiles_visited;
            const uint32_t first = entry->class_offsets[road_class];
            const uint32_t end = entry->class_offsets[road_class + 1U];
            for (uint32_t r = first; r < end; ++r) {
                const OpenRideORMapPyramidOverlayLineRecord *record =
                    &entry->tile.records[r];
                float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
                record_to_screen(transform, x, y, record,
                                 &x1, &y1, &x2, &y2);
                const float dx = x2 - x1;
                const float dy = y2 - y1;
                const float length = sqrtf(dx * dx + dy * dy);
                if (length < 0.001f) continue;

                if (!ensure_dash_segment_capacity(
                        renderer,
                        renderer->dash_segment_count + 1U)) {
                    return false;
                }
                const float margin = (float)stroke_width + 3.0f;
                OverlayDashSegment *segment =
                    &renderer->dash_segments[renderer->dash_segment_count++];
                *segment = (OverlayDashSegment){
                    .x1 = x1,
                    .y1 = y1,
                    .x2 = x2,
                    .y2 = y2,
                    .length = length,
                    .endpoint_key = {
                        openride_dashed_line_endpoint_key(
                            x, y, record->x1, record->y1),
                        openride_dashed_line_endpoint_key(
                            x, y, record->x2, record->y2)
                    },
                    .next_endpoint = {-1, -1},
                    .visible = !((x1 < -margin && x2 < -margin)
                        || (x1 > width + margin && x2 > width + margin)
                        || (y1 < -margin && y2 < -margin)
                        || (y1 > height + margin && y2 > height + margin))
                };
            }
        }
    }
    return true;
}

static bool build_dash_graph(OpenRideORMapPyramidOverlayRenderer *renderer)
{
    if (!prepare_dash_nodes(renderer)) return false;
    for (uint32_t i = 0U; i < renderer->dash_segment_count; ++i) {
        OverlayDashSegment *segment = &renderer->dash_segments[i];
        segment->drawn = false;
        for (uint32_t endpoint = 0U; endpoint < 2U; ++endpoint) {
            const uint32_t node_index = dash_node_get(
                renderer,
                segment->endpoint_key[endpoint]);
            OverlayDashNode *node = &renderer->dash_nodes[node_index];
            segment->node_index[endpoint] = node_index;
            segment->next_endpoint[endpoint] = node->first_endpoint;
            node->first_endpoint = (int32_t)(i * 2U + endpoint);
            ++node->degree;
        }
    }
    return true;
}

static bool draw_dash_chain(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    OverlayBatch *batch,
    uint32_t segment_index,
    uint32_t start_endpoint,
    int width,
    OpenRideMapColor color)
{
    const float period = 13.0f;
    float phase = 0.0f;
    for (;;) {
        OverlayDashSegment *segment = &renderer->dash_segments[segment_index];
        if (segment->drawn) return true;

        const bool reverse = start_endpoint != 0U;
        if (segment->visible) {
            if (!draw_dashed_line(
                    renderer,
                    batch,
                    reverse ? segment->x2 : segment->x1,
                    reverse ? segment->y2 : segment->y1,
                    reverse ? segment->x1 : segment->x2,
                    reverse ? segment->y1 : segment->y2,
                    width,
                    color,
                    phase)) {
                return false;
            }
            ++renderer->road_debug.segments_drawn;
        }
        segment->drawn = true;
        phase = openride_dashed_line_advance_phase(
            phase,
            segment->length,
            period);

        const uint32_t arrival_endpoint = 1U - start_endpoint;
        const OverlayDashNode *node =
            &renderer->dash_nodes[segment->node_index[arrival_endpoint]];
        if (node->degree != 2U) return true;

        int32_t encoded = node->first_endpoint;
        int32_t next = -1;
        while (encoded >= 0) {
            const uint32_t candidate_segment = (uint32_t)encoded / 2U;
            const uint32_t candidate_endpoint = (uint32_t)encoded & 1U;
            if (!renderer->dash_segments[candidate_segment].drawn) {
                next = encoded;
                break;
            }
            encoded = renderer->dash_segments[candidate_segment]
                .next_endpoint[candidate_endpoint];
        }
        if (next < 0) return true;
        segment_index = (uint32_t)next / 2U;
        start_endpoint = (uint32_t)next & 1U;
    }
}

static bool draw_dashed_road_class(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    const OverlayLevelTransform *transform,
    int width,
    int height,
    int zoom,
    const OverlayVisibleRange *range,
    uint8_t road_class,
    OpenRideMapRoadPaint paint)
{
    if (!collect_dashed_road_segments(renderer,
                                      transform,
                                      width,
                                      height,
                                      zoom,
                                      range,
                                      road_class,
                                      paint.width)
        || !build_dash_graph(renderer)) {
        return false;
    }

    OverlayBatch batch = {0};
    for (uint32_t i = 0U; i < renderer->dash_segment_count; ++i) {
        OverlayDashSegment *segment = &renderer->dash_segments[i];
        const uint32_t degree0 =
            renderer->dash_nodes[segment->node_index[0]].degree;
        const uint32_t degree1 =
            renderer->dash_nodes[segment->node_index[1]].degree;
        if (segment->drawn || (degree0 == 2U && degree1 == 2U)) continue;
        const uint32_t start_endpoint = degree0 != 2U ? 0U : 1U;
        if (!draw_dash_chain(renderer,
                             &batch,
                             i,
                             start_endpoint,
                             paint.width,
                             paint.line)) {
            return false;
        }
    }
    for (uint32_t i = 0U; i < renderer->dash_segment_count; ++i) {
        if (!renderer->dash_segments[i].drawn
            && !draw_dash_chain(renderer,
                                &batch,
                                i,
                                0U,
                                paint.width,
                                paint.line)) {
            return false;
        }
    }
    return flush_geometry(renderer, &batch);
}

static bool draw_road_class(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    const OpenRideMapCamera *camera,
    int width,
    int height,
    int zoom,
    const OverlayVisibleRange *range,
    uint8_t road_class,
    bool casing,
    OpenRideMapRoadPaint paint)
{
    if (!range || !range->valid) return false;

    const OverlayLevelTransform transform = make_level_transform(
        camera, width, height, zoom);
    if (!casing && paint.dashed) {
        return draw_dashed_road_class(renderer,
                                      &transform,
                                      width,
                                      height,
                                      zoom,
                                      range,
                                      road_class,
                                      paint);
    }
    OverlayBatch batch = {0};
    bool ok = true;
    for (int y = range->first_y; y <= range->last_y && ok; ++y) {
        for (int x = range->first_x; x <= range->last_x && ok; ++x) {
            OverlayCacheEntry *entry = cache_lookup(
                renderer,
                OPENRIDE_ORMAP_PYRAMID_OVERLAY_ROAD,
                zoom,
                x,
                y,
                false);
            if (!entry) return false;

            ++renderer->road_debug.tiles_visited;
            const uint32_t first = entry->class_offsets[road_class];
            const uint32_t end = entry->class_offsets[road_class + 1U];
            for (uint32_t r = first; r < end; ++r) {
                const OpenRideORMapPyramidOverlayLineRecord *record =
                    &entry->tile.records[r];
                float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
                record_to_screen(&transform, x, y, record,
                                 &x1, &y1, &x2, &y2);

                const int stroke_width = casing
                    ? paint.casing_width
                    : paint.width;
                const float margin = (float)stroke_width + 3.0f;
                if ((x1 < -margin && x2 < -margin)
                    || (x1 > width + margin && x2 > width + margin)
                    || (y1 < -margin && y2 < -margin)
                    || (y1 > height + margin && y2 > height + margin)) {
                    continue;
                }

                const OpenRideMapColor color = casing
                    ? paint.casing
                    : paint.line;
                if (casing) {
                    ok = draw_line(renderer, &batch,
                                   x1, y1, x2, y2,
                                   paint.casing_width,
                                   color);
                } else {
                    ok = draw_line(renderer, &batch,
                                   x1, y1, x2, y2,
                                   paint.width,
                                   color);
                }
                if (ok) ++renderer->road_debug.segments_drawn;
            }
        }
    }

    return ok && flush_geometry(renderer, &batch);
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

static double waterway_fade(uint8_t kind, double zoom)
{
    switch ((OpenRideORMapWaterwayKind)kind) {
        case OPENRIDE_ORMAP_WATERWAY_RIVER:
        case OPENRIDE_ORMAP_WATERWAY_CANAL:
            return smoothstep(zoom, 12.0, 12.40);
        case OPENRIDE_ORMAP_WATERWAY_STREAM:
            return smoothstep(zoom, 12.5, 12.90);
        case OPENRIDE_ORMAP_WATERWAY_DRAIN:
            return smoothstep(zoom, 14.5, 14.90);
        default:
            return 0.0;
    }
}

static bool draw_water_level(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    const OpenRideMapCamera *camera,
    int width,
    int height,
    int zoom,
    const OverlayVisibleRange *range)
{
    if (!range || !range->valid) return false;

    const OpenRideMapColor base_color =
        openride_map_palette(renderer->style).water;
    const OverlayLevelTransform transform = make_level_transform(
        camera, width, height, zoom);
    OverlayBatch batch = {0};
    bool ok = true;
    for (int y = range->first_y; y <= range->last_y && ok; ++y) {
        for (int x = range->first_x; x <= range->last_x && ok; ++x) {
            OverlayCacheEntry *entry = cache_lookup(
                renderer,
                OPENRIDE_ORMAP_PYRAMID_OVERLAY_WATERWAY,
                zoom,
                x,
                y,
                false);
            if (!entry) return false;

            for (uint32_t r = 0U; r < entry->tile.count; ++r) {
                const OpenRideORMapPyramidOverlayLineRecord *record =
                    &entry->tile.records[r];
                const int line_width =
                    waterway_width(record->kind, camera->zoom);
                const double fade =
                    waterway_fade(record->kind, camera->zoom);
                if (line_width <= 0 || fade <= 0.001) continue;

                OpenRideMapColor color = base_color;
                color.a = scaled_alpha(235U, fade);
                float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
                record_to_screen(&transform, x, y, record,
                                 &x1, &y1, &x2, &y2);
                const float margin = (float)line_width + 3.0f;
                if ((x1 < -margin && x2 < -margin)
                    || (x1 > width + margin && x2 > width + margin)
                    || (y1 < -margin && y2 < -margin)
                    || (y1 > height + margin && y2 > height + margin)) {
                    continue;
                }
                ok = draw_line(renderer, &batch,
                               x1, y1, x2, y2,
                               line_width,
                               color);
            }
        }
    }

    return ok && flush_geometry(renderer, &batch);
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

static bool boxes_overlap(OverlayLabelBox a, OverlayLabelBox b)
{
    return a.left < b.right && a.right > b.left
        && a.top < b.bottom && a.bottom > b.top;
}

static uint32_t label_collect_region_references(
    const OpenRideORMapLabel *labels,
    uint32_t count,
    uint32_t references[3])
{
    uint32_t selected = 0U;
    for (int pass = 0; pass < 3 && selected < 3U; ++pass) {
        const uint8_t wanted = pass == 0
            ? OPENRIDE_PLACE_CITY
            : (pass == 1 ? OPENRIDE_PLACE_TOWN : OPENRIDE_PLACE_VILLAGE);
        for (uint32_t i = 0U; i < count && selected < 3U; ++i) {
            if (labels[i].kind == wanted && labels[i].name[0] != '\0') {
                references[selected++] = i;
            }
        }
    }
    return selected;
}

static bool label_is_reference(const uint32_t references[3],
                               uint32_t count,
                               uint32_t index)
{
    for (uint32_t i = 0U; i < count; ++i) {
        if (references[i] == index) return true;
    }
    return false;
}

static double label_lod_fade(const OpenRideORMapLabel *label, double zoom)
{
    switch ((OpenRideORMapLabelLOD)label->lod) {
        case OPENRIDE_ORMAP_LABEL_LOD_REGIONAL:
            return smoothstep(zoom, 6.0, 6.55);
        case OPENRIDE_ORMAP_LABEL_LOD_OVERVIEW:
            return smoothstep(zoom, 9.50, 10.35);
        case OPENRIDE_ORMAP_LABEL_LOD_LOCAL:
            return smoothstep(zoom, 11.85, 12.80);
        case OPENRIDE_ORMAP_LABEL_LOD_DETAIL:
        default:
            return smoothstep(zoom, 13.40, 14.10);
    }
}

static OpenRidePointD label_world_to_screen(
    OpenRidePointD world,
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
    return (OpenRidePointD){
        width * 0.5 + world_dx * bearing_cos + world_dy * bearing_sin,
        height * 0.5 - world_dx * bearing_sin + world_dy * bearing_cos
    };
}

OpenRideORMapPyramidOverlayRenderer *
openride_ormap_pyramid_overlay_renderer_create(
    SDL_Renderer *renderer,
    const char *ormap11_path,
    char *error,
    size_t error_size)
{
    if (!renderer || !ormap11_path) {
        set_error(error, error_size, "invalid overlay renderer arguments");
        return NULL;
    }

    OpenRideORMapPyramidOverlayMap *map =
        openride_ormap_pyramid_overlay_open(ormap11_path, error, error_size);
    if (!map) return NULL;

    OpenRideORMapPyramidOverlayRenderer *overlay = calloc(1U, sizeof(*overlay));
    if (!overlay) {
        openride_ormap_pyramid_overlay_close(map);
        set_error(error, error_size, "out of memory creating overlay renderer");
        return NULL;
    }
    overlay->renderer = renderer;
    overlay->map = map;
    overlay->style = OPENRIDE_MAP_STYLE_TRAIL;
    overlay->healthy = true;
    overlay->road_debug.prewarm_zoom = -1;

    overlay->premult_blend = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ONE,
        SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ONE,
        SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        SDL_BLENDOPERATION_ADD);

    SDL_Texture *previous_target = SDL_GetRenderTarget(renderer);
    SDL_BlendMode previous_blend = SDL_BLENDMODE_NONE;
    const bool have_previous_blend = SDL_GetRenderDrawBlendMode(renderer, &previous_blend);
    SDL_Texture *probe = SDL_CreateTexture(renderer,
                                           SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET,
                                           4,
                                           4);
    bool target_ok = probe != NULL;
    if (target_ok) {
        target_ok = SDL_SetTextureBlendMode(probe, overlay->premult_blend)
            && SDL_SetRenderTarget(renderer, probe)
            && SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND)
            && SDL_SetRenderTarget(renderer, previous_target);
    }
    if (have_previous_blend) {
        (void)SDL_SetRenderDrawBlendMode(renderer, previous_blend);
    }
    if (probe) SDL_DestroyTexture(probe);
    if (!target_ok) {
        openride_ormap_pyramid_overlay_renderer_destroy(overlay);
        set_error(error, error_size,
                  "SDL target/custom blend unavailable for v11 overlay");
        return NULL;
    }

    uint32_t label_count = 0U;
    const OpenRideORMapLabel *labels =
        openride_ormap_pyramid_overlay_labels(map, &label_count);
    if (labels && label_count > 0U) {
        overlay->label_world_positions = malloc(
            (size_t)label_count * sizeof(*overlay->label_world_positions));
        if (overlay->label_world_positions) {
            overlay->label_world_position_count = label_count;
            for (uint32_t i = 0U; i < label_count; ++i) {
                overlay->label_world_positions[i] = openride_mercator_forward(
                    labels[i].lat_e7 / 10000000.0,
                    labels[i].lon_e7 / 10000000.0);
            }
        }
    }

    SDL_Log("OpenRide v11 overlay V3.9.1 active: "
            "roads=z10/z12/z14 waterways=z13 labels=%u",
            label_count);
    set_error(error, error_size, "");
    return overlay;
}

void openride_ormap_pyramid_overlay_renderer_destroy(
    OpenRideORMapPyramidOverlayRenderer *renderer)
{
    if (!renderer) return;
    for (uint32_t i = 0U; i < OVERLAY_CACHE_CAPACITY; ++i) {
        cache_entry_destroy(&renderer->cache[i]);
    }
    if (renderer->layer_compositor) SDL_DestroyTexture(renderer->layer_compositor);
    free(renderer->vertices);
    free(renderer->indices);
    free(renderer->dash_segments);
    free(renderer->dash_nodes);
    free(renderer->label_world_positions);
    openride_ormap_pyramid_overlay_close(renderer->map);
    free(renderer);
}

void openride_ormap_pyramid_overlay_renderer_set_style(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    OpenRideMapStyle style)
{
    if (renderer) renderer->style = style;
}

void openride_ormap_pyramid_overlay_renderer_begin_frame(
    OpenRideORMapPyramidOverlayRenderer *renderer)
{
    if (!renderer) return;
    ++renderer->frame_counter;
    renderer->road_load_budget = OVERLAY_ROAD_LOAD_BUDGET;
    renderer->water_load_budget = OVERLAY_WATER_LOAD_BUDGET;
    renderer->needs_followup = false;
    renderer->road_debug_active = false;
    memset(&renderer->road_debug, 0, sizeof(renderer->road_debug));
    renderer->road_debug.prewarm_zoom = -1;
}

bool openride_ormap_pyramid_overlay_renderer_draw_waterways(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    const OpenRideMapCamera *camera,
    int width,
    int height)
{
    if (!renderer || !camera || !renderer->healthy) return false;
    if (!openride_ormap_pyramid_overlay_layer_available(
            renderer->map,
            OPENRIDE_ORMAP_PYRAMID_OVERLAY_WATERWAY)) return false;
    if (camera->zoom < 12.0) return true;

    OverlayVisibleRange range = {0};
    if (!prepare_visible_level(
            renderer,
            camera,
            width,
            height,
            OPENRIDE_ORMAP_PYRAMID_OVERLAY_WATERWAY,
            OPENRIDE_ORMAP_WATER_ZOOM,
            &range)) {
        return false;
    }

    OverlayTargetState layer_state;
    if (!layer_begin(renderer, width, height, &layer_state)) return false;

    renderer->road_debug_active = false;
    bool ok = draw_water_level(
        renderer,
        camera,
        width,
        height,
        OPENRIDE_ORMAP_WATER_ZOOM,
        &range);
    if (!layer_end(renderer, &layer_state, ok)) ok = false;
    if (!ok) renderer->healthy = false;
    return ok;
}

bool openride_ormap_pyramid_overlay_renderer_draw_roads(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    const OpenRideMapCamera *camera,
    int width,
    int height)
{
    if (!renderer || !camera || !renderer->healthy) return false;
    if (!openride_ormap_pyramid_overlay_layer_available(
            renderer->map,
            OPENRIDE_ORMAP_PYRAMID_OVERLAY_ROAD)) return false;

    const uint64_t started = SDL_GetTicksNS();
    OpenRideMapRoadPaint paints[OPENRIDE_ROAD_OTHER + 1];
    bool visible[OPENRIDE_ROAD_OTHER + 1];
    memset(paints, 0, sizeof(paints));
    memset(visible, 0, sizeof(visible));

    for (int road_class = OPENRIDE_ROAD_UNKNOWN;
         road_class <= OPENRIDE_ROAD_OTHER;
         ++road_class) {
        visible[road_class] = openride_map_road_paint(
            renderer->style,
            road_kind((uint8_t)road_class),
            false,
            camera->zoom,
            &paints[road_class]);
        if (!visible[road_class]) continue;
        apply_android_minor_road_lod(camera->zoom, road_class, &paints[road_class]);
        const double fade = android_road_class_fade(camera->zoom, road_class);
        scale_color_alpha(&paints[road_class].line, fade);
        scale_color_alpha(&paints[road_class].casing, fade);
        if (paints[road_class].line.a == 0U) visible[road_class] = false;
    }

    struct RoadOwnerLevel {
        int zoom;
        bool required;
        OverlayVisibleRange range;
    } levels[] = {
        {.zoom = OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM},
        {.zoom = OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM},
        {.zoom = OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM}
    };

    for (size_t order = 0U;
         order < sizeof(ROAD_DRAW_ORDER) / sizeof(ROAD_DRAW_ORDER[0]);
         ++order) {
        const uint8_t road_class = ROAD_DRAW_ORDER[order];
        if (!visible[road_class]) continue;
        const int zoom = road_owner_zoom(road_class);
        for (size_t i = 0U; i < sizeof(levels) / sizeof(levels[0]); ++i) {
            if (levels[i].zoom == zoom) levels[i].required = true;
        }
    }

    bool any_level = false;
    for (size_t i = 0U; i < sizeof(levels) / sizeof(levels[0]); ++i) {
        if (!levels[i].required) continue;
        any_level = true;
        if (!prepare_visible_level(
                renderer,
                camera,
                width,
                height,
                OPENRIDE_ORMAP_PYRAMID_OVERLAY_ROAD,
                levels[i].zoom,
                &levels[i].range)) {
            renderer->road_debug.roads_ms =
                (double)(SDL_GetTicksNS() - started) / 1000000.0;
            return false;
        }
    }

    if (!any_level) {
        renderer->road_debug.roads_ms =
            (double)(SDL_GetTicksNS() - started) / 1000000.0;
        return true;
    }

    OverlayTargetState layer_state;
    const uint64_t compositor_begin_started = SDL_GetTicksNS();
    if (!layer_begin(renderer, width, height, &layer_state)) {
        renderer->road_debug.compositor_ms +=
            (double)(SDL_GetTicksNS() - compositor_begin_started) / 1000000.0;
        renderer->road_debug.roads_ms =
            (double)(SDL_GetTicksNS() - started) / 1000000.0;
        return false;
    }
    renderer->road_debug.compositor_ms +=
        (double)(SDL_GetTicksNS() - compositor_begin_started) / 1000000.0;
    renderer->road_debug_active = true;
    bool layer_ok = true;
    const uint64_t geometry_started = SDL_GetTicksNS();

    for (int pass = 0; pass < 2; ++pass) {
        const bool casing = pass == 0;
        for (size_t order = 0U;
             order < sizeof(ROAD_DRAW_ORDER) / sizeof(ROAD_DRAW_ORDER[0]);
             ++order) {
            const uint8_t road_class = ROAD_DRAW_ORDER[order];
            if (!visible[road_class]) continue;
            const OpenRideMapRoadPaint paint = paints[road_class];
            if (casing
                && !(paint.casing_width > paint.width && paint.casing.a > 0U)) continue;
            const int zoom = road_owner_zoom(road_class);
            const OverlayVisibleRange *range = NULL;
            for (size_t i = 0U; i < sizeof(levels) / sizeof(levels[0]); ++i) {
                if (levels[i].zoom == zoom) range = &levels[i].range;
            }
            if (!draw_road_class(renderer,
                                 camera,
                                 width,
                                 height,
                                 zoom,
                                 range,
                                 road_class,
                                 casing,
                                 paint)) {
                layer_ok = false;
                break;
            }
        }
        if (!layer_ok) break;
    }

    renderer->road_debug.geometry_ms +=
        (double)(SDL_GetTicksNS() - geometry_started) / 1000000.0;
    renderer->road_debug_active = false;
    const uint64_t compositor_end_started = SDL_GetTicksNS();
    if (!layer_end(renderer, &layer_state, layer_ok)) layer_ok = false;
    renderer->road_debug.compositor_ms +=
        (double)(SDL_GetTicksNS() - compositor_end_started) / 1000000.0;
    if (!layer_ok) renderer->healthy = false;
    renderer->road_debug.roads_ms =
        (double)(SDL_GetTicksNS() - started) / 1000000.0;
    return layer_ok;
}

bool openride_ormap_pyramid_overlay_renderer_draw_labels(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    const OpenRideMapCamera *camera,
    int width,
    int height)
{
    if (!renderer || !camera || !renderer->healthy) return false;
    uint32_t count = 0U;
    const OpenRideORMapLabel *labels =
        openride_ormap_pyramid_overlay_labels(renderer->map, &count);
    if (!labels || count == 0U) return false;

    const OpenRidePointD center = openride_mercator_forward(
        camera->center_lat, camera->center_lon);
    const double world_size = openride_world_size_pixels(camera->zoom);
    const double angle = camera->bearing_deg * 3.14159265358979323846 / 180.0;
    const double bearing_cos = cos(angle);
    const double bearing_sin = sin(angle);

    uint32_t max_boxes = OVERLAY_LABEL_BOX_MAX;
    if (camera->zoom < 8.0) max_boxes = 6U;
    else if (camera->zoom < 10.0) max_boxes = 8U;
    else if (camera->zoom < 12.5) max_boxes = 12U;
    else if (camera->zoom < 14.0) max_boxes = 20U;
    else if (camera->zoom < 16.0) max_boxes = 32U;
    else max_boxes = 48U;

    uint32_t references[3] = {0U, 0U, 0U};
    const uint32_t reference_count =
        label_collect_region_references(labels, count, references);
    OverlayLabelBox boxes[OVERLAY_LABEL_BOX_MAX];
    uint32_t box_count = 0U;
    const OpenRideMapPalette palette = openride_map_palette(renderer->style);

    SDL_BlendMode previous = SDL_BLENDMODE_NONE;
    const bool have_previous = SDL_GetRenderDrawBlendMode(renderer->renderer, &previous);
    SDL_SetRenderDrawBlendMode(renderer->renderer, SDL_BLENDMODE_BLEND);

    for (uint32_t i = 0U; i < count && box_count < max_boxes; ++i) {
        const OpenRideORMapLabel *label = &labels[i];
        if (label->name[0] == '\0') continue;
        const char *kind = label_kind_name(label->kind);
        const bool reference = label_is_reference(references, reference_count, i);
        if (!reference
            && !openride_map_place_label_visible(kind, 0, camera->zoom)) continue;

        double fade = label_lod_fade(label, camera->zoom);
        if (reference) {
            const double persistent = smoothstep(camera->zoom, 6.0, 6.55);
            if (persistent > fade) fade = persistent;
        }
        if (fade <= 0.001) continue;

        const OpenRidePointD world = renderer->label_world_positions
            && renderer->label_world_position_count == count
                ? renderer->label_world_positions[i]
                : openride_mercator_forward(
                    label->lat_e7 / 10000000.0,
                    label->lon_e7 / 10000000.0);
        const OpenRidePointD p = label_world_to_screen(
            world, center, world_size,
            bearing_cos, bearing_sin,
            width, height);
        if (p.x < -100.0 || p.x > width + 100.0
            || p.y < -40.0 || p.y > height + 40.0) continue;

        const float text_w = (float)strlen(label->name) * 8.0f;
        const OverlayLabelBox box = {
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

        OpenRideMapColor halo = palette.label_halo;
        OpenRideMapColor color = palette.label;
        scale_color_alpha(&halo, fade);
        scale_color_alpha(&color, fade);
        boxes[box_count++] = box;

        SDL_SetRenderDrawColor(renderer->renderer, halo.r, halo.g, halo.b, halo.a);
        SDL_RenderDebugText(renderer->renderer,
                            (float)p.x - text_w * 0.5f + 1.0f,
                            (float)p.y + 1.0f,
                            label->name);
        SDL_SetRenderDrawColor(renderer->renderer, color.r, color.g, color.b, color.a);
        SDL_RenderDebugText(renderer->renderer,
                            (float)p.x - text_w * 0.5f,
                            (float)p.y,
                            label->name);
    }

    SDL_SetRenderDrawBlendMode(renderer->renderer,
                               have_previous ? previous : SDL_BLENDMODE_NONE);
    return true;
}

bool openride_ormap_pyramid_overlay_renderer_needs_followup_frame(
    const OpenRideORMapPyramidOverlayRenderer *renderer)
{
    return renderer && renderer->needs_followup;
}

void openride_ormap_pyramid_overlay_renderer_get_road_debug_stats(
    const OpenRideORMapPyramidOverlayRenderer *renderer,
    OpenRideORMapRoadDebugStats *stats)
{
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
    stats->prewarm_zoom = -1;
    if (renderer) *stats = renderer->road_debug;
}
