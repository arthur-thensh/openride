#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "map/map_renderer.h"
#include "map/vector_map_renderer.h"
#include "openride/map_camera.h"
#include "openride/map_selection.h"
#include "openride/mbtiles.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define OPENRIDE_CLICK_DRAG_THRESHOLD 5.0
#define OPENRIDE_MARKER_HIT_RADIUS 26.0

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

static void draw_filled_circle(SDL_Renderer *renderer, float cx, float cy, float radius)
{
    const int r = (int)ceilf(radius);

    for (int y = -r; y <= r; ++y) {
        const float fy = (float)y;
        const float inside = radius * radius - fy * fy;
        if (inside < 0.0f) continue;

        const float dx = sqrtf(inside);
        SDL_RenderLine(renderer, cx - dx, cy + fy, cx + dx, cy + fy);
    }
}

static void draw_circle_outline(SDL_Renderer *renderer, float cx, float cy, float radius)
{
    const int segments = 48;
    float previous_x = cx + radius;
    float previous_y = cy;

    for (int i = 1; i <= segments; ++i) {
        const float angle = (float)(6.28318530717958647692 * (double)i / (double)segments);
        const float x = cx + cosf(angle) * radius;
        const float y = cy + sinf(angle) * radius;
        SDL_RenderLine(renderer, previous_x, previous_y, x, y);
        previous_x = x;
        previous_y = y;
    }
}

static void draw_thick_line(SDL_Renderer *renderer,
                            float x1,
                            float y1,
                            float x2,
                            float y2,
                            int width)
{
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = sqrtf(dx * dx + dy * dy);

    if (length < 0.001f || width <= 1) {
        SDL_RenderLine(renderer, x1, y1, x2, y2);
        return;
    }

    const float nx = -dy / length;
    const float ny = dx / length;
    const float half = ((float)width - 1.0f) * 0.5f;

    for (int i = 0; i < width; ++i) {
        const float offset = (float)i - half;
        SDL_RenderLine(renderer,
                       x1 + nx * offset,
                       y1 + ny * offset,
                       x2 + nx * offset,
                       y2 + ny * offset);
    }
}

