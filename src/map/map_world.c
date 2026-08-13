#include "map/map_world.h"

#include "openride/ormap.h"
#include "openride/region_manager.h"
#include "openride/routing_graph.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORLD_ROAD_ZOOM 10
#define WORLD_MAX_LINE_WIDTH 3
#define WORLD_GEOMETRY_BATCH_VERTEX_LIMIT 8192U
#define WORLD_GEOMETRY_BATCH_INDEX_LIMIT 12288U

typedef struct WorldLine {
    double lat1;
    double lon1;
    double lat2;
    double lon2;
    uint8_t kind;
} WorldLine;

typedef struct WorldLineArray {
    WorldLine *items;
    size_t count;
    size_t capacity;
} WorldLineArray;

typedef struct WorldGeometryBatch {
    uint32_t vertex_count;
    uint32_t index_count;
} WorldGeometryBatch;

typedef struct OpenRideMapWorldRegion {
    const OpenRideRegionDefinition *definition;
    OpenRideORMapMetadata metadata;
    WorldLineArray boundary;
    WorldLineArray roads;
    WorldLineArray waterways;
} OpenRideMapWorldRegion;

struct OpenRideMapWorld {
    SDL_Renderer *renderer;
    OpenRideMapWorldRegion *regions;
    size_t region_count;
    SDL_Vertex *vertices;
    int *indices;
    uint32_t vertex_capacity;
    uint32_t index_capacity;
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "unknown error");
}

static void line_array_destroy(WorldLineArray *array)
{
    if (!array) return;
    free(array->items);
    memset(array, 0, sizeof(*array));
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
    WorldLine *line = &array->items[array->count++];
    line->lat1 = lat1;
    line->lon1 = lon1;
    line->lat2 = lat2;
    line->lon2 = lon2;
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
            if (!line_array_append(&region->roads,
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
    line_array_destroy(&region->boundary);
    line_array_destroy(&region->roads);
    line_array_destroy(&region->waterways);
    memset(region, 0, sizeof(*region));
}

static void destroy_regions(OpenRideMapWorldRegion *regions, size_t count)
{
    if (!regions) return;
    for (size_t i = 0U; i < count; ++i) world_region_destroy(&regions[i]);
    free(regions);
}

static bool build_world_region(const OpenRideRegionDefinition *definition,
                               const char *ormap_path,
                               const char *poly_path,
                               OpenRideMapWorldRegion *out,
                               char *error,
                               size_t error_size)
{
    memset(out, 0, sizeof(*out));
    out->definition = definition;
    OpenRideORMap *map = openride_ormap_open(ormap_path, error, error_size);
    if (!map) return false;
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(map);
    if (!metadata) {
        openride_ormap_close(map);
        set_error(error, error_size, "map-world region has no metadata");
        return false;
    }
    out->metadata = *metadata;
    const bool ok = load_major_roads(map, out, error, error_size)
        && load_major_waterways(map, out, error, error_size)
        && load_poly_boundary(poly_path, &out->boundary, error, error_size);
    openride_ormap_close(map);
    if (!ok) {
        world_region_destroy(out);
        return false;
    }
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
        if (!build_world_region(definition,
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
    WorldGeometryBatch batch = {0};
    for (size_t i = 0U; i < array->count; ++i) {
        const WorldLine *line = &array->items[i];
        if (filter_kind && line->kind != kind_filter) continue;
        const OpenRidePointD a = openride_geo_to_screen(camera,
                                                        line->lat1,
                                                        line->lon1,
                                                        viewport_width,
                                                        viewport_height);
        const OpenRidePointD b = openride_geo_to_screen(camera,
                                                        line->lat2,
                                                        line->lon2,
                                                        viewport_width,
                                                        viewport_height);
        if (!line_maybe_visible(a, b, viewport_width, viewport_height)) continue;
        if (!geometry_batch_line(world, &batch, a, b, line_width, color)) break;
    }
    geometry_batch_flush(world, &batch);
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

    SDL_SetRenderDrawColor(world->renderer,
                           palette->label_halo.r,
                           palette->label_halo.g,
                           palette->label_halo.b,
                           235U);
    SDL_RenderDebugText(world->renderer, x - 1.0f, y, name);
    SDL_RenderDebugText(world->renderer, x + 1.0f, y, name);
    SDL_RenderDebugText(world->renderer, x, y - 1.0f, name);
    SDL_RenderDebugText(world->renderer, x, y + 1.0f, name);

    SDL_SetRenderDrawColor(world->renderer,
                           palette->label.r,
                           palette->label.g,
                           palette->label.b,
                           245U);
    SDL_RenderDebugText(world->renderer, x, y, name);
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

    const OpenRideMapPalette palette = openride_map_palette(style);
    for (size_t i = 0U; i < world->region_count; ++i) {
        const OpenRideMapWorldRegion *region = &world->regions[i];
        if (skip_region_id && region->definition
            && strcmp(skip_region_id, region->definition->id) == 0) {
            continue;
        }

        /* Coverage is an availability hint, not an administrative border. */
        OpenRideMapColor boundary = palette.boundary;
        boundary.a = camera->zoom < OPENRIDE_MAP_WORLD_DETAIL_ZOOM ? 78U : 52U;
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
         * The current .ormap distinguishes river/canal but does not rank
         * rivers by importance. Delay hydrography at low zoom rather than
         * rendering every river at z6.
         */
        if (camera->zoom >= 7.25) {
            OpenRideMapColor water = palette.water_line;
            water.a = camera->zoom < 8.75 ? 155U : 195U;
            draw_line_array(world,
                            camera,
                            &region->waterways,
                            water,
                            camera->zoom >= 9.0 ? 2 : 1,
                            viewport_width,
                            viewport_height,
                            OPENRIDE_ORMAP_WATERWAY_RIVER,
                            true);

            if (camera->zoom >= 8.75) {
                water.a = 160U;
                draw_line_array(world,
                                camera,
                                &region->waterways,
                                water,
                                1,
                                viewport_width,
                                viewport_height,
                                OPENRIDE_ORMAP_WATERWAY_CANAL,
                                true);
            }
        }

        /* Progressive road hierarchy: motorway -> trunk -> primary. */
        for (int road_class = OPENRIDE_ROAD_MOTORWAY;
             road_class <= OPENRIDE_ROAD_PRIMARY;
             ++road_class) {
            if (road_class == OPENRIDE_ROAD_TRUNK && camera->zoom < 7.0) continue;
            if (road_class == OPENRIDE_ROAD_PRIMARY && camera->zoom < 8.25) continue;

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

            int line_width = paint.width;
            if (camera->zoom < 8.0 && line_width > 2) line_width = 2;
            if (road_class == OPENRIDE_ROAD_PRIMARY) line_width = 1;

            draw_line_array(world,
                            camera,
                            &region->roads,
                            color,
                            line_width,
                            viewport_width,
                            viewport_height,
                            (uint8_t)road_class,
                            true);
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
}


size_t openride_map_world_region_count(const OpenRideMapWorld *world)
{
    return world ? world->region_count : 0U;
}
