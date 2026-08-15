#include "openride/ormap.h"

#include <sqlite3.h>
#include <zlib.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define V7_MASK_LAYER_BYTES \
    (((OPENRIDE_ORMAP_MASK_GRID * OPENRIDE_ORMAP_MASK_GRID) + 7U) / 8U)
#define V7_MASK_RAW_SIZE (12U + V7_MASK_LAYER_BYTES * 3U)

typedef struct V7MaskBucket {
    uint64_t key;
    unsigned char layers[V7_MASK_LAYER_BYTES * 3U];
    unsigned char used;
} V7MaskBucket;

typedef struct V7MaskMap {
    V7MaskBucket *buckets;
    uint32_t capacity;
    uint32_t count;
} V7MaskMap;

bool openride_ormap_build_v6(const char *pbf_path,
                             const char *routing_graph_path,
                             const char *places_database_path,
                             const char *output_path,
                             const char *region_name,
                             OpenRideORMapBuildStats *stats_out,
                             char *error,
                             size_t error_size);

static void v7_set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "unknown error");
}

static uint16_t v7_read_u16_le(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8U);
}

static uint32_t v7_read_u32_le(const unsigned char *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8U)
        | ((uint32_t)p[2] << 16U)
        | ((uint32_t)p[3] << 24U);
}

static void v7_write_u16_le(unsigned char *p, uint16_t value)
{
    p[0] = (unsigned char)(value & 0xffU);
    p[1] = (unsigned char)((value >> 8U) & 0xffU);
}

static void v7_write_u32_le(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)(value & 0xffU);
    p[1] = (unsigned char)((value >> 8U) & 0xffU);
    p[2] = (unsigned char)((value >> 16U) & 0xffU);
    p[3] = (unsigned char)((value >> 24U) & 0xffU);
}

static uint64_t v7_mask_key(int x, int y)
{
    return ((uint64_t)(uint32_t)x << 32U) | (uint32_t)y;
}

static void v7_decode_mask_key(uint64_t key, int *x, int *y)
{
    if (x) *x = (int)(uint32_t)(key >> 32U);
    if (y) *y = (int)(uint32_t)key;
}

static uint32_t v7_hash64(uint64_t value)
{
    value ^= value >> 33U;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33U;
    value *= UINT64_C(0xc4ceb9fe1a85ec53);
    value ^= value >> 33U;
    return (uint32_t)(value ^ (value >> 32U));
}

static bool v7_mask_rehash(V7MaskMap *map, uint32_t capacity)
{
    V7MaskBucket *buckets = calloc(capacity, sizeof(*buckets));
    if (!buckets) return false;
    for (uint32_t i = 0U; i < map->capacity; ++i) {
        V7MaskBucket old = map->buckets[i];
        if (!old.used) continue;
        uint32_t slot = v7_hash64(old.key) & (capacity - 1U);
        while (buckets[slot].used) {
            slot = (slot + 1U) & (capacity - 1U);
        }
        buckets[slot] = old;
    }
    free(map->buckets);
    map->buckets = buckets;
    map->capacity = capacity;
    return true;
}

