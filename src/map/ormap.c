#include "openride/ormap.h"
#include "openride/place_search.h"

#include <sqlite3.h>
#include <zlib.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct OpenRideORMap {
    sqlite3 *db;
    sqlite3_stmt *road_query;
    sqlite3_stmt *water_query;
    sqlite3_stmt *area_query;
    sqlite3_stmt *mask_query;
    OpenRideORMapMetadata metadata;
    OpenRideORMapLabel *labels;
    uint32_t label_count;
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "unknown error");
}

static uint16_t read_u16_le(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8U);
}

static uint32_t read_u32_le(const unsigned char *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8U)
        | ((uint32_t)p[2] << 16U)
        | ((uint32_t)p[3] << 24U);
}

static bool parse_int(const char *text, int *value)
{
    if (!text || !value) return false;
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno || end == text || parsed < INT_MIN || parsed > INT_MAX) return false;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end != '\0') return false;
    *value = (int)parsed;
    return true;
}

static bool parse_csv_doubles(const char *text, double *values, size_t count)
{
    if (!text || !values || count == 0U) return false;
    const char *cursor = text;
    for (size_t i = 0U; i < count; ++i) {
        char *end = NULL;
        errno = 0;
        values[i] = strtod(cursor, &end);
        if (errno || end == cursor) return false;
        while (*end == ' ' || *end == '\t') ++end;
        if (i + 1U < count) {
            if (*end != ',') return false;
            cursor = end + 1;
            while (*cursor == ' ' || *cursor == '\t') ++cursor;
        } else if (*end != '\0') {
            return false;
        }
    }
    return true;
}

static bool load_metadata(OpenRideORMap *map, char *error, size_t error_size)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(map->db,
                           "SELECT name,value FROM metadata",
                           -1,
                           &stmt,
                           NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(map->db));
        return false;
    }

    snprintf(map->metadata.name, sizeof(map->metadata.name), "OpenRide offline map");
    snprintf(map->metadata.attribution,
             sizeof(map->metadata.attribution),
             "OpenStreetMap contributors");
    map->metadata.format_version = 1;
    map->metadata.min_zoom = OPENRIDE_ORMAP_MIN_ROAD_ZOOM;
    map->metadata.max_zoom = OPENRIDE_ORMAP_MAX_ZOOM;
    map->metadata.road_max_zoom = OPENRIDE_ORMAP_ROAD_DATA_MAX_ZOOM;
    map->metadata.mask_zoom = OPENRIDE_ORMAP_MASK_ZOOM;
    map->metadata.water_zoom = OPENRIDE_ORMAP_WATER_ZOOM;
    map->metadata.area_coarse_zoom = OPENRIDE_ORMAP_AREA_COARSE_ZOOM;
    map->metadata.area_detail_zoom = OPENRIDE_ORMAP_AREA_DETAIL_ZOOM;

    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *key = (const char *)sqlite3_column_text(stmt, 0);
        const char *value = (const char *)sqlite3_column_text(stmt, 1);
        if (!key || !value) continue;
        if (strcmp(key, "name") == 0) {
            snprintf(map->metadata.name, sizeof(map->metadata.name), "%s", value);
        } else if (strcmp(key, "attribution") == 0) {
            snprintf(map->metadata.attribution, sizeof(map->metadata.attribution), "%s", value);
        } else if (strcmp(key, "minzoom") == 0) {
            (void)parse_int(value, &map->metadata.min_zoom);
        } else if (strcmp(key, "maxzoom") == 0) {
            (void)parse_int(value, &map->metadata.max_zoom);
        } else if (strcmp(key, "roadmaxzoom") == 0) {
            (void)parse_int(value, &map->metadata.road_max_zoom);
        } else if (strcmp(key, "maskzoom") == 0) {
            (void)parse_int(value, &map->metadata.mask_zoom);
        } else if (strcmp(key, "waterzoom") == 0) {
            (void)parse_int(value, &map->metadata.water_zoom);
        } else if (strcmp(key, "areacoarsezoom") == 0) {
            (void)parse_int(value, &map->metadata.area_coarse_zoom);
        } else if (strcmp(key, "areadetailzoom") == 0) {
            (void)parse_int(value, &map->metadata.area_detail_zoom);
        } else if (strcmp(key, "center") == 0) {
            double values[3];
            if (parse_csv_doubles(value, values, 3U)) {
                map->metadata.center_lon = values[0];
                map->metadata.center_lat = values[1];
                map->metadata.center_zoom = values[2];
                map->metadata.has_center = true;
            }
        } else if (strcmp(key, "bounds") == 0) {
            double values[4];
            if (parse_csv_doubles(value, values, 4U)) {
                map->metadata.west = values[0];
                map->metadata.south = values[1];
                map->metadata.east = values[2];
                map->metadata.north = values[3];
                map->metadata.has_bounds = true;
            }
        } else if (strcmp(key, "format_version") == 0) {
            int version = 0;
            if (!parse_int(value, &version) || version < 1
                || version > (int)OPENRIDE_ORMAP_FORMAT_VERSION) {
                sqlite3_finalize(stmt);
                set_error(error, error_size, "unsupported .ormap format version");
                return false;
            }
            map->metadata.format_version = version;
        }
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        set_error(error, error_size, sqlite3_errmsg(map->db));
        return false;
    }
    if (map->metadata.format_version == 1) {
        /* v1 had no separate road/water metadata. */
        map->metadata.road_max_zoom = map->metadata.max_zoom;
        map->metadata.water_zoom = OPENRIDE_ORMAP_WATER_ZOOM;
    }
    if (map->metadata.format_version < 3) {
        map->metadata.area_coarse_zoom = OPENRIDE_ORMAP_AREA_COARSE_ZOOM;
        map->metadata.area_detail_zoom = OPENRIDE_ORMAP_AREA_DETAIL_ZOOM;
    }
    if (!map->metadata.has_center && map->metadata.has_bounds) {
        map->metadata.center_lon = (map->metadata.west + map->metadata.east) * 0.5;
        map->metadata.center_lat = (map->metadata.south + map->metadata.north) * 0.5;
        map->metadata.center_zoom = 12.0;
        map->metadata.has_center = true;
    }
    return true;
}

