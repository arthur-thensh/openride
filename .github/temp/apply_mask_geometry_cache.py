#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


# ---------------------------------------------------------------------------
# Runtime structures / profiling
# ---------------------------------------------------------------------------
h_path = ROOT / "src/map/ormap_renderer.h"
h = h_path.read_text()
h = replace_once(
    h,
    "typedef struct OpenRideORMapMaskCacheEntry {\n"
    "    bool occupied;\n"
    "    int zoom;\n"
    "    int x;\n"
    "    int y;\n"
    "    uint64_t last_used;\n"
    "    OpenRideORMapMaskTile tile;\n"
    "} OpenRideORMapMaskCacheEntry;",
    "typedef struct OpenRideMaskRect {\n"
    "    uint8_t x0;\n"
    "    uint8_t y0;\n"
    "    uint8_t x1;\n"
    "    uint8_t y1;\n"
    "} OpenRideMaskRect;\n\n"
    "typedef struct OpenRideORMapMaskCacheEntry {\n"
    "    bool occupied;\n"
    "    bool geometry_compiled;\n"
    "    int zoom;\n"
    "    int x;\n"
    "    int y;\n"
    "    uint64_t last_used;\n"
    "    OpenRideORMapMaskTile tile;\n"
    "    OpenRideMaskRect *builtup_rects;\n"
    "    uint32_t builtup_rect_count;\n"
    "    OpenRideMaskRect *water_rects;\n"
    "    uint32_t water_rect_count;\n"
    "    OpenRideMaskRect *forest_rects;\n"
    "    uint32_t forest_rect_count;\n"
    "} OpenRideORMapMaskCacheEntry;",
    "mask cache entry",
)
h = replace_once(
    h,
    "typedef struct OpenRideORMapAreaDebugStats {\n"
    "    double areas_ms;\n"
    "    double load_ms;\n"
    "    uint32_t tiles_visited;\n"
    "    uint32_t triangles_drawn;\n"
    "    uint32_t batches;\n"
    "    uint32_t prewarm_loads;\n"
    "    uint32_t draw_loads;\n"
    "    uint32_t deferred_loads;\n"
    "} OpenRideORMapAreaDebugStats;",
    "typedef struct OpenRideORMapAreaDebugStats {\n"
    "    double areas_ms;\n"
    "    double load_ms;\n"
    "    double mask_compile_ms;\n"
    "    uint32_t tiles_visited;\n"
    "    uint32_t triangles_drawn;\n"
    "    uint32_t batches;\n"
    "    uint32_t prewarm_loads;\n"
    "    uint32_t draw_loads;\n"
    "    uint32_t deferred_loads;\n"
    "    uint32_t mask_tiles;\n"
    "    uint32_t mask_rects;\n"
    "    uint32_t mask_batches;\n"
    "    uint32_t mask_compile_rects;\n"
    "    uint32_t mask_cache_hits;\n"
    "    uint32_t mask_cache_misses;\n"
    "    uint32_t mask_compile_failures;\n"
    "} OpenRideORMapAreaDebugStats;",
    "mask debug stats",
)
h = replace_once(
    h,
    "    bool road_debug_active;\n    bool area_debug_active;\n",
    "    bool road_debug_active;\n    bool area_debug_active;\n    bool mask_debug_active;\n",
    "mask debug active flag",
)
h_path.write_text(h)


# ---------------------------------------------------------------------------
# ORMap renderer: compile mask bitmaps once into vertically merged rectangles.
# ---------------------------------------------------------------------------
c_path = ROOT / "src/map/ormap_renderer.c"
c = c_path.read_text()

c = replace_once(
    c,
    "    if (renderer->area_debug_active) {\n"
    "        ++renderer->area_debug.batches;\n"
    "    }\n",
    "    if (renderer->area_debug_active) {\n"
    "        ++renderer->area_debug.batches;\n"
    "    }\n"
    "    if (renderer->mask_debug_active) {\n"
    "        ++renderer->area_debug.mask_batches;\n"
    "    }\n",
    "mask batch profiling",
)