static void draw_scaled_text(SDL_Renderer *renderer,
                             float x,
                             float y,
                             float scale,
                             const char *text)
{
    float old_scale_x = 1.0f;
    float old_scale_y = 1.0f;

    if (!text || scale <= 0.0f) return;
    if (!SDL_GetRenderScale(renderer, &old_scale_x, &old_scale_y)) return;

    if (SDL_SetRenderScale(renderer, old_scale_x * scale, old_scale_y * scale)) {
        SDL_RenderDebugText(renderer,
                            x / scale,
                            y / scale,
                            text);
        SDL_SetRenderScale(renderer, old_scale_x, old_scale_y);
    }
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

static OpenRidePointD marker_screen_position(const OpenRideMapCamera *camera,
                                             const OpenRideMapSelection *selection,
                                             OpenRideSelectionMarker marker,
                                             int viewport_width,
                                             int viewport_height)
{
    OpenRideGeoPosition position = {0};

    if (marker == OPENRIDE_MARKER_START) {
        position = selection->start;
    } else if (marker == OPENRIDE_MARKER_DESTINATION) {
        position = selection->destination;
    }

    return openride_geo_to_screen(camera,
                                  position.lat,
                                  position.lon,
                                  viewport_width,
                                  viewport_height);
}

static OpenRideSelectionMarker marker_at_screen(const OpenRideMapCamera *camera,
                                                const OpenRideMapSelection *selection,
                                                double x,
                                                double y,
                                                int viewport_width,
                                                int viewport_height)
{
    const double radius_sq = OPENRIDE_MARKER_HIT_RADIUS * OPENRIDE_MARKER_HIT_RADIUS;

    if (selection->has_destination) {
        const OpenRidePointD p = marker_screen_position(camera,
                                                        selection,
                                                        OPENRIDE_MARKER_DESTINATION,
                                                        viewport_width,
                                                        viewport_height);
        const double dx = x - p.x;
        const double dy = y - (p.y - 25.0);
        if (dx * dx + dy * dy <= radius_sq) {
            return OPENRIDE_MARKER_DESTINATION;
        }
    }

    if (selection->has_start) {
        const OpenRidePointD p = marker_screen_position(camera,
                                                        selection,
                                                        OPENRIDE_MARKER_START,
                                                        viewport_width,
                                                        viewport_height);
        const double dx = x - p.x;
        const double dy = y - (p.y - 25.0);
        if (dx * dx + dy * dy <= radius_sq) {
            return OPENRIDE_MARKER_START;
        }
    }

    return OPENRIDE_MARKER_NONE;
}

static void draw_marker(SDL_Renderer *renderer,
                        OpenRidePointD p,
                        OpenRideSelectionMarker marker)
{
    Uint8 r = 42;
    Uint8 g = 157;
    Uint8 b = 84;
    const char *label = "D";
    const float center_x = (float)p.x;
    const float center_y = (float)p.y - 25.0f;

    if (marker == OPENRIDE_MARKER_DESTINATION) {
        r = 214;
        g = 66;
        b = 57;
        label = "A";
    }

    /* Pin shadow and stem. The selected coordinate is the tip of the pin. */
    SDL_SetRenderDrawColor(renderer, 18, 22, 26, 100);
    draw_filled_circle(renderer, center_x + 2.0f, center_y + 3.0f, 16.0f);
    draw_thick_line(renderer,
                    center_x + 2.0f,
                    center_y + 11.0f,
                    center_x + 2.0f,
                    (float)p.y + 2.0f,
                    5);

    SDL_SetRenderDrawColor(renderer, 248, 248, 246, SDL_ALPHA_OPAQUE);
    draw_thick_line(renderer,
                    center_x,
                    center_y + 10.0f,
                    center_x,
                    (float)p.y,
                    5);
    draw_filled_circle(renderer, center_x, center_y, 15.0f);

    SDL_SetRenderDrawColor(renderer, r, g, b, SDL_ALPHA_OPAQUE);
    draw_filled_circle(renderer, center_x, center_y, 12.0f);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
    draw_circle_outline(renderer, center_x, center_y, 12.0f);
    SDL_RenderDebugText(renderer, center_x - 4.0f, center_y - 4.0f, label);
}

static void draw_selection(SDL_Renderer *renderer,
                           const OpenRideMapCamera *camera,
                           const OpenRideMapSelection *selection,
                           int viewport_width,
                           int viewport_height)
{
    OpenRidePointD start = {0};
    OpenRidePointD destination = {0};

    if (selection->has_start) {
        start = marker_screen_position(camera,
                                       selection,
                                       OPENRIDE_MARKER_START,
                                       viewport_width,
                                       viewport_height);
    }

    if (selection->has_destination) {
        destination = marker_screen_position(camera,
                                             selection,
                                             OPENRIDE_MARKER_DESTINATION,
                                             viewport_width,
                                             viewport_height);
    }

    if (selection->has_start && selection->has_destination) {
        SDL_SetRenderDrawColor(renderer, 248, 248, 246, 205);
        draw_thick_line(renderer,
                        (float)start.x,
                        (float)start.y,
                        (float)destination.x,
                        (float)destination.y,
                        7);
        SDL_SetRenderDrawColor(renderer, 41, 91, 139, 225);
        draw_thick_line(renderer,
                        (float)start.x,
                        (float)start.y,
                        (float)destination.x,
                        (float)destination.y,
                        3);
    }

    if (selection->has_start) {
        draw_marker(renderer, start, OPENRIDE_MARKER_START);
    }

    if (selection->has_destination) {
        draw_marker(renderer, destination, OPENRIDE_MARKER_DESTINATION);
    }
}

static void draw_overlay(SDL_Renderer *renderer,
                         const OpenRideMapCamera *camera,
                         const OpenRideMapSelection *selection,
                         const OpenRideMBTilesMetadata *metadata,
                         bool vector_map,
                         int viewport_width,
                         int viewport_height)
{
    const float panel_x = 10.0f;
    const float panel_y = 10.0f;
    const float panel_w = 420.0f;
    const float panel_h = 104.0f;
    SDL_FRect panel = {panel_x, panel_y, panel_w, panel_h};

    SDL_SetRenderDrawColor(renderer, 24, 28, 32, 218);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 75);
    SDL_RenderRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 247, 248, 249, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(renderer, panel_x + 12.0f, panel_y + 10.0f, "OpenRide v0.8");

    SDL_SetRenderDrawColor(renderer, 174, 181, 188, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugTextFormat(renderer,
                              panel_x + 12.0f,
                              panel_y + 25.0f,
                              "centre %.5f %.5f  |  z %.1f  |  %s",
                              camera->center_lat,
                              camera->center_lon,
                              camera->zoom,
                              vector_map ? "OSM offline" : "raster offline");

    if (selection->has_start) {
        SDL_FRect chip = {panel_x + 12.0f, panel_y + 44.0f, 10.0f, 10.0f};
        SDL_SetRenderDrawColor(renderer, 42, 157, 84, SDL_ALPHA_OPAQUE);
        SDL_RenderFillRect(renderer, &chip);
        SDL_SetRenderDrawColor(renderer, 238, 241, 243, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugTextFormat(renderer,
                                  panel_x + 30.0f,
                                  panel_y + 45.0f,
                                  "Depart       %.5f  %.5f",
                                  selection->start.lat,
                                  selection->start.lon);
    } else {
        SDL_SetRenderDrawColor(renderer, 238, 241, 243, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugText(renderer,
                            panel_x + 12.0f,
                            panel_y + 45.0f,
                            "Clique sur la carte pour choisir le depart");
    }

    if (selection->has_destination) {
        SDL_FRect chip = {panel_x + 12.0f, panel_y + 60.0f, 10.0f, 10.0f};
        SDL_SetRenderDrawColor(renderer, 214, 66, 57, SDL_ALPHA_OPAQUE);
        SDL_RenderFillRect(renderer, &chip);
        SDL_SetRenderDrawColor(renderer, 238, 241, 243, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugTextFormat(renderer,
                                  panel_x + 30.0f,
                                  panel_y + 61.0f,
                                  "Destination  %.5f  %.5f",
                                  selection->destination.lat,
                                  selection->destination.lon);
    } else if (selection->has_start) {
        SDL_SetRenderDrawColor(renderer, 238, 241, 243, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugText(renderer,
                            panel_x + 12.0f,
                            panel_y + 61.0f,
                            "Clique sur la carte pour choisir la destination");
    }

    SDL_SetRenderDrawColor(renderer, 157, 166, 174, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(renderer,
                        panel_x + 12.0f,
                        panel_y + 82.0f,
                        "glisser: deplacer  |  clic droit: supprimer  |  C: effacer");

    if (selection->has_start && selection->has_destination) {
        const double distance_m = openride_geo_distance_m(selection->start.lat,
                                                          selection->start.lon,
                                                          selection->destination.lat,
                                                          selection->destination.lon);
        char distance_text[32];
        if (distance_m >= 1000.0) {
            snprintf(distance_text, sizeof(distance_text), "%.1f km", distance_m / 1000.0);
        } else {
            snprintf(distance_text, sizeof(distance_text), "%.0f m", distance_m);
        }

        const float distance_w = 210.0f;
        const float distance_h = 66.0f;
        const float distance_x = viewport_width >= 680
            ? (float)viewport_width - distance_w - 10.0f
            : 10.0f;
        const float distance_y = viewport_width >= 680
            ? 10.0f
            : panel_y + panel_h + 8.0f;
        SDL_FRect distance_panel = {
            distance_x,
            distance_y,
            distance_w,
            distance_h
        };

        SDL_SetRenderDrawColor(renderer, 24, 28, 32, 218);
        SDL_RenderFillRect(renderer, &distance_panel);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 75);
        SDL_RenderRect(renderer, &distance_panel);

        SDL_SetRenderDrawColor(renderer, 174, 181, 188, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugText(renderer,
                            distance_x + 14.0f,
                            distance_y + 10.0f,
                            "DISTANCE DIRECTE");
        SDL_SetRenderDrawColor(renderer, 247, 248, 249, SDL_ALPHA_OPAQUE);
        draw_scaled_text(renderer,
                         distance_x + 14.0f,
                         distance_y + 30.0f,
                         1.75f,
                         distance_text);
    }

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
    OpenRideMapSelection selection;
    openride_map_selection_init(&selection);

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    OpenRideMapRenderer raster_renderer;
    OpenRideVectorMapRenderer vector_renderer;
    bool renderer_initialized = false;
    bool running = true;
    bool dragging_map = false;
    bool map_drag_moved = false;
    double mouse_down_x = 0.0;
    double mouse_down_y = 0.0;
    OpenRideSelectionMarker dragging_marker = OPENRIDE_MARKER_NONE;

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
                    } else if (event.key.key == SDLK_C) {
                        openride_map_selection_clear(&selection);
                    }
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    int width = 0;
                    int height = 0;
                    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);

                    if (event.button.button == SDL_BUTTON_LEFT) {
                        mouse_down_x = (double)event.button.x;
                        mouse_down_y = (double)event.button.y;
                        map_drag_moved = false;
                        dragging_marker = marker_at_screen(&camera,
                                                           &selection,
                                                           mouse_down_x,
                                                           mouse_down_y,
                                                           width,
                                                           height);
                        dragging_map = dragging_marker == OPENRIDE_MARKER_NONE;
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        const OpenRideSelectionMarker marker = marker_at_screen(
                            &camera,
                            &selection,
                            (double)event.button.x,
                            (double)event.button.y,
                            width,
                            height);
                        openride_map_selection_remove(&selection, marker);
                    }
                    break;
                }

                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        if (dragging_marker != OPENRIDE_MARKER_NONE) {
                            dragging_marker = OPENRIDE_MARKER_NONE;
                        } else if (dragging_map && !map_drag_moved) {
                            int width = 0;
                            int height = 0;
                            double lat = 0.0;
                            double lon = 0.0;
                            SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
                            openride_screen_to_geo(&camera,
                                                   (double)event.button.x,
                                                   (double)event.button.y,
                                                   width,
                                                   height,
                                                   &lat,
                                                   &lon);
                            (void)openride_map_selection_add(&selection, lat, lon);
                        }
                        dragging_map = false;
                        map_drag_moved = false;
                    }
                    break;

                case SDL_EVENT_MOUSE_MOTION:
                    if (dragging_marker != OPENRIDE_MARKER_NONE) {
                        int width = 0;
                        int height = 0;
                        double lat = 0.0;
                        double lon = 0.0;
                        SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
                        openride_screen_to_geo(&camera,
                                               (double)event.motion.x,
                                               (double)event.motion.y,
                                               width,
                                               height,
                                               &lat,
                                               &lon);
                        openride_map_selection_set(&selection,
                                                   dragging_marker,
                                                   lat,
                                                   lon);
                    } else if (dragging_map) {
                        const double dx = (double)event.motion.x - mouse_down_x;
                        const double dy = (double)event.motion.y - mouse_down_y;
                        const double movement = sqrt(dx * dx + dy * dy);

                        if (movement >= OPENRIDE_CLICK_DRAG_THRESHOLD) {
                            map_drag_moved = true;
                        }

                        if (map_drag_moved) {
                            openride_camera_pan(&camera,
                                                (double)event.motion.xrel,
                                                (double)event.motion.yrel);
                        }
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

        draw_selection(renderer, &camera, &selection, width, height);
        draw_center_marker(renderer, width, height);
        draw_overlay(renderer,
                     &camera,
                     &selection,
                     metadata,
                     vector_map,
                     width,
                     height);
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
