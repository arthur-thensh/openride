#include "map/vector_map_renderer.h"

#include "openride/mvt.h"
#include "openride/map_style.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define OPENRIDE_TILE_SIZE 256.0
#define OPENRIDE_MAX_UNCOMPRESSED_TILE (32U * 1024U * 1024U)
#define OPENRIDE_MAX_LABEL_CANDIDATES 2048
#define OPENRIDE_MAX_PLACED_LABELS 384
#define OPENRIDE_LABEL_NAME_CAPACITY 96

typedef struct LabelCandidate {
    char name[OPENRIDE_LABEL_NAME_CAPACITY];
    float x;
    float y;
    float width;
    float height;
    int priority;
} LabelCandidate;

typedef struct LabelLayout {
    LabelCandidate candidates[OPENRIDE_MAX_LABEL_CANDIDATES];
    size_t candidate_count;
    SDL_FRect placed[OPENRIDE_MAX_PLACED_LABELS];
    size_t placed_count;
    int viewport_width;
    int viewport_height;
} LabelLayout;

typedef struct DrawContext {
    SDL_Renderer *renderer;
    double tile_left;
    double tile_top;
    double tile_size;
    double camera_zoom;
    OpenRideMapStyle style;
    int pass;
    LabelLayout *labels;
} DrawContext;

typedef struct GeometryDrawState {
    DrawContext *draw;
    uint32_t extent;
    float r;
    float g;
    float b;
    float a;
    float casing_r;
    float casing_g;
    float casing_b;
    int width;
    int casing_width;
    bool dashed;

    bool has_previous;
    float previous_x;
    float previous_y;
    float path_start_x;
    float path_start_y;
} GeometryDrawState;

typedef struct PointCapture {
    DrawContext *draw;
    uint32_t extent;
    bool found;
    float x;
    float y;
} PointCapture;

static int wrap_tile_x(int x, int tile_count)
{
    int wrapped = x % tile_count;
    if (wrapped < 0) wrapped += tile_count;
    return wrapped;
}

static OpenRideVectorTileCacheEntry *find_cache_entry(OpenRideVectorMapRenderer *renderer,
                                                       int zoom,
                                                       int x,
                                                       int y)
{
    for (size_t i = 0; i < OPENRIDE_VECTOR_TILE_CACHE_CAPACITY; ++i) {
        OpenRideVectorTileCacheEntry *entry = &renderer->cache[i];
        if (entry->occupied && entry->zoom == zoom && entry->x == x && entry->y == y) {
            return entry;
        }
    }
    return NULL;
}

static OpenRideVectorTileCacheEntry *choose_cache_slot(OpenRideVectorMapRenderer *renderer)
{
    OpenRideVectorTileCacheEntry *oldest = &renderer->cache[0];

    for (size_t i = 0; i < OPENRIDE_VECTOR_TILE_CACHE_CAPACITY; ++i) {
        OpenRideVectorTileCacheEntry *entry = &renderer->cache[i];
        if (!entry->occupied) return entry;
        if (entry->last_used < oldest->last_used) oldest = entry;
    }

    free(oldest->bytes);
    memset(oldest, 0, sizeof(*oldest));
    return oldest;
}

static bool looks_compressed(const unsigned char *data, size_t size)
{
    if (!data || size < 2) return false;
    if (data[0] == 0x1f && data[1] == 0x8b) return true; /* gzip */
    if (data[0] == 0x78) return true;                   /* common zlib header */
    return false;
}