insert_before_mask_cache = r'''static bool mask_bit(const unsigned char *bits, uint32_t index)
{
    return bits && (bits[index >> 3U] & (unsigned char)(1U << (index & 7U))) != 0U;
}

static bool mask_rects_compile(const OpenRideORMapMaskTile *tile,
                               const unsigned char *bits,
                               OpenRideMaskRect **rects_out,
                               uint32_t *count_out)
{
    if (!rects_out || !count_out) return false;
    *rects_out = NULL;
    *count_out = 0U;
    if (!tile || !bits || tile->grid_size == 0U) return true;

    const uint32_t grid = tile->grid_size;
    const uint32_t max_runs_per_row = (grid + 1U) / 2U;
    const uint32_t max_rects = grid * max_runs_per_row;
    OpenRideMaskRect *rects = malloc((size_t)max_rects * sizeof(*rects));
    if (!rects) return false;

    uint32_t previous[128];
    uint32_t previous_count = 0U;
    uint32_t rect_count = 0U;

    for (uint32_t y = 0U; y < grid; ++y) {
        uint32_t current[128];
        uint32_t current_count = 0U;
        uint32_t x = 0U;

        while (x < grid) {
            while (x < grid && !mask_bit(bits, y * grid + x)) ++x;
            if (x >= grid) break;
            const uint32_t start = x;
            while (x < grid && mask_bit(bits, y * grid + x)) ++x;
            const uint32_t end = x;

            uint32_t rect_index = UINT32_MAX;
            for (uint32_t p = 0U; p < previous_count; ++p) {
                OpenRideMaskRect *candidate = &rects[previous[p]];
                if ((uint32_t)candidate->x0 == start
                    && (uint32_t)candidate->x1 == end
                    && (uint32_t)candidate->y1 == y) {
                    rect_index = previous[p];
                    candidate->y1 = (uint8_t)(y + 1U);
                    break;
                }
            }

            if (rect_index == UINT32_MAX) {
                if (rect_count >= max_rects) {
                    free(rects);
                    return false;
                }
                rect_index = rect_count++;
                rects[rect_index] = (OpenRideMaskRect){
                    .x0 = (uint8_t)start,
                    .y0 = (uint8_t)y,
                    .x1 = (uint8_t)end,
                    .y1 = (uint8_t)(y + 1U)
                };
            }
            current[current_count++] = rect_index;
        }

        memcpy(previous, current, (size_t)current_count * sizeof(*current));
        previous_count = current_count;
    }

    if (rect_count == 0U) {
        free(rects);
        return true;
    }

    OpenRideMaskRect *shrunk = realloc(rects, (size_t)rect_count * sizeof(*rects));
    if (shrunk) rects = shrunk;
    *rects_out = rects;
    *count_out = rect_count;
    return true;
}

static void mask_cache_entry_destroy(OpenRideORMapMaskCacheEntry *entry)
{
    if (!entry) return;
    if (entry->occupied) openride_ormap_mask_tile_destroy(&entry->tile);
    free(entry->builtup_rects);
    free(entry->water_rects);
    free(entry->forest_rects);
    memset(entry, 0, sizeof(*entry));
}

static bool mask_cache_compile_geometry(OpenRideORMapMaskCacheEntry *entry)
{
    if (!entry) return false;

    OpenRideMaskRect *builtup = NULL;
    OpenRideMaskRect *water = NULL;
    OpenRideMaskRect *forest = NULL;
    uint32_t builtup_count = 0U;
    uint32_t water_count = 0U;
    uint32_t forest_count = 0U;

    if (!mask_rects_compile(&entry->tile, entry->tile.builtup,
                            &builtup, &builtup_count)
        || !mask_rects_compile(&entry->tile, entry->tile.water,
                               &water, &water_count)
        || !mask_rects_compile(&entry->tile, entry->tile.forest,
                               &forest, &forest_count)) {
        free(builtup);
        free(water);
        free(forest);
        return false;
    }

    entry->builtup_rects = builtup;
    entry->builtup_rect_count = builtup_count;
    entry->water_rects = water;
    entry->water_rect_count = water_count;
    entry->forest_rects = forest;
    entry->forest_rect_count = forest_count;
    entry->geometry_compiled = true;
    return true;
}

'''
c = replace_once(
    c,
    "static bool mask_cache_contains(const OpenRideORMapRenderer *renderer,\n",
    insert_before_mask_cache + "static bool mask_cache_contains(const OpenRideORMapRenderer *renderer,\n",
    "mask compile helpers",
)

