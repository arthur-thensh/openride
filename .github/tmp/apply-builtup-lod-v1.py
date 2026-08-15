from pathlib import Path


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, got {count}")
    return text.replace(old, new, 1)

# Header: area/built-up debug counters.
h = Path("src/map/ormap_renderer.h")
text = h.read_text()
road_stats = '''typedef struct OpenRideORMapRoadDebugStats {
    double roads_ms;
    double load_ms;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t prewarm_loads;
    uint32_t draw_loads;
    uint32_t deferred_loads;
    uint32_t tiles_visited;
    uint32_t segments_drawn;
    uint32_t batches;
    int prewarm_zoom;
} OpenRideORMapRoadDebugStats;
'''
area_stats = road_stats + '''\ntypedef struct OpenRideORMapAreaDebugStats {
    double areas_ms;
    double load_ms;
    uint32_t tiles_visited;
    uint32_t triangles_drawn;
    uint32_t batches;
    uint32_t mask_prewarm_loads;
} OpenRideORMapAreaDebugStats;
'''
text = replace_once(text, road_stats, area_stats, "area debug struct")
text = replace_once(
    text,
    '''    OpenRideORMapRoadDebugStats road_debug;\n    uint32_t road_draw_load_budget_remaining;\n    bool road_debug_active;\n''',
    '''    OpenRideORMapRoadDebugStats road_debug;\n    OpenRideORMapAreaDebugStats area_debug;\n    uint32_t road_draw_load_budget_remaining;\n    bool road_debug_active;\n    bool area_debug_active;\n''',
    "renderer area state",
)
text = replace_once(
    text,
    '''void openride_ormap_renderer_get_road_debug_stats(\n    const OpenRideORMapRenderer *renderer,\n    OpenRideORMapRoadDebugStats *stats);\n''',
    '''void openride_ormap_renderer_get_road_debug_stats(\n    const OpenRideORMapRenderer *renderer,\n    OpenRideORMapRoadDebugStats *stats);\nvoid openride_ormap_renderer_get_area_debug_stats(\n    const OpenRideORMapRenderer *renderer,\n    OpenRideORMapAreaDebugStats *stats);\n''',
    "area debug getter declaration",
)
h.write_text(text)

# Renderer implementation.
p = Path("src/map/ormap_renderer.c")
text = p.read_text()
text = replace_once(
    text,
    '''#define ORMAP_ROAD_PREWARM_TILE_BUDGET 3U\n#define ORMAP_ROAD_DRAW_LOAD_BUDGET 2U\n\nstatic OpenRideORMapRoadDebugStats g_last_road_debug_stats;\nstatic const OpenRideORMapRenderer *g_last_road_debug_renderer = NULL;\n''',
    '''#define ORMAP_ROAD_PREWARM_TILE_BUDGET 3U\n#define ORMAP_ROAD_DRAW_LOAD_BUDGET 2U\n#define ORMAP_BUILTUP_MASK_PREWARM_TILE_BUDGET 6U\n#define ORMAP_BUILTUP_PREWARM_START 12.20\n#define ORMAP_BUILTUP_OVERVIEW_START 13.15\n#define ORMAP_BUILTUP_OVERVIEW_FULL 13.35\n#define ORMAP_BUILTUP_DETAIL_START 14.00\n#define ORMAP_BUILTUP_DETAIL_END 14.55\n\nstatic OpenRideORMapRoadDebugStats g_last_road_debug_stats;\nstatic const OpenRideORMapRenderer *g_last_road_debug_renderer = NULL;\nstatic OpenRideORMapAreaDebugStats g_last_area_debug_stats;\nstatic const OpenRideORMapRenderer *g_last_area_debug_renderer = NULL;\n''',
    "builtup constants and globals",
)
text = replace_once(
    text,
    '''    if (renderer->road_debug_active) {\n        ++renderer->road_debug.batches;\n    }\n    SDL_RenderGeometry(renderer->renderer,\n''',
    '''    if (renderer->road_debug_active) {\n        ++renderer->road_debug.batches;\n    }\n    if (renderer->area_debug_active) {\n        ++renderer->area_debug.batches;\n    }\n    SDL_RenderGeometry(renderer->renderer,\n''',
    "surface batch counter",
)

