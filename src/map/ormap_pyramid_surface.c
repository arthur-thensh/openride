#include "openride/ormap_pyramid_surface.h"

#include "ormap_pyramid_surface_internal.h"
#include "openride/osm_import.h"

#include <math.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define TILE_MAP_INITIAL_CAPACITY 256U
#define HASH_LOAD_NUMERATOR 7U
#define HASH_LOAD_DENOMINATOR 10U

typedef struct SurfaceTileBucket {
    bool used;
    uint64_t key;
    OpenRideORMapPyramidSurfaceTriangle *triangles;
    uint32_t count;
    uint32_t capacity;
} SurfaceTileBucket;

typedef struct SurfaceTileMap {
    SurfaceTileBucket *buckets;
    uint32_t capacity;
    uint32_t count;
} SurfaceTileMap;

typedef struct SurfaceBuildContext {
    SurfaceTileMap levels[
        OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM
        - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM
        + 1];
    OpenRideORMapPyramidSurfaceBuildStats *stats;
    bool failed;
} SurfaceBuildContext;

static void set_error(char *error,
                      size_t error_size,
                      const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
}

static void write_u16_le(unsigned char *p, uint16_t value)
{
    p[0] = (unsigned char)(value & 0xffU);
    p[1] = (unsigned char)((value >> 8U) & 0xffU);
}

static void write_u32_le(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)(value & 0xffU);
    p[1] = (unsigned char)((value >> 8U) & 0xffU);
    p[2] = (unsigned char)((value >> 16U) & 0xffU);
    p[3] = (unsigned char)((value >> 24U) & 0xffU);
}

static uint64_t tile_key(int zoom, int x, int y)
{
    return ((uint64_t)(uint32_t)zoom << 58U)
        | ((uint64_t)(uint32_t)x << 29U)
        | (uint64_t)(uint32_t)y;
}

static void decode_tile_key(uint64_t key,
                            int *zoom,
                            int *x,
                            int *y)
{
    if (zoom) *zoom = (int)(key >> 58U);
    if (x) *x = (int)((key >> 29U) & UINT64_C(0x1fffffff));
    if (y) *y = (int)(key & UINT64_C(0x1fffffff));
}

static uint32_t hash64(uint64_t value)
{
    value ^= value >> 33U;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33U;
    value *= UINT64_C(0xc4ceb9fe1a85ec53);
    value ^= value >> 33U;
    return (uint32_t)value;
}

static void bucket_destroy(SurfaceTileBucket *bucket)
{
    if (!bucket) return;
    free(bucket->triangles);
    memset(bucket, 0, sizeof(*bucket));
}

static void tile_map_destroy(SurfaceTileMap *map)
{
    if (!map) return;
    for (uint32_t i = 0U; i < map->capacity; ++i) {
        if (map->buckets[i].used) {
            bucket_destroy(&map->buckets[i]);
        }
    }
    free(map->buckets);
    memset(map, 0, sizeof(*map));
}

static bool tile_map_rehash(SurfaceTileMap *map,
                            uint32_t new_capacity)
{
    SurfaceTileBucket *new_buckets =
        calloc(new_capacity, sizeof(*new_buckets));
    if (!new_buckets) return false;

    SurfaceTileBucket *old_buckets = map->buckets;
    const uint32_t old_capacity = map->capacity;

    map->buckets = new_buckets;
    map->capacity = new_capacity;
    map->count = 0U;

    for (uint32_t i = 0U; i < old_capacity; ++i) {
        if (!old_buckets[i].used) continue;

        uint32_t slot =
            hash64(old_buckets[i].key)
            & (new_capacity - 1U);

        while (map->buckets[slot].used) {
            slot = (slot + 1U) & (new_capacity - 1U);
        }

        map->buckets[slot] = old_buckets[i];
        ++map->count;
    }

    free(old_buckets);
    return true;
}

static SurfaceTileBucket *tile_map_get(
    SurfaceTileMap *map,
    int zoom,
    int x,
    int y,
    bool create)
{
    if (!map) return NULL;

    if (map->capacity == 0U) {
        if (!create) return NULL;

        map->capacity = TILE_MAP_INITIAL_CAPACITY;
        map->buckets =
            calloc(map->capacity, sizeof(*map->buckets));
        if (!map->buckets) {
            map->capacity = 0U;
            return NULL;
        }
    }

    if (create
        && (uint64_t)(map->count + 1U)
                * HASH_LOAD_DENOMINATOR
            > (uint64_t)map->capacity
                * HASH_LOAD_NUMERATOR) {
        if (map->capacity > UINT32_MAX / 2U) return NULL;
        if (!tile_map_rehash(
                map,
                map->capacity * 2U)) {
            return NULL;
        }
    }

    const uint64_t key = tile_key(zoom, x, y);
    uint32_t slot =
        hash64(key) & (map->capacity - 1U);

    while (map->buckets[slot].used) {
        if (map->buckets[slot].key == key) {
            return &map->buckets[slot];
        }
        slot = (slot + 1U) & (map->capacity - 1U);
    }

    if (!create) return NULL;

    map->buckets[slot].used = true;
    map->buckets[slot].key = key;
    ++map->count;
    return &map->buckets[slot];
}

static bool bucket_push(
    SurfaceTileBucket *bucket,
    OpenRideORMapPyramidSurfaceTriangle triangle)
{
    if (!bucket) return false;

    if (bucket->count >= bucket->capacity) {
        uint32_t capacity =
            bucket->capacity
                ? bucket->capacity * 2U
                : 16U;
        if (capacity < bucket->count + 1U) {
            capacity = bucket->count + 1U;
        }

        OpenRideORMapPyramidSurfaceTriangle *grown =
            realloc(
                bucket->triangles,
                (size_t)capacity * sizeof(*grown));
        if (!grown) return false;

        bucket->triangles = grown;
        bucket->capacity = capacity;
    }

    bucket->triangles[bucket->count++] = triangle;
    return true;
}

static double clamp_latitude(double latitude)
{
    if (latitude > 85.05112878) return 85.05112878;
    if (latitude < -85.05112878) return -85.05112878;
    return latitude;
}

static double mercator_x(double longitude)
{
    double x = (longitude + 180.0) / 360.0;
    if (x < 0.0) x = 0.0;
    if (x > 1.0) x = 1.0;
    return x;
}

