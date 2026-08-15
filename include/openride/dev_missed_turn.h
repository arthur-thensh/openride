#ifndef OPENRIDE_DEV_MISSED_TURN_H
#define OPENRIDE_DEV_MISSED_TURN_H

#include "openride/routing_engine.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Developer-only plan for a realistic missed-turn simulation.
 *
 * The planned navigation route is not modified. This object contains a short
 * geometry following a genuine outgoing branch of the same regional graph,
 * plus the position along the planned route where the simulator should switch
 * to that branch.
 */
typedef struct OpenRideDevMissedTurnPlan {
    OpenRideRoute branch_route;
    double trigger_position_m;
    uint32_t route_node_index;
    double max_distance_from_route_m;
} OpenRideDevMissedTurnPlan;

void openride_dev_missed_turn_plan_init(OpenRideDevMissedTurnPlan *plan);
void openride_dev_missed_turn_plan_destroy(OpenRideDevMissedTurnPlan *plan);

bool openride_dev_missed_turn_plan_build(
    const OpenRideRoutingGraph *graph,
    const OpenRideRoute *planned_route,
    double current_position_m,
    double minimum_ahead_m,
    double maximum_ahead_m,
    double minimum_off_route_m,
    OpenRideDevMissedTurnPlan *plan,
    char *error,
    size_t error_size);

#endif