static V7MaskBucket *v7_mask_get(V7MaskMap *map,
                                 int x,
                                 int y,
                                 bool create)
{
    if (!map) return NULL;
    if (map->capacity == 0U) {
        if (!create || !v7_mask_rehash(map, 1024U)) return NULL;
    }
    if (create && (map->count + 1U) * 10U >= map->capacity * 7U) {
        if (map->capacity > UINT32_MAX / 2U
            || !v7_mask_rehash(map, map->capacity * 2U)) {
            return NULL;
        }
    }
    const uint64_t key = v7_mask_key(x, y);
    uint32_t slot = v7_hash64(key) & (map->capacity - 1U);
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

static void v7_mask_destroy(V7MaskMap *map)
{
    if (!map) return;
    free(map->buckets);
    memset(map, 0, sizeof(*map));
}

static bool v7_bit_get(const unsigned char *bits, uint32_t index)
{
    return bits
        && (bits[index >> 3U] & (unsigned char)(1U << (index & 7U))) != 0U;
}

static void v7_bit_set(unsigned char *bits, uint32_t index)
{
    bits[index >> 3U] |= (unsigned char)(1U << (index & 7U));
}

static bool v7_decompress_blob(const void *blob,
                               int blob_size,
                               unsigned char **raw_out,
                               size_t *raw_size_out)
{
    *raw_out = NULL;
    *raw_size_out = 0U;
    if (!blob || blob_size < 5) return false;
    const unsigned char *bytes = blob;
    const uint32_t raw_size = v7_read_u32_le(bytes);
    if (raw_size == 0U || raw_size > 1024U * 1024U) return false;
    unsigned char *raw = malloc(raw_size);
    if (!raw) return false;
    uLongf output_size = raw_size;
    const int rc = uncompress(raw,
                              &output_size,
                              bytes + 4U,
                              (uLong)(blob_size - 4));
    if (rc != Z_OK || output_size != raw_size) {
        free(raw);
        return false;
    }
    *raw_out = raw;
    *raw_size_out = raw_size;
    return true;
}

static bool v7_compress_blob(const unsigned char *raw,
                             size_t raw_size,
                             unsigned char **blob_out,
                             size_t *blob_size_out)
{
    *blob_out = NULL;
    *blob_size_out = 0U;
    if (!raw || raw_size == 0U || raw_size > UINT32_MAX) return false;
    uLongf capacity = compressBound((uLong)raw_size);
    unsigned char *blob = malloc((size_t)capacity + 4U);
    if (!blob) return false;
    v7_write_u32_le(blob, (uint32_t)raw_size);
    const int rc = compress2(blob + 4U,
                             &capacity,
                             raw,
                             (uLong)raw_size,
                             Z_BEST_SPEED);
    if (rc != Z_OK) {
        free(blob);
        return false;
    }
    *blob_out = blob;
    *blob_size_out = (size_t)capacity + 4U;
    return true;
}

static bool v7_accumulate_detail_tile(V7MaskMap *overview,
                                      int tile_x,
                                      int tile_y,
                                      const unsigned char *raw,
                                      size_t raw_size)
{
    if (!overview || !raw || raw_size != V7_MASK_RAW_SIZE
        || memcmp(raw, "ORM1", 4U) != 0
        || v7_read_u16_le(raw + 4U) != 1U
        || raw[6] != OPENRIDE_ORMAP_MASK_GRID
        || raw[7] != 3U
        || v7_read_u32_le(raw + 8U) != V7_MASK_LAYER_BYTES) {
        return false;
    }

    const int parent_x = tile_x >> 1;
    const int parent_y = tile_y >> 1;
    const uint32_t child_x = (uint32_t)tile_x & 1U;
    const uint32_t child_y = (uint32_t)tile_y & 1U;
    V7MaskBucket *parent = v7_mask_get(overview,
                                       parent_x,
                                       parent_y,
                                       true);
    if (!parent) return false;

    for (uint32_t layer = 0U; layer < 3U; ++layer) {
        const unsigned char *source = raw + 12U
            + (size_t)layer * V7_MASK_LAYER_BYTES;
        unsigned char *target = parent->layers
            + (size_t)layer * V7_MASK_LAYER_BYTES;
        for (uint32_t y = 0U; y < OPENRIDE_ORMAP_MASK_GRID; ++y) {
            for (uint32_t x = 0U; x < OPENRIDE_ORMAP_MASK_GRID; ++x) {
                if (!v7_bit_get(source,
                                y * OPENRIDE_ORMAP_MASK_GRID + x)) {
                    continue;
                }
                /*
                 * z15 is one tile zoom below z16 while both tile payloads use
                 * the same 32x32 grid. A parent cell therefore represents a
                 * 2x2 block of detail cells. OR aggregation moves an outline
                 * by at most one z16 cell while cutting visible tile count by
                 * roughly four at the problematic z13-z14 camera range.
                 */
                const uint32_t parent_local_x =
                    (child_x * OPENRIDE_ORMAP_MASK_GRID + x) >> 1U;
                const uint32_t parent_local_y =
                    (child_y * OPENRIDE_ORMAP_MASK_GRID + y) >> 1U;
                v7_bit_set(target,
                           parent_local_y * OPENRIDE_ORMAP_MASK_GRID
                           + parent_local_x);
            }
        }
    }
    return true;
}

static bool v7_load_overview_masks(sqlite3 *db,
                                   V7MaskMap *overview,
                                   char *error,
                                   size_t error_size)
{
    sqlite3_stmt *select = NULL;
    if (sqlite3_prepare_v2(
            db,
            "SELECT tile_column,tile_row,tile_data FROM mask_tiles "
            "WHERE zoom_level=?1 ORDER BY tile_row,tile_column",
            -1,
            &select,
            NULL) != SQLITE_OK) {
        v7_set_error(error, error_size, sqlite3_errmsg(db));
        return false;
    }
    sqlite3_bind_int(select, 1, OPENRIDE_ORMAP_MASK_DETAIL_ZOOM);

    bool ok = true;
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(select)) == SQLITE_ROW) {
        const int tile_x = sqlite3_column_int(select, 0);
        const int tile_y = sqlite3_column_int(select, 1);
        unsigned char *raw = NULL;
        size_t raw_size = 0U;
        if (!v7_decompress_blob(sqlite3_column_blob(select, 2),
                                sqlite3_column_bytes(select, 2),
                                &raw,
                                &raw_size)
            || !v7_accumulate_detail_tile(overview,
                                          tile_x,
                                          tile_y,
                                          raw,
                                          raw_size)) {
            free(raw);
            v7_set_error(error,
                         error_size,
                         "unable to build z15 mask overview from z16 tiles");
            ok = false;
            break;
        }
        free(raw);
    }
    if (ok && rc != SQLITE_DONE) {
        v7_set_error(error, error_size, sqlite3_errmsg(db));
        ok = false;
    }
    sqlite3_finalize(select);
    return ok;
}

