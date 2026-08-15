#include "openride/ormap.h"
#include "openride/osm_import.h"
#include "openride/routing_graph.h"

#include <sqlite3.h>
#include <zlib.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static double mx(double lon) { return (lon + 180.0) / 360.0; }
static double my(double lat)
{
    const double pi = 3.14159265358979323846;
    const double rad = lat * pi / 180.0;
    return (1.0 - asinh(tan(rad)) / pi) * 0.5;
}

static void write_u16_le_test(unsigned char *p, uint16_t value)
{
    p[0] = (unsigned char)(value & 0xffU);
    p[1] = (unsigned char)((value >> 8U) & 0xffU);
}

static void write_u32_le_test(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)(value & 0xffU);
    p[1] = (unsigned char)((value >> 8U) & 0xffU);
    p[2] = (unsigned char)((value >> 16U) & 0xffU);
    p[3] = (unsigned char)((value >> 24U) & 0xffU);
}

static void create_area_fixture(const char *path)
{
    remove(path);
    sqlite3 *db = NULL;
    assert(sqlite3_open(path, &db) == SQLITE_OK);
    const char *schema =
        "CREATE TABLE metadata(name TEXT PRIMARY KEY,value TEXT NOT NULL);"
        "INSERT INTO metadata(name,value) VALUES('format_version','3');"
        "INSERT INTO metadata(name,value) VALUES('areacoarsezoom','11');"
        "INSERT INTO metadata(name,value) VALUES('areadetailzoom','14');"
        "CREATE TABLE road_tiles(zoom_level INTEGER,tile_column INTEGER,tile_row INTEGER,tile_data BLOB,PRIMARY KEY(zoom_level,tile_column,tile_row));"
        "CREATE TABLE water_tiles(zoom_level INTEGER,tile_column INTEGER,tile_row INTEGER,tile_data BLOB,PRIMARY KEY(zoom_level,tile_column,tile_row));"
        "CREATE TABLE area_tiles(zoom_level INTEGER,tile_column INTEGER,tile_row INTEGER,tile_data BLOB,PRIMARY KEY(zoom_level,tile_column,tile_row));"
        "CREATE TABLE mask_tiles(zoom_level INTEGER,tile_column INTEGER,tile_row INTEGER,tile_data BLOB,PRIMARY KEY(zoom_level,tile_column,tile_row));"
        "CREATE TABLE labels(lat_e7 INTEGER,lon_e7 INTEGER,kind INTEGER,rank INTEGER,name TEXT);";
    assert(sqlite3_exec(db, schema, NULL, NULL, NULL) == SQLITE_OK);

    unsigned char raw[12U + 14U];
    memcpy(raw, "ORA1", 4U);
    write_u16_le_test(raw + 4U, 1U);
    write_u16_le_test(raw + 6U, 14U);
    write_u32_le_test(raw + 8U, 1U);
    write_u16_le_test(raw + 12U, 1000U);
    write_u16_le_test(raw + 14U, 2000U);
    write_u16_le_test(raw + 16U, 3000U);
    write_u16_le_test(raw + 18U, 4000U);
    write_u16_le_test(raw + 20U, 5000U);
    write_u16_le_test(raw + 22U, 6000U);
    raw[24] = OPENRIDE_ORMAP_AREA_WATER;
    raw[25] = 0U;

    uLongf compressed_size = compressBound((uLong)sizeof(raw));
    unsigned char *blob = malloc((size_t)compressed_size + 4U);
    assert(blob != NULL);
    write_u32_le_test(blob, (uint32_t)sizeof(raw));
    assert(compress2(blob + 4U,
                     &compressed_size,
                     raw,
                     (uLong)sizeof(raw),
                     Z_BEST_SPEED) == Z_OK);

    sqlite3_stmt *insert = NULL;
    assert(sqlite3_prepare_v2(db,
                              "INSERT INTO area_tiles(zoom_level,tile_column,tile_row,tile_data) VALUES(?1,?2,?3,?4)",
                              -1,
                              &insert,
                              NULL) == SQLITE_OK);
    sqlite3_bind_int(insert, 1, OPENRIDE_ORMAP_AREA_DETAIL_ZOOM);
    sqlite3_bind_int(insert, 2, 123);
    sqlite3_bind_int(insert, 3, 456);
    sqlite3_bind_blob(insert,
                      4,
                      blob,
                      (int)compressed_size + 4,
                      SQLITE_TRANSIENT);
    assert(sqlite3_step(insert) == SQLITE_DONE);
    sqlite3_finalize(insert);
    free(blob);
    sqlite3_close(db);
}

