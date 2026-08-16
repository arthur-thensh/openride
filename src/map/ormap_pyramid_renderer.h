#ifndef OPENRIDE_ORMAP_PYRAMID_RENDERER_H
#define OPENRIDE_ORMAP_PYRAMID_RENDERER_H

#include <SDL3/SDL.h>

#include "openride/map_camera.h"
#include "openride/map_style.h"
#include "map/ormap_renderer.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct OpenRideORMapPyramidRenderer
    OpenRideORMapPyramidRenderer;

OpenRideORMapPyramidRenderer *
openride_ormap_pyramid_renderer_create(
    SDL_Renderer *renderer,
    const char *ormap11_path,
    char *error,
    size_t error_size);

void openride_ormap_pyramid_renderer_destroy(
    OpenRideORMapPyramidRenderer *renderer);

void openride_ormap_pyramid_renderer_set_style(
    OpenRideORMapPyramidRenderer *renderer,
    OpenRideMapStyle style);

void openride_ormap_pyramid_renderer_begin_frame(
    OpenRideORMapPyramidRenderer *renderer);

void openride_ormap_pyramid_renderer_draw_surfaces(
    OpenRideORMapPyramidRenderer *renderer,
    const OpenRideMapCamera *camera,
    int viewport_width,
    int viewport_height);

void openride_ormap_pyramid_renderer_draw_buildings(
    OpenRideORMapPyramidRenderer *renderer,
    const OpenRideMapCamera *camera,
    int viewport_width,
    int viewport_height);

bool openride_ormap_pyramid_renderer_needs_followup_frame(
    const OpenRideORMapPyramidRenderer *renderer);

void openride_ormap_pyramid_renderer_get_area_debug_stats(
    const OpenRideORMapPyramidRenderer *renderer,
    OpenRideORMapAreaDebugStats *stats);

#endif