static uint8_t label_lod_from_kind(int kind)
{
    switch ((OpenRidePlaceKind)kind) {
        case OPENRIDE_PLACE_CITY:
            return OPENRIDE_ORMAP_LABEL_LOD_REGIONAL;
        case OPENRIDE_PLACE_TOWN:
            return OPENRIDE_ORMAP_LABEL_LOD_OVERVIEW;
        case OPENRIDE_PLACE_VILLAGE:
        case OPENRIDE_PLACE_SUBURB:
            return OPENRIDE_ORMAP_LABEL_LOD_LOCAL;
        default:
            return OPENRIDE_ORMAP_LABEL_LOD_DETAIL;
    }
}

static bool load_labels(OpenRideORMap *map, char *error, size_t error_size)
{
    sqlite3_stmt *stmt = NULL;
    const bool has_lod = map->metadata.format_version >= 6;
    const char *sql = has_lod
        ? "SELECT lat_e7,lon_e7,kind,rank,name,lod FROM labels ORDER BY lod,rank DESC"
        : "SELECT lat_e7,lon_e7,kind,rank,name FROM labels ORDER BY rank DESC";
    int rc = sqlite3_prepare_v2(map->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        /* Labels are optional in format v1 so old test fixtures stay useful. */
        return true;
    }

    uint32_t count = 0U;
    uint32_t capacity = 0U;
    OpenRideORMapLabel *labels = NULL;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (count == capacity) {
            uint32_t next = capacity == 0U ? 256U : capacity * 2U;
            if (next < capacity) {
                rc = SQLITE_NOMEM;
                break;
            }
            OpenRideORMapLabel *grown = realloc(labels,
                                                (size_t)next * sizeof(*grown));
            if (!grown) {
                rc = SQLITE_NOMEM;
                break;
            }
            labels = grown;
            capacity = next;
        }
        OpenRideORMapLabel *label = &labels[count++];
        memset(label, 0, sizeof(*label));
        label->lat_e7 = sqlite3_column_int(stmt, 0);
        label->lon_e7 = sqlite3_column_int(stmt, 1);
        label->kind = sqlite3_column_int(stmt, 2);
        label->rank = sqlite3_column_int(stmt, 3);
        const char *name = (const char *)sqlite3_column_text(stmt, 4);
        label->lod = has_lod
            ? (uint8_t)sqlite3_column_int(stmt, 5)
            : label_lod_from_kind(label->kind);
        if (label->lod > OPENRIDE_ORMAP_LABEL_LOD_DETAIL) {
            label->lod = OPENRIDE_ORMAP_LABEL_LOD_DETAIL;
        }
        snprintf(label->name, sizeof(label->name), "%s", name ? name : "");
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        free(labels);
        set_error(error, error_size, "unable to read .ormap labels");
        return false;
    }
    map->labels = labels;
    map->label_count = count;
    return true;
}