static bool v7_write_overview_masks(sqlite3 *db,
                                    const V7MaskMap *overview,
                                    uint64_t *written_out,
                                    char *error,
                                    size_t error_size)
{
    if (written_out) *written_out = 0U;
    if (sqlite3_exec(db,
                     "DELETE FROM mask_tiles WHERE zoom_level="
                     "15",
                     NULL,
                     NULL,
                     NULL) != SQLITE_OK) {
        v7_set_error(error, error_size, sqlite3_errmsg(db));
        return false;
    }

    sqlite3_stmt *insert = NULL;
    if (sqlite3_prepare_v2(
            db,
            "INSERT INTO mask_tiles(zoom_level,tile_column,tile_row,tile_data) "
            "VALUES(?1,?2,?3,?4)",
            -1,
            &insert,
            NULL) != SQLITE_OK) {
        v7_set_error(error, error_size, sqlite3_errmsg(db));
        return false;
    }

    bool ok = true;
    uint64_t written = 0U;
    for (uint32_t i = 0U; i < overview->capacity && ok; ++i) {
        const V7MaskBucket *bucket = &overview->buckets[i];
        if (!bucket->used) continue;
        bool has_data = false;
        for (size_t b = 0U; b < sizeof(bucket->layers); ++b) {
            if (bucket->layers[b] != 0U) {
                has_data = true;
                break;
            }
        }
        if (!has_data) continue;

        unsigned char raw[V7_MASK_RAW_SIZE];
        memcpy(raw, "ORM1", 4U);
        v7_write_u16_le(raw + 4U, 1U);
        raw[6] = OPENRIDE_ORMAP_MASK_GRID;
        raw[7] = 3U;
        v7_write_u32_le(raw + 8U, V7_MASK_LAYER_BYTES);
        memcpy(raw + 12U,
               bucket->layers,
               sizeof(bucket->layers));

        unsigned char *blob = NULL;
        size_t blob_size = 0U;
        if (!v7_compress_blob(raw,
                              sizeof(raw),
                              &blob,
                              &blob_size)) {
            v7_set_error(error,
                         error_size,
                         "unable to compress z15 mask overview tile");
            ok = false;
            break;
        }

        int tile_x = 0;
        int tile_y = 0;
        v7_decode_mask_key(bucket->key, &tile_x, &tile_y);
        sqlite3_reset(insert);
        sqlite3_clear_bindings(insert);
        sqlite3_bind_int(insert, 1, OPENRIDE_ORMAP_MASK_OVERVIEW_ZOOM);
        sqlite3_bind_int(insert, 2, tile_x);
        sqlite3_bind_int(insert, 3, tile_y);
        sqlite3_bind_blob(insert,
                          4,
                          blob,
                          (int)blob_size,
                          SQLITE_TRANSIENT);
        if (sqlite3_step(insert) != SQLITE_DONE) {
            v7_set_error(error, error_size, sqlite3_errmsg(db));
            ok = false;
        } else {
            ++written;
        }
        free(blob);
    }
    sqlite3_finalize(insert);
    if (written_out) *written_out = written;
    return ok;
}

