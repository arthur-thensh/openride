#include "app_planner_async_runtime.h"

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
        ok = openride_app_route_generate_loop_proposals(
            context->graph,
            context->graph_loaded,
            &context->selection,
            context->profile,
            context->loop_target_distance_m,
            context->loop_direction,
            context->loop_seed,
            &context->proposals,
            &context->start_snap,
            context->status,
            sizeof(context->status));
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
    const OpenRideRoutingGraph *graph,
    bool graph_loaded,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile,
    double target_distance_m,
    OpenRideLoopDirection direction,
    uint32_t seed)
{
    if (!selection || !selection->has_start) return NULL;
    if (!prepare_job(context,
                     OPENRIDE_RIDE_PLANNER_GENERATING_LOOPS,
                     graph,
                     graph_loaded,
                     selection,
                     profile)) {
        return NULL;
    }
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