OpenRideORMap *openride_ormap_open(const char *path,
                                   char *error,
                                   size_t error_size)
{
    if (!path || path[0] == '\0') {
        set_error(error, error_size, ".ormap path is empty");
        return NULL;
    }
    OpenRideORMap *map = calloc(1U, sizeof(*map));
    if (!map) {
        set_error(error, error_size, "out of memory");
        return NULL;
    }
    const int flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX;
    if (sqlite3_open_v2(path, &map->db, flags, NULL) != SQLITE_OK) {
        set_error(error,
                  error_size,
                  map->db ? sqlite3_errmsg(map->db) : "unable to open .ormap");
        openride_ormap_close(map);
        return NULL;
    }
    if (!load_metadata(map, error, error_size)
        || !load_labels(map, error, error_size)) {
        openride_ormap_close(map);
        return NULL;
    }
    if (sqlite3_prepare_v2(map->db,
                           "SELECT tile_data FROM road_tiles "
                           "WHERE zoom_level=?1 AND tile_column=?2 AND tile_row=?3",
                           -1,
                           &map->road_query,
                           NULL) != SQLITE_OK
        || sqlite3_prepare_v2(map->db,
                              "SELECT tile_data FROM mask_tiles "
                              "WHERE zoom_level=?1 AND tile_column=?2 AND tile_row=?3",
                              -1,
                              &map->mask_query,
                              NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(map->db));
        openride_ormap_close(map);
        return NULL;
    }
    if (map->metadata.format_version >= 2
        && sqlite3_prepare_v2(map->db,
                              "SELECT tile_data FROM water_tiles "
                              "WHERE zoom_level=?1 AND tile_column=?2 AND tile_row=?3",
                              -1,
                              &map->water_query,
                              NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(map->db));
        openride_ormap_close(map);
        return NULL;
    }
    if (map->metadata.format_version >= 3
        && sqlite3_prepare_v2(map->db,
                              "SELECT tile_data FROM area_tiles "
                              "WHERE zoom_level=?1 AND tile_column=?2 AND tile_row=?3",
                              -1,
                              &map->area_query,
                              NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(map->db));
        openride_ormap_close(map);
        return NULL;
    }
    set_error(error, error_size, "");
    return map;
}

void openride_ormap_close(OpenRideORMap *map)
{
    if (!map) return;
    if (map->road_query) sqlite3_finalize(map->road_query);
    if (map->water_query) sqlite3_finalize(map->water_query);
    if (map->area_query) sqlite3_finalize(map->area_query);
    if (map->mask_query) sqlite3_finalize(map->mask_query);
    if (map->db) sqlite3_close(map->db);
    free(map->labels);
    free(map);
}

const OpenRideORMapMetadata *openride_ormap_metadata(const OpenRideORMap *map)
{
    return map ? &map->metadata : NULL;
}