static bool v7_write_metadata(sqlite3 *db,
                              char *error,
                              size_t error_size)
{
    sqlite3_stmt *upsert = NULL;
    if (sqlite3_prepare_v2(
            db,
            "INSERT INTO metadata(name,value) VALUES(?1,?2) "
            "ON CONFLICT(name) DO UPDATE SET value=excluded.value",
            -1,
            &upsert,
            NULL) != SQLITE_OK) {
        v7_set_error(error, error_size, sqlite3_errmsg(db));
        return false;
    }

    char version[16];
    char overview_zoom[16];
    snprintf(version, sizeof(version), "%u", OPENRIDE_ORMAP_FORMAT_VERSION);
    snprintf(overview_zoom,
             sizeof(overview_zoom),
             "%d",
             OPENRIDE_ORMAP_MASK_OVERVIEW_ZOOM);
    const char *pairs[][2] = {
        {"format_version", version},
        {"maskoverviewzoom", overview_zoom}
    };
    bool ok = true;
    for (size_t i = 0U; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
        sqlite3_reset(upsert);
        sqlite3_clear_bindings(upsert);
        sqlite3_bind_text(upsert, 1, pairs[i][0], -1, SQLITE_STATIC);
        sqlite3_bind_text(upsert, 2, pairs[i][1], -1, SQLITE_TRANSIENT);
        if (sqlite3_step(upsert) != SQLITE_DONE) {
            v7_set_error(error, error_size, sqlite3_errmsg(db));
            ok = false;
            break;
        }
    }
    sqlite3_finalize(upsert);
    return ok;
}

static bool v7_postprocess_masks(const char *output_path,
                                 OpenRideORMapBuildStats *stats,
                                 char *error,
                                 size_t error_size)
{
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(output_path,
                        &db,
                        SQLITE_OPEN_READWRITE,
                        NULL) != SQLITE_OK) {
        v7_set_error(error,
                     error_size,
                     db ? sqlite3_errmsg(db) : "unable to reopen .ormap for v7");
        if (db) sqlite3_close(db);
        return false;
    }

    V7MaskMap overview = {0};
    bool ok = sqlite3_exec(db, "BEGIN", NULL, NULL, NULL) == SQLITE_OK;
    if (!ok) v7_set_error(error, error_size, sqlite3_errmsg(db));
    if (ok) ok = v7_load_overview_masks(db, &overview, error, error_size);

    uint64_t written = 0U;
    if (ok) {
        ok = v7_write_overview_masks(db,
                                     &overview,
                                     &written,
                                     error,
                                     error_size);
    }
    if (ok) ok = v7_write_metadata(db, error, error_size);

    if (ok) {
        ok = sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK;
        if (!ok) v7_set_error(error, error_size, sqlite3_errmsg(db));
    } else {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
    }

    v7_mask_destroy(&overview);
    sqlite3_close(db);
    if (ok && stats) stats->mask_tiles_written += written;
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
    if (!openride_ormap_build_v6(pbf_path,
                                 routing_graph_path,
                                 places_database_path,
                                 output_path,
                                 region_name,
                                 stats_out,
                                 error,
                                 error_size)) {
        return false;
    }

    if (!v7_postprocess_masks(output_path,
                              stats_out,
                              error,
                              error_size)) {
        remove(output_path);
        return false;
    }

    v7_set_error(error, error_size, "");
    return true;
}
