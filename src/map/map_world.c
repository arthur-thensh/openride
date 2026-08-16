#include "map/map_world.h"
#include "map/ormap_renderer.h"

#include "openride/ormap.h"
#include "openride/place_search.h"
#include "openride/region_manager.h"
#include "openride/france_lite.h"
#include "openride/france_regions_lite.h"
#include "openride/routing_graph.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORLD_ROAD_ZOOM 10
#define WORLD_MAJOR_ROAD_CLASS_COUNT 3
#define WORLD_MAX_LINE_WIDTH 3
#define WORLD_REGION_REFERENCE_LABEL_TARGET 3U
#define WORLD_MAJOR_CITY_LABEL_MAX 96U
#define WORLD_FRANCE_BASE_LABEL_MAX 48U
#define WORLD_FRANCE_BASE_CANDIDATE_MAX 128U
#define WORLD_LINE_CHUNK_SIZE 64U
#define WORLD_FRANCE_NETWORK_COORD_MAX 65535U

#include "france_overview_network_data.inc"
/*
 * MapWorld is an orientation overview, not the navigation cartography.
 * Snap its major-road geometry to a ~131k WebMercator lattice and collapse
 * duplicate cell-to-cell edges. At z10.75 one lattice step is ~3.4 px;
 * at z6-z9 it is sub-pixel to ~1 px, while removing huge numbers of tiny
 * OSM/routing-graph segments and parallel carriageway duplicates.
 */
#define WORLD_OVERVIEW_ROAD_GRID 131072U
#define WORLD_GEOMETRY_BATCH_VERTEX_LIMIT 8192U
#define WORLD_GEOMETRY_BATCH_INDEX_LIMIT 12288U

typedef struct WorldLine {
    double x1;
    double y1;
    double x2;
    double y2;
    uint8_t kind;
} WorldLine;

typedef struct WorldQuantizedEdge {
    uint32_t ax;
    uint32_t ay;
    uint32_t bx;
    uint32_t by;
    uint8_t kind;
} WorldQuantizedEdge;

typedef struct WorldLineChunk {
    size_t first;
    size_t count;
    double min_x;
    double min_y;
    double max_x;
    double max_y;
} WorldLineChunk;

typedef struct WorldLineArray {
    WorldLine *items;
    size_t count;
    size_t capacity;
    WorldLineChunk *chunks;
    size_t chunk_count;
} WorldLineArray;

typedef struct WorldGeometryBatch {
    uint32_t vertex_count;
    uint32_t index_count;
} WorldGeometryBatch;

typedef struct WorldCityLabelBox {
    float left;
    float top;
    float right;
    float bottom;
} WorldCityLabelBox;

typedef struct WorldFranceLabelCandidate {
    const OpenRideFranceLitePlace *place;
    OpenRidePointD point;
} WorldFranceLabelCandidate;

typedef struct OpenRideMapWorldRegion {
    const OpenRideRegionDefinition *definition;
    OpenRideORMapMetadata metadata;
    WorldLineArray boundary;
    WorldLineArray roads[WORLD_MAJOR_ROAD_CLASS_COUNT];
    WorldLineArray waterways;
    OpenRideORMap *map;
    OpenRideORMapRenderer *detail_renderer;
    bool detail_visible;
} OpenRideMapWorldRegion;

struct OpenRideMapWorld {
    SDL_Renderer *renderer;
    OpenRideMapWorldRegion *regions;
    size_t region_count;
    WorldLineArray france_boundaries;
    WorldLineArray france_coastline;
    WorldLineArray france_roads[WORLD_MAJOR_ROAD_CLASS_COUNT];
    bool france_base_ready;
    SDL_Vertex *vertices;
    int *indices;
    uint32_t vertex_capacity;
    uint32_t index_capacity;
    bool debug_enabled;
    OpenRideMapWorldDebugStats debug;
};

static void map_world_accumulate_road_debug(
    OpenRideORMapRoadDebugStats *dst,
    const OpenRideORMapRoadDebugStats *src)
{
    if (!dst || !src) return;
    dst->roads_ms += src->roads_ms;
    dst->load_ms += src->load_ms;
    dst->cache_hits += src->cache_hits;
    dst->cache_misses += src->cache_misses;
    dst->prewarm_loads += src->prewarm_loads;
    dst->draw_loads += src->draw_loads;
    dst->deferred_loads += src->deferred_loads;
    dst->tiles_visited += src->tiles_visited;
    dst->segments_drawn += src->segments_drawn;
    dst->batches += src->batches;
    if (src->prewarm_zoom >= 0) {
        if (dst->prewarm_zoom < 0) dst->prewarm_zoom = src->prewarm_zoom;
        else if (dst->prewarm_zoom != src->prewarm_zoom) dst->prewarm_zoom = -2;
    }
}

static void map_world_accumulate_area_debug(
    OpenRideORMapAreaDebugStats *dst,
    const OpenRideORMapAreaDebugStats *src)
{
    if (!dst || !src) return;
    dst->areas_ms += src->areas_ms;
    dst->load_ms += src->load_ms;
    dst->mask_compile_ms += src->mask_compile_ms;
    dst->tiles_visited += src->tiles_visited;
    dst->triangles_drawn += src->triangles_drawn;
    dst->batches += src->batches;
    dst->prewarm_loads += src->prewarm_loads;
    dst->draw_loads += src->draw_loads;
    dst->deferred_loads += src->deferred_loads;
    dst->mask_tiles += src->mask_tiles;
    dst->mask_rects += src->mask_rects;
    dst->mask_batches += src->mask_batches;
    dst->mask_compile_rects += src->mask_compile_rects;
    dst->mask_cache_hits += src->mask_cache_hits;
    dst->mask_cache_misses += src->mask_cache_misses;
    dst->mask_compile_failures += src->mask_compile_failures;
}

void openride_map_world_debug_begin_frame(OpenRideMapWorld *world)
{
    if (!world) return;
    memset(&world->debug, 0, sizeof(world->debug));
    world->debug.road.prewarm_zoom = -1;
    world->debug_enabled = true;
}

void openride_map_world_debug_end_frame(OpenRideMapWorld *world)
{
    if (!world) return;
    world->debug_enabled = false;
}

void openride_map_world_get_debug_stats(
    const OpenRideMapWorld *world,
    OpenRideMapWorldDebugStats *stats)
{
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
    stats->road.prewarm_zoom = -1;
    if (world) *stats = world->debug;
}

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "unknown error");
}

static void line_array_destroy(WorldLineArray *array)
{
    if (!array) return;
    free(array->items);
    free(array->chunks);
    memset(array, 0, sizeof(*array));
}

static bool line_array_build_chunks(WorldLineArray *array)
{
    if (!array) return false;

    free(array->chunks);
    array->chunks = NULL;
    array->chunk_count = 0U;

    if (array->count == 0U) return true;

    const size_t chunk_count =
        (array->count + WORLD_LINE_CHUNK_SIZE - 1U)
        / WORLD_LINE_CHUNK_SIZE;
    WorldLineChunk *chunks =
        calloc(chunk_count, sizeof(*chunks));
    if (!chunks) return false;

    for (size_t c = 0U; c < chunk_count; ++c) {
        WorldLineChunk *chunk = &chunks[c];
        chunk->first = c * WORLD_LINE_CHUNK_SIZE;
        const size_t remaining = array->count - chunk->first;
        chunk->count =
            remaining < WORLD_LINE_CHUNK_SIZE
                ? remaining
                : WORLD_LINE_CHUNK_SIZE;

        const WorldLine *first = &array->items[chunk->first];
        chunk->min_x = first->x1 < first->x2 ? first->x1 : first->x2;
        chunk->max_x = first->x1 > first->x2 ? first->x1 : first->x2;
        chunk->min_y = first->y1 < first->y2 ? first->y1 : first->y2;
        chunk->max_y = first->y1 > first->y2 ? first->y1 : first->y2;

        for (size_t i = 1U; i < chunk->count; ++i) {
            const WorldLine *line =
                &array->items[chunk->first + i];
            const double xs[2] = {line->x1, line->x2};
            const double ys[2] = {line->y1, line->y2};

            for (int endpoint = 0; endpoint < 2; ++endpoint) {
                if (xs[endpoint] < chunk->min_x) {
                    chunk->min_x = xs[endpoint];
                }
                if (xs[endpoint] > chunk->max_x) {
                    chunk->max_x = xs[endpoint];
                }
                if (ys[endpoint] < chunk->min_y) {
                    chunk->min_y = ys[endpoint];
                }
                if (ys[endpoint] > chunk->max_y) {
                    chunk->max_y = ys[endpoint];
                }
            }
        }
    }

    array->chunks = chunks;
    array->chunk_count = chunk_count;
    return true;
}

static bool line_array_append(WorldLineArray *array,
                              double lat1,
                              double lon1,
                              double lat2,
                              double lon2,
                              uint8_t kind)
{
    if (!array) return false;
    if (array->count == array->capacity) {
        size_t next = array->capacity == 0U ? 512U : array->capacity * 2U;
        if (next < array->capacity) return false;
        WorldLine *grown = realloc(array->items, next * sizeof(*grown));
        if (!grown) return false;
        array->items = grown;
        array->capacity = next;
    }
    const OpenRidePointD a = openride_mercator_forward(lat1, lon1);
    const OpenRidePointD b = openride_mercator_forward(lat2, lon2);
    WorldLine *line = &array->items[array->count++];
    line->x1 = a.x;
    line->y1 = a.y;
    line->x2 = b.x;
    line->y2 = b.y;
    line->kind = kind;
    return true;
}