old_slot = r'''static OpenRideORMapMaskCacheEntry *mask_cache_slot(OpenRideORMapRenderer *renderer,
                                                     int zoom,
                                                     int x,
                                                     int y,
                                                     bool prewarm,
                                                     bool budgeted_draw_load)
{
    const size_t base = tile_cache_set_base(OPENRIDE_ORMAP_MASK_CACHE_CAPACITY,
                                            zoom, x, y);
    OpenRideORMapMaskCacheEntry *victim = NULL;
    OpenRideORMapMaskCacheEntry *oldest = &renderer->masks[base];
    for (size_t i = 0U; i < ORMAP_CACHE_ASSOCIATIVITY; ++i) {
        OpenRideORMapMaskCacheEntry *entry = &renderer->masks[base + i];
        if (entry->occupied && entry->zoom == zoom && entry->x == x && entry->y == y) {
            entry->last_used = renderer->frame_counter;
            return entry;
        }
        if (!entry->occupied && !victim) victim = entry;
        if (entry->occupied && oldest->occupied && entry->last_used < oldest->last_used) {
            oldest = entry;
        }
    }

    if (budgeted_draw_load && !prewarm) {
        if (renderer->area_draw_load_budget_remaining == 0U) {
            ++renderer->area_debug.deferred_loads;
            return NULL;
        }
        --renderer->area_draw_load_budget_remaining;
    }

    if (!victim) {
        victim = oldest;
        if (prewarm && victim->occupied
            && victim->last_used + 1U >= renderer->frame_counter) {
            return NULL;
        }
    }
    if (victim->occupied) openride_ormap_mask_tile_destroy(&victim->tile);
    memset(victim, 0, sizeof(*victim));
    victim->occupied = true;
    victim->zoom = zoom;
    victim->x = x;
    victim->y = y;
    victim->last_used = renderer->frame_counter;
    const uint64_t load_started = SDL_GetTicksNS();
    char error[160] = {0};
    if (!openride_ormap_load_mask_tile(renderer->map,
                                       zoom,
                                       x,
                                       y,
                                       &victim->tile,
                                       error,
                                       sizeof(error))) {
        /* Missing mask tile is expected in rural areas. */
    }
    if (renderer->area_debug_active) {
        renderer->area_debug.load_ms +=
            (double)(SDL_GetTicksNS() - load_started) / 1000000.0;
        if (prewarm) ++renderer->area_debug.prewarm_loads;
        else ++renderer->area_debug.draw_loads;
    }
    return victim;
}'''
new_slot = r'''static OpenRideORMapMaskCacheEntry *mask_cache_slot(OpenRideORMapRenderer *renderer,
                                                     int zoom,
                                                     int x,
                                                     int y,
                                                     bool prewarm,
                                                     bool budgeted_draw_load)
{
    const size_t base = tile_cache_set_base(OPENRIDE_ORMAP_MASK_CACHE_CAPACITY,
                                            zoom, x, y);
    OpenRideORMapMaskCacheEntry *victim = NULL;
    OpenRideORMapMaskCacheEntry *oldest = &renderer->masks[base];
    for (size_t i = 0U; i < ORMAP_CACHE_ASSOCIATIVITY; ++i) {
        OpenRideORMapMaskCacheEntry *entry = &renderer->masks[base + i];
        if (entry->occupied && entry->zoom == zoom && entry->x == x && entry->y == y) {
            entry->last_used = renderer->frame_counter;
            if (renderer->area_debug_active) ++renderer->area_debug.mask_cache_hits;
            return entry;
        }
        if (!entry->occupied && !victim) victim = entry;
        if (entry->occupied && oldest->occupied && entry->last_used < oldest->last_used) {
            oldest = entry;
        }
    }

    if (renderer->area_debug_active) ++renderer->area_debug.mask_cache_misses;
    if (budgeted_draw_load && !prewarm) {
        if (renderer->area_draw_load_budget_remaining == 0U) {
            ++renderer->area_debug.deferred_loads;
            return NULL;
        }
        --renderer->area_draw_load_budget_remaining;
    }

    if (!victim) {
        victim = oldest;
        if (prewarm && victim->occupied
            && victim->last_used + 1U >= renderer->frame_counter) {
            return NULL;
        }
    }
    mask_cache_entry_destroy(victim);
    victim->occupied = true;
    victim->zoom = zoom;
    victim->x = x;
    victim->y = y;
    victim->last_used = renderer->frame_counter;

    const uint64_t load_started = SDL_GetTicksNS();
    char error[160] = {0};
    (void)openride_ormap_load_mask_tile(renderer->map,
                                        zoom,
                                        x,
                                        y,
                                        &victim->tile,
                                        error,
                                        sizeof(error));
    const uint64_t load_finished = SDL_GetTicksNS();

    const uint64_t compile_started = load_finished;
    const bool compiled = mask_cache_compile_geometry(victim);
    const uint64_t compile_finished = SDL_GetTicksNS();

    if (renderer->area_debug_active) {
        renderer->area_debug.load_ms +=
            (double)(load_finished - load_started) / 1000000.0;
        renderer->area_debug.mask_compile_ms +=
            (double)(compile_finished - compile_started) / 1000000.0;
        if (compiled) {
            renderer->area_debug.mask_compile_rects +=
                victim->builtup_rect_count
                + victim->water_rect_count
                + victim->forest_rect_count;
        } else {
            ++renderer->area_debug.mask_compile_failures;
        }
        if (prewarm) ++renderer->area_debug.prewarm_loads;
        else ++renderer->area_debug.draw_loads;
    }
    return victim;
}'''
c = replace_once(c, old_slot, new_slot, "mask cache slot")

