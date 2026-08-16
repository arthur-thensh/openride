#include "app_route_runtime.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define OPENRIDE_MAX_SNAP_DISTANCE_M 2000.0
#define OPENRIDE_GPX_RECORDING_MIN_STEP_M 10.0
#define OPENRIDE_GPX_NAVIGATION_SPEED_KPH 50.0

static double app_route_clampd(double value, double min_value, double max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

void openride_app_route_fit_camera_to_gpx(OpenRideMapCamera *camera,
                              const OpenRideGPXDocument *document,
                              int viewport_width,
                              int viewport_height,
                              double min_zoom,
                              double max_zoom)
{
    if (!camera || !document || viewport_width <= 100 || viewport_height <= 100) return;
    const OpenRideGPXBounds bounds = openride_gpx_document_bounds(document);
    if (!bounds.valid) return;

    const OpenRidePointD nw = openride_mercator_forward(bounds.max_lat, bounds.min_lon);
    const OpenRidePointD se = openride_mercator_forward(bounds.min_lat, bounds.max_lon);
    double dx = fabs(se.x - nw.x);
    if (dx > 0.5) dx = 1.0 - dx;
    const double dy = fabs(se.y - nw.y);
    const double usable_w = (double)viewport_width - 100.0;
    const double usable_h = (double)viewport_height - 100.0;
    double zoom = max_zoom;

    if (dx > 1e-12 && dy > 1e-12) {
        const double zoom_x = log2(usable_w / (256.0 * dx));
        const double zoom_y = log2(usable_h / (256.0 * dy));
        zoom = fmin(zoom_x, zoom_y);
    } else if (dx > 1e-12) {
        zoom = log2(usable_w / (256.0 * dx));
    } else if (dy > 1e-12) {
        zoom = log2(usable_h / (256.0 * dy));
    }

    camera->zoom = app_route_clampd(zoom, min_zoom, max_zoom);
    camera->center_lat = (bounds.min_lat + bounds.max_lat) * 0.5;
    camera->center_lon = (bounds.min_lon + bounds.max_lon) * 0.5;
}

bool openride_app_route_load_gpx_overlay(const char *path,
                             OpenRideGPXDocument *document,
                             char *status,
                             size_t status_size)
{
    char error[256] = {0};
    if (!path || !openride_platform_file_exists(path)) {
        snprintf(status,
                 status_size,
                 "GPX introuvable: %.180s",
                 path ? path : "-");
        return false;
    }

    if (!openride_gpx_load_file(path, document, error, sizeof(error))) {
        snprintf(status,
                 status_size,
                 "import GPX impossible: %.160s",
                 error[0] ? error : "erreur inconnue");
        return false;
    }

    snprintf(status,
             status_size,
             "GPX charge: %u track | %u route | %u waypoint",
             document->track_points.count,
             document->route_points.count,
             document->waypoints.count);
    return true;
}

void openride_app_route_record_gps_sample(OpenRideGPXDocument *recording,
                              const OpenRideGPSSample *sample,
                              double *last_recorded_position_m)
{
    if (!recording || !sample || !sample->valid || !last_recorded_position_m) return;
    if (*last_recorded_position_m >= 0.0
        && sample->route_position_m >= *last_recorded_position_m
        && sample->route_position_m - *last_recorded_position_m < OPENRIDE_GPX_RECORDING_MIN_STEP_M) {
        return;
    }

    OpenRideGPXPoint point;
    memset(&point, 0, sizeof(point));
    point.lat = sample->lat;
    point.lon = sample->lon;
    point.starts_new_segment = (recording->track_points.count == 0U);
    if (openride_gpx_document_append(recording, OPENRIDE_GPX_POINT_TRACK, &point)) {
        if (recording->track_points.count == 1U) recording->track_segment_count = 1U;
        *last_recorded_position_m = sample->route_position_m;
    }
}

bool openride_app_route_recalculate(const OpenRideRoutingGraph *graph,
                              bool graph_loaded,
                              const OpenRideMapSelection *selection,
                              OpenRideRoutingProfile profile,
                              OpenRideRoute *route,
                              OpenRideRoutingSnap *start_snap,
                              OpenRideRoutingSnap *destination_snap,
                              char *status,
                              size_t status_size)
{
    openride_route_destroy(route);
    if (start_snap) {
        memset(start_snap, 0, sizeof(*start_snap));
        start_snap->segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    }
    if (destination_snap) {
        memset(destination_snap, 0, sizeof(*destination_snap));
        destination_snap->segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    }

    if (!graph_loaded) {
        snprintf(status, status_size, "graphe routier non installe");
        return false;
    }
    if (!openride_map_selection_complete(selection)) {
        snprintf(status, status_size, "choisis un depart et une destination");
        return false;
    }

    OpenRideRoutingSnap local_start = {0};
    OpenRideRoutingSnap local_destination = {0};
    local_start.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    local_destination.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;

    if (!openride_routing_graph_snap_to_segment(graph,
                                                selection->start.lat,
                                                selection->start.lon,
                                                OPENRIDE_MAX_SNAP_DISTANCE_M,
                                                &local_start)
        || !openride_routing_graph_snap_to_segment(graph,
                                                   selection->destination.lat,
                                                   selection->destination.lon,
                                                   OPENRIDE_MAX_SNAP_DISTANCE_M,
                                                   &local_destination)) {
        snprintf(status, status_size, "point trop loin du reseau routier");
        return false;
    }

    if (start_snap) *start_snap = local_start;
    if (destination_snap) *destination_snap = local_destination;

    OpenRideSnappedRoutingRequest request = openride_snapped_routing_request_default();
    request.start = local_start;
    request.destination = local_destination;
    request.profile = profile;

    char route_error[256] = {0};
    if (!openride_routing_engine_calculate_snapped(graph,
                                                   &request,
                                                   route,
                                                   route_error,
                                                   sizeof(route_error))) {
        snprintf(status,
                 status_size,
                 "itineraire impossible: %.180s",
                 route_error[0] ? route_error : "erreur inconnue");
        return false;
    }

    snprintf(status, status_size, "itineraire calcule sur segments");
    return true;
}



static int SDLCALL routing_world_thread_main(void *userdata)
{
    OpenRideRoutingWorldThreadContext *context = userdata;
    if (!context) return 1;

    const bool ok = context->installed_alternative
        ? openride_routing_world_calculate_installed_alternative_cached(
              &context->paths,
              context->active_region,
              context->active_graph,
              context->cache,
              context->selection.start.lat,
              context->selection.start.lon,
              context->selection.destination.lat,
              context->selection.destination.lon,
              OPENRIDE_MAX_SNAP_DISTANCE_M,
              context->profile,
              &context->route,
              &context->result,
              context->error,
              sizeof(context->error))
        : openride_routing_world_calculate_selection_cached(
              &context->paths,
              context->active_region,
              context->active_graph,
              context->cache,
              &context->selection,
              OPENRIDE_MAX_SNAP_DISTANCE_M,
              context->profile,
              &context->route,
              &context->result,
              context->error,
              sizeof(context->error));

    SDL_SetAtomicInt(&context->success, ok ? 1 : 0);
    SDL_SetAtomicInt(&context->done, 1);
    return ok ? 0 : 1;
}

static SDL_Thread *start_routing_world_thread_mode(
    OpenRideRoutingWorldThreadContext *context,
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *active_region,
    const OpenRideRoutingGraph *active_graph,
    OpenRideRoutingWorldCache *cache,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile,
    bool installed_alternative,
    bool reroute,
    bool resume_simulator)
{
    if (!context || !paths || !selection
        || !openride_map_selection_complete(selection)) {
        return NULL;
    }

    openride_route_destroy(&context->route);
    memset(context, 0, sizeof(*context));
    context->paths = *paths;
    context->active_region = active_region;
    context->active_graph = active_graph;
    context->cache = cache;
    context->selection = *selection;
    context->profile = profile;
    context->installed_alternative = installed_alternative;
    context->reroute = reroute;
    context->resume_simulator = resume_simulator;
    SDL_SetAtomicInt(&context->done, 0);
    SDL_SetAtomicInt(&context->success, 0);

    return SDL_CreateThread(routing_world_thread_main,
                            "OpenRide-routing-world",
                            context);
}

SDL_Thread *openride_app_route_start_world_thread(
    OpenRideRoutingWorldThreadContext *context,
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *active_region,
    const OpenRideRoutingGraph *active_graph,
    OpenRideRoutingWorldCache *cache,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile,
    bool reroute,
    bool resume_simulator)
{
    return start_routing_world_thread_mode(
        context,
        paths,
        active_region,
        active_graph,
        cache,
        selection,
        profile,
        false,
        reroute,
        resume_simulator);
}

SDL_Thread *openride_app_route_start_world_installed_alternative_thread(
    OpenRideRoutingWorldThreadContext *context,
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *active_region,
    const OpenRideRoutingGraph *active_graph,
    OpenRideRoutingWorldCache *cache,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile)
{
    return start_routing_world_thread_mode(
        context,
        paths,
        active_region,
        active_graph,
        cache,
        selection,
        profile,
        true,
        false,
        false);
}

bool openride_app_route_world_request_matches(
    const OpenRideRoutingWorldThreadContext *context,
    const OpenRideRegionDefinition *active_region,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile)
{
    if (!context || !selection) return false;
    if (context->active_region != active_region || context->profile != profile) return false;
    if (context->selection.has_start != selection->has_start
        || context->selection.has_destination != selection->has_destination) {
        return false;
    }
    if (strcmp(context->selection.start_region_id,
               selection->start_region_id) != 0
        || strcmp(context->selection.destination_region_id,
                  selection->destination_region_id) != 0) {
        return false;
    }
    if (selection->has_start
        && (context->selection.start.lat != selection->start.lat
            || context->selection.start.lon != selection->start.lon)) {
        return false;
    }
    if (selection->has_destination
        && (context->selection.destination.lat != selection->destination.lat
            || context->selection.destination.lon != selection->destination.lon)) {
        return false;
    }
    return true;
}

bool openride_app_route_generate_loop(const OpenRideRoutingGraph *graph,
                                bool graph_loaded,
                                const OpenRideMapSelection *selection,
                                OpenRideRoutingProfile profile,
                                double target_distance_m,
                                OpenRideLoopDirection direction,
                                uint32_t seed,
                                OpenRideRoute *route,
                                OpenRideLoopStats *stats,
                                OpenRideRoutePoint waypoints[OPENRIDE_LOOP_MAX_WAYPOINTS],
                                uint32_t *waypoint_count,
                                OpenRideRoutingSnap *start_snap,
                                char *status,
                                size_t status_size)
{
    openride_route_destroy(route);
    if (stats) memset(stats, 0, sizeof(*stats));
    if (waypoint_count) *waypoint_count = 0U;
    if (start_snap) {
        memset(start_snap, 0, sizeof(*start_snap));
        start_snap->segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    }

    if (!graph_loaded) {
        snprintf(status, status_size, "graphe routier non installe");
        return false;
    }
    if (!selection->has_start) {
        snprintf(status, status_size, "choisis d'abord un point de depart");
        return false;
    }

    OpenRideRoutingSnap local_start = {0};
    local_start.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    if (!openride_routing_graph_snap_to_segment(graph,
                                                selection->start.lat,
                                                selection->start.lon,
                                                OPENRIDE_MAX_SNAP_DISTANCE_M,
                                                &local_start)) {
        snprintf(status, status_size, "depart trop loin du reseau routier");
        return false;
    }

    OpenRideLoopRequest request = openride_loop_request_default();
    request.start = local_start;
    request.profile = profile;
    request.direction = direction;
    request.target_distance_m = target_distance_m;
    request.candidate_count = 6U;
    request.seed = seed;

    OpenRideLoopResult generated = {0};
    char loop_error[256] = {0};
    if (!openride_loop_generator_generate(graph,
                                          &request,
                                          &generated,
                                          loop_error,
                                          sizeof(loop_error))) {
        snprintf(status,
                 status_size,
                 "boucle impossible: %.180s",
                 loop_error[0] ? loop_error : "erreur inconnue");
        return false;
    }

    if (start_snap) *start_snap = local_start;
    if (stats) *stats = generated.stats;
    if (waypoints && generated.waypoint_count > 0U) {
        memcpy(waypoints,
               generated.waypoints,
               sizeof(generated.waypoints));
    }
    if (waypoint_count) *waypoint_count = generated.waypoint_count;

    *route = generated.route;
    memset(&generated.route, 0, sizeof(generated.route));
    snprintf(status,
             status_size,
             "boucle %.1f km | score %.0f | %u/%u candidats",
             route->distance_m / 1000.0,
             generated.stats.score,
             generated.stats.successful_candidates,
             generated.stats.attempted_candidates);
    openride_loop_result_destroy(&generated);
    return true;
}

void openride_app_route_clear_navigation_session(OpenRideNavigationEngine *navigation,
                                     OpenRideGPSSimulator *simulator,
                                     OpenRideNavigationState *navigation_state,
                                     OpenRideGPSSample *gps_sample,
                                     bool *gps_sample_valid)
{
    openride_navigation_engine_clear_route(navigation);
    openride_gps_simulator_clear_route(simulator);
    if (navigation_state) memset(navigation_state, 0, sizeof(*navigation_state));
    if (gps_sample) memset(gps_sample, 0, sizeof(*gps_sample));
    if (gps_sample_valid) *gps_sample_valid = false;
}

bool openride_app_route_prepare_navigation_session(OpenRideNavigationEngine *navigation,
                                       OpenRideGPSSimulator *simulator,
                                       OpenRideNavigationInstructionList *instructions,
                                       const OpenRideRoutingGraph *graph,
                                       const OpenRideRoute *route,
                                       char *status,
                                       size_t status_size)
{
    char error[192] = {0};
    if (!openride_navigation_engine_set_route(navigation,
                                              route,
                                              error,
                                              sizeof(error))) {
        snprintf(status,
                 status_size,
                 "navigation indisponible: %.140s",
                 error[0] ? error : "geometrie invalide");
        return false;
    }
    if (!openride_gps_simulator_set_route(simulator,
                                          route,
                                          60.0,
                                          error,
                                          sizeof(error))) {
        openride_navigation_engine_clear_route(navigation);
        snprintf(status,
                 status_size,
                 "simulateur GPS indisponible: %.130s",
                 error[0] ? error : "geometrie invalide");
        return false;
    }
    openride_navigation_instructions_destroy(instructions);
    const OpenRideRoutingGraph *instruction_graph =
        route->nodes && route->node_count > 0U ? graph : NULL;
    if (!openride_navigation_instructions_build(instruction_graph,
                                                route,
                                                instructions,
                                                error,
                                                sizeof(error))) {
        openride_gps_simulator_clear_route(simulator);
        openride_navigation_engine_clear_route(navigation);
        snprintf(status,
                 status_size,
                 "instructions indisponibles: %.125s",
                 error[0] ? error : "geometrie invalide");
        return false;
    }
    return true;
}


bool openride_app_route_reroute_from_position(
    const OpenRideRoutingGraph *graph,
    bool graph_loaded,
    OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile,
    double lat,
    double lon,
    OpenRideRoute *route,
    OpenRideRoutingSnap *start_snap,
    OpenRideRoutingSnap *destination_snap,
    OpenRideNavigationEngine *navigation,
    OpenRideGPSSimulator *simulator,
    OpenRideNavigationInstructionList *instructions,
    OpenRideNavigationSession *session,
    OpenRideLocationFilter *location_filter,
    bool resume_simulator,
    char *status,
    size_t status_size)
{
    if (!selection || !selection->has_destination || !route || !navigation
        || !simulator || !instructions || !session || !location_filter) {
        if (status && status_size) snprintf(status, status_size, "recalcul impossible");
        return false;
    }

    openride_navigation_engine_clear_route(navigation);
    openride_gps_simulator_clear_route(simulator);
    openride_navigation_instructions_destroy(instructions);
    openride_map_selection_set(selection, OPENRIDE_MARKER_START, lat, lon);

    const bool ok = openride_app_route_recalculate(graph,
                                      graph_loaded,
                                      selection,
                                      profile,
                                      route,
                                      start_snap,
                                      destination_snap,
                                      status,
                                      status_size);
    if (!ok) return false;

    if (!openride_app_route_prepare_navigation_session(navigation,
                                    simulator,
                                    instructions,
                                    graph,
                                    route,
                                    status,
                                    status_size)) {
        openride_route_destroy(route);
        return false;
    }

    openride_navigation_session_mark_rerouted(session);
    openride_location_filter_reset(location_filter);
    if (resume_simulator) openride_gps_simulator_start(simulator);
    return true;
}

bool openride_app_route_prepare_gpx_navigation(const OpenRideGPXDocument *document,
                                   OpenRideRoutingGraph *graph,
                                   OpenRideMapSelection *selection,
                                   OpenRideRoute *route,
                                   OpenRideNavigationEngine *navigation,
                                   OpenRideGPSSimulator *simulator,
                                   OpenRideNavigationInstructionList *instructions,
                                   OpenRideNavigationSession *session,
                                   OpenRideLocationFilter *location_filter,
                                   char *status,
                                   size_t status_size)
{
    if (!document || !selection || !route || !navigation || !simulator
        || !instructions || !session || !location_filter) {
        return false;
    }

    char error[192] = {0};
    openride_navigation_engine_clear_route(navigation);
    openride_gps_simulator_clear_route(simulator);
    openride_navigation_instructions_destroy(instructions);

    if (!openride_gpx_build_navigation_route(document,
                                             OPENRIDE_GPX_NAVIGATION_SPEED_KPH,
                                             route,
                                             error,
                                             sizeof(error))) {
        snprintf(status,
                 status_size,
                 "navigation GPX impossible: %.145s",
                 error[0] ? error : "trace invalide");
        return false;
    }

    openride_map_selection_clear(selection);
    openride_map_selection_set(selection,
                               OPENRIDE_MARKER_START,
                               route->geometry[0].lat,
                               route->geometry[0].lon);
    openride_map_selection_set(selection,
                               OPENRIDE_MARKER_DESTINATION,
                               route->geometry[route->geometry_count - 1U].lat,
                               route->geometry[route->geometry_count - 1U].lon);

    if (!openride_app_route_prepare_navigation_session(navigation,
                                    simulator,
                                    instructions,
                                    graph,
                                    route,
                                    status,
                                    status_size)) {
        openride_route_destroy(route);
        return false;
    }

    openride_navigation_session_reset(session);
    openride_location_filter_reset(location_filter);
    snprintf(status,
             status_size,
             "trace GPX prete: %.1f km | S pour simuler",
             route->distance_m / 1000.0);
    return true;
}

bool openride_app_route_add_selection_from_screen(OpenRideMapSelection *selection,
                                      const OpenRideMapCamera *camera,
                                      double screen_x,
                                      double screen_y,
                                      int viewport_width,
                                      int viewport_height,
                                      bool *route_dirty,
                                      bool *loop_active,
                                      uint32_t *loop_waypoint_count,
                                      char *status,
                                      size_t status_size)
{
    double lat = 0.0;
    double lon = 0.0;
    if (!selection || !camera) return false;
    openride_screen_to_geo(camera,
                           screen_x,
                           screen_y,
                           viewport_width,
                           viewport_height,
                           &lat,
                           &lon);
    const OpenRideSelectionMarker added = openride_map_selection_add(selection, lat, lon);
    if (added == OPENRIDE_MARKER_NONE) return false;
    if (loop_active) *loop_active = false;
    if (loop_waypoint_count) *loop_waypoint_count = 0U;
    if (route_dirty) *route_dirty = openride_map_selection_complete(selection);
    if (status && status_size > 0U && route_dirty && !*route_dirty) {
        snprintf(status, status_size, "choisis la destination");
    }
    return true;
}

