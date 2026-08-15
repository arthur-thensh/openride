#include "openride/ormap.h"
#include "openride/ormap_landcover_mesh.h"
#include "openride/osm_import.h"

#include <sqlite3.h>
#include <zlib.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define V4_PI 3.14159265358979323846
#define V4_MASK_LAYER_BYTES \
    (((OPENRIDE_ORMAP_MASK_GRID * OPENRIDE_ORMAP_MASK_GRID) + 7U) / 8U)
#define V4_COARSE_GRID 128U
#define V4_COARSE_LAYER_BYTES ((V4_COARSE_GRID * V4_COARSE_GRID + 7U) / 8U)
#define V4_AREA_RECORD_SIZE 14U

typedef struct V4UrbanMaskBucket {
    uint64_t key;
    unsigned char bits[V4_MASK_LAYER_BYTES];
    unsigned char used;
} V4UrbanMaskBucket;

typedef struct V4UrbanMaskMap {
    V4UrbanMaskBucket *buckets;
    uint32_t capacity;
    uint32_t count;
} V4UrbanMaskMap;

typedef struct V4CoarseBucket {
    uint64_t key;
    unsigned char bits[V4_COARSE_LAYER_BYTES];
    unsigned char used;
} V4CoarseBucket;

typedef struct V4CoarseMap {
    V4CoarseBucket *buckets;
    uint32_t capacity;
    uint32_t count;
} V4CoarseMap;

typedef struct V4BuildContext {
    V4UrbanMaskMap urban;
    V4UrbanMaskMap green;
} V4BuildContext;

bool openride_ormap_build_legacy(const char *pbf_path,
                                 const char *routing_graph_path,
                                 const char *places_database_path,
                                 const char *output_path,
                                 const char *region_name,
                                 OpenRideORMapBuildStats *stats_out,
                                 char *error,
                                 size_t error_size);

static void v4_set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "unknown error");
}

static uint16_t v4_read_u16_le(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8U);
}

static uint32_t v4_read_u32_le(const unsigned char *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8U)
        | ((uint32_t)p[2] << 16U)
        | ((uint32_t)p[3] << 24U);
}

static void v4_write_u16_le(unsigned char *p, uint16_t value)
{
    p[0] = (unsigned char)(value & 0xffU);
    p[1] = (unsigned char)((value >> 8U) & 0xffU);
}

static void v4_write_u32_le(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)(value & 0xffU);
    p[1] = (unsigned char)((value >> 8U) & 0xffU);
    p[2] = (unsigned char)((value >> 16U) & 0xffU);
    p[3] = (unsigned char)((value >> 24U) & 0xffU);
}

static uint64_t v4_tile_key(int x, int y)
{
    return ((uint64_t)(uint32_t)x << 32U) | (uint32_t)y;
}

static void v4_decode_tile_key(uint64_t key, int *x, int *y)
{
    if (x) *x = (int)(uint32_t)(key >> 32U);
    if (y) *y = (int)(uint32_t)key;
}

static uint32_t v4_hash64(uint64_t value)
{
    value ^= value >> 33U;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33U;
    value *= UINT64_C(0xc4ceb9fe1a85ec53);
    value ^= value >> 33U;
    return (uint32_t)(value ^ (value >> 32U));
}

static bool v4_urban_rehash(V4UrbanMaskMap *map, uint32_t capacity)
{
    V4UrbanMaskBucket *buckets = calloc(capacity, sizeof(*buckets));
    if (!buckets) return false;
    for (uint32_t i = 0U; i < map->capacity; ++i) {
        V4UrbanMaskBucket old = map->buckets[i];
        if (!old.used) continue;
        uint32_t slot = v4_hash64(old.key) & (capacity - 1U);
        while (buckets[slot].used) slot = (slot + 1U) & (capacity - 1U);
        buckets[slot] = old;
    }
    free(map->buckets);
    map->buckets = buckets;
    map->capacity = capacity;
    return true;
}

static V4UrbanMaskBucket *v4_urban_get(V4UrbanMaskMap *map,
                                       int x,
                                       int y,
                                       bool create)
{
    if (!map) return NULL;
    if (map->capacity == 0U) {
        if (!create || !v4_urban_rehash(map, 1024U)) return NULL;
    }
    if (create && (map->count + 1U) * 10U >= map->capacity * 7U) {
        if (map->capacity > UINT32_MAX / 2U
            || !v4_urban_rehash(map, map->capacity * 2U)) {
            return NULL;
        }
    }
    const uint64_t key = v4_tile_key(x, y);
    uint32_t slot = v4_hash64(key) & (map->capacity - 1U);
    while (map->buckets[slot].used) {
        if (map->buckets[slot].key == key) return &map->buckets[slot];
        slot = (slot + 1U) & (map->capacity - 1U);
    }
    if (!create) return NULL;
    map->buckets[slot].used = 1U;
    map->buckets[slot].key = key;
    ++map->count;
    return &map->buckets[slot];
}

static void v4_urban_destroy(V4UrbanMaskMap *map)
{
    if (!map) return;
    free(map->buckets);
    memset(map, 0, sizeof(*map));
}

static bool v4_coarse_rehash(V4CoarseMap *map, uint32_t capacity)
{
    V4CoarseBucket *buckets = calloc(capacity, sizeof(*buckets));
    if (!buckets) return false;
    for (uint32_t i = 0U; i < map->capacity; ++i) {
        V4CoarseBucket old = map->buckets[i];
        if (!old.used) continue;
        uint32_t slot = v4_hash64(old.key) & (capacity - 1U);
        while (buckets[slot].used) slot = (slot + 1U) & (capacity - 1U);
        buckets[slot] = old;
    }
    free(map->buckets);
    map->buckets = buckets;
    map->capacity = capacity;
    return true;
}

