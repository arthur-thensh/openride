#include "openride/ormap_pyramid_overlay.h"

#include <limits.h>
#include <math.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define ORL_HEADER_SIZE 12U
#define ORL_RECORD_SIZE 12U
#define ORL_OUTER_SIZE 4U
#define BUILD_INITIAL_CAPACITY 1024U
#define BUILD_LOAD_NUMERATOR 7U
#define BUILD_LOAD_DENOMINATOR 10U

typedef struct BuildBucket {
    bool occupied;
    int x;
    int y;
    OpenRideORMapPyramidOverlayLineRecord *records;
    uint32_t count;
    uint32_t capacity;
} BuildBucket;

typedef struct BuildBucketMap {
    BuildBucket *buckets;
    uint32_t count;
    uint32_t capacity;
} BuildBucketMap;

struct OpenRideORMapPyramidOverlayMap {
    sqlite3 *db;
    sqlite3_stmt *load_stmt;
    OpenRideORMapLabel *labels;
    uint32_t label_count;
    bool roads_available;
    bool waterways_available;
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
}

static void set_sql_error(char *error,
                          size_t error_size,
                          sqlite3 *db,
                          const char *prefix)
{
    if (!error || error_size == 0U) return;
    snprintf(error,
             error_size,
             "%s: %s",
             prefix ? prefix : "SQLite error",
             db ? sqlite3_errmsg(db) : "unknown");
}

static bool exec_sql(sqlite3 *db,
                     const char *sql,
                     char *error,
                     size_t error_size)
{
    char *sqlite_error = NULL;
    const int rc = sqlite3_exec(db, sql, NULL, NULL, &sqlite_error);
    if (rc == SQLITE_OK) return true;

    if (error && error_size > 0U) {
        snprintf(error,
                 error_size,
                 "SQLite: %s",
                 sqlite_error ? sqlite_error : sqlite3_errmsg(db));
    }
    sqlite3_free(sqlite_error);
    return false;
}

static void write_u16(unsigned char *p, uint16_t value)
{
    p[0] = (unsigned char)(value & 0xffU);
    p[1] = (unsigned char)((value >> 8U) & 0xffU);
}

static void write_u32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)(value & 0xffU);
    p[1] = (unsigned char)((value >> 8U) & 0xffU);
    p[2] = (unsigned char)((value >> 16U) & 0xffU);
    p[3] = (unsigned char)((value >> 24U) & 0xffU);
}

static uint16_t read_u16(const unsigned char *p)
{
    return (uint16_t)p[0]
        | (uint16_t)((uint16_t)p[1] << 8U);
}

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8U)
        | ((uint32_t)p[2] << 16U)
        | ((uint32_t)p[3] << 24U);
}

static uint32_t tile_hash(int x, int y)
{
    uint32_t value = (uint32_t)x * UINT32_C(0x85ebca6b);
    value ^= (uint32_t)y * UINT32_C(0xc2b2ae35);
    value ^= value >> 16U;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15U;
    return value;
}

static void build_bucket_destroy(BuildBucket *bucket)
{
    if (!bucket) return;
    free(bucket->records);
    memset(bucket, 0, sizeof(*bucket));
}

static void build_map_destroy(BuildBucketMap *map)
{
    if (!map) return;
    for (uint32_t i = 0U; i < map->capacity; ++i) {
        if (map->buckets[i].occupied) build_bucket_destroy(&map->buckets[i]);
    }
    free(map->buckets);
    memset(map, 0, sizeof(*map));
}

static bool build_map_insert_existing(BuildBucket *table,
                                      uint32_t capacity,
                                      BuildBucket bucket)
{
    uint32_t slot = tile_hash(bucket.x, bucket.y) & (capacity - 1U);
    for (uint32_t probe = 0U; probe < capacity; ++probe) {
        if (!table[slot].occupied) {
            table[slot] = bucket;
            return true;
        }
        slot = (slot + 1U) & (capacity - 1U);
    }
    return false;
}

static bool build_map_rehash(BuildBucketMap *map, uint32_t capacity)
{
    if (!map || capacity < BUILD_INITIAL_CAPACITY
        || (capacity & (capacity - 1U)) != 0U) {
        return false;
    }

    BuildBucket *next = calloc(capacity, sizeof(*next));
    if (!next) return false;

    for (uint32_t i = 0U; i < map->capacity; ++i) {
        if (!map->buckets[i].occupied) continue;
        if (!build_map_insert_existing(next, capacity, map->buckets[i])) {
            free(next);
            return false;
        }
    }

    free(map->buckets);
    map->buckets = next;
    map->capacity = capacity;
    return true;
}

static bool build_map_ensure(BuildBucketMap *map)
{
    if (!map) return false;
    if (map->capacity == 0U) {
        return build_map_rehash(map, BUILD_INITIAL_CAPACITY);
    }

    if ((uint64_t)(map->count + 1U) * BUILD_LOAD_DENOMINATOR
        <= (uint64_t)map->capacity * BUILD_LOAD_NUMERATOR) {
        return true;
    }

    if (map->capacity > UINT32_MAX / 2U) return false;
    return build_map_rehash(map, map->capacity * 2U);
}

