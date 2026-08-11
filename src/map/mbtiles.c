#include "openride/mbtiles.h"

#include <sqlite3.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct OpenRideMBTiles {
    sqlite3 *db;
    sqlite3_stmt *tile_query;
    OpenRideMBTilesMetadata metadata;
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0) return;
    snprintf(error, error_size, "%s", message ? message : "unknown error");
}

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static bool parse_int(const char *text, int *value)
{
    if (!text || !value) return false;

    char *end = NULL;
    errno = 0;
    const long parsed = strtol(text, &end, 10);

    if (errno != 0 || end == text || parsed < INT_MIN || parsed > INT_MAX) return false;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end != '\0') return false;

    *value = (int)parsed;
    return true;
}

static bool parse_csv_doubles(const char *text, double *values, size_t count)
{
    if (!text || !values || count == 0) return false;

    const char *cursor = text;

    for (size_t i = 0; i < count; ++i) {
        char *end = NULL;
        errno = 0;
        values[i] = strtod(cursor, &end);
        if (errno != 0 || end == cursor) return false;

        while (*end == ' ' || *end == '\t') ++end;

        if (i + 1 < count) {
            if (*end != ',') return false;
            cursor = end + 1;
            while (*cursor == ' ' || *cursor == '\t') ++cursor;
        } else {
            if (*end != '\0') return false;
        }
    }

    return true;
}

static bool load_metadata(OpenRideMBTiles *map, char *error, size_t error_size)
{
    static const char *sql = "SELECT name, value FROM metadata";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(map->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(map->db));
        return false;
    }

    map->metadata.min_zoom = -1;
    map->metadata.max_zoom = -1;
    copy_string(map->metadata.name, sizeof(map->metadata.name), "Offline map");

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *key = (const char *)sqlite3_column_text(stmt, 0);
        const char *value = (const char *)sqlite3_column_text(stmt, 1);

        if (!key || !value) continue;

        if (strcmp(key, "name") == 0) {
            copy_string(map->metadata.name, sizeof(map->metadata.name), value);
        } else if (strcmp(key, "format") == 0) {
            copy_string(map->metadata.format, sizeof(map->metadata.format), value);
        } else if (strcmp(key, "attribution") == 0) {
            copy_string(map->metadata.attribution, sizeof(map->metadata.attribution), value);
        } else if (strcmp(key, "minzoom") == 0) {
            (void)parse_int(value, &map->metadata.min_zoom);
        } else if (strcmp(key, "maxzoom") == 0) {
            (void)parse_int(value, &map->metadata.max_zoom);
        } else if (strcmp(key, "center") == 0) {
            double values[3];
            if (parse_csv_doubles(value, values, 3)) {
                map->metadata.center_lon = values[0];
                map->metadata.center_lat = values[1];
                map->metadata.center_zoom = values[2];
                map->metadata.has_center = true;
            }
        } else if (strcmp(key, "bounds") == 0) {
            double values[4];
            if (parse_csv_doubles(value, values, 4)) {
                map->metadata.west = values[0];
                map->metadata.south = values[1];
                map->metadata.east = values[2];
                map->metadata.north = values[3];
                map->metadata.has_bounds = true;
            }
        }
    }

    if (rc != SQLITE_DONE) {
        set_error(error, error_size, sqlite3_errmsg(map->db));
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);

    if (map->metadata.min_zoom < 0 || map->metadata.max_zoom < 0) {
        static const char *range_sql =
            "SELECT MIN(zoom_level), MAX(zoom_level) FROM tiles";

        rc = sqlite3_prepare_v2(map->db, range_sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            set_error(error, error_size, sqlite3_errmsg(map->db));
            return false;
        }

        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            if (map->metadata.min_zoom < 0) {
                map->metadata.min_zoom = sqlite3_column_int(stmt, 0);
            }
            if (map->metadata.max_zoom < 0) {
                map->metadata.max_zoom = sqlite3_column_int(stmt, 1);
            }
        }

        sqlite3_finalize(stmt);
    }

    if (map->metadata.min_zoom < 0 || map->metadata.max_zoom < map->metadata.min_zoom) {
        set_error(error, error_size, "MBTiles contains no valid zoom levels");
        return false;
    }

    if (!map->metadata.has_center && map->metadata.has_bounds) {
        map->metadata.center_lon = (map->metadata.west + map->metadata.east) * 0.5;
        map->metadata.center_lat = (map->metadata.south + map->metadata.north) * 0.5;
        map->metadata.center_zoom = (double)map->metadata.min_zoom;
        map->metadata.has_center = true;
    }

    return true;
}