static double mercator_y(double latitude)
{
    const double radians =
        clamp_latitude(latitude)
        * 3.14159265358979323846 / 180.0;

    double y =
        (1.0
         - log(tan(radians) + 1.0 / cos(radians))
             / 3.14159265358979323846)
        * 0.5;

    if (y < 0.0) y = 0.0;
    if (y > 1.0) y = 1.0;
    return y;
}

static double point_segment_distance_sq(
    OpenRideORMapPyramidPoint point,
    OpenRideORMapPyramidPoint a,
    OpenRideORMapPyramidPoint b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length_sq = dx * dx + dy * dy;

    if (length_sq <= 1e-30) {
        const double px = point.x - a.x;
        const double py = point.y - a.y;
        return px * px + py * py;
    }

    double t =
        ((point.x - a.x) * dx
         + (point.y - a.y) * dy)
        / length_sq;

    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    const double px = point.x - (a.x + t * dx);
    const double py = point.y - (a.y + t * dy);
    return px * px + py * py;
}

static void rdp_mark(
    const OpenRideORMapPyramidPoint *points,
    uint32_t first,
    uint32_t last,
    double tolerance_sq,
    unsigned char *keep)
{
    if (!points || !keep || last <= first + 1U) return;

    double farthest = tolerance_sq;
    uint32_t split = 0U;

    for (uint32_t i = first + 1U; i < last; ++i) {
        const double distance =
            point_segment_distance_sq(
                points[i],
                points[first],
                points[last]);

        if (distance > farthest) {
            farthest = distance;
            split = i;
        }
    }

    if (split == 0U) return;

    keep[split] = 1U;
    rdp_mark(points, first, split, tolerance_sq, keep);
    rdp_mark(points, split, last, tolerance_sq, keep);
}

double openride_ormap_pyramid_polygon_signed_area(
    const OpenRideORMapPyramidPoint *points,
    uint32_t count)
{
    if (!points || count < 3U) return 0.0;

    double area = 0.0;

    for (uint32_t i = 0U; i < count; ++i) {
        const OpenRideORMapPyramidPoint a = points[i];
        const OpenRideORMapPyramidPoint b =
            points[(i + 1U) % count];
        area += a.x * b.y - b.x * a.y;
    }

    return area * 0.5;
}

bool openride_ormap_pyramid_simplify_closed_ring(
    const OpenRideORMapPyramidPoint *input,
    uint32_t input_count,
    double tolerance,
    OpenRideORMapPyramidPoint **output,
    uint32_t *output_count)
{
    if (!output || !output_count) return false;

    *output = NULL;
    *output_count = 0U;

    if (!input || input_count < 3U) return true;

    uint32_t unique_limit = input_count;

    if (input_count >= 2U
        && fabs(input[0].x - input[input_count - 1U].x) < 1e-15
        && fabs(input[0].y - input[input_count - 1U].y) < 1e-15) {
        --unique_limit;
    }

    OpenRideORMapPyramidPoint *ring =
        malloc(
            ((size_t)unique_limit + 1U)
            * sizeof(*ring));
    if (!ring) return false;

    uint32_t count = 0U;

    for (uint32_t i = 0U; i < unique_limit; ++i) {
        if (count > 0U
            && fabs(input[i].x - ring[count - 1U].x) < 1e-15
            && fabs(input[i].y - ring[count - 1U].y) < 1e-15) {
            continue;
        }

        ring[count++] = input[i];
    }

    if (count < 3U) {
        free(ring);
        return true;
    }

    uint32_t split = 1U;
    double farthest = -1.0;

    for (uint32_t i = 1U; i < count; ++i) {
        const double dx = ring[i].x - ring[0].x;
        const double dy = ring[i].y - ring[0].y;
        const double distance = dx * dx + dy * dy;

        if (distance > farthest) {
            farthest = distance;
            split = i;
        }
    }

    ring[count] = ring[0];

    unsigned char *keep =
        calloc((size_t)count + 1U, 1U);
    if (!keep) {
        free(ring);
        return false;
    }

    keep[0] = 1U;
    keep[split] = 1U;
    keep[count] = 1U;

    const double tolerance_sq =
        tolerance > 0.0
            ? tolerance * tolerance
            : 0.0;

    rdp_mark(ring, 0U, split, tolerance_sq, keep);
    rdp_mark(ring, split, count, tolerance_sq, keep);

    OpenRideORMapPyramidPoint *simplified =
        malloc((size_t)count * sizeof(*simplified));
    if (!simplified) {
        free(keep);
        free(ring);
        return false;
    }

    uint32_t simplified_count = 0U;

    for (uint32_t i = 0U; i < count; ++i) {
        if (keep[i]) {
            simplified[simplified_count++] = ring[i];
        }
    }

    free(keep);
    free(ring);

    if (simplified_count < 3U) {
        free(simplified);
        return true;
    }

    *output = simplified;
    *output_count = simplified_count;
    return true;
}

static double point_cross(
    OpenRideORMapPyramidPoint a,
    OpenRideORMapPyramidPoint b,
    OpenRideORMapPyramidPoint c)
{
    return (b.x - a.x) * (c.y - a.y)
        - (b.y - a.y) * (c.x - a.x);
}

static bool point_inside_triangle(
    OpenRideORMapPyramidPoint point,
    OpenRideORMapPyramidPoint a,
    OpenRideORMapPyramidPoint b,
    OpenRideORMapPyramidPoint c,
    double orientation)
{
    const double epsilon = 1e-15;

    return orientation * point_cross(a, b, point) > epsilon
        && orientation * point_cross(b, c, point) > epsilon
        && orientation * point_cross(c, a, point) > epsilon;
}

static bool clip_inside(
    OpenRideORMapPyramidPoint point,
    unsigned edge,
    double bound)
{
    switch (edge) {
        case 0U: return point.x >= bound;
        case 1U: return point.x <= bound;
        case 2U: return point.y >= bound;
        default: return point.y <= bound;
    }
}

