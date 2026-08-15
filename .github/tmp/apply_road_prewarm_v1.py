from pathlib import Path
import re


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def regex_once(text: str, pattern: str, replacement: str, label: str) -> str:
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one regex match, found {count}")
    return updated


header_path = Path("src/map/ormap_renderer.h")
header = header_path.read_text()

header = replace_once(
    header,
    "typedef struct OpenRideORMapRenderer {\n",
    """typedef struct OpenRideORMapRoadDebugStats {
    double roads_ms;
    double load_ms;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t prewarm_loads;
    uint32_t draw_loads;
    uint32_t deferred_loads;
    int prewarm_zoom;
} OpenRideORMapRoadDebugStats;

typedef struct OpenRideORMapRenderer {
""",
    "debug stats struct",
)

header = replace_once(
    header,
    "    uint64_t frame_counter;\n",
    """    uint64_t frame_counter;
    OpenRideORMapRoadDebugStats road_debug;
    uint32_t road_draw_load_budget_remaining;
    double road_previous_camera_zoom;
    bool road_has_previous_camera_zoom;
    int road_zoom_direction;
""",
    "renderer road debug state",
)

header = replace_once(
    header,
    "void openride_ormap_renderer_begin_frame(OpenRideORMapRenderer *renderer);\n",
    """void openride_ormap_renderer_begin_frame(OpenRideORMapRenderer *renderer);
void openride_ormap_renderer_get_road_debug_stats(
    const OpenRideORMapRenderer *renderer,
    OpenRideORMapRoadDebugStats *stats);
""",
    "debug getter prototype",
)
header_path.write_text(header)


renderer_path = Path("src/map/ormap_renderer.c")
renderer = renderer_path.read_text()

renderer = replace_once(
    renderer,
    "#define ORMAP_GEOMETRY_BATCH_INDEX_LIMIT 24576U\n",
    """#define ORMAP_GEOMETRY_BATCH_INDEX_LIMIT 24576U
#define ORMAP_ROAD_PREWARM_TILE_BUDGET 3U
#define ORMAP_ROAD_DRAW_LOAD_BUDGET 2U
""",
    "road load budgets",
)

road_cache_impl = r'''static bool road_cache_contains(const OpenRideORMapRenderer *renderer,
                                int zoom,
                                int x,
                                int y)
{
    if (!renderer) return false;
    const size_t base = tile_cache_set_base(OPENRIDE_ORMAP_ROAD_CACHE_CAPACITY,
                                            zoom, x, y);
    for (size_t i = 0U; i < ORMAP_CACHE_ASSOCIATIVITY; ++i) {
        const OpenRideORMapRoadCacheEntry *entry = &renderer->roads[base + i];
        if (entry->occupied && entry->zoom == zoom && entry->x == x && entry->y == y) {
            return true;
        }
    }
    return false;
}

static OpenRideORMapRoadCacheEntry *road_cache_slot(OpenRideORMapRenderer *renderer,
                                                     int zoom,
                                                     int x,
                                                     int y,
                                                     bool prewarm,
                                                     bool budgeted_draw_load)
{
    const size_t base = tile_cache_set_base(OPENRIDE_ORMAP_ROAD_CACHE_CAPACITY,
                                            zoom, x, y);
    OpenRideORMapRoadCacheEntry *victim = NULL;
    OpenRideORMapRoadCacheEntry *oldest = &renderer->roads[base];
    for (size_t i = 0U; i < ORMAP_CACHE_ASSOCIATIVITY; ++i) {
        OpenRideORMapRoadCacheEntry *entry = &renderer->roads[base + i];
        if (entry->occupied && entry->zoom == zoom && entry->x == x && entry->y == y) {
            entry->last_used = renderer->frame_counter;
            ++renderer->road_debug.cache_hits;
            return entry;
        }
        if (!entry->occupied && !victim) victim = entry;
        if (entry->occupied && oldest->occupied && entry->last_used < oldest->last_used) {
            oldest = entry;
        }
    }

    ++renderer->road_debug.cache_misses;
    if (budgeted_draw_load && !prewarm) {
        if (renderer->road_draw_load_budget_remaining == 0U) {
            ++renderer->road_debug.deferred_loads;
            return NULL;
        }
        --renderer->road_draw_load_budget_remaining;
    }

    if (!victim) victim = oldest;
    road_cache_entry_destroy(victim);
    victim->occupied = true;
    victim->zoom = zoom;
    victim->x = x;
    victim->y = y;
    victim->last_used = renderer->frame_counter;

    const uint64_t load_started = SDL_GetTicksNS();
    char error[160] = {0};
    if (!openride_ormap_load_road_tile(renderer->map,
                                       zoom,
                                       x,
                                       y,
                                       &victim->tile,
                                       error,
                                       sizeof(error))) {
        /* Keep an occupied empty entry to cache absent tiles too. */
    } else {
        (void)road_cache_build_class_index(victim);
    }
    renderer->road_debug.load_ms +=
        (double)(SDL_GetTicksNS() - load_started) / 1000000.0;
    if (prewarm) {
        ++renderer->road_debug.prewarm_loads;
    } else {
        ++renderer->road_debug.draw_loads;
    }
    return victim;
}

static OpenRideORMapMaskCacheEntry *mask_cache_slot'''