static BuildBucket *build_map_get(BuildBucketMap *map, int x, int y)
{
    if (!map || !build_map_ensure(map)) return NULL;

    uint32_t slot = tile_hash(x, y) & (map->capacity - 1U);
    for (uint32_t probe = 0U; probe < map->capacity; ++probe) {
        BuildBucket *bucket = &map->buckets[slot];
        if (!bucket->occupied) {
            *bucket = (BuildBucket){
                .occupied = true,
                .x = x,
                .y = y
            };
            ++map->count;
            return bucket;
        }
        if (bucket->x == x && bucket->y == y) return bucket;
        slot = (slot + 1U) & (map->capacity - 1U);
    }

    return NULL;
}

static bool bucket_append(BuildBucket *bucket,
                          OpenRideORMapPyramidOverlayLineRecord record)
{
    if (!bucket) return false;
    if (bucket->count == bucket->capacity) {
        uint32_t capacity = bucket->capacity ? bucket->capacity * 2U : 128U;
        if (capacity < bucket->capacity) return false;
        OpenRideORMapPyramidOverlayLineRecord *grown =
            realloc(bucket->records, (size_t)capacity * sizeof(*grown));
        if (!grown) return false;
        bucket->records = grown;
        bucket->capacity = capacity;
    }
    bucket->records[bucket->count++] = record;
    return true;
}

static int compare_record(const void *left, const void *right)
{
    const OpenRideORMapPyramidOverlayLineRecord *a = left;
    const OpenRideORMapPyramidOverlayLineRecord *b = right;
    if (a->kind != b->kind) return a->kind < b->kind ? -1 : 1;
    if (a->flags != b->flags) return a->flags < b->flags ? -1 : 1;
    if (a->aux != b->aux) return a->aux < b->aux ? -1 : 1;
    if (a->x1 != b->x1) return a->x1 < b->x1 ? -1 : 1;
    if (a->y1 != b->y1) return a->y1 < b->y1 ? -1 : 1;
    if (a->x2 != b->x2) return a->x2 < b->x2 ? -1 : 1;
    if (a->y2 != b->y2) return a->y2 < b->y2 ? -1 : 1;
    return 0;
}

static bool clip_test(double p, double q, double *u1, double *u2)
{
    if (fabs(p) < 1e-18) return q >= 0.0;
    const double r = q / p;
    if (p < 0.0) {
        if (r > *u2) return false;
        if (r > *u1) *u1 = r;
    } else {
        if (r < *u1) return false;
        if (r < *u2) *u2 = r;
    }
    return true;
}

static bool clip_segment(double x1,
                         double y1,
                         double x2,
                         double y2,
                         double min_x,
                         double min_y,
                         double max_x,
                         double max_y,
                         double *cx1,
                         double *cy1,
                         double *cx2,
                         double *cy2)
{
    double u1 = 0.0;
    double u2 = 1.0;
    const double dx = x2 - x1;
    const double dy = y2 - y1;

    if (!clip_test(-dx, x1 - min_x, &u1, &u2)
        || !clip_test(dx, max_x - x1, &u1, &u2)
        || !clip_test(-dy, y1 - min_y, &u1, &u2)
        || !clip_test(dy, max_y - y1, &u1, &u2)
        || u2 < u1) {
        return false;
    }

    *cx1 = x1 + u1 * dx;
    *cy1 = y1 + u1 * dy;
    *cx2 = x1 + u2 * dx;
    *cy2 = y1 + u2 * dy;
    return true;
}

static uint16_t quantize_local(double value)
{
    if (value <= 0.0) return 0U;
    if (value >= 1.0) return UINT16_MAX;
    return (uint16_t)lround(value * 65535.0);
}

static bool emit_segment(BuildBucketMap *map,
                         int target_zoom,
                         double x1,
                         double y1,
                         double x2,
                         double y2,
                         uint8_t kind,
                         uint8_t aux,
                         uint16_t flags)
{
    const int count = 1 << target_zoom;
    double min_x = fmin(x1, x2);
    double max_x = fmax(x1, x2);
    double min_y = fmin(y1, y2);
    double max_y = fmax(y1, y2);
    const double epsilon = 1e-12;

    if (max_x >= 1.0) max_x = 1.0 - epsilon;
    if (max_y >= 1.0) max_y = 1.0 - epsilon;
    if (min_x < 0.0) min_x = 0.0;
    if (min_y < 0.0) min_y = 0.0;

    int first_x = (int)floor(min_x * count);
    int last_x = (int)floor(max_x * count);
    int first_y = (int)floor(min_y * count);
    int last_y = (int)floor(max_y * count);
    if (first_x < 0) first_x = 0;
    if (first_y < 0) first_y = 0;
    if (last_x >= count) last_x = count - 1;
    if (last_y >= count) last_y = count - 1;

    for (int ty = first_y; ty <= last_y; ++ty) {
        for (int tx = first_x; tx <= last_x; ++tx) {
            const double tile_min_x = (double)tx / count;
            const double tile_min_y = (double)ty / count;
            const double tile_max_x = (double)(tx + 1) / count;
            const double tile_max_y = (double)(ty + 1) / count;

            double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
            if (!clip_segment(x1, y1, x2, y2,
                              tile_min_x, tile_min_y,
                              tile_max_x, tile_max_y,
                              &ax, &ay, &bx, &by)) {
                continue;
            }
            if (hypot(bx - ax, by - ay) < 1e-14) continue;

            BuildBucket *bucket = build_map_get(map, tx, ty);
            if (!bucket) return false;

            if (!bucket_append(
                    bucket,
                    (OpenRideORMapPyramidOverlayLineRecord){
                        .x1 = quantize_local(ax * count - tx),
                        .y1 = quantize_local(ay * count - ty),
                        .x2 = quantize_local(bx * count - tx),
                        .y2 = quantize_local(by * count - ty),
                        .kind = kind,
                        .aux = aux,
                        .flags = flags
                    })) {
                return false;
            }
        }
    }
    return true;
}

