#ifndef OPENRIDE_MAP_WORLD_H
#define OPENRIDE_MAP_WORLD_H

#include <SDL3/SDL.h>

#include "openride/map_camera.h"
#include "openride/map_style.h"
#include "openride/platform_paths.h"

#include <stdbool.h>
#include <stddef.h>

#define OPENRIDE_MAP_WORLD_MIN_ZOOM 6.0
#define OPENRIDE_MAP_WORLD_DETAIL_ZOOM 10.0
#define OPENRIDE_MAP_WORLD_MAX_OVERVIEW_ZOOM 11.30

typedef struct OpenRideMapWorld OpenRideMapWorld;

OpenRideMapWorld *openride_map_world_create(SDL_Renderer *renderer,
                                             const OpenRidePlatformPaths *paths,
                                             char *error,
                                             size_t error_size);

bool openride_map_world_refresh(OpenRideMapWorld *world,
                                const OpenRidePlatformPaths *paths,
                                char *error,
                                size_t error_size);

void openride_map_world_destroy(OpenRideMapWorld *world);

void openride_map_world_draw(OpenRideMapWorld *world,
                             const OpenRideMapCamera *camera,
                             OpenRideMapStyle style,
                             const char *skip_region_id,
                             int viewport_width,
                             int viewport_height);
void openride_map_world_draw_detail(OpenRideMapWorld *world,
                                    const OpenRideMapCamera *camera,
                                    OpenRideMapStyle style,
                                    int viewport_width,
                                    int viewport_height);

size_t openride_map_world_region_count(const OpenRideMapWorld *world);

#endif