bool openride_ormap_list_tiles(OpenRideORMap *map,
                               OpenRideORMapTileLayer layer,
                               int zoom,
                               OpenRideORMapTileCoord **coords,
                               uint32_t *count,
                               char *error,
                               size_t error_size)
{
    if (coords) *coords = NULL;
    if (count) *count = 0U;
    if (!map || !coords || !count) {
        set_error(error, error_size, "invalid .ormap tile list arguments");
        return false;
    }
    const char *sql = NULL;
    switch (layer) {
        case OPENRIDE_ORMAP_TILE_LAYER_ROAD:
            sql = "SELECT tile_column,tile_row FROM road_tiles "
                  "WHERE zoom_level=?1 ORDER BY tile_row,tile_column";
            break;
        case OPENRIDE_ORMAP_TILE_LAYER_WATER:
            if (map->metadata.format_version < 2) {
                set_error(error, error_size, "");
                return true;
            }
            sql = "SELECT tile_column,tile_row FROM water_tiles "
                  "WHERE zoom_level=?1 ORDER BY tile_row,tile_column";
            break;
        case OPENRIDE_ORMAP_TILE_LAYER_AREA:
            if (map->metadata.format_version < 3) {
                set_error(error, error_size, "");
                return true;
            }
            sql = "SELECT tile_column,tile_row FROM area_tiles "
                  "WHERE zoom_level=?1 ORDER BY tile_row,tile_column";
            break;
        case OPENRIDE_ORMAP_TILE_LAYER_MASK:
            sql = "SELECT tile_column,tile_row FROM mask_tiles "
                  "WHERE zoom_level=?1 ORDER BY tile_row,tile_column";
            break;
        default:
            set_error(error, error_size, "invalid .ormap tile layer");
            return false;
    }
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(map->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(map->db));
        return false;
    }
    sqlite3_bind_int(stmt, 1, zoom);
    uint32_t used = 0U;
    uint32_t capacity = 0U;
    OpenRideORMapTileCoord *items = NULL;
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (used == capacity) {
            uint32_t next = capacity == 0U ? 128U : capacity * 2U;
            if (next < capacity) {
                rc = SQLITE_NOMEM;
                break;
            }
            OpenRideORMapTileCoord *grown = realloc(items,
                                                     (size_t)next * sizeof(*grown));
            if (!grown) {
                rc = SQLITE_NOMEM;
                break;
            }
            items = grown;
            capacity = next;
        }
        items[used].x = sqlite3_column_int(stmt, 0);
        items[used].y = sqlite3_column_int(stmt, 1);
        ++used;
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        free(items);
        set_error(error,
                  error_size,
                  rc == SQLITE_NOMEM ? "out of memory listing .ormap tiles"
                                     : sqlite3_errmsg(map->db));
        return false;
    }
    *coords = items;
    *count = used;
    set_error(error, error_size, "");
    return true;
}

void openride_ormap_tile_coords_destroy(OpenRideORMapTileCoord *coords)
{
    free(coords);
}


static bool load_blob(sqlite3 *db,
                      sqlite3_stmt *stmt,
                      int zoom,
                      int x,
                      int y,
                      unsigned char **raw,
                      size_t *raw_size,
                      char *error,
                      size_t error_size)
{
    *raw = NULL;
    *raw_size = 0U;
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    sqlite3_bind_int(stmt, 1, zoom);
    sqlite3_bind_int(stmt, 2, x);
    sqlite3_bind_int(stmt, 3, y);
    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        sqlite3_reset(stmt);
        return false;
    }
    if (rc != SQLITE_ROW) {
        set_error(error, error_size, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        return false;
    }
    const unsigned char *blob = sqlite3_column_blob(stmt, 0);
    const int blob_size = sqlite3_column_bytes(stmt, 0);
    if (!blob || blob_size < 5) {
        sqlite3_reset(stmt);
        return false;
    }
    const uint32_t expected = read_u32_le(blob);
    if (expected == 0U || expected > 16U * 1024U * 1024U) {
        sqlite3_reset(stmt);
        set_error(error, error_size, "invalid compressed .ormap tile size");
        return false;
    }
    unsigned char *buffer = malloc(expected);
    if (!buffer) {
        sqlite3_reset(stmt);
        set_error(error, error_size, "out of memory loading .ormap tile");
        return false;
    }
    uLongf destination = (uLongf)expected;
    const int zrc = uncompress(buffer,
                               &destination,
                               blob + 4,
                               (uLong)(blob_size - 4));
    sqlite3_reset(stmt);
    if (zrc != Z_OK || destination != expected) {
        free(buffer);
        set_error(error, error_size, "unable to decompress .ormap tile");
        return false;
    }
    *raw = buffer;
    *raw_size = expected;
    return true;
}