static bool decompress_tile(const unsigned char *input,
                            size_t input_size,
                            unsigned char **output,
                            size_t *output_size)
{
    if (!input || !output || !output_size) return false;

    *output = NULL;
    *output_size = 0;

    if (!looks_compressed(input, input_size)) {
        unsigned char *copy = malloc(input_size);
        if (!copy) return false;
        memcpy(copy, input, input_size);
        *output = copy;
        *output_size = input_size;
        return true;
    }

    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = (Bytef *)input;
    stream.avail_in = (uInt)input_size;

    if (inflateInit2(&stream, 15 + 32) != Z_OK) return false;

    size_t capacity = input_size * 4;
    if (capacity < 64U * 1024U) capacity = 64U * 1024U;
    if (capacity > OPENRIDE_MAX_UNCOMPRESSED_TILE) capacity = OPENRIDE_MAX_UNCOMPRESSED_TILE;

    unsigned char *buffer = malloc(capacity);
    if (!buffer) {
        inflateEnd(&stream);
        return false;
    }

    int status = Z_OK;
    while (status == Z_OK) {
        if (stream.total_out >= capacity) {
            if (capacity >= OPENRIDE_MAX_UNCOMPRESSED_TILE) {
                free(buffer);
                inflateEnd(&stream);
                return false;
            }

            size_t grown_capacity = capacity * 2;
            if (grown_capacity > OPENRIDE_MAX_UNCOMPRESSED_TILE) {
                grown_capacity = OPENRIDE_MAX_UNCOMPRESSED_TILE;
            }

            unsigned char *grown = realloc(buffer, grown_capacity);
            if (!grown) {
                free(buffer);
                inflateEnd(&stream);
                return false;
            }

            buffer = grown;
            capacity = grown_capacity;
        }

        stream.next_out = buffer + stream.total_out;
        stream.avail_out = (uInt)(capacity - stream.total_out);
        status = inflate(&stream, Z_NO_FLUSH);
    }

    if (status != Z_STREAM_END) {
        free(buffer);
        inflateEnd(&stream);
        return false;
    }

    *output_size = (size_t)stream.total_out;
    *output = buffer;
    inflateEnd(&stream);
    return true;
}

static OpenRideVectorTileCacheEntry *load_tile(OpenRideVectorMapRenderer *renderer,
                                                int zoom,
                                                int x,
                                                int y)
{
    OpenRideVectorTileCacheEntry *entry = find_cache_entry(renderer, zoom, x, y);
    if (entry) {
        entry->last_used = renderer->frame_counter;
        return entry;
    }

    OpenRideTileData tile = {0};
    char error[256] = {0};
    if (!openride_mbtiles_load_tile(renderer->map,
                                    zoom,
                                    x,
                                    y,
                                    &tile,
                                    error,
                                    sizeof(error))) {
        return NULL;
    }

    unsigned char *decoded = NULL;
    size_t decoded_size = 0;
    if (!decompress_tile(tile.bytes, tile.size, &decoded, &decoded_size)) {
        SDL_Log("Unable to decompress MVT tile z=%d x=%d y=%d", zoom, x, y);
        openride_tile_data_free(&tile);
        return NULL;
    }
    openride_tile_data_free(&tile);

    entry = choose_cache_slot(renderer);
    entry->occupied = true;
    entry->zoom = zoom;
    entry->x = x;
    entry->y = y;
    entry->bytes = decoded;
    entry->size = decoded_size;
    entry->last_used = renderer->frame_counter;
    return entry;
}

static void set_draw_color(SDL_Renderer *renderer, float r, float g, float b, float a)
{
    SDL_SetRenderDrawColor(renderer,
                           (Uint8)r,
                           (Uint8)g,
                           (Uint8)b,
                           (Uint8)a);
}

static void draw_thick_line(SDL_Renderer *renderer,
                            float x1,
                            float y1,
                            float x2,
                            float y2,
                            int width)
{
    if (width <= 1) {
        SDL_RenderLine(renderer, x1, y1, x2, y2);
        return;
    }

    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.001f) return;

    const float nx = -dy / length;
    const float ny = dx / length;
    const float start = -0.5f * (float)(width - 1);

    for (int i = 0; i < width; ++i) {
        const float offset = start + (float)i;
        SDL_RenderLine(renderer,
                       x1 + nx * offset,
                       y1 + ny * offset,
                       x2 + nx * offset,
                       y2 + ny * offset);
    }
}

static void draw_dashed_line(SDL_Renderer *renderer,
                             float x1,
                             float y1,
                             float x2,
                             float y2,
                             int width)
{
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.001f) return;

    const float dash = 7.0f;
    const float gap = 5.0f;
    float position = 0.0f;

    while (position < length) {
        const float end = fminf(position + dash, length);
        const float t1 = position / length;
        const float t2 = end / length;
        draw_thick_line(renderer,
                        x1 + dx * t1,
                        y1 + dy * t1,
                        x1 + dx * t2,
                        y1 + dy * t2,
                        width);
        position += dash + gap;
    }
}

