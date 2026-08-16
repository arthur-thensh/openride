#include "openride/ormap_pyramid_surface.h"

#include "ormap_pyramid_surface_internal.h"
#include "openride/osm_import.h"

#include <math.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define BUILDING_TILE_MAP_INITIAL_CAPACITY 1024U
#define BUILDING_HASH_LOAD_NUMERATOR 7U
#define BUILDING_HASH_LOAD_DENOMINATOR 10U

typedef struct BuildingTileBucket {
    bool used;
    uint64_t key;
    unsigned char *body;
    uint32_t body_size;
    uint32_t body_capacity;
    uint32_t polygon_count;
    uint64_t vertex_count;
} BuildingTileBucket;

typedef struct BuildingTileMap {
    BuildingTileBucket *buckets;
    uint32_t capacity;
    uint32_t count;
} BuildingTileMap;

typedef struct BuildingBuildContext {
    BuildingTileMap tiles;
    OpenRideORMapPyramidBuildingBuildStats *stats;
    OpenRideORMapPyramidPoint *world;
    OpenRideORMapPyramidPoint *clip_a;
    OpenRideORMapPyramidPoint *clip_b;
    uint16_t *quantized;
    uint32_t scratch_capacity;
    bool failed;
} BuildingBuildContext;

static void building_set_error(
    char *error,
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

static uint64_t building_tile_key(int x, int y)
{
    return ((uint64_t)(uint32_t)x << 32U)
        | (uint64_t)(uint32_t)y;
}

static void decode_building_tile_key(
    uint64_t key,
    int *x,
    int *y)
{
    if (x) *x = (int)(uint32_t)(key >> 32U);
    if (y) *y = (int)(uint32_t)key;
}

static uint32_t building_hash64(uint64_t value)
{
    value ^= value >> 33U;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33U;
    value *= UINT64_C(0xc4ceb9fe1a85ec53);
    value ^= value >> 33U;
    return (uint32_t)value;
}

static void building_tile_map_destroy(BuildingTileMap *map)
{
    if (!map) return;

    for (uint32_t i = 0U; i < map->capacity; ++i) {
        if (map->buckets[i].used) {
            free(map->buckets[i].body);
        }
    }

    free(map->buckets);
    memset(map, 0, sizeof(*map));
}

static bool building_tile_map_rehash(
    BuildingTileMap *map,
    uint32_t new_capacity)
{
    BuildingTileBucket *new_buckets =
        calloc(new_capacity, sizeof(*new_buckets));
    if (!new_buckets) return false;

    BuildingTileBucket *old = map->buckets;
    const uint32_t old_capacity = map->capacity;

    map->buckets = new_buckets;
    map->capacity = new_capacity;
    map->count = 0U;

    for (uint32_t i = 0U; i < old_capacity; ++i) {
        if (!old[i].used) continue;

        uint32_t slot =
            building_hash64(old[i].key)
            & (new_capacity - 1U);

        while (map->buckets[slot].used) {
            slot = (slot + 1U)
                & (new_capacity - 1U);
        }

        map->buckets[slot] = old[i];
        ++map->count;
    }

    free(old);
    return true;
}

static BuildingTileBucket *building_tile_get(
    BuildingTileMap *map,
    int x,
    int y,
    bool create)
{
    if (!map) return NULL;

    if (map->capacity == 0U) {
        if (!create) return NULL;

        map->capacity =
            BUILDING_TILE_MAP_INITIAL_CAPACITY;
        map->buckets =
            calloc(
                map->capacity,
                sizeof(*map->buckets));

        if (!map->buckets) {
            map->capacity = 0U;
            return NULL;
        }
    }

    if (create
        && (uint64_t)(map->count + 1U)
                * BUILDING_HASH_LOAD_DENOMINATOR
            > (uint64_t)map->capacity
                * BUILDING_HASH_LOAD_NUMERATOR) {
        if (map->capacity > UINT32_MAX / 2U) {
            return NULL;
        }

        if (!building_tile_map_rehash(
                map,
                map->capacity * 2U)) {
            return NULL;
        }
    }

    const uint64_t key = building_tile_key(x, y);
    uint32_t slot =
        building_hash64(key)
        & (map->capacity - 1U);

    while (map->buckets[slot].used) {
        if (map->buckets[slot].key == key) {
            return &map->buckets[slot];
        }

        slot = (slot + 1U)
            & (map->capacity - 1U);
    }

    if (!create) return NULL;

    map->buckets[slot].used = true;
    map->buckets[slot].key = key;
    ++map->count;
    return &map->buckets[slot];
}

static bool bucket_reserve(
    BuildingTileBucket *bucket,
    uint32_t extra)
{
    if (!bucket) return false;

    if (extra > UINT32_MAX - bucket->body_size) {
        return false;
    }

    const uint32_t required =
        bucket->body_size + extra;

    if (required <= bucket->body_capacity) {
        return true;
    }

    uint32_t capacity =
        bucket->body_capacity
            ? bucket->body_capacity
            : 256U;

    while (capacity < required) {
        if (capacity > UINT32_MAX / 2U) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }

    unsigned char *grown =
        realloc(bucket->body, capacity);
    if (!grown) return false;

    bucket->body = grown;
    bucket->body_capacity = capacity;
    return true;
}

static double building_clamp_latitude(double latitude)
{
    if (latitude > 85.05112878) return 85.05112878;
    if (latitude < -85.05112878) return -85.05112878;
    return latitude;
}

static double building_mercator_x(double longitude)
{
    double x = (longitude + 180.0) / 360.0;

    if (x < 0.0) x = 0.0;
    if (x > 1.0) x = 1.0;
    return x;
}

static double building_mercator_y(double latitude)
{
    const double radians =
        building_clamp_latitude(latitude)
        * 3.14159265358979323846 / 180.0;

    double y =
        (1.0
         - log(
             tan(radians)
             + 1.0 / cos(radians))
             / 3.14159265358979323846)
        * 0.5;

    if (y < 0.0) y = 0.0;
    if (y > 1.0) y = 1.0;
    return y;
}

static bool building_scratch_reserve(
    BuildingBuildContext *context,
    uint32_t required)
{
    if (!context) return false;

    /*
     * Sutherland-Hodgman clipping can add at most one vertex per clipping
     * boundary per input edge in this rectangle case. Give the scratch arrays
     * generous headroom so no clipping step can overrun them.
     */
    if (required > UINT32_MAX - 16U) return false;
    required += 16U;

    if (required <= context->scratch_capacity) {
        return true;
    }

    uint32_t capacity =
        context->scratch_capacity
            ? context->scratch_capacity
            : 64U;

    while (capacity < required) {
        if (capacity > UINT32_MAX / 2U) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }

    OpenRideORMapPyramidPoint *world =
        realloc(
            context->world,
            (size_t)capacity * sizeof(*world));
    if (!world) return false;
    context->world = world;

    OpenRideORMapPyramidPoint *clip_a =
        realloc(
            context->clip_a,
            (size_t)capacity * sizeof(*clip_a));
    if (!clip_a) return false;
    context->clip_a = clip_a;

    OpenRideORMapPyramidPoint *clip_b =
        realloc(
            context->clip_b,
            (size_t)capacity * sizeof(*clip_b));
    if (!clip_b) return false;
    context->clip_b = clip_b;

    uint16_t *quantized =
        realloc(
            context->quantized,
            (size_t)capacity * 2U
                * sizeof(*quantized));
    if (!quantized) return false;
    context->quantized = quantized;

    context->scratch_capacity = capacity;
    return true;
}

static bool building_clip_inside(
    OpenRideORMapPyramidPoint point,
    unsigned edge,
    double bound)
{
    switch (edge) {
        case 0U:
            return point.x >= bound;
        case 1U:
            return point.x <= bound;
        case 2U:
            return point.y >= bound;
        default:
            return point.y <= bound;
    }
}

static OpenRideORMapPyramidPoint building_clip_intersection(
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
        result.y =
            a.y + (b.y - a.y) * t;
    } else {
        const double dy = b.y - a.y;
        const double t =
            fabs(dy) < 1e-20
                ? 0.0
                : (bound - a.y) / dy;

        result.x =
            a.x + (b.x - a.x) * t;
        result.y = bound;
    }

    return result;
}

