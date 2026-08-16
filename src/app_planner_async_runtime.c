#include "app_planner_async_runtime.h"

#include "openride/france_regions_lite.h"
#include "openride/region_manager.h"

#include <stdio.h>
#include <string.h>

static bool selection_matches(const OpenRideMapSelection *a,
                              const OpenRideMapSelection *b)
{
    if (!a || !b) return false;
    if (a->has_start != b->has_start
        || a->has_destination != b->has_destination) {
        return false;
    }
    if (a->has_start
        && (a->start.lat != b->start.lat || a->start.lon != b->start.lon)) {
        return false;
    }
    if (a->has_destination
        && (a->destination.lat != b->destination.lat
            || a->destination.lon != b->destination.lon)) {
        return false;
    }
    return true;
}

void openride_app_planner_async_reset(OpenRidePlannerAsyncContext *context)
{
    if (!context) return;
    openride_route_destroy(&context->route);
    openride_loop_proposal_set_destroy(&context->proposals);
    memset(context, 0, sizeof(*context));
    context->start_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    context->destination_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
}

static bool same_region(const OpenRideRegionDefinition *a,
                        const OpenRideRegionDefinition *b)
{
    return a && b && a->id && b->id && strcmp(a->id, b->id) == 0;
}

static const OpenRideRoutingGraph *resolve_loop_graph(
    OpenRidePlannerAsyncContext *context,
    OpenRideRoutingGraph *owned_graph,
    bool *owned_loaded)
{
    if (owned_loaded) *owned_loaded = false;
    if (!context || !owned_graph || !owned_loaded) return NULL;

    const char *region_id = openride_france_regions_lite_region_id(
        context->selection.start.lat,
        context->selection.start.lon);
    if (!region_id || !region_id[0]) {
        snprintf(context->status,
                 sizeof(context->status),
                 "Aucune region OpenRide ne couvre ce point de depart");
        SDL_Log("RidePlanner: no region coverage for loop start lat=%.7f lon=%.7f",
                context->selection.start.lat,
                context->selection.start.lon);
        return NULL;
    }

    const OpenRideRegionDefinition *start_region = openride_region_find(region_id);
    if (!start_region) {
        snprintf(context->status,
                 sizeof(context->status),
                 "Region de depart inconnue: %.80s",
                 region_id);
        SDL_Log("RidePlanner: unknown loop start region id=%s", region_id);
        return NULL;
    }

    if (same_region(start_region, context->active_region)
        && context->graph_loaded
        && context->graph) {
        SDL_Log("RidePlanner: loop region=%s uses active routing graph",
                start_region->id);
        return context->graph;
    }

    OpenRideRegionStatus status;
    char region_error[256] = {0};
    if (!openride_region_get_status(&context->paths,
                                    start_region,
                                    &status,
                                    region_error,
                                    sizeof(region_error))) {
        snprintf(context->status,
                 sizeof(context->status),
                 "Impossible de verifier la region %.80s",
                 start_region->name ? start_region->name : start_region->id);
        SDL_Log("RidePlanner: region status failed id=%s error=%s",
                start_region->id,
                region_error[0] ? region_error : "unknown error");
        return NULL;
    }

    if (!status.routing_installed || !status.routing_path[0]) {
        snprintf(context->status,
                 sizeof(context->status),
                 "Installe la region %.90s pour generer cette balade",
                 start_region->name ? start_region->name : start_region->id);
        SDL_Log("RidePlanner: routing graph not installed for loop region=%s",
                start_region->id);
        return NULL;
    }

    char graph_error[256] = {0};
    if (!openride_routing_graph_load(owned_graph,
                                     status.routing_path,
                                     graph_error,
                                     sizeof(graph_error))) {
        snprintf(context->status,
                 sizeof(context->status),
                 "Impossible de charger le routage de %.80s",
                 start_region->name ? start_region->name : start_region->id);
        SDL_Log("RidePlanner: failed loading loop graph region=%s path=%s error=%s",
                start_region->id,
                status.routing_path,
                graph_error[0] ? graph_error : "unknown error");
        return NULL;
    }

    *owned_loaded = true;
    SDL_Log("RidePlanner: loop start region=%s active=%s -> temporary graph loaded "
            "nodes=%u segments=%u",
            start_region->id,
            context->active_region && context->active_region->id
                ? context->active_region->id : "none",
            owned_graph->node_count,
            owned_graph->segment_index.segment_count);
    return owned_graph;
}