static void tile_point_geo(int zoom,
                           int tile_x,
                           int tile_y,
                           double local_x,
                           double local_y,
                           double *lat,
                           double *lon)
{
    const double scale = (double)(UINT32_C(1) << zoom);
    OpenRidePointD point = {
        ((double)tile_x + local_x) / scale,
        ((double)tile_y + local_y) / scale
    };
    openride_mercator_inverse(point, lat, lon);
}

static char *trim_poly_line(char *line)
{
    if (!line) return NULL;
    while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') ++line;
    char *end = line + strlen(line);
    while (end > line && (end[-1] == ' ' || end[-1] == '\t'
                          || end[-1] == '\r' || end[-1] == '\n')) {
        --end;
    }
    *end = '\0';
    return line;
}

static bool load_poly_boundary(const char *path,
                               WorldLineArray *boundary,
                               char *error,
                               size_t error_size)
{
    if (!path || !boundary || path[0] == '\0') return true;
    FILE *file = fopen(path, "rb");
    if (!file) {
        /* The overview boundary is optional for older installed regions. */
        return true;
    }
    char line[512];
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return true;
    }
    bool in_ring = false;
    bool have_first = false;
    double first_lon = 0.0;
    double first_lat = 0.0;
    double prev_lon = 0.0;
    double prev_lat = 0.0;
    while (fgets(line, sizeof(line), file)) {
        char *text = trim_poly_line(line);
        if (!text || text[0] == '\0') continue;
        if (strcmp(text, "END") == 0) {
            if (!in_ring) break;
            if (have_first
                && (fabs(prev_lon - first_lon) > 1e-12
                    || fabs(prev_lat - first_lat) > 1e-12)) {
                if (!line_array_append(boundary,
                                       prev_lat, prev_lon,
                                       first_lat, first_lon,
                                       0U)) {
                    fclose(file);
                    set_error(error, error_size,
                              "out of memory storing map-world poly boundary");
                    return false;
                }
            }
            in_ring = false;
            have_first = false;
            continue;
        }
        if (!in_ring) {
            /* Ring identifiers may start with ! for holes. Draw both outlines. */
            in_ring = true;
            have_first = false;
            continue;
        }
        char *end_lon = NULL;
        const double lon = strtod(text, &end_lon);
        if (end_lon == text) continue;
        char *end_lat = NULL;
        const double lat = strtod(end_lon, &end_lat);
        if (end_lat == end_lon) continue;
        if (!have_first) {
            first_lon = lon;
            first_lat = lat;
            prev_lon = lon;
            prev_lat = lat;
            have_first = true;
            continue;
        }
        if (!line_array_append(boundary,
                               prev_lat, prev_lon,
                               lat, lon,
                               0U)) {
            fclose(file);
            set_error(error, error_size,
                      "out of memory storing map-world poly boundary");
            return false;
        }
        prev_lon = lon;
        prev_lat = lat;
    }
    fclose(file);
    return true;
}

static bool load_major_roads(OpenRideORMap *map,
                             OpenRideMapWorldRegion *region,
                             char *error,
                             size_t error_size)
{
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(map);
    if (!metadata) return false;
    int zoom = WORLD_ROAD_ZOOM;
    if (zoom < metadata->min_zoom) zoom = metadata->min_zoom;
    if (zoom > metadata->road_max_zoom) zoom = metadata->road_max_zoom;
    OpenRideORMapTileCoord *coords = NULL;
    uint32_t coord_count = 0U;
    if (!openride_ormap_list_tiles(map,
                                   OPENRIDE_ORMAP_TILE_LAYER_ROAD,
                                   zoom,
                                   &coords,
                                   &coord_count,
                                   error,
                                   error_size)) {
        return false;
    }
    for (uint32_t c = 0U; c < coord_count; ++c) {
        OpenRideORMapRoadTile tile = {0};
        char tile_error[160] = {0};
        if (!openride_ormap_load_road_tile(map,
                                           zoom,
                                           coords[c].x,
                                           coords[c].y,
                                           &tile,
                                           tile_error,
                                           sizeof(tile_error))) {
            continue;
        }
        for (uint32_t r = 0U; r < tile.count; ++r) {
            const OpenRideORMapRoadRecord *record = &tile.records[r];
            if (record->road_class < OPENRIDE_ROAD_MOTORWAY
                || record->road_class > OPENRIDE_ROAD_PRIMARY) {
                continue;
            }
            double lat1 = 0.0, lon1 = 0.0, lat2 = 0.0, lon2 = 0.0;
            tile_point_geo(zoom, coords[c].x, coords[c].y,
                           (double)record->x1 / 65535.0,
                           (double)record->y1 / 65535.0,
                           &lat1, &lon1);
            tile_point_geo(zoom, coords[c].x, coords[c].y,
                           (double)record->x2 / 65535.0,
                           (double)record->y2 / 65535.0,
                           &lat2, &lon2);
            const int road_index =
                (int)record->road_class - (int)OPENRIDE_ROAD_MOTORWAY;
            if (road_index < 0
                || road_index >= WORLD_MAJOR_ROAD_CLASS_COUNT) {
                continue;
            }
            if (!line_array_append(&region->roads[road_index],
                                   lat1, lon1, lat2, lon2, record->road_class)) {
                openride_ormap_road_tile_destroy(&tile);
                openride_ormap_tile_coords_destroy(coords);
                set_error(error, error_size, "out of memory storing map-world roads");
                return false;
            }
        }
        openride_ormap_road_tile_destroy(&tile);
    }
    openride_ormap_tile_coords_destroy(coords);
    return true;
}

static bool load_major_waterways(OpenRideORMap *map,
                                 OpenRideMapWorldRegion *region,
                                 char *error,
                                 size_t error_size)
{
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(map);
    if (!metadata || metadata->format_version < 2) return true;
    const int zoom = metadata->water_zoom;
    OpenRideORMapTileCoord *coords = NULL;
    uint32_t coord_count = 0U;
    if (!openride_ormap_list_tiles(map,
                                   OPENRIDE_ORMAP_TILE_LAYER_WATER,
                                   zoom,
                                   &coords,
                                   &coord_count,
                                   error,
                                   error_size)) {
        return false;
    }
    for (uint32_t c = 0U; c < coord_count; ++c) {
        OpenRideORMapWaterTile tile = {0};
        char tile_error[160] = {0};
        if (!openride_ormap_load_water_tile(map,
                                            zoom,
                                            coords[c].x,
                                            coords[c].y,
                                            &tile,
                                            tile_error,
                                            sizeof(tile_error))) {
            continue;
        }
        for (uint32_t r = 0U; r < tile.count; ++r) {
            const OpenRideORMapWaterRecord *record = &tile.records[r];
            if (record->kind != OPENRIDE_ORMAP_WATERWAY_RIVER
                && record->kind != OPENRIDE_ORMAP_WATERWAY_CANAL) {
                continue;
            }
            double lat1 = 0.0, lon1 = 0.0, lat2 = 0.0, lon2 = 0.0;
            tile_point_geo(zoom, coords[c].x, coords[c].y,
                           (double)record->x1 / 65535.0,
                           (double)record->y1 / 65535.0,
                           &lat1, &lon1);
            tile_point_geo(zoom, coords[c].x, coords[c].y,
                           (double)record->x2 / 65535.0,
                           (double)record->y2 / 65535.0,
                           &lat2, &lon2);
            if (!line_array_append(&region->waterways,
                                   lat1, lon1, lat2, lon2, record->kind)) {
                openride_ormap_water_tile_destroy(&tile);
                openride_ormap_tile_coords_destroy(coords);
                set_error(error, error_size, "out of memory storing map-world waterways");
                return false;
            }
        }
        openride_ormap_water_tile_destroy(&tile);
    }
    openride_ormap_tile_coords_destroy(coords);
    return true;
}

static void world_region_destroy(OpenRideMapWorldRegion *region)
{
    if (!region) return;
    if (region->detail_renderer) {
        openride_ormap_renderer_destroy(region->detail_renderer);
        free(region->detail_renderer);
    }
    if (region->map) {
        openride_ormap_close(region->map);
    }
    line_array_destroy(&region->boundary);
    for (int i = 0; i < WORLD_MAJOR_ROAD_CLASS_COUNT; ++i) {
        line_array_destroy(&region->roads[i]);
    }
    line_array_destroy(&region->waterways);
    memset(region, 0, sizeof(*region));
}

static void destroy_regions(OpenRideMapWorldRegion *regions, size_t count)
{
    if (!regions) return;
    for (size_t i = 0U; i < count; ++i) world_region_destroy(&regions[i]);
    free(regions);
}

static bool line_array_append_world(WorldLineArray *array,
                                    double x1,
                                    double y1,
                                    double x2,
                                    double y2,
                                    uint8_t kind)
{
    if (!array) return false;
    if (array->count == array->capacity) {
        size_t next =
            array->capacity == 0U ? 512U : array->capacity * 2U;
        if (next < array->capacity) return false;
        WorldLine *grown =
            realloc(array->items, next * sizeof(*grown));
        if (!grown) return false;
        array->items = grown;
        array->capacity = next;
    }

    WorldLine *line = &array->items[array->count++];
    line->x1 = x1;
    line->y1 = y1;
    line->x2 = x2;
    line->y2 = y2;
    line->kind = kind;
    return true;
}

static uint32_t world_quantize_coordinate(double value, uint32_t grid)
{
    if (value <= 0.0) return 0U;
    if (value >= 1.0) return grid;
    return (uint32_t)llround(value * (double)grid);
}

static bool world_quantized_point_less(uint32_t ax,
                                       uint32_t ay,
                                       uint32_t bx,
                                       uint32_t by)
{
    return ax < bx || (ax == bx && ay < by);
}

