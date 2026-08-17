#include "openride/ormap_pyramid_overlay.h"
#include "openride/place_search.h"

#include <assert.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void put_u16(unsigned char *p, uint16_t value)
{
    p[0] = (unsigned char)(value & 0xffU);
    p[1] = (unsigned char)((value >> 8U) & 0xffU);
}

static void put_u32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)(value & 0xffU);
    p[1] = (unsigned char)((value >> 8U) & 0xffU);
    p[2] = (unsigned char)((value >> 16U) & 0xffU);
    p[3] = (unsigned char)((value >> 24U) & 0xffU);
}

static unsigned char *compress_blob(const unsigned char *raw,
                                    size_t raw_size,
                                    int *blob_size)
{
    assert(raw);
    assert(blob_size);

    uLongf compressed_size = compressBound((uLong)raw_size);
    unsigned char *blob = malloc((size_t)compressed_size + 4U);
    assert(blob);
    put_u32(blob, (uint32_t)raw_size);
    assert(compress2(blob + 4U,
                     &compressed_size,
                     raw,
                     (uLong)raw_size,
                     Z_BEST_SPEED) == Z_OK);
    *blob_size = (int)compressed_size + 4;
    return blob;
}

static void insert_tile(sqlite3 *db,
                        const char *sql,
                        int zoom,
                        const unsigned char *blob,
                        int blob_size)
{
    sqlite3_stmt *insert = NULL;
    assert(sqlite3_prepare_v2(db, sql, -1, &insert, NULL) == SQLITE_OK);
    assert(sqlite3_bind_int(insert, 1, zoom) == SQLITE_OK);
    assert(sqlite3_bind_blob(insert,
                             2,
                             blob,
                             blob_size,
                             SQLITE_TRANSIENT) == SQLITE_OK);
    assert(sqlite3_step(insert) == SQLITE_DONE);
    sqlite3_finalize(insert);
}

