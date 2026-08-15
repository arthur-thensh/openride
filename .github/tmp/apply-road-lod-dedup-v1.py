from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)


# --- renderer header -------------------------------------------------------
path = Path("src/map/ormap_renderer.h")
text = path.read_text()
text = replace_once(
    text,
    """typedef struct OpenRideORMapRoadDebugStats {\n    double roads_ms;\n    double load_ms;\n    uint32_t cache_hits;\n    uint32_t cache_misses;\n    uint32_t prewarm_loads;\n    uint32_t draw_loads;\n    uint32_t deferred_loads;\n    int prewarm_zoom;\n} OpenRideORMapRoadDebugStats;\n""",
    """typedef struct OpenRideORMapRoadDebugStats {\n    double roads_ms;\n    double load_ms;\n    uint32_t cache_hits;\n    uint32_t cache_misses;\n    uint32_t prewarm_loads;\n    uint32_t draw_loads;\n    uint32_t deferred_loads;\n    uint32_t tiles_visited;\n    uint32_t segments_drawn;\n    uint32_t batches;\n    int prewarm_zoom;\n} OpenRideORMapRoadDebugStats;\n""",
    "road debug stats fields",
)
text = replace_once(
    text,
    """    OpenRideORMapRoadDebugStats road_debug;\n    uint32_t road_draw_load_budget_remaining;\n""",
    """    OpenRideORMapRoadDebugStats road_debug;\n    uint32_t road_draw_load_budget_remaining;\n    bool road_debug_active;\n""",
    "road debug active field",
)
path.write_text(text)


# --- renderer implementation ----------------------------------------------
path = Path("src/map/ormap_renderer.c")
text = path.read_text()
text = replace_once(
    text,
    """    SDL_RenderGeometry(renderer->renderer,\n""",
    """    if (renderer->road_debug_active) {\n        ++renderer->road_debug.batches;\n    }\n    SDL_RenderGeometry(renderer->renderer,\n""",
    "count road geometry batches",
)

marker = "static void draw_road_pass(OpenRideORMapRenderer *renderer,"
insert = r'''typedef enum RoadLODClassGroup {
    ROAD_LOD_GROUP_MAJOR = 0,
    ROAD_LOD_GROUP_PRIMARY,
    ROAD_LOD_GROUP_LOCAL,
    ROAD_LOD_GROUP_DETAIL
} RoadLODClassGroup;

static RoadLODClassGroup road_lod_class_group(int road_class)
{
    switch ((OpenRideRoadClass)road_class) {
        case OPENRIDE_ROAD_MOTORWAY:
        case OPENRIDE_ROAD_TRUNK:
            return ROAD_LOD_GROUP_MAJOR;
        case OPENRIDE_ROAD_PRIMARY:
            return ROAD_LOD_GROUP_PRIMARY;
        case OPENRIDE_ROAD_SECONDARY:
        case OPENRIDE_ROAD_TERTIARY:
            return ROAD_LOD_GROUP_LOCAL;
        default:
            return ROAD_LOD_GROUP_DETAIL;
    }
}

static int road_lod_group_source(RoadLODClassGroup group, double zoom)
{
    switch (group) {
        case ROAD_LOD_GROUP_MAJOR:
            if (zoom < 11.25) return 0;
            if (zoom < 12.80) return 1;
            if (zoom < 14.40) return 2;
            return 3;
        case ROAD_LOD_GROUP_PRIMARY:
            if (zoom < 12.80) return 1;
            if (zoom < 14.40) return 2;
            return 3;
        case ROAD_LOD_GROUP_LOCAL:
            return zoom < 14.40 ? 2 : 3;
        case ROAD_LOD_GROUP_DETAIL:
        default:
            return 3;
    }
}

static double road_lod_group_fade(RoadLODClassGroup group,
                                  double regional_to_overview,
                                  double overview_to_local,
                                  double local_to_detail)
{
    switch (group) {
        case ROAD_LOD_GROUP_MAJOR:
            return 1.0;
        case ROAD_LOD_GROUP_PRIMARY:
            return regional_to_overview;
        case ROAD_LOD_GROUP_LOCAL:
            return overview_to_local;
        case ROAD_LOD_GROUP_DETAIL:
        default:
            return local_to_detail;
    }
}

static void road_paint_table_configure_lod(RoadPaintTable *table,
                                           int lod_index,
                                           double zoom,
                                           double regional_to_overview,
                                           double overview_to_local,
                                           double local_to_detail)
{
    if (!table) return;
    for (int road_class = OPENRIDE_ROAD_UNKNOWN;
         road_class <= OPENRIDE_ROAD_OTHER;
         ++road_class) {
        if (!table->visible[road_class]) continue;
        const RoadLODClassGroup group = road_lod_class_group(road_class);
        if (road_lod_group_source(group, zoom) != lod_index) {
            table->visible[road_class] = false;
            continue;
        }
        const double fade = road_lod_group_fade(group,
                                                regional_to_overview,
                                                overview_to_local,
                                                local_to_detail);
        if (fade <= 0.001) {
            table->visible[road_class] = false;
            continue;
        }
        ormap_scale_color_alpha(&table->paints[road_class].line, fade);
        ormap_scale_color_alpha(&table->paints[road_class].casing, fade);
    }
}

'''
if text.count(marker) != 1:
    raise SystemExit(f"draw_road_pass marker: expected one match, got {text.count(marker)}")
text = text.replace(marker, insert + marker, 1)

