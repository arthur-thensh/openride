from pathlib import Path
import re


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def regex_once(text, pattern, replacement, label):
    text, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one regex match, found {count}")
    return text

# ---------------------------------------------------------------------------
# Public ORMap constants / statistics.
# ---------------------------------------------------------------------------
path = Path("include/openride/ormap.h")
text = path.read_text()
text = replace_once(text,
    "#define OPENRIDE_ORMAP_FORMAT_VERSION 4U",
    "#define OPENRIDE_ORMAP_FORMAT_VERSION 5U",
    "format version")
text = replace_once(text,
    "/* Filled vector areas use a compact coarse/detail LOD pair. */\n"
    "#define OPENRIDE_ORMAP_AREA_COARSE_ZOOM 11\n"
    "#define OPENRIDE_ORMAP_AREA_DETAIL_ZOOM 14",
    "/* Generalized landcover is stored at three vector LODs. Relative to a\n"
    " * z11 footprint these correspond to effective 32/64/128-cell grids. */\n"
    "#define OPENRIDE_ORMAP_AREA_REGIONAL_ZOOM 9\n"
    "#define OPENRIDE_ORMAP_AREA_OVERVIEW_ZOOM 10\n"
    "#define OPENRIDE_ORMAP_AREA_LOCAL_ZOOM 11\n"
    "/* Compatibility metadata key used by v3/v4 readers: local is the former coarse LOD. */\n"
    "#define OPENRIDE_ORMAP_AREA_COARSE_ZOOM OPENRIDE_ORMAP_AREA_LOCAL_ZOOM\n"
    "#define OPENRIDE_ORMAP_AREA_DETAIL_ZOOM 14",
    "area LOD constants")
text = replace_once(text,
    "    uint64_t area_triangles_written;\n"
    "    uint64_t area_tiles_written;",
    "    uint64_t area_triangles_written;\n"
    "    uint64_t area_tiles_written;\n"
    "    uint64_t landcover_regional_triangles;\n"
    "    uint64_t landcover_overview_triangles;\n"
    "    uint64_t landcover_local_triangles;",
    "landcover stats")
text = text.replace("In v4 this representation is used", "In v5 this representation is used")
text = text.replace("v4 stores built-up landuse at two robust LODs:",
                    "v5 stores generalized landcover at three vector LODs plus detailed masks:")
path.write_text(text)

# ---------------------------------------------------------------------------
# Builder: build local z11, derive z10/z9, morphologically generalize each.
# ---------------------------------------------------------------------------
path = Path("src/map/ormap_builder_v4.c")
text = path.read_text()
text = replace_once(text,
    "#define V4_COARSE_GRID 128U\n#define V4_COARSE_LAYER_BYTES ((V4_COARSE_GRID * V4_COARSE_GRID + 7U) / 8U)",
    "#define V4_COARSE_GRID 128U\n"
    "#define V4_MASK_GRID_LOG2 5U\n"
    "#define V4_COARSE_GRID_LOG2 7U\n"
    "#define V4_COARSE_LAYER_BYTES ((V4_COARSE_GRID * V4_COARSE_GRID + 7U) / 8U)",
    "grid log2 constants")
text = replace_once(text,
    "typedef struct V4CoarseMap {\n"
    "    V4CoarseBucket *buckets;\n"
    "    uint32_t capacity;\n"
    "    uint32_t count;\n"
    "} V4CoarseMap;",
    "typedef struct V4CoarseMap {\n"
    "    V4CoarseBucket *buckets;\n"
    "    uint32_t capacity;\n"
    "    uint32_t count;\n"
    "    int zoom;\n"
    "} V4CoarseMap;",
    "coarse map zoom")