static uint32_t building_clip_edge(
    const OpenRideORMapPyramidPoint *input,
    uint32_t input_count,
    OpenRideORMapPyramidPoint *output,
    unsigned edge,
    double bound)
{
    if (!input || !output || input_count == 0U) {
        return 0U;
    }

    uint32_t output_count = 0U;

    OpenRideORMapPyramidPoint previous =
        input[input_count - 1U];
    bool previous_inside =
        building_clip_inside(
            previous,
            edge,
            bound);

    for (uint32_t i = 0U;
         i < input_count;
         ++i) {
        const OpenRideORMapPyramidPoint current =
            input[i];
        const bool current_inside =
            building_clip_inside(
                current,
                edge,
                bound);

        if (current_inside != previous_inside) {
            output[output_count++] =
                building_clip_intersection(
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

static uint16_t building_quantize_local(
    double coordinate,
    int tile)
{
    const double buffer =
        OPENRIDE_ORMAP_PYRAMID_BUILDING_BUFFER_FRACTION;

    double local =
        (coordinate - (double)tile + buffer)
        / (1.0 + 2.0 * buffer);

    if (local < 0.0) local = 0.0;
    if (local > 1.0) local = 1.0;

    return (uint16_t)lround(local * 65535.0);
}

static bool quantized_polygon_nonzero(
    const uint16_t *coordinates,
    uint32_t count)
{
    if (!coordinates || count < 3U) {
        return false;
    }

    int64_t twice_area = 0;

    for (uint32_t i = 0U; i < count; ++i) {
        const uint32_t j = (i + 1U) % count;

        twice_area +=
            (int64_t)coordinates[i * 2U]
                * coordinates[j * 2U + 1U]
            - (int64_t)coordinates[j * 2U]
                * coordinates[i * 2U + 1U];
    }

    return twice_area != 0;
}

static bool append_clipped_polygon(
    BuildingTileBucket *bucket,
    BuildingBuildContext *context,
    const OpenRideORMapPyramidPoint *points,
    uint32_t count,
    int tile_x,
    int tile_y)
{
    if (!bucket || !context || !points || count < 3U) {
        return true;
    }

    uint32_t quantized_count = 0U;

    for (uint32_t i = 0U; i < count; ++i) {
        const uint16_t qx =
            building_quantize_local(
                points[i].x,
                tile_x);
        const uint16_t qy =
            building_quantize_local(
                points[i].y,
                tile_y);

        if (quantized_count > 0U) {
            const uint16_t previous_x =
                context->quantized[
                    (quantized_count - 1U) * 2U];
            const uint16_t previous_y =
                context->quantized[
                    (quantized_count - 1U) * 2U + 1U];

            if (qx == previous_x
                && qy == previous_y) {
                continue;
            }
        }

        context->quantized[
            quantized_count * 2U] = qx;
        context->quantized[
            quantized_count * 2U + 1U] = qy;
        ++quantized_count;
    }

    if (quantized_count >= 2U
        && context->quantized[0]
            == context->quantized[
                (quantized_count - 1U) * 2U]
        && context->quantized[1]
            == context->quantized[
                (quantized_count - 1U) * 2U + 1U]) {
        --quantized_count;
    }

    if (quantized_count < 3U
        || quantized_count > UINT16_MAX
        || !quantized_polygon_nonzero(
            context->quantized,
            quantized_count)) {
        return true;
    }

    const uint64_t record_size64 =
        4U + (uint64_t)quantized_count * 4U;

    if (record_size64 > UINT32_MAX) {
        return false;
    }

    const uint32_t record_size =
        (uint32_t)record_size64;

    if (!bucket_reserve(bucket, record_size)) {
        return false;
    }

    unsigned char *write =
        bucket->body + bucket->body_size;

    write_u16_le(
        write + 0U,
        (uint16_t)quantized_count);
    write_u16_le(write + 2U, 0U);

    size_t offset = 4U;

    for (uint32_t i = 0U;
         i < quantized_count;
         ++i) {
        write_u16_le(
            write + offset,
            context->quantized[i * 2U]);
        write_u16_le(
            write + offset + 2U,
            context->quantized[i * 2U + 1U]);
        offset += 4U;
    }

    bucket->body_size += record_size;
    ++bucket->polygon_count;
    bucket->vertex_count += quantized_count;

    ++context->stats->tile_polygons_stored;
    context->stats->vertices_stored +=
        quantized_count;

    return true;
}

static bool store_building_footprint(
    BuildingBuildContext *context,
    const double *latitudes,
    const double *longitudes,
    uint32_t point_count)
{
    if (!context
        || !latitudes
        || !longitudes
        || point_count < 4U) {
        return true;
    }

    uint32_t unique_count = point_count;

    if (point_count >= 2U
        && fabs(
            latitudes[0]
            - latitudes[point_count - 1U]) < 1e-12
        && fabs(
            longitudes[0]
            - longitudes[point_count - 1U]) < 1e-12) {
        --unique_count;
    }

    if (unique_count < 3U) {
        ++context->stats->invalid_polygons;
        return true;
    }

    if (!building_scratch_reserve(
            context,
            unique_count * 2U)) {
        return false;
    }

    for (uint32_t i = 0U; i < unique_count; ++i) {
        context->world[i] =
            (OpenRideORMapPyramidPoint){
                .x =
                    building_mercator_x(
                        longitudes[i]),
                .y =
                    building_mercator_y(
                        latitudes[i])
            };
    }

    OpenRideORMapPyramidPoint *simplified = NULL;
    uint32_t simplified_count = 0U;

    const double tolerance =
        0.05
        / (256.0
           * (double)(
               1U
               << OPENRIDE_ORMAP_PYRAMID_BUILDING_ZOOM));

    if (!openride_ormap_pyramid_simplify_closed_ring(
            context->world,
            unique_count,
            tolerance,
            &simplified,
            &simplified_count)) {
        return false;
    }

    if (!simplified || simplified_count < 3U) {
        free(simplified);
        ++context->stats->invalid_polygons;
        return true;
    }

    const double area_world =
        fabs(
            openride_ormap_pyramid_polygon_signed_area(
                simplified,
                simplified_count));

    const double pixels_per_world =
        256.0
        * (double)(
            1U
            << OPENRIDE_ORMAP_PYRAMID_BUILDING_ZOOM);

    const double pixel_area =
        area_world
        * pixels_per_world
        * pixels_per_world;

    /*
     * Below two z16 pixels a footprint cannot produce useful close-view
     * context and is disproportionately likely to collapse after quantization.
     */
    if (pixel_area < 2.0) {
        free(simplified);
        ++context->stats->skipped_too_small;
        return true;
    }

    const double tile_scale =
        (double)(
            1U
            << OPENRIDE_ORMAP_PYRAMID_BUILDING_ZOOM);

    double min_x = 1e30;
    double min_y = 1e30;
    double max_x = -1e30;
    double max_y = -1e30;

    for (uint32_t i = 0U;
         i < simplified_count;
         ++i) {
        simplified[i].x *= tile_scale;
        simplified[i].y *= tile_scale;

        min_x = fmin(min_x, simplified[i].x);
        min_y = fmin(min_y, simplified[i].y);
        max_x = fmax(max_x, simplified[i].x);
        max_y = fmax(max_y, simplified[i].y);
    }

    const double buffer =
        OPENRIDE_ORMAP_PYRAMID_BUILDING_BUFFER_FRACTION;
    const int tile_limit =
        (1 << OPENRIDE_ORMAP_PYRAMID_BUILDING_ZOOM)
        - 1;

    int first_x = (int)floor(min_x - buffer);
    int first_y = (int)floor(min_y - buffer);
    int last_x = (int)floor(max_x + buffer);
    int last_y = (int)floor(max_y + buffer);

    if (first_x < 0) first_x = 0;
    if (first_y < 0) first_y = 0;
    if (last_x > tile_limit) last_x = tile_limit;
    if (last_y > tile_limit) last_y = tile_limit;

    bool stored = false;

    for (int tile_y = first_y;
         tile_y <= last_y;
         ++tile_y) {
        for (int tile_x = first_x;
             tile_x <= last_x;
             ++tile_x) {
            if (!building_scratch_reserve(
                    context,
                    simplified_count * 2U)) {
                free(simplified);
                return false;
            }

            memcpy(
                context->clip_a,
                simplified,
                (size_t)simplified_count
                    * sizeof(*simplified));

            uint32_t count = simplified_count;

            count = building_clip_edge(
                context->clip_a,
                count,
                context->clip_b,
                0U,
                (double)tile_x - buffer);
            if (count < 3U) continue;

            count = building_clip_edge(
                context->clip_b,
                count,
                context->clip_a,
                1U,
                (double)tile_x + 1.0 + buffer);
            if (count < 3U) continue;

            count = building_clip_edge(
                context->clip_a,
                count,
                context->clip_b,
                2U,
                (double)tile_y - buffer);
            if (count < 3U) continue;

            count = building_clip_edge(
                context->clip_b,
                count,
                context->clip_a,
                3U,
                (double)tile_y + 1.0 + buffer);
            if (count < 3U) continue;

            BuildingTileBucket *bucket =
                building_tile_get(
                    &context->tiles,
                    tile_x,
                    tile_y,
                    true);

            if (!bucket) {
                free(simplified);
                return false;
            }

            const uint32_t before =
                bucket->polygon_count;

            if (!append_clipped_polygon(
                    bucket,
                    context,
                    context->clip_a,
                    count,
                    tile_x,
                    tile_y)) {
                free(simplified);
                return false;
            }

            if (bucket->polygon_count > before) {
                stored = true;
            }
        }
    }

    free(simplified);

    if (stored) {
        ++context->stats->footprints_stored;
    } else {
        ++context->stats->invalid_polygons;
    }

    return true;
}

static bool building_visitor(
    OpenRideOSMMapFeatureKind kind,
    const double *latitudes,
    const double *longitudes,
    uint32_t point_count,
    void *userdata)
{
    BuildingBuildContext *context = userdata;

    if (!context || context->failed) {
        return false;
    }

    if (kind
        != OPENRIDE_OSM_MAP_FEATURE_BUILDING_FOOTPRINT) {
        return true;
    }

    ++context->stats->footprints_seen;

    if (!store_building_footprint(
            context,
            latitudes,
            longitudes,
            point_count)) {
        context->failed = true;
        return false;
    }

    return true;
}

static bool building_exec_sql(
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

    building_set_error(
        error,
        error_size,
        sqlite_error
            ? sqlite_error
            : sqlite3_errmsg(db));

    sqlite3_free(sqlite_error);
    return false;
}

static bool validate_v11_database(
    sqlite3 *db,
    char *error,
    size_t error_size)
{
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(
            db,
            "SELECT value FROM metadata "
            "WHERE name='format_version'",
            -1,
            &stmt,
            NULL) != SQLITE_OK) {
        building_set_error(
            error,
            error_size,
            sqlite3_errmsg(db));
        return false;
    }

    bool ok = false;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *value =
            sqlite3_column_text(stmt, 0);

        ok = value
            && atoi((const char *)value)
                == (int)
                    OPENRIDE_ORMAP_PYRAMID_FORMAT_VERSION;
    }

    sqlite3_finalize(stmt);

    if (!ok) {
        building_set_error(
            error,
            error_size,
            "building layer requires an existing ORMap v11 database");
    }

    return ok;
}

static bool encode_building_bucket(
    const BuildingTileBucket *bucket,
    unsigned char **compressed_out,
    size_t *compressed_size_out,
    uint64_t *raw_size_out)
{
    if (!bucket
        || !compressed_out
        || !compressed_size_out
        || !raw_size_out) {
        return false;
    }

    *compressed_out = NULL;
    *compressed_size_out = 0U;
    *raw_size_out = 0U;

    const uint64_t raw_size64 =
        16U + bucket->body_size;

    if (raw_size64 > SIZE_MAX
        || bucket->vertex_count > UINT32_MAX) {
        return false;
    }

    const size_t raw_size =
        (size_t)raw_size64;

    unsigned char *raw = malloc(raw_size);
    if (!raw) return false;

    memcpy(raw, "ORB1", 4U);
    write_u16_le(
        raw + 4U,
        OPENRIDE_ORMAP_PYRAMID_BUILDING_BLOB_VERSION);
    write_u16_le(raw + 6U, 0U);
    write_u32_le(
        raw + 8U,
        bucket->polygon_count);
    write_u32_le(
        raw + 12U,
        (uint32_t)bucket->vertex_count);

    if (bucket->body_size > 0U) {
        memcpy(
            raw + 16U,
            bucket->body,
            bucket->body_size);
    }

    const uLongf bound =
        compressBound((uLong)raw_size);

    unsigned char *compressed =
        malloc((size_t)bound);

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
    *compressed_size_out =
        (size_t)compressed_size;
    *raw_size_out = raw_size64;
    return true;
}

static bool write_building_tiles(
    sqlite3 *db,
    BuildingBuildContext *context,
    char *error,
    size_t error_size)
{
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(
            db,
            "INSERT INTO building_tiles("
            "zoom,tile_column,tile_row,tile_data"
            ") VALUES(?1,?2,?3,?4)",
            -1,
            &stmt,
            NULL) != SQLITE_OK) {
        building_set_error(
            error,
            error_size,
            sqlite3_errmsg(db));
        return false;
    }

    bool ok = true;

    for (uint32_t i = 0U;
         i < context->tiles.capacity && ok;
         ++i) {
        const BuildingTileBucket *bucket =
            &context->tiles.buckets[i];

        if (!bucket->used
            || bucket->polygon_count == 0U) {
            continue;
        }

        unsigned char *compressed = NULL;
        size_t compressed_size = 0U;
        uint64_t raw_size = 0U;

        if (!encode_building_bucket(
                bucket,
                &compressed,
                &compressed_size,
                &raw_size)) {
            building_set_error(
                error,
                error_size,
                "unable to encode ORB1 building tile");
            ok = false;
            break;
        }

        int x = 0;
        int y = 0;

        decode_building_tile_key(
            bucket->key,
            &x,
            &y);

        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        sqlite3_bind_int(
            stmt,
            1,
            OPENRIDE_ORMAP_PYRAMID_BUILDING_ZOOM);
        sqlite3_bind_int(stmt, 2, x);
        sqlite3_bind_int(stmt, 3, y);
        sqlite3_bind_blob(
            stmt,
            4,
            compressed,
            (int)compressed_size,
            SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            building_set_error(
                error,
                error_size,
                sqlite3_errmsg(db));
            ok = false;
        } else {
            ++context->stats->tiles_total;
            context->stats->raw_bytes += raw_size;
            context->stats->compressed_bytes +=
                compressed_size;
        }

        free(compressed);
    }

    sqlite3_finalize(stmt);
    return ok;
}

bool openride_ormap_pyramid_buildings_append(
    const char *pbf_path,
    const char *ormap11_path,
    OpenRideORMapPyramidBuildingBuildStats *stats_out,
    char *error,
    size_t error_size)
{
    if (!pbf_path || !ormap11_path) {
        building_set_error(
            error,
            error_size,
            "invalid v11 building build arguments");
        return false;
    }

    OpenRideORMapPyramidBuildingBuildStats stats = {0};

    BuildingBuildContext context = {
        .stats = &stats
    };

    OpenRideOSMMapFeatureStats osm_stats = {0};

    bool ok =
        openride_osm_pbf_visit_building_footprints(
            pbf_path,
            building_visitor,
            &context,
            &osm_stats,
            error,
            error_size);

    stats.osm_ways_seen = osm_stats.osm_way_count;

    if (context.failed) {
        ok = false;

        if (!error || error[0] == '\0') {
            building_set_error(
                error,
                error_size,
                "out of memory building v11 footprints");
        }
    }

    sqlite3 *db = NULL;

    if (ok
        && sqlite3_open_v2(
            ormap11_path,
            &db,
            SQLITE_OPEN_READWRITE,
            NULL) != SQLITE_OK) {
        building_set_error(
            error,
            error_size,
            db
                ? sqlite3_errmsg(db)
                : "unable to open v11 database for buildings");
        ok = false;
    }

    if (ok) {
        ok = validate_v11_database(
            db,
            error,
            error_size);
    }

    if (ok) {
        ok = building_exec_sql(
            db,
            "BEGIN IMMEDIATE;"
            "DROP TABLE IF EXISTS building_tiles;"
            "CREATE TABLE building_tiles("
            "zoom INTEGER NOT NULL,"
            "tile_column INTEGER NOT NULL,"
            "tile_row INTEGER NOT NULL,"
            "tile_data BLOB NOT NULL,"
            "PRIMARY KEY(zoom,tile_column,tile_row)"
            ") WITHOUT ROWID;"
            "DELETE FROM metadata "
            "WHERE name IN("
            "'building_zoom',"
            "'building_policy',"
            "'building_blob'"
            ");"
            "INSERT INTO metadata(name,value) "
            "VALUES('building_zoom','16');"
            "INSERT INTO metadata(name,value) "
            "VALUES('building_policy','closed-way-footprints-z16');"
            "INSERT INTO metadata(name,value) "
            "VALUES('building_blob','ORB1');",
            error,
            error_size);
    }

    if (ok) {
        ok = write_building_tiles(
            db,
            &context,
            error,
            error_size);
    }

    if (ok) {
        ok = building_exec_sql(
            db,
            "COMMIT;PRAGMA optimize;",
            error,
            error_size);
    } else if (db) {
        sqlite3_exec(
            db,
            "ROLLBACK;",
            NULL,
            NULL,
            NULL);
    }

    if (db) sqlite3_close(db);

    building_tile_map_destroy(&context.tiles);
    free(context.world);
    free(context.clip_a);
    free(context.clip_b);
    free(context.quantized);

    if (stats_out) {
        *stats_out = stats;
    }

    if (ok) {
        building_set_error(error, error_size, "");
    }

    return ok;
}

static bool building_table_exists(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(
            db,
            "SELECT 1 FROM sqlite_master "
            "WHERE type='table' AND name='building_tiles'",
            -1,
            &stmt,
            NULL) != SQLITE_OK) {
        return false;
    }

    const bool exists =
        sqlite3_step(stmt) == SQLITE_ROW;

    sqlite3_finalize(stmt);
    return exists;
}