static int compare_world_quantized_edge(const void *left,
                                        const void *right)
{
    const WorldQuantizedEdge *a = left;
    const WorldQuantizedEdge *b = right;

    if (a->ax != b->ax) return a->ax < b->ax ? -1 : 1;
    if (a->ay != b->ay) return a->ay < b->ay ? -1 : 1;
    if (a->bx != b->bx) return a->bx < b->bx ? -1 : 1;
    if (a->by != b->by) return a->by < b->by ? -1 : 1;
    if (a->kind != b->kind) return a->kind < b->kind ? -1 : 1;
    return 0;
}

static bool world_quantized_edge_equal(const WorldQuantizedEdge *a,
                                       const WorldQuantizedEdge *b)
{
    return a->ax == b->ax
        && a->ay == b->ay
        && a->bx == b->bx
        && a->by == b->by
        && a->kind == b->kind;
}

static bool line_array_generalize_for_overview(
    const WorldLineArray *source,
    WorldLineArray *destination)
{
    if (!source || !destination) return false;
    memset(destination, 0, sizeof(*destination));

    if (source->count == 0U) return true;
    if (source->count > SIZE_MAX / sizeof(WorldQuantizedEdge)) {
        return false;
    }

    WorldQuantizedEdge *edges =
        malloc(source->count * sizeof(*edges));
    if (!edges) return false;

    size_t edge_count = 0U;
    for (size_t i = 0U; i < source->count; ++i) {
        const WorldLine *line = &source->items[i];

        uint32_t ax =
            world_quantize_coordinate(
                line->x1, WORLD_OVERVIEW_ROAD_GRID);
        uint32_t ay =
            world_quantize_coordinate(
                line->y1, WORLD_OVERVIEW_ROAD_GRID);
        uint32_t bx =
            world_quantize_coordinate(
                line->x2, WORLD_OVERVIEW_ROAD_GRID);
        uint32_t by =
            world_quantize_coordinate(
                line->y2, WORLD_OVERVIEW_ROAD_GRID);

        /* Sub-grid road fragments are invisible in the world overview. */
        if (ax == bx && ay == by) continue;

        /*
         * Roads are rendered as undirected overview strokes. Canonicalise
         * endpoints so parallel/duplicate edges collapse after sorting.
         */
        if (world_quantized_point_less(bx, by, ax, ay)) {
            const uint32_t tx = ax;
            const uint32_t ty = ay;
            ax = bx;
            ay = by;
            bx = tx;
            by = ty;
        }

        edges[edge_count++] = (WorldQuantizedEdge){
            .ax = ax,
            .ay = ay,
            .bx = bx,
            .by = by,
            .kind = line->kind
        };
    }

    if (edge_count > 1U) {
        qsort(edges,
              edge_count,
              sizeof(*edges),
              compare_world_quantized_edge);
    }

    const double inverse_grid =
        1.0 / (double)WORLD_OVERVIEW_ROAD_GRID;
    bool ok = true;
    bool have_previous = false;
    WorldQuantizedEdge previous = {0};

    for (size_t i = 0U; i < edge_count; ++i) {
        const WorldQuantizedEdge *edge = &edges[i];
        if (have_previous
            && world_quantized_edge_equal(edge, &previous)) {
            continue;
        }

        if (!line_array_append_world(
                destination,
                (double)edge->ax * inverse_grid,
                (double)edge->ay * inverse_grid,
                (double)edge->bx * inverse_grid,
                (double)edge->by * inverse_grid,
                edge->kind)) {
            ok = false;
            break;
        }

        previous = *edge;
        have_previous = true;
    }

    free(edges);

    if (!ok) {
        line_array_destroy(destination);
        return false;
    }

    if (!line_array_build_chunks(destination)) {
        line_array_destroy(destination);
        return false;
    }

    return true;
}

static bool generalize_region_overview_roads(
    OpenRideMapWorldRegion *region)
{
    if (!region) return false;

    for (int road_index = 0;
         road_index < WORLD_MAJOR_ROAD_CLASS_COUNT;
         ++road_index) {
        WorldLineArray generalized = {0};
        if (!line_array_generalize_for_overview(
                &region->roads[road_index],
                &generalized)) {
            return false;
        }

        line_array_destroy(&region->roads[road_index]);
        region->roads[road_index] = generalized;
    }

    return true;
}

static bool build_world_region(SDL_Renderer *renderer,
                               const OpenRideRegionDefinition *definition,
                               const char *ormap_path,
                               const char *poly_path,
                               OpenRideMapWorldRegion *out,
                               char *error,
                               size_t error_size)
{
    memset(out, 0, sizeof(*out));
    out->definition = definition;
    out->map = openride_ormap_open(ormap_path, error, error_size);
    if (!out->map) return false;

    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(out->map);
    if (!metadata) {
        set_error(error, error_size, "map-world region has no metadata");
        world_region_destroy(out);
        return false;
    }

    out->metadata = *metadata;
    bool ok = load_major_roads(out->map, out, error, error_size)
        && load_major_waterways(out->map, out, error, error_size)
        && load_poly_boundary(poly_path, &out->boundary, error, error_size);

    /*
     * Boundaries are already cheap. Major roads are not: the raw z10 routing
     * geometry contains hundreds of thousands of tiny segments. Generalise
     * those arrays once at load time, then build v12 spatial chunks over the
     * much smaller result.
     */
    if (ok) {
        ok = line_array_build_chunks(&out->boundary)
            && generalize_region_overview_roads(out);
    }
    if (!ok) {
        if (!error || error[0] == '\0') {
            set_error(error,
                      error_size,
                      "out of memory indexing map-world overview geometry");
        }
        world_region_destroy(out);
        return false;
    }

    out->detail_renderer = calloc(1U, sizeof(*out->detail_renderer));
    if (!out->detail_renderer) {
        set_error(error, error_size, "out of memory creating map-world detail renderer");
        world_region_destroy(out);
        return false;
    }
    if (!openride_ormap_renderer_init(out->detail_renderer, renderer, out->map)) {
        set_error(error, error_size, "unable to initialize map-world detail renderer");
        world_region_destroy(out);
        return false;
    }
    return true;
}

static bool append_france_network_edges(
    WorldLineArray *destination,
    const uint64_t *edges,
    size_t edge_count,
    uint8_t kind)
{
    if (!destination) return false;
    const double inverse =
        1.0 / (double)WORLD_FRANCE_NETWORK_COORD_MAX;

    for (size_t i = 0U; i < edge_count; ++i) {
        const uint64_t edge = edges[i];
        const uint32_t a = (uint32_t)(edge >> 32U);
        const uint32_t b = (uint32_t)edge;

        const double ax =
            (double)(a & UINT32_C(0xffff)) * inverse;
        const double ay =
            (double)(a >> 16U) * inverse;
        const double bx =
            (double)(b & UINT32_C(0xffff)) * inverse;
        const double by =
            (double)(b >> 16U) * inverse;

        if (!line_array_append_world(
                destination,
                ax,
                ay,
                bx,
                by,
                kind)) {
            return false;
        }
    }

    return line_array_build_chunks(destination);
}

static bool build_france_generated_network(OpenRideMapWorld *world)
{
    if (!world) return false;

    if (!append_france_network_edges(
            &world->france_coastline,
            OPENRIDE_FRANCE_OVERVIEW_COAST_EDGES,
            OPENRIDE_FRANCE_OVERVIEW_COAST_EDGES_COUNT,
            0U)) {
        return false;
    }

    if (!append_france_network_edges(
            &world->france_roads[0],
            OPENRIDE_FRANCE_OVERVIEW_MOTORWAY_EDGES,
            OPENRIDE_FRANCE_OVERVIEW_MOTORWAY_EDGES_COUNT,
            OPENRIDE_ROAD_MOTORWAY)
        || !append_france_network_edges(
            &world->france_roads[1],
            OPENRIDE_FRANCE_OVERVIEW_TRUNK_EDGES,
            OPENRIDE_FRANCE_OVERVIEW_TRUNK_EDGES_COUNT,
            OPENRIDE_ROAD_TRUNK)
        || !append_france_network_edges(
            &world->france_roads[2],
            OPENRIDE_FRANCE_OVERVIEW_PRIMARY_EDGES,
            OPENRIDE_FRANCE_OVERVIEW_PRIMARY_EDGES_COUNT,
            OPENRIDE_ROAD_PRIMARY)) {
        return false;
    }

    return true;
}