coarse_helpers = r'''static int64_t v4_coarse_global_cells(const V4CoarseMap *map)
{
    if (!map || map->zoom < 0 || map->zoom > 22) return 0;
    return (int64_t)(1U << map->zoom) * V4_COARSE_GRID;
}

static bool v4_coarse_set(V4CoarseMap *map,
                          int64_t global_x,
                          int64_t global_y)
{
    if (!map || global_x < 0 || global_y < 0) return true;
    const int64_t global_cells = v4_coarse_global_cells(map);
    if (global_cells <= 0 || global_x >= global_cells || global_y >= global_cells) {
        return true;
    }
    const int tx = (int)(global_x / V4_COARSE_GRID);
    const int ty = (int)(global_y / V4_COARSE_GRID);
    V4CoarseBucket *bucket = v4_coarse_get(map, tx, ty, true);
    if (!bucket) return false;
    const uint32_t x = (uint32_t)(global_x % V4_COARSE_GRID);
    const uint32_t y = (uint32_t)(global_y % V4_COARSE_GRID);
    v4_bit_set(bucket->bits, y * V4_COARSE_GRID + x);
    return true;
}

static bool v4_coarse_get_global(const V4CoarseMap *map,
                                 int64_t gx,
                                 int64_t gy)
{
    if (!map || map->capacity == 0U || gx < 0 || gy < 0) return false;
    const int64_t global_cells = v4_coarse_global_cells(map);
    if (global_cells <= 0 || gx >= global_cells || gy >= global_cells) return false;
    const int tx = (int)(gx / V4_COARSE_GRID);
    const int ty = (int)(gy / V4_COARSE_GRID);
    const uint64_t key = v4_tile_key(tx, ty);
    uint32_t slot = v4_hash64(key) & (map->capacity - 1U);
    while (map->buckets[slot].used) {
        const V4CoarseBucket *bucket = &map->buckets[slot];
        if (bucket->key == key) {
            const uint32_t x = (uint32_t)(gx % V4_COARSE_GRID);
            const uint32_t y = (uint32_t)(gy % V4_COARSE_GRID);
            return v4_bit_get(bucket->bits, y * V4_COARSE_GRID + x);
        }
        slot = (slot + 1U) & (map->capacity - 1U);
    }
    return false;
}

static bool v4_build_coarse_map(const V4UrbanMaskMap *source,
                                V4CoarseMap *coarse,
                                int zoom)
{
    if (!source || !coarse || zoom < 0) return false;
    const unsigned source_bits = OPENRIDE_ORMAP_MASK_ZOOM + V4_MASK_GRID_LOG2;
    const unsigned target_bits = (unsigned)zoom + V4_COARSE_GRID_LOG2;
    if (target_bits > source_bits) return false;
    const unsigned shift = source_bits - target_bits;
    coarse->zoom = zoom;

    for (uint32_t b = 0U; b < source->capacity; ++b) {
        const V4UrbanMaskBucket *bucket = &source->buckets[b];
        if (!bucket->used) continue;
        int tx = 0;
        int ty = 0;
        v4_decode_tile_key(bucket->key, &tx, &ty);
        for (uint32_t y = 0U; y < OPENRIDE_ORMAP_MASK_GRID; ++y) {
            for (uint32_t x = 0U; x < OPENRIDE_ORMAP_MASK_GRID; ++x) {
                if (!v4_bit_get(bucket->bits,
                                y * OPENRIDE_ORMAP_MASK_GRID + x)) {
                    continue;
                }
                const int64_t high_x =
                    (int64_t)tx * OPENRIDE_ORMAP_MASK_GRID + x;
                const int64_t high_y =
                    (int64_t)ty * OPENRIDE_ORMAP_MASK_GRID + y;
                if (!v4_coarse_set(coarse,
                                   high_x >> shift,
                                   high_y >> shift)) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool v4_build_parent_coarse(const V4CoarseMap *child,
                                   V4CoarseMap *parent)
{
    if (!child || !parent || child->zoom <= 0) return false;
    parent->zoom = child->zoom - 1;
    for (uint32_t b = 0U; b < child->capacity; ++b) {
        const V4CoarseBucket *bucket = &child->buckets[b];
        if (!bucket->used) continue;
        int tx = 0;
        int ty = 0;
        v4_decode_tile_key(bucket->key, &tx, &ty);
        for (uint32_t y = 0U; y < V4_COARSE_GRID; ++y) {
            for (uint32_t x = 0U; x < V4_COARSE_GRID; ++x) {
                if (!v4_bit_get(bucket->bits, y * V4_COARSE_GRID + x)) continue;
                const int64_t gx = (int64_t)tx * V4_COARSE_GRID + x;
                const int64_t gy = (int64_t)ty * V4_COARSE_GRID + y;
                if (!v4_coarse_set(parent, gx >> 1, gy >> 1)) return false;
            }
        }
    }
    return true;
}

static bool v4_close_coarse(V4CoarseMap *map)
{
    if (!map || map->capacity == 0U) return true;
    V4CoarseMap dilated = {.zoom = map->zoom};
    for (uint32_t b = 0U; b < map->capacity; ++b) {
        const V4CoarseBucket *bucket = &map->buckets[b];
        if (!bucket->used) continue;
        int tx = 0;
        int ty = 0;
        v4_decode_tile_key(bucket->key, &tx, &ty);
        for (uint32_t y = 0U; y < V4_COARSE_GRID; ++y) {
            for (uint32_t x = 0U; x < V4_COARSE_GRID; ++x) {
                if (!v4_bit_get(bucket->bits, y * V4_COARSE_GRID + x)) continue;
                const int64_t gx = (int64_t)tx * V4_COARSE_GRID + x;
                const int64_t gy = (int64_t)ty * V4_COARSE_GRID + y;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!v4_coarse_set(&dilated, gx + dx, gy + dy)) {
                            v4_coarse_destroy(&dilated);
                            return false;
                        }
                    }
                }
            }
        }
    }

    V4CoarseMap closed = {.zoom = map->zoom};
    for (uint32_t b = 0U; b < dilated.capacity; ++b) {
        const V4CoarseBucket *bucket = &dilated.buckets[b];
        if (!bucket->used) continue;
        int tx = 0;
        int ty = 0;
        v4_decode_tile_key(bucket->key, &tx, &ty);
        for (uint32_t y = 0U; y < V4_COARSE_GRID; ++y) {
            for (uint32_t x = 0U; x < V4_COARSE_GRID; ++x) {
                if (!v4_bit_get(bucket->bits, y * V4_COARSE_GRID + x)) continue;
                const int64_t gx = (int64_t)tx * V4_COARSE_GRID + x;
                const int64_t gy = (int64_t)ty * V4_COARSE_GRID + y;
                bool keep = true;
                for (int dy = -1; dy <= 1 && keep; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!v4_coarse_get_global(&dilated, gx + dx, gy + dy)) {
                            keep = false;
                            break;
                        }
                    }
                }
                if (keep && !v4_coarse_set(&closed, gx, gy)) {
                    v4_coarse_destroy(&dilated);
                    v4_coarse_destroy(&closed);
                    return false;
                }
            }
        }
    }
    v4_coarse_destroy(&dilated);
    v4_coarse_destroy(map);
    *map = closed;
    return true;
}

static bool v4_filter_coarse(V4CoarseMap *map, unsigned minimum_neighbours)
{
    if (!map || map->capacity == 0U) return true;
    V4CoarseMap filtered = {.zoom = map->zoom};
    for (uint32_t b = 0U; b < map->capacity; ++b) {
        const V4CoarseBucket *bucket = &map->buckets[b];
        if (!bucket->used) continue;
        int tx = 0;
        int ty = 0;
        v4_decode_tile_key(bucket->key, &tx, &ty);
        for (uint32_t y = 0U; y < V4_COARSE_GRID; ++y) {
            for (uint32_t x = 0U; x < V4_COARSE_GRID; ++x) {
                if (!v4_bit_get(bucket->bits, y * V4_COARSE_GRID + x)) continue;
                const int64_t gx = (int64_t)tx * V4_COARSE_GRID + x;
                const int64_t gy = (int64_t)ty * V4_COARSE_GRID + y;
                unsigned neighbours = 0U;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (v4_coarse_get_global(map, gx + dx, gy + dy)) {
                            ++neighbours;
                        }
                    }
                }
                if (neighbours >= minimum_neighbours
                    && !v4_coarse_set(&filtered, gx, gy)) {
                    v4_coarse_destroy(&filtered);
                    return false;
                }
            }
        }
    }
    v4_coarse_destroy(map);
    *map = filtered;
    return true;
}

static bool v4_prepare_coarse(V4CoarseMap *map,
                              unsigned close_passes,
                              unsigned minimum_neighbours)
{
    for (unsigned pass = 0U; pass < close_passes; ++pass) {
        if (!v4_close_coarse(map)) return false;
    }
    return v4_filter_coarse(map, minimum_neighbours);
}
'''
text = regex_once(text,
    r"static bool v4_coarse_set\(.*?\nstatic bool v4_decompress_blob",
    coarse_helpers + "\nstatic bool v4_decompress_blob",
    "replace coarse helpers")

