#ifndef OPENRIDE_APP_REGION_RUNTIME_H
#define OPENRIDE_APP_REGION_RUNTIME_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_atomic.h>

#include "map/map_renderer.h"
#include "map/map_world.h"
#include "map/ormap_renderer.h"
#include "map/vector_map_renderer.h"
#include "openride/map_camera.h"
#include "openride/map_style.h"
#include "openride/mbtiles.h"
#include "openride/ormap.h"
#include "openride/place_search.h"
#include "openride/platform_paths.h"
#include "openride/region_install.h"
#include "openride/region_manager.h"
#include "openride/routing_graph.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __ANDROID__
typedef struct OpenRideRegionPrepareThreadContext {
    OpenRidePlatformPaths paths;
    const OpenRideRegionDefinition *region;
    SDL_AtomicInt stage;
    SDL_AtomicInt done;
    SDL_AtomicInt success;
    OpenRideRegionPrepareStats stats;
    char error[256];
} OpenRideRegionPrepareThreadContext;

SDL_Thread *openride_app_region_start_prepare_thread(
    OpenRideRegionPrepareThreadContext *context,
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *region);

const char *openride_app_region_prepare_stage_text(int stage);
double openride_app_region_prepare_stage_progress(int stage);

bool openride_app_region_start_android_file_download(
    const OpenRideRegionDefinition *region,
    bool poly,
    bool *download_started,
    bool *download_is_poly,
    bool *region_busy,
    double *region_progress,
    char *work_status,
    size_t work_status_size);

bool openride_app_region_begin_android_install(
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *region,
    OpenRideRegionStatus *status,
    OpenRideRegionPrepareThreadContext *prepare_context,
    SDL_Thread **prepare_thread,
    bool *download_started,
    bool *download_is_poly,
    bool *region_busy,
    double *region_progress,
    char *work_status,
    size_t work_status_size,
    char *error,
    size_t error_size);
#endif

void openride_app_region_metadata_from_ormap(
    OpenRideMBTilesMetadata *out,
    const OpenRideORMapMetadata *source);

OpenRideMapCamera openride_app_region_camera_from_metadata(
    const OpenRideMBTilesMetadata *metadata);

const OpenRideRegionDefinition *openride_app_region_step(
    const OpenRideRegionDefinition *region,
    int direction);

const OpenRideRegionDefinition *openride_app_region_select_initial(
    const OpenRidePlatformPaths *paths,
    const char *saved_region_id,
    OpenRideRegionStatus *status,
    char *error,
    size_t error_size);

bool openride_app_region_activate_runtime(
    SDL_Renderer *renderer,
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *region,
    OpenRideMapStyle map_style,
    OpenRideMBTiles **map,
    OpenRideORMap **ormap,
    bool *ormap_map,
    bool *vector_map,
    bool *scalable_map,
    OpenRideMBTilesMetadata *metadata_storage,
    const OpenRideMBTilesMetadata **metadata,
    OpenRideMapRenderer *raster_renderer,
    OpenRideVectorMapRenderer *vector_renderer,
    OpenRideORMapRenderer *ormap_renderer,
    OpenRideRoutingGraph *routing_graph,
    bool *graph_loaded,
    OpenRidePlaceIndex **place_index,
    OpenRideMapCamera *camera,
    OpenRideRegionStatus *status_out,
    char *error,
    size_t error_size);

void openride_app_region_refresh_map_world_overview(
    OpenRideMapWorld *map_world,
    const OpenRidePlatformPaths *paths);

#endif