static bool build_france_base_geometry(OpenRideMapWorld *world,
                                       char *error,
                                       size_t error_size)
{
    if (!world) return false;

    line_array_destroy(&world->france_boundaries);
    line_array_destroy(&world->france_coastline);
    for (int i = 0; i < WORLD_MAJOR_ROAD_CLASS_COUNT; ++i) {
        line_array_destroy(&world->france_roads[i]);
    }
    world->france_base_ready = false;

    const size_t region_count =
        openride_france_regions_lite_region_count();

    for (size_t region_index = 0U;
         region_index < region_count;
         ++region_index) {
        OpenRideFranceRegionsLiteRegionView region = {0};
        if (!openride_france_regions_lite_region_at(
                region_index,
                &region)) {
            continue;
        }

        const uint32_t ring_end =
            region.first_ring + region.ring_count;
        for (uint32_t ring_index = region.first_ring;
             ring_index < ring_end;
             ++ring_index) {
            OpenRideFranceRegionsLiteRingView ring = {0};
            if (!openride_france_regions_lite_ring_at(
                    ring_index,
                    &ring)
                || ring.point_count < 2U) {
                continue;
            }

            OpenRideFranceRegionsLitePointView first = {0};
            OpenRideFranceRegionsLitePointView previous = {0};
            if (!openride_france_regions_lite_point_at(
                    ring.point_offset,
                    &first)) {
                continue;
            }
            previous = first;

            for (uint32_t local = 1U;
                 local < ring.point_count;
                 ++local) {
                OpenRideFranceRegionsLitePointView current = {0};
                if (!openride_france_regions_lite_point_at(
                        ring.point_offset + local,
                        &current)) {
                    continue;
                }

                if (!line_array_append(
                        &world->france_boundaries,
                        previous.lat,
                        previous.lon,
                        current.lat,
                        current.lon,
                        ring.hole ? 1U : 0U)) {
                    set_error(
                        error,
                        error_size,
                        "out of memory building bundled France overview");
                    line_array_destroy(&world->france_boundaries);
                    return false;
                }
                previous = current;
            }

            if (fabs(previous.lat - first.lat) > 1e-9
                || fabs(previous.lon - first.lon) > 1e-9) {
                if (!line_array_append(
                        &world->france_boundaries,
                        previous.lat,
                        previous.lon,
                        first.lat,
                        first.lon,
                        ring.hole ? 1U : 0U)) {
                    set_error(
                        error,
                        error_size,
                        "out of memory closing bundled France overview ring");
                    line_array_destroy(&world->france_boundaries);
                    return false;
                }
            }
        }
    }

    if (!line_array_build_chunks(&world->france_boundaries)
        || !build_france_generated_network(world)) {
        set_error(
            error,
            error_size,
            "out of memory indexing bundled France overview");
        line_array_destroy(&world->france_boundaries);
        line_array_destroy(&world->france_coastline);
        for (int i = 0; i < WORLD_MAJOR_ROAD_CLASS_COUNT; ++i) {
            line_array_destroy(&world->france_roads[i]);
        }
        return false;
    }

    world->france_base_ready =
        world->france_boundaries.count > 0U
        || world->france_coastline.count > 0U
        || world->france_roads[0].count > 0U
        || world->france_roads[1].count > 0U
        || world->france_roads[2].count > 0U;
    return true;
}

OpenRideMapWorld *openride_map_world_create(SDL_Renderer *renderer,
                                             const OpenRidePlatformPaths *paths,
                                             char *error,
                                             size_t error_size)
{
    if (!renderer || !paths) {
        set_error(error, error_size, "invalid map-world arguments");
        return NULL;
    }
    OpenRideMapWorld *world = calloc(1U, sizeof(*world));
    if (!world) {
        set_error(error, error_size, "out of memory creating map world");
        return NULL;
    }
    world->renderer = renderer;
    if (!build_france_base_geometry(world, error, error_size)) {
        openride_map_world_destroy(world);
        return NULL;
    }
    if (!openride_map_world_refresh(world, paths, error, error_size)) {
        openride_map_world_destroy(world);
        return NULL;
    }
    return world;
}

bool openride_map_world_refresh(OpenRideMapWorld *world,
                                const OpenRidePlatformPaths *paths,
                                char *error,
                                size_t error_size)
{
    if (!world || !paths) {
        set_error(error, error_size, "invalid map-world refresh arguments");
        return false;
    }
    OpenRideMapWorldRegion *next = NULL;
    size_t next_count = 0U;
    const size_t catalog_count = openride_region_count();
    if (catalog_count > 0U) {
        next = calloc(catalog_count, sizeof(*next));
        if (!next) {
            set_error(error, error_size, "out of memory refreshing map world");
            return false;
        }
    }
    for (size_t i = 0U; i < catalog_count; ++i) {
        const OpenRideRegionDefinition *definition = openride_region_at(i);
        if (!definition) continue;
        OpenRideRegionStatus status;
        char status_error[256] = {0};
        if (!openride_region_get_status(paths,
                                        definition,
                                        &status,
                                        status_error,
                                        sizeof(status_error))) {
            destroy_regions(next, next_count);
            set_error(error, error_size,
                      status_error[0] ? status_error : "unable to inspect installed region");
            return false;
        }
        if (!status.ormap_installed) continue;
        char region_error[256] = {0};
        if (!build_world_region(world->renderer,
                                definition,
                                status.ormap_path,
                                status.poly_path,
                                &next[next_count],
                                region_error,
                                sizeof(region_error))) {
            destroy_regions(next, next_count + 1U);
            set_error(error, error_size,
                      region_error[0] ? region_error : "unable to build map-world region");
            return false;
        }
        ++next_count;
    }
    destroy_regions(world->regions, world->region_count);
    world->regions = next;
    world->region_count = next_count;
    set_error(error, error_size, "");
    return true;
}

void openride_map_world_destroy(OpenRideMapWorld *world)
{
    if (!world) return;
    destroy_regions(world->regions, world->region_count);
    line_array_destroy(&world->france_boundaries);
    line_array_destroy(&world->france_coastline);
    for (int i = 0; i < WORLD_MAJOR_ROAD_CLASS_COUNT; ++i) {
        line_array_destroy(&world->france_roads[i]);
    }
    free(world->vertices);
    free(world->indices);
    free(world);
}

static const char *road_kind(uint8_t road_class)
{
    switch ((OpenRideRoadClass)road_class) {
        case OPENRIDE_ROAD_MOTORWAY: return "motorway";
        case OPENRIDE_ROAD_TRUNK: return "trunk";
        case OPENRIDE_ROAD_PRIMARY: return "primary";
        default: return "other";
    }
}

static bool line_maybe_visible(OpenRidePointD a,
                               OpenRidePointD b,
                               int width,
                               int height)
{
    const double margin = 32.0;
    if (a.x < -margin && b.x < -margin) return false;
    if (a.y < -margin && b.y < -margin) return false;
    if (a.x > (double)width + margin && b.x > (double)width + margin) return false;
    if (a.y > (double)height + margin && b.y > (double)height + margin) return false;
    return true;
}

static bool ensure_geometry_scratch(OpenRideMapWorld *world,
                                    uint32_t vertex_count,
                                    uint32_t index_count)
{
    if (!world) return false;
    if (vertex_count > world->vertex_capacity) {
        uint32_t capacity = world->vertex_capacity == 0U ? 4096U : world->vertex_capacity;
        while (capacity < vertex_count) {
            if (capacity > UINT32_MAX / 2U) {
                capacity = vertex_count;
                break;
            }
            capacity *= 2U;
        }
        SDL_Vertex *grown = realloc(world->vertices, (size_t)capacity * sizeof(*grown));
        if (!grown) return false;
        world->vertices = grown;
        world->vertex_capacity = capacity;
    }
    if (index_count > world->index_capacity) {
        uint32_t capacity = world->index_capacity == 0U ? 6144U : world->index_capacity;
        while (capacity < index_count) {
            if (capacity > UINT32_MAX / 2U) {
                capacity = index_count;
                break;
            }
            capacity *= 2U;
        }
        int *grown = realloc(world->indices, (size_t)capacity * sizeof(*grown));
        if (!grown) return false;
        world->indices = grown;
        world->index_capacity = capacity;
    }
    return true;
}

static void geometry_batch_flush(OpenRideMapWorld *world, WorldGeometryBatch *batch)
{
    if (!world || !batch || batch->vertex_count == 0U || batch->index_count == 0U) {
        if (batch) {
            batch->vertex_count = 0U;
            batch->index_count = 0U;
        }
        return;
    }
    SDL_RenderGeometry(world->renderer,
                       NULL,
                       world->vertices,
                       (int)batch->vertex_count,
                       world->indices,
                       (int)batch->index_count);
    batch->vertex_count = 0U;
    batch->index_count = 0U;
}

static bool geometry_batch_reserve(OpenRideMapWorld *world,
                                   WorldGeometryBatch *batch,
                                   uint32_t add_vertices,
                                   uint32_t add_indices)
{
    if (!world || !batch) return false;
    if (batch->vertex_count > WORLD_GEOMETRY_BATCH_VERTEX_LIMIT - add_vertices
        || batch->index_count > WORLD_GEOMETRY_BATCH_INDEX_LIMIT - add_indices) {
        geometry_batch_flush(world, batch);
    }
    return ensure_geometry_scratch(world,
                                   batch->vertex_count + add_vertices,
                                   batch->index_count + add_indices);
}

static void set_geometry_vertex(SDL_Vertex *vertex,
                                float x,
                                float y,
                                OpenRideMapColor color)
{
    vertex->position.x = x;
    vertex->position.y = y;
    vertex->color.r = color.r / 255.0f;
    vertex->color.g = color.g / 255.0f;
    vertex->color.b = color.b / 255.0f;
    vertex->color.a = color.a / 255.0f;
    vertex->tex_coord.x = 0.0f;
    vertex->tex_coord.y = 0.0f;
}