static V4CoarseBucket *v4_coarse_get(V4CoarseMap *map,
                                     int x,
                                     int y,
                                     bool create)
{
    if (!map) return NULL;
    if (map->capacity == 0U) {
        if (!create || !v4_coarse_rehash(map, 256U)) return NULL;
    }
    if (create && (map->count + 1U) * 10U >= map->capacity * 7U) {
        if (map->capacity > UINT32_MAX / 2U
            || !v4_coarse_rehash(map, map->capacity * 2U)) {
            return NULL;
        }
    }
    const uint64_t key = v4_tile_key(x, y);
    uint32_t slot = v4_hash64(key) & (map->capacity - 1U);
    while (map->buckets[slot].used) {
        if (map->buckets[slot].key == key) return &map->buckets[slot];
        slot = (slot + 1U) & (map->capacity - 1U);
    }
    if (!create) return NULL;
    map->buckets[slot].used = 1U;
    map->buckets[slot].key = key;
    ++map->count;
    return &map->buckets[slot];
}

static void v4_coarse_destroy(V4CoarseMap *map)
{
    if (!map) return;
    free(map->buckets);
    memset(map, 0, sizeof(*map));
}

static bool v4_bit_get(const unsigned char *bits, uint32_t index)
{
    return bits
        && (bits[index >> 3U] & (unsigned char)(1U << (index & 7U))) != 0U;
}

static void v4_bit_set(unsigned char *bits, uint32_t index)
{
    bits[index >> 3U] |= (unsigned char)(1U << (index & 7U));
}

static double v4_mercator_x(double lon)
{
    return (lon + 180.0) / 360.0;
}

static double v4_mercator_y(double lat)
{
    if (lat > 85.05112878) lat = 85.05112878;
    if (lat < -85.05112878) lat = -85.05112878;
    const double rad = lat * V4_PI / 180.0;
    return (1.0 - asinh(tan(rad)) / V4_PI) * 0.5;
}

static bool v4_set_global(V4UrbanMaskMap *map, int64_t gx, int64_t gy)
{
    if (!map || gx < 0 || gy < 0) return true;
    const int64_t global_cells =
        (int64_t)(1U << OPENRIDE_ORMAP_MASK_ZOOM) * OPENRIDE_ORMAP_MASK_GRID;
    if (gx >= global_cells || gy >= global_cells) return true;
    const int tx = (int)(gx / OPENRIDE_ORMAP_MASK_GRID);
    const int ty = (int)(gy / OPENRIDE_ORMAP_MASK_GRID);
    V4UrbanMaskBucket *bucket = v4_urban_get(map, tx, ty, true);
    if (!bucket) return false;
    const uint32_t x = (uint32_t)(gx % OPENRIDE_ORMAP_MASK_GRID);
    const uint32_t y = (uint32_t)(gy % OPENRIDE_ORMAP_MASK_GRID);
    v4_bit_set(bucket->bits, y * OPENRIDE_ORMAP_MASK_GRID + x);
    return true;
}

static bool v4_get_global(const V4UrbanMaskMap *map, int64_t gx, int64_t gy)
{
    if (!map || map->capacity == 0U || gx < 0 || gy < 0) return false;
    const int64_t global_cells =
        (int64_t)(1U << OPENRIDE_ORMAP_MASK_ZOOM) * OPENRIDE_ORMAP_MASK_GRID;
    if (gx >= global_cells || gy >= global_cells) return false;
    const int tx = (int)(gx / OPENRIDE_ORMAP_MASK_GRID);
    const int ty = (int)(gy / OPENRIDE_ORMAP_MASK_GRID);
    const uint64_t key = v4_tile_key(tx, ty);
    uint32_t slot = v4_hash64(key) & (map->capacity - 1U);
    while (map->buckets[slot].used) {
        const V4UrbanMaskBucket *bucket = &map->buckets[slot];
        if (bucket->key == key) {
            const uint32_t x = (uint32_t)(gx % OPENRIDE_ORMAP_MASK_GRID);
            const uint32_t y = (uint32_t)(gy % OPENRIDE_ORMAP_MASK_GRID);
            return v4_bit_get(bucket->bits,
                              y * OPENRIDE_ORMAP_MASK_GRID + x);
        }
        slot = (slot + 1U) & (map->capacity - 1U);
    }
    return false;
}