# The helper above now owns mask_bit(). Remove the old definition beside the draw path.
c = replace_once(
    c,
    "static bool mask_bit(const unsigned char *bits, uint32_t index)\n"
    "{\n"
    "    return bits && (bits[index >> 3U] & (unsigned char)(1U << (index & 7U))) != 0U;\n"
    "}\n\n"
    "static OpenRideMapColor forest_color",
    "static OpenRideMapColor forest_color",
    "old mask_bit definition",
)

# Count fallback rectangles too; this makes allocation-failure fallback visible in profiling.
c = replace_once(
    c,
    "            if (!geometry_batch_rotated_rect(renderer,\n"
    "                                             batch,\n"
    "                                             camera,",
    "            ++renderer->area_debug.mask_rects;\n"
    "            if (!geometry_batch_rotated_rect(renderer,\n"
    "                                             batch,\n"
    "                                             camera,",
    "fallback mask rect counter",
)

cached_draw_helpers = r'''
static bool draw_compiled_mask_layer(OpenRideORMapRenderer *renderer,
                                     GeometryBatch *batch,
                                     const OpenRideMapCamera *camera,
                                     int width,
                                     int height,
                                     double tile_size,
                                     double tile_left,
                                     double tile_top,
                                     const OpenRideORMapMaskTile *tile,
                                     const OpenRideMaskRect *rects,
                                     uint32_t rect_count,
                                     OpenRideMapColor color)
{
    if (!tile || tile->grid_size == 0U || rect_count == 0U) return true;
    const double cell = tile_size / (double)tile->grid_size;
    for (uint32_t i = 0U; i < rect_count; ++i) {
        const OpenRideMaskRect *rect = &rects[i];
        ++renderer->area_debug.mask_rects;
        if (!geometry_batch_rotated_rect(renderer,
                                         batch,
                                         camera,
                                         width,
                                         height,
                                         (float)(tile_left + rect->x0 * cell),
                                         (float)(tile_top + rect->y0 * cell),
                                         (float)(tile_left + rect->x1 * cell + 0.5),
                                         (float)(tile_top + rect->y1 * cell + 0.5),
                                         color)) {
            return false;
        }
    }
    return true;
}

static bool draw_cached_mask_layer(OpenRideORMapRenderer *renderer,
                                   GeometryBatch *batch,
                                   const OpenRideMapCamera *camera,
                                   int width,
                                   int height,
                                   double tile_size,
                                   double tile_left,
                                   double tile_top,
                                   const OpenRideORMapMaskCacheEntry *entry,
                                   const unsigned char *bits,
                                   const OpenRideMaskRect *rects,
                                   uint32_t rect_count,
                                   OpenRideMapColor color)
{
    if (!entry) return true;
    if (entry->geometry_compiled) {
        return draw_compiled_mask_layer(renderer, batch, camera, width, height,
                                        tile_size, tile_left, tile_top,
                                        &entry->tile, rects, rect_count, color);
    }
    return draw_mask_layer(renderer, batch, camera, width, height,
                           tile_size, tile_left, tile_top,
                           &entry->tile, bits, color);
}

'''
c = replace_once(
    c,
    "static void draw_masks(OpenRideORMapRenderer *renderer,\n",
    cached_draw_helpers + "static void draw_masks(OpenRideORMapRenderer *renderer,\n",
    "compiled mask draw helpers",
)