bool openride_ormap_load_road_tile(OpenRideORMap *map,
                                   int zoom,
                                   int x,
                                   int y,
                                   OpenRideORMapRoadTile *tile,
                                   char *error,
                                   size_t error_size)
{
    if (!tile) return false;
    memset(tile, 0, sizeof(*tile));
    if (!map || !map->road_query) return false;
    unsigned char *raw = NULL;
    size_t raw_size = 0U;
    if (!load_blob(map->db,
                   map->road_query,
                   zoom,
                   x,
                   y,
                   &raw,
                   &raw_size,
                   error,
                   error_size)) {
        return false;
    }
    if (raw_size < 12U || memcmp(raw, "ORR1", 4U) != 0
        || read_u16_le(raw + 4) != 1U
        || read_u16_le(raw + 6) != 12U) {
        free(raw);
        set_error(error, error_size, "invalid .ormap road tile header");
        return false;
    }
    const uint32_t count = read_u32_le(raw + 8);
    if ((uint64_t)12U + (uint64_t)count * 12U != raw_size) {
        free(raw);
        set_error(error, error_size, "invalid .ormap road tile length");
        return false;
    }
    if (count > 0U) {
        tile->records = calloc(count, sizeof(*tile->records));
        if (!tile->records) {
            free(raw);
            set_error(error, error_size, "out of memory decoding road tile");
            return false;
        }
    }
    tile->count = count;
    const unsigned char *p = raw + 12U;
    for (uint32_t i = 0U; i < count; ++i, p += 12U) {
        OpenRideORMapRoadRecord *record = &tile->records[i];
        record->x1 = read_u16_le(p + 0U);
        record->y1 = read_u16_le(p + 2U);
        record->x2 = read_u16_le(p + 4U);
        record->y2 = read_u16_le(p + 6U);
        record->road_class = p[8];
        record->surface = p[9];
        record->flags = read_u16_le(p + 10U);
    }
    free(raw);
    return true;
}

void openride_ormap_road_tile_destroy(OpenRideORMapRoadTile *tile)
{
    if (!tile) return;
    free(tile->records);
    memset(tile, 0, sizeof(*tile));
}

bool openride_ormap_load_water_tile(OpenRideORMap *map,
                                    int zoom,
                                    int x,
                                    int y,
                                    OpenRideORMapWaterTile *tile,
                                    char *error,
                                    size_t error_size)
{
    if (!tile) return false;
    memset(tile, 0, sizeof(*tile));
    if (!map) return false;
    if (map->metadata.format_version < 2) {
        set_error(error, error_size, "");
        return true;
    }
    if (!map->water_query) return false;
    unsigned char *raw = NULL;
    size_t raw_size = 0U;
    if (!load_blob(map->db,
                   map->water_query,
                   zoom,
                   x,
                   y,
                   &raw,
                   &raw_size,
                   error,
                   error_size)) {
        return false;
    }
    if (raw_size < 12U || memcmp(raw, "ORW1", 4U) != 0
        || read_u16_le(raw + 4U) != 1U
        || read_u16_le(raw + 6U) != 10U) {
        free(raw);
        set_error(error, error_size, "invalid .ormap water tile header");
        return false;
    }
    const uint32_t count = read_u32_le(raw + 8U);
    if ((uint64_t)12U + (uint64_t)count * 10U != raw_size) {
        free(raw);
        set_error(error, error_size, "invalid .ormap water tile length");
        return false;
    }
    if (count > 0U) {
        tile->records = calloc(count, sizeof(*tile->records));
        if (!tile->records) {
            free(raw);
            set_error(error, error_size, "out of memory decoding water tile");
            return false;
        }
    }
    tile->count = count;
    const unsigned char *q = raw + 12U;
    for (uint32_t i = 0U; i < count; ++i, q += 10U) {
        OpenRideORMapWaterRecord *record = &tile->records[i];
        record->x1 = read_u16_le(q + 0U);
        record->y1 = read_u16_le(q + 2U);
        record->x2 = read_u16_le(q + 4U);
        record->y2 = read_u16_le(q + 6U);
        record->kind = q[8];
        record->reserved = q[9];
    }
    free(raw);
    return true;
}

void openride_ormap_water_tile_destroy(OpenRideORMapWaterTile *tile)
{
    if (!tile) return;
    free(tile->records);
    memset(tile, 0, sizeof(*tile));
}

