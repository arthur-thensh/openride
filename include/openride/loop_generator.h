#ifndef OPENRIDE_LOOP_GENERATOR_H
#define OPENRIDE_LOOP_GENERATOR_H

#include "openride/routing_engine.h"
#include "openride/routing_graph.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPENRIDE_LOOP_MAX_WAYPOINTS 3U
#define OPENRIDE_LOOP_MAX_CANDIDATES 16U
#define OPENRIDE_LOOP_MAX_PROPOSALS 3U

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
    /* Hard rejection limit for a generated waypoint. */
    double max_waypoint_snap_distance_m;
    /* Soft quality target used when ranking candidate geometries. */
    double preferred_waypoint_snap_distance_m;
    uint32_t candidate_count;
    uint32_t seed;
    bool avoid_tolls;
    bool avoid_ferries;
} OpenRideLoopRequest;

typedef struct OpenRideLoopCandidateStats {
    bool successful;
    double distance_m;
    double score;
    double distance_error_ratio;
    double overlap_ratio;
    double max_waypoint_snap_distance_m;
    double shape_score;
    double waypoint_quality_score;
} OpenRideLoopCandidateStats;

typedef struct OpenRideLoopStats {
    uint32_t attempted_candidates;
    uint32_t successful_candidates;
    double score;
    double distance_error_ratio;
    double overlap_ratio;
    double max_waypoint_snap_distance_m;
    double shape_score;
    double waypoint_quality_score;
    uint32_t selected_candidate_index;
    uint32_t candidate_stat_count;
    OpenRideLoopCandidateStats candidates[OPENRIDE_LOOP_MAX_CANDIDATES];
} OpenRideLoopStats;

typedef struct OpenRideLoopResult {
    OpenRideRoute route;
    OpenRideRoutePoint waypoints[OPENRIDE_LOOP_MAX_WAYPOINTS];
    uint32_t waypoint_count;
    OpenRideLoopStats stats;
} OpenRideLoopResult;

/*
 * A retained, fully-routable loop proposal. Route ownership belongs to the
 * proposal set until the caller explicitly moves a route out of it.
 */
typedef struct OpenRideLoopProposal {
    OpenRideRoute route;
    OpenRideRoutePoint waypoints[OPENRIDE_LOOP_MAX_WAYPOINTS];
    uint32_t waypoint_count;
    uint32_t source_candidate_index;
    OpenRideLoopCandidateStats stats;
} OpenRideLoopProposal;

typedef struct OpenRideLoopProposalSet {
    OpenRideLoopProposal items[OPENRIDE_LOOP_MAX_PROPOSALS];
    uint32_t count;
    OpenRideLoopStats generation_stats;
} OpenRideLoopProposalSet;

OpenRideLoopRequest openride_loop_request_default(void);
void openride_loop_result_destroy(OpenRideLoopResult *result);
void openride_loop_proposal_set_destroy(OpenRideLoopProposalSet *proposals);

bool openride_loop_generator_generate(const OpenRideRoutingGraph *graph,
                                      const OpenRideLoopRequest *request,
                                      OpenRideLoopResult *result,
                                      char *error,
                                      size_t error_size);

/* Generate and retain the best loop routes, sorted by descending score. */
bool openride_loop_generator_generate_proposals(
    const OpenRideRoutingGraph *graph,
    const OpenRideLoopRequest *request,
    OpenRideLoopProposalSet *proposals,
    char *error,
    size_t error_size);

/* Move one proposal route to the caller and discard the remaining proposals. */
bool openride_loop_proposal_set_take(OpenRideLoopProposalSet *proposals,
                                     uint32_t index,
                                     OpenRideRoute *route,
                                     OpenRideRoutePoint waypoints[OPENRIDE_LOOP_MAX_WAYPOINTS],
                                     uint32_t *waypoint_count,
                                     OpenRideLoopCandidateStats *stats,
                                     uint32_t *source_candidate_index);

const char *openride_loop_direction_name(OpenRideLoopDirection direction);
OpenRideLoopDirection openride_loop_direction_next(OpenRideLoopDirection direction);

#endif