static bool encode_tile(const BuildBucket *bucket,
                        unsigned char **blob_out,
                        int *blob_size_out,
                        uint64_t *raw_bytes,
                        uint64_t *compressed_bytes)
{
    if (!bucket || !blob_out || !blob_size_out) return false;
    *blob_out = NULL;
    *blob_size_out = 0;

    if (bucket->count > (UINT32_MAX - ORL_HEADER_SIZE) / ORL_RECORD_SIZE) {
        return false;
    }

    const uint32_t raw_size = ORL_HEADER_SIZE + bucket->count * ORL_RECORD_SIZE;
    unsigned char *raw = malloc(raw_size);
    if (!raw) return false;

    memcpy(raw, "ORL1", 4U);
    write_u16(raw + 4U, OPENRIDE_ORMAP_PYRAMID_OVERLAY_VERSION);
    write_u16(raw + 6U, ORL_RECORD_SIZE);
    write_u32(raw + 8U, bucket->count);

    for (uint32_t i = 0U; i < bucket->count; ++i) {
        const OpenRideORMapPyramidOverlayLineRecord *record = &bucket->records[i];
        unsigned char *p = raw + ORL_HEADER_SIZE + i * ORL_RECORD_SIZE;
        write_u16(p + 0U, record->x1);
        write_u16(p + 2U, record->y1);
        write_u16(p + 4U, record->x2);
        write_u16(p + 6U, record->y2);
        p[8] = record->kind;
        p[9] = record->aux;
        write_u16(p + 10U, record->flags);
    }

    uLongf compressed_capacity = compressBound(raw_size);
    if (compressed_capacity > (uLongf)INT_MAX - ORL_OUTER_SIZE) {
        free(raw);
        return false;
    }

    unsigned char *blob = malloc((size_t)compressed_capacity + ORL_OUTER_SIZE);
    if (!blob) {
        free(raw);
        return false;
    }

    write_u32(blob, raw_size);
    const int zrc = compress2(blob + ORL_OUTER_SIZE,
                              &compressed_capacity,
                              raw,
                              raw_size,
                              Z_BEST_SPEED);
    free(raw);
    if (zrc != Z_OK) {
        free(blob);
        return false;
    }

    *blob_out = blob;
    *blob_size_out = (int)compressed_capacity + (int)ORL_OUTER_SIZE;
    if (raw_bytes) *raw_bytes += raw_size;
    if (compressed_bytes) *compressed_bytes += (uint64_t)*blob_size_out;
    return true;
}

static bool write_level(sqlite3 *db,
                        BuildBucketMap *map,
                        OpenRideORMapPyramidOverlayLayer layer,
                        int zoom,
                        uint64_t *tile_count,
                        uint64_t *record_count,
                        uint64_t *raw_bytes,
                        uint64_t *compressed_bytes,
                        char *error,
                        size_t error_size)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "INSERT OR REPLACE INTO overlay_line_tiles"
        "(layer,zoom,tile_column,tile_row,tile_data) VALUES(?,?,?,?,?)";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        set_sql_error(error, error_size, db, "prepare overlay tile insert");
        return false;
    }

    bool ok = true;
    for (uint32_t i = 0U; i < map->capacity; ++i) {
        BuildBucket *bucket = &map->buckets[i];
        if (!bucket->occupied || bucket->count == 0U) continue;

        qsort(bucket->records,
              bucket->count,
              sizeof(*bucket->records),
              compare_record);

        unsigned char *blob = NULL;
        int blob_size = 0;
        if (!encode_tile(bucket,
                         &blob,
                         &blob_size,
                         raw_bytes,
                         compressed_bytes)) {
            set_error(error, error_size, "unable to encode overlay line tile");
            ok = false;
            break;
        }

        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        sqlite3_bind_int(stmt, 1, (int)layer);
        sqlite3_bind_int(stmt, 2, zoom);
        sqlite3_bind_int(stmt, 3, bucket->x);
        sqlite3_bind_int(stmt, 4, bucket->y);
        sqlite3_bind_blob(stmt, 5, blob, blob_size, SQLITE_TRANSIENT);
        const int rc = sqlite3_step(stmt);
        free(blob);
        if (rc != SQLITE_DONE) {
            set_sql_error(error, error_size, db, "write overlay line tile");
            ok = false;
            break;
        }
        if (tile_count) ++*tile_count;
        if (record_count) *record_count += bucket->count;
    }

    sqlite3_finalize(stmt);
    return ok;
}

