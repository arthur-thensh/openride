#include "map/ormap_renderer.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define V4_TILE_SIZE 256.0
#define V4_CACHE_ASSOCIATIVITY 4U
#define V4_BATCH_VERTEX_LIMIT 16384U
#define V4_BATCH_INDEX_LIMIT 24576U

typedef struct V4GeometryBatch {
    uint32_t vertex_count;
    uint32_t index_count;
} V4GeometryBatch;

typedef struct V4Rotation {
    double cosine;
    double sine;
    double cx;
    double cy;
} V4Rotation;

void openride_ormap_renderer_draw_layer_legacy(
    OpenRideORMapRenderer *renderer,
    const OpenRideMapCamera *camera,
    int viewport_width,
    int viewport_height,
    OpenRideORMapRenderLayer layer);

static double v4_smoothstep(double value, double start, double end)
{
    if (value <= start) return 0.0;
    if (value >= end) return 1.0;
    double t = (value - start) / (end - start);
    return t * t * (3.0 - 2.0 * t);
}

static uint8_t v4_scaled_alpha(uint8_t alpha, double factor)
{
    if (factor <= 0.0) return 0U;
    if (factor >= 1.0) return alpha;
    return (uint8_t)lround((double)alpha * factor);
}

static int v4_wrap_x(int x, int count)
{
    int wrapped = x % count;
    if (wrapped < 0) wrapped += count;
    return wrapped;
}

static V4Rotation v4_rotation(const OpenRideMapCamera *camera,
                              int width,
                              int height)
{
    const double angle = camera->bearing_deg
        * 3.14159265358979323846 / 180.0;
    V4Rotation rotation = {
        .cosine = cos(angle),
        .sine = sin(angle),
        .cx = width * 0.5,
        .cy = height * 0.5
    };
    return rotation;
}

static void v4_rotate_point(const V4Rotation *rotation, float *x, float *y)
{
    if (!rotation || !x || !y) return;
    const double dx = *x - rotation->cx;
    const double dy = *y - rotation->cy;
    *x = (float)(rotation->cx
                 + dx * rotation->cosine
                 + dy * rotation->sine);
    *y = (float)(rotation->cy
                 - dx * rotation->sine
                 + dy * rotation->cosine);
}