static void test_area_tile_decode(long pid)
{
    char path[256];
    snprintf(path, sizeof(path), "/tmp/openride-ormap-area-%ld.ormap", pid);
    create_area_fixture(path);

    char error[512] = {0};
    OpenRideORMap *map = openride_ormap_open(path, error, sizeof(error));
    assert(map != NULL);
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(map);
    assert(metadata != NULL);
    assert(metadata->format_version == 3);
    assert(metadata->area_coarse_zoom == OPENRIDE_ORMAP_AREA_COARSE_ZOOM);    assert(metadata->area_detail_zoom == OPENRIDE_ORMAP_AREA_DETAIL_ZOOM);
    OpenRideORMapTileCoord *coords = NULL;
    uint32_t coord_count = 0U;
    assert(openride_ormap_list_tiles(map,
                                      OPENRIDE_ORMAP_TILE_LAYER_AREA,
                                      OPENRIDE_ORMAP_AREA_DETAIL_ZOOM,
                                      &coords,
                                      &coord_count,
                                      error,
                                      sizeof(error)));
    assert(coord_count == 1U);
    assert(coords != NULL);
    assert(coords[0].x == 123);
    assert(coords[0].y == 456);
    openride_ormap_tile_coords_destroy(coords);
    OpenRideORMapAreaTile tile = {0};
    assert(openride_ormap_load_area_tile(map,
                                         OPENRIDE_ORMAP_AREA_DETAIL_ZOOM,
                                         123,
                                         456,
                                         &tile,
                                         error,
                                         sizeof(error)));
    assert(tile.count == 1U);
    assert(tile.triangles != NULL);
    assert(tile.triangles[0].x1 == 1000U);
    assert(tile.triangles[0].y3 == 6000U);
    assert(tile.triangles[0].kind == OPENRIDE_ORMAP_AREA_WATER);
    openride_ormap_area_tile_destroy(&tile);
    openride_ormap_close(map);
    remove(path);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    assert(OPENRIDE_ORMAP_AREA_GREEN == 3);
    char graph_path[256], places_path[256], map_path[256];
    const long pid = (long)getpid();
    snprintf(graph_path, sizeof(graph_path), "/tmp/openride-ormap-%ld.orgraph", pid);
    snprintf(places_path, sizeof(places_path), "/tmp/openride-ormap-%ld.sqlite", pid);
    snprintf(map_path, sizeof(map_path), "/tmp/openride-ormap-%ld.ormap", pid);
    remove(graph_path); remove(places_path); remove(map_path);

    char error[512] = {0};
    OpenRideOSMImportStats routing_stats = {0};
    OpenRideOSMPlaceImportStats place_stats = {0};
    assert(openride_osm_pbf_import_file(argv[1],
                                        graph_path,
                                        &routing_stats,
                                        error,
                                        sizeof(error)));
    assert(openride_osm_pbf_import_places(argv[1],
                                          places_path,
                                          &place_stats,
                                          error,
                                          sizeof(error)));
    OpenRideORMapBuildStats map_stats = {0};
    assert(openride_ormap_build(argv[1],
                                graph_path,
                                places_path,
                                map_path,
                                "Tiny test",
                                &map_stats,
                                error,
                                sizeof(error)));
    assert(map_stats.routing_segments_seen > 0U);
    assert(map_stats.road_tiles_written > 0U);

    OpenRideORMap *map = openride_ormap_open(map_path, error, sizeof(error));
    assert(map != NULL);
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(map);
    assert(metadata != NULL);
    assert(metadata->format_version == (int)OPENRIDE_ORMAP_FORMAT_VERSION);
    assert(metadata->max_zoom == OPENRIDE_ORMAP_MAX_ZOOM);
    assert(metadata->road_max_zoom == OPENRIDE_ORMAP_ROAD_DATA_MAX_ZOOM);
    assert(metadata->mask_zoom == OPENRIDE_ORMAP_MASK_ZOOM);
    assert(metadata->water_zoom == OPENRIDE_ORMAP_WATER_ZOOM);
    assert(metadata->area_coarse_zoom == OPENRIDE_ORMAP_AREA_COARSE_ZOOM);
    assert(metadata->area_detail_zoom == OPENRIDE_ORMAP_AREA_DETAIL_ZOOM);

    OpenRideRoutingGraph graph = {0};
    assert(openride_routing_graph_load(&graph, graph_path, error, sizeof(error)));
    assert(graph.segment_index.segment_count > 0U);
    const OpenRideRoutingSegment *segment = &graph.segment_index.segments[0];
    double lat_a, lon_a, lat_b, lon_b;
    openride_routing_node_geo(&graph.nodes[segment->a], &lat_a, &lon_a);
    openride_routing_node_geo(&graph.nodes[segment->b], &lat_b, &lon_b);
    const int z = OPENRIDE_ORMAP_ROAD_DATA_MAX_ZOOM;
    const int n = 1 << z;
    const int tx = (int)floor(mx((lon_a + lon_b) * 0.5) * n);
    const int ty = (int)floor(my((lat_a + lat_b) * 0.5) * n);
    OpenRideORMapRoadTile tile = {0};
    assert(openride_ormap_load_road_tile(map,
                                         z,
                                         tx,
                                         ty,
                                         &tile,
                                         error,
                                         sizeof(error)));
    assert(tile.count > 0U);
    openride_ormap_road_tile_destroy(&tile);
    openride_routing_graph_destroy(&graph);
    openride_ormap_close(map);

    test_area_tile_decode(pid);

    remove(graph_path); remove(places_path); remove(map_path);
    puts("ORMap tests: OK");
    return 0;
}
