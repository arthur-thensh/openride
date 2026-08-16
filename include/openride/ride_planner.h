#ifndef OPENRIDE_RIDE_PLANNER_H
#define OPENRIDE_RIDE_PLANNER_H

#include "openride/routing_engine.h"

#include <stdbool.h>
#include <stdint.h>

#define OPENRIDE_ROUTE_CHOICE_MAX_PROPOSALS 3U

typedef enum OpenRideRidePlannerMode {
    OPENRIDE_RIDE_PLANNER_ROUTE = 0,
    OPENRIDE_RIDE_PLANNER_LOOP
} OpenRideRidePlannerMode;

typedef enum OpenRideRidePlannerBusy {
    OPENRIDE_RIDE_PLANNER_IDLE = 0,
    OPENRIDE_RIDE_PLANNER_CALCULATING_ROUTE,
    OPENRIDE_RIDE_PLANNER_GENERATING_LOOPS
} OpenRideRidePlannerBusy;

typedef enum OpenRideRouteChoiceKind {
    OPENRIDE_ROUTE_CHOICE_NONE = 0,
    OPENRIDE_ROUTE_CHOICE_LOOP,
    OPENRIDE_ROUTE_CHOICE_ROUTE
} OpenRideRouteChoiceKind;

/*
 * Generic route-choice state.
 *
 * RouteChoice never owns route memory. Proposal containers keep ownership.
 * This lets the UI preview a route without moving it into the confirmed
 * application route. The owner must reset/rebind RouteChoice before proposal
 * storage is destroyed or replaced.
 */
typedef struct OpenRideRouteChoice {
    OpenRideRouteChoiceKind kind;
    bool active;
    const OpenRideRoute *proposals[OPENRIDE_ROUTE_CHOICE_MAX_PROPOSALS];
    uint32_t proposal_count;
    int32_t preview_index;
    int32_t confirmed_index;
} OpenRideRouteChoice;

static inline void openride_route_choice_reset(OpenRideRouteChoice *choice)
{
    if (!choice) return;
    choice->kind = OPENRIDE_ROUTE_CHOICE_NONE;
    choice->active = false;
    choice->proposal_count = 0U;
    choice->preview_index = -1;
    choice->confirmed_index = -1;
    for (uint32_t i = 0U; i < OPENRIDE_ROUTE_CHOICE_MAX_PROPOSALS; ++i) {
        choice->proposals[i] = NULL;
    }
}

static inline bool openride_route_choice_begin(OpenRideRouteChoice *choice,
                                               OpenRideRouteChoiceKind kind,
                                               uint32_t proposal_count)
{
    if (!choice || kind == OPENRIDE_ROUTE_CHOICE_NONE || proposal_count == 0U) {
        if (choice) openride_route_choice_reset(choice);
        return false;
    }
    if (proposal_count > OPENRIDE_ROUTE_CHOICE_MAX_PROPOSALS) {
        proposal_count = OPENRIDE_ROUTE_CHOICE_MAX_PROPOSALS;
    }
    openride_route_choice_reset(choice);
    choice->kind = kind;
    choice->active = true;
    choice->proposal_count = proposal_count;
    choice->preview_index = 0;
    return true;
}

static inline bool openride_route_choice_bind(OpenRideRouteChoice *choice,
                                              uint32_t index,
                                              const OpenRideRoute *route)
{
    if (!choice || !choice->active || index >= choice->proposal_count || !route) {
        return false;
    }
    choice->proposals[index] = route;
    return true;
}

static inline bool openride_route_choice_select_preview(OpenRideRouteChoice *choice,
                                                        uint32_t index)
{
    if (!choice || !choice->active || index >= choice->proposal_count
        || !choice->proposals[index]) {
        return false;
    }
    choice->preview_index = (int32_t)index;
    choice->confirmed_index = -1;
    return true;
}

static inline const OpenRideRoute *openride_route_choice_preview_route(
    const OpenRideRouteChoice *choice)
{
    if (!choice || !choice->active || choice->preview_index < 0
        || (uint32_t)choice->preview_index >= choice->proposal_count) {
        return NULL;
    }
    return choice->proposals[(uint32_t)choice->preview_index];
}

static inline bool openride_route_choice_confirm_preview(OpenRideRouteChoice *choice,
                                                         uint32_t *confirmed_index)
{
    if (!choice || !choice->active || choice->preview_index < 0
        || (uint32_t)choice->preview_index >= choice->proposal_count
        || !choice->proposals[(uint32_t)choice->preview_index]) {
        return false;
    }
    choice->confirmed_index = choice->preview_index;
    if (confirmed_index) {
        *confirmed_index = (uint32_t)choice->confirmed_index;
    }
    return true;
}

#endif