static bool source_record_world(int source_zoom,
                                int tile_x,
                                int tile_y,
                                uint16_t qx,
                                uint16_t qy,
                                double *world_x,
                                double *world_y)
{
    if (source_zoom < 0 || source_zoom > 30 || !world_x || !world_y) return false;
    const double count = (double)(1U << source_zoom);
    *world_x = ((double)tile_x + (double)qx / 65535.0) / count;
    *world_y = ((double)tile_y + (double)qy / 65535.0) / count;
    return true;
}

static bool build_road_level(OpenRideORMap *source,
                             sqlite3 *db,
                             int source_zoom,
                             int target_zoom,
                             OpenRideORMapPyramidOverlayBuildStats *stats,
                             char *error,
                             size_t error_size)
{
    OpenRideORMapTileCoord *coords = NULL;
    uint32_t coord_count = 0U;
    if (!openride_ormap_list_tiles(source,
                                   OPENRIDE_ORMAP_TILE_LAYER_ROAD,
                                   source_zoom,
                                   &coords,
                                   &coord_count,
                                   error,
                                   error_size)) {
        return false;
    }

    BuildBucketMap map = {0};
    bool ok = true;
    for (uint32_t c = 0U; c < coord_count && ok; ++c) {
        OpenRideORMapRoadTile tile = {0};
        if (!openride_ormap_load_road_tile(source,
                                           source_zoom,
                                           coords[c].x,
                                           coords[c].y,
                                           &tile,
                                           error,
                                           error_size)) {
            ok = false;
            break;
        }

        for (uint32_t r = 0U; r < tile.count; ++r) {
            const OpenRideORMapRoadRecord *record = &tile.records[r];
            double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
            if (!source_record_world(source_zoom, coords[c].x, coords[c].y,
                                     record->x1, record->y1, &x1, &y1)
                || !source_record_world(source_zoom, coords[c].x, coords[c].y,
                                        record->x2, record->y2, &x2, &y2)
                || !emit_segment(&map,
                                 target_zoom,
                                 x1, y1, x2, y2,
                                 record->road_class,
                                 record->surface,
                                 record->flags)) {
                set_error(error, error_size,
                          "out of memory retiling v11 road geometry");
                ok = false;
                break;
            }
        }
        openride_ormap_road_tile_destroy(&tile);
    }
    openride_ormap_tile_coords_destroy(coords);

    if (ok) {
        const int index = target_zoom - OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM;
        ok = write_level(db,
                         &map,
                         OPENRIDE_ORMAP_PYRAMID_OVERLAY_ROAD,
                         target_zoom,
                         &stats->road_tiles_by_zoom[index],
                         &stats->road_records_by_zoom[index],
                         &stats->raw_bytes,
                         &stats->compressed_bytes,
                         error,
                         error_size);
    }
    build_map_destroy(&map);
    return ok;
}

static bool build_water_level(OpenRideORMap *source,
                              sqlite3 *db,
                              int source_zoom,
                              int target_zoom,
                              OpenRideORMapPyramidOverlayBuildStats *stats,
                              char *error,
                              size_t error_size)
{
    OpenRideORMapTileCoord *coords = NULL;
    uint32_t coord_count = 0U;
    if (!openride_ormap_list_tiles(source,
                                   OPENRIDE_ORMAP_TILE_LAYER_WATER,
                                   source_zoom,
                                   &coords,
                                   &coord_count,
                                   error,
                                   error_size)) {
        return false;
    }

    BuildBucketMap map = {0};
    bool ok = true;
    for (uint32_t c = 0U; c < coord_count && ok; ++c) {
        OpenRideORMapWaterTile tile = {0};
        if (!openride_ormap_load_water_tile(source,
                                            source_zoom,
                                            coords[c].x,
                                            coords[c].y,
                                            &tile,
                                            error,
                                            error_size)) {
            ok = false;
            break;
        }

        for (uint32_t r = 0U; r < tile.count; ++r) {
            const OpenRideORMapWaterRecord *record = &tile.records[r];
            double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
            if (!source_record_world(source_zoom, coords[c].x, coords[c].y,
                                     record->x1, record->y1, &x1, &y1)
                || !source_record_world(source_zoom, coords[c].x, coords[c].y,
                                        record->x2, record->y2, &x2, &y2)
                || !emit_segment(&map,
                                 target_zoom,
                                 x1, y1, x2, y2,
                                 record->kind,
                                 0U,
                                 0U)) {
                set_error(error, error_size,
                          "out of memory retiling v11 waterway geometry");
                ok = false;
                break;
            }
        }
        openride_ormap_water_tile_destroy(&tile);
    }
    openride_ormap_tile_coords_destroy(coords);

    if (ok) {
        const int index = target_zoom - OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM;
        ok = write_level(db,
                         &map,
                         OPENRIDE_ORMAP_PYRAMID_OVERLAY_WATERWAY,
                         target_zoom,
                         &stats->water_tiles_by_zoom[index],
                         &stats->water_records_by_zoom[index],
                         &stats->raw_bytes,
                         &stats->compressed_bytes,
                         error,
                         error_size);
    }
    build_map_destroy(&map);
    return ok;
}

