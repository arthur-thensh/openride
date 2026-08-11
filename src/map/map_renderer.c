#include "map/map_renderer.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define OPENRIDE_TILE_SIZE 256.0

static int wrap_tile_x(int x, int tile_count)
{
    int wrapped = x % tile_count;
    if (wrapped < 0) wrapped += tile_count;
    return wrapped;
}

static OpenRideTileCacheEntry *find_cache_entry(OpenRideMapRenderer *map_renderer,
                                                 int zoom,
                                                 int x,
                                                 int y)
{
    for (size_t i = 0; i < OPENRIDE_TILE_CACHE_CAPACITY; ++i) {
        OpenRideTileCacheEntry *entry = &map_renderer->cache[i];
        if (entry->occupied && entry->zoom == zoom && entry->x == x && entry->y == y) {
            return entry;
        }
    }

    return NULL;
}

static OpenRideTileCacheEntry *choose_cache_slot(OpenRideMapRenderer *map_renderer)
{
    OpenRideTileCacheEntry *oldest = &map_renderer->cache[0];

    for (size_t i = 0; i < OPENRIDE_TILE_CACHE_CAPACITY; ++i) {
        OpenRideTileCacheEntry *entry = &map_renderer->cache[i];

        if (!entry->occupied) {
            return entry;
        }

        if (entry->last_used < oldest->last_used) {
            oldest = entry;
        }
    }

    if (oldest->texture) {
        SDL_DestroyTexture(oldest->texture);
        oldest->texture = NULL;
    }

    oldest->occupied = false;
    return oldest;
}

static SDL_Texture *load_tile_texture(OpenRideMapRenderer *map_renderer,
                                      int zoom,
                                      int x,
                                      int y)
{
    OpenRideTileCacheEntry *entry = find_cache_entry(map_renderer, zoom, x, y);
    if (entry) {
        entry->last_used = map_renderer->frame_counter;
        return entry->texture;
    }

    entry = choose_cache_slot(map_renderer);
    entry->occupied = true;
    entry->zoom = zoom;
    entry->x = x;
    entry->y = y;
    entry->last_used = map_renderer->frame_counter;
    entry->texture = NULL;

    OpenRideTileData tile = {0};
    char error[256] = {0};

    if (!openride_mbtiles_load_tile(map_renderer->map,
                                    zoom,
                                    x,
                                    y,
                                    &tile,
                                    error,
                                    sizeof(error))) {
        return NULL;
    }

    SDL_IOStream *io = SDL_IOFromConstMem(tile.bytes, tile.size);
    if (!io) {
        SDL_Log("Unable to create tile IO stream z=%d x=%d y=%d: %s",
                zoom, x, y, SDL_GetError());
        openride_tile_data_free(&tile);
        return NULL;
    }

    SDL_Surface *surface = SDL_LoadSurface_IO(io, true);
    if (!surface) {
        SDL_Log("Unable to decode tile z=%d x=%d y=%d: %s",
                zoom, x, y, SDL_GetError());
        openride_tile_data_free(&tile);
        return NULL;
    }

    entry->texture = SDL_CreateTextureFromSurface(map_renderer->renderer, surface);
    SDL_DestroySurface(surface);
    openride_tile_data_free(&tile);

    if (!entry->texture) {
        SDL_Log("Unable to create tile texture z=%d x=%d y=%d: %s",
                zoom, x, y, SDL_GetError());
        return NULL;
    }

    SDL_SetTextureScaleMode(entry->texture, SDL_SCALEMODE_LINEAR);
    return entry->texture;
}

bool openride_map_renderer_init(OpenRideMapRenderer *map_renderer,
                                SDL_Renderer *renderer,
                                OpenRideMBTiles *map)
{
    if (!map_renderer || !renderer || !map) return false;

    memset(map_renderer, 0, sizeof(*map_renderer));
    map_renderer->renderer = renderer;
    map_renderer->map = map;
    return true;
}

void openride_map_renderer_destroy(OpenRideMapRenderer *map_renderer)
{
    if (!map_renderer) return;

    for (size_t i = 0; i < OPENRIDE_TILE_CACHE_CAPACITY; ++i) {
        if (map_renderer->cache[i].texture) {
            SDL_DestroyTexture(map_renderer->cache[i].texture);
            map_renderer->cache[i].texture = NULL;
        }
    }

    memset(map_renderer, 0, sizeof(*map_renderer));
}

void openride_map_renderer_draw(OpenRideMapRenderer *map_renderer,
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

    for (int ty = first_y; ty <= last_y; ++ty) {
        if (ty < 0 || ty >= tile_count) continue;

        for (int tx = first_x; tx <= last_x; ++tx) {
            const int query_x = wrap_tile_x(tx, tile_count);
            SDL_Texture *texture = load_tile_texture(map_renderer,
                                                     tile_zoom,
                                                     query_x,
                                                     ty);

            SDL_FRect destination = {
                .x = (float)((double)tx * tile_screen_size - left_world),
                .y = (float)((double)ty * tile_screen_size - top_world),
                .w = (float)(tile_screen_size + 0.5),
                .h = (float)(tile_screen_size + 0.5)
            };

            if (texture) {
                SDL_RenderTexture(map_renderer->renderer, texture, NULL, &destination);
            } else {
                SDL_SetRenderDrawColor(map_renderer->renderer, 38, 42, 48, SDL_ALPHA_OPAQUE);
                SDL_RenderFillRect(map_renderer->renderer, &destination);
                SDL_SetRenderDrawColor(map_renderer->renderer, 66, 71, 80, SDL_ALPHA_OPAQUE);
                SDL_RenderRect(map_renderer->renderer, &destination);
            }
        }
    }
}