static sqlite3_int64 query_int64(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *stmt = NULL;
    assert(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    const sqlite3_int64 value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

static void test_decode_fixture(void)
{
    const char *path = "/tmp/openride-ormap-pyramid-overlay-decode.ormap11";
    remove(path);

    sqlite3 *db = NULL;
    assert(sqlite3_open(path, &db) == SQLITE_OK);
    assert(sqlite3_exec(
        db,
        "CREATE TABLE metadata(name TEXT PRIMARY KEY,value TEXT NOT NULL);"
        "INSERT INTO metadata VALUES('format_version','11');"
        "INSERT INTO metadata VALUES('overlay_format_version','1');"
        "CREATE TABLE overlay_line_tiles("
        "layer INTEGER,zoom INTEGER,tile_column INTEGER,tile_row INTEGER,"
        "tile_data BLOB,PRIMARY KEY(layer,zoom,tile_column,tile_row));"
        "CREATE TABLE overlay_labels("
        "ordinal INTEGER,lat_e7 INTEGER,lon_e7 INTEGER,kind INTEGER,rank INTEGER,lod INTEGER,"
        "name TEXT);"
        "INSERT INTO overlay_labels VALUES(0,507000000,31000000,1,100,0,'Test');",
        NULL, NULL, NULL) == SQLITE_OK);

    unsigned char raw[24] = {0};
    memcpy(raw, "ORL1", 4U);
    put_u16(raw + 4U, 1U);
    put_u16(raw + 6U, 12U);
    put_u32(raw + 8U, 1U);
    put_u16(raw + 12U, 100U);
    put_u16(raw + 14U, 200U);
    put_u16(raw + 16U, 300U);
    put_u16(raw + 18U, 400U);
    raw[20] = OPENRIDE_ROAD_PRIMARY;
    raw[21] = OPENRIDE_SURFACE_ASPHALT;
    put_u16(raw + 22U, OPENRIDE_EDGE_FLAG_NONE);

    int blob_size = 0;
    unsigned char *blob = compress_blob(raw, sizeof(raw), &blob_size);

    sqlite3_stmt *insert = NULL;
    assert(sqlite3_prepare_v2(
        db,
        "INSERT INTO overlay_line_tiles VALUES(1,9,1,2,?)",
        -1, &insert, NULL) == SQLITE_OK);
    assert(sqlite3_bind_blob(insert, 1, blob,
                             blob_size,
                             SQLITE_STATIC) == SQLITE_OK);
    assert(sqlite3_step(insert) == SQLITE_DONE);
    sqlite3_finalize(insert);
    free(blob);
    sqlite3_close(db);

    char error[256] = {0};
    OpenRideORMapPyramidOverlayMap *map =
        openride_ormap_pyramid_overlay_open(path, error, sizeof(error));
    assert(map);
    assert(openride_ormap_pyramid_overlay_layer_available(
        map, OPENRIDE_ORMAP_PYRAMID_OVERLAY_ROAD));

    OpenRideORMapPyramidOverlayLineTile tile = {0};
    assert(openride_ormap_pyramid_overlay_load_tile(
        map,
        OPENRIDE_ORMAP_PYRAMID_OVERLAY_ROAD,
        9, 1, 2,
        &tile,
        error,
        sizeof(error)));
    assert(tile.count == 1U);
    assert(tile.records[0].x1 == 100U);
    assert(tile.records[0].y1 == 200U);
    assert(tile.records[0].x2 == 300U);
    assert(tile.records[0].y2 == 400U);
    assert(tile.records[0].kind == OPENRIDE_ROAD_PRIMARY);
    assert(tile.records[0].aux == OPENRIDE_SURFACE_ASPHALT);
    openride_ormap_pyramid_overlay_tile_destroy(&tile);

    uint32_t label_count = 0U;
    const OpenRideORMapLabel *labels =
        openride_ormap_pyramid_overlay_labels(map, &label_count);
    assert(labels && label_count == 1U);
    assert(strcmp(labels[0].name, "Test") == 0);

    OpenRideORMapPyramidOverlayInspectStats stats = {0};
    assert(openride_ormap_pyramid_overlay_inspect(
        path, &stats, error, sizeof(error)));
    assert(stats.road_tiles_by_zoom[0] == 1U);
    assert(stats.road_records_by_zoom[0] == 1U);
    assert(stats.labels == 1U);
    assert(stats.malformed_tiles == 0U);
    assert(stats.invalid_records == 0U);

    openride_ormap_pyramid_overlay_close(map);
    remove(path);
}

static void create_v8_fixture(const char *path)
{
    remove(path);

    sqlite3 *db = NULL;
    assert(sqlite3_open(path, &db) == SQLITE_OK);
    assert(sqlite3_exec(
        db,
        "CREATE TABLE metadata(name TEXT PRIMARY KEY,value TEXT NOT NULL);"
        "INSERT INTO metadata VALUES('format_version','8');"
        "INSERT INTO metadata VALUES('minzoom','8');"
        "INSERT INTO metadata VALUES('maxzoom','16');"
        "INSERT INTO metadata VALUES('roadmaxzoom','14');"
        "INSERT INTO metadata VALUES('waterzoom','13');"
        "CREATE TABLE road_tiles("
        "zoom_level INTEGER,tile_column INTEGER,tile_row INTEGER,tile_data BLOB,"
        "PRIMARY KEY(zoom_level,tile_column,tile_row));"
        "CREATE TABLE water_tiles("
        "zoom_level INTEGER,tile_column INTEGER,tile_row INTEGER,tile_data BLOB,"
        "PRIMARY KEY(zoom_level,tile_column,tile_row));"
        "CREATE TABLE area_tiles("
        "zoom_level INTEGER,tile_column INTEGER,tile_row INTEGER,tile_data BLOB,"
        "PRIMARY KEY(zoom_level,tile_column,tile_row));"
        "CREATE TABLE mask_tiles("
        "zoom_level INTEGER,tile_column INTEGER,tile_row INTEGER,tile_data BLOB,"
        "PRIMARY KEY(zoom_level,tile_column,tile_row));"
        "CREATE TABLE labels("
        "lat_e7 INTEGER,lon_e7 INTEGER,kind INTEGER,rank INTEGER,lod INTEGER,name TEXT);"
        "INSERT INTO labels VALUES(507000000,31000000,1,100,0,'Test City');"
        "INSERT INTO labels VALUES(506000000,30000000,3,50,2,'Test Village');",
        NULL, NULL, NULL) == SQLITE_OK);

    unsigned char road_raw[24] = {0};
    memcpy(road_raw, "ORR1", 4U);
    put_u16(road_raw + 4U, 1U);
    put_u16(road_raw + 6U, 12U);
    put_u32(road_raw + 8U, 1U);
    put_u16(road_raw + 12U, 1000U);
    put_u16(road_raw + 14U, 1200U);
    put_u16(road_raw + 16U, 2000U);
    put_u16(road_raw + 18U, 2200U);
    road_raw[20] = OPENRIDE_ROAD_PRIMARY;
    road_raw[21] = OPENRIDE_SURFACE_GRAVEL;
    put_u16(road_raw + 22U, OPENRIDE_EDGE_FLAG_TOLL);

    int road_blob_size = 0;
    unsigned char *road_blob = compress_blob(
        road_raw, sizeof(road_raw), &road_blob_size);
    static const int road_zooms[] = {
        OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM,
        OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM,
        OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM,
        OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM
    };
    for (size_t i = 0U; i < sizeof(road_zooms) / sizeof(road_zooms[0]); ++i) {
        insert_tile(
            db,
            "INSERT INTO road_tiles VALUES(?1,0,0,?2)",
            road_zooms[i],
            road_blob,
            road_blob_size);
    }
    free(road_blob);

    unsigned char water_raw[22] = {0};
    memcpy(water_raw, "ORW1", 4U);
    put_u16(water_raw + 4U, 1U);
    put_u16(water_raw + 6U, 10U);
    put_u32(water_raw + 8U, 1U);
    put_u16(water_raw + 12U, 1000U);
    put_u16(water_raw + 14U, 1200U);
    put_u16(water_raw + 16U, 2000U);
    put_u16(water_raw + 18U, 2200U);
    water_raw[20] = OPENRIDE_ORMAP_WATERWAY_CANAL;
    water_raw[21] = 0U;

    int water_blob_size = 0;
    unsigned char *water_blob = compress_blob(
        water_raw, sizeof(water_raw), &water_blob_size);
    insert_tile(
        db,
        "INSERT INTO water_tiles VALUES(?1,0,0,?2)",
        OPENRIDE_ORMAP_WATER_ZOOM,
        water_blob,
        water_blob_size);
    free(water_blob);
    sqlite3_close(db);
}

static void create_v11_fixture(const char *path)
{
    remove(path);

    sqlite3 *db = NULL;
    assert(sqlite3_open(path, &db) == SQLITE_OK);
    assert(sqlite3_exec(
        db,
        "CREATE TABLE metadata(name TEXT PRIMARY KEY,value TEXT NOT NULL);"
        "INSERT INTO metadata VALUES('format_version','11');"
        "CREATE TABLE surface_tiles("
        "zoom INTEGER,tile_column INTEGER,tile_row INTEGER,tile_data BLOB,"
        "PRIMARY KEY(zoom,tile_column,tile_row));"
        "INSERT INTO surface_tiles VALUES(9,0,0,X'A1');"
        "CREATE TABLE building_tiles("
        "zoom INTEGER,tile_column INTEGER,tile_row INTEGER,tile_data BLOB,"
        "PRIMARY KEY(zoom,tile_column,tile_row));"
        "INSERT INTO building_tiles VALUES(16,0,0,X'B2');",
        NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);
}

static void test_append_round_trip(void)
{
    const char *source_path =
        "/tmp/openride-ormap-pyramid-overlay-source.ormap";
    const char *target_path =
        "/tmp/openride-ormap-pyramid-overlay-target.ormap11";
    create_v8_fixture(source_path);
    create_v11_fixture(target_path);

    OpenRideORMapPyramidOverlayBuildStats build = {0};
    char error[256] = {0};
    assert(openride_ormap_pyramid_overlay_append(
        source_path, target_path, &build, error, sizeof(error)));

    for (int zoom = OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM;
         zoom <= OPENRIDE_ORMAP_PYRAMID_ROAD_MAX_ZOOM;
         ++zoom) {
        const int index = zoom - OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM;
        assert(build.road_tiles_by_zoom[index] == 1U);
        assert(build.road_records_by_zoom[index] == 1U);
    }
    for (int zoom = OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM;
         zoom <= OPENRIDE_ORMAP_PYRAMID_WATER_MAX_ZOOM;
         ++zoom) {
        const int index = zoom - OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM;
        assert(build.water_tiles_by_zoom[index] == 1U);
        assert(build.water_records_by_zoom[index] == 1U);
    }
    assert(build.labels == 2U);
    assert(build.raw_bytes > 0U);
    assert(build.compressed_bytes > 0U);

    sqlite3 *db = NULL;
    assert(sqlite3_open_v2(
        target_path, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK);
    assert(query_int64(
        db,
        "SELECT COUNT(*) FROM surface_tiles WHERE hex(tile_data)='A1'") == 1);
    assert(query_int64(
        db,
        "SELECT COUNT(*) FROM building_tiles WHERE hex(tile_data)='B2'") == 1);
    sqlite3_close(db);

    OpenRideORMapPyramidOverlayMap *map =
        openride_ormap_pyramid_overlay_open(
            target_path, error, sizeof(error));
    assert(map);
    assert(openride_ormap_pyramid_overlay_layer_available(
        map, OPENRIDE_ORMAP_PYRAMID_OVERLAY_ROAD));
    assert(openride_ormap_pyramid_overlay_layer_available(
        map, OPENRIDE_ORMAP_PYRAMID_OVERLAY_WATERWAY));

    for (int zoom = OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM;
         zoom <= OPENRIDE_ORMAP_PYRAMID_ROAD_MAX_ZOOM;
         ++zoom) {
        OpenRideORMapPyramidOverlayLineTile tile = {0};
        assert(openride_ormap_pyramid_overlay_load_tile(
            map,
            OPENRIDE_ORMAP_PYRAMID_OVERLAY_ROAD,
            zoom, 0, 0,
            &tile,
            error,
            sizeof(error)));
        assert(tile.count == 1U);
        assert(tile.records[0].kind == OPENRIDE_ROAD_PRIMARY);
        assert(tile.records[0].aux == OPENRIDE_SURFACE_GRAVEL);
        assert(tile.records[0].flags == OPENRIDE_EDGE_FLAG_TOLL);
        openride_ormap_pyramid_overlay_tile_destroy(&tile);
    }

    for (int zoom = OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM;
         zoom <= OPENRIDE_ORMAP_PYRAMID_WATER_MAX_ZOOM;
         ++zoom) {
        OpenRideORMapPyramidOverlayLineTile tile = {0};
        assert(openride_ormap_pyramid_overlay_load_tile(
            map,
            OPENRIDE_ORMAP_PYRAMID_OVERLAY_WATERWAY,
            zoom, 0, 0,
            &tile,
            error,
            sizeof(error)));
        assert(tile.count == 1U);
        assert(tile.records[0].kind == OPENRIDE_ORMAP_WATERWAY_CANAL);
        assert(tile.records[0].aux == 0U);
        assert(tile.records[0].flags == 0U);
        openride_ormap_pyramid_overlay_tile_destroy(&tile);
    }

    uint32_t label_count = 0U;
    const OpenRideORMapLabel *labels =
        openride_ormap_pyramid_overlay_labels(map, &label_count);
    assert(labels && label_count == 2U);
    assert(strcmp(labels[0].name, "Test City") == 0);
    assert(labels[0].kind == OPENRIDE_PLACE_CITY);
    assert(labels[0].rank == 100);
    assert(labels[0].lod == OPENRIDE_ORMAP_LABEL_LOD_REGIONAL);
    assert(strcmp(labels[1].name, "Test Village") == 0);
    assert(labels[1].kind == OPENRIDE_PLACE_VILLAGE);
    assert(labels[1].rank == 50);
    assert(labels[1].lod == OPENRIDE_ORMAP_LABEL_LOD_LOCAL);
    openride_ormap_pyramid_overlay_close(map);

    OpenRideORMapPyramidOverlayInspectStats inspected = {0};
    assert(openride_ormap_pyramid_overlay_inspect(
        target_path, &inspected, error, sizeof(error)));
    for (int zoom = OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM;
         zoom <= OPENRIDE_ORMAP_PYRAMID_ROAD_MAX_ZOOM;
         ++zoom) {
        const int index = zoom - OPENRIDE_ORMAP_PYRAMID_ROAD_MIN_ZOOM;
        assert(inspected.road_tiles_by_zoom[index] == 1U);
        assert(inspected.road_records_by_zoom[index] == 1U);
    }
    for (int zoom = OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM;
         zoom <= OPENRIDE_ORMAP_PYRAMID_WATER_MAX_ZOOM;
         ++zoom) {
        const int index = zoom - OPENRIDE_ORMAP_PYRAMID_WATER_MIN_ZOOM;
        assert(inspected.water_tiles_by_zoom[index] == 1U);
        assert(inspected.water_records_by_zoom[index] == 1U);
    }
    assert(inspected.labels == 2U);
    assert(inspected.malformed_tiles == 0U);
    assert(inspected.invalid_records == 0U);

    remove(source_path);
    remove(target_path);
}

int main(void)
{
    test_decode_fixture();
    test_append_round_trip();
    printf("ormap_pyramid_overlay: OK\n");
    return 0;
}