static bool copy_labels(OpenRideORMap *source,
                        sqlite3 *db,
                        OpenRideORMapPyramidOverlayBuildStats *stats,
                        char *error,
                        size_t error_size)
{
    uint32_t count = 0U;
    const OpenRideORMapLabel *labels = openride_ormap_labels(source, &count);

    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "INSERT INTO overlay_labels(ordinal,lat_e7,lon_e7,kind,rank,lod,name)"
        " VALUES(?,?,?,?,?,?,?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        set_sql_error(error, error_size, db, "prepare overlay label insert");
        return false;
    }

    bool ok = true;
    for (uint32_t i = 0U; i < count; ++i) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)i);
        sqlite3_bind_int(stmt, 2, labels[i].lat_e7);
        sqlite3_bind_int(stmt, 3, labels[i].lon_e7);
        sqlite3_bind_int(stmt, 4, labels[i].kind);
        sqlite3_bind_int(stmt, 5, labels[i].rank);
        sqlite3_bind_int(stmt, 6, labels[i].lod);
        sqlite3_bind_text(stmt, 7, labels[i].name, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            set_sql_error(error, error_size, db, "write overlay label");
            ok = false;
            break;
        }
    }
    sqlite3_finalize(stmt);
    if (ok) stats->labels = count;
    return ok;
}

static bool metadata_value(sqlite3 *db,
                           const char *name,
                           char *value,
                           size_t value_size)
{
    if (!db || !name || !value || value_size == 0U) return false;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db,
                           "SELECT value FROM metadata WHERE name=?",
                           -1,
                           &stmt,
                           NULL) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(stmt, 0);
        if (text) {
            snprintf(value, value_size, "%s", (const char *)text);
            ok = true;
        }
    }
    sqlite3_finalize(stmt);
    return ok;
}

static bool verify_v11(sqlite3 *db, char *error, size_t error_size)
{
    char value[32] = {0};
    if (!metadata_value(db, "format_version", value, sizeof(value))
        || atoi(value) != (int)OPENRIDE_ORMAP_PYRAMID_FORMAT_VERSION) {
        set_error(error, error_size, "target is not an ORMap v11 database");
        return false;
    }
    return true;
}

bool openride_ormap_pyramid_overlay_append(
    const char *ormap8_path,
    const char *ormap11_path,
    OpenRideORMapPyramidOverlayBuildStats *stats,
    char *error,
    size_t error_size)
{
    if (!ormap8_path || !ormap11_path || !stats) {
        set_error(error, error_size, "invalid overlay build arguments");
        return false;
    }
    memset(stats, 0, sizeof(*stats));

    OpenRideORMap *source = openride_ormap_open(ormap8_path, error, error_size);
    if (!source) return false;
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(source);
    if (!metadata || metadata->format_version != (int)OPENRIDE_ORMAP_FORMAT_VERSION) {
        openride_ormap_close(source);
        set_error(error, error_size,
                  "overlay source must be the stable ORMap v8 file");
        return false;
    }

    sqlite3 *db = NULL;
    if (sqlite3_open_v2(ormap11_path,
                        &db,
                        SQLITE_OPEN_READWRITE,
                        NULL) != SQLITE_OK) {
        set_sql_error(error, error_size, db, "open ORMap v11 overlay target");
        if (db) sqlite3_close(db);
        openride_ormap_close(source);
        return false;
    }

    bool ok = verify_v11(db, error, error_size);
    if (ok) {
        ok = exec_sql(
            db,
            "BEGIN IMMEDIATE;"
            "CREATE TABLE IF NOT EXISTS overlay_line_tiles("
            " layer INTEGER NOT NULL,"
            " zoom INTEGER NOT NULL,"
            " tile_column INTEGER NOT NULL,"
            " tile_row INTEGER NOT NULL,"
            " tile_data BLOB NOT NULL,"
            " PRIMARY KEY(layer,zoom,tile_column,tile_row));"
            "CREATE TABLE IF NOT EXISTS overlay_labels("
            " ordinal INTEGER NOT NULL,"
            " lat_e7 INTEGER NOT NULL,"
            " lon_e7 INTEGER NOT NULL,"
            " kind INTEGER NOT NULL,"
            " rank INTEGER NOT NULL,"
            " lod INTEGER NOT NULL,"
            " name TEXT NOT NULL);"
            "DELETE FROM overlay_line_tiles;"
            "DELETE FROM overlay_labels;",
            error,
            error_size);
    }

    static const int road_source_zoom[] = {
        OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM,
        OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM,
        OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM,
        OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM,
        OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM,
        OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM
    };

    for (int target = OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM;
         ok && target <= OPENRIDE_ORMAP_PYRAMID_ROAD_MAX_ZOOM;
         ++target) {
        const int index = target - OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM;
        ok = build_road_level(source,
                              db,
                              road_source_zoom[index],
                              target,
                              stats,
                              error,
                              error_size);
    }

    for (int target = OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM;
         ok && target <= OPENRIDE_ORMAP_PYRAMID_WATER_MAX_ZOOM;
         ++target) {
        ok = build_water_level(source,
                               db,
                               OPENRIDE_ORMAP_WATER_ZOOM,
                               target,
                               stats,
                               error,
                               error_size);
    }

    if (ok) ok = copy_labels(source, db, stats, error, error_size);

    if (ok) {
        char sql[1024];
        snprintf(sql,
                 sizeof(sql),
                 "INSERT OR REPLACE INTO metadata(name,value) VALUES"
                 "('overlay_format_version','%u'),"
                 "('overlay_road_min_zoom','%d'),"
                 "('overlay_road_max_zoom','%d'),"
                 "('overlay_water_min_zoom','%d'),"
                 "('overlay_water_max_zoom','%d'),"
                 "('overlay_label_count','%llu');"
                 "COMMIT;",
                 OPENRIDE_ORMAP_PYRAMID_OVERLAY_VERSION,
                 OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM,
                 OPENRIDE_ORMAP_PYRAMID_ROAD_MAX_ZOOM,
                 OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM,
                 OPENRIDE_ORMAP_PYRAMID_WATER_MAX_ZOOM,
                 (unsigned long long)stats->labels);
        ok = exec_sql(db, sql, error, error_size);
    }

    if (!ok) (void)sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    sqlite3_close(db);
    openride_ormap_close(source);
    return ok;
}