static OpenRideORMapPyramidPoint clip_intersection(
    OpenRideORMapPyramidPoint a,
    OpenRideORMapPyramidPoint b,
    unsigned edge,
    double bound)
{
    OpenRideORMapPyramidPoint result = a;

    if (edge <= 1U) {
        const double dx = b.x - a.x;
        const double t =
            fabs(dx) < 1e-20
                ? 0.0
                : (bound - a.x) / dx;

        result.x = bound;
        result.y = a.y + (b.y - a.y) * t;
    } else {
        const double dy = b.y - a.y;
        const double t =
            fabs(dy) < 1e-20
                ? 0.0
                : (bound - a.y) / dy;

        result.x = a.x + (b.x - a.x) * t;
        result.y = bound;
    }

    return result;
}

static uint32_t clip_polygon_edge(
    const OpenRideORMapPyramidPoint *input,
    uint32_t input_count,
    OpenRideORMapPyramidPoint *output,
    unsigned edge,
    double bound)
{
    if (!input || !output || input_count == 0U) return 0U;

    uint32_t output_count = 0U;
    OpenRideORMapPyramidPoint previous =
        input[input_count - 1U];
    bool previous_inside =
        clip_inside(previous, edge, bound);

    for (uint32_t i = 0U; i < input_count; ++i) {
        const OpenRideORMapPyramidPoint current = input[i];
        const bool current_inside =
            clip_inside(current, edge, bound);

        if (current_inside != previous_inside) {
            output[output_count++] =
                clip_intersection(
                    previous,
                    current,
                    edge,
                    bound);
        }

        if (current_inside) {
            output[output_count++] = current;
        }

        previous = current;
        previous_inside = current_inside;
    }

    return output_count;
}

static uint16_t quantize_local(double coordinate, int tile)
{
    const double buffer =
        OPENRIDE_ORMAP_PYRAMID_SURFACE_BUFFER_FRACTION;

    double local =
        (coordinate - (double)tile + buffer)
        / (1.0 + 2.0 * buffer);

    if (local < 0.0) local = 0.0;
    if (local > 1.0) local = 1.0;

    return (uint16_t)lround(local * 65535.0);
}

static bool emit_triangle(
    SurfaceTileMap *map,
    int zoom,
    uint8_t kind,
    OpenRideORMapPyramidPoint a,
    OpenRideORMapPyramidPoint b,
    OpenRideORMapPyramidPoint c,
    OpenRideORMapPyramidSurfaceBuildStats *stats)
{
    const int tile_count = 1 << zoom;
    const double buffer =
        OPENRIDE_ORMAP_PYRAMID_SURFACE_BUFFER_FRACTION;

    a.x *= tile_count;
    a.y *= tile_count;
    b.x *= tile_count;
    b.y *= tile_count;
    c.x *= tile_count;
    c.y *= tile_count;

    const double min_x = fmin(a.x, fmin(b.x, c.x));
    const double max_x = fmax(a.x, fmax(b.x, c.x));
    const double min_y = fmin(a.y, fmin(b.y, c.y));
    const double max_y = fmax(a.y, fmax(b.y, c.y));

    int first_x = (int)floor(min_x - buffer);
    int last_x = (int)floor(max_x + buffer);
    int first_y = (int)floor(min_y - buffer);
    int last_y = (int)floor(max_y + buffer);

    if (first_x < 0) first_x = 0;
    if (first_y < 0) first_y = 0;
    if (last_x >= tile_count) last_x = tile_count - 1;
    if (last_y >= tile_count) last_y = tile_count - 1;

    for (int ty = first_y; ty <= last_y; ++ty) {
        for (int tx = first_x; tx <= last_x; ++tx) {
            OpenRideORMapPyramidPoint p0[8] = {a, b, c};
            OpenRideORMapPyramidPoint p1[8];
            uint32_t count = 3U;

            count = clip_polygon_edge(
                p0, count, p1, 0U, (double)tx - buffer);
            if (count < 3U) continue;

            count = clip_polygon_edge(
                p1, count, p0, 1U, (double)tx + 1.0 + buffer);
            if (count < 3U) continue;

            count = clip_polygon_edge(
                p0, count, p1, 2U, (double)ty - buffer);
            if (count < 3U) continue;

            count = clip_polygon_edge(
                p1, count, p0, 3U, (double)ty + 1.0 + buffer);
            if (count < 3U) continue;

            SurfaceTileBucket *bucket =
                tile_map_get(map, zoom, tx, ty, true);
            if (!bucket) return false;

            for (uint32_t i = 1U; i + 1U < count; ++i) {
                if (fabs(point_cross(
                        p0[0],
                        p0[i],
                        p0[i + 1U])) < 1e-15) {
                    continue;
                }

                const OpenRideORMapPyramidSurfaceTriangle triangle = {
                    .x1 = quantize_local(p0[0].x, tx),
                    .y1 = quantize_local(p0[0].y, ty),
                    .x2 = quantize_local(p0[i].x, tx),
                    .y2 = quantize_local(p0[i].y, ty),
                    .x3 = quantize_local(p0[i + 1U].x, tx),
                    .y3 = quantize_local(p0[i + 1U].y, ty),
                    .kind = kind,
                    .reserved = 0U
                };

                if (!bucket_push(bucket, triangle)) return false;

                const int level =
                    zoom - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM;

                ++stats->triangles_by_zoom[level];
                ++stats->triangles_total;
            }
        }
    }

    return true;
}

typedef struct PendingSurfaceTriangle {
    OpenRideORMapPyramidPoint a;
    OpenRideORMapPyramidPoint b;
    OpenRideORMapPyramidPoint c;
} PendingSurfaceTriangle;

static bool pending_triangle_push(
    PendingSurfaceTriangle **triangles,
    uint32_t *count,
    uint32_t *capacity,
    OpenRideORMapPyramidPoint a,
    OpenRideORMapPyramidPoint b,
    OpenRideORMapPyramidPoint c)
{
    if (!triangles || !count || !capacity) return false;

    if (*count >= *capacity) {
        uint32_t next =
            *capacity ? *capacity * 2U : 32U;

        if (next < *count + 1U) {
            next = *count + 1U;
        }

        PendingSurfaceTriangle *grown =
            realloc(
                *triangles,
                (size_t)next * sizeof(*grown));

        if (!grown) return false;

        *triangles = grown;
        *capacity = next;
    }

    (*triangles)[(*count)++] =
        (PendingSurfaceTriangle){
            .a = a,
            .b = b,
            .c = c
        };

    return true;
}