# Generalize append routine: zoom comes from the LOD map and statistics are accumulated.
text = replace_once(text,
    "static bool v4_append_coarse_layer(sqlite3 *db,\n"
    "                         const V4CoarseMap *coarse,\n"
    "                         uint8_t kind,\n"
    "                         char *error,\n"
    "                         size_t error_size)",
    "static bool v4_append_coarse_layer(sqlite3 *db,\n"
    "                         const V4CoarseMap *coarse,\n"
    "                         uint8_t kind,\n"
    "                         uint64_t *triangle_counter,\n"
    "                         char *error,\n"
    "                         size_t error_size)",
    "append signature")
text = replace_once(text,
    "    for (uint32_t b = 0U; b < coarse->capacity && ok; ++b) {",
    "    const int zoom = coarse ? coarse->zoom : -1;\n"
    "    if (zoom < 0) {\n"
    "        ok = false;\n"
    "        v4_set_error(error, error_size, \"invalid landcover LOD zoom\");\n"
    "        goto done;\n"
    "    }\n\n"
    "    for (uint32_t b = 0U; b < coarse->capacity && ok; ++b) {",
    "append zoom validation")
text = replace_once(text,
    "        const double tolerance = kind == OPENRIDE_ORMAP_AREA_GREEN ? 1.20 : 0.90;",
    "        const double lod_delta = (double)(OPENRIDE_ORMAP_AREA_LOCAL_ZOOM - zoom);\n"
    "        const double tolerance =\n"
    "            (kind == OPENRIDE_ORMAP_AREA_GREEN ? 1.25 : 1.00)\n"
    "            + lod_delta * 0.55;",
    "LOD simplification tolerance")