static int v4_compare_double(const void *left, const void *right)
{
    const double a = *(const double *)left;
    const double b = *(const double *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

static bool v4_rasterize_polygon(V4UrbanMaskMap *map,
                                 const double *latitudes,
                                 const double *longitudes,
                                 uint32_t point_count)
{
    if (!map || !latitudes || !longitudes || point_count < 4U) return true;
    double *xs = malloc((size_t)point_count * sizeof(*xs));
    double *ys = malloc((size_t)point_count * sizeof(*ys));
    double *intersections = malloc((size_t)point_count * sizeof(*intersections));
    if (!xs || !ys || !intersections) {
        free(xs);
        free(ys);
        free(intersections);
        return false;
    }

    const double cells = (double)(1U << OPENRIDE_ORMAP_MASK_ZOOM)
        * OPENRIDE_ORMAP_MASK_GRID;
    double min_y = HUGE_VAL;
    double max_y = -HUGE_VAL;
    for (uint32_t i = 0U; i < point_count; ++i) {
        xs[i] = v4_mercator_x(longitudes[i]) * cells;
        ys[i] = v4_mercator_y(latitudes[i]) * cells;
        if (ys[i] < min_y) min_y = ys[i];
        if (ys[i] > max_y) max_y = ys[i];
    }

    int64_t row0 = (int64_t)floor(min_y);
    int64_t row1 = (int64_t)floor(max_y);
    const int64_t max_cell =
        (int64_t)(1U << OPENRIDE_ORMAP_MASK_ZOOM) * OPENRIDE_ORMAP_MASK_GRID - 1;
    if (row0 < 0) row0 = 0;
    if (row1 > max_cell) row1 = max_cell;

    bool ok = true;
    for (int64_t gy = row0; gy <= row1 && ok; ++gy) {
        const double scan_y = (double)gy + 0.5;
        uint32_t count = 0U;
        for (uint32_t i = 0U, j = point_count - 1U;
             i < point_count;
             j = i++) {
            const double y0 = ys[j];
            const double y1 = ys[i];
            if ((y0 > scan_y) == (y1 > scan_y)) continue;
            intersections[count++] =
                xs[j] + (scan_y - y0) * (xs[i] - xs[j]) / (y1 - y0);
        }
        if (count < 2U) continue;
        qsort(intersections, count, sizeof(*intersections), v4_compare_double);
        for (uint32_t i = 0U; i + 1U < count; i += 2U) {
            double left = intersections[i];
            double right = intersections[i + 1U];
            if (right < left) {
                const double temp = left;
                left = right;
                right = temp;
            }
            int64_t gx0 = (int64_t)ceil(left - 0.5);
            int64_t gx1 = (int64_t)floor(right - 0.5);
            if (gx0 < 0) gx0 = 0;
            if (gx1 > max_cell) gx1 = max_cell;
            for (int64_t gx = gx0; gx <= gx1; ++gx) {
                if (!v4_set_global(map, gx, gy)) {
                    ok = false;
                    break;
                }
            }
        }
    }

    free(xs);
    free(ys);
    free(intersections);
    return ok;
}

static bool v4_collect_landcover(OpenRideOSMMapFeatureKind kind,
                         const double *latitudes,
                         const double *longitudes,
                         uint32_t point_count,
                         void *userdata)
{
    V4BuildContext *context = userdata;
    if (!context || point_count == 0U) return true;

    V4UrbanMaskMap *target = NULL;
    if (kind == OPENRIDE_OSM_MAP_FEATURE_BUILTUP_AREA) {
        target = &context->urban;
    } else if (kind == OPENRIDE_OSM_MAP_FEATURE_FOREST_AREA) {
        target = &context->green;
    } else {
        return true;
    }

    /* Individual buildings arrive as a single representative point. They are
     * useful to consolidate urban fabric, but greenery must always be an area. */
    if (point_count == 1U) {
        if (kind != OPENRIDE_OSM_MAP_FEATURE_BUILTUP_AREA) return true;
        const double cells = (double)(1U << OPENRIDE_ORMAP_MASK_ZOOM)
  * OPENRIDE_ORMAP_MASK_GRID;
        const int64_t gx =
  (int64_t)floor(v4_mercator_x(longitudes[0]) * cells);
        const int64_t gy =
  (int64_t)floor(v4_mercator_y(latitudes[0]) * cells);
        return v4_set_global(target, gx, gy);
    }

    return v4_rasterize_polygon(target,
                      latitudes,
                      longitudes,
                      point_count);
}

static bool v4_merge_urban(V4UrbanMaskMap *map)
{
    if (!map || map->capacity == 0U) return true;
    V4UrbanMaskMap dilated = {0};
    for (uint32_t b = 0U; b < map->capacity; ++b) {
        const V4UrbanMaskBucket *bucket = &map->buckets[b];
        if (!bucket->used) continue;
        int tx = 0;
        int ty = 0;
        v4_decode_tile_key(bucket->key, &tx, &ty);
        for (uint32_t y = 0U; y < OPENRIDE_ORMAP_MASK_GRID; ++y) {
            for (uint32_t x = 0U; x < OPENRIDE_ORMAP_MASK_GRID; ++x) {
                if (!v4_bit_get(bucket->bits,
                                y * OPENRIDE_ORMAP_MASK_GRID + x)) {
                    continue;
                }
                const int64_t gx =
                    (int64_t)tx * OPENRIDE_ORMAP_MASK_GRID + x;
                const int64_t gy =
                    (int64_t)ty * OPENRIDE_ORMAP_MASK_GRID + y;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!v4_set_global(&dilated, gx + dx, gy + dy)) {
                            v4_urban_destroy(&dilated);
                            return false;
                        }
                    }
                }
            }
        }
    }

    V4UrbanMaskMap closed = {0};
    for (uint32_t b = 0U; b < dilated.capacity; ++b) {
        const V4UrbanMaskBucket *bucket = &dilated.buckets[b];
        if (!bucket->used) continue;
        int tx = 0;
        int ty = 0;
        v4_decode_tile_key(bucket->key, &tx, &ty);
        for (uint32_t y = 0U; y < OPENRIDE_ORMAP_MASK_GRID; ++y) {
            for (uint32_t x = 0U; x < OPENRIDE_ORMAP_MASK_GRID; ++x) {
                if (!v4_bit_get(bucket->bits,
                                y * OPENRIDE_ORMAP_MASK_GRID + x)) {
                    continue;
                }
                const int64_t gx =
                    (int64_t)tx * OPENRIDE_ORMAP_MASK_GRID + x;
                const int64_t gy =
                    (int64_t)ty * OPENRIDE_ORMAP_MASK_GRID + y;
                bool keep = true;
                for (int dy = -1; dy <= 1 && keep; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!v4_get_global(&dilated, gx + dx, gy + dy)) {
                            keep = false;
                            break;
                        }
                    }
                }
                if (keep && !v4_set_global(&closed, gx, gy)) {
                    v4_urban_destroy(&dilated);
                    v4_urban_destroy(&closed);
                    return false;
                }
            }
        }
    }
    v4_urban_destroy(&dilated);
    v4_urban_destroy(map);
    *map = closed;
    return true;
}