c = replace_once(
    c,
    "    const bool previous_area_debug_active = renderer->area_debug_active;\n"
    "    renderer->area_debug_active = true;\n\n"
    "    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);",
    "    const bool previous_area_debug_active = renderer->area_debug_active;\n"
    "    const bool previous_mask_debug_active = renderer->mask_debug_active;\n"
    "    renderer->area_debug_active = true;\n"
    "    renderer->mask_debug_active = true;\n\n"
    "    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(renderer->map);",
    "draw_masks debug activation",
)

c = replace_once(
    c,
    "        renderer->area_debug_active = previous_area_debug_active;\n"
    "        return;\n"
    "    }\n\n"
    "    const int count = 1 << zoom;",
    "        renderer->mask_debug_active = previous_mask_debug_active;\n"
    "        renderer->area_debug_active = previous_area_debug_active;\n"
    "        return;\n"
    "    }\n\n"
    "    const int count = 1 << zoom;",
    "draw_masks early restore",
)

c = replace_once(
    c,
    "            OpenRideORMapMaskCacheEntry *entry = mask_cache_slot(renderer, zoom, qx, ty, false, true);\n"
    "            if (!entry) continue;\n"
    "            const double left = width * 0.5 + tx * tile_size - center_x;",
    "            OpenRideORMapMaskCacheEntry *entry = mask_cache_slot(renderer, zoom, qx, ty, false, true);\n"
    "            if (!entry) continue;\n"
    "            ++renderer->area_debug.mask_tiles;\n"
    "            const double left = width * 0.5 + tx * tile_size - center_x;",
    "mask tile counter",
)

