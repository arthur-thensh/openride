from pathlib import Path

cmake = Path("CMakeLists.txt")
text = cmake.read_text()
old = "    src/map/ormap_builder.c\n    src/map/ormap_builder_v4.c\n    src/map/mvt.c\n"
new = "    src/map/ormap_builder.c\n    src/map/ormap_builder_v4.c\n    src/map/ormap_landcover_mesh.c\n    src/map/mvt.c\n"
if old not in text:
    raise SystemExit("CMake core source marker not found")
text = text.replace(old, new, 1)
old = """    add_executable(test_ormap
        tests/test_ormap.c
    )
    target_link_libraries(test_ormap PRIVATE openride_core)
    add_test(NAME ormap COMMAND test_ormap "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/tiny.osm.pbf")

"""
new = old + """    add_executable(test_ormap_landcover_mesh
        tests/test_ormap_landcover_mesh.c
    )
    target_link_libraries(test_ormap_landcover_mesh PRIVATE openride_core)
    add_test(NAME ormap_landcover_mesh COMMAND test_ormap_landcover_mesh)

"""
if old not in text:
    raise SystemExit("CMake test marker not found")
cmake.write_text(text.replace(old, new, 1))

header = Path("include/openride/ormap.h")
text = header.read_text()
old = " * In v4 this representation is used for water and the coarse urban overview;\n"
new = " * In v4 this representation is used for water and generalized overview landcover;\n"
if old not in text:
    raise SystemExit("ormap area comment marker not found")
text = text.replace(old, new, 1)
old = " * a coarse rectilinear urban layer for regional views and the merged semantic\n"
new = " * a generalized contour mesh for overview views and the merged semantic\n"
if old not in text:
    raise SystemExit("ormap build comment marker not found")
header.write_text(text.replace(old, new, 1))

builder = Path("src/map/ormap_builder_v4.c")
text = builder.read_text()
old = '#include "openride/ormap.h"\n#include "openride/osm_import.h"\n'
new = '#include "openride/ormap.h"\n#include "openride/ormap_landcover_mesh.h"\n#include "openride/osm_import.h"\n'
if old not in text:
    raise SystemExit("builder include marker not found")
text = text.replace(old, new, 1)

start_marker = "static bool v4_append_coarse_layer("
end_marker = "static bool v4_write_urban_masks("
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("builder coarse function markers not found")

replacement = r'''static bool v4_append_coarse_layer(sqlite3 *db,
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

'''
builder.write_text(text[:start] + replacement + text[end:])