text = text.replace("sqlite3_bind_int(select, 1, OPENRIDE_ORMAP_AREA_COARSE_ZOOM);",
                    "sqlite3_bind_int(select, 1, zoom);")
text = text.replace("sqlite3_bind_int(upsert, 1, OPENRIDE_ORMAP_AREA_COARSE_ZOOM);",
                    "sqlite3_bind_int(upsert, 1, zoom);")
text = replace_once(text,
    "        for (uint32_t i = 0U; i < mesh.triangle_count; ++i) {",
    "        const uint32_t new_triangle_count = mesh.triangle_count;\n"
    "        for (uint32_t i = 0U; i < mesh.triangle_count; ++i) {",
    "save mesh triangle count")
text = replace_once(text,
    "        ok = sqlite3_step(upsert) == SQLITE_DONE;\n"
    "        free(compressed);\n"
    "        if (!ok) v4_set_error(error, error_size, sqlite3_errmsg(db));\n"
    "    }\n\n"
    "done:\n"
    "    if (select) sqlite3_finalize(select);\n"
    "    if (upsert) sqlite3_finalize(upsert);\n"
    "    return ok;\n"
    "}\n\n"
    "static bool v4_write_urban_masks",
    "        ok = sqlite3_step(upsert) == SQLITE_DONE;\n"
    "        free(compressed);\n"
    "        if (!ok) {\n"
    "            v4_set_error(error, error_size, sqlite3_errmsg(db));\n"
    "        } else if (triangle_counter) {\n"
    "            *triangle_counter += new_triangle_count;\n"
    "        }\n"
    "    }\n\n"
    "done:\n"
    "    if (select) sqlite3_finalize(select);\n"
    "    if (upsert) sqlite3_finalize(upsert);\n"
    "    return ok;\n"
    "}\n\n"
    "static bool v4_write_urban_masks",
    "append statistics")