static bool triangulate_ring(
    SurfaceTileMap *map,
    int zoom,
    uint8_t kind,
    const OpenRideORMapPyramidPoint *points,
    uint32_t count,
    OpenRideORMapPyramidSurfaceBuildStats *stats)
{
    if (!map || !points || count < 3U || !stats) return true;

    const double signed_area =
        openride_ormap_pyramid_polygon_signed_area(points, count);

    if (fabs(signed_area) < 1e-18) {
        ++stats->invalid_or_small_polygons;
        return true;
    }

    const double orientation =
        signed_area > 0.0 ? 1.0 : -1.0;

    uint32_t *indices =
        malloc((size_t)count * sizeof(*indices));
    if (!indices) return false;

    PendingSurfaceTriangle *pending = NULL;
    uint32_t pending_count = 0U;
    uint32_t pending_capacity = 0U;

    for (uint32_t i = 0U; i < count; ++i) {
        indices[i] = i;
    }

    uint32_t remaining = count;
    uint64_t guard =
        (uint64_t)count * (uint64_t)count + 1U;

    bool complete = true;

    while (remaining > 3U && guard-- > 0U) {
        bool clipped = false;

        for (uint32_t i = 0U; i < remaining; ++i) {
            const uint32_t prev =
                indices[(i + remaining - 1U) % remaining];
            const uint32_t curr = indices[i];
            const uint32_t next =
                indices[(i + 1U) % remaining];

            const OpenRideORMapPyramidPoint a = points[prev];
            const OpenRideORMapPyramidPoint b = points[curr];
            const OpenRideORMapPyramidPoint c = points[next];

            if (orientation * point_cross(a, b, c) <= 1e-18) {
                continue;
            }

            bool contains = false;

            for (uint32_t j = 0U; j < remaining; ++j) {
                const uint32_t candidate = indices[j];

                if (candidate == prev
                    || candidate == curr
                    || candidate == next) {
                    continue;
                }

                if (point_inside_triangle(
                        points[candidate],
                        a,
                        b,
                        c,
                        orientation)) {
                    contains = true;
                    break;
                }
            }

            if (contains) continue;

            /*
             * Critical V3.8.6 rule:
             *
             * Do NOT write an ear to surface_tiles yet. A ring that becomes
             * impossible to finish must leave absolutely no partial geometry.
             */
            if (!pending_triangle_push(
                    &pending,
                    &pending_count,
                    &pending_capacity,
                    a,
                    b,
                    c)) {
                free(pending);
                free(indices);
                return false;
            }

            memmove(
                indices + i,
                indices + i + 1U,
                (size_t)(remaining - i - 1U)
                    * sizeof(*indices));

            --remaining;
            clipped = true;
            break;
        }

        if (!clipped) {
            complete = false;
            break;
        }
    }

    if (complete && remaining == 3U) {
        if (!pending_triangle_push(
                &pending,
                &pending_count,
                &pending_capacity,
                points[indices[0]],
                points[indices[1]],
                points[indices[2]])) {
            free(pending);
            free(indices);
            return false;
        }
    } else if (remaining > 3U) {
        complete = false;
    }

    free(indices);

    if (!complete) {
        ++stats->triangulation_failures;
        stats->triangulation_partial_triangles_discarded +=
            pending_count;

        free(pending);
        return true;
    }

    /*
     * Only a fully completed triangulation is committed to tile buckets.
     * A fatal allocation/write failure still aborts the entire builder and
     * lets the surrounding SQLite transaction roll back normally.
     */
    for (uint32_t i = 0U; i < pending_count; ++i) {
        if (!emit_triangle(
                map,
                zoom,
                kind,
                pending[i].a,
                pending[i].b,
                pending[i].c,
                stats)) {
            free(pending);
            return false;
        }
    }

    free(pending);
    return true;
}

static double simplify_pixels_for_zoom(int zoom)
{
    switch (zoom) {
        case 9: return 1.25;
        case 10: return 0.90;
        case 11: return 0.65;
        case 12: return 0.45;
        case 13: return 0.30;
        default: return 0.20;
    }
}

static double minimum_area_pixels_for_zoom(
    int zoom,
    uint8_t kind)
{
    double value = 0.20;

    switch (zoom) {
        case 9: value = 5.0; break;
        case 10: value = 3.0; break;
        case 11: value = 1.5; break;
        case 12: value = 0.8; break;
        case 13: value = 0.4; break;
        default: value = 0.20; break;
    }

    if (kind == OPENRIDE_ORMAP_PYRAMID_SURFACE_WATER) {
        value *= 0.50;
    }

    return value;
}

static double tolerance_world_for_zoom(int zoom)
{
    return simplify_pixels_for_zoom(zoom)
        / (256.0 * (double)(1U << zoom));
}

static bool polygon_large_enough(
    const OpenRideORMapPyramidPoint *points,
    uint32_t count,
    int zoom,
    uint8_t kind)
{
    const double area =
        fabs(openride_ormap_pyramid_polygon_signed_area(
            points,
            count));

    const double pixels_per_world =
        256.0 * (double)(1U << zoom);

    const double pixel_area =
        area * pixels_per_world * pixels_per_world;

    return pixel_area
        >= minimum_area_pixels_for_zoom(zoom, kind);
}