start = text.index(marker)
end = text.index("static int android_road_data_zoom", start)
func = text[start:end]
func = replace_once(
    func,
    """{\n    const int count = 1 << zoom;\n""",
    """{\n    const bool previous_road_debug_active = renderer->road_debug_active;\n    renderer->road_debug_active = true;\n    const int count = 1 << zoom;\n""",
    "activate road batch debug",
)
func = replace_once(
    func,
    """            const int qx = wrap_x(tx, count);\n            OpenRideORMapRoadCacheEntry *entry =\n""",
    """            const int qx = wrap_x(tx, count);\n            ++renderer->road_debug.tiles_visited;\n            OpenRideORMapRoadCacheEntry *entry =\n""",
    "count road tile visits",
)
count_ok = func.count("                        bool ok = true;\n")
if count_ok != 2:
    raise SystemExit(f"segment draw sites: expected two matches, got {count_ok}")
func = func.replace(
    "                        bool ok = true;\n",
    "                        ++renderer->road_debug.segments_drawn;\n                        bool ok = true;\n",
)
count_return = func.count("                            geometry_batch_flush(renderer, &batch);\n                            return;\n")
if count_return != 2:
    raise SystemExit(f"road early returns: expected two matches, got {count_return}")
func = func.replace(
    "                            geometry_batch_flush(renderer, &batch);\n                            return;\n",
    "                            geometry_batch_flush(renderer, &batch);\n                            renderer->road_debug_active = previous_road_debug_active;\n                            return;\n",
)
end_block = """    geometry_batch_flush(renderer, &batch);\n}\n\n"""
if not func.endswith(end_block):
    raise SystemExit("draw_road_pass final flush block not found")
func = func[:-len(end_block)] + """    geometry_batch_flush(renderer, &batch);\n    renderer->road_debug_active = previous_road_debug_active;\n}\n\n"""
text = text[:start] + func + text[end:]

old_lod_block = r'''    const int zooms[4] = {
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
'''
new_lod_block = r'''    const int zooms[4] = {
        OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM,
        OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM,
        OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM,
        OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM
    };
    RoadPaintTable tables[4];
    bool active[4] = {false, false, false, false};
    for (int i = 0; i < 4; ++i) {
        build_road_paint_table(renderer, camera->zoom, 1.0, &tables[i]);
        road_paint_table_configure_lod(&tables[i],
                                       i,
                                       camera->zoom,
                                       regional_to_overview,
                                       overview_to_local,
                                       local_to_detail);
        for (int road_class = OPENRIDE_ROAD_UNKNOWN;
             road_class <= OPENRIDE_ROAD_OTHER;
             ++road_class) {
            if (tables[i].visible[road_class]) {
                active[i] = true;
                break;
            }
        }
    }

    /*
     * Each road class has exactly one geometry owner at any camera zoom.
     * A handoff therefore draws the stable/common hierarchy once and only
     * fades in classes newly introduced by the next semantic LOD. The source
     * for shared classes changes only after that handoff is complete; road
     * LOD V1 does no geometric simplification, so this avoids duplicate
     * rasterization without introducing a visible opacity dip.
     */
    for (int pass = 0; pass < 2; ++pass) {
        const bool casing = pass == 0;
        for (int i = 0; i < 4; ++i) {
            if (!active[i]) continue;
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
'''
text = replace_once(text, old_lod_block, new_lod_block, "replace duplicated LOD rendering")
text = replace_once(
    text,
    """    renderer->road_debug.prewarm_zoom = -1;\n    renderer->road_draw_load_budget_remaining = ORMAP_ROAD_DRAW_LOAD_BUDGET;\n""",
    """    renderer->road_debug.prewarm_zoom = -1;\n    renderer->road_draw_load_budget_remaining = ORMAP_ROAD_DRAW_LOAD_BUDGET;\n    renderer->road_debug_active = false;\n""",
    "reset road debug batch flag",
)
path.write_text(text)


# --- deterministic DEV HUD -------------------------------------------------
path = Path("src/main.c")
text = path.read_text()
text = replace_once(
    text,
    """            char gps_line[96];\n            char road_line[160];\n""",
    """            char gps_line[96];\n            char road_line[160];\n            char road_work_line[96];\n""",
    "HUD work line declaration",
)
text = replace_once(
    text,
    """                     road_debug.deferred_loads,\n                     road_debug.prewarm_zoom);\n            float badge_width = 330.0f * ui_scale;\n""",
    """                     road_debug.deferred_loads,\n                     road_debug.prewarm_zoom);\n            snprintf(road_work_line,\n                     sizeof(road_work_line),\n                     \"T%u S%u B%u\",\n                     road_debug.tiles_visited,\n                     road_debug.segments_drawn,\n                     road_debug.batches);\n            float badge_width = 330.0f * ui_scale;\n""",
    "HUD work line format",
)
text = replace_once(
    text,
    """                badge_width,\n                62.0f * ui_scale\n""",
    """                badge_width,\n                80.0f * ui_scale\n""",
    "HUD badge height",
)
text = replace_once(
    text,
    """            draw_scaled_text(renderer,\n                             badge.x + 8.0f * ui_scale,\n                             badge.y + 43.0f * ui_scale,\n                             text_scale,\n                             road_line);\n""",
    """            draw_scaled_text(renderer,\n                             badge.x + 8.0f * ui_scale,\n                             badge.y + 43.0f * ui_scale,\n                             text_scale,\n                             road_line);\n            draw_scaled_text(renderer,\n                             badge.x + 8.0f * ui_scale,\n                             badge.y + 61.0f * ui_scale,\n                             text_scale,\n                             road_work_line);\n""",
    "HUD draw work line",
)
path.write_text(text)

print("road LOD dedup V1 applied")
