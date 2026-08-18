#ifndef OPENRIDE_MAP_WORLD_H
#define OPENRIDE_MAP_WORLD_H

#include <SDL3/SDL.h>

#ifdef __ANDROID__
#include "openride/drive_perspective_renderer.h"
/* MapWorld owns the top-level installed-region clear. During Drive this starts
 * an offscreen map scene; internal V11 compositor sources do not include this
 * header and therefore keep clearing their own targets normally. */
#define SDL_RenderClear openride_drive_perspective_clear
#endif

#include "map/ormap_renderer.h"
#include "openride/map_camera.h"
#include "openride/map_style.h"
#include "openride/platform_paths.h"

#include <stdbool.h>
#include <stddef.h>

#define OPENRIDE_MAP_WORLD_MIN_ZOOM 6.0
#define OPENRIDE_MAP_WORLD_DETAIL_ZOOM 10.0
#define OPENRIDE_MAP_WORLD_MAX_OVERVIEW_ZOOM 11.30

typedef struct OpenRideMapWorldDebugStats {
    bool overview_drawn;
    bool detail_drawn;
    bool ormap_stats_valid;
    uint32_t visible_detail_regions;
    double overview_ms;
    double detail_ms;
    double masks_ms;
    double areas_ms;
    double waterways_ms;
    double roads_ms;
    double labels_ms;
    OpenRideORMapRoadDebugStats road;
    OpenRideORMapAreaDebugStats area;
} OpenRideMapWorldDebugStats;

typedef struct OpenRideMapWorldFollowupSources {
    bool detail_v8;
    bool pyramid_v11;
    bool overlay_v11;
    uint32_t detail_v8_deferred_loads;
    uint32_t pyramid_v11_draw_loads;
    uint32_t pyramid_v11_deferred_loads;
    uint32_t overlay_v11_draw_loads;
    uint32_t overlay_v11_deferred_loads;
    uint32_t overlay_v11_cache_hits;
    uint32_t overlay_v11_cache_misses;
} OpenRideMapWorldFollowupSources;

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

bool openride_map_world_base_available(
    const OpenRideMapWorld *world);
void openride_map_world_draw_base_overview(
    OpenRideMapWorld *world,
    const OpenRideMapCamera *camera,
    OpenRideMapStyle style,
    int viewport_width,
    int viewport_height);

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

void openride_map_world_debug_begin_frame(OpenRideMapWorld *world);
void openride_map_world_debug_end_frame(OpenRideMapWorld *world);
void openride_map_world_get_debug_stats(
    const OpenRideMapWorld *world,
    OpenRideMapWorldDebugStats *stats);

bool openride_map_world_needs_followup_frame(
    const OpenRideMapWorld *world);
void openride_map_world_get_followup_sources(
    const OpenRideMapWorld *world,
    OpenRideMapWorldFollowupSources *sources);

size_t openride_map_world_region_count(const OpenRideMapWorld *world);

#endif
