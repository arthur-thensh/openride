#include "app_region_runtime.h"

#ifdef __ANDROID__
#include "openride/android_region_download.h"
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double app_region_clampd(double value, double min_value, double max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

#ifdef __ANDROID__
static void region_prepare_progress(OpenRideRegionPrepareStage stage,
                                    const char *message,
                                    void *userdata)
{
    (void)message;
    OpenRideRegionPrepareThreadContext *context = userdata;
    if (context) SDL_SetAtomicInt(&context->stage, (int)stage);
}

static int SDLCALL region_prepare_thread_main(void *userdata)
{
    OpenRideRegionPrepareThreadContext *context = userdata;
    if (!context) return 1;
    const bool ok = openride_region_prepare_from_pbf(&context->paths,
                                                      context->region,
                                                      false,
                                                      region_prepare_progress,
                                                      context,
                                                      &context->stats,
                                                      context->error,
                                                      sizeof(context->error));
    SDL_SetAtomicInt(&context->success, ok ? 1 : 0);
    SDL_SetAtomicInt(&context->done, 1);
    return ok ? 0 : 1;
}

SDL_Thread *openride_app_region_start_prepare_thread(
    OpenRideRegionPrepareThreadContext *context,
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *region)
{
    if (!context || !paths || !region) return NULL;
    memset(context, 0, sizeof(*context));
    context->paths = *paths;
    context->region = region;
    SDL_SetAtomicInt(&context->stage, OPENRIDE_REGION_PREPARE_ROUTING);
    return SDL_CreateThread(region_prepare_thread_main,
                            "OpenRide-region-prepare",
                            context);
}

const char *openride_app_region_prepare_stage_text(int stage)
{
    switch ((OpenRideRegionPrepareStage)stage) {
        case OPENRIDE_REGION_PREPARE_ROUTING: return "Preparation 1/4: routage";
        case OPENRIDE_REGION_PREPARE_SEARCH: return "Preparation 2/4: recherche";
        case OPENRIDE_REGION_PREPARE_MAP: return "Preparation 3/4: carte .ormap";
        case OPENRIDE_REGION_PREPARE_PYRAMID: return "Preparation 4/4: carte .ormap11";
        case OPENRIDE_REGION_PREPARE_FINALIZING: return "Finalisation de la region";
        case OPENRIDE_REGION_PREPARE_COMPLETE: return "Region prete";
        case OPENRIDE_REGION_PREPARE_ERROR: return "Erreur de preparation";
        default: return "Preparation...";
    }
}

double openride_app_region_prepare_stage_progress(int stage)
{
    switch ((OpenRideRegionPrepareStage)stage) {
        case OPENRIDE_REGION_PREPARE_ROUTING: return 0.18;
        case OPENRIDE_REGION_PREPARE_SEARCH: return 0.50;
        case OPENRIDE_REGION_PREPARE_MAP: return 0.68;
        case OPENRIDE_REGION_PREPARE_PYRAMID: return 0.82;
        case OPENRIDE_REGION_PREPARE_FINALIZING: return 0.96;
        case OPENRIDE_REGION_PREPARE_COMPLETE: return 1.0;
        default: return -1.0;
    }
}

bool openride_app_region_start_android_file_download(
    const OpenRideRegionDefinition *region,
    bool poly,
    bool *download_started,
    bool *download_is_poly,
    bool *region_busy,
    double *region_progress,
    char *work_status,
    size_t work_status_size)
{
    if (!region || !download_started || !download_is_poly
        || !region_busy || !region_progress || !work_status) return false;
    const char *url = poly ? region->poly_url : region->pbf_url;
    const char *filename = poly ? region->poly_filename : region->pbf_filename;
    const char *directory = poly ? "data/maps" : "data/downloads";
    char relative_path[384];
    snprintf(relative_path, sizeof(relative_path), "%s/%s", directory, filename);
    if (!openride_android_region_download_start(url, relative_path)) {
        snprintf(work_status,
                 work_status_size,
                 "%s",
                 poly
                     ? "Impossible de lancer le telechargement du contour"
                     : "Impossible de lancer le telechargement OSM");
        return false;
    }
    *download_started = true;
    *download_is_poly = poly;
    *region_busy = true;
    *region_progress = 0.0;
    snprintf(work_status,
             work_status_size,
             "%s",
             poly ? "Telechargement du contour de region"
                  : "Telechargement des donnees OSM");
    return true;
}

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
    size_t error_size)
{
    if (!paths || !region || !status || !prepare_context || !prepare_thread
        || !download_started || !download_is_poly || !region_busy
        || !region_progress || !work_status) return false;
    if (*region_busy || *prepare_thread || *download_started) return false;
    if (!openride_region_get_status(paths, region, status, error, error_size)) return false;

    /* The Geofabrik .poly is tiny and provides the exact offline coverage outline. */
    if (!status->poly_present) {
        return openride_app_region_start_android_file_download(region,
                                                  true,
                                                  download_started,
                                                  download_is_poly,
                                                  region_busy,
                                                  region_progress,
                                                  work_status,
                                                  work_status_size);
    }
    if (status->source_pbf_present) {
        *prepare_thread = openride_app_region_start_prepare_thread(prepare_context, paths, region);
        if (!*prepare_thread) {
            snprintf(work_status, work_status_size, "Impossible de lancer la preparation");
            return false;
        }
        const OpenRideRegionPrepareStage initial_stage =
            openride_region_status_ready(status)
                ? OPENRIDE_REGION_PREPARE_PYRAMID
                : OPENRIDE_REGION_PREPARE_ROUTING;
        *region_busy = true;
        *region_progress =
            openride_app_region_prepare_stage_progress(initial_stage);
        snprintf(work_status,
                 work_status_size,
                 "%s",
                 openride_app_region_prepare_stage_text(initial_stage));
        return true;
    }
    return openride_app_region_start_android_file_download(region,
                                              false,
                                              download_started,
                                              download_is_poly,
                                              region_busy,
                                              region_progress,
                                              work_status,
                                              work_status_size);
}
#endif