static bool style_street(const OpenRideMVTFeatureView *feature,
                         GeometryDrawState *state)
{
    const char *kind = openride_mvt_get_string(feature, "kind");
    bool rail = false;
    (void)openride_mvt_get_bool(feature, "rail", &rail);

    OpenRideMapRoadPaint paint;
    if (!openride_map_road_paint(state->draw->style,
                                 kind,
                                 rail,
                                 state->draw->camera_zoom,
                                 &paint)) {
        return false;
    }

    state->r = (float)paint.line.r;
    state->g = (float)paint.line.g;
    state->b = (float)paint.line.b;
    state->a = (float)paint.line.a;
    state->casing_r = (float)paint.casing.r;
    state->casing_g = (float)paint.casing.g;
    state->casing_b = (float)paint.casing.b;
    state->width = paint.width;
    state->casing_width = paint.casing_width;
    state->dashed = paint.dashed;
    return true;
}

static bool draw_geometry_callback(OpenRideMVTGeometryCommand command,
                                   int32_t x,
                                   int32_t y,
                                   void *user_data)
{
    GeometryDrawState *state = (GeometryDrawState *)user_data;
    const float sx = (float)(state->draw->tile_left
                    + ((double)x / (double)state->extent) * state->draw->tile_size);
    const float sy = (float)(state->draw->tile_top
                    + ((double)y / (double)state->extent) * state->draw->tile_size);

    if (command == OPENRIDE_MVT_MOVE_TO) {
        state->has_previous = true;
        state->previous_x = sx;
        state->previous_y = sy;
        state->path_start_x = sx;
        state->path_start_y = sy;
        return true;
    }

    if (command == OPENRIDE_MVT_LINE_TO && state->has_previous) {
        if (state->casing_width > state->width) {
            set_draw_color(state->draw->renderer,
                           state->casing_r,
                           state->casing_g,
                           state->casing_b,
                           255.0f);
            draw_thick_line(state->draw->renderer,
                            state->previous_x,
                            state->previous_y,
                            sx,
                            sy,
                            state->casing_width);
        }

        set_draw_color(state->draw->renderer, state->r, state->g, state->b, state->a);
        if (state->dashed) {
            draw_dashed_line(state->draw->renderer,
                             state->previous_x,
                             state->previous_y,
                             sx,
                             sy,
                             state->width);
        } else {
            draw_thick_line(state->draw->renderer,
                            state->previous_x,
                            state->previous_y,
                            sx,
                            sy,
                            state->width);
        }

        state->previous_x = sx;
        state->previous_y = sy;
        return true;
    }

    if (command == OPENRIDE_MVT_CLOSE_PATH && state->has_previous) {
        set_draw_color(state->draw->renderer, state->r, state->g, state->b, state->a);
        draw_thick_line(state->draw->renderer,
                        state->previous_x,
                        state->previous_y,
                        state->path_start_x,
                        state->path_start_y,
                        state->width);
        state->previous_x = state->path_start_x;
        state->previous_y = state->path_start_y;
    }

    return true;
}

static bool capture_point_callback(OpenRideMVTGeometryCommand command,
                                   int32_t x,
                                   int32_t y,
                                   void *user_data)
{
    PointCapture *capture = (PointCapture *)user_data;
    if (capture->found || command != OPENRIDE_MVT_MOVE_TO) return true;

    capture->x = (float)(capture->draw->tile_left
               + ((double)x / (double)capture->extent) * capture->draw->tile_size);
    capture->y = (float)(capture->draw->tile_top
               + ((double)y / (double)capture->extent) * capture->draw->tile_size);
    capture->found = true;
    return true;
}