mask_marker = '''static OpenRideORMapMaskCacheEntry *mask_cache_slot(OpenRideORMapRenderer *renderer,\n                                                     int zoom,\n                                                     int x,\n                                                     int y)\n{\n'''
mask_helpers = '''static bool mask_cache_contains(const OpenRideORMapRenderer *renderer,\n                                int zoom,\n                                int x,\n                                int y)\n{\n    if (!renderer) return false;\n    const size_t base = tile_cache_set_base(OPENRIDE_ORMAP_MASK_CACHE_CAPACITY,\n                                            zoom, x, y);\n    for (size_t i = 0U; i < ORMAP_CACHE_ASSOCIATIVITY; ++i) {\n        const OpenRideORMapMaskCacheEntry *entry = &renderer->masks[base + i];\n        if (entry->occupied && entry->zoom == zoom && entry->x == x && entry->y == y) {\n            return true;\n        }\n    }\n    return false;\n}\n\n''' + mask_marker
text = replace_once(text, mask_marker, mask_helpers, "mask cache contains")

old_mask_load = '''    char error[160] = {0};\n    if (!openride_ormap_load_mask_tile(renderer->map,\n                                       zoom,\n                                       x,\n                                       y,\n                                       &victim->tile,\n                                       error,\n                                       sizeof(error))) {\n        /* Missing mask tile is expected in rural areas. */\n    }\n    return victim;\n}\n\nstatic OpenRideORMapWaterCacheEntry *water_cache_slot'''
new_mask_load = '''    const uint64_t load_started = SDL_GetTicksNS();\n    char error[160] = {0};\n    if (!openride_ormap_load_mask_tile(renderer->map,\n                                       zoom,\n                                       x,\n                                       y,\n                                       &victim->tile,\n                                       error,\n                                       sizeof(error))) {\n        /* Missing mask tile is expected in rural areas. */\n    }\n    if (renderer->area_debug_active) {\n        renderer->area_debug.load_ms +=\n            (double)(SDL_GetTicksNS() - load_started) / 1000000.0;\n    }\n    return victim;\n}\n\nstatic uint32_t prewarm_mask_level(OpenRideORMapRenderer *renderer,\n                                   const OpenRideMapCamera *camera,\n                                   int width,\n                                   int height,\n                                   int zoom,\n                                   double viewport_zoom,\n                                   uint32_t budget)\n{\n    if (!renderer || !camera || budget == 0U) return 0U;\n    const int count = 1 << zoom;\n    const double scale = pow(2.0, viewport_zoom - zoom);\n    const double tile_size = ORMAP_TILE_SIZE * scale;\n    const OpenRidePointD center =\n        openride_mercator_forward(camera->center_lat, camera->center_lon);\n    const double world_size = tile_size * count;\n    const double center_x = center.x * world_size;\n    const double center_y = center.y * world_size;\n    const double bearing =\n        camera->bearing_deg * 3.14159265358979323846 / 180.0;\n    const double half_w = fabs(cos(bearing)) * width * 0.5\n        + fabs(sin(bearing)) * height * 0.5;\n    const double half_h = fabs(sin(bearing)) * width * 0.5\n        + fabs(cos(bearing)) * height * 0.5;\n    const int first_x = (int)floor((center_x - half_w) / tile_size);\n    const int last_x = (int)floor((center_x + half_w) / tile_size);\n    const int first_y = (int)floor((center_y - half_h) / tile_size);\n    const int last_y = (int)floor((center_y + half_h) / tile_size);\n\n    uint32_t loaded = 0U;\n    for (int ty = first_y; ty <= last_y; ++ty) {\n        if (ty < 0 || ty >= count) continue;\n        for (int tx = first_x; tx <= last_x; ++tx) {\n            const int qx = wrap_x(tx, count);\n            if (mask_cache_contains(renderer, zoom, qx, ty)) continue;\n            if (!mask_cache_slot(renderer, zoom, qx, ty)) continue;\n            ++renderer->area_debug.mask_prewarm_loads;\n            if (++loaded >= budget) return loaded;\n        }\n    }\n    return loaded;\n}\n\nstatic OpenRideORMapWaterCacheEntry *water_cache_slot'''
text = replace_once(text, old_mask_load, new_mask_load, "mask load timing and prewarm")