void openride_app_region_metadata_from_ormap(OpenRideMBTilesMetadata *out,
                                const OpenRideORMapMetadata *source)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "%s", source ? source->name : "OpenRide");
    snprintf(out->format, sizeof(out->format), "ormap");
    snprintf(out->attribution,
             sizeof(out->attribution),
             "%s",
             source ? source->attribution : "OpenStreetMap contributors");
    out->min_zoom = source ? source->min_zoom : OPENRIDE_ORMAP_MIN_ROAD_ZOOM;
    out->max_zoom = source ? source->max_zoom : OPENRIDE_ORMAP_MAX_ROAD_ZOOM;
    if (source) {
        out->has_center = source->has_center;
        out->center_lat = source->center_lat;
        out->center_lon = source->center_lon;
        out->center_zoom = source->center_zoom;
        out->has_bounds = source->has_bounds;
        out->west = source->west;
        out->south = source->south;
        out->east = source->east;
        out->north = source->north;
    }
}

OpenRideMapCamera openride_app_region_camera_from_metadata(const OpenRideMBTilesMetadata *metadata)
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
    camera.zoom = app_region_clampd(camera.zoom,
                         (double)metadata->min_zoom,
                         20.0);

    return camera;
}

const OpenRideRegionDefinition *openride_app_region_step(const OpenRideRegionDefinition *region,
                                                       int direction)
{
    const size_t count = openride_region_count();
    if (count == 0U) return NULL;
    size_t index = 0U;
    if (region) {
        for (size_t i = 0U; i < count; ++i) {
            const OpenRideRegionDefinition *candidate = openride_region_at(i);
            if (candidate && strcmp(candidate->id, region->id) == 0) {
                index = i;
                break;
            }
        }
    }
    if (direction < 0) {
        index = index == 0U ? count - 1U : index - 1U;
    } else if (direction > 0) {
        index = (index + 1U) % count;
    }
    return openride_region_at(index);
}