static bool geometry_batch_line(OpenRideMapWorld *world,
                                WorldGeometryBatch *batch,
                                OpenRidePointD a,
                                OpenRidePointD b,
                                int width,
                                OpenRideMapColor color)
{
    if (!world || !batch) return false;
    if (width < 1) width = 1;
    if (width > WORLD_MAX_LINE_WIDTH) width = WORLD_MAX_LINE_WIDTH;
    const float x1 = (float)a.x;
    const float y1 = (float)a.y;
    const float x2 = (float)b.x;
    const float y2 = (float)b.y;
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.001f) return true;
    if (!geometry_batch_reserve(world, batch, 4U, 6U)) return false;
    const float half = 0.5f * (float)width;
    const float nx = -dy / length * half;
    const float ny = dx / length * half;
    const float xs[4] = {x1 + nx, x2 + nx, x2 - nx, x1 - nx};
    const float ys[4] = {y1 + ny, y2 + ny, y2 - ny, y1 - ny};
    const uint32_t base_vertex = batch->vertex_count;
    const uint32_t base_index = batch->index_count;
    for (uint32_t i = 0U; i < 4U; ++i) {
        set_geometry_vertex(&world->vertices[base_vertex + i], xs[i], ys[i], color);
    }
    const int base = (int)base_vertex;
    world->indices[base_index + 0U] = base + 0;
    world->indices[base_index + 1U] = base + 1;
    world->indices[base_index + 2U] = base + 2;
    world->indices[base_index + 3U] = base + 0;
    world->indices[base_index + 4U] = base + 2;
    world->indices[base_index + 5U] = base + 3;
    batch->vertex_count += 4U;
    batch->index_count += 6U;
    return true;
}

static OpenRidePointD world_line_to_screen(double world_x,
                                               double world_y,
                                               OpenRidePointD center,
                                               double world_size,
                                               double bearing_cos,
                                               double bearing_sin,
                                               int viewport_width,
                                               int viewport_height)
{
    double dx = world_x - center.x;
    if (dx > 0.5) dx -= 1.0;
    if (dx < -0.5) dx += 1.0;

    const double dy = world_y - center.y;
    const double world_dx = dx * world_size;
    const double world_dy = dy * world_size;

    const double screen_dx =
        world_dx * bearing_cos + world_dy * bearing_sin;
    const double screen_dy =
        -world_dx * bearing_sin + world_dy * bearing_cos;

    return (OpenRidePointD){
        (double)viewport_width * 0.5 + screen_dx,
        (double)viewport_height * 0.5 + screen_dy
    };
}

static bool world_line_chunk_maybe_visible(
    const WorldLineChunk *chunk,
    OpenRidePointD center,
    double world_size,
    double bearing_cos,
    double bearing_sin,
    int viewport_width,
    int viewport_height)
{
    if (!chunk) return true;

    /*
     * A chunk spanning more than half the Mercator world may cross the
     * dateline. Keep it rather than risking an incorrect cull.
     */
    if (chunk->max_x - chunk->min_x > 0.5) return true;

    const double xs[4] = {
        chunk->min_x,
        chunk->max_x,
        chunk->max_x,
        chunk->min_x
    };
    const double ys[4] = {
        chunk->min_y,
        chunk->min_y,
        chunk->max_y,
        chunk->max_y
    };

    const double margin = 48.0;
    bool all_left = true;
    bool all_right = true;
    bool all_above = true;
    bool all_below = true;

    for (int i = 0; i < 4; ++i) {
        const OpenRidePointD p = world_line_to_screen(
            xs[i],
            ys[i],
            center,
            world_size,
            bearing_cos,
            bearing_sin,
            viewport_width,
            viewport_height);

        all_left = all_left && p.x < -margin;
        all_right =
            all_right && p.x > (double)viewport_width + margin;
        all_above = all_above && p.y < -margin;
        all_below =
            all_below && p.y > (double)viewport_height + margin;
    }

    return !(all_left || all_right || all_above || all_below);
}

static void draw_line_array(OpenRideMapWorld *world,
                            const OpenRideMapCamera *camera,
                            const WorldLineArray *array,
                            OpenRideMapColor color,
                            int line_width,
                            int viewport_width,
                            int viewport_height,
                            uint8_t kind_filter,
                            bool filter_kind)
{
    if (!world || !world->renderer || !camera || !array) return;

    const OpenRidePointD center =
        openride_mercator_forward(camera->center_lat, camera->center_lon);
    const double world_size = openride_world_size_pixels(camera->zoom);
    const double angle =
        camera->bearing_deg * 3.14159265358979323846 / 180.0;
    const double bearing_cos = cos(angle);
    const double bearing_sin = sin(angle);

    WorldGeometryBatch batch = {0};

    if (array->chunk_count == 0U || !array->chunks) {
        for (size_t i = 0U; i < array->count; ++i) {
            const WorldLine *line = &array->items[i];
            if (filter_kind && line->kind != kind_filter) continue;

            const OpenRidePointD a = world_line_to_screen(
                line->x1,
                line->y1,
                center,
                world_size,
                bearing_cos,
                bearing_sin,
                viewport_width,
                viewport_height);
            const OpenRidePointD b = world_line_to_screen(
                line->x2,
                line->y2,
                center,
                world_size,
                bearing_cos,
                bearing_sin,
                viewport_width,
                viewport_height);

            if (!line_maybe_visible(a, b,
                                    viewport_width, viewport_height)) {
                continue;
            }
            if (!geometry_batch_line(world, &batch,
                                     a, b, line_width, color)) {
                break;
            }
        }
        geometry_batch_flush(world, &batch);
        return;
    }

    for (size_t c = 0U; c < array->chunk_count; ++c) {
        const WorldLineChunk *chunk = &array->chunks[c];
        if (!world_line_chunk_maybe_visible(
                chunk,
                center,
                world_size,
                bearing_cos,
                bearing_sin,
                viewport_width,
                viewport_height)) {
            continue;
        }

        const size_t end = chunk->first + chunk->count;
        for (size_t i = chunk->first; i < end; ++i) {
            const WorldLine *line = &array->items[i];
            if (filter_kind && line->kind != kind_filter) continue;

            const OpenRidePointD a = world_line_to_screen(
                line->x1,
                line->y1,
                center,
                world_size,
                bearing_cos,
                bearing_sin,
                viewport_width,
                viewport_height);
            const OpenRidePointD b = world_line_to_screen(
                line->x2,
                line->y2,
                center,
                world_size,
                bearing_cos,
                bearing_sin,
                viewport_width,
                viewport_height);

            if (!line_maybe_visible(a, b,
                                    viewport_width, viewport_height)) {
                continue;
            }
            if (!geometry_batch_line(world, &batch,
                                     a, b, line_width, color)) {
                geometry_batch_flush(world, &batch);
                return;
            }
        }
    }

    geometry_batch_flush(world, &batch);
}

static double openride_zoom_smoothstep(double zoom,
                                      double start,
                                      double end)
{
    if (zoom <= start) return 0.0;
    if (zoom >= end) return 1.0;
    double t = (zoom - start) / (end - start);
    return t * t * (3.0 - 2.0 * t);
}

static uint8_t openride_scaled_alpha(uint8_t alpha, double factor)
{
    if (factor <= 0.0) return 0U;
    if (factor >= 1.0) return alpha;
    return (uint8_t)lround((double)alpha * factor);
}

static void draw_region_label(OpenRideMapWorld *world,
                              const OpenRideMapCamera *camera,
                              const OpenRideMapWorldRegion *region,
                              const OpenRideMapPalette *palette,
                              int viewport_width,
                              int viewport_height)
{
    if (!world || !world->renderer || !camera || !region || !region->definition
        || !palette || !region->definition->name || region->definition->name[0] == '\0') {
        return;
    }

    double lat = 0.0;
    double lon = 0.0;
    if (region->metadata.has_center) {
        lat = region->metadata.center_lat;
        lon = region->metadata.center_lon;
    } else if (region->metadata.has_bounds) {
        lat = (region->metadata.south + region->metadata.north) * 0.5;
        lon = (region->metadata.west + region->metadata.east) * 0.5;
    } else {
        return;
    }

    const OpenRidePointD point = openride_geo_to_screen(camera,
                                                         lat,
                                                         lon,
                                                         viewport_width,
                                                         viewport_height);
    const char *name = region->definition->name;
    const float text_width = (float)strlen(name) * 8.0f;
    const float text_height = 8.0f;
    const float x = (float)point.x - text_width * 0.5f;
    const float y = (float)point.y - text_height * 0.5f;

    if (x + text_width < 0.0f || y + text_height < 0.0f
        || x >= (float)viewport_width || y >= (float)viewport_height) {
        return;
    }

    const double label_fade_out =
        1.0 - openride_zoom_smoothstep(camera->zoom, 8.90, 9.50);

    SDL_SetRenderDrawColor(world->renderer,
                           palette->label_halo.r,
                           palette->label_halo.g,
                           palette->label_halo.b,
                           openride_scaled_alpha(235U, label_fade_out));
    SDL_RenderDebugText(world->renderer, x - 1.0f, y, name);
    SDL_RenderDebugText(world->renderer, x + 1.0f, y, name);
    SDL_RenderDebugText(world->renderer, x, y - 1.0f, name);
    SDL_RenderDebugText(world->renderer, x, y + 1.0f, name);

    SDL_SetRenderDrawColor(world->renderer,
                           palette->label.r,
                           palette->label.g,
                           palette->label.b,
                           openride_scaled_alpha(245U, label_fade_out));
    SDL_RenderDebugText(world->renderer, x, y, name);
}

static bool world_city_label_boxes_overlap(WorldCityLabelBox a,
                                           WorldCityLabelBox b)
{
    return a.left < b.right
        && a.right > b.left
        && a.top < b.bottom
        && a.bottom > b.top;
}

static bool world_reference_label_kind_matches(int pass,
                                               uint8_t kind)
{
    if (pass == 0) return kind == OPENRIDE_PLACE_CITY;
    if (pass == 1) return kind == OPENRIDE_PLACE_TOWN;
    return kind == OPENRIDE_PLACE_VILLAGE;
}