old_area_load = '''    char error[160] = {0};\n    if (!openride_ormap_load_area_tile(renderer->map,\n                                       zoom,\n                                       x,\n                                       y,\n                                       &victim->tile,\n                                       error,\n                                       sizeof(error))) {\n        /* Missing vector area tiles are common over rural/empty regions. */\n    }\n    return victim;\n}\n\nstatic bool geometry_batch_rotated_rect'''
new_area_load = '''    const uint64_t load_started = SDL_GetTicksNS();\n    char error[160] = {0};\n    if (!openride_ormap_load_area_tile(renderer->map,\n                                       zoom,\n                                       x,\n                                       y,\n                                       &victim->tile,\n                                       error,\n                                       sizeof(error))) {\n        /* Missing vector area tiles are common over rural/empty regions. */\n    }\n    if (renderer->area_debug_active) {\n        renderer->area_debug.load_ms +=\n            (double)(SDL_GetTicksNS() - load_started) / 1000000.0;\n    }\n    return victim;\n}\n\nstatic bool geometry_batch_rotated_rect'''
text = replace_once(text, old_area_load, new_area_load, "area load timing")

# Replace mask drawing with v6 built-up overview + progressive prewarm.
start = text.index("static void draw_masks(")
end = text.index("static double decode_area_coord", start)
new_draw_masks = r'''static void draw_masks(OpenRideORMapRenderer *renderer,
                       const OpenRideMapCamera *camera,
                       int width,
                       int height)
{
    const uint64_t masks_started = SDL_GetTicksNS();
    const bool previous_area_debug_active = renderer->area_debug_active;
    renderer->area_debug_active = true;

    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    const bool v4 = metadata && metadata->format_version >= 4;
    const bool v6 = metadata && metadata->format_version >= 6;
    const double forest_start = v4 ? 13.40 : 14.0;
    const double forest_end = v4 ? 14.40 : 14.50;
    const int zoom = metadata ? metadata->mask_zoom : OPENRIDE_ORMAP_MASK_ZOOM;

    if (v6
        && camera->zoom >= ORMAP_BUILTUP_PREWARM_START
        && camera->zoom < ORMAP_BUILTUP_OVERVIEW_FULL) {
        (void)prewarm_mask_level(renderer,
                                 camera,
                                 width,
                                 height,
                                 zoom,
                                 ORMAP_BUILTUP_OVERVIEW_START,
                                 ORMAP_BUILTUP_MASK_PREWARM_TILE_BUDGET);
    }

    const double builtup_overview_fade = v6
        ? ormap_zoom_smoothstep(camera->zoom,
                                ORMAP_BUILTUP_OVERVIEW_START,
                                ORMAP_BUILTUP_OVERVIEW_FULL)
            * (1.0 - ormap_zoom_smoothstep(camera->zoom,
                                           ORMAP_BUILTUP_DETAIL_START,
                                           ORMAP_BUILTUP_DETAIL_END))
        : 0.0;
    const bool draw_forest =
        camera->zoom >= forest_start && camera->zoom <= 17.2;
    const bool draw_builtup_overview = builtup_overview_fade > 0.001;
    const bool draw_legacy_masks =
        (!metadata || metadata->format_version < 3) && draw_forest;
    if (!draw_forest && !draw_builtup_overview && !draw_legacy_masks) {
        renderer->area_debug.areas_ms +=
            (double)(SDL_GetTicksNS() - masks_started) / 1000000.0;
        renderer->area_debug_active = previous_area_debug_active;
        return;
    }

    const int count = 1 << zoom;
    const double scale = pow(2.0, camera->zoom - zoom);
    const double tile_size = ORMAP_TILE_SIZE * scale;
    const OpenRidePointD center = openride_mercator_forward(camera->center_lat,
                                                             camera->center_lon);
    const double world_size = tile_size * count;
    const double center_x = center.x * world_size;
    const double center_y = center.y * world_size;
    const double bearing = camera->bearing_deg * 3.14159265358979323846 / 180.0;
    const double half_w = fabs(cos(bearing)) * width * 0.5 + fabs(sin(bearing)) * height * 0.5;
    const double half_h = fabs(sin(bearing)) * width * 0.5 + fabs(cos(bearing)) * height * 0.5;
    const int first_x = (int)floor((center_x - half_w) / tile_size);
    const int last_x = (int)floor((center_x + half_w) / tile_size);
    const int first_y = (int)floor((center_y - half_h) / tile_size);
    const int last_y = (int)floor((center_y + half_h) / tile_size);
    const OpenRideMapPalette palette = openride_map_palette(renderer->style);
    OpenRideMapColor builtup = palette.building;
    builtup.a = renderer->style == OPENRIDE_MAP_STYLE_TRAIL ? 92 : 125;
    OpenRideMapColor water = palette.water;
    water.a = 210;
    OpenRideMapColor forest = forest_color(renderer->style);

    const double forest_fade =
        ormap_zoom_smoothstep(camera->zoom, forest_start, forest_end);
    ormap_scale_color_alpha(&forest, forest_fade);
    ormap_scale_color_alpha(&builtup,
                            v6 ? builtup_overview_fade : forest_fade);
    ormap_scale_color_alpha(&water, forest_fade);

    GeometryBatch batch = {0};
    for (int ty = first_y; ty <= last_y; ++ty) {
        if (ty < 0 || ty >= count) continue;
        for (int tx = first_x; tx <= last_x; ++tx) {
            const int qx = wrap_x(tx, count);
            ++renderer->area_debug.tiles_visited;
            OpenRideORMapMaskCacheEntry *entry = mask_cache_slot(renderer, zoom, qx, ty);
            if (!entry) continue;
            const double left = width * 0.5 + tx * tile_size - center_x;
            const double top = height * 0.5 + ty * tile_size - center_y;

            if (draw_forest && entry->tile.forest && forest.a > 0U) {
                if (!draw_mask_layer(renderer, &batch, camera, width, height,
                                     tile_size, left, top, &entry->tile,
                                     entry->tile.forest, forest)) {
                    geometry_batch_flush(renderer, &batch);
                    goto masks_done;
                }
            }

            if (draw_builtup_overview && entry->tile.builtup && builtup.a > 0U) {
                if (!draw_mask_layer(renderer, &batch, camera, width, height,
                                     tile_size, left, top, &entry->tile,
                                     entry->tile.builtup, builtup)) {
                    geometry_batch_flush(renderer, &batch);
                    goto masks_done;
                }
            }

            /* v1/v2 stored filled water/built-up areas only as semantic cells. */
            if (draw_legacy_masks) {
                if (entry->tile.builtup && builtup.a > 0U
                    && !draw_mask_layer(renderer, &batch, camera, width, height,
                                        tile_size, left, top, &entry->tile,
                                        entry->tile.builtup, builtup)) {
                    geometry_batch_flush(renderer, &batch);
                    goto masks_done;
                }
                if (entry->tile.water && water.a > 0U
                    && !draw_mask_layer(renderer, &batch, camera, width, height,
                                        tile_size, left, top, &entry->tile,
                                        entry->tile.water, water)) {
                    geometry_batch_flush(renderer, &batch);
                    goto masks_done;
                }
            }
        }
    }
    geometry_batch_flush(renderer, &batch);

masks_done:
    renderer->area_debug.areas_ms +=
        (double)(SDL_GetTicksNS() - masks_started) / 1000000.0;
    renderer->area_debug_active = previous_area_debug_active;
}

'''
text = text[:start] + new_draw_masks + text[end:]

