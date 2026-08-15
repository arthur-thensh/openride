from pathlib import Path
import re


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one exact match, found {count}")
    p.write_text(text.replace(old, new, 1))


def sub_once(path, pattern, repl):
    p = Path(path)
    text = p.read_text()
    new, count = re.subn(pattern, repl, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError(f"{path}: expected one regex match, found {count}")
    p.write_text(new)


# Public ORMap format/API -----------------------------------------------------
replace_once(
    "include/openride/ormap.h",
    "#define OPENRIDE_ORMAP_FORMAT_VERSION 5U\n#define OPENRIDE_ORMAP_MIN_ROAD_ZOOM 10\n/* Road geometry is detailed enough at z14 and is scaled above that zoom. */\n#define OPENRIDE_ORMAP_ROAD_DATA_MAX_ZOOM 14\n",
    "#define OPENRIDE_ORMAP_FORMAT_VERSION 6U\n/* v6 stores four semantic road LODs instead of regenerating every zoom. */\n#define OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM 8\n#define OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM 10\n#define OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM 12\n#define OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM 14\n#define OPENRIDE_ORMAP_MIN_ROAD_ZOOM OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM\n/* Road geometry is detailed enough at z14 and is scaled above that zoom. */\n#define OPENRIDE_ORMAP_ROAD_DATA_MAX_ZOOM OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM\n"
)
replace_once(
    "include/openride/ormap.h",
    "typedef struct OpenRideORMapLabel {\n    int32_t lat_e7;\n    int32_t lon_e7;\n    int kind;\n    int rank;\n    char name[96];\n} OpenRideORMapLabel;\n",
    "typedef enum OpenRideORMapLabelLOD {\n    OPENRIDE_ORMAP_LABEL_LOD_REGIONAL = 0,\n    OPENRIDE_ORMAP_LABEL_LOD_OVERVIEW = 1,\n    OPENRIDE_ORMAP_LABEL_LOD_LOCAL = 2,\n    OPENRIDE_ORMAP_LABEL_LOD_DETAIL = 3\n} OpenRideORMapLabelLOD;\n\ntypedef struct OpenRideORMapLabel {\n    int32_t lat_e7;\n    int32_t lon_e7;\n    int kind;\n    int rank;\n    uint8_t lod;\n    char name[96];\n} OpenRideORMapLabel;\n"
)
replace_once(
    "include/openride/ormap.h",
    "    uint64_t road_records_written;\n    uint64_t road_tiles_written;\n",
    "    uint64_t road_records_written;\n    uint64_t road_tiles_written;\n    uint64_t road_regional_records;\n    uint64_t road_overview_records;\n    uint64_t road_local_records;\n    uint64_t road_detail_records;\n"
)
replace_once(
    "include/openride/ormap.h",
    "    uint64_t mask_tiles_written;\n    uint64_t labels_written;\n",
    "    uint64_t mask_tiles_written;\n    uint64_t labels_written;\n    uint64_t label_regional_count;\n    uint64_t label_overview_count;\n    uint64_t label_local_count;\n    uint64_t label_detail_count;\n"
)

# Builder: semantic road LODs + explicit label LOD ---------------------------
sub_once(
    "src/map/ormap_builder.c",
    r"static bool road_visible_at_zoom\(OpenRideRoadClass road_class, int zoom\)\n\{.*?\n\}\n\nstatic const OpenRideRoutingEdge \*segment_edge",
    '''static bool road_visible_at_zoom(OpenRideRoadClass road_class, int zoom)\n{\n    if (zoom <= OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM) {\n        return road_class == OPENRIDE_ROAD_MOTORWAY\n            || road_class == OPENRIDE_ROAD_TRUNK;\n    }\n    if (zoom <= OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM) {\n        return road_class >= OPENRIDE_ROAD_MOTORWAY\n            && road_class <= OPENRIDE_ROAD_PRIMARY;\n    }\n    if (zoom <= OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM) {\n        return road_class >= OPENRIDE_ROAD_MOTORWAY\n            && road_class <= OPENRIDE_ROAD_TERTIARY;\n    }\n    return road_class != OPENRIDE_ROAD_UNKNOWN;\n}\n\nstatic const OpenRideRoutingEdge *segment_edge'''
)
replace_once(
    "src/map/ormap_builder.c",
    "static bool collect_roads_at_zoom(const OpenRideRoutingGraph *graph,\n",
    '''static void count_road_record_for_lod(OpenRideORMapBuildStats *stats, int zoom)\n{\n    if (!stats) return;\n    if (zoom == OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM) {\n        ++stats->road_regional_records;\n    } else if (zoom == OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM) {\n        ++stats->road_overview_records;\n    } else if (zoom == OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM) {\n        ++stats->road_local_records;\n    } else if (zoom == OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM) {\n        ++stats->road_detail_records;\n    }\n}\n\nstatic bool collect_roads_at_zoom(const OpenRideRoutingGraph *graph,\n'''
)
replace_once(
    "src/map/ormap_builder.c",
    "                    ++stats->road_records_written;\n",
    "                    ++stats->road_records_written;\n                    count_road_record_for_lod(stats, zoom);\n"
)
sub_once(
    "src/map/ormap_builder.c",
    r"        for \(int zoom = OPENRIDE_ORMAP_MIN_ROAD_ZOOM;\n             ok && zoom <= OPENRIDE_ORMAP_MAX_ROAD_ZOOM;\n             \+\+zoom\) \{\n            RoadTileMap road_tiles = \{0\};\n            ok = collect_roads_at_zoom\(&graph, zoom, &road_tiles, &stats, error, error_size\)\n                && write_road_tiles\(db, &road_tiles, &stats, error, error_size\);\n            road_map_destroy\(&road_tiles\);\n        \}",
    '''        const int road_lods[] = {\n            OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM,\n            OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM,\n            OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM,\n            OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM\n        };\n        for (size_t road_lod = 0U;\n             ok && road_lod < sizeof(road_lods) / sizeof(road_lods[0]);\n             ++road_lod) {\n            const int zoom = road_lods[road_lod];\n            RoadTileMap road_tiles = {0};\n            ok = collect_roads_at_zoom(&graph, zoom, &road_tiles, &stats, error, error_size)\n                && write_road_tiles(db, &road_tiles, &stats, error, error_size);\n            road_map_destroy(&road_tiles);\n        }'''
)
sub_once(
    "src/map/ormap_builder.c",
    r"static bool copy_labels\(sqlite3 \*db,.*?\n\}\n\n#define ORMAP_METADATA_BIN_DEG",
    '''static uint8_t label_lod_from_kind(int kind)\n{\n    switch (kind) {\n        case 1: /* city */\n            return OPENRIDE_ORMAP_LABEL_LOD_REGIONAL;\n        case 2: /* town */\n            return OPENRIDE_ORMAP_LABEL_LOD_OVERVIEW;\n        case 3: /* village */\n        case 5: /* suburb */\n            return OPENRIDE_ORMAP_LABEL_LOD_LOCAL;\n        case 4: /* hamlet */\n        case 6: /* quarter */\n        default:\n            return OPENRIDE_ORMAP_LABEL_LOD_DETAIL;\n    }\n}\n\nstatic void count_label_lod(OpenRideORMapBuildStats *stats, uint8_t lod)\n{\n    if (!stats) return;\n    if (lod == OPENRIDE_ORMAP_LABEL_LOD_REGIONAL) ++stats->label_regional_count;\n    else if (lod == OPENRIDE_ORMAP_LABEL_LOD_OVERVIEW) ++stats->label_overview_count;\n    else if (lod == OPENRIDE_ORMAP_LABEL_LOD_LOCAL) ++stats->label_local_count;\n    else ++stats->label_detail_count;\n}\n\nstatic bool copy_labels(sqlite3 *db,\n                        const char *places_path,\n                        OpenRideORMapBuildStats *stats,\n                        char *error,\n                        size_t error_size)\n{\n    sqlite3 *places = NULL;\n    if (sqlite3_open_v2(places_path,\n                        &places,\n                        SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX,\n                        NULL) != SQLITE_OK) {\n        set_error(error, error_size, places ? sqlite3_errmsg(places) : "unable to open places DB");\n        if (places) sqlite3_close(places);\n        return false;\n    }\n    sqlite3_stmt *select = NULL;\n    sqlite3_stmt *insert = NULL;\n    bool ok = sqlite3_prepare_v2(places,\n                                 "SELECT lat_e7,lon_e7,kind,rank,name FROM places "\n                                 "WHERE kind BETWEEN 1 AND 6 ORDER BY rank DESC",\n                                 -1,\n                                 &select,\n                                 NULL) == SQLITE_OK\n        && sqlite3_prepare_v2(db,\n                             "INSERT INTO labels(lat_e7,lon_e7,kind,rank,lod,name) "\n                             "VALUES(?1,?2,?3,?4,?5,?6)",\n                             -1,\n                             &insert,\n                             NULL) == SQLITE_OK;\n    if (!ok) set_error(error, error_size, "unable to prepare label copy");\n    while (ok && sqlite3_step(select) == SQLITE_ROW) {\n        const int kind = sqlite3_column_int(select, 2);\n        const uint8_t lod = label_lod_from_kind(kind);\n        sqlite3_reset(insert);\n        sqlite3_bind_int(insert, 1, sqlite3_column_int(select, 0));\n        sqlite3_bind_int(insert, 2, sqlite3_column_int(select, 1));\n        sqlite3_bind_int(insert, 3, kind);\n        sqlite3_bind_int(insert, 4, sqlite3_column_int(select, 3));\n        sqlite3_bind_int(insert, 5, lod);\n        sqlite3_bind_text(insert,\n                          6,\n                          (const char *)sqlite3_column_text(select, 4),\n                          -1,\n                          SQLITE_TRANSIENT);\n        if (sqlite3_step(insert) != SQLITE_DONE) {\n            ok = false;\n            set_error(error, error_size, sqlite3_errmsg(db));\n        } else {\n            ++stats->labels_written;\n            count_label_lod(stats, lod);\n        }\n    }\n    if (select) sqlite3_finalize(select);\n    if (insert) sqlite3_finalize(insert);\n    sqlite3_close(places);\n    return ok;\n}\n\n#define ORMAP_METADATA_BIN_DEG'''
)
replace_once(
    "src/map/ormap_builder.c",
    '            "CREATE TABLE labels(lat_e7 INTEGER NOT NULL,lon_e7 INTEGER NOT NULL,kind INTEGER NOT NULL,rank INTEGER NOT NULL,name TEXT NOT NULL);";',
    '            "CREATE TABLE labels(lat_e7 INTEGER NOT NULL,lon_e7 INTEGER NOT NULL,kind INTEGER NOT NULL,rank INTEGER NOT NULL,lod INTEGER NOT NULL,name TEXT NOT NULL);";'
)
replace_once(
    "src/map/ormap_builder.c",
    '                             "CREATE INDEX idx_labels_rank ON labels(rank DESC);",',
    '                             "CREATE INDEX idx_labels_lod_rank ON labels(lod,rank DESC);",'
)

# Reader: v6 label LOD, old maps remain compatible ---------------------------
replace_once(
    "src/map/ormap.c",
    '#include "openride/ormap.h"\n',
    '#include "openride/ormap.h"\n#include "openride/place_search.h"\n'
)
sub_once(
    "src/map/ormap.c",
    r"static bool load_labels\(OpenRideORMap \*map, char \*error, size_t error_size\)\n\{.*?\n\}\n\nOpenRideORMap \*openride_ormap_open",
    '''static uint8_t label_lod_from_kind(int kind)\n{\n    switch ((OpenRidePlaceKind)kind) {\n        case OPENRIDE_PLACE_CITY:\n            return OPENRIDE_ORMAP_LABEL_LOD_REGIONAL;\n        case OPENRIDE_PLACE_TOWN:\n            return OPENRIDE_ORMAP_LABEL_LOD_OVERVIEW;\n        case OPENRIDE_PLACE_VILLAGE:\n        case OPENRIDE_PLACE_SUBURB:\n            return OPENRIDE_ORMAP_LABEL_LOD_LOCAL;\n        default:\n            return OPENRIDE_ORMAP_LABEL_LOD_DETAIL;\n    }\n}\n\nstatic bool load_labels(OpenRideORMap *map, char *error, size_t error_size)\n{\n    sqlite3_stmt *stmt = NULL;\n    const bool has_lod = map->metadata.format_version >= 6;\n    const char *sql = has_lod\n        ? "SELECT lat_e7,lon_e7,kind,rank,name,lod FROM labels ORDER BY lod,rank DESC"\n        : "SELECT lat_e7,lon_e7,kind,rank,name FROM labels ORDER BY rank DESC";\n    int rc = sqlite3_prepare_v2(map->db, sql, -1, &stmt, NULL);\n    if (rc != SQLITE_OK) {\n        /* Labels are optional in format v1 so old test fixtures stay useful. */\n        return true;\n    }\n\n    uint32_t count = 0U;\n    uint32_t capacity = 0U;\n    OpenRideORMapLabel *labels = NULL;\n    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {\n        if (count == capacity) {\n            uint32_t next = capacity == 0U ? 256U : capacity * 2U;\n            if (next < capacity) {\n                rc = SQLITE_NOMEM;\n                break;\n            }\n            OpenRideORMapLabel *grown = realloc(labels,\n                                                (size_t)next * sizeof(*grown));\n            if (!grown) {\n                rc = SQLITE_NOMEM;\n                break;\n            }\n            labels = grown;\n            capacity = next;\n        }\n        OpenRideORMapLabel *label = &labels[count++];\n        memset(label, 0, sizeof(*label));\n        label->lat_e7 = sqlite3_column_int(stmt, 0);\n        label->lon_e7 = sqlite3_column_int(stmt, 1);\n        label->kind = sqlite3_column_int(stmt, 2);\n        label->rank = sqlite3_column_int(stmt, 3);\n        const char *name = (const char *)sqlite3_column_text(stmt, 4);\n        label->lod = has_lod\n            ? (uint8_t)sqlite3_column_int(stmt, 5)\n            : label_lod_from_kind(label->kind);\n        if (label->lod > OPENRIDE_ORMAP_LABEL_LOD_DETAIL) {\n            label->lod = OPENRIDE_ORMAP_LABEL_LOD_DETAIL;\n        }\n        snprintf(label->name, sizeof(label->name), "%s", name ? name : "");\n    }\n    sqlite3_finalize(stmt);\n    if (rc != SQLITE_DONE) {\n        free(labels);\n        set_error(error, error_size, "unable to read .ormap labels");\n        return false;\n    }\n    map->labels = labels;\n    map->label_count = count;\n    return true;\n}\n\nOpenRideORMap *openride_ormap_open'''
)

# Renderer: complementary road crossfades + semantic label fades -------------
sub_once(
    "src/map/ormap_renderer.c",
    r"static void apply_road_fades\(double zoom,.*?\n\}\n\nstatic void draw_road_pass",
    '''static void apply_road_fades(double zoom,\n                             int road_class,\n                             double level_fade,\n                             OpenRideMapRoadPaint *paint)\n{\n    if (!paint) return;\n    const double fade =\n        ormap_detail_handoff_fade(zoom)\n        * android_road_class_fade(zoom, road_class)\n        * level_fade;\n    ormap_scale_color_alpha(&paint->line, fade);\n    ormap_scale_color_alpha(&paint->casing, fade);\n}\n\nstatic void build_road_paint_table(OpenRideORMapRenderer *renderer,\n                                   double zoom,\n                                   double level_fade,\n                                   RoadPaintTable *table)\n{\n    memset(table, 0, sizeof(*table));\n    for (int road_class = OPENRIDE_ROAD_UNKNOWN;\n         road_class <= OPENRIDE_ROAD_OTHER;\n         ++road_class) {\n        table->visible[road_class] = openride_map_road_paint(\n            renderer->style,\n            road_kind((uint8_t)road_class),\n            false,\n            zoom,\n            &table->paints[road_class]);\n        if (table->visible[road_class]) {\n            apply_android_minor_road_lod(\n                zoom,\n                road_class,\n                &table->paints[road_class]);\n            apply_road_fades(\n                zoom,\n                road_class,\n                level_fade,\n                &table->paints[road_class]);\n        }\n    }\n}\n\nstatic void draw_road_pass'''
)
sub_once(
    "src/map/ormap_renderer.c",
    r"static int android_road_data_zoom\(const OpenRideORMapMetadata \*metadata,.*?\n\}\n\nstatic const char \*label_kind_name",
    '''static int android_road_data_zoom(const OpenRideORMapMetadata *metadata,\n                                  double camera_zoom)\n{\n    if (!metadata) return OPENRIDE_ORMAP_MIN_ROAD_ZOOM;\n\n    int zoom = (int)floor(camera_zoom);\n#ifdef __ANDROID__\n    const int min_zoom = metadata->min_zoom;\n    if (camera_zoom < 11.75) {\n        zoom = min_zoom;\n    } else if (camera_zoom < 12.75) {\n        zoom = min_zoom + 1;\n    } else if (camera_zoom < 13.75) {\n        zoom = min_zoom + 2;\n    } else if (camera_zoom < 14.50) {\n        zoom = min_zoom + 3;\n    } else {\n        zoom = min_zoom + 4;\n    }\n#endif\n\n    if (zoom < metadata->min_zoom) zoom = metadata->min_zoom;\n    if (zoom > metadata->road_max_zoom) zoom = metadata->road_max_zoom;\n    return zoom;\n}\n\nstatic void draw_roads_legacy(OpenRideORMapRenderer *renderer,\n                              const OpenRideMapCamera *camera,\n                              int width,\n                              int height,\n                              const OpenRideORMapMetadata *metadata)\n{\n    const int zoom = android_road_data_zoom(metadata, camera->zoom);\n    RoadPaintTable paint_table;\n    build_road_paint_table(renderer, camera->zoom, 1.0, &paint_table);\n    draw_road_pass(renderer, camera, width, height, zoom, &paint_table, true);\n    draw_road_pass(renderer, camera, width, height, zoom, &paint_table, false);\n}\n\nstatic void draw_roads(OpenRideORMapRenderer *renderer,\n                       const OpenRideMapCamera *camera,\n                       int width,\n                       int height)\n{\n    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);\n    if (!metadata) return;\n    if (metadata->format_version < 6) {\n        draw_roads_legacy(renderer, camera, width, height, metadata);\n        return;\n    }\n\n    const double regional_to_overview =\n        ormap_zoom_smoothstep(camera->zoom, 10.55, 11.25);\n    const double overview_to_local =\n        ormap_zoom_smoothstep(camera->zoom, 11.85, 12.80);\n    const double local_to_detail =\n        ormap_zoom_smoothstep(camera->zoom, 13.40, 14.40);\n\n    const int zooms[4] = {\n        OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM,\n        OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM,\n        OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM,\n        OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM\n    };\n    const double fades[4] = {\n        1.0 - regional_to_overview,\n        regional_to_overview * (1.0 - overview_to_local),\n        overview_to_local * (1.0 - local_to_detail),\n        local_to_detail\n    };\n    RoadPaintTable tables[4];\n    for (int i = 0; i < 4; ++i) {\n        build_road_paint_table(renderer, camera->zoom, fades[i], &tables[i]);\n    }\n\n    /* Keep road hierarchy coherent through a LOD handoff: all casings are\n     * submitted before all coloured strokes, even when two datasets overlap. */\n    for (int pass = 0; pass < 2; ++pass) {\n        const bool casing = pass == 0;\n        for (int i = 0; i < 4; ++i) {\n            if (fades[i] <= 0.001) continue;\n            draw_road_pass(renderer,\n                           camera,\n                           width,\n                           height,\n                           zooms[i],\n                           &tables[i],\n                           casing);\n        }\n    }\n}\n\nstatic const char *label_kind_name'''
)
replace_once(
    "src/map/ormap_renderer.c",
    '''static double label_fade_factor(const char *kind, double zoom)\n{\n    const double start = label_fade_start_zoom(kind);\n    return ormap_zoom_smoothstep(zoom, start, start + 0.65);\n}\n''',
    '''static double label_fade_factor(const char *kind, double zoom)\n{\n    const double start = label_fade_start_zoom(kind);\n    return ormap_zoom_smoothstep(zoom, start, start + 0.65);\n}\n\nstatic double label_lod_fade(const OpenRideORMapLabel *label, double zoom)\n{\n    if (!label) return 0.0;\n    switch ((OpenRideORMapLabelLOD)label->lod) {\n        case OPENRIDE_ORMAP_LABEL_LOD_REGIONAL:\n            return ormap_zoom_smoothstep(zoom, 10.0, 10.55);\n        case OPENRIDE_ORMAP_LABEL_LOD_OVERVIEW:\n            return ormap_zoom_smoothstep(zoom, 10.55, 11.25);\n        case OPENRIDE_ORMAP_LABEL_LOD_LOCAL:\n            return ormap_zoom_smoothstep(zoom, 11.85, 12.80);\n        case OPENRIDE_ORMAP_LABEL_LOD_DETAIL:\n        default:\n            return ormap_zoom_smoothstep(zoom, 13.40, 14.10);\n    }\n}\n'''
)
replace_once(
    "src/map/ormap_renderer.c",
    '''    uint32_t count = 0U;\n    const OpenRideORMapLabel *labels = openride_ormap_labels(renderer->map, &count);\n    if (!labels || count == 0U) return;\n''',
    '''    uint32_t count = 0U;\n    const OpenRideORMapLabel *labels = openride_ormap_labels(renderer->map, &count);\n    if (!labels || count == 0U) return;\n    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);\n'''
)
replace_once(
    "src/map/ormap_renderer.c",
    '''        const bool persistent_region_reference =\n            label_is_region_reference(labels, count, i);\n        const double label_fade = persistent_region_reference\n            ? detail_label_fade\n            : detail_label_fade * label_fade_factor(kind, camera->zoom);\n''',
    '''        const bool persistent_region_reference =\n            label_is_region_reference(labels, count, i);\n        const double label_fade = metadata && metadata->format_version >= 6\n            ? detail_label_fade * label_lod_fade(label, camera->zoom)\n            : (persistent_region_reference\n                ? detail_label_fade\n                : detail_label_fade * label_fade_factor(kind, camera->zoom));\n'''
)

# Builder CLI instrumentation -------------------------------------------------
replace_once(
    "src/tools/ormap_import_main.c",
    '''    printf("  tuiles routes      : %" PRIu64 "\\n", stats.road_tiles_written);\n''',
    '''    printf("  tuiles routes      : %" PRIu64 "\\n", stats.road_tiles_written);\n    printf("  routes regional    : %" PRIu64 " segments\\n", stats.road_regional_records);\n    printf("  routes overview    : %" PRIu64 " segments\\n", stats.road_overview_records);\n    printf("  routes local       : %" PRIu64 " segments\\n", stats.road_local_records);\n    printf("  routes detail      : %" PRIu64 " segments\\n", stats.road_detail_records);\n'''
)
replace_once(
    "src/tools/ormap_import_main.c",
    '''    printf("  labels              : %" PRIu64 "\\n", stats.labels_written);\n''',
    '''    printf("  labels              : %" PRIu64 "\\n", stats.labels_written);\n    printf("  labels regional    : %" PRIu64 "\\n", stats.label_regional_count);\n    printf("  labels overview    : %" PRIu64 "\\n", stats.label_overview_count);\n    printf("  labels local       : %" PRIu64 "\\n", stats.label_local_count);\n    printf("  labels detail      : %" PRIu64 "\\n", stats.label_detail_count);\n'''
)

# Tests ----------------------------------------------------------------------
replace_once(
    "tests/test_ormap.c",
    "    assert(OPENRIDE_ORMAP_FORMAT_VERSION == 5U);\n",
    '''    assert(OPENRIDE_ORMAP_FORMAT_VERSION == 6U);\n    assert(OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM < OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM);\n    assert(OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM < OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM);\n    assert(OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM < OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM);\n'''
)
replace_once(
    "tests/test_ormap.c",
    '''    assert(map_stats.routing_segments_seen > 0U);\n    assert(map_stats.road_tiles_written > 0U);\n''',
    '''    assert(map_stats.routing_segments_seen > 0U);\n    assert(map_stats.road_tiles_written > 0U);\n    assert(map_stats.road_detail_records > 0U);\n    assert(map_stats.road_records_written\n           == map_stats.road_regional_records\n            + map_stats.road_overview_records\n            + map_stats.road_local_records\n            + map_stats.road_detail_records);\n    assert(map_stats.labels_written\n           == map_stats.label_regional_count\n            + map_stats.label_overview_count\n            + map_stats.label_local_count\n            + map_stats.label_detail_count);\n'''
)
replace_once(
    "tests/test_ormap.c",
    '''    assert(metadata->format_version == (int)OPENRIDE_ORMAP_FORMAT_VERSION);\n    assert(metadata->max_zoom == OPENRIDE_ORMAP_MAX_ZOOM);\n''',
    '''    assert(metadata->format_version == (int)OPENRIDE_ORMAP_FORMAT_VERSION);\n    assert(metadata->min_zoom == OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM);\n    assert(metadata->max_zoom == OPENRIDE_ORMAP_MAX_ZOOM);\n'''
)

print("road + label LOD v1 patch applied")