static bool decode_blob(const void *blob_data,
                        int blob_size,
                        OpenRideORMapPyramidOverlayLineTile *tile,
                        char *error,
                        size_t error_size)
{
    if (!tile) return false;
    memset(tile, 0, sizeof(*tile));
    if (!blob_data || blob_size < (int)ORL_OUTER_SIZE) {
        set_error(error, error_size, "invalid overlay tile blob");
        return false;
    }

    const unsigned char *blob = blob_data;
    const uint32_t raw_size = read_u32(blob);
    if (raw_size < ORL_HEADER_SIZE
        || raw_size > UINT32_C(256) * UINT32_C(1024) * UINT32_C(1024)) {
        set_error(error, error_size, "invalid overlay tile raw size");
        return false;
    }

    unsigned char *raw = malloc(raw_size);
    if (!raw) {
        set_error(error, error_size, "out of memory decoding overlay tile");
        return false;
    }

    uLongf output_size = raw_size;
    const int zrc = uncompress(raw,
                               &output_size,
                               blob + ORL_OUTER_SIZE,
                               (uLong)(blob_size - (int)ORL_OUTER_SIZE));
    if (zrc != Z_OK || output_size != raw_size) {
        free(raw);
        set_error(error, error_size, "unable to decompress overlay tile");
        return false;
    }

    if (memcmp(raw, "ORL1", 4U) != 0
        || read_u16(raw + 4U) != OPENRIDE_ORMAP_PYRAMID_OVERLAY_VERSION
        || read_u16(raw + 6U) != ORL_RECORD_SIZE) {
        free(raw);
        set_error(error, error_size, "unsupported overlay tile payload");
        return false;
    }

    const uint32_t count = read_u32(raw + 8U);
    if (count > (UINT32_MAX - ORL_HEADER_SIZE) / ORL_RECORD_SIZE
        || ORL_HEADER_SIZE + count * ORL_RECORD_SIZE != raw_size) {
        free(raw);
        set_error(error, error_size, "malformed overlay tile record count");
        return false;
    }

    OpenRideORMapPyramidOverlayLineRecord *records = NULL;
    if (count > 0U) {
        records = malloc((size_t)count * sizeof(*records));
        if (!records) {
            free(raw);
            set_error(error, error_size, "out of memory storing overlay tile");
            return false;
        }
    }

    for (uint32_t i = 0U; i < count; ++i) {
        const unsigned char *p = raw + ORL_HEADER_SIZE + i * ORL_RECORD_SIZE;
        records[i] = (OpenRideORMapPyramidOverlayLineRecord){
            .x1 = read_u16(p + 0U),
            .y1 = read_u16(p + 2U),
            .x2 = read_u16(p + 4U),
            .y2 = read_u16(p + 6U),
            .kind = p[8],
            .aux = p[9],
            .flags = read_u16(p + 10U)
        };
    }

    free(raw);
    tile->records = records;
    tile->count = count;
    return true;
}

static bool overlay_count(sqlite3 *db,
                          const char *sql,
                          int parameter,
                          uint64_t *count)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, parameter);
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *count = (uint64_t)sqlite3_column_int64(stmt, 0);
        ok = true;
    }
    sqlite3_finalize(stmt);
    return ok;
}