static bool v4_filter_sparse(V4UrbanMaskMap *map)
{
    if (!map || map->capacity == 0U) return true;
    V4UrbanMaskMap filtered = {0};
    for (uint32_t b = 0U; b < map->capacity; ++b) {
        const V4UrbanMaskBucket *bucket = &map->buckets[b];
        if (!bucket->used) continue;
        int tx = 0;
        int ty = 0;
        v4_decode_tile_key(bucket->key, &tx, &ty);
        for (uint32_t y = 0U; y < OPENRIDE_ORMAP_MASK_GRID; ++y) {
            for (uint32_t x = 0U; x < OPENRIDE_ORMAP_MASK_GRID; ++x) {
                if (!v4_bit_get(bucket->bits,
                                y * OPENRIDE_ORMAP_MASK_GRID + x)) {
                    continue;
                }
                const int64_t gx =
                    (int64_t)tx * OPENRIDE_ORMAP_MASK_GRID + x;
                const int64_t gy =
                    (int64_t)ty * OPENRIDE_ORMAP_MASK_GRID + y;
                unsigned neighbours = 0U;
                for (int dy = -1; dy <= 1 && neighbours < 3U; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (v4_get_global(map, gx + dx, gy + dy)) ++neighbours;
                        if (neighbours >= 3U) break;
                    }
                }
                if (neighbours >= 3U
                    && !v4_set_global(&filtered, gx, gy)) {
                    v4_urban_destroy(&filtered);
                    return false;
                }
            }
        }
    }
    v4_urban_destroy(map);
    *map = filtered;
    return true;
}

static bool v4_coarse_set(V4CoarseMap *map,
                          int64_t global_x,
                          int64_t global_y)
{
    if (!map || global_x < 0 || global_y < 0) return true;
    const int64_t global_cells =
        (int64_t)(1U << OPENRIDE_ORMAP_AREA_COARSE_ZOOM) * V4_COARSE_GRID;
    if (global_x >= global_cells || global_y >= global_cells) return true;
    const int tx = (int)(global_x / V4_COARSE_GRID);
    const int ty = (int)(global_y / V4_COARSE_GRID);
    V4CoarseBucket *bucket = v4_coarse_get(map, tx, ty, true);
    if (!bucket) return false;
    const uint32_t x = (uint32_t)(global_x % V4_COARSE_GRID);
    const uint32_t y = (uint32_t)(global_y % V4_COARSE_GRID);
    v4_bit_set(bucket->bits, y * V4_COARSE_GRID + x);
    return true;
}

static bool v4_build_coarse_map(const V4UrbanMaskMap *source,
                      V4CoarseMap *coarse)
{
    if (!source || !coarse) return false;
    /* z16 * 32 = z21 semantic cells; z11 * 128 = z18 overview cells. */
    const unsigned shift = 3U;
    for (uint32_t b = 0U; b < source->capacity; ++b) {
        const V4UrbanMaskBucket *bucket = &source->buckets[b];
        if (!bucket->used) continue;
        int tx = 0;
        int ty = 0;
        v4_decode_tile_key(bucket->key, &tx, &ty);
        for (uint32_t y = 0U; y < OPENRIDE_ORMAP_MASK_GRID; ++y) {
  for (uint32_t x = 0U; x < OPENRIDE_ORMAP_MASK_GRID; ++x) {
      if (!v4_bit_get(bucket->bits,
                      y * OPENRIDE_ORMAP_MASK_GRID + x)) {
          continue;
      }
      const int64_t high_x =
          (int64_t)tx * OPENRIDE_ORMAP_MASK_GRID + x;
      const int64_t high_y =
          (int64_t)ty * OPENRIDE_ORMAP_MASK_GRID + y;
      if (!v4_coarse_set(coarse,
                         high_x >> shift,
                         high_y >> shift)) {
          return false;
      }
  }
        }
    }
    return true;
}

static bool v4_coarse_get_global(const V4CoarseMap *map,
                       int64_t gx,
                       int64_t gy)
{
    if (!map || map->capacity == 0U || gx < 0 || gy < 0) return false;
    const int64_t global_cells =
        (int64_t)(1U << OPENRIDE_ORMAP_AREA_COARSE_ZOOM) * V4_COARSE_GRID;
    if (gx >= global_cells || gy >= global_cells) return false;
    const int tx = (int)(gx / V4_COARSE_GRID);
    const int ty = (int)(gy / V4_COARSE_GRID);
    const uint64_t key = v4_tile_key(tx, ty);
    uint32_t slot = v4_hash64(key) & (map->capacity - 1U);
    while (map->buckets[slot].used) {
        const V4CoarseBucket *bucket = &map->buckets[slot];
        if (bucket->key == key) {
  const uint32_t x = (uint32_t)(gx % V4_COARSE_GRID);
  const uint32_t y = (uint32_t)(gy % V4_COARSE_GRID);
  return v4_bit_get(bucket->bits, y * V4_COARSE_GRID + x);
        }
        slot = (slot + 1U) & (map->capacity - 1U);
    }
    return false;
}

static bool v4_smooth_coarse(V4CoarseMap *map)
{
    if (!map || map->capacity == 0U) return true;
    V4CoarseMap filtered = {0};
    for (uint32_t b = 0U; b < map->capacity; ++b) {
        const V4CoarseBucket *bucket = &map->buckets[b];
        if (!bucket->used) continue;
        int tx = 0;
        int ty = 0;
        v4_decode_tile_key(bucket->key, &tx, &ty);
        for (uint32_t y = 0U; y < V4_COARSE_GRID; ++y) {
  for (uint32_t x = 0U; x < V4_COARSE_GRID; ++x) {
      if (!v4_bit_get(bucket->bits, y * V4_COARSE_GRID + x)) continue;
      const int64_t gx = (int64_t)tx * V4_COARSE_GRID + x;
      const int64_t gy = (int64_t)ty * V4_COARSE_GRID + y;
      unsigned neighbours = 0U;
      for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
              if (v4_coarse_get_global(map, gx + dx, gy + dy)) {
                  ++neighbours;
              }
          }
      }
      /* Majority-ish cleanup: preserve coherent edges while dropping
       * one-cell teeth and isolated raster artefacts. */
      if (neighbours >= 3U
          && !v4_coarse_set(&filtered, gx, gy)) {
          v4_coarse_destroy(&filtered);
          return false;
      }
  }
        }
    }
    v4_coarse_destroy(map);
    *map = filtered;
    return true;
}