static bool build_polygon_levels(
    SurfaceBuildContext *context,
    uint8_t kind,
    const double *latitudes,
    const double *longitudes,
    uint32_t point_count)
{
    OpenRideORMapPyramidPoint *canonical =
        malloc((size_t)point_count * sizeof(*canonical));
    if (!canonical) return false;

    for (uint32_t i = 0U; i < point_count; ++i) {
        canonical[i] = (OpenRideORMapPyramidPoint){
            .x = mercator_x(longitudes[i]),
            .y = mercator_y(latitudes[i])
        };
    }

    OpenRideORMapPyramidPoint *current = NULL;
    uint32_t current_count = 0U;

    if (!openride_ormap_pyramid_simplify_closed_ring(
            canonical,
            point_count,
            tolerance_world_for_zoom(
                OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM),
            &current,
            &current_count)) {
        free(canonical);
        return false;
    }

    free(canonical);

    if (!current || current_count < 3U) {
        free(current);
        ++context->stats->invalid_or_small_polygons;
        return true;
    }

    /*
     * Critical v11 invariant:
     *
     * z14 is derived from the canonical OSM ring, then z13 from z14, z12 from
     * z13, and so on. Coarse geometry can only remove fine vertices; it can
     * never invent a different outline.
     */
    for (int zoom = OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM;
         zoom >= OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM;
         --zoom) {
        if (polygon_large_enough(
                current,
                current_count,
                zoom,
                kind)) {
            const int level =
                zoom - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM;

            if (!triangulate_ring(
                    &context->levels[level],
                    zoom,
                    kind,
                    current,
                    current_count,
                    context->stats)) {
                free(current);
                return false;
            }
        }

        if (zoom == OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM) {
            break;
        }

        OpenRideORMapPyramidPoint *coarser = NULL;
        uint32_t coarser_count = 0U;

        if (!openride_ormap_pyramid_simplify_closed_ring(
                current,
                current_count,
                tolerance_world_for_zoom(zoom - 1),
                &coarser,
                &coarser_count)) {
            free(current);
            return false;
        }

        free(current);
        current = coarser;
        current_count = coarser_count;

        if (!current || current_count < 3U) {
            free(current);
            return true;
        }
    }

    free(current);
    return true;
}

static bool surface_feature_visitor(
    OpenRideOSMMapFeatureKind kind,
    const double *latitudes,
    const double *longitudes,
    uint32_t point_count,
    void *userdata)
{
    SurfaceBuildContext *context = userdata;

    if (!context || context->failed) return false;

    ++context->stats->osm_features_seen;

    uint8_t surface_kind = 0U;

    if (kind == OPENRIDE_OSM_MAP_FEATURE_BUILTUP_AREA) {
        ++context->stats->builtup_polygons;

        if (point_count == 1U) {
            ++context->stats
                ->representative_building_points_ignored;
            return true;
        }

        surface_kind =
            OPENRIDE_ORMAP_PYRAMID_SURFACE_BUILTUP;
    } else if (kind == OPENRIDE_OSM_MAP_FEATURE_WATER_AREA) {
        ++context->stats->water_polygons;
        surface_kind =
            OPENRIDE_ORMAP_PYRAMID_SURFACE_WATER;
    } else if (kind == OPENRIDE_OSM_MAP_FEATURE_FOREST_AREA) {
        ++context->stats->green_polygons;
        surface_kind =
            OPENRIDE_ORMAP_PYRAMID_SURFACE_GREEN;
    } else {
        return true;
    }

    if (point_count < 4U) return true;

    ++context->stats->surface_polygons_seen;

    if (!build_polygon_levels(
            context,
            surface_kind,
            latitudes,
            longitudes,
            point_count)) {
        context->failed = true;
        return false;
    }

    return true;
}

static bool exec_sql(
    sqlite3 *db,
    const char *sql,
    char *error,
    size_t error_size)
{
    char *sqlite_error = NULL;

    const int rc =
        sqlite3_exec(
            db,
            sql,
            NULL,
            NULL,
            &sqlite_error);

    if (rc == SQLITE_OK) return true;

    set_error(
        error,
        error_size,
        sqlite_error ? sqlite_error : sqlite3_errmsg(db));

    sqlite3_free(sqlite_error);
    return false;
}

static bool write_metadata(
    sqlite3 *db,
    const char *region_name,
    char *error,
    size_t error_size)
{
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(
            db,
            "INSERT INTO metadata(name,value) VALUES(?1,?2)",
            -1,
            &stmt,
            NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(db));
        return false;
    }

    char version[32];
    char min_zoom[32];
    char max_zoom[32];

    snprintf(
        version,
        sizeof(version),
        "%u",
        OPENRIDE_ORMAP_PYRAMID_FORMAT_VERSION);
    snprintf(
        min_zoom,
        sizeof(min_zoom),
        "%d",
        OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM);
    snprintf(
        max_zoom,
        sizeof(max_zoom),
        "%d",
        OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM);

    const char *pairs[][2] = {
        {"format", "OpenRide ORMap Pyramid"},
        {"format_version", version},
        {"surface_minzoom", min_zoom},
        {"surface_maxzoom", max_zoom},
        {"surface_policy", "hierarchical-z14-to-z9"},
        {"name", region_name ? region_name : "OpenRide v11 region"},
        {"attribution", "OpenStreetMap contributors"}
    };

    bool ok = true;

    for (size_t i = 0U;
         i < sizeof(pairs) / sizeof(pairs[0]);
         ++i) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        sqlite3_bind_text(
            stmt, 1, pairs[i][0], -1, SQLITE_STATIC);
        sqlite3_bind_text(
            stmt, 2, pairs[i][1], -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            set_error(error, error_size, sqlite3_errmsg(db));
            ok = false;
            break;
        }
    }

    sqlite3_finalize(stmt);
    return ok;
}

static bool encode_bucket(
    const SurfaceTileBucket *bucket,
    unsigned char **compressed_out,
    size_t *compressed_size_out,
    uint64_t *raw_size_out)
{
    const size_t record_size = 14U;

    *compressed_out = NULL;
    *compressed_size_out = 0U;
    *raw_size_out = 0U;

    if ((uint64_t)bucket->count
        > (SIZE_MAX - 12U) / record_size) {
        return false;
    }

    const size_t raw_size =
        12U + (size_t)bucket->count * record_size;

    unsigned char *raw = malloc(raw_size);
    if (!raw) return false;

    memcpy(raw, "ORP1", 4U);
    write_u16_le(
        raw + 4U,
        OPENRIDE_ORMAP_PYRAMID_SURFACE_BLOB_VERSION);
    write_u16_le(raw + 6U, (uint16_t)record_size);
    write_u32_le(raw + 8U, bucket->count);

    size_t offset = 12U;

    for (uint32_t i = 0U; i < bucket->count; ++i) {
        const OpenRideORMapPyramidSurfaceTriangle *t =
            &bucket->triangles[i];

        write_u16_le(raw + offset + 0U, t->x1);
        write_u16_le(raw + offset + 2U, t->y1);
        write_u16_le(raw + offset + 4U, t->x2);
        write_u16_le(raw + offset + 6U, t->y2);
        write_u16_le(raw + offset + 8U, t->x3);
        write_u16_le(raw + offset + 10U, t->y3);
        raw[offset + 12U] = t->kind;
        raw[offset + 13U] = 0U;
        offset += record_size;
    }

    const uLongf bound = compressBound((uLong)raw_size);
    unsigned char *compressed = malloc((size_t)bound);

    if (!compressed) {
        free(raw);
        return false;
    }

    uLongf compressed_size = bound;

    const int zrc =
        compress2(
            compressed,
            &compressed_size,
            raw,
            (uLong)raw_size,
            Z_BEST_SPEED);

    free(raw);

    if (zrc != Z_OK) {
        free(compressed);
        return false;
    }

    *compressed_out = compressed;
    *compressed_size_out = (size_t)compressed_size;
    *raw_size_out = (uint64_t)raw_size;
    return true;
}

