#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "map/map_renderer.h"
#include "map/vector_map_renderer.h"
#include "openride/map_camera.h"
#include "openride/mbtiles.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static double clampd(double value, double min_value, double max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static bool file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    fclose(file);
    return true;
}

static bool is_vector_map(const OpenRideMBTilesMetadata *metadata)
{
    if (!metadata) return false;
    return strcmp(metadata->format, "pbf") == 0
        || strcmp(metadata->format, "mvt") == 0
        || strcmp(metadata->format, "application/x-protobuf") == 0;
}

static const char *default_map_path(void)
{
    static const char *real_map = "data/maps/nord-pas-de-calais-shortbread.mbtiles";
    static const char *demo_map = "data/maps/demo.mbtiles";

    return file_exists(real_map) ? real_map : demo_map;
}

static void draw_center_marker(SDL_Renderer *renderer, int width, int height)
{
    const float cx = (float)width * 0.5f;
    const float cy = (float)height * 0.5f;

    SDL_SetRenderDrawColor(renderer, 20, 20, 20, SDL_ALPHA_OPAQUE);
    SDL_RenderLine(renderer, cx - 11.0f, cy, cx + 11.0f, cy);
    SDL_RenderLine(renderer, cx, cy - 11.0f, cx, cy + 11.0f);

    SDL_SetRenderDrawColor(renderer, 245, 245, 245, SDL_ALPHA_OPAQUE);
    SDL_RenderLine(renderer, cx - 9.0f, cy, cx + 9.0f, cy);
    SDL_RenderLine(renderer, cx, cy - 9.0f, cx, cy + 9.0f);
}

static void draw_overlay(SDL_Renderer *renderer,
                         const OpenRideMapCamera *camera,
                         const OpenRideMBTilesMetadata *metadata,
                         bool vector_map,
                         int viewport_width,
                         int viewport_height)
{
    (void)viewport_width;

    SDL_SetRenderDrawColor(renderer, 249, 249, 247, 225);
    SDL_FRect panel = {8.0f, 8.0f, 350.0f, 58.0f};
    SDL_RenderFillRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 34, 37, 40, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(renderer, 14.0f, 14.0f, "OpenRide v0.5.1 - OFFLINE");
    SDL_RenderDebugTextFormat(renderer,
                              14.0f,
                              28.0f,
                              "lat %.5f  lon %.5f  z %.1f",
                              camera->center_lat,
                              camera->center_lon,
                              camera->zoom);
    SDL_RenderDebugTextFormat(renderer,
                              14.0f,
                              42.0f,
                              "%s | drag mouse | wheel zoom | Esc",
                              vector_map ? "OSM vector" : "raster");

    if (metadata->attribution[0] != '\0' && viewport_height > 28) {
        size_t text_len = strlen(metadata->attribution);
        if (text_len > 90U) text_len = 90U;
        const float backing_width = (float)(text_len * 8U + 12U);
        SDL_FRect attribution_panel = {
            8.0f,
            (float)viewport_height - 24.0f,
            backing_width,
            16.0f
        };
        SDL_SetRenderDrawColor(renderer, 249, 249, 247, 210);
        SDL_RenderFillRect(renderer, &attribution_panel);
        SDL_SetRenderDrawColor(renderer, 65, 68, 70, 255);
        SDL_RenderDebugText(renderer,
                            14.0f,
                            (float)viewport_height - 20.0f,
                            metadata->attribution);
    }
}

static OpenRideMapCamera camera_from_metadata(const OpenRideMBTilesMetadata *metadata)
{
    OpenRideMapCamera camera = {
        .center_lat = 50.370800,
        .center_lon = 3.080200,
        .zoom = 12.0
    };

    if (!metadata) return camera;

    if (metadata->has_center) {
        camera.center_lat = metadata->center_lat;
        camera.center_lon = metadata->center_lon;
        camera.zoom = metadata->center_zoom;
    }

    camera.zoom = clampd(camera.zoom,
                         (double)metadata->min_zoom,
                         20.0);

    return camera;
}