static bool world_reference_label_try_place(
    WorldCityLabelBox *boxes,
    uint32_t box_count,
    float base_x,
    float base_y,
    float text_width,
    int viewport_width,
    int viewport_height,
    float *x_out,
    float *y_out,
    WorldCityLabelBox *box_out)
{
    static const float offsets[][2] = {
        {0.0f, 0.0f},
        {0.0f, -12.0f},
        {0.0f, 12.0f},
        {-14.0f, 0.0f},
        {14.0f, 0.0f}
    };

    for (size_t attempt = 0U;
         attempt < sizeof(offsets) / sizeof(offsets[0]);
         ++attempt) {
        const float x = base_x + offsets[attempt][0];
        const float y = base_y + offsets[attempt][1];

        if (x + text_width < 0.0f
            || y + 8.0f < 0.0f
            || x >= (float)viewport_width
            || y >= (float)viewport_height) {
            continue;
        }

        const WorldCityLabelBox box = {
            .left = x - 5.0f,
            .top = y - 3.0f,
            .right = x + text_width + 5.0f,
            .bottom = y + 11.0f
        };

        bool collision = false;
        for (uint32_t b = 0U; b < box_count; ++b) {
            if (world_city_label_boxes_overlap(box, boxes[b])) {
                collision = true;
                break;
            }
        }
        if (collision) continue;

        *x_out = x;
        *y_out = y;
        *box_out = box;
        return true;
    }

    return false;
}

static void draw_major_city_labels(OpenRideMapWorld *world,
                                   const OpenRideMapCamera *camera,
                                   const OpenRideMapPalette *palette,
                                   double opacity,
                                   int viewport_width,
                                   int viewport_height)
{
    if (!world || !world->renderer || !camera || !palette
        || opacity <= 0.0) return;

    WorldCityLabelBox boxes[WORLD_MAJOR_CITY_LABEL_MAX];
    uint32_t box_count = 0U;

    SDL_BlendMode previous_blend_mode = SDL_BLENDMODE_NONE;
    const bool have_previous_blend_mode =
        SDL_GetRenderDrawBlendMode(world->renderer, &previous_blend_mode);
    SDL_SetRenderDrawBlendMode(world->renderer, SDL_BLENDMODE_BLEND);

    for (size_t region_index = 0U;
         region_index < world->region_count
         && box_count < WORLD_MAJOR_CITY_LABEL_MAX;
         ++region_index) {
        const OpenRideMapWorldRegion *region = &world->regions[region_index];
        if (!region->map) continue;

        uint32_t label_count = 0U;
        const OpenRideORMapLabel *labels =
            openride_ormap_labels(region->map, &label_count);
        if (!labels || label_count == 0U) continue;

        uint32_t emitted_for_region = 0U;

        /*
         * Prefer true OSM cities. If a region has too few, complete with its
         * highest-ranked towns, then villages as a final fallback.
         *
         * The ORMap label array is globally rank-sorted, therefore each pass
         * naturally selects the most important settlements of that kind.
         */
        for (int pass = 0;
             pass < 3
             && emitted_for_region < WORLD_REGION_REFERENCE_LABEL_TARGET
             && box_count < WORLD_MAJOR_CITY_LABEL_MAX;
             ++pass) {
            for (uint32_t i = 0U;
                 i < label_count
                 && emitted_for_region < WORLD_REGION_REFERENCE_LABEL_TARGET
                 && box_count < WORLD_MAJOR_CITY_LABEL_MAX;
                 ++i) {
                const OpenRideORMapLabel *label = &labels[i];
                if (!world_reference_label_kind_matches(pass, label->kind)
                    || label->name[0] == '\0') {
                    continue;
                }

                const OpenRidePointD point =
                    openride_geo_to_screen(camera,
                                           label->lat_e7 / 10000000.0,
                                           label->lon_e7 / 10000000.0,
                                           viewport_width,
                                           viewport_height);

                const float text_width =
                    (float)strlen(label->name) * 8.0f;
                const float base_x =
                    (float)point.x - text_width * 0.5f;
                const float base_y = (float)point.y - 4.0f;

                float x = 0.0f;
                float y = 0.0f;
                WorldCityLabelBox box = {0};

                if (!world_reference_label_try_place(
                        boxes,
                        box_count,
                        base_x,
                        base_y,
                        text_width,
                        viewport_width,
                        viewport_height,
                        &x,
                        &y,
                        &box)) {
                    continue;
                }

                boxes[box_count++] = box;
                ++emitted_for_region;

                SDL_SetRenderDrawColor(world->renderer,
                                       palette->label_halo.r,
                                       palette->label_halo.g,
                                       palette->label_halo.b,
                                       openride_scaled_alpha(245U, opacity));
                SDL_RenderDebugText(world->renderer,
                                    x - 1.0f,
                                    y,
                                    label->name);
                SDL_RenderDebugText(world->renderer,
                                    x + 1.0f,
                                    y,
                                    label->name);
                SDL_RenderDebugText(world->renderer,
                                    x,
                                    y - 1.0f,
                                    label->name);
                SDL_RenderDebugText(world->renderer,
                                    x,
                                    y + 1.0f,
                                    label->name);

                SDL_SetRenderDrawColor(world->renderer,
                                       palette->label.r,
                                       palette->label.g,
                                       palette->label.b,
                                       openride_scaled_alpha(
                                           SDL_ALPHA_OPAQUE, opacity));
                SDL_RenderDebugText(world->renderer, x, y, label->name);
            }
        }

    }

    SDL_SetRenderDrawBlendMode(
        world->renderer,
        have_previous_blend_mode
            ? previous_blend_mode
            : SDL_BLENDMODE_NONE);
}

static bool world_region_id_installed(
    const OpenRideMapWorld *world,
    const char *region_id)
{
    if (!world || !region_id || region_id[0] == '\0') return false;

    for (size_t i = 0U; i < world->region_count; ++i) {
        const OpenRideMapWorldRegion *region = &world->regions[i];
        if (region->definition
            && strcmp(region->definition->id, region_id) == 0) {
            return true;
        }
    }
    return false;
}

static int compare_france_label_candidate(
    const void *left,
    const void *right)
{
    const WorldFranceLabelCandidate *a = left;
    const WorldFranceLabelCandidate *b = right;

    if (a->place->rank != b->place->rank) {
        return a->place->rank > b->place->rank ? -1 : 1;
    }
    return strcmp(a->place->name, b->place->name);
}

static void draw_france_base_city_labels(
    OpenRideMapWorld *world,
    const OpenRideMapCamera *camera,
    const OpenRideMapPalette *palette,
    int viewport_width,
    int viewport_height)
{
    if (!world || !world->renderer || !camera || !palette) return;

    int min_rank = 72;
    uint32_t max_labels = WORLD_FRANCE_BASE_LABEL_MAX;
    if (camera->zoom < 7.0) {
        min_rank = 98;
        max_labels = 8U;
    } else if (camera->zoom < 8.0) {
        min_rank = 94;
        max_labels = 14U;
    } else if (camera->zoom < 9.0) {
        min_rank = 88;
        max_labels = 20U;
    } else if (camera->zoom < 10.5) {
        min_rank = 82;
        max_labels = 28U;
    } else if (camera->zoom < 12.5) {
        min_rank = 76;
        max_labels = 38U;
    }

    WorldFranceLabelCandidate
        candidates[WORLD_FRANCE_BASE_CANDIDATE_MAX];
    uint32_t candidate_count = 0U;

    const size_t place_count =
        openride_france_lite_place_count();
    for (size_t i = 0U;
         i < place_count
         && candidate_count < WORLD_FRANCE_BASE_CANDIDATE_MAX;
         ++i) {
        const OpenRideFranceLitePlace *place =
            openride_france_lite_place_at(i);
        if (!place
            || !place->name
            || place->name[0] == '\0'
            || place->rank < min_rank
            || world_region_id_installed(
                world,
                place->region_id)) {
            continue;
        }

        const OpenRidePointD point =
            openride_geo_to_screen(
                camera,
                place->lat,
                place->lon,
                viewport_width,
                viewport_height);

        if (point.x < -140.0
            || point.x > (double)viewport_width + 140.0
            || point.y < -60.0
            || point.y > (double)viewport_height + 60.0) {
            continue;
        }

        candidates[candidate_count++] =
            (WorldFranceLabelCandidate){
                .place = place,
                .point = point
            };
    }

    if (candidate_count > 1U) {
        qsort(
            candidates,
            candidate_count,
            sizeof(candidates[0]),
            compare_france_label_candidate);
    }

    WorldCityLabelBox boxes[WORLD_FRANCE_BASE_LABEL_MAX];
    uint32_t box_count = 0U;

    SDL_BlendMode previous_blend_mode = SDL_BLENDMODE_NONE;
    const bool have_previous_blend_mode =
        SDL_GetRenderDrawBlendMode(
            world->renderer,
            &previous_blend_mode);
    SDL_SetRenderDrawBlendMode(
        world->renderer,
        SDL_BLENDMODE_BLEND);

    for (uint32_t i = 0U;
         i < candidate_count
         && box_count < max_labels;
         ++i) {
        const WorldFranceLabelCandidate *candidate =
            &candidates[i];
        const char *name = candidate->place->name;
        const float text_width =
            (float)strlen(name) * 8.0f;
        const float base_x =
            (float)candidate->point.x
            - text_width * 0.5f;
        const float base_y =
            (float)candidate->point.y - 4.0f;

        float x = 0.0f;
        float y = 0.0f;
        WorldCityLabelBox box = {0};
        if (!world_reference_label_try_place(
                boxes,
                box_count,
                base_x,
                base_y,
                text_width,
                viewport_width,
                viewport_height,
                &x,
                &y,
                &box)) {
            continue;
        }

        boxes[box_count++] = box;

        SDL_SetRenderDrawColor(
            world->renderer,
            palette->label_halo.r,
            palette->label_halo.g,
            palette->label_halo.b,
            225U);
        SDL_RenderDebugText(
            world->renderer, x - 1.0f, y, name);
        SDL_RenderDebugText(
            world->renderer, x + 1.0f, y, name);
        SDL_RenderDebugText(
            world->renderer, x, y - 1.0f, name);
        SDL_RenderDebugText(
            world->renderer, x, y + 1.0f, name);

        SDL_SetRenderDrawColor(
            world->renderer,
            palette->label.r,
            palette->label.g,
            palette->label.b,
            SDL_ALPHA_OPAQUE);
        SDL_RenderDebugText(
            world->renderer, x, y, name);
    }

    SDL_SetRenderDrawBlendMode(
        world->renderer,
        have_previous_blend_mode
            ? previous_blend_mode
            : SDL_BLENDMODE_NONE);
}