# Replace the three layer calls with cached geometry calls.
c = c.replace(
    "draw_mask_layer(renderer, &batch, camera, width, height,\n"
    "                                     tile_size, left, top, &entry->tile,\n"
    "                                     entry->tile.forest, forest)",
    "draw_cached_mask_layer(renderer, &batch, camera, width, height,\n"
    "                                            tile_size, left, top, entry,\n"
    "                                            entry->tile.forest,\n"
    "                                            entry->forest_rects,\n"
    "                                            entry->forest_rect_count, forest)",
)
c = c.replace(
    "draw_mask_layer(renderer, &batch, camera, width, height,\n"
    "                                     tile_size, left, top, &entry->tile,\n"
    "                                     entry->tile.builtup, builtup)",
    "draw_cached_mask_layer(renderer, &batch, camera, width, height,\n"
    "                                            tile_size, left, top, entry,\n"
    "                                            entry->tile.builtup,\n"
    "                                            entry->builtup_rects,\n"
    "                                            entry->builtup_rect_count, builtup)",
)
c = c.replace(
    "draw_mask_layer(renderer, &batch, camera, width, height,\n"
    "                                        tile_size, left, top, &entry->tile,\n"
    "                                        entry->tile.builtup, builtup)",
    "draw_cached_mask_layer(renderer, &batch, camera, width, height,\n"
    "                                               tile_size, left, top, entry,\n"
    "                                               entry->tile.builtup,\n"
    "                                               entry->builtup_rects,\n"
    "                                               entry->builtup_rect_count, builtup)",
)
c = c.replace(
    "draw_mask_layer(renderer, &batch, camera, width, height,\n"
    "                                        tile_size, left, top, &entry->tile,\n"
    "                                        entry->tile.water, water)",
    "draw_cached_mask_layer(renderer, &batch, camera, width, height,\n"
    "                                               tile_size, left, top, entry,\n"
    "                                               entry->tile.water,\n"
    "                                               entry->water_rects,\n"
    "                                               entry->water_rect_count, water)",
)
if c.count("draw_mask_layer(renderer, &batch, camera, width, height,") != 0:
    raise SystemExit("mask draw replacement: stale direct draw_mask_layer calls remain")

c = replace_once(
    c,
    "masks_done:\n"
    "    renderer->area_debug.areas_ms +=\n"
    "        (double)(SDL_GetTicksNS() - masks_started) / 1000000.0;\n"
    "    renderer->area_debug_active = previous_area_debug_active;\n",
    "masks_done:\n"
    "    renderer->area_debug.areas_ms +=\n"
    "        (double)(SDL_GetTicksNS() - masks_started) / 1000000.0;\n"
    "    renderer->mask_debug_active = previous_mask_debug_active;\n"
    "    renderer->area_debug_active = previous_area_debug_active;\n",
    "draw_masks final restore",
)

c = replace_once(
    c,
    "    for (size_t i = 0U; i < OPENRIDE_ORMAP_MASK_CACHE_CAPACITY; ++i) {\n"
    "        if (renderer->masks[i].occupied) openride_ormap_mask_tile_destroy(&renderer->masks[i].tile);\n"
    "    }",
    "    for (size_t i = 0U; i < OPENRIDE_ORMAP_MASK_CACHE_CAPACITY; ++i) {\n"
    "        mask_cache_entry_destroy(&renderer->masks[i]);\n"
    "    }",
    "mask renderer destroy",
)

c = replace_once(
    c,
    "    renderer->road_debug_active = false;\n"
    "    renderer->area_debug_active = false;\n"
    "    renderer->area_detail_ready = false;",
    "    renderer->road_debug_active = false;\n"
    "    renderer->area_debug_active = false;\n"
    "    renderer->mask_debug_active = false;\n"
    "    renderer->area_detail_ready = false;",
    "mask begin frame reset",
)

c_path.write_text(c)


# ---------------------------------------------------------------------------
# MapWorld aggregation: retain mask profiling from the renderers actually used.
# ---------------------------------------------------------------------------
w_path = ROOT / "src/map/map_world.c"
w = w_path.read_text()
w = replace_once(
    w,
    "    dst->areas_ms += src->areas_ms;\n"
    "    dst->load_ms += src->load_ms;\n"
    "    dst->tiles_visited += src->tiles_visited;\n"
    "    dst->triangles_drawn += src->triangles_drawn;\n"
    "    dst->batches += src->batches;\n"
    "    dst->prewarm_loads += src->prewarm_loads;\n"
    "    dst->draw_loads += src->draw_loads;\n"
    "    dst->deferred_loads += src->deferred_loads;",
    "    dst->areas_ms += src->areas_ms;\n"
    "    dst->load_ms += src->load_ms;\n"
    "    dst->mask_compile_ms += src->mask_compile_ms;\n"
    "    dst->tiles_visited += src->tiles_visited;\n"
    "    dst->triangles_drawn += src->triangles_drawn;\n"
    "    dst->batches += src->batches;\n"
    "    dst->prewarm_loads += src->prewarm_loads;\n"
    "    dst->draw_loads += src->draw_loads;\n"
    "    dst->deferred_loads += src->deferred_loads;\n"
    "    dst->mask_tiles += src->mask_tiles;\n"
    "    dst->mask_rects += src->mask_rects;\n"
    "    dst->mask_batches += src->mask_batches;\n"
    "    dst->mask_compile_rects += src->mask_compile_rects;\n"
    "    dst->mask_cache_hits += src->mask_cache_hits;\n"
    "    dst->mask_cache_misses += src->mask_cache_misses;\n"
    "    dst->mask_compile_failures += src->mask_compile_failures;",
    "map world mask stats aggregation",
)
w_path.write_text(w)