text = replace_once(text,
    "static bool v4_postprocess(const char *pbf_path,\n"
    "                 const char *output_path,\n"
    "                 char *error,\n"
    "                 size_t error_size)",
    "static bool v4_postprocess(const char *pbf_path,\n"
    "                 const char *output_path,\n"
    "                 OpenRideORMapBuildStats *stats,\n"
    "                 char *error,\n"
    "                 size_t error_size)",
    "postprocess stats signature")

lod_build_block = '''    V4CoarseMap local_urban = {.zoom = OPENRIDE_ORMAP_AREA_LOCAL_ZOOM};
    V4CoarseMap local_green = {.zoom = OPENRIDE_ORMAP_AREA_LOCAL_ZOOM};
    V4CoarseMap overview_urban = {.zoom = OPENRIDE_ORMAP_AREA_OVERVIEW_ZOOM};
    V4CoarseMap overview_green = {.zoom = OPENRIDE_ORMAP_AREA_OVERVIEW_ZOOM};
    V4CoarseMap regional_urban = {.zoom = OPENRIDE_ORMAP_AREA_REGIONAL_ZOOM};
    V4CoarseMap regional_green = {.zoom = OPENRIDE_ORMAP_AREA_REGIONAL_ZOOM};

    if (!v4_build_coarse_map(&context.urban,
                             &local_urban,
                             OPENRIDE_ORMAP_AREA_LOCAL_ZOOM)
        || !v4_build_coarse_map(&context.green,
                                &local_green,
                                OPENRIDE_ORMAP_AREA_LOCAL_ZOOM)
        || !v4_prepare_coarse(&local_urban, 1U, 3U)
        || !v4_prepare_coarse(&local_green, 1U, 4U)
        || !v4_build_parent_coarse(&local_urban, &overview_urban)
        || !v4_build_parent_coarse(&local_green, &overview_green)
        || !v4_prepare_coarse(&overview_urban, 1U, 4U)
        || !v4_prepare_coarse(&overview_green, 1U, 5U)
        || !v4_build_parent_coarse(&overview_urban, &regional_urban)
        || !v4_build_parent_coarse(&overview_green, &regional_green)
        || !v4_prepare_coarse(&regional_urban, 2U, 5U)
        || !v4_prepare_coarse(&regional_green, 2U, 5U)) {
        v4_coarse_destroy(&regional_green);
        v4_coarse_destroy(&regional_urban);
        v4_coarse_destroy(&overview_green);
        v4_coarse_destroy(&overview_urban);
        v4_coarse_destroy(&local_green);
        v4_coarse_destroy(&local_urban);
        v4_urban_destroy(&context.green);
        v4_urban_destroy(&context.urban);
        v4_set_error(error, error_size, "unable to prepare v5 landcover LODs");
        return false;
    }
'''
text = regex_once(text,
    r"    V4CoarseMap coarse_urban = \{0\};.*?    sqlite3 \*db = NULL;",
    lod_build_block + "\n    sqlite3 *db = NULL;",
    "LOD build block")

