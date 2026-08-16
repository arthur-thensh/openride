#ifndef OPENRIDE_APP_PLANNER_ASYNC_RUNTIME_H
#define OPENRIDE_APP_PLANNER_ASYNC_RUNTIME_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_atomic.h>

#include "app_route_runtime.h"
#include "openride/ride_planner.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct OpenRidePlannerAsyncContext {
    OpenRideRidePlannerBusy kind;
    const OpenRideRoutingGraph *graph;
    bool graph_loaded;
    OpenRideMapSelection selection;
    OpenRideRoutingProfile profile;
    double loop_target_distance_m;
    OpenRideLoopDirection loop_direction;
    uint32_t loop_seed;

    SDL_AtomicInt done;
    SDL_AtomicInt success;

    OpenRideRoute route;
    OpenRideLoopProposalSet proposals;
    OpenRideRoutingSnap start_snap;
    OpenRideRoutingSnap destination_snap;
    char status[256];
} OpenRidePlannerAsyncContext;

void openride_app_planner_async_reset(OpenRidePlannerAsyncContext *context);

SDL_Thread *openride_app_planner_async_start_route(
    OpenRidePlannerAsyncContext *context,
    const OpenRideRoutingGraph *graph,
    bool graph_loaded,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile);

SDL_Thread *openride_app_planner_async_start_loops(
    OpenRidePlannerAsyncContext *context,
    const OpenRideRoutingGraph *graph,
    bool graph_loaded,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile,
    double target_distance_m,
    OpenRideLoopDirection direction,
    uint32_t seed);

bool openride_app_planner_async_route_request_matches(
    const OpenRidePlannerAsyncContext *context,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile);

bool openride_app_planner_async_loop_request_matches(
    const OpenRidePlannerAsyncContext *context,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile,
    double target_distance_m,
    OpenRideLoopDirection direction);

#endif