static bool v4_decompress_blob(const void *blob,
                               int blob_size,
                               unsigned char **raw,
                               size_t *raw_size)
{
    if (!blob || blob_size < 5 || !raw || !raw_size) return false;
    const unsigned char *bytes = blob;
    const uint32_t expected = v4_read_u32_le(bytes);
    if (expected == 0U || expected > 64U * 1024U * 1024U) return false;
    unsigned char *output = malloc(expected);
    if (!output) return false;
    uLongf size = expected;
    if (uncompress(output,
                   &size,
                   bytes + 4U,
                   (uLong)(blob_size - 4)) != Z_OK
        || size != expected) {
        free(output);
        return false;
    }
    *raw = output;
    *raw_size = expected;
    return true;
}

static bool v4_compress_blob(const unsigned char *raw,
                             size_t raw_size,
                             unsigned char **blob,
                             size_t *blob_size)
{
    if (!raw || !blob || !blob_size || raw_size > UINT32_MAX) return false;
    uLongf capacity = compressBound((uLong)raw_size);
    unsigned char *output = malloc((size_t)capacity + 4U);
    if (!output) return false;
    v4_write_u32_le(output, (uint32_t)raw_size);
    if (compress2(output + 4U,
                  &capacity,
                  raw,
                  (uLong)raw_size,
                  Z_BEST_SPEED) != Z_OK) {
        free(output);
        return false;
    }
    *blob = output;
    *blob_size = (size_t)capacity + 4U;
    return true;
}

static uint16_t v4_quantize_area_local(double local)
{
    const double buffer = OPENRIDE_ORMAP_AREA_BUFFER_FRACTION;
    const double span = 1.0 + 2.0 * buffer;
    double normalized = (local + buffer) / span;
    if (normalized < 0.0) normalized = 0.0;
    if (normalized > 1.0) normalized = 1.0;
    return (uint16_t)llround(normalized * 65535.0);
}

static void v4_write_area_record(unsigned char *record,
                                 double x1,
                                 double y1,
                                 double x2,
                                 double y2,
                                 double x3,
                                 double y3,
                                 uint8_t kind)
{
    v4_write_u16_le(record + 0U, v4_quantize_area_local(x1));
    v4_write_u16_le(record + 2U, v4_quantize_area_local(y1));
    v4_write_u16_le(record + 4U, v4_quantize_area_local(x2));
    v4_write_u16_le(record + 6U, v4_quantize_area_local(y2));
    v4_write_u16_le(record + 8U, v4_quantize_area_local(x3));
    v4_write_u16_le(record + 10U, v4_quantize_area_local(y3));
    record[12] = kind;
    record[13] = 0U;
}

static bool v4_filter_detail_builtup(sqlite3 *db,
                                     char *error,
                                     size_t error_size)
{
    sqlite3_stmt *select = NULL;
    sqlite3_stmt *update = NULL;
    sqlite3_stmt *remove = NULL;
    bool ok = sqlite3_prepare_v2(
        db,
        "SELECT tile_column,tile_row,tile_data FROM area_tiles WHERE zoom_level=?1",
        -1,
        &select,
        NULL) == SQLITE_OK
        && sqlite3_prepare_v2(
            db,
            "UPDATE area_tiles SET tile_data=?1 WHERE zoom_level=?2 AND tile_column=?3 AND tile_row=?4",
            -1,
            &update,
            NULL) == SQLITE_OK
        && sqlite3_prepare_v2(
            db,
            "DELETE FROM area_tiles WHERE zoom_level=?1 AND tile_column=?2 AND tile_row=?3",
            -1,
            &remove,
            NULL) == SQLITE_OK;
    if (!ok) {
        v4_set_error(error, error_size, sqlite3_errmsg(db));
        goto done;
    }

    sqlite3_bind_int(select, 1, OPENRIDE_ORMAP_AREA_DETAIL_ZOOM);
    while (ok && sqlite3_step(select) == SQLITE_ROW) {
        const int tx = sqlite3_column_int(select, 0);
        const int ty = sqlite3_column_int(select, 1);
        const void *blob = sqlite3_column_blob(select, 2);
        const int blob_size = sqlite3_column_bytes(select, 2);
        unsigned char *raw = NULL;
        size_t raw_size = 0U;
        if (!v4_decompress_blob(blob, blob_size, &raw, &raw_size)
            || raw_size < 12U
            || memcmp(raw, "ORA1", 4U) != 0
            || v4_read_u16_le(raw + 6U) != V4_AREA_RECORD_SIZE) {
            free(raw);
            ok = false;
            v4_set_error(error, error_size, "invalid v3 detail area tile");
            break;
        }
        const uint32_t count = v4_read_u32_le(raw + 8U);
        if (12U + (size_t)count * V4_AREA_RECORD_SIZE > raw_size) {
            free(raw);
            ok = false;
            v4_set_error(error, error_size, "truncated v3 detail area tile");
            break;
        }
        uint32_t kept = 0U;
        for (uint32_t i = 0U; i < count; ++i) {
            const unsigned char *source = raw + 12U + (size_t)i * V4_AREA_RECORD_SIZE;
            if (source[12] == OPENRIDE_ORMAP_AREA_BUILTUP) continue;
            unsigned char *target = raw + 12U + (size_t)kept * V4_AREA_RECORD_SIZE;
            if (target != source) memmove(target, source, V4_AREA_RECORD_SIZE);
            ++kept;
        }
        if (kept == count) {
            free(raw);
            continue;
        }
        if (kept == 0U) {
            sqlite3_reset(remove);
            sqlite3_clear_bindings(remove);
            sqlite3_bind_int(remove, 1, OPENRIDE_ORMAP_AREA_DETAIL_ZOOM);
            sqlite3_bind_int(remove, 2, tx);
            sqlite3_bind_int(remove, 3, ty);
            ok = sqlite3_step(remove) == SQLITE_DONE;
            free(raw);
            if (!ok) v4_set_error(error, error_size, sqlite3_errmsg(db));
            continue;
        }

        v4_write_u32_le(raw + 8U, kept);
        const size_t filtered_size = 12U + (size_t)kept * V4_AREA_RECORD_SIZE;
        unsigned char *compressed = NULL;
        size_t compressed_size = 0U;
        ok = v4_compress_blob(raw,
                              filtered_size,
                              &compressed,
                              &compressed_size);
        free(raw);
        if (!ok) {
            v4_set_error(error, error_size, "unable to recompress detail area tile");
            break;
        }
        sqlite3_reset(update);
        sqlite3_clear_bindings(update);
        sqlite3_bind_blob(update,
                          1,
                          compressed,
                          (int)compressed_size,
                          SQLITE_TRANSIENT);
        sqlite3_bind_int(update, 2, OPENRIDE_ORMAP_AREA_DETAIL_ZOOM);
        sqlite3_bind_int(update, 3, tx);
        sqlite3_bind_int(update, 4, ty);
        ok = sqlite3_step(update) == SQLITE_DONE;
        free(compressed);
        if (!ok) v4_set_error(error, error_size, sqlite3_errmsg(db));
    }

done:
    if (select) sqlite3_finalize(select);
    if (update) sqlite3_finalize(update);
    if (remove) sqlite3_finalize(remove);
    return ok;
}