OpenRideMBTiles *openride_mbtiles_open(const char *path,
                                       char *error,
                                       size_t error_size)
{
    if (!path || path[0] == '\0') {
        set_error(error, error_size, "MBTiles path is empty");
        return NULL;
    }

    OpenRideMBTiles *map = calloc(1, sizeof(*map));
    if (!map) {
        set_error(error, error_size, "Out of memory");
        return NULL;
    }

    const int flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX;
    int rc = sqlite3_open_v2(path, &map->db, flags, NULL);
    if (rc != SQLITE_OK) {
        set_error(error,
                  error_size,
                  map->db ? sqlite3_errmsg(map->db) : "Unable to open MBTiles database");
        openride_mbtiles_close(map);
        return NULL;
    }

    if (!load_metadata(map, error, error_size)) {
        openride_mbtiles_close(map);
        return NULL;
    }

    static const char *tile_sql =
        "SELECT tile_data FROM tiles "
        "WHERE zoom_level = ?1 AND tile_column = ?2 AND tile_row = ?3";

    rc = sqlite3_prepare_v2(map->db, tile_sql, -1, &map->tile_query, NULL);
    if (rc != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(map->db));
        openride_mbtiles_close(map);
        return NULL;
    }

    return map;
}

void openride_mbtiles_close(OpenRideMBTiles *map)
{
    if (!map) return;

    if (map->tile_query) {
        sqlite3_finalize(map->tile_query);
    }

    if (map->db) {
        sqlite3_close(map->db);
    }

    free(map);
}

const OpenRideMBTilesMetadata *openride_mbtiles_metadata(const OpenRideMBTiles *map)
{
    return map ? &map->metadata : NULL;
}

bool openride_mbtiles_load_tile(OpenRideMBTiles *map,
                                int zoom,
                                int x,
                                int y_xyz,
                                OpenRideTileData *out_tile,
                                char *error,
                                size_t error_size)
{
    if (!out_tile) {
        set_error(error, error_size, "out_tile is NULL");
        return false;
    }

    out_tile->bytes = NULL;
    out_tile->size = 0;

    if (!map || !map->tile_query) {
        set_error(error, error_size, "MBTiles map is not open");
        return false;
    }

    if (zoom < 0 || zoom > 30) {
        set_error(error, error_size, "Unsupported tile zoom");
        return false;
    }

    const int tile_count = 1 << zoom;
    if (x < 0 || x >= tile_count || y_xyz < 0 || y_xyz >= tile_count) {
        return false;
    }

    const int y_tms = tile_count - 1 - y_xyz;

    sqlite3_reset(map->tile_query);
    sqlite3_clear_bindings(map->tile_query);

    sqlite3_bind_int(map->tile_query, 1, zoom);
    sqlite3_bind_int(map->tile_query, 2, x);
    sqlite3_bind_int(map->tile_query, 3, y_tms);

    const int rc = sqlite3_step(map->tile_query);

    if (rc == SQLITE_DONE) {
        sqlite3_reset(map->tile_query);
        return false;
    }

    if (rc != SQLITE_ROW) {
        set_error(error, error_size, sqlite3_errmsg(map->db));
        sqlite3_reset(map->tile_query);
        return false;
    }

    const void *blob = sqlite3_column_blob(map->tile_query, 0);
    const int bytes = sqlite3_column_bytes(map->tile_query, 0);

    if (!blob || bytes <= 0) {
        sqlite3_reset(map->tile_query);
        return false;
    }

    out_tile->bytes = malloc((size_t)bytes);
    if (!out_tile->bytes) {
        set_error(error, error_size, "Out of memory while copying tile");
        sqlite3_reset(map->tile_query);
        return false;
    }

    memcpy(out_tile->bytes, blob, (size_t)bytes);
    out_tile->size = (size_t)bytes;

    sqlite3_reset(map->tile_query);
    return true;
}

void openride_tile_data_free(OpenRideTileData *tile)
{
    if (!tile) return;
    free(tile->bytes);
    tile->bytes = NULL;
    tile->size = 0;
}