int main(int argc, char **argv)
{
    const char *map_path = argc >= 2 ? argv[1] : default_map_path();
    char error[512] = {0};

    OpenRideMBTiles *map = openride_mbtiles_open(map_path, error, sizeof(error));
    if (!map) {
        fprintf(stderr, "Unable to open offline map: %s\n", map_path);
        fprintf(stderr, "Reason: %s\n", error[0] ? error : "unknown error");
        fprintf(stderr, "\nRun ./scripts/download_real_map.sh to install the real OSM map.\n");
        fprintf(stderr, "Usage: ./build/openride [path/to/map.mbtiles]\n");
        return 1;
    }

    const OpenRideMBTilesMetadata *metadata = openride_mbtiles_metadata(map);
    const bool vector_map = is_vector_map(metadata);
    OpenRideMapCamera camera = camera_from_metadata(metadata);

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    OpenRideMapRenderer raster_renderer;
    OpenRideVectorMapRenderer vector_renderer;
    bool renderer_initialized = false;
    bool running = true;
    bool dragging = false;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        openride_mbtiles_close(map);
        return 1;
    }

    if (!SDL_CreateWindowAndRenderer(
            "OpenRide - Offline map",
            1200,
            800,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
            &window,
            &renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        openride_mbtiles_close(map);
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    if (vector_map) {
        renderer_initialized = openride_vector_map_renderer_init(&vector_renderer, renderer, map);
    } else {
        renderer_initialized = openride_map_renderer_init(&raster_renderer, renderer, map);
    }

    if (!renderer_initialized) {
        SDL_Log("Unable to initialize offline map renderer");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        openride_mbtiles_close(map);
        SDL_Quit();
        return 1;
    }

    while (running) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            SDL_ConvertEventToRenderCoordinates(renderer, &event);

            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;

                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_ESCAPE) {
                        running = false;
                    }
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        dragging = true;
                    }
                    break;

                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        dragging = false;
                    }
                    break;

                case SDL_EVENT_MOUSE_MOTION:
                    if (dragging) {
                        openride_camera_pan(&camera,
                                            (double)event.motion.xrel,
                                            (double)event.motion.yrel);
                    }
                    break;

                case SDL_EVENT_MOUSE_WHEEL: {
                    int width = 0;
                    int height = 0;
                    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);

                    const double requested_delta = (double)event.wheel.y * 0.5;
                    const double max_zoom = vector_map ? 18.0 : (double)metadata->max_zoom;
                    const double target_zoom = clampd(
                        camera.zoom + requested_delta,
                        (double)metadata->min_zoom,
                        max_zoom);

                    openride_camera_zoom_at(&camera,
                                            target_zoom - camera.zoom,
                                            (double)event.wheel.mouse_x,
                                            (double)event.wheel.mouse_y,
                                            width,
                                            height);
                    break;
                }

                default:
                    break;
            }
        }

        int width = 0;
        int height = 0;
        if (!SDL_GetCurrentRenderOutputSize(renderer, &width, &height)) {
            SDL_Log("SDL_GetCurrentRenderOutputSize failed: %s", SDL_GetError());
            break;
        }

        if (vector_map) {
            openride_vector_map_renderer_draw(&vector_renderer, &camera, width, height);
        } else {
            SDL_SetRenderDrawColor(renderer, 28, 32, 38, SDL_ALPHA_OPAQUE);
            SDL_RenderClear(renderer);
            openride_map_renderer_draw(&raster_renderer, &camera, width, height);
        }

        draw_center_marker(renderer, width, height);
        draw_overlay(renderer, &camera, metadata, vector_map, width, height);
        SDL_RenderPresent(renderer);
    }

    if (vector_map) {
        openride_vector_map_renderer_destroy(&vector_renderer);
    } else {
        openride_map_renderer_destroy(&raster_renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    openride_mbtiles_close(map);
    SDL_Quit();

    return 0;
}