static bool v4_append_coarse_layer(sqlite3 *db,
                         const V4CoarseMap *coarse,
                         uint8_t kind,
                         char *error,
                         size_t error_size)
{
    sqlite3_stmt *select = NULL;
    sqlite3_stmt *upsert = NULL;
    bool ok = sqlite3_prepare_v2(
        db,
        "SELECT tile_data FROM area_tiles WHERE zoom_level=?1 AND tile_column=?2 AND tile_row=?3",
        -1,
        &select,
        NULL) == SQLITE_OK
        && sqlite3_prepare_v2(
            db,
            "INSERT INTO area_tiles(zoom_level,tile_column,tile_row,tile_data) VALUES(?1,?2,?3,?4) "
            "ON CONFLICT(zoom_level,tile_column,tile_row) DO UPDATE SET tile_data=excluded.tile_data",
            -1,
            &upsert,
            NULL) == SQLITE_OK;
    if (!ok) {
        v4_set_error(error, error_size, sqlite3_errmsg(db));
        goto done;
    }

    for (uint32_t b = 0U; b < coarse->capacity && ok; ++b) {
        const V4CoarseBucket *bucket = &coarse->buckets[b];
        if (!bucket->used) continue;
        int tx = 0;
        int ty = 0;
        v4_decode_tile_key(bucket->key, &tx, &ty);

        OpenRideORMapLandcoverMesh mesh = {0};
        const double tolerance = kind == OPENRIDE_ORMAP_AREA_GREEN ? 1.20 : 0.90;
        if (!openride_ormap_landcover_mesh_build(bucket->bits,
                                                 V4_COARSE_GRID,
                                                 tolerance,
                                                 &mesh)) {
            ok = false;
            v4_set_error(error, error_size, "unable to contour coarse landcover tile");
            break;
        }
        if (mesh.triangle_count == 0U) {
            openride_ormap_landcover_mesh_destroy(&mesh);
            continue;
        }

        unsigned char *old_raw = NULL;
        size_t old_raw_size = 0U;
        uint32_t old_count = 0U;
        sqlite3_reset(select);
        sqlite3_clear_bindings(select);
        sqlite3_bind_int(select, 1, OPENRIDE_ORMAP_AREA_COARSE_ZOOM);
        sqlite3_bind_int(select, 2, tx);
        sqlite3_bind_int(select, 3, ty);
        const int select_rc = sqlite3_step(select);
        if (select_rc == SQLITE_ROW) {
            if (!v4_decompress_blob(sqlite3_column_blob(select, 0),
                                    sqlite3_column_bytes(select, 0),
                                    &old_raw,
                                    &old_raw_size)
                || old_raw_size < 12U
                || memcmp(old_raw, "ORA1", 4U) != 0
                || v4_read_u16_le(old_raw + 6U) != V4_AREA_RECORD_SIZE) {
                free(old_raw);
                openride_ormap_landcover_mesh_destroy(&mesh);
                ok = false;
                v4_set_error(error, error_size, "invalid coarse area tile");
                break;
            }
            old_count = v4_read_u32_le(old_raw + 8U);
            if (12U + (size_t)old_count * V4_AREA_RECORD_SIZE > old_raw_size) {
                free(old_raw);
                openride_ormap_landcover_mesh_destroy(&mesh);
                ok = false;
                v4_set_error(error, error_size, "truncated coarse area tile");
                break;
            }
        } else if (select_rc != SQLITE_DONE) {
            openride_ormap_landcover_mesh_destroy(&mesh);
            ok = false;
            v4_set_error(error, error_size, sqlite3_errmsg(db));
            break;
        }

        if (old_count > UINT32_MAX - mesh.triangle_count) {
            free(old_raw);
            openride_ormap_landcover_mesh_destroy(&mesh);
            ok = false;
            v4_set_error(error, error_size, "coarse landcover area overflow");
            break;
        }
        const uint32_t total_count = old_count + mesh.triangle_count;
        const size_t raw_size = 12U + (size_t)total_count * V4_AREA_RECORD_SIZE;
        unsigned char *raw = malloc(raw_size);
        if (!raw) {
            free(old_raw);
            openride_ormap_landcover_mesh_destroy(&mesh);
            ok = false;
            v4_set_error(error, error_size, "out of memory building coarse landcover tile");
            break;
        }
        memcpy(raw, "ORA1", 4U);
        v4_write_u16_le(raw + 4U, 1U);
        v4_write_u16_le(raw + 6U, V4_AREA_RECORD_SIZE);
        v4_write_u32_le(raw + 8U, total_count);
        if (old_count > 0U) {
            memcpy(raw + 12U,
                   old_raw + 12U,
                   (size_t)old_count * V4_AREA_RECORD_SIZE);
        }
        free(old_raw);

        for (uint32_t i = 0U; i < mesh.triangle_count; ++i) {
            const OpenRideORMapLandcoverTriangle *triangle = &mesh.triangles[i];
            unsigned char *record = raw + 12U
                + (size_t)(old_count + i) * V4_AREA_RECORD_SIZE;
            v4_write_area_record(record,
                                 triangle->x0,
                                 triangle->y0,
                                 triangle->x1,
                                 triangle->y1,
                                 triangle->x2,
                                 triangle->y2,
                                 kind);
        }
        openride_ormap_landcover_mesh_destroy(&mesh);

        unsigned char *compressed = NULL;
        size_t compressed_size = 0U;
        ok = v4_compress_blob(raw, raw_size, &compressed, &compressed_size);
        free(raw);
        if (!ok) {
            v4_set_error(error, error_size, "unable to compress coarse landcover tile");
            break;
        }
        sqlite3_reset(upsert);
        sqlite3_clear_bindings(upsert);
        sqlite3_bind_int(upsert, 1, OPENRIDE_ORMAP_AREA_COARSE_ZOOM);
        sqlite3_bind_int(upsert, 2, tx);
        sqlite3_bind_int(upsert, 3, ty);
        sqlite3_bind_blob(upsert,
                          4,
                          compressed,
                          (int)compressed_size,
                          SQLITE_TRANSIENT);
        ok = sqlite3_step(upsert) == SQLITE_DONE;
        free(compressed);
        if (!ok) v4_set_error(error, error_size, sqlite3_errmsg(db));
    }

done:
    if (select) sqlite3_finalize(select);
    if (upsert) sqlite3_finalize(upsert);
    return ok;
}