OpenRideORMapPyramidOverlayMap *
openride_ormap_pyramid_overlay_open(
    const char *ormap11_path,
    char *error,
    size_t error_size)
{
    if (!ormap11_path) {
        set_error(error, error_size, "invalid overlay path");
        return NULL;
    }

    sqlite3 *db = NULL;
    if (sqlite3_open_v2(ormap11_path,
                        &db,
                        SQLITE_OPEN_READONLY,
                        NULL) != SQLITE_OK) {
        set_sql_error(error, error_size, db, "open ORMap v11 overlay");
        if (db) sqlite3_close(db);
        return NULL;
    }

    char version[32] = {0};
    if (!metadata_value(db,
                        "overlay_format_version",
                        version,
                        sizeof(version))
        || atoi(version) != (int)OPENRIDE_ORMAP_PYRAMID_OVERLAY_VERSION) {
        sqlite3_close(db);
        set_error(error, error_size, "ORMap v11 overlay payload not present");
        return NULL;
    }

    OpenRideORMapPyramidOverlayMap *map = calloc(1U, sizeof(*map));
    if (!map) {
        sqlite3_close(db);
        set_error(error, error_size, "out of memory opening overlay");
        return NULL;
    }
    map->db = db;

    if (sqlite3_prepare_v2(
            db,
            "SELECT tile_data FROM overlay_line_tiles"
            " WHERE layer=? AND zoom=? AND tile_column=? AND tile_row=?",
            -1,
            &map->load_stmt,
            NULL) != SQLITE_OK) {
        set_sql_error(error, error_size, db, "prepare overlay tile reader");
        openride_ormap_pyramid_overlay_close(map);
        return NULL;
    }

    uint64_t road_tiles = 0U;
    uint64_t water_tiles = 0U;
    if (!overlay_count(db,
                       "SELECT COUNT(*) FROM overlay_line_tiles WHERE layer=?",
                       OPENRIDE_ORMAP_PYRAMID_OVERLAY_ROAD,
                       &road_tiles)
        || !overlay_count(db,
                          "SELECT COUNT(*) FROM overlay_line_tiles WHERE layer=?",
                          OPENRIDE_ORMAP_PYRAMID_OVERLAY_WATERWAY,
                          &water_tiles)) {
        set_sql_error(error, error_size, db, "inspect overlay layers");
        openride_ormap_pyramid_overlay_close(map);
        return NULL;
    }
    map->roads_available = road_tiles > 0U;
    map->waterways_available = water_tiles > 0U;

    sqlite3_stmt *labels_stmt = NULL;
    if (sqlite3_prepare_v2(
            db,
            "SELECT lat_e7,lon_e7,kind,rank,lod,name"
            " FROM overlay_labels ORDER BY ordinal ASC",
            -1,
            &labels_stmt,
            NULL) != SQLITE_OK) {
        set_sql_error(error, error_size, db, "prepare overlay labels");
        openride_ormap_pyramid_overlay_close(map);
        return NULL;
    }

    uint32_t capacity = 0U;
    for (;;) {
        const int rc = sqlite3_step(labels_stmt);
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            sqlite3_finalize(labels_stmt);
            set_sql_error(error, error_size, db, "load overlay labels");
            openride_ormap_pyramid_overlay_close(map);
            return NULL;
        }

        if (map->label_count == capacity) {
            uint32_t next = capacity ? capacity * 2U : 128U;
            if (next < capacity) {
                sqlite3_finalize(labels_stmt);
                set_error(error, error_size, "too many overlay labels");
                openride_ormap_pyramid_overlay_close(map);
                return NULL;
            }
            OpenRideORMapLabel *grown =
                realloc(map->labels, (size_t)next * sizeof(*grown));
            if (!grown) {
                sqlite3_finalize(labels_stmt);
                set_error(error, error_size, "out of memory loading overlay labels");
                openride_ormap_pyramid_overlay_close(map);
                return NULL;
            }
            map->labels = grown;
            capacity = next;
        }

        OpenRideORMapLabel *label = &map->labels[map->label_count++];
        memset(label, 0, sizeof(*label));
        label->lat_e7 = sqlite3_column_int(labels_stmt, 0);
        label->lon_e7 = sqlite3_column_int(labels_stmt, 1);
        label->kind = sqlite3_column_int(labels_stmt, 2);
        label->rank = sqlite3_column_int(labels_stmt, 3);
        label->lod = (uint8_t)sqlite3_column_int(labels_stmt, 4);
        const unsigned char *name = sqlite3_column_text(labels_stmt, 5);
        if (name) snprintf(label->name, sizeof(label->name), "%s", (const char *)name);
    }
    sqlite3_finalize(labels_stmt);

    set_error(error, error_size, "");
    return map;
}

void openride_ormap_pyramid_overlay_close(OpenRideORMapPyramidOverlayMap *map)
{
    if (!map) return;
    if (map->load_stmt) sqlite3_finalize(map->load_stmt);
    if (map->db) sqlite3_close(map->db);
    free(map->labels);
    free(map);
}

bool openride_ormap_pyramid_overlay_layer_available(
    const OpenRideORMapPyramidOverlayMap *map,
    OpenRideORMapPyramidOverlayLayer layer)
{
    if (!map) return false;
    if (layer == OPENRIDE_ORMAP_PYRAMID_OVERLAY_ROAD) return map->roads_available;
    if (layer == OPENRIDE_ORMAP_PYRAMID_OVERLAY_WATERWAY) return map->waterways_available;
    return false;
}