lod_write_block = '''    /* Each v5 landcover LOD uses the same ORA1 triangle encoding. Green is
     * always appended before urban so built-up remains visually dominant. */
    struct V4LandcoverWrite {
        const V4CoarseMap *green;
        const V4CoarseMap *urban;
        uint64_t *counter;
    } writes[3] = {
        {&regional_green, &regional_urban,
         stats ? &stats->landcover_regional_triangles : NULL},
        {&overview_green, &overview_urban,
         stats ? &stats->landcover_overview_triangles : NULL},
        {&local_green, &local_urban,
         stats ? &stats->landcover_local_triangles : NULL}
    };
    for (uint32_t level = 0U; level < 3U && ok; ++level) {
        ok = v4_append_coarse_layer(db,
                                    writes[level].green,
                                    OPENRIDE_ORMAP_AREA_GREEN,
                                    writes[level].counter,
                                    error,
                                    error_size);
        if (ok) {
            ok = v4_append_coarse_layer(db,
                                        writes[level].urban,
                                        OPENRIDE_ORMAP_AREA_BUILTUP,
                                        writes[level].counter,
                                        error,
                                        error_size);
        }
    }
    if (ok && stats) {
        stats->area_triangles_written += stats->landcover_regional_triangles
            + stats->landcover_overview_triangles
            + stats->landcover_local_triangles;
    }
'''
text = regex_once(text,
    r"    /\* Green first, urban second:.*?    if \(ok\) ok = v4_write_urban_masks",
    lod_write_block + "    if (ok) ok = v4_write_urban_masks",
    "LOD write block")

text = replace_once(text,
    "    v4_coarse_destroy(&coarse_green);\n"
    "    v4_coarse_destroy(&coarse_urban);",
    "    v4_coarse_destroy(&regional_green);\n"
    "    v4_coarse_destroy(&regional_urban);\n"
    "    v4_coarse_destroy(&overview_green);\n"
    "    v4_coarse_destroy(&overview_urban);\n"
    "    v4_coarse_destroy(&local_green);\n"
    "    v4_coarse_destroy(&local_urban);",
    "LOD cleanup")
text = replace_once(text,
    "    if (!v4_postprocess(pbf_path, output_path, error, error_size)) {",
    "    if (stats_out) {\n"
    "        stats_out->landcover_regional_triangles = 0U;\n"
    "        stats_out->landcover_overview_triangles = 0U;\n"
    "        stats_out->landcover_local_triangles = 0U;\n"
    "    }\n\n"
    "    if (!v4_postprocess(pbf_path, output_path, stats_out, error, error_size)) {",
    "postprocess call")
text = text.replace("/* v4 keeps semantic built-up masks", "/* v5 keeps semantic built-up masks")
path.write_text(text)