renderer = regex_once(
    renderer,
    r"static OpenRideORMapRoadCacheEntry \*road_cache_slot\(.*?\n\}\n\nstatic OpenRideORMapMaskCacheEntry \*mask_cache_slot",
    road_cache_impl,
    "road cache implementation",
)

renderer = replace_once(
    renderer,
    "static void draw_road_pass(OpenRideORMapRenderer *renderer,\n"
    "                           const OpenRideMapCamera *camera,\n"
    "                           int width,\n"
    "                           int height,\n"
    "                           int zoom,\n"
    "                           const RoadPaintTable *paint_table,\n"
    "                           bool casing_pass)\n",
    "static void draw_road_pass(OpenRideORMapRenderer *renderer,\n"
    "                           const OpenRideMapCamera *camera,\n"
    "                           int width,\n"
    "                           int height,\n"
    "                           int zoom,\n"
    "                           const RoadPaintTable *paint_table,\n"
    "                           bool casing_pass,\n"
    "                           bool budgeted_draw_load)\n",
    "draw road pass signature",
)

renderer = replace_once(
    renderer,
    "            OpenRideORMapRoadCacheEntry *entry = road_cache_slot(renderer, zoom, qx, ty);\n",
    "            OpenRideORMapRoadCacheEntry *entry =\n"
    "                road_cache_slot(renderer, zoom, qx, ty, false, budgeted_draw_load);\n",
    "road draw cache lookup",
)

paint_helper_anchor = "static void draw_road_pass(OpenRideORMapRenderer *renderer,\n"
paint_helper = r'''static bool road_paint_table_has_casing(const RoadPaintTable *table)
{
    if (!table) return false;
    for (int road_class = OPENRIDE_ROAD_UNKNOWN;
         road_class <= OPENRIDE_ROAD_OTHER;
         ++road_class) {
        if (!table->visible[road_class]) continue;
        const OpenRideMapRoadPaint *paint = &table->paints[road_class];
        if (paint->casing_width > paint->width && paint->casing.a > 0U) return true;
    }
    return false;
}

'''
renderer = replace_once(
    renderer,
    paint_helper_anchor,
    paint_helper + paint_helper_anchor,
    "casing helper",
)

prewarm_helpers = r'''static uint32_t prewarm_road_level(OpenRideORMapRenderer *renderer,
                                   const OpenRideMapCamera *camera,
                                   int width,
                                   int height,
                                   int zoom,
                                   uint32_t budget)
{
    if (!renderer || !camera || budget == 0U) return 0U;
    const int count = 1 << zoom;
    const double scale = pow(2.0, camera->zoom - zoom);
    const double tile_size = ORMAP_TILE_SIZE * scale;
    const OpenRidePointD center = openride_mercator_forward(camera->center_lat,
                                                             camera->center_lon);
    const double world_size = tile_size * count;
    const double center_x = center.x * world_size;
    const double center_y = center.y * world_size;
    const double bearing = camera->bearing_deg * 3.14159265358979323846 / 180.0;
    const double half_w = fabs(cos(bearing)) * width * 0.5
        + fabs(sin(bearing)) * height * 0.5;
    const double half_h = fabs(sin(bearing)) * width * 0.5
        + fabs(cos(bearing)) * height * 0.5;
    const int first_x = (int)floor((center_x - half_w) / tile_size);
    const int last_x = (int)floor((center_x + half_w) / tile_size);
    const int first_y = (int)floor((center_y - half_h) / tile_size);
    const int last_y = (int)floor((center_y + half_h) / tile_size);

    uint32_t loaded = 0U;
    for (int ty = first_y; ty <= last_y; ++ty) {
        if (ty < 0 || ty >= count) continue;
        for (int tx = first_x; tx <= last_x; ++tx) {
            const int qx = wrap_x(tx, count);
            if (road_cache_contains(renderer, zoom, qx, ty)) continue;
            (void)road_cache_slot(renderer, zoom, qx, ty, true, false);
            if (++loaded >= budget) return loaded;
        }
    }
    return loaded;
}

static int road_prewarm_target_zoom(OpenRideORMapRenderer *renderer,
                                    double camera_zoom)
{
    if (!renderer) return -1;
    if (!renderer->road_has_previous_camera_zoom) {
        renderer->road_previous_camera_zoom = camera_zoom;
        renderer->road_has_previous_camera_zoom = true;
        return -1;
    }

    const double delta = camera_zoom - renderer->road_previous_camera_zoom;
    renderer->road_previous_camera_zoom = camera_zoom;
    if (delta > 0.0001) {
        renderer->road_zoom_direction = 1;
    } else if (delta < -0.0001) {
        renderer->road_zoom_direction = -1;
    }

    if (renderer->road_zoom_direction > 0) {
        if (camera_zoom >= 12.80 && camera_zoom < 14.40) {
            return OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM;
        }
        if (camera_zoom >= 11.15 && camera_zoom < 12.80) {
            return OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM;
        }
        if (camera_zoom >= 9.95 && camera_zoom < 11.25) {
            return OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM;
        }
    } else if (renderer->road_zoom_direction < 0) {
        if (camera_zoom <= 15.00 && camera_zoom > 13.40) {
            return OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM;
        }
        if (camera_zoom <= 13.40 && camera_zoom > 11.85) {
            return OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM;
        }
        if (camera_zoom <= 11.85 && camera_zoom > 10.55) {
            return OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM;
        }
    }
    return -1;
}

'''
renderer = replace_once(
    renderer,
    "static void draw_roads_legacy(OpenRideORMapRenderer *renderer,\n",
    prewarm_helpers + "static void draw_roads_legacy(OpenRideORMapRenderer *renderer,\n",
    "road prewarm helpers",
)