# Area detail instrumentation and v6 built-up detail handoff.
text = replace_once(
    text,
    '''    const double builtup_fade =\n        ormap_zoom_smoothstep(camera->zoom, 13.0, 13.45) * level_fade;\n''',
    '''    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);\n    const bool v6 = metadata && metadata->format_version >= 6;\n    const double builtup_fade =\n        ormap_zoom_smoothstep(camera->zoom,\n                              v6 ? ORMAP_BUILTUP_DETAIL_START : 13.0,\n                              v6 ? ORMAP_BUILTUP_DETAIL_END : 13.45)\n        * level_fade;\n''',
    "detail builtup fade",
)
text = replace_once(
    text,
    '''        for (int tx = first_x; tx <= last_x; ++tx) {\n            const int qx = wrap_x(tx, count);\n            OpenRideORMapAreaCacheEntry *entry = area_cache_slot(renderer, zoom, qx, ty);\n''',
    '''        for (int tx = first_x; tx <= last_x; ++tx) {\n            const int qx = wrap_x(tx, count);\n            ++renderer->area_debug.tiles_visited;\n            OpenRideORMapAreaCacheEntry *entry = area_cache_slot(renderer, zoom, qx, ty);\n''',
    "area tile counter",
)
text = replace_once(
    text,
    '''                const OpenRideMapColor color = triangle->kind == OPENRIDE_ORMAP_AREA_WATER\n                    ? water_color : builtup_color;\n                if (!geometry_batch_triangle(renderer,\n''',
    '''                const OpenRideMapColor color = triangle->kind == OPENRIDE_ORMAP_AREA_WATER\n                    ? water_color : builtup_color;\n                if (color.a == 0U) continue;\n                ++renderer->area_debug.triangles_drawn;\n                if (!geometry_batch_triangle(renderer,\n''',
    "area triangle counter",
)