static bool v4_write_urban_masks(sqlite3 *db,
                                 const V4UrbanMaskMap *urban,
                                 char *error,
                                 size_t error_size)
{
    sqlite3_stmt *select = NULL;
    sqlite3_stmt *upsert = NULL;
    bool ok = sqlite3_prepare_v2(
        db,
        "SELECT tile_data FROM mask_tiles WHERE zoom_level=?1 AND tile_column=?2 AND tile_row=?3",
        -1,
        &select,
        NULL) == SQLITE_OK
        && sqlite3_prepare_v2(
            db,
            "INSERT INTO mask_tiles(zoom_level,tile_column,tile_row,tile_data) VALUES(?1,?2,?3,?4) "
            "ON CONFLICT(zoom_level,tile_column,tile_row) DO UPDATE SET tile_data=excluded.tile_data",
            -1,
            &upsert,
            NULL) == SQLITE_OK;
    if (!ok) {
        v4_set_error(error, error_size, sqlite3_errmsg(db));
        goto done;
    }

    for (uint32_t b = 0U; b < urban->capacity && ok; ++b) {
        const V4UrbanMaskBucket *bucket = &urban->buckets[b];
        if (!bucket->used) continue;
        int tx = 0;
        int ty = 0;
        v4_decode_tile_key(bucket->key, &tx, &ty);

        const size_t expected_raw_size = 12U + V4_MASK_LAYER_BYTES * 3U;
        unsigned char raw[12U + V4_MASK_LAYER_BYTES * 3U];
        memset(raw, 0, sizeof(raw));
        memcpy(raw, "ORM1", 4U);
        v4_write_u16_le(raw + 4U, 1U);
        raw[6] = OPENRIDE_ORMAP_MASK_GRID;
        raw[7] = 3U;
        v4_write_u32_le(raw + 8U, V4_MASK_LAYER_BYTES);

        sqlite3_reset(select);
        sqlite3_clear_bindings(select);
        sqlite3_bind_int(select, 1, OPENRIDE_ORMAP_MASK_ZOOM);
        sqlite3_bind_int(select, 2, tx);
        sqlite3_bind_int(select, 3, ty);
        const int select_rc = sqlite3_step(select);
        if (select_rc == SQLITE_ROW) {
            unsigned char *existing = NULL;
            size_t existing_size = 0U;
            if (v4_decompress_blob(sqlite3_column_blob(select, 0),
                                   sqlite3_column_bytes(select, 0),
                                   &existing,
                                   &existing_size)
                && existing_size == expected_raw_size
                && memcmp(existing, "ORM1", 4U) == 0
                && existing[6] == OPENRIDE_ORMAP_MASK_GRID
                && existing[7] == 3U
                && v4_read_u32_le(existing + 8U) == V4_MASK_LAYER_BYTES) {
                memcpy(raw + 12U + V4_MASK_LAYER_BYTES,
                       existing + 12U + V4_MASK_LAYER_BYTES,
                       V4_MASK_LAYER_BYTES * 2U);
            }
            free(existing);
        } else if (select_rc != SQLITE_DONE) {
            ok = false;
            v4_set_error(error, error_size, sqlite3_errmsg(db));
            break;
        }
        memcpy(raw + 12U, bucket->bits, V4_MASK_LAYER_BYTES);

        unsigned char *compressed = NULL;
        size_t compressed_size = 0U;
        ok = v4_compress_blob(raw,
                              expected_raw_size,
                              &compressed,
                              &compressed_size);
        if (!ok) {
            v4_set_error(error, error_size, "unable to compress urban mask tile");
            break;
        }
        sqlite3_reset(upsert);
        sqlite3_clear_bindings(upsert);
        sqlite3_bind_int(upsert, 1, OPENRIDE_ORMAP_MASK_ZOOM);
        sqlite3_bind_int(upsert, 2, tx);
        sqlite3_bind_int(upsert, 3, ty);
        sqlite3_bind_blob(upsert,
                          4,
                          compressed,
                          (int)compressed_size,
                          SQLITE_TRANSIENT);
        ok = sqlite3_step(upsert) == SQLITE_DONE;
        free(compressed);
        if (!ok) v4_set_error(error, error_size, sqlite3_errmsg(db));
    }

done:
    if (select) sqlite3_finalize(select);
    if (upsert) sqlite3_finalize(upsert);
    return ok;
}