legacy_pattern = r'''static void draw_roads_legacy\(OpenRideORMapRenderer \*renderer,.*?\n\}\n\nstatic void draw_roads\(OpenRideORMapRenderer \*renderer,.*?\n\}\n\nstatic const char \*label_kind_name'''
new_road_render = r'''static void draw_roads_legacy(OpenRideORMapRenderer *renderer,
                              const OpenRideMapCamera *camera,
                              int width,
                              int height,
                              const OpenRideORMapMetadata *metadata)
{
    const int zoom = android_road_data_zoom(metadata, camera->zoom);
    RoadPaintTable paint_table;
    build_road_paint_table(renderer, camera->zoom, 1.0, &paint_table);
    if (road_paint_table_has_casing(&paint_table)) {
        draw_road_pass(renderer,
                       camera,
                       width,
                       height,
                       zoom,
                       &paint_table,
                       true,
                       false);
    }
    draw_road_pass(renderer,
                   camera,
                   width,
                   height,
                   zoom,
                   &paint_table,
                   false,
                   false);
}

static void draw_roads(OpenRideORMapRenderer *renderer,
                       const OpenRideMapCamera *camera,
                       int width,
                       int height)
{
    if (!renderer) return;
    const uint64_t roads_started = SDL_GetTicksNS();
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    if (!metadata) {
        renderer->road_debug.roads_ms =
            (double)(SDL_GetTicksNS() - roads_started) / 1000000.0;
        return;
    }
    if (metadata->format_version < 6) {
        draw_roads_legacy(renderer, camera, width, height, metadata);
        renderer->road_debug.roads_ms =
            (double)(SDL_GetTicksNS() - roads_started) / 1000000.0;
        return;
    }

    const int prewarm_zoom = road_prewarm_target_zoom(renderer, camera->zoom);
    renderer->road_debug.prewarm_zoom = prewarm_zoom;
    if (prewarm_zoom >= 0) {
        (void)prewarm_road_level(renderer,
                                 camera,
                                 width,
                                 height,
                                 prewarm_zoom,
                                 ORMAP_ROAD_PREWARM_TILE_BUDGET);
    }

    const double regional_to_overview =
        ormap_zoom_smoothstep(camera->zoom, 10.55, 11.25);
    const double overview_to_local =
        ormap_zoom_smoothstep(camera->zoom, 11.85, 12.80);
    const double local_to_detail =
        ormap_zoom_smoothstep(camera->zoom, 13.40, 14.40);

    const int zooms[4] = {
        OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM,
        OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM,
        OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM,
        OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM
    };
    const double fades[4] = {
        1.0 - regional_to_overview,
        regional_to_overview * (1.0 - overview_to_local),
        overview_to_local * (1.0 - local_to_detail),
        local_to_detail
    };
    RoadPaintTable tables[4];
    for (int i = 0; i < 4; ++i) {
        build_road_paint_table(renderer, camera->zoom, fades[i], &tables[i]);
    }

    /* Keep road hierarchy coherent through a LOD handoff. On Android the
     * low/mid-zoom style removes all casings, so avoid traversing every tile
     * for an empty casing pass. */
    for (int pass = 0; pass < 2; ++pass) {
        const bool casing = pass == 0;
        for (int i = 0; i < 4; ++i) {
            if (fades[i] <= 0.001) continue;
            if (casing && !road_paint_table_has_casing(&tables[i])) continue;
            draw_road_pass(renderer,
                           camera,
                           width,
                           height,
                           zooms[i],
                           &tables[i],
                           casing,
                           true);
        }
    }
    renderer->road_debug.roads_ms =
        (double)(SDL_GetTicksNS() - roads_started) / 1000000.0;
}

static const char *label_kind_name'''
renderer = regex_once(renderer, legacy_pattern, new_road_render, "road renderer v6")