static void draw_france_generated_network(
    OpenRideMapWorld *world,
    const OpenRideMapCamera *camera,
    OpenRideMapStyle style,
    const OpenRideMapPalette *palette,
    int viewport_width,
    int viewport_height)
{
    if (!world || !camera || !palette) return;

    /*
     * The generated coastline is the primary national geographic reference.
     * Keep it visible longer than the road atlas so non-downloaded areas still
     * retain a natural silhouette while ORMap takes over the road detail.
     */
    if (world->france_coastline.count > 0U
        && camera->zoom < 13.0) {
        OpenRideMapColor coast = palette->water_line;
        double coast_alpha = 0.90;
        if (camera->zoom > 10.75) {
            coast_alpha *=
                1.0 - openride_zoom_smoothstep(
                    camera->zoom,
                    10.75,
                    13.0);
        }
        coast.a =
            openride_scaled_alpha(205U, coast_alpha);
        draw_line_array(
            world,
            camera,
            &world->france_coastline,
            coast,
            camera->zoom >= 8.0 ? 2 : 1,
            viewport_width,
            viewport_height,
            0U,
            false);
    }

    /*
     * France Overview road ownership:
     *
     *   z6        motorway
     *   z7        + trunk
     *   z8        + primary, initially faint
     *   z9-z10.5  full national hierarchy
     *   z10.5-12  progressive handoff to regional ORMap
     *
     * No secondary/tertiary roads belong in this national layer.
     */
    const double atlas_fade =
        1.0 - openride_zoom_smoothstep(
            camera->zoom,
            10.5,
            12.0);
    if (atlas_fade <= 0.001) return;

    for (int road_index = 0;
         road_index < WORLD_MAJOR_ROAD_CLASS_COUNT;
         ++road_index) {
        const OpenRideRoadClass road_class =
            (OpenRideRoadClass)(
                (int)OPENRIDE_ROAD_MOTORWAY + road_index);

        double class_factor = 1.0;
        switch (road_class) {
            case OPENRIDE_ROAD_MOTORWAY:
                class_factor = 1.0;
                break;

            case OPENRIDE_ROAD_TRUNK:
                if (camera->zoom < 7.0) continue;
                class_factor =
                    0.58
                    + 0.32 * openride_zoom_smoothstep(
                        camera->zoom,
                        7.0,
                        8.6);
                break;

            case OPENRIDE_ROAD_PRIMARY:
                if (camera->zoom < 8.0) continue;
                class_factor =
                    0.24
                    + 0.62 * openride_zoom_smoothstep(
                        camera->zoom,
                        8.0,
                        9.35);
                break;

            default:
                continue;
        }

        const char *kind =
            road_kind((uint8_t)road_class);
        OpenRideMapRoadPaint paint;
        if (!openride_map_road_paint(
                style,
                kind,
                false,
                camera->zoom,
                &paint)) {
            continue;
        }

        /*
         * The national atlas must remain context, not look like local
         * navigation geometry. Keep lower classes thinner at z8-z9 while
         * preserving motorway readability.
         */
        int width = paint.width;
        if (road_class == OPENRIDE_ROAD_PRIMARY
            && camera->zoom < 9.6) {
            width = 1;
        } else if (road_class == OPENRIDE_ROAD_TRUNK
                   && camera->zoom < 8.2
                   && width > 1) {
            width = 1;
        }

        paint.line.a =
            openride_scaled_alpha(
                paint.line.a,
                0.90 * class_factor * atlas_fade);

        draw_line_array(
            world,
            camera,
            &world->france_roads[road_index],
            paint.line,
            width,
            viewport_width,
            viewport_height,
            0U,
            false);
    }
}

bool openride_map_world_base_available(
    const OpenRideMapWorld *world)
{
    return world && world->france_base_ready;
}

void openride_map_world_draw_base_overview(
    OpenRideMapWorld *world,
    const OpenRideMapCamera *camera,
    OpenRideMapStyle style,
    int viewport_width,
    int viewport_height)
{
    if (!openride_map_world_base_available(world)
        || !world->renderer
        || !camera
        || viewport_width <= 0
        || viewport_height <= 0
        || camera->zoom < OPENRIDE_MAP_WORLD_MIN_ZOOM) {
        return;
    }

    const bool debug_enabled = world->debug_enabled;
    const uint64_t debug_started_ns =
        debug_enabled ? SDL_GetTicksNS() : 0U;
    if (debug_enabled) world->debug.overview_drawn = true;

    const OpenRideMapPalette palette =
        openride_map_palette(style);

    /*
     * This is intentionally a lightweight context layer, not navigation
     * cartography. Once the generated OSM coastline exists, the historical
     * Geofabrik extraction rings become secondary administrative context
     * instead of pretending to be a coastline.
     */
    OpenRideMapColor boundary = palette.boundary;
    double boundary_alpha = 0.0;
    if (camera->zoom < 9.0) {
        boundary_alpha = 0.68;
    } else if (camera->zoom < 11.0) {
        boundary_alpha =
            0.68
            * (1.0 - openride_zoom_smoothstep(
                camera->zoom,
                9.0,
                11.0));
        if (boundary_alpha < 0.22) boundary_alpha = 0.22;
    } else if (camera->zoom < 13.0) {
        boundary_alpha =
            0.22
            * (1.0 - openride_zoom_smoothstep(
                camera->zoom,
                11.0,
                13.0));
    }

    if (world->france_coastline.count > 0U) {
        /*
         * These rings are Geofabrik extraction/admin context, not coastline.
         * Once real OSM coastline exists they should never compete visually
         * with the geography or the national road hierarchy.
         */
        boundary_alpha *= 0.14;
    }

    if (boundary_alpha > 0.001) {
        boundary.a =
            openride_scaled_alpha(115U, boundary_alpha);
        draw_line_array(
            world,
            camera,
            &world->france_boundaries,
            boundary,
            1,
            viewport_width,
            viewport_height,
            0U,
            false);
    }

    draw_france_generated_network(
        world,
        camera,
        style,
        &palette,
        viewport_width,
        viewport_height);

    draw_france_base_city_labels(
        world,
        camera,
        &palette,
        viewport_width,
        viewport_height);

    if (debug_enabled) {
        world->debug.overview_ms +=
            (double)(SDL_GetTicksNS() - debug_started_ns)
            / 1000000.0;
    }
}

static bool overview_region_maybe_visible(
    const OpenRideMapWorldRegion *region,
    const OpenRideMapCamera *camera,
    int viewport_width,
    int viewport_height)
{
    if (!region || !camera || !region->metadata.has_bounds) return true;

    const double latitudes[4] = {
        region->metadata.north,
        region->metadata.north,
        region->metadata.south,
        region->metadata.south
    };
    const double longitudes[4] = {
        region->metadata.west,
        region->metadata.east,
        region->metadata.west,
        region->metadata.east
    };

    const double margin = 96.0;
    bool all_left = true;
    bool all_right = true;
    bool all_above = true;
    bool all_below = true;

    for (int i = 0; i < 4; ++i) {
        const OpenRidePointD point =
            openride_geo_to_screen(camera,
                                   latitudes[i],
                                   longitudes[i],
                                   viewport_width,
                                   viewport_height);
        all_left = all_left && point.x < -margin;
        all_right =
            all_right && point.x > (double)viewport_width + margin;
        all_above = all_above && point.y < -margin;
        all_below =
            all_below && point.y > (double)viewport_height + margin;
    }

    return !(all_left || all_right || all_above || all_below);
}


