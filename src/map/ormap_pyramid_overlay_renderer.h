#ifndef OPENRIDE_ORMAP_PYRAMID_OVERLAY_RENDERER_H
#define OPENRIDE_ORMAP_PYRAMID_OVERLAY_RENDERER_H

#include <SDL3/SDL.h>

#include "openride/map_camera.h"
#include "openride/map_style.h"
#include "map/ormap_renderer.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct OpenRideORMapPyramidOverlayRenderer
    OpenRideORMapPyramidOverlayRenderer;

OpenRideORMapPyramidOverlayRenderer *
openride_ormap_pyramid_overlay_renderer_create(
    SDL_Renderer *renderer,
    const char *ormap11_path,
    char *error,
    size_t error_size);

void openride_ormap_pyramid_overlay_renderer_destroy(
    OpenRideORMapPyramidOverlayRenderer *renderer);

void openride_ormap_pyramid_overlay_renderer_set_style(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    OpenRideMapStyle style);

void openride_ormap_pyramid_overlay_renderer_begin_frame(
    OpenRideORMapPyramidOverlayRenderer *renderer);

bool openride_ormap_pyramid_overlay_renderer_draw_waterways(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    const OpenRideMapCamera *camera,
    int viewport_width,
    int viewport_height);

bool openride_ormap_pyramid_overlay_renderer_draw_roads(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    const OpenRideMapCamera *camera,
    int viewport_width,
    int viewport_height);

bool openride_ormap_pyramid_overlay_renderer_draw_labels(
    OpenRideORMapPyramidOverlayRenderer *renderer,
    const OpenRideMapCamera *camera,
    int viewport_width,
    int viewport_height);

bool openride_ormap_pyramid_overlay_renderer_needs_followup_frame(
    const OpenRideORMapPyramidOverlayRenderer *renderer);

void openride_ormap_pyramid_overlay_renderer_get_road_debug_stats(
    const OpenRideORMapPyramidOverlayRenderer *renderer,
    OpenRideORMapRoadDebugStats *stats);

#endif
