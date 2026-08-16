#include "openride/ormap_pyramid_surface.h"
#include "map/ormap_pyramid_surface_internal.h"

#include <assert.h>
#include <math.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool point_equal(
    OpenRideORMapPyramidPoint a,
    OpenRideORMapPyramidPoint b)
{
    return fabs(a.x - b.x) < 1e-12
        && fabs(a.y - b.y) < 1e-12;
}

static bool point_in_ring(
    OpenRideORMapPyramidPoint point,
    const OpenRideORMapPyramidPoint *ring,
    uint32_t count)
{
    for (uint32_t i = 0U; i < count; ++i) {
        if (point_equal(point, ring[i])) return true;
    }
    return false;
}

static void test_nested_simplification(void)
{
    const OpenRideORMapPyramidPoint source[] = {
        {0.00, 0.00},
        {0.10, 0.01},
        {0.20, 0.00},
        {0.35, 0.04},
        {0.50, 0.00},
        {0.60, 0.25},
        {0.55, 0.50},
        {0.30, 0.58},
        {0.08, 0.45},
        {0.00, 0.20},
        {0.00, 0.00}
    };

    OpenRideORMapPyramidPoint *fine = NULL;
    uint32_t fine_count = 0U;

    assert(openride_ormap_pyramid_simplify_closed_ring(
        source,
        (uint32_t)(sizeof(source) / sizeof(source[0])),
        0.005,
        &fine,
        &fine_count));

    assert(fine_count >= 3U);

    OpenRideORMapPyramidPoint *coarse = NULL;
    uint32_t coarse_count = 0U;

    assert(openride_ormap_pyramid_simplify_closed_ring(
        fine,
        fine_count,
        0.05,
        &coarse,
        &coarse_count));

    assert(coarse_count >= 3U);
    assert(coarse_count <= fine_count);

    for (uint32_t i = 0U; i < coarse_count; ++i) {
        assert(point_in_ring(coarse[i], fine, fine_count));
    }

    free(coarse);
    free(fine);
}

static void test_schema_build(const char *pbf_path)
{
    char output[256];

    snprintf(
        output,
        sizeof(output),
        "/tmp/openride-pyramid-%ld.ormap11",
        (long)getpid());

    remove(output);

    OpenRideORMapPyramidSurfaceBuildStats stats = {0};
    char error[512] = {0};

    assert(openride_ormap_pyramid_surface_build(
        pbf_path,
        output,
        "Test Pyramid",
        &stats,
        error,
        sizeof(error)));

    OpenRideORMapPyramidBuildingBuildStats building = {0};

    assert(openride_ormap_pyramid_buildings_append(
        pbf_path,
        output,
        &building,
        error,
        sizeof(error)));

    sqlite3 *db = NULL;

    assert(sqlite3_open_v2(
        output,
        &db,
        SQLITE_OPEN_READONLY,
        NULL) == SQLITE_OK);

    sqlite3_stmt *stmt = NULL;

    assert(sqlite3_prepare_v2(
        db,
        "SELECT value FROM metadata WHERE name='format_version'",
        -1,
        &stmt,
        NULL) == SQLITE_OK);

    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int(stmt, 0)
           == (int)OPENRIDE_ORMAP_PYRAMID_FORMAT_VERSION);

    sqlite3_finalize(stmt);

    assert(sqlite3_prepare_v2(
        db,
        "SELECT value FROM metadata WHERE name='surface_policy'",
        -1,
        &stmt,
        NULL) == SQLITE_OK);

    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(strcmp(
        (const char *)sqlite3_column_text(stmt, 0),
        "hierarchical-z14-to-z9") == 0);

    sqlite3_finalize(stmt);

    assert(sqlite3_prepare_v2(
        db,
        "SELECT COUNT(*) FROM surface_tiles "
        "WHERE zoom < 9 OR zoom > 14",
        -1,
        &stmt,
        NULL) == SQLITE_OK);

    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int64(stmt, 0) == 0);

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    assert(stats.osm_features_seen >= stats.surface_polygons_seen);

    OpenRideORMapPyramidSurfaceMap *map =
        openride_ormap_pyramid_surface_open(
            output,
            error,
            sizeof(error));
    assert(map);

    const OpenRideORMapPyramidSurfaceMetadata *metadata =
        openride_ormap_pyramid_surface_metadata(map);
    assert(metadata);
    assert(
        metadata->format_version
        == (int)OPENRIDE_ORMAP_PYRAMID_FORMAT_VERSION);
    assert(metadata->min_zoom == 9);
    assert(metadata->max_zoom == 14);

    openride_ormap_pyramid_surface_close(map);

    OpenRideORMapPyramidSurfaceInspectStats inspect = {0};

    assert(openride_ormap_pyramid_surface_inspect(
        output,
        &inspect,
        error,
        sizeof(error)));

    assert(inspect.malformed_tiles == 0U);
    assert(inspect.invalid_kinds == 0U);
    assert(inspect.invalid_payloads == 0U);
    assert(inspect.malformed_building_tiles == 0U);

    remove(output);
}

int main(int argc, char **argv)
{
    assert(argc == 2);

    test_nested_simplification();
    test_schema_build(argv[1]);

    puts("ormap_pyramid_surface: ok");
    return 0;
}