static bool inspect_building_blob(
    const void *blob,
    int blob_size,
    uint32_t *polygon_count_out,
    uint32_t *vertex_count_out)
{
    if (!blob
        || blob_size <= 0
        || !polygon_count_out
        || !vertex_count_out) {
        return false;
    }

    *polygon_count_out = 0U;
    *vertex_count_out = 0U;

    uLongf raw_capacity =
        (uLongf)blob_size * 5U + 64U;

    unsigned char *raw = NULL;
    int zrc = Z_BUF_ERROR;

    for (int attempt = 0;
         attempt < 12 && zrc == Z_BUF_ERROR;
         ++attempt) {
        unsigned char *grown =
            realloc(raw, (size_t)raw_capacity);

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

        if (raw_capacity > UINT32_MAX / 2U) {
            break;
        }

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

    const uint32_t polygon_count =
        read_u32_le(raw + 8U);
    const uint32_t expected_vertices =
        read_u32_le(raw + 12U);

    size_t offset = 16U;
    uint64_t vertices = 0U;

    for (uint32_t polygon = 0U;
         polygon < polygon_count;
         ++polygon) {
        if (offset + 4U > raw_capacity) {
            free(raw);
            return false;
        }

        const uint16_t count =
            read_u16_le(raw + offset);
        const uint16_t flags =
            read_u16_le(raw + offset + 2U);

        offset += 4U;

        if (count < 3U
            || flags != 0U
            || (uint64_t)count * 4U
                > raw_capacity - offset) {
            free(raw);
            return false;
        }

        offset += (size_t)count * 4U;
        vertices += count;
    }

    const bool valid =
        offset == raw_capacity
        && vertices == expected_vertices
        && vertices <= UINT32_MAX;

    free(raw);

    if (!valid) return false;

    *polygon_count_out = polygon_count;
    *vertex_count_out = (uint32_t)vertices;
    return true;
}

bool openride_ormap_pyramid_buildings_inspect(
    const char *path,
    OpenRideORMapPyramidSurfaceInspectStats *stats,
    char *error,
    size_t error_size)
{
    if (!path || !stats) {
        building_set_error(
            error,
            error_size,
            "invalid building inspection arguments");
        return false;
    }

    sqlite3 *db = NULL;

    if (sqlite3_open_v2(
            path,
            &db,
            SQLITE_OPEN_READONLY,
            NULL) != SQLITE_OK) {
        building_set_error(
            error,
            error_size,
            db
                ? sqlite3_errmsg(db)
                : "unable to open v11 building database");
        if (db) sqlite3_close(db);
        return false;
    }

    if (!building_table_exists(db)) {
        sqlite3_close(db);
        building_set_error(error, error_size, "");
        return true;
    }

    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(
            db,
            "SELECT zoom,tile_data,LENGTH(tile_data) "
            "FROM building_tiles "
            "ORDER BY tile_column,tile_row",
            -1,
            &stmt,
            NULL) != SQLITE_OK) {
        building_set_error(
            error,
            error_size,
            sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }

    bool ok = true;
    int rc = SQLITE_ROW;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const int zoom =
            sqlite3_column_int(stmt, 0);
        const void *blob =
            sqlite3_column_blob(stmt, 1);
        const int blob_size =
            sqlite3_column_int(stmt, 2);

        ++stats->building_tiles;

        if (blob_size > 0) {
            stats->building_compressed_bytes +=
                (uint64_t)blob_size;
        }

        if (zoom
            != OPENRIDE_ORMAP_PYRAMID_BUILDING_ZOOM) {
            ++stats->malformed_building_tiles;
            ok = false;
            continue;
        }

        uint32_t polygons = 0U;
        uint32_t vertices = 0U;

        if (!inspect_building_blob(
                blob,
                blob_size,
                &polygons,
                &vertices)) {
            ++stats->malformed_building_tiles;
            ok = false;
            continue;
        }

        stats->building_tile_polygons += polygons;
        stats->building_vertices += vertices;

        if (polygons
            > stats->max_buildings_per_tile) {
            stats->max_buildings_per_tile =
                polygons;
        }
    }

    if (rc != SQLITE_DONE) {
        building_set_error(
            error,
            error_size,
            sqlite3_errmsg(db));
        ok = false;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (!ok) {
        if (!error || error[0] == '\0') {
            building_set_error(
                error,
                error_size,
                "invalid ORB1 building tile data");
        }
        return false;
    }

    building_set_error(error, error_size, "");
    return true;
}