renderer = replace_once(
    renderer,
    "void openride_ormap_renderer_begin_frame(OpenRideORMapRenderer *renderer)\n"
    "{\n"
    "    if (renderer) ++renderer->frame_counter;\n"
    "}\n",
    """void openride_ormap_renderer_begin_frame(OpenRideORMapRenderer *renderer)
{
    if (!renderer) return;
    ++renderer->frame_counter;
    memset(&renderer->road_debug, 0, sizeof(renderer->road_debug));
    renderer->road_debug.prewarm_zoom = -1;
    renderer->road_draw_load_budget_remaining = ORMAP_ROAD_DRAW_LOAD_BUDGET;
}

void openride_ormap_renderer_get_road_debug_stats(
    const OpenRideORMapRenderer *renderer,
    OpenRideORMapRoadDebugStats *stats)
{
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
    stats->prewarm_zoom = -1;
    if (renderer) *stats = renderer->road_debug;
}
""",
    "begin frame and getter",
)
renderer_path.write_text(renderer)


main_path = Path("src/main.c")
main = main_path.read_text()

main = replace_once(
    main,
    "            char zoom_line[96];\n"
    "            char gps_line[96];\n",
    """            char zoom_line[96];
            char gps_line[96];
            char road_line[160];
            OpenRideORMapRoadDebugStats road_debug;
            memset(&road_debug, 0, sizeof(road_debug));
            road_debug.prewarm_zoom = -1;
            if (ormap_map && renderer_initialized) {
                openride_ormap_renderer_get_road_debug_stats(&ormap_renderer,
                                                              &road_debug);
            }
""",
    "zoom test road debug declarations",
)

main = replace_once(
    main,
    "            snprintf(gps_line, sizeof(gps_line),\n"
    "                     \"GPS %.6f, %.6f\",\n"
    "                     OPENRIDE_MAP_ZOOM_TEST_LAT,\n"
    "                     OPENRIDE_MAP_ZOOM_TEST_LON);\n"
    "            SDL_FRect badge = {\n"
    "                (float)safe.x + margin,\n"
    "                (float)safe.y + margin,\n"
    "                238.0f * ui_scale,\n"
    "                44.0f * ui_scale\n"
    "            };\n",
    """            snprintf(gps_line, sizeof(gps_line),
                     \"GPS %.6f, %.6f\",
                     OPENRIDE_MAP_ZOOM_TEST_LAT,
                     OPENRIDE_MAP_ZOOM_TEST_LON);
            snprintf(road_line,
                     sizeof(road_line),
                     \"R %.1fms L%.1f H%u M%u P%u D%u X%u z%d\",
                     road_debug.roads_ms,
                     road_debug.load_ms,
                     road_debug.cache_hits,
                     road_debug.cache_misses,
                     road_debug.prewarm_loads,
                     road_debug.draw_loads,
                     road_debug.deferred_loads,
                     road_debug.prewarm_zoom);
            float badge_width = 330.0f * ui_scale;
            const float max_badge_width = (float)safe.w - 2.0f * margin;
            if (badge_width > max_badge_width) badge_width = max_badge_width;
            SDL_FRect badge = {
                (float)safe.x + margin,
                (float)safe.y + margin,
                badge_width,
                62.0f * ui_scale
            };
""",
    "zoom test road debug text",
)

main = replace_once(
    main,
    "            draw_scaled_text(renderer,\n"
    "                             badge.x + 8.0f * ui_scale,\n"
    "                             badge.y + 25.0f * ui_scale,\n"
    "                             text_scale,\n"
    "                             gps_line);\n",
    """            draw_scaled_text(renderer,
                             badge.x + 8.0f * ui_scale,
                             badge.y + 25.0f * ui_scale,
                             text_scale,
                             gps_line);
            draw_scaled_text(renderer,
                             badge.x + 8.0f * ui_scale,
                             badge.y + 43.0f * ui_scale,
                             text_scale,
                             road_line);
""",
    "zoom test road debug draw",
)
main_path.write_text(main)

print("road prewarm + profiling patch applied")