bool openride_ormap_pyramid_overlay_load_tile(
    OpenRideORMapPyramidOverlayMap *map,
    OpenRideORMapPyramidOverlayLayer layer,
    int zoom,
    int x,
    int y,
    OpenRideORMapPyramidOverlayLineTile *tile,
    char *error,
    size_t error_size)
{
    if (!map || !map->load_stmt || !tile) {
        set_error(error, error_size, "invalid overlay tile load");
        return false;
    }

    memset(tile, 0, sizeof(*tile));
    sqlite3_reset(map->load_stmt);
    sqlite3_clear_bindings(map->load_stmt);
    sqlite3_bind_int(map->load_stmt, 1, (int)layer);
    sqlite3_bind_int(map->load_stmt, 2, zoom);
    sqlite3_bind_int(map->load_stmt, 3, x);
    sqlite3_bind_int(map->load_stmt, 4, y);

    const int rc = sqlite3_step(map->load_stmt);
    if (rc == SQLITE_DONE) {
        set_error(error, error_size, "");
        return true;
    }
    if (rc != SQLITE_ROW) {
        set_sql_error(error, error_size, map->db, "load overlay line tile");
        return false;
    }

    return decode_blob(sqlite3_column_blob(map->load_stmt, 0),
                       sqlite3_column_bytes(map->load_stmt, 0),
                       tile,
                       error,
                       error_size);
}

void openride_ormap_pyramid_overlay_tile_destroy(
    OpenRideORMapPyramidOverlayLineTile *tile)
{
    if (!tile) return;
    free(tile->records);
    memset(tile, 0, sizeof(*tile));
}

const OpenRideORMapLabel *openride_ormap_pyramid_overlay_labels(
    const OpenRideORMapPyramidOverlayMap *map,
    uint32_t *count)
{
    if (count) *count = map ? map->label_count : 0U;
    return map ? map->labels : NULL;
}

bool openride_ormap_pyramid_overlay_inspect(
    const char *ormap11_path,
    OpenRideORMapPyramidOverlayInspectStats *stats,
    char *error,
    size_t error_size)
{
    if (!ormap11_path || !stats) {
        set_error(error, error_size, "invalid overlay inspection arguments");
        return false;
    }
    memset(stats, 0, sizeof(*stats));

    OpenRideORMapPyramidOverlayMap *map =
        openride_ormap_pyramid_overlay_open(ormap11_path, error, error_size);
    if (!map) return false;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(
            map->db,
            "SELECT layer,zoom,tile_data FROM overlay_line_tiles"
            " ORDER BY layer,zoom,tile_column,tile_row",
            -1,
            &stmt,
            NULL) != SQLITE_OK) {
        set_sql_error(error, error_size, map->db, "inspect overlay tiles");
        openride_ormap_pyramid_overlay_close(map);
        return false;
    }

    for (;;) {
        const int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            set_sql_error(error, error_size, map->db, "iterate overlay tiles");
            openride_ormap_pyramid_overlay_close(map);
            return false;
        }

        const int layer = sqlite3_column_int(stmt, 0);
        const int zoom = sqlite3_column_int(stmt, 1);
        const int blob_size = sqlite3_column_bytes(stmt, 2);
        OpenRideORMapPyramidOverlayLineTile tile = {0};
        char local_error[160] = {0};
        if (!decode_blob(sqlite3_column_blob(stmt, 2),
                         blob_size,
                         &tile,
                         local_error,
                         sizeof(local_error))) {
            ++stats->malformed_tiles;
            continue;
        }

        stats->compressed_bytes += (uint64_t)blob_size;
        bool layer_valid = false;
        if (layer == OPENRIDE_ORMAP_PYRAMID_OVERLAY_ROAD
            && zoom >= OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM
            && zoom <= OPENRIDE_ORMAP_PYRAMID_ROAD_MAX_ZOOM) {
            const int index = zoom - OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM;
            ++stats->road_tiles_by_zoom[index];
            stats->road_records_by_zoom[index] += tile.count;
            layer_valid = true;
            for (uint32_t i = 0U; i < tile.count; ++i) {
                if (tile.records[i].kind > OPENRIDE_ROAD_OTHER) {
                    ++stats->invalid_records;
                }
            }
        } else if (layer == OPENRIDE_ORMAP_PYRAMID_OVERLAY_WATERWAY
                   && zoom >= OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM
                   && zoom <= OPENRIDE_ORMAP_PYRAMID_WATER_MAX_ZOOM) {
            const int index = zoom - OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM;
            ++stats->water_tiles_by_zoom[index];
            stats->water_records_by_zoom[index] += tile.count;
            layer_valid = true;
            for (uint32_t i = 0U; i < tile.count; ++i) {
                if (tile.records[i].kind < OPENRIDE_ORMAP_WATERWAY_RIVER
                    || tile.records[i].kind > OPENRIDE_ORMAP_WATERWAY_DRAIN) {
                    ++stats->invalid_records;
                }
            }
        }
        if (!layer_valid) ++stats->invalid_records;
        openride_ormap_pyramid_overlay_tile_destroy(&tile);
    }
    sqlite3_finalize(stmt);

    stats->labels = map->label_count;
    openride_ormap_pyramid_overlay_close(map);
    set_error(error, error_size, "");
    return true;
}