# ---------------------------------------------------------------------------
# Renderer: preserve v4 behavior, use three complementary vector LODs for v5.
# ---------------------------------------------------------------------------
path = Path("src/map/ormap_renderer_v4.c")
text = path.read_text()
new_renderer = r'''static void v4_draw_landcover_level_kind(OpenRideORMapRenderer *renderer,
                                             const OpenRideMapCamera *camera,
                                             int width,
                                             int height,
                                             int zoom,
                                             uint8_t kind,
                                             OpenRideMapColor color)
{
    if (color.a == 0U) return;
    const int count = 1 << zoom;
    double tile_size = 0.0;
    double center_x = 0.0;
    double center_y = 0.0;
    int first_x = 0, last_x = 0, first_y = 0, last_y = 0;
    V4Rotation rotation;
    v4_visible_tile_range(camera,
                          width,
                          height,
                          zoom,
                          &tile_size,
                          &center_x,
                          &center_y,
                          &first_x,
                          &last_x,
                          &first_y,
                          &last_y,
                          &rotation);

    V4GeometryBatch batch = {0};
    for (int ty = first_y; ty <= last_y; ++ty) {
        if (ty < 0 || ty >= count) continue;
        for (int tx = first_x; tx <= last_x; ++tx) {
            const int qx = v4_wrap_x(tx, count);
            OpenRideORMapAreaCacheEntry *entry =
                v4_area_cache_slot(renderer, zoom, qx, ty);
            if (!entry || entry->tile.count == 0U) continue;
            const double left = width * 0.5 + tx * tile_size - center_x;
            const double top = height * 0.5 + ty * tile_size - center_y;
            for (uint32_t i = 0U; i < entry->tile.count; ++i) {
                const OpenRideORMapAreaTriangle *triangle =
                    &entry->tile.triangles[i];
                if (triangle->kind != kind) continue;
                const uint16_t xs[3] = {
                    triangle->x1, triangle->x2, triangle->x3
                };
                const uint16_t ys[3] = {
                    triangle->y1, triangle->y2, triangle->y3
                };
                float x[3];
                float y[3];
                float min_x = FLT_MAX;
                float min_y = FLT_MAX;
                float max_x = -FLT_MAX;
                float max_y = -FLT_MAX;
                for (uint32_t v = 0U; v < 3U; ++v) {
                    x[v] = (float)(left
                        + v4_decode_area_coord(xs[v]) * tile_size);
                    y[v] = (float)(top
                        + v4_decode_area_coord(ys[v]) * tile_size);
                    v4_rotate_point(&rotation, &x[v], &y[v]);
                    if (x[v] < min_x) min_x = x[v];
                    if (x[v] > max_x) max_x = x[v];
                    if (y[v] < min_y) min_y = y[v];
                    if (y[v] > max_y) max_y = y[v];
                }
                if (max_x < -2.0f || min_x > width + 2.0f
                    || max_y < -2.0f || min_y > height + 2.0f) {
                    continue;
                }
                if (!v4_batch_triangle(renderer, &batch, x, y, color)) {
                    v4_batch_flush(renderer, &batch);
                    return;
                }
            }
        }
    }
    v4_batch_flush(renderer, &batch);
}

static void v4_draw_coarse_landcover(OpenRideORMapRenderer *renderer,
                                     const OpenRideMapCamera *camera,
                                     int width,
                                     int height)
{
    if (camera->zoom < 10.0 || camera->zoom > 14.45) return;
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    if (!metadata || metadata->format_version < 4) return;

    const double handoff_in = v4_smoothstep(camera->zoom, 10.0, 11.30);
    const double urban_out = 1.0 - v4_smoothstep(camera->zoom, 13.15, 13.75);
    const double green_out = 1.0 - v4_smoothstep(camera->zoom, 13.40, 14.40);

    if (metadata->format_version < 5) {
        const OpenRideMapColor green =
            v4_green_color(renderer, handoff_in * green_out);
        const OpenRideMapColor urban =
            v4_builtup_color(renderer, false, handoff_in * urban_out);
        v4_draw_landcover_level_kind(renderer,
                                     camera,
                                     width,
                                     height,
                                     metadata->area_coarse_zoom,
                                     OPENRIDE_ORMAP_AREA_GREEN,
                                     green);
        v4_draw_landcover_level_kind(renderer,
                                     camera,
                                     width,
                                     height,
                                     metadata->area_coarse_zoom,
                                     OPENRIDE_ORMAP_AREA_BUILTUP,
                                     urban);
        return;
    }

    const double regional_to_overview =
        v4_smoothstep(camera->zoom, 10.55, 11.25);
    const double overview_to_local =
        v4_smoothstep(camera->zoom, 11.85, 12.80);
    const double weights[3] = {
        1.0 - regional_to_overview,
        regional_to_overview * (1.0 - overview_to_local),
        overview_to_local
    };
    const int zooms[3] = {
        OPENRIDE_ORMAP_AREA_REGIONAL_ZOOM,
        OPENRIDE_ORMAP_AREA_OVERVIEW_ZOOM,
        OPENRIDE_ORMAP_AREA_LOCAL_ZOOM
    };

    /* Draw every green LOD before every urban LOD so the semantic hierarchy
     * stays stable during crossfades. At most two adjacent LODs have non-zero
     * weight at once. */
    for (uint32_t pass = 0U; pass < 2U; ++pass) {
        const uint8_t kind = pass == 0U
            ? OPENRIDE_ORMAP_AREA_GREEN
            : OPENRIDE_ORMAP_AREA_BUILTUP;
        const double detail_out = kind == OPENRIDE_ORMAP_AREA_GREEN
            ? green_out : urban_out;
        for (uint32_t level = 0U; level < 3U; ++level) {
            const double factor = handoff_in * detail_out * weights[level];
            if (factor <= 0.001) continue;
            const OpenRideMapColor color = kind == OPENRIDE_ORMAP_AREA_GREEN
                ? v4_green_color(renderer, factor)
                : v4_builtup_color(renderer, false, factor);
            v4_draw_landcover_level_kind(renderer,
                                         camera,
                                         width,
                                         height,
                                         zooms[level],
                                         kind,
                                         color);
        }
    }
}
'''
text = regex_once(text,
    r"static void v4_draw_coarse_landcover\(.*?\n\}\n\nstatic void v4_draw_detail_builtup",
    new_renderer + "\nstatic void v4_draw_detail_builtup",
    "replace landcover renderer")