void openride_map_world_draw(OpenRideMapWorld *world,
                             const OpenRideMapCamera *camera,
                             OpenRideMapStyle style,
                             const char *skip_region_id,
                             int viewport_width,
                             int viewport_height)
{
    if (!world || !world->renderer || !camera
        || viewport_width <= 0 || viewport_height <= 0
        || camera->zoom > OPENRIDE_MAP_WORLD_MAX_OVERVIEW_ZOOM) {
        return;
    }

    const bool debug_enabled = world->debug_enabled;
    const uint64_t debug_started_ns = debug_enabled ? SDL_GetTicksNS() : 0U;
    if (debug_enabled) world->debug.overview_drawn = true;

    const OpenRideMapPalette palette = openride_map_palette(style);
    const double overview_handoff =
        1.0 - openride_zoom_smoothstep(camera->zoom, 10.0, 11.30);
    for (size_t i = 0U; i < world->region_count; ++i) {
        const OpenRideMapWorldRegion *region = &world->regions[i];
        if (skip_region_id && region->definition
            && strcmp(skip_region_id, region->definition->id) == 0) {
            continue;
        }
        if (!overview_region_maybe_visible(region,
                                           camera,
                                           viewport_width,
                                           viewport_height)) {
            continue;
        }
        /* Coverage is an availability hint, not an administrative border. */
        OpenRideMapColor boundary = palette.boundary;
        boundary.a = camera->zoom < OPENRIDE_MAP_WORLD_DETAIL_ZOOM ? 78U : 52U;
        const double boundary_fade_out =
            1.0 - openride_zoom_smoothstep(camera->zoom, 9.60, 10.70);
        boundary.a = openride_scaled_alpha(
            boundary.a, boundary_fade_out * overview_handoff);
        draw_line_array(world,
                        camera,
                        &region->boundary,
                        boundary,
                        1,
                        viewport_width,
                        viewport_height,
                        0U,
                        false);

        /*
         * No overview waterways: the current format has no importance rank,
         * so drawing every river/canal dominates Android CPU around z8-z9.
         * Detailed cartography owns hydrography at close zoom.
         */

        /* Progressive road hierarchy: motorway -> trunk -> primary. */
        for (int road_class = OPENRIDE_ROAD_MOTORWAY;
             road_class <= OPENRIDE_ROAD_PRIMARY;
             ++road_class) {
            if (road_class == OPENRIDE_ROAD_TRUNK
                && camera->zoom < 7.5) continue;
            if (road_class == OPENRIDE_ROAD_PRIMARY
                && camera->zoom < 9.65) {
                continue;
            }

            OpenRideMapRoadPaint paint;
            const double paint_zoom = camera->zoom < 10.0 ? 10.0 : camera->zoom;
            if (!openride_map_road_paint(style,
                                         road_kind((uint8_t)road_class),
                                         false,
                                         paint_zoom,
                                         &paint)) {
                continue;
            }

            OpenRideMapColor color = paint.line;
            color.a = road_class == OPENRIDE_ROAD_MOTORWAY ? 220U : 190U;

            if (road_class == OPENRIDE_ROAD_PRIMARY) {
                const double primary_fade =
                    openride_zoom_smoothstep(camera->zoom, 9.65, 10.55);
                color.a = openride_scaled_alpha(190U, primary_fade);
            }
            color.a = openride_scaled_alpha(color.a, overview_handoff);
            if (color.a == 0U) continue;

            int line_width = paint.width;
            if (camera->zoom < 8.0 && line_width > 2) line_width = 2;
            if (road_class == OPENRIDE_ROAD_PRIMARY) line_width = 1;

            const int road_index =
                road_class - (int)OPENRIDE_ROAD_MOTORWAY;
            draw_line_array(world,
                            camera,
                            &region->roads[road_index],
                            color,
                            line_width,
                            viewport_width,
                            viewport_height,
                            0U,
                            false);
        }
    }

    /*
     * Region names belong to the world overview. Above z10, detailed
     * cartography regains priority.
     */
    if (camera->zoom <= 9.5) {
        for (size_t i = 0U; i < world->region_count; ++i) {
            const OpenRideMapWorldRegion *region = &world->regions[i];
            if (skip_region_id && region->definition
                && strcmp(skip_region_id, region->definition->id) == 0) {
                continue;
            }

            draw_region_label(world,
                              camera,
                              region,
                              &palette,
                              viewport_width,
                              viewport_height);
        }
    }

    /*
     * Keep 2-3 important settlement names per installed region as
     * permanent orientation landmarks throughout the overview.
     */
    draw_major_city_labels(world,
                           camera,
                           &palette,
                           overview_handoff,
                           viewport_width,
                           viewport_height);

    if (debug_enabled) {
        world->debug.overview_ms +=
            (double)(SDL_GetTicksNS() - debug_started_ns) / 1000000.0;
    }
}


static bool detail_region_maybe_visible(const OpenRideMapWorldRegion *region,
                                        const OpenRideMapCamera *camera,
                                        int viewport_width,
                                        int viewport_height)
{
    if (!region || !camera || !region->detail_renderer) return false;
    if (!region->metadata.has_bounds) return true;

    const double latitudes[4] = {
        region->metadata.north,
        region->metadata.north,
        region->metadata.south,
        region->metadata.south
    };
    const double longitudes[4] = {
        region->metadata.west,
        region->metadata.east,
        region->metadata.west,
        region->metadata.east
    };

    const double margin = 128.0;
    bool all_left = true;
    bool all_right = true;
    bool all_above = true;
    bool all_below = true;

    for (int i = 0; i < 4; ++i) {
        const OpenRidePointD point = openride_geo_to_screen(camera,
                                                             latitudes[i],
                                                             longitudes[i],
                                                             viewport_width,
                                                             viewport_height);
        all_left = all_left && point.x < -margin;
        all_right = all_right && point.x > (double)viewport_width + margin;
        all_above = all_above && point.y < -margin;
        all_below = all_below && point.y > (double)viewport_height + margin;
    }

    return !(all_left || all_right || all_above || all_below);
}

/*
 * Multi-region ORMap compositor for installed regions.
 *
 * The historical name is kept to avoid a broad API rename in this migration,
 * but this path now owns the full z6-z18 installed-region cartography.
 */
void openride_map_world_draw_detail(OpenRideMapWorld *world,
                                    const OpenRideMapCamera *camera,
                                    OpenRideMapStyle style,
                                    int viewport_width,
                                    int viewport_height)
{
    if (!world || !world->renderer || !camera
        || viewport_width <= 0 || viewport_height <= 0
        || camera->zoom < OPENRIDE_MAP_WORLD_MIN_ZOOM) {
        return;
    }

    const bool debug_enabled = world->debug_enabled;
    const uint64_t debug_started_ns = debug_enabled ? SDL_GetTicksNS() : 0U;
    if (debug_enabled) world->debug.detail_drawn = true;

    const OpenRideMapPalette palette = openride_map_palette(style);
    SDL_SetRenderDrawColor(world->renderer,
                           palette.background.r,
                           palette.background.g,
                           palette.background.b,
                           SDL_ALPHA_OPAQUE);
    SDL_RenderClear(world->renderer);

    openride_map_world_draw_base_overview(
        world,
        camera,
        style,
        viewport_width,
        viewport_height);

    size_t visible_count = 0U;
    for (size_t i = 0U; i < world->region_count; ++i) {
        OpenRideMapWorldRegion *region = &world->regions[i];
        region->detail_visible = detail_region_maybe_visible(region,
                                                              camera,
                                                              viewport_width,
                                                              viewport_height);
        if (!region->detail_visible) continue;

        ++visible_count;
        openride_ormap_renderer_set_style(region->detail_renderer, style);
        openride_ormap_renderer_begin_frame(region->detail_renderer);
    }

    if (debug_enabled) world->debug.visible_detail_regions = (uint32_t)visible_count;
    if (visible_count == 0U) {
        if (debug_enabled) {
            world->debug.detail_ms +=
                (double)(SDL_GetTicksNS() - debug_started_ns) / 1000000.0;
        }
        return;
    }

    /*
     * Render by cartographic layer across every visible .ormap instead of
     * rendering one complete region at a time. This prevents a neighbouring
     * region's fills from covering roads already drawn by another region.
     */
    for (int layer = OPENRIDE_ORMAP_RENDER_LAYER_MASKS;
         layer <= OPENRIDE_ORMAP_RENDER_LAYER_LABELS;
         ++layer) {
        const uint64_t layer_started_ns = debug_enabled ? SDL_GetTicksNS() : 0U;
        for (size_t i = 0U; i < world->region_count; ++i) {
            OpenRideMapWorldRegion *region = &world->regions[i];
            if (!region->detail_visible) continue;
            openride_ormap_renderer_draw_layer(region->detail_renderer,
                                               camera,
                                               viewport_width,
                                               viewport_height,
                                               (OpenRideORMapRenderLayer)layer);
        }
        if (debug_enabled) {
            const double layer_ms = (double)(SDL_GetTicksNS() - layer_started_ns) / 1000000.0;
            switch ((OpenRideORMapRenderLayer)layer) {
                case OPENRIDE_ORMAP_RENDER_LAYER_MASKS: world->debug.masks_ms += layer_ms; break;
                case OPENRIDE_ORMAP_RENDER_LAYER_AREAS: world->debug.areas_ms += layer_ms; break;
                case OPENRIDE_ORMAP_RENDER_LAYER_WATERWAYS: world->debug.waterways_ms += layer_ms; break;
                case OPENRIDE_ORMAP_RENDER_LAYER_ROADS: world->debug.roads_ms += layer_ms; break;
                case OPENRIDE_ORMAP_RENDER_LAYER_LABELS: world->debug.labels_ms += layer_ms; break;
                default: break;
            }
        }
    }

    if (debug_enabled) {
        for (size_t i = 0U; i < world->region_count; ++i) {
            const OpenRideMapWorldRegion *region = &world->regions[i];
            if (!region->detail_visible || !region->detail_renderer) continue;
            map_world_accumulate_road_debug(&world->debug.road, &region->detail_renderer->road_debug);
            map_world_accumulate_area_debug(&world->debug.area, &region->detail_renderer->area_debug);
        }
        world->debug.ormap_stats_valid = true;
        world->debug.detail_ms +=
            (double)(SDL_GetTicksNS() - debug_started_ns) / 1000000.0;
    }
}


bool openride_map_world_needs_followup_frame(
    const OpenRideMapWorld *world)
{
    if (!world) return false;
    for (size_t i = 0U; i < world->region_count; ++i) {
        const OpenRideMapWorldRegion *region = &world->regions[i];
        if (!region->detail_visible || !region->detail_renderer) continue;
        if (openride_ormap_renderer_needs_followup_frame(
                region->detail_renderer)) {
            return true;
        }
    }
    return false;
}

size_t openride_map_world_region_count(const OpenRideMapWorld *world)
{
    return world ? world->region_count : 0U;
}