static bool v4_postprocess(const char *pbf_path,
                 const char *output_path,
                 char *error,
                 size_t error_size)
{
    V4BuildContext context = {0};
    OpenRideOSMMapFeatureStats feature_stats = {0};
    if (!openride_osm_pbf_visit_map_features(pbf_path,
                                    v4_collect_landcover,
                                    &context,
                                    &feature_stats,
                                    error,
                                    error_size)) {
        v4_urban_destroy(&context.green);
        v4_urban_destroy(&context.urban);
        return false;
    }

    if (!v4_merge_urban(&context.urban)
        || !v4_filter_sparse(&context.urban)
        || !v4_merge_urban(&context.green)
        || !v4_filter_sparse(&context.green)) {
        v4_urban_destroy(&context.green);
        v4_urban_destroy(&context.urban);
        v4_set_error(error, error_size, "unable to prepare v4 landcover masks");
        return false;
    }

    V4CoarseMap coarse_urban = {0};
    V4CoarseMap coarse_green = {0};
    if (!v4_build_coarse_map(&context.urban, &coarse_urban)
        || !v4_build_coarse_map(&context.green, &coarse_green)
        || !v4_smooth_coarse(&coarse_urban)
        || !v4_smooth_coarse(&coarse_green)) {
        v4_coarse_destroy(&coarse_green);
        v4_coarse_destroy(&coarse_urban);
        v4_urban_destroy(&context.green);
        v4_urban_destroy(&context.urban);
        v4_set_error(error, error_size, "unable to prepare v4 coarse landcover");
        return false;
    }

    sqlite3 *db = NULL;
    bool ok = sqlite3_open_v2(output_path,
                    &db,
                    SQLITE_OPEN_READWRITE,
                    NULL) == SQLITE_OK;
    if (!ok) {
        v4_set_error(error,
           error_size,
           db ? sqlite3_errmsg(db) : "unable to reopen .ormap for v4");
    }
    if (ok) {
        ok = sqlite3_exec(db, "BEGIN", NULL, NULL, NULL) == SQLITE_OK;
        if (!ok) v4_set_error(error, error_size, sqlite3_errmsg(db));
    }
    if (ok) ok = v4_filter_detail_builtup(db, error, error_size);
    /* Green first, urban second: urban remains visually dominant where the
     * simplified backgrounds overlap. Water stays in the legacy vector layer. */
    if (ok) {
        ok = v4_append_coarse_layer(db,
                          &coarse_green,
                          OPENRIDE_ORMAP_AREA_GREEN,
                          error,
                          error_size);
    }
    if (ok) {
        ok = v4_append_coarse_layer(db,
                          &coarse_urban,
                          OPENRIDE_ORMAP_AREA_BUILTUP,
                          error,
                          error_size);
    }
    if (ok) ok = v4_write_urban_masks(db, &context.urban, error, error_size);
    if (ok) {
        sqlite3_stmt *metadata = NULL;
        ok = sqlite3_prepare_v2(
  db,
  "INSERT INTO metadata(name,value) VALUES('format_version',?1) "
  "ON CONFLICT(name) DO UPDATE SET value=excluded.value",
  -1,
  &metadata,
  NULL) == SQLITE_OK;
        if (ok) {
  char version[16];
  snprintf(version, sizeof(version), "%u", OPENRIDE_ORMAP_FORMAT_VERSION);
  sqlite3_bind_text(metadata, 1, version, -1, SQLITE_TRANSIENT);
  ok = sqlite3_step(metadata) == SQLITE_DONE;
        }
        if (!ok) v4_set_error(error, error_size, sqlite3_errmsg(db));
        if (metadata) sqlite3_finalize(metadata);
    }
    if (db) {
        if (ok) {
  ok = sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK;
  if (!ok) v4_set_error(error, error_size, sqlite3_errmsg(db));
        } else {
  sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        }
        sqlite3_close(db);
    }

    v4_coarse_destroy(&coarse_green);
    v4_coarse_destroy(&coarse_urban);
    v4_urban_destroy(&context.green);
    v4_urban_destroy(&context.urban);
    return ok;
}

bool openride_ormap_build(const char *pbf_path,
                          const char *routing_graph_path,
                          const char *places_database_path,
                          const char *output_path,
                          const char *region_name,
                          OpenRideORMapBuildStats *stats_out,
                          char *error,
                          size_t error_size)
{
    if (!openride_ormap_build_legacy(pbf_path,
                                     routing_graph_path,
                                     places_database_path,
                                     output_path,
                                     region_name,
                                     stats_out,
                                     error,
                                     error_size)) {
        return false;
    }

    if (!v4_postprocess(pbf_path, output_path, error, error_size)) {
        remove(output_path);
        return false;
    }

    if (stats_out) {
        /* v4 keeps semantic built-up masks instead of final contour triangles. */
        stats_out->builtup_contours = 0U;
    }
    v4_set_error(error, error_size, "");
    return true;
}