static bool write_tiles(
    sqlite3 *db,
    SurfaceBuildContext *context,
    char *error,
    size_t error_size)
{
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(
            db,
            "INSERT INTO surface_tiles("
            "zoom,tile_column,tile_row,tile_data"
            ") VALUES(?1,?2,?3,?4)",
            -1,
            &stmt,
            NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(db));
        return false;
    }

    bool ok = true;

    for (int zoom = OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM;
         zoom <= OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM && ok;
         ++zoom) {
        const int level =
            zoom - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM;
        SurfaceTileMap *map = &context->levels[level];

        for (uint32_t i = 0U;
             i < map->capacity && ok;
             ++i) {
            const SurfaceTileBucket *bucket = &map->buckets[i];

            if (!bucket->used || bucket->count == 0U) {
                continue;
            }

            unsigned char *compressed = NULL;
            size_t compressed_size = 0U;
            uint64_t raw_size = 0U;

            if (!encode_bucket(
                    bucket,
                    &compressed,
                    &compressed_size,
                    &raw_size)) {
                set_error(
                    error,
                    error_size,
                    "unable to encode v11 surface tile");
                ok = false;
                break;
            }

            int key_zoom = 0;
            int x = 0;
            int y = 0;

            decode_tile_key(
                bucket->key,
                &key_zoom,
                &x,
                &y);

            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);

            sqlite3_bind_int(stmt, 1, key_zoom);
            sqlite3_bind_int(stmt, 2, x);
            sqlite3_bind_int(stmt, 3, y);
            sqlite3_bind_blob(
                stmt,
                4,
                compressed,
                (int)compressed_size,
                SQLITE_TRANSIENT);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                set_error(error, error_size, sqlite3_errmsg(db));
                ok = false;
            } else {
                ++context->stats->tiles_by_zoom[level];
                ++context->stats->tiles_total;
                context->stats->raw_bytes += raw_size;
                context->stats->compressed_bytes += compressed_size;
            }

            free(compressed);
        }
    }

    sqlite3_finalize(stmt);
    return ok;
}

bool openride_ormap_pyramid_surface_build(
    const char *pbf_path,
    const char *output_path,
    const char *region_name,
    OpenRideORMapPyramidSurfaceBuildStats *stats_out,
    char *error,
    size_t error_size)
{
    if (!pbf_path || !output_path) {
        set_error(error, error_size, "invalid v11 surface build arguments");
        return false;
    }

    OpenRideORMapPyramidSurfaceBuildStats stats = {0};
    SurfaceBuildContext context = {.stats = &stats};
    OpenRideOSMMapFeatureStats osm_stats = {0};

    bool ok =
        openride_osm_pbf_visit_map_features(
            pbf_path,
            surface_feature_visitor,
            &context,
            &osm_stats,
            error,
            error_size);

    if (context.failed) {
        ok = false;

        if (!error || error[0] == '\0') {
            set_error(
                error,
                error_size,
                "out of memory building v11 surface pyramid");
        }
    }

    sqlite3 *db = NULL;

    if (ok) {
        remove(output_path);

        if (sqlite3_open_v2(
                output_path,
                &db,
                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                NULL) != SQLITE_OK) {
            set_error(
                error,
                error_size,
                db ? sqlite3_errmsg(db)
                   : "unable to create v11 surface database");
            ok = false;
        }
    }

    if (ok) {
        ok = exec_sql(
            db,
            "PRAGMA journal_mode=OFF;"
            "PRAGMA synchronous=OFF;"
            "PRAGMA temp_store=MEMORY;"
            "BEGIN IMMEDIATE;"
            "CREATE TABLE metadata("
            "name TEXT PRIMARY KEY,"
            "value TEXT NOT NULL"
            ") WITHOUT ROWID;"
            "CREATE TABLE surface_tiles("
            "zoom INTEGER NOT NULL,"
            "tile_column INTEGER NOT NULL,"
            "tile_row INTEGER NOT NULL,"
            "tile_data BLOB NOT NULL,"
            "PRIMARY KEY(zoom,tile_column,tile_row)"
            ") WITHOUT ROWID;",
            error,
            error_size);
    }

    if (ok) {
        ok = write_metadata(
            db,
            region_name,
            error,
            error_size);
    }

    if (ok) {
        ok = write_tiles(
            db,
            &context,
            error,
            error_size);
    }

    if (ok) {
        ok = exec_sql(
            db,
            "COMMIT;PRAGMA optimize;",
            error,
            error_size);
    } else if (db) {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    }

    if (db) sqlite3_close(db);

    for (size_t i = 0U;
         i < sizeof(context.levels) / sizeof(context.levels[0]);
         ++i) {
        tile_map_destroy(&context.levels[i]);
    }

    if (!ok) remove(output_path);
    if (stats_out) *stats_out = stats;
    if (ok) set_error(error, error_size, "");

    return ok;
}

struct OpenRideORMapPyramidSurfaceMap {
    sqlite3 *db;
    OpenRideORMapPyramidSurfaceMetadata metadata;
};

static uint16_t read_u16_le(const unsigned char *p)
{
    return (uint16_t)(
        (uint16_t)p[0]
        | ((uint16_t)p[1] << 8U));
}

static uint32_t read_u32_le(const unsigned char *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8U)
        | ((uint32_t)p[2] << 16U)
        | ((uint32_t)p[3] << 24U);
}