# ---------------------------------------------------------------------------
# Zoom benchmark schema v3: expose mask cache/geometry counters.
# ---------------------------------------------------------------------------
l_path = ROOT / "src/map/map_zoom_test_logger.c"
l = l_path.read_text()
l = replace_once(l, "# format_version=2", "# format_version=3", "benchmark format version")
l = replace_once(
    l,
    "max_area_ms=0.0,max_area_load_ms=0.0;",
    "max_area_ms=0.0,max_area_load_ms=0.0,max_mask_compile_ms=0.0;",
    "benchmark max mask compile declaration",
)
l = replace_once(
    l,
    "if(s->area.load_ms>max_area_load_ms)max_area_load_ms=s->area.load_ms;}",
    "if(s->area.load_ms>max_area_load_ms)max_area_load_ms=s->area.load_ms;if(s->area.mask_compile_ms>max_mask_compile_ms)max_mask_compile_ms=s->area.mask_compile_ms;}",
    "benchmark max mask compile accumulation",
)
l = replace_once(
    l,
    "# area_load_ms_max=%.3f\\n\",max_update_ms,max_map_ms,max_world_ms,max_masks_ms,max_areas_layer_ms,max_waterways_ms,max_roads_layer_ms,max_labels_ms,max_ui_ms,max_present_ms,max_unaccounted_ms,max_road_ms,max_road_load_ms,max_area_ms,max_area_load_ms);",
    "# area_load_ms_max=%.3f\\n# mask_compile_ms_max=%.3f\\n\",max_update_ms,max_map_ms,max_world_ms,max_masks_ms,max_areas_layer_ms,max_waterways_ms,max_roads_layer_ms,max_labels_ms,max_ui_ms,max_present_ms,max_unaccounted_ms,max_road_ms,max_road_load_ms,max_area_ms,max_area_load_ms,max_mask_compile_ms);",
    "benchmark mask compile max header",
)
l = replace_once(
    l,
    "area_ms,area_load_ms,area_tiles,area_triangles,area_batches,area_prewarm_loads,area_draw_loads,area_deferred_loads\\n",
    "area_ms,area_load_ms,area_tiles,area_triangles,area_batches,area_prewarm_loads,area_draw_loads,area_deferred_loads,mask_tiles,mask_rects,mask_batches,mask_compile_ms,mask_compile_rects,mask_cache_hits,mask_cache_misses,mask_compile_failures\\n",
    "benchmark mask columns",
)
l = replace_once(
    l,
    "%.3f,%.3f,%u,%u,%u,%u,%u,%u\\n\",i,s->elapsed_ms",
    "%.3f,%.3f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.3f,%u,%u,%u,%u\\n\",i,s->elapsed_ms",
    "benchmark mask row format",
)
l = replace_once(
    l,
    "(unsigned)s->area.deferred_loads);}",
    "(unsigned)s->area.deferred_loads,(unsigned)s->area.mask_tiles,(unsigned)s->area.mask_rects,(unsigned)s->area.mask_batches,s->area.mask_compile_ms,(unsigned)s->area.mask_compile_rects,(unsigned)s->area.mask_cache_hits,(unsigned)s->area.mask_cache_misses,(unsigned)s->area.mask_compile_failures);}",
    "benchmark mask row values",
)
l_path.write_text(l)

print("Mask geometry cache patch applied.")