start = text.index("static void draw_areas(")
end = text.index("static int waterway_width", start)
new_draw_areas = r'''static void draw_areas(OpenRideORMapRenderer *renderer,
                       const OpenRideMapCamera *camera,
                       int width,
                       int height)
{
    const uint64_t areas_started = SDL_GetTicksNS();
    const bool previous_area_debug_active = renderer->area_debug_active;
    renderer->area_debug_active = true;

    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);
    if (metadata && metadata->format_version >= 3 && camera->zoom >= 10.0) {
        const double detail_mix =
            ormap_zoom_smoothstep(camera->zoom, 12.15, 12.85);
        const bool v6 = metadata->format_version >= 6;
        const bool draw_detail_builtup =
            camera->zoom >= (v6 ? ORMAP_BUILTUP_DETAIL_START : 13.0);

        if (detail_mix < 1.0) {
            draw_area_level(renderer,
                            camera,
                            width,
                            height,
                            metadata->area_coarse_zoom,
                            false,
                            true,
                            1.0 - detail_mix);
        }
        if (detail_mix > 0.0) {
            draw_area_level(renderer,
                            camera,
                            width,
                            height,
                            metadata->area_detail_zoom,
                            draw_detail_builtup,
                            true,
                            detail_mix);
        }
    }

    renderer->area_debug.areas_ms +=
        (double)(SDL_GetTicksNS() - areas_started) / 1000000.0;
    renderer->area_debug_active = previous_area_debug_active;
    g_last_area_debug_stats = renderer->area_debug;
    g_last_area_debug_renderer = renderer;
}

'''
text = text[:start] + new_draw_areas + text[end:]