static bool metadata_value(
    sqlite3 *db,
    const char *name,
    char *value,
    size_t value_size)
{
    if (!db || !name || !value || value_size == 0U) {
        return false;
    }

    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(
            db,
            "SELECT value FROM metadata WHERE name=?1",
            -1,
            &stmt,
            NULL) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(
        stmt,
        1,
        name,
        -1,
        SQLITE_STATIC);

    bool ok = false;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *text =
            sqlite3_column_text(stmt, 0);

        if (text) {
            snprintf(
                value,
                value_size,
                "%s",
                (const char *)text);
            ok = true;
        }
    }

    sqlite3_finalize(stmt);
    return ok;
}

void openride_ormap_pyramid_surface_tile_destroy(
    OpenRideORMapPyramidSurfaceTile *tile)
{
    if (!tile) return;

    free(tile->triangles);
    memset(tile, 0, sizeof(*tile));
}

static bool decode_surface_blob(
    const void *blob,
    int blob_size,
    OpenRideORMapPyramidSurfaceTile *tile,
    char *error,
    size_t error_size)
{
    if (!blob || blob_size <= 0 || !tile) {
        set_error(error, error_size, "invalid ORP1 surface blob");
        return false;
    }

    openride_ormap_pyramid_surface_tile_destroy(tile);

    uLongf raw_capacity =
        (uLongf)blob_size * 5U + 64U;
    unsigned char *raw = NULL;
    int zrc = Z_BUF_ERROR;

    for (int attempt = 0;
         attempt < 10 && zrc == Z_BUF_ERROR;
         ++attempt) {
        unsigned char *grown =
            realloc(raw, (size_t)raw_capacity);

        if (!grown) {
            free(raw);
            set_error(
                error,
                error_size,
                "out of memory decoding ORP1 surface blob");
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

        if (raw_capacity > UINT32_MAX / 2U) {
            break;
        }

        raw_capacity *= 2U;
    }

    if (zrc != Z_OK
        || raw_capacity < 12U
        || memcmp(raw, "ORP1", 4U) != 0) {
        free(raw);
        set_error(
            error,
            error_size,
            "unable to decode ORP1 surface blob");
        return false;
    }

    const uint16_t version = read_u16_le(raw + 4U);
    const uint16_t record_size = read_u16_le(raw + 6U);
    const uint32_t count = read_u32_le(raw + 8U);

    if (version
            != OPENRIDE_ORMAP_PYRAMID_SURFACE_BLOB_VERSION
        || record_size != 14U
        || (uint64_t)count * record_size + 12U
            != (uint64_t)raw_capacity) {
        free(raw);
        set_error(
            error,
            error_size,
            "unsupported ORP1 surface payload");
        return false;
    }

    if (count > 0U) {
        tile->triangles =
            calloc(count, sizeof(*tile->triangles));

        if (!tile->triangles) {
            free(raw);
            set_error(
                error,
                error_size,
                "out of memory materializing ORP1 surface tile");
            return false;
        }
    }

    size_t offset = 12U;

    for (uint32_t i = 0U; i < count; ++i) {
        OpenRideORMapPyramidSurfaceTriangle *t =
            &tile->triangles[i];

        t->x1 = read_u16_le(raw + offset + 0U);
        t->y1 = read_u16_le(raw + offset + 2U);
        t->x2 = read_u16_le(raw + offset + 4U);
        t->y2 = read_u16_le(raw + offset + 6U);
        t->x3 = read_u16_le(raw + offset + 8U);
        t->y3 = read_u16_le(raw + offset + 10U);
        t->kind = raw[offset + 12U];
        t->reserved = raw[offset + 13U];

        offset += record_size;
    }

    free(raw);
    tile->count = count;
    set_error(error, error_size, "");
    return true;
}

OpenRideORMapPyramidSurfaceMap *
openride_ormap_pyramid_surface_open(
    const char *path,
    char *error,
    size_t error_size)
{
    if (!path) {
        set_error(error, error_size, "invalid v11 surface path");
        return NULL;
    }

    OpenRideORMapPyramidSurfaceMap *map =
        calloc(1U, sizeof(*map));

    if (!map) {
        set_error(
            error,
            error_size,
            "out of memory opening v11 surface map");
        return NULL;
    }

    if (sqlite3_open_v2(
            path,
            &map->db,
            SQLITE_OPEN_READONLY,
            NULL) != SQLITE_OK) {
        set_error(
            error,
            error_size,
            map->db ? sqlite3_errmsg(map->db)
                    : "unable to open v11 surface map");
        openride_ormap_pyramid_surface_close(map);
        return NULL;
    }

    char value[160] = {0};

    if (!metadata_value(
            map->db,
            "format_version",
            value,
            sizeof(value))
        || atoi(value)
            != (int)OPENRIDE_ORMAP_PYRAMID_FORMAT_VERSION) {
        set_error(
            error,
            error_size,
            "not an OpenRide ORMap v11 surface pyramid");
        openride_ormap_pyramid_surface_close(map);
        return NULL;
    }

    map->metadata.format_version = atoi(value);

    if (metadata_value(
            map->db,
            "name",
            value,
            sizeof(value))) {
        snprintf(
            map->metadata.name,
            sizeof(map->metadata.name),
            "%s",
            value);
    }

    if (!metadata_value(
            map->db,
            "surface_minzoom",
            value,
            sizeof(value))) {
        set_error(
            error,
            error_size,
            "surface_minzoom metadata missing");
        openride_ormap_pyramid_surface_close(map);
        return NULL;
    }

    map->metadata.min_zoom = atoi(value);

    if (!metadata_value(
            map->db,
            "surface_maxzoom",
            value,
            sizeof(value))) {
        set_error(
            error,
            error_size,
            "surface_maxzoom metadata missing");
        openride_ormap_pyramid_surface_close(map);
        return NULL;
    }

    map->metadata.max_zoom = atoi(value);

    set_error(error, error_size, "");
    return map;
}

void openride_ormap_pyramid_surface_close(
    OpenRideORMapPyramidSurfaceMap *map)
{
    if (!map) return;

    if (map->db) {
        sqlite3_close(map->db);
    }

    free(map);
}

const OpenRideORMapPyramidSurfaceMetadata *
openride_ormap_pyramid_surface_metadata(
    const OpenRideORMapPyramidSurfaceMap *map)
{
    return map ? &map->metadata : NULL;
}

bool openride_ormap_pyramid_surface_tile_exists(
    OpenRideORMapPyramidSurfaceMap *map,
    int zoom,
    int x,
    int y,
    bool *exists,
    char *error,
    size_t error_size)
{
    if (!map || !map->db || !exists) {
        set_error(
            error,
            error_size,
            "invalid v11 tile-exists arguments");
        return false;
    }

    *exists = false;

    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(
            map->db,
            "SELECT 1 FROM surface_tiles "
            "WHERE zoom=?1 "
            "AND tile_column=?2 "
            "AND tile_row=?3",
            -1,
            &stmt,
            NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(map->db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, zoom);
    sqlite3_bind_int(stmt, 2, x);
    sqlite3_bind_int(stmt, 3, y);

    const int rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        *exists = true;
    } else if (rc != SQLITE_DONE) {
        set_error(error, error_size, sqlite3_errmsg(map->db));
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);
    set_error(error, error_size, "");
    return true;
}

bool openride_ormap_pyramid_surface_load_tile(
    OpenRideORMapPyramidSurfaceMap *map,
    int zoom,
    int x,
    int y,
    OpenRideORMapPyramidSurfaceTile *tile,
    char *error,
    size_t error_size)
{
    if (!map || !map->db || !tile) {
        set_error(
            error,
            error_size,
            "invalid v11 surface-tile arguments");
        return false;
    }

    openride_ormap_pyramid_surface_tile_destroy(tile);

    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(
            map->db,
            "SELECT tile_data FROM surface_tiles "
            "WHERE zoom=?1 "
            "AND tile_column=?2 "
            "AND tile_row=?3",
            -1,
            &stmt,
            NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(map->db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, zoom);
    sqlite3_bind_int(stmt, 2, x);
    sqlite3_bind_int(stmt, 3, y);

    const int rc = sqlite3_step(stmt);

    if (rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        set_error(error, error_size, "");
        return true;
    }

    if (rc != SQLITE_ROW) {
        set_error(error, error_size, sqlite3_errmsg(map->db));
        sqlite3_finalize(stmt);
        return false;
    }

    const void *blob = sqlite3_column_blob(stmt, 0);
    const int blob_size = sqlite3_column_bytes(stmt, 0);

    const bool ok =
        decode_surface_blob(
            blob,
            blob_size,
            tile,
            error,
            error_size);

    sqlite3_finalize(stmt);
    return ok;
}

bool openride_ormap_pyramid_surface_inspect(
    const char *path,
    OpenRideORMapPyramidSurfaceInspectStats *stats_out,
    char *error,
    size_t error_size)
{
    if (!path || !stats_out) {
        set_error(
            error,
            error_size,
            "invalid v11 inspection arguments");
        return false;
    }

    memset(stats_out, 0, sizeof(*stats_out));

    OpenRideORMapPyramidSurfaceMap *map =
        openride_ormap_pyramid_surface_open(
            path,
            error,
            error_size);

    if (!map) return false;

    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(
            map->db,
            "SELECT zoom,tile_data,LENGTH(tile_data) "
            "FROM surface_tiles "
            "ORDER BY zoom,tile_column,tile_row",
            -1,
            &stmt,
            NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(map->db));
        openride_ormap_pyramid_surface_close(map);
        return false;
    }

    bool ok = true;
    int rc = SQLITE_ROW;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const int zoom = sqlite3_column_int(stmt, 0);
        const void *blob = sqlite3_column_blob(stmt, 1);
        const int blob_size = sqlite3_column_int(stmt, 2);

        if (zoom < OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM
            || zoom > OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM) {
            ++stats_out->malformed_tiles;
            ok = false;
            continue;
        }

        const int level =
            zoom - OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM;

        ++stats_out->tiles_by_zoom[level];
        ++stats_out->tile_count;

        if (blob_size > 0) {
            stats_out->compressed_bytes_by_zoom[level]
                += (uint64_t)blob_size;
            stats_out->compressed_bytes
                += (uint64_t)blob_size;
        }

        OpenRideORMapPyramidSurfaceTile tile = {0};
        char decode_error[256] = {0};

        if (!decode_surface_blob(
                blob,
                blob_size,
                &tile,
                decode_error,
                sizeof(decode_error))) {
            ++stats_out->malformed_tiles;
            ok = false;
            continue;
        }

        stats_out->triangles_by_zoom[level] += tile.count;
        stats_out->triangle_count += tile.count;

        if (tile.count
            > stats_out->max_triangles_per_tile_by_zoom[level]) {
            stats_out->max_triangles_per_tile_by_zoom[level] =
                tile.count;
        }

        for (uint32_t i = 0U; i < tile.count; ++i) {
            const OpenRideORMapPyramidSurfaceTriangle *t =
                &tile.triangles[i];

            switch (t->kind) {
                case OPENRIDE_ORMAP_PYRAMID_SURFACE_BUILTUP:
                    ++stats_out
                        ->builtup_triangles_by_zoom[level];
                    break;

                case OPENRIDE_ORMAP_PYRAMID_SURFACE_WATER:
                    ++stats_out
                        ->water_triangles_by_zoom[level];
                    break;

                case OPENRIDE_ORMAP_PYRAMID_SURFACE_GREEN:
                    ++stats_out
                        ->green_triangles_by_zoom[level];
                    break;

                default:
                    ++stats_out->invalid_kinds;
                    ok = false;
                    break;
            }

            if (t->reserved != 0U) {
                ++stats_out->invalid_payloads;
                ok = false;
            }
        }

        openride_ormap_pyramid_surface_tile_destroy(&tile);
    }

    if (rc != SQLITE_DONE) {
        set_error(error, error_size, sqlite3_errmsg(map->db));
        ok = false;
    }

    sqlite3_finalize(stmt);
    openride_ormap_pyramid_surface_close(map);

    if (!ok) {
        if (!error || error[0] == '\0') {
            set_error(
                error,
                error_size,
                "v11 surface inspection found invalid data");
        }

        return false;
    }

    if (!openride_ormap_pyramid_buildings_inspect(
            path,
            stats_out,
            error,
            error_size)) {
        return false;
    }

    set_error(error, error_size, "");
    return true;
}