static bool render_feature(const OpenRideMVTFeatureView *feature, void *user_data)
{
    DrawContext *draw = (DrawContext *)user_data;
    const char *layer = feature->layer_name;
    if (!layer) return true;

    if (draw->pass == 1) {
        if (strcmp(layer, "place_labels") != 0 ||
            feature->geometry_type != OPENRIDE_MVT_POINT ||
            !draw->labels) {
            return true;
        }

        const char *name = openride_mvt_get_string(feature, "name");
        if (!name || name[0] == '\0') return true;

        const char *kind = openride_mvt_get_string(feature, "kind");
        int64_t population = 0;
        (void)openride_mvt_get_int64(feature, "population", &population);

        if (!openride_map_place_label_visible(kind, population, draw->camera_zoom)) {
            return true;
        }

        PointCapture capture = {draw, feature->extent, false, 0.0f, 0.0f};
        if (!openride_mvt_visit_geometry(feature, capture_point_callback, &capture) ||
            !capture.found) {
            return true;
        }

        LabelLayout *labels = draw->labels;
        if (labels->candidate_count >= OPENRIDE_MAX_LABEL_CANDIDATES) return true;

        LabelCandidate *candidate = &labels->candidates[labels->candidate_count++];
        memset(candidate, 0, sizeof(*candidate));
        snprintf(candidate->name, sizeof(candidate->name), "%s", name);

        size_t visible_chars = strlen(candidate->name);
        if (visible_chars > 30U) visible_chars = 30U;
        candidate->width = (float)(visible_chars * 8U);
        if (candidate->width < 16.0f) candidate->width = 16.0f;
        candidate->height = 10.0f;
        candidate->x = capture.x - candidate->width * 0.5f;
        candidate->y = capture.y - 5.0f;
        candidate->priority = openride_map_place_label_priority(kind, population);
        return true;
    }

    GeometryDrawState state;
    memset(&state, 0, sizeof(state));
    state.draw = draw;
    state.extent = feature->extent ? feature->extent : 4096;
    state.a = 255.0f;
    state.width = 1;

    if (strcmp(layer, "streets") == 0 && feature->geometry_type == OPENRIDE_MVT_LINESTRING) {
        const char *kind = openride_mvt_get_string(feature, "kind");
        bool rail = false;
        (void)openride_mvt_get_bool(feature, "rail", &rail);
        if (!rail && !openride_map_road_visible_for_style(draw->style, kind, draw->camera_zoom)) {
            return true;
        }

        if (style_street(feature, &state)) {
            (void)openride_mvt_visit_geometry(feature, draw_geometry_callback, &state);
        }
    } else if (strcmp(layer, "water_lines") == 0 && feature->geometry_type == OPENRIDE_MVT_LINESTRING) {
        const OpenRideMapPalette palette = openride_map_palette(draw->style);
        state.r = (float)palette.water_line.r;
        state.g = (float)palette.water_line.g;
        state.b = (float)palette.water_line.b;
        state.a = (float)palette.water_line.a;
        state.width = draw->style == OPENRIDE_MAP_STYLE_TOPO ? 2 : 1;
        (void)openride_mvt_visit_geometry(feature, draw_geometry_callback, &state);
    } else if (strcmp(layer, "water_polygons") == 0 && feature->geometry_type == OPENRIDE_MVT_POLYGON) {
        const OpenRideMapPalette palette = openride_map_palette(draw->style);
        state.r = (float)palette.water.r;
        state.g = (float)palette.water.g;
        state.b = (float)palette.water.b;
        state.a = (float)palette.water.a;
        state.width = 2;
        (void)openride_mvt_visit_geometry(feature, draw_geometry_callback, &state);
    } else if (strcmp(layer, "boundaries") == 0 && feature->geometry_type == OPENRIDE_MVT_LINESTRING) {
        const OpenRideMapPalette palette = openride_map_palette(draw->style);
        state.r = (float)palette.boundary.r;
        state.g = (float)palette.boundary.g;
        state.b = (float)palette.boundary.b;
        state.a = (float)palette.boundary.a;
        state.width = 1;
        state.dashed = true;
        (void)openride_mvt_visit_geometry(feature, draw_geometry_callback, &state);
    } else if (strcmp(layer, "buildings") == 0 && feature->geometry_type == OPENRIDE_MVT_POLYGON &&
               openride_map_buildings_visible(draw->style, draw->camera_zoom)) {
        const OpenRideMapPalette palette = openride_map_palette(draw->style);
        state.r = (float)palette.building.r;
        state.g = (float)palette.building.g;
        state.b = (float)palette.building.b;
        state.a = (float)palette.building.a;
        state.width = 1;
        (void)openride_mvt_visit_geometry(feature, draw_geometry_callback, &state);
    }

    return true;
}