static int SDLCALL planner_thread_main(void *userdata)
{
    OpenRidePlannerAsyncContext *context = userdata;
    if (!context) return 1;

    bool ok = false;
    if (context->kind == OPENRIDE_RIDE_PLANNER_CALCULATING_ROUTE) {
        ok = openride_app_route_recalculate(context->graph,
                                            context->graph_loaded,
                                            &context->selection,
                                            context->profile,
                                            &context->route,
                                            &context->start_snap,
                                            &context->destination_snap,
                                            context->status,
                                            sizeof(context->status));
    } else if (context->kind == OPENRIDE_RIDE_PLANNER_GENERATING_LOOPS) {
        OpenRideRoutingGraph owned_graph = {0};
        bool owned_loaded = false;
        const OpenRideRoutingGraph *loop_graph =
            resolve_loop_graph(context, &owned_graph, &owned_loaded);

        if (loop_graph) {
            /*
             * A single random seed can occasionally fail to produce a routable
             * shape. Retry only that transient case; validation/snap errors
             * remain immediate and visible.
             */
            for (uint32_t attempt = 0U; attempt < 3U; ++attempt) {
                context->status[0] = '\0';
                ok = openride_app_route_generate_loop_proposals(
                    loop_graph,
                    true,
                    &context->selection,
                    context->profile,
                    context->loop_target_distance_m,
                    context->loop_direction,
                    context->loop_seed + attempt * 0x9e3779b9U,
                    &context->proposals,
                    &context->start_snap,
                    context->status,
                    sizeof(context->status));
                if (ok) break;
                if (!strstr(context->status, "no loop candidate")) break;
            }
            if (!ok && strstr(context->status, "no loop candidate")) {
                snprintf(context->status,
                         sizeof(context->status),
                         "Aucune balade trouvee avec ces reglages. Essaie 50 km ou le profil Balade.");
            }
        }

        if (owned_loaded) {
            openride_routing_graph_destroy(&owned_graph);
        }
        if (!ok) {
            SDL_Log("RidePlanner: loop generation failed: %s",
                    context->status[0] ? context->status : "unknown error");
        }
    }

    SDL_SetAtomicInt(&context->success, ok ? 1 : 0);
    SDL_SetAtomicInt(&context->done, 1);
    return ok ? 0 : 1;
}

static bool prepare_job(OpenRidePlannerAsyncContext *context,
                        OpenRideRidePlannerBusy kind,
                        const OpenRideRoutingGraph *graph,
                        bool graph_loaded,
                        const OpenRideMapSelection *selection,
                        OpenRideRoutingProfile profile)
{
    if (!context || !selection || kind == OPENRIDE_RIDE_PLANNER_IDLE) return false;
    openride_app_planner_async_reset(context);
    context->kind = kind;
    context->graph = graph;
    context->graph_loaded = graph_loaded;
    context->selection = *selection;
    context->profile = profile;
    SDL_SetAtomicInt(&context->done, 0);
    SDL_SetAtomicInt(&context->success, 0);
    return true;
}

static SDL_Thread *launch_job(OpenRidePlannerAsyncContext *context)
{
    SDL_Thread *thread = SDL_CreateThread(planner_thread_main,
                                          "OpenRide-planner",
                                          context);
    if (!thread) {
        openride_app_planner_async_reset(context);
    }
    return thread;
}

SDL_Thread *openride_app_planner_async_start_route(
    OpenRidePlannerAsyncContext *context,
    const OpenRideRoutingGraph *graph,
    bool graph_loaded,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile)
{
    if (!selection || !openride_map_selection_complete(selection)) return NULL;
    if (!prepare_job(context,
                     OPENRIDE_RIDE_PLANNER_CALCULATING_ROUTE,
                     graph,
                     graph_loaded,
                     selection,
                     profile)) {
        return NULL;
    }
    return launch_job(context);
}

SDL_Thread *openride_app_planner_async_start_loops(
    OpenRidePlannerAsyncContext *context,
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *active_region,
    const OpenRideRoutingGraph *graph,
    bool graph_loaded,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile,
    double target_distance_m,
    OpenRideLoopDirection direction,
    uint32_t seed)
{
    if (!paths || !selection || !selection->has_start) return NULL;
    if (!prepare_job(context,
                     OPENRIDE_RIDE_PLANNER_GENERATING_LOOPS,
                     graph,
                     graph_loaded,
                     selection,
                     profile)) {
        return NULL;
    }
    context->paths = *paths;
    context->active_region = active_region;
    context->loop_target_distance_m = target_distance_m;
    context->loop_direction = direction;
    context->loop_seed = seed;
    return launch_job(context);
}

bool openride_app_planner_async_route_request_matches(
    const OpenRidePlannerAsyncContext *context,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile)
{
    return context
        && context->kind == OPENRIDE_RIDE_PLANNER_CALCULATING_ROUTE
        && context->profile == profile
        && selection_matches(&context->selection, selection);
}

bool openride_app_planner_async_loop_request_matches(
    const OpenRidePlannerAsyncContext *context,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile,
    double target_distance_m,
    OpenRideLoopDirection direction)
{
    return context
        && context->kind == OPENRIDE_RIDE_PLANNER_GENERATING_LOOPS
        && context->profile == profile
        && context->loop_target_distance_m == target_distance_m
        && context->loop_direction == direction
        && selection_matches(&context->selection, selection);
}