const OpenRideRegionDefinition *openride_app_region_select_initial(
    const OpenRidePlatformPaths *paths,
    const char *saved_region_id,
    OpenRideRegionStatus *status,
    char *error,
    size_t error_size)
{
    const OpenRideRegionDefinition *selected = openride_region_find(saved_region_id);
    if (!selected) selected = openride_region_default();
    if (selected
        && openride_region_get_status(paths, selected, status, error, error_size)
        && openride_region_status_ready(status)) {
        return selected;
    }
    for (size_t i = 0U; i < openride_region_count(); ++i) {
        const OpenRideRegionDefinition *candidate = openride_region_at(i);
        OpenRideRegionStatus candidate_status;
        if (!candidate) continue;
        if (openride_region_get_status(paths,
                                       candidate,
                                       &candidate_status,
                                       error,
                                       error_size)
            && openride_region_status_ready(&candidate_status)) {
            if (status) *status = candidate_status;
            return candidate;
        }
    }
    if (selected) {
        openride_region_get_status(paths, selected, status, error, error_size);
    }
    return selected;
}

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
    size_t error_size)
{
    if (!renderer || !paths || !region || !map || !ormap || !ormap_map
        || !vector_map || !scalable_map || !metadata_storage || !metadata
        || !raster_renderer || !vector_renderer || !ormap_renderer
        || !routing_graph || !graph_loaded || !place_index || !camera) {
        if (error && error_size) snprintf(error, error_size, "invalid runtime region switch");
        return false;
    }

    OpenRideRegionStatus status;
    if (!openride_region_get_status(paths, region, &status, error, error_size)
        || !openride_region_status_ready(&status)) {
        if (error && error_size && error[0] == '\0') {
            snprintf(error, error_size, "region data are not complete");
        }
        return false;
    }

    OpenRideORMap *new_ormap = openride_ormap_open(status.ormap_path, error, error_size);
    if (!new_ormap) return false;

    OpenRideRoutingGraph new_graph = {0};
    if (!openride_routing_graph_load(&new_graph, status.routing_path, error, error_size)) {
        openride_ormap_close(new_ormap);
        return false;
    }

    OpenRidePlaceIndex *new_place_index = openride_place_index_open(status.search_path,
                                                                    error,
                                                                    error_size);
    if (!new_place_index) {
        openride_routing_graph_destroy(&new_graph);
        openride_ormap_close(new_ormap);
        return false;
    }

    OpenRideORMapRenderer *new_renderer = calloc(1U, sizeof(*new_renderer));
    if (!new_renderer) {
        if (error && error_size) snprintf(error, error_size, "unable to allocate map renderer");
        openride_place_index_close(new_place_index);
        openride_routing_graph_destroy(&new_graph);
        openride_ormap_close(new_ormap);
        return false;
    }
    if (!openride_ormap_renderer_init(new_renderer, renderer, new_ormap)) {
        if (error && error_size) snprintf(error, error_size, "unable to initialize .ormap renderer");
        free(new_renderer);
        openride_place_index_close(new_place_index);
        openride_routing_graph_destroy(&new_graph);
        openride_ormap_close(new_ormap);
        return false;
    }
    openride_ormap_renderer_set_style(new_renderer, map_style);

    if (*ormap_map) {
        openride_ormap_renderer_destroy(ormap_renderer);
    } else if (*vector_map) {
        openride_vector_map_renderer_destroy(vector_renderer);
    } else if (*map) {
        openride_map_renderer_destroy(raster_renderer);
    }

    openride_place_index_close(*place_index);
    openride_routing_graph_destroy(routing_graph);
    openride_mbtiles_close(*map);
    openride_ormap_close(*ormap);

    *map = NULL;
    *ormap = new_ormap;
    *ormap_renderer = *new_renderer;
    free(new_renderer);
    *routing_graph = new_graph;
    *graph_loaded = true;
    *place_index = new_place_index;
    *ormap_map = true;
    *vector_map = false;
    *scalable_map = true;
    openride_app_region_metadata_from_ormap(metadata_storage, openride_ormap_metadata(new_ormap));
    *metadata = metadata_storage;
    *camera = openride_app_region_camera_from_metadata(*metadata);
    if (status_out) *status_out = status;
    if (error && error_size) error[0] = '\0';
    return true;
}


void openride_app_region_refresh_map_world_overview(OpenRideMapWorld *map_world,
                                               const OpenRidePlatformPaths *paths)
{
    if (!map_world || !paths) return;
    char world_error[256] = {0};
    if (!openride_map_world_refresh(map_world,
                                    paths,
                                    world_error,
                                    sizeof(world_error))) {
        SDL_Log("MapWorld refresh failed: %s",
                world_error[0] ? world_error : "unknown error");
    }
}