bool openride_ormap_load_area_tile(OpenRideORMap *map,
                                   int zoom,
                                   int x,
                                   int y,
                                   OpenRideORMapAreaTile *tile,
                                   char *error,
                                   size_t error_size)
{
    if (!tile) return false;
    memset(tile, 0, sizeof(*tile));
    if (!map) return false;
    if (map->metadata.format_version < 3) {
        set_error(error, error_size, "");
        return true;
    }
    if (!map->area_query) return false;

    unsigned char *raw = NULL;
    size_t raw_size = 0U;
    if (!load_blob(map->db,
                   map->area_query,
                   zoom,
                   x,
                   y,
                   &raw,
                   &raw_size,
                   error,
                   error_size)) {
        return false;
    }
    if (raw_size < 12U || memcmp(raw, "ORA1", 4U) != 0
        || read_u16_le(raw + 4U) != 1U
        || read_u16_le(raw + 6U) != 14U) {
        free(raw);
        set_error(error, error_size, "invalid .ormap area tile header");
        return false;
    }
    const uint32_t count = read_u32_le(raw + 8U);
    if ((uint64_t)12U + (uint64_t)count * 14U != raw_size) {
        free(raw);
        set_error(error, error_size, "invalid .ormap area tile length");
        return false;
    }
    if (count > 0U) {
        tile->triangles = calloc(count, sizeof(*tile->triangles));
        if (!tile->triangles) {
            free(raw);
            set_error(error, error_size, "out of memory decoding area tile");
            return false;
        }
    }
    tile->count = count;
    const unsigned char *q = raw + 12U;
    for (uint32_t i = 0U; i < count; ++i, q += 14U) {
        OpenRideORMapAreaTriangle *triangle = &tile->triangles[i];
        triangle->x1 = read_u16_le(q + 0U);
        triangle->y1 = read_u16_le(q + 2U);
        triangle->x2 = read_u16_le(q + 4U);
        triangle->y2 = read_u16_le(q + 6U);
        triangle->x3 = read_u16_le(q + 8U);
        triangle->y3 = read_u16_le(q + 10U);
        triangle->kind = q[12];
        triangle->reserved = q[13];
    }
    free(raw);
    return true;
}

void openride_ormap_area_tile_destroy(OpenRideORMapAreaTile *tile)
{
    if (!tile) return;
    free(tile->triangles);
    memset(tile, 0, sizeof(*tile));
}

bool openride_ormap_load_mask_tile(OpenRideORMap *map,
                                   int zoom,
                                   int x,
                                   int y,
                                   OpenRideORMapMaskTile *tile,
                                   char *error,
                                   size_t error_size)
{
    if (!tile) return false;
    memset(tile, 0, sizeof(*tile));
    if (!map || !map->mask_query) return false;
    unsigned char *raw = NULL;
    size_t raw_size = 0U;
    if (!load_blob(map->db,
                   map->mask_query,
                   zoom,
                   x,
                   y,
                   &raw,
                   &raw_size,
                   error,
                   error_size)) {
        return false;
    }
    if (raw_size < 12U || memcmp(raw, "ORM1", 4U) != 0
        || read_u16_le(raw + 4U) != 1U) {
        free(raw);
        set_error(error, error_size, "invalid .ormap mask tile header");
        return false;
    }
    const uint8_t grid = raw[6];
    const uint8_t layers = raw[7];
    const uint32_t layer_bytes = read_u32_le(raw + 8U);
    if (grid == 0U || layers != 3U
        || layer_bytes != ((uint32_t)grid * (uint32_t)grid + 7U) / 8U
        || 12U + (size_t)layer_bytes * 3U != raw_size) {
        free(raw);
        set_error(error, error_size, "invalid .ormap mask tile length");
        return false;
    }
    unsigned char *copy = malloc((size_t)layer_bytes * 3U);
    if (!copy) {
        free(raw);
        set_error(error, error_size, "out of memory decoding mask tile");
        return false;
    }
    memcpy(copy, raw + 12U, (size_t)layer_bytes * 3U);
    tile->grid_size = grid;
    tile->layer_bytes = layer_bytes;
    tile->builtup = copy;
    tile->water = copy + layer_bytes;
    tile->forest = copy + layer_bytes * 2U;
    free(raw);
    return true;
}

void openride_ormap_mask_tile_destroy(OpenRideORMapMaskTile *tile)
{
    if (!tile) return;
    free(tile->builtup);
    memset(tile, 0, sizeof(*tile));
}

const OpenRideORMapLabel *openride_ormap_labels(const OpenRideORMap *map,
                                                 uint32_t *count)
{
    if (count) *count = map ? map->label_count : 0U;
    return map ? map->labels : NULL;
}