static int compare_label_candidates(const void *a, const void *b)
{
    const LabelCandidate *left = (const LabelCandidate *)a;
    const LabelCandidate *right = (const LabelCandidate *)b;
    if (left->priority < right->priority) return 1;
    if (left->priority > right->priority) return -1;
    return strcmp(left->name, right->name);
}

static bool rects_overlap(const SDL_FRect *a, const SDL_FRect *b)
{
    return a->x < b->x + b->w &&
           a->x + a->w > b->x &&
           a->y < b->y + b->h &&
           a->y + a->h > b->y;
}

static bool label_would_overlap(const LabelLayout *labels, const SDL_FRect *box)
{
    for (size_t i = 0; i < labels->placed_count; ++i) {
        if (rects_overlap(&labels->placed[i], box)) return true;
    }
    return false;
}

static void draw_text_with_halo(SDL_Renderer *renderer,
                                float x,
                                float y,
                                const char *text,
                                OpenRideMapStyle style)
{
    const OpenRideMapPalette palette = openride_map_palette(style);
    SDL_SetRenderDrawColor(renderer,
                           palette.label_halo.r,
                           palette.label_halo.g,
                           palette.label_halo.b,
                           palette.label_halo.a);
    SDL_RenderDebugText(renderer, x - 1.0f, y, text);
    SDL_RenderDebugText(renderer, x + 1.0f, y, text);
    SDL_RenderDebugText(renderer, x, y - 1.0f, text);
    SDL_RenderDebugText(renderer, x, y + 1.0f, text);

    SDL_SetRenderDrawColor(renderer,
                           palette.label.r,
                           palette.label.g,
                           palette.label.b,
                           palette.label.a);
    SDL_RenderDebugText(renderer, x, y, text);
}

static void draw_collected_labels(SDL_Renderer *renderer,
                                  LabelLayout *labels,
                                  OpenRideMapStyle style)
{
    if (!renderer || !labels || labels->candidate_count == 0) return;

    qsort(labels->candidates,
          labels->candidate_count,
          sizeof(labels->candidates[0]),
          compare_label_candidates);

    labels->placed_count = 0;

    for (size_t i = 0; i < labels->candidate_count; ++i) {
        const LabelCandidate *candidate = &labels->candidates[i];
        const float margin_x = 5.0f;
        const float margin_y = 3.0f;
        SDL_FRect collision = {
            candidate->x - margin_x,
            candidate->y - margin_y,
            candidate->width + margin_x * 2.0f,
            candidate->height + margin_y * 2.0f
        };

        if (collision.x + collision.w < 0.0f ||
            collision.y + collision.h < 0.0f ||
            collision.x > (float)labels->viewport_width ||
            collision.y > (float)labels->viewport_height) {
            continue;
        }

        if (label_would_overlap(labels, &collision)) continue;
        if (labels->placed_count >= OPENRIDE_MAX_PLACED_LABELS) break;

        labels->placed[labels->placed_count++] = collision;
        draw_text_with_halo(renderer, candidate->x, candidate->y, candidate->name, style);
    }
}

static void draw_one_tile(OpenRideVectorMapRenderer *renderer,
                          OpenRideVectorTileCacheEntry *tile,
                          double left,
                          double top,
                          double tile_size,
                          double camera_zoom,
                          int pass,
                          LabelLayout *labels)
{
    if (!tile || !tile->bytes || tile->size == 0) return;

    DrawContext draw = {
        .renderer = renderer->renderer,
        .tile_left = left,
        .tile_top = top,
        .tile_size = tile_size,
        .camera_zoom = camera_zoom,
        .style = renderer->style,
        .pass = pass,
        .labels = labels
    };

    char error[256] = {0};
    if (!openride_mvt_visit_tile(tile->bytes,
                                 tile->size,
                                 render_feature,
                                 &draw,
                                 error,
                                 sizeof(error))) {
        SDL_Log("MVT parse error: %s", error);
    }
}

bool openride_vector_map_renderer_init(OpenRideVectorMapRenderer *map_renderer,
                                       SDL_Renderer *renderer,
                                       OpenRideMBTiles *map)
{
    if (!map_renderer || !renderer || !map) return false;
    memset(map_renderer, 0, sizeof(*map_renderer));
    map_renderer->renderer = renderer;
    map_renderer->map = map;
    map_renderer->style = OPENRIDE_MAP_STYLE_TRAIL;
    return true;
}