path.write_text(text)

# ---------------------------------------------------------------------------
# Builder CLI: expose LOD triangle counts so real-region generation can be
# compared without profiling the renderer first.
# ---------------------------------------------------------------------------
path = Path("src/tools/ormap_import_main.c")
text = path.read_text()
text = replace_once(text,
    "    printf(\"  triangles surfaces : %\" PRIu64 \"\\n\", stats.area_triangles_written);\n"
    "    printf(\"  tuiles surfaces    : %\" PRIu64 \"\\n\", stats.area_tiles_written);",
    "    printf(\"  triangles surfaces : %\" PRIu64 \"\\n\", stats.area_triangles_written);\n"
    "    printf(\"  landcover regional : %\" PRIu64 \" triangles\\n\",\n"
    "           stats.landcover_regional_triangles);\n"
    "    printf(\"  landcover overview : %\" PRIu64 \" triangles\\n\",\n"
    "           stats.landcover_overview_triangles);\n"
    "    printf(\"  landcover local    : %\" PRIu64 \" triangles\\n\",\n"
    "           stats.landcover_local_triangles);\n"
    "    printf(\"  tuiles surfaces    : %\" PRIu64 \"\\n\", stats.area_tiles_written);",
    "CLI LOD stats")
path.write_text(text)

# ---------------------------------------------------------------------------
# Tests: assert the LOD ordering/capability contract.
# ---------------------------------------------------------------------------
path = Path("tests/test_ormap.c")
text = path.read_text()
text = replace_once(text,
    "    assert(OPENRIDE_ORMAP_AREA_GREEN == 3);",
    "    assert(OPENRIDE_ORMAP_AREA_GREEN == 3);\n"
    "    assert(OPENRIDE_ORMAP_FORMAT_VERSION == 5U);\n"
    "    assert(OPENRIDE_ORMAP_AREA_REGIONAL_ZOOM < OPENRIDE_ORMAP_AREA_OVERVIEW_ZOOM);\n"
    "    assert(OPENRIDE_ORMAP_AREA_OVERVIEW_ZOOM < OPENRIDE_ORMAP_AREA_LOCAL_ZOOM);\n"
    "    assert(OPENRIDE_ORMAP_AREA_LOCAL_ZOOM == OPENRIDE_ORMAP_AREA_COARSE_ZOOM);",
    "LOD contract tests")
path.write_text(text)

print("landcover LOD v2 patch applied")