static bool v4_ensure_geometry(OpenRideORMapRenderer *renderer,
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
        SDL_Vertex *vertices = realloc(
            renderer->area_vertices,
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

static void v4_batch_flush(OpenRideORMapRenderer *renderer,
                           V4GeometryBatch *batch)
{
    if (!renderer || !batch || batch->vertex_count == 0U
        || batch->index_count == 0U) {
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

static bool v4_batch_reserve(OpenRideORMapRenderer *renderer,
                             V4GeometryBatch *batch,
                             uint32_t add_vertices,
                             uint32_t add_indices)
{
    if (!renderer || !batch) return false;
    if (add_vertices > V4_BATCH_VERTEX_LIMIT
        || add_indices > V4_BATCH_INDEX_LIMIT) {
        return false;
    }
    if (batch->vertex_count > V4_BATCH_VERTEX_LIMIT - add_vertices
        || batch->index_count > V4_BATCH_INDEX_LIMIT - add_indices) {
        v4_batch_flush(renderer, batch);
    }
    return v4_ensure_geometry(renderer,
                              batch->vertex_count + add_vertices,
                              batch->index_count + add_indices);
}

static void v4_set_vertex(SDL_Vertex *vertex,
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

static bool v4_batch_triangle(OpenRideORMapRenderer *renderer,
                              V4GeometryBatch *batch,
                              const float x[3],
                              const float y[3],
                              OpenRideMapColor color)
{
    if (!v4_batch_reserve(renderer, batch, 3U, 3U)) return false;
    const uint32_t base_vertex = batch->vertex_count;
    const uint32_t base_index = batch->index_count;
    for (uint32_t i = 0U; i < 3U; ++i) {
        v4_set_vertex(&renderer->area_vertices[base_vertex + i],
                      x[i],
                      y[i],
                      color);
        renderer->area_indices[base_index + i] = (int)(base_vertex + i);
    }
    batch->vertex_count += 3U;
    batch->index_count += 3U;
    return true;
}

static bool v4_batch_quad(OpenRideORMapRenderer *renderer,
                          V4GeometryBatch *batch,
                          const float x[4],
                          const float y[4],
                          OpenRideMapColor color)
{
    if (!v4_batch_reserve(renderer, batch, 4U, 6U)) return false;
    const uint32_t base_vertex = batch->vertex_count;
    const uint32_t base_index = batch->index_count;
    for (uint32_t i = 0U; i < 4U; ++i) {
        v4_set_vertex(&renderer->area_vertices[base_vertex + i],
                      x[i],
                      y[i],
                      color);
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

static uint32_t v4_cache_hash(int zoom, int x, int y)
{
    uint32_t value = (uint32_t)zoom * UINT32_C(0x9e3779b9);
    value ^= (uint32_t)x * UINT32_C(0x85ebca6b);
    value ^= (uint32_t)y * UINT32_C(0xc2b2ae35);
    value ^= value >> 16U;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15U;
    return value;
}

static size_t v4_cache_base(size_t capacity, int zoom, int x, int y)
{
    const size_t set_count = capacity / V4_CACHE_ASSOCIATIVITY;
    return (size_t)(v4_cache_hash(zoom, x, y) % (uint32_t)set_count)
        * V4_CACHE_ASSOCIATIVITY;
}

static OpenRideORMapAreaCacheEntry *v4_area_cache_slot(
    OpenRideORMapRenderer *renderer,
    int zoom,
    int x,
    int y)
{
    const size_t base = v4_cache_base(OPENRIDE_ORMAP_AREA_CACHE_CAPACITY,
                                      zoom,
                                      x,
                                      y);
    OpenRideORMapAreaCacheEntry *victim = NULL;
    OpenRideORMapAreaCacheEntry *oldest = &renderer->areas[base];
    for (size_t i = 0U; i < V4_CACHE_ASSOCIATIVITY; ++i) {
        OpenRideORMapAreaCacheEntry *entry = &renderer->areas[base + i];
        if (entry->occupied && entry->zoom == zoom
            && entry->x == x && entry->y == y) {
            entry->last_used = renderer->frame_counter;
            return entry;
        }
        if (!entry->occupied && !victim) victim = entry;
        if (entry->occupied && oldest->occupied
            && entry->last_used < oldest->last_used) {
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
    (void)openride_ormap_load_area_tile(renderer->map,
                                        zoom,
                                        x,
                                        y,
                                        &victim->tile,
                                        error,
                                        sizeof(error));
    return victim;
}

static OpenRideORMapMaskCacheEntry *v4_mask_cache_slot(
    OpenRideORMapRenderer *renderer,
    int zoom,
    int x,
    int y)
{
    const size_t base = v4_cache_base(OPENRIDE_ORMAP_MASK_CACHE_CAPACITY,
                                      zoom,
                                      x,
                                      y);
    OpenRideORMapMaskCacheEntry *victim = NULL;
    OpenRideORMapMaskCacheEntry *oldest = &renderer->masks[base];
    for (size_t i = 0U; i < V4_CACHE_ASSOCIATIVITY; ++i) {
        OpenRideORMapMaskCacheEntry *entry = &renderer->masks[base + i];
        if (entry->occupied && entry->zoom == zoom
            && entry->x == x && entry->y == y) {
            entry->last_used = renderer->frame_counter;
            return entry;
        }
        if (!entry->occupied && !victim) victim = entry;
        if (entry->occupied && oldest->occupied
            && entry->last_used < oldest->last_used) {
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
    (void)openride_ormap_load_mask_tile(renderer->map,
                                        zoom,
                                        x,
                                        y,
                                        &victim->tile,
                                        error,
                                        sizeof(error));
    return victim;
}

static double v4_decode_area_coord(uint16_t value)
{
    const double buffer = OPENRIDE_ORMAP_AREA_BUFFER_FRACTION;
    return ((double)value / 65535.0) * (1.0 + 2.0 * buffer) - buffer;
}

static bool v4_mask_bit(const unsigned char *bits, uint32_t index)
{
    return bits
        && (bits[index >> 3U] & (unsigned char)(1U << (index & 7U))) != 0U;
}

static void v4_visible_tile_range(const OpenRideMapCamera *camera,
                                  int width,
                                  int height,
                                  int zoom,
                                  double *tile_size_out,
                                  double *center_x_out,
                                  double *center_y_out,
                                  int *first_x,
                                  int *last_x,
                                  int *first_y,
                                  int *last_y,
                                  V4Rotation *rotation_out)
{
    const int count = 1 << zoom;
    const double scale = pow(2.0, camera->zoom - zoom);
    const double tile_size = V4_TILE_SIZE * scale;
    const OpenRidePointD center = openride_mercator_forward(camera->center_lat,
                                                             camera->center_lon);
    const double world_size = tile_size * count;
    const double center_x = center.x * world_size;
    const double center_y = center.y * world_size;
    const V4Rotation rotation = v4_rotation(camera, width, height);
    const double half_w = fabs(rotation.cosine) * width * 0.5
        + fabs(rotation.sine) * height * 0.5;
    const double half_h = fabs(rotation.sine) * width * 0.5
        + fabs(rotation.cosine) * height * 0.5;

    *tile_size_out = tile_size;
    *center_x_out = center_x;
    *center_y_out = center_y;
    *first_x = (int)floor((center_x - half_w) / tile_size);
    *last_x = (int)floor((center_x + half_w) / tile_size);
    *first_y = (int)floor((center_y - half_h) / tile_size);
    *last_y = (int)floor((center_y + half_h) / tile_size);
    *rotation_out = rotation;
}

static OpenRideMapColor v4_builtup_color(OpenRideORMapRenderer *renderer,
                                         bool detail,
                                         double factor)
{
    OpenRideMapColor color = openride_map_palette(renderer->style).building;
    if (renderer->style == OPENRIDE_MAP_STYLE_TRAIL) {
        color.a = detail ? 88U : 72U;
    } else {
        color.a = detail ? 108U : 88U;
    }
    color.a = v4_scaled_alpha(color.a, factor);
    return color;
}

static OpenRideMapColor v4_green_color(OpenRideORMapRenderer *renderer,
                             double factor)
{
    OpenRideMapColor color;
    if (renderer->style == OPENRIDE_MAP_STYLE_TOPO) {
        color = (OpenRideMapColor){180U, 203U, 170U, 150U};
    } else if (renderer->style == OPENRIDE_MAP_STYLE_TRAIL) {
        color = (OpenRideMapColor){194U, 210U, 184U, 118U};
    } else {
        color = (OpenRideMapColor){207U, 216U, 201U, 98U};
    }
    color.a = v4_scaled_alpha(color.a, factor);
    return color;
}

static void v4_draw_coarse_landcover(OpenRideORMapRenderer *renderer,
                           const OpenRideMapCamera *camera,
                           int width,
                           int height)
{
    if (camera->zoom < 9.20 || camera->zoom > 13.85) return;
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    if (!metadata || metadata->format_version < 4) return;

    const double urban_in = v4_smoothstep(camera->zoom, 9.70, 10.45);
    const double green_in = v4_smoothstep(camera->zoom, 9.20, 10.05);
    const double fade_out = 1.0 - v4_smoothstep(camera->zoom, 13.15, 13.75);
    const OpenRideMapColor urban =
        v4_builtup_color(renderer, false, urban_in * fade_out);
    const OpenRideMapColor green =
        v4_green_color(renderer, green_in * fade_out);
    if (urban.a == 0U && green.a == 0U) return;

    const int zoom = metadata->area_coarse_zoom;
    const int count = 1 << zoom;
    double tile_size = 0.0;
    double center_x = 0.0;
    double center_y = 0.0;
    int first_x = 0, last_x = 0, first_y = 0, last_y = 0;
    V4Rotation rotation;
    v4_visible_tile_range(camera,
                width,
                height,
                zoom,
                &tile_size,
                &center_x,
                &center_y,
                &first_x,
                &last_x,
                &first_y,
                &last_y,
                &rotation);

    V4GeometryBatch batch = {0};
    /* Two passes guarantee the intended background hierarchy even if a tile
     * also contains legacy area records: green first, then urban. */
    const uint8_t kinds[2] = {
        OPENRIDE_ORMAP_AREA_GREEN,
        OPENRIDE_ORMAP_AREA_BUILTUP
    };
    for (uint32_t pass = 0U; pass < 2U; ++pass) {
        const uint8_t kind = kinds[pass];
        const OpenRideMapColor color =
  kind == OPENRIDE_ORMAP_AREA_GREEN ? green : urban;
        if (color.a == 0U) continue;
        for (int ty = first_y; ty <= last_y; ++ty) {
  if (ty < 0 || ty >= count) continue;
  for (int tx = first_x; tx <= last_x; ++tx) {
      const int qx = v4_wrap_x(tx, count);
      OpenRideORMapAreaCacheEntry *entry =
          v4_area_cache_slot(renderer, zoom, qx, ty);
      if (!entry || entry->tile.count == 0U) continue;
      const double left = width * 0.5 + tx * tile_size - center_x;
      const double top = height * 0.5 + ty * tile_size - center_y;
      for (uint32_t i = 0U; i < entry->tile.count; ++i) {
          const OpenRideORMapAreaTriangle *triangle =
              &entry->tile.triangles[i];
          if (triangle->kind != kind) continue;
          const uint16_t xs[3] = {
              triangle->x1, triangle->x2, triangle->x3
          };
          const uint16_t ys[3] = {
              triangle->y1, triangle->y2, triangle->y3
          };
          float x[3];
          float y[3];
          float min_x = FLT_MAX;
          float min_y = FLT_MAX;
          float max_x = -FLT_MAX;
          float max_y = -FLT_MAX;
          for (uint32_t v = 0U; v < 3U; ++v) {
              x[v] = (float)(left
                  + v4_decode_area_coord(xs[v]) * tile_size);
              y[v] = (float)(top
                  + v4_decode_area_coord(ys[v]) * tile_size);
              v4_rotate_point(&rotation, &x[v], &y[v]);
              if (x[v] < min_x) min_x = x[v];
              if (x[v] > max_x) max_x = x[v];
              if (y[v] < min_y) min_y = y[v];
              if (y[v] > max_y) max_y = y[v];
          }
          if (max_x < -2.0f || min_x > width + 2.0f
              || max_y < -2.0f || min_y > height + 2.0f) {
              continue;
          }
          if (!v4_batch_triangle(renderer, &batch, x, y, color)) {
              v4_batch_flush(renderer, &batch);
              return;
          }
      }
  }
        }
        v4_batch_flush(renderer, &batch);
    }
}

static void v4_draw_detail_builtup(OpenRideORMapRenderer *renderer,
                                   const OpenRideMapCamera *camera,
                                   int width,
                                   int height)
{
    if (camera->zoom < 13.15) return;
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    if (!metadata || metadata->format_version < 4) return;

    const double factor = v4_smoothstep(camera->zoom, 13.15, 13.75);
    const OpenRideMapColor color = v4_builtup_color(renderer, true, factor);
    if (color.a == 0U) return;

    const int zoom = metadata->mask_zoom;
    const int count = 1 << zoom;
    double tile_size = 0.0;
    double center_x = 0.0;
    double center_y = 0.0;
    int first_x = 0, last_x = 0, first_y = 0, last_y = 0;
    V4Rotation rotation;
    v4_visible_tile_range(camera,
                          width,
                          height,
                          zoom,
                          &tile_size,
                          &center_x,
                          &center_y,
                          &first_x,
                          &last_x,
                          &first_y,
                          &last_y,
                          &rotation);

    V4GeometryBatch batch = {0};
    for (int ty = first_y; ty <= last_y; ++ty) {
        if (ty < 0 || ty >= count) continue;
        for (int tx = first_x; tx <= last_x; ++tx) {
            const int qx = v4_wrap_x(tx, count);
            OpenRideORMapMaskCacheEntry *entry =
                v4_mask_cache_slot(renderer, zoom, qx, ty);
            if (!entry || !entry->tile.builtup || entry->tile.grid_size == 0U) {
                continue;
            }
            const uint32_t grid = entry->tile.grid_size;
            const double cell = tile_size / grid;
            const double left = width * 0.5 + tx * tile_size - center_x;
            const double top = height * 0.5 + ty * tile_size - center_y;
            for (uint32_t y = 0U; y < grid; ++y) {
                uint32_t x = 0U;
                while (x < grid) {
                    while (x < grid
                           && !v4_mask_bit(entry->tile.builtup,
                                           y * grid + x)) {
                        ++x;
                    }
                    if (x >= grid) break;
                    const uint32_t start = x;
                    while (x < grid
                           && v4_mask_bit(entry->tile.builtup,
                                          y * grid + x)) {
                        ++x;
                    }
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
                    for (uint32_t v = 0U; v < 4U; ++v) {
                        v4_rotate_point(&rotation, &px[v], &py[v]);
                    }
                    if (!v4_batch_quad(renderer, &batch, px, py, color)) {
                        v4_batch_flush(renderer, &batch);
                        return;
                    }
                }
            }
        }
    }
    v4_batch_flush(renderer, &batch);
}

void openride_ormap_renderer_draw_layer(OpenRideORMapRenderer *renderer,
                                        const OpenRideMapCamera *camera,
                                        int viewport_width,
                                        int viewport_height,
                                        OpenRideORMapRenderLayer layer)
{
    if (!renderer || !camera || viewport_width <= 0 || viewport_height <= 0) {
        return;
    }
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    const bool v4 = metadata && metadata->format_version >= 4;

    if (v4 && layer == OPENRIDE_ORMAP_RENDER_LAYER_AREAS) {
        /* Low-zoom landcover stays below vector water, roads and labels. */
        v4_draw_coarse_landcover(renderer,
                               camera,
                               viewport_width,
                               viewport_height);
    }

    openride_ormap_renderer_draw_layer_legacy(renderer,
                                               camera,
                                               viewport_width,
                                               viewport_height,
                                               layer);

    if (v4 && layer == OPENRIDE_ORMAP_RENDER_LAYER_MASKS) {
        /* High-resolution semantic built-up mask replaces v3 contour triangles. */
        v4_draw_detail_builtup(renderer,
                               camera,
                               viewport_width,
                               viewport_height);
    }
}

void openride_ormap_renderer_draw(OpenRideORMapRenderer *renderer,
                                  const OpenRideMapCamera *camera,
                                  int viewport_width,
                                  int viewport_height)
{
    if (!renderer || !camera || viewport_width <= 0 || viewport_height <= 0) {
        return;
    }

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
        openride_ormap_renderer_draw_layer(
            renderer,
            camera,
            viewport_width,
            viewport_height,
            (OpenRideORMapRenderLayer)layer);
    }
}