void openride_vector_map_renderer_set_style(OpenRideVectorMapRenderer *map_renderer,
                                            OpenRideMapStyle style)
{
    if (!map_renderer) return;
    map_renderer->style = style;
}

OpenRideMapStyle openride_vector_map_renderer_style(const OpenRideVectorMapRenderer *map_renderer)
{
    return map_renderer ? map_renderer->style : OPENRIDE_MAP_STYLE_TRAIL;
}

void openride_vector_map_renderer_destroy(OpenRideVectorMapRenderer *map_renderer)
{
    if (!map_renderer) return;

    for (size_t i = 0; i < OPENRIDE_VECTOR_TILE_CACHE_CAPACITY; ++i) {
        free(map_renderer->cache[i].bytes);
    }

    memset(map_renderer, 0, sizeof(*map_renderer));
}

void openride_vector_map_renderer_draw(OpenRideVectorMapRenderer *map_renderer,
                                       const OpenRideMapCamera *camera,
                                       int viewport_width,
                                       int viewport_height)
{
    if (!map_renderer || !camera || viewport_width <= 0 || viewport_height <= 0) return;

    const OpenRideMBTilesMetadata *metadata = openride_mbtiles_metadata(map_renderer->map);
    if (!metadata) return;

    map_renderer->frame_counter += 1;

    int tile_zoom = (int)floor(camera->zoom);
    if (tile_zoom < metadata->min_zoom) tile_zoom = metadata->min_zoom;
    if (tile_zoom > metadata->max_zoom) tile_zoom = metadata->max_zoom;
    if (tile_zoom < 0 || tile_zoom > 30) return;

    const int tile_count = 1 << tile_zoom;
    const double scale = pow(2.0, camera->zoom - (double)tile_zoom);
    const double tile_screen_size = OPENRIDE_TILE_SIZE * scale;

    const OpenRidePointD center = openride_mercator_forward(camera->center_lat,
                                                             camera->center_lon);
    const double world_size = tile_screen_size * (double)tile_count;
    const double center_world_x = center.x * world_size;
    const double center_world_y = center.y * world_size;
    const double left_world = center_world_x - (double)viewport_width * 0.5;
    const double top_world = center_world_y - (double)viewport_height * 0.5;
    const double right_world = center_world_x + (double)viewport_width * 0.5;
    const double bottom_world = center_world_y + (double)viewport_height * 0.5;

    const int first_x = (int)floor(left_world / tile_screen_size);
    const int last_x = (int)floor(right_world / tile_screen_size);
    const int first_y = (int)floor(top_world / tile_screen_size);
    const int last_y = (int)floor(bottom_world / tile_screen_size);

    const OpenRideMapPalette palette = openride_map_palette(map_renderer->style);
    SDL_SetRenderDrawColor(map_renderer->renderer,
                           palette.background.r,
                           palette.background.g,
                           palette.background.b,
                           palette.background.a);
    SDL_RenderClear(map_renderer->renderer);

    LabelLayout labels;
    memset(&labels, 0, sizeof(labels));
    labels.viewport_width = viewport_width;
    labels.viewport_height = viewport_height;

    /* Pass 0: geographic geometry. Pass 1: collect labels globally. */
    for (int pass = 0; pass <= 1; ++pass) {
        for (int ty = first_y; ty <= last_y; ++ty) {
            if (ty < 0 || ty >= tile_count) continue;

            for (int tx = first_x; tx <= last_x; ++tx) {
                const int query_x = wrap_tile_x(tx, tile_count);
                OpenRideVectorTileCacheEntry *tile = load_tile(map_renderer,
                                                                tile_zoom,
                                                                query_x,
                                                                ty);
                if (!tile) continue;

                const double left = (double)tx * tile_screen_size - left_world;
                const double top = (double)ty * tile_screen_size - top_world;

                draw_one_tile(map_renderer,
                              tile,
                              left,
                              top,
                              tile_screen_size,
                              camera->zoom,
                              pass,
                              &labels);
            }
        }
    }

    draw_collected_labels(map_renderer->renderer, &labels, map_renderer->style);
}
