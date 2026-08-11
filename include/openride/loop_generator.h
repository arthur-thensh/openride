#ifndef OPENRIDE_LOOP_GENERATOR_H
#define OPENRIDE_LOOP_GENERATOR_H

#include "openride/routing_engine.h"
#include "openride/routing_graph.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPENRIDE_LOOP_MAX_WAYPOINTS 3U

typedef enum OpenRideLoopDirection {
    OPENRIDE_LOOP_DIRECTION_ANY = 0,
    OPENRIDE_LOOP_DIRECTION_NORTH,
    OPENRIDE_LOOP_DIRECTION_EAST,
    OPENRIDE_LOOP_DIRECTION_SOUTH,
    OPENRIDE_LOOP_DIRECTION_WEST
} OpenRideLoopDirection;

typedef struct OpenRideLoopRequest {
    OpenRideRoutingSnap start;
    OpenRideRoutingProfile profile;
    OpenRideLoopDirection direction;
    double target_distance_m;
    double max_waypoint_snap_distance_m;
    uint32_t candidate_count;
    uint32_t seed;
    bool avoid_tolls;
    bool avoid_ferries;
} OpenRideLoopRequest;

typedef struct OpenRideLoopStats {
    uint32_t attempted_candidates;
    uint32_t successful_candidates;
    double score;
    double distance_error_ratio;
    double overlap_ratio;
    double max_waypoint_snap_distance_m;
} OpenRideLoopStats;

typedef struct OpenRideLoopResult {
    OpenRideRoute route;
    OpenRideRoutePoint waypoints[OPENRIDE_LOOP_MAX_WAYPOINTS];
    uint32_t waypoint_count;
    OpenRideLoopStats stats;
} OpenRideLoopResult;

OpenRideLoopRequest openride_loop_request_default(void);
void openride_loop_result_destroy(OpenRideLoopResult *result);

bool openride_loop_generator_generate(const OpenRideRoutingGraph *graph,
                                      const OpenRideLoopRequest *request,
                                      OpenRideLoopResult *result,
                                      char *error,
                                      size_t error_size);

const char *openride_loop_direction_name(OpenRideLoopDirection direction);
OpenRideLoopDirection openride_loop_direction_next(OpenRideLoopDirection direction);

#endif