# Reset and getter.
text = replace_once(
    text,
    '''    memset(&renderer->road_debug, 0, sizeof(renderer->road_debug));\n    renderer->road_debug.prewarm_zoom = -1;\n    renderer->road_draw_load_budget_remaining = ORMAP_ROAD_DRAW_LOAD_BUDGET;\n''',
    '''    memset(&renderer->road_debug, 0, sizeof(renderer->road_debug));\n    memset(&renderer->area_debug, 0, sizeof(renderer->area_debug));\n    renderer->road_debug.prewarm_zoom = -1;\n    renderer->road_draw_load_budget_remaining = ORMAP_ROAD_DRAW_LOAD_BUDGET;\n''',
    "area debug reset",
)
road_getter_end = '''    } else if (renderer) {\n        *stats = renderer->road_debug;\n    }\n}\n\nvoid openride_ormap_renderer_draw_layer'''
area_getter = '''    } else if (renderer) {\n        *stats = renderer->road_debug;\n    }\n}\n\nvoid openride_ormap_renderer_get_area_debug_stats(\n    const OpenRideORMapRenderer *renderer,\n    OpenRideORMapAreaDebugStats *stats)\n{\n    if (!stats) return;\n    memset(stats, 0, sizeof(*stats));\n    if (renderer && renderer == g_last_area_debug_renderer) {\n        *stats = renderer->area_debug;\n    } else if (g_last_area_debug_renderer) {\n        *stats = g_last_area_debug_stats;\n    } else if (renderer) {\n        *stats = renderer->area_debug;\n    }\n}\n\nvoid openride_ormap_renderer_draw_layer'''
text = replace_once(text, road_getter_end, area_getter, "area debug getter")
p.write_text(text)

# DEV HUD: add surface metrics line.
m = Path("src/main.c")
text = m.read_text()
text = replace_once(
    text,
    '''            char road_line[160];\n            char road_work_line[96];\n            OpenRideORMapRoadDebugStats road_debug;\n            memset(&road_debug, 0, sizeof(road_debug));\n            road_debug.prewarm_zoom = -1;\n            if (ormap_map && renderer_initialized) {\n                openride_ormap_renderer_get_road_debug_stats(&ormap_renderer,\n                                                              &road_debug);\n            }\n''',
    '''            char road_line[160];\n            char road_work_line[96];\n            char area_line[160];\n            OpenRideORMapRoadDebugStats road_debug;\n            OpenRideORMapAreaDebugStats area_debug;\n            memset(&road_debug, 0, sizeof(road_debug));\n            memset(&area_debug, 0, sizeof(area_debug));\n            road_debug.prewarm_zoom = -1;\n            if (ormap_map && renderer_initialized) {\n                openride_ormap_renderer_get_road_debug_stats(&ormap_renderer,\n                                                              &road_debug);\n                openride_ormap_renderer_get_area_debug_stats(&ormap_renderer,\n                                                              &area_debug);\n            }\n''',
    "HUD area debug state",
)
text = replace_once(
    text,
    '''            snprintf(road_work_line,\n                     sizeof(road_work_line),\n                     "T%u S%u B%u",\n                     road_debug.tiles_visited,\n                     road_debug.segments_drawn,\n                     road_debug.batches);\n            float badge_width = 330.0f * ui_scale;\n''',
    '''            snprintf(road_work_line,\n                     sizeof(road_work_line),\n                     "T%u S%u B%u",\n                     road_debug.tiles_visited,\n                     road_debug.segments_drawn,\n                     road_debug.batches);\n            snprintf(area_line,\n                     sizeof(area_line),\n                     "A %.1fms AL%.1f AT%u AG%u AB%u AP%u",\n                     area_debug.areas_ms,\n                     area_debug.load_ms,\n                     area_debug.tiles_visited,\n                     area_debug.triangles_drawn,\n                     area_debug.batches,\n                     area_debug.mask_prewarm_loads);\n            float badge_width = 330.0f * ui_scale;\n''',
    "HUD area line format",
)
text = replace_once(
    text,
    '''                badge_width,\n                80.0f * ui_scale\n''',
    '''                badge_width,\n                98.0f * ui_scale\n''',
    "HUD badge height",
)
text = replace_once(
    text,
    '''            draw_scaled_text(renderer,\n                             badge.x + 8.0f * ui_scale,\n                             badge.y + 61.0f * ui_scale,\n                             text_scale,\n                             road_work_line);\n''',
    '''            draw_scaled_text(renderer,\n                             badge.x + 8.0f * ui_scale,\n                             badge.y + 61.0f * ui_scale,\n                             text_scale,\n                             road_work_line);\n            draw_scaled_text(renderer,\n                             badge.x + 8.0f * ui_scale,\n                             badge.y + 79.0f * ui_scale,\n                             text_scale,\n                             area_line);\n''',
    "HUD area line draw",
)
m.write_text(text)
