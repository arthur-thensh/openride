#include "openride/loop_generator.h"
#include "openride/routing_graph.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static OpenRideRoutingNodeId grid_node(unsigned row, unsigned col, unsigned size)
{
    return (OpenRideRoutingNodeId)(row * size + col);
}

static void build_grid(OpenRideRoutingGraph *graph)
{
    const unsigned size = 11U;
    const double center_lat = 50.3708;
    const double center_lon = 3.0802;
    const double step = 0.005;
    OpenRideRoutingGraphBuilder *builder = openride_routing_graph_builder_create();
    assert(builder);

    for (unsigned row = 0U; row < size; ++row) {
        for (unsigned col = 0U; col < size; ++col) {
            const double lat = center_lat + ((double)row - 5.0) * step;
            const double lon = center_lon + ((double)col - 5.0) * step;
            const OpenRideRoutingNodeId id = openride_routing_graph_builder_add_node(
                builder, lat, lon);
            assert(id == grid_node(row, col, size));
        }
    }

    OpenRideRoutingEdgeAttributes attrs = openride_routing_edge_attributes_default();
    attrs.road_class = OPENRIDE_ROAD_UNCLASSIFIED;
    attrs.surface = OPENRIDE_SURFACE_ASPHALT;
    attrs.max_speed_kph = 60U;

    for (unsigned row = 0U; row < size; ++row) {
        for (unsigned col = 0U; col < size; ++col) {
            const OpenRideRoutingNodeId here = grid_node(row, col, size);
            if (col + 1U < size) {
                assert(openride_routing_graph_builder_add_bidirectional_edge(
                    builder, here, grid_node(row, col + 1U, size), &attrs));
            }
            if (row + 1U < size) {
                assert(openride_routing_graph_builder_add_bidirectional_edge(
                    builder, here, grid_node(row + 1U, col, size), &attrs));
            }
        }
    }

    char error[256] = {0};
    assert(openride_routing_graph_builder_build(builder, graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);
}

int main(void)
{
    OpenRideRoutingGraph graph = {0};
    build_grid(&graph);

    OpenRideRoutingSnap start = {0};
    assert(openride_routing_graph_snap_to_segment(&graph,
                                                  50.3708,
                                                  3.0802,
                                                  1000.0,
                                                  &start));

    OpenRideLoopRequest request = openride_loop_request_default();
    request.start = start;
    request.target_distance_m = 8000.0;
    request.max_waypoint_snap_distance_m = 1200.0;
    request.preferred_waypoint_snap_distance_m = 250.0;
    request.candidate_count = 4U;
    request.seed = 12345U;
    request.profile = OPENRIDE_ROUTING_PROFILE_TOURING;

    OpenRideLoopResult result = {0};
    char error[256] = {0};
    assert(openride_loop_generator_generate(&graph,
                                            &request,
                                            &result,
                                            error,
                                            sizeof(error)));
    assert(result.route.geometry_count >= 5U);
    assert(result.route.distance_m > 3000.0);
    assert(result.waypoint_count == OPENRIDE_LOOP_MAX_WAYPOINTS);
    assert(result.stats.attempted_candidates == request.candidate_count);
    assert(result.stats.successful_candidates > 0U);
    assert(result.stats.score >= 0.0 && result.stats.score <= 100.0);
    assert(result.stats.distance_error_ratio >= 0.0);
    assert(result.stats.overlap_ratio >= 0.0 && result.stats.overlap_ratio <= 1.0);
    assert(result.stats.shape_score >= 0.0 && result.stats.shape_score <= 1.0);
    assert(result.stats.waypoint_quality_score >= 0.0
           && result.stats.waypoint_quality_score <= 1.0);
    assert(result.stats.candidate_stat_count == request.candidate_count);
    assert(result.stats.selected_candidate_index < request.candidate_count);
    assert(result.stats.candidates[result.stats.selected_candidate_index].successful);

    unsigned successful_stats = 0U;
    for (uint32_t i = 0U; i < result.stats.candidate_stat_count; ++i) {
        const OpenRideLoopCandidateStats *stats = &result.stats.candidates[i];
        if (!stats->successful) continue;
        ++successful_stats;
        assert(stats->distance_m > 0.0);
        assert(stats->score >= 0.0 && stats->score <= 100.0);
        assert(stats->shape_score >= 0.0 && stats->shape_score <= 1.0);
        assert(stats->waypoint_quality_score >= 0.0
               && stats->waypoint_quality_score <= 1.0);
    }
    assert(successful_stats == result.stats.successful_candidates);

    const OpenRideRoutePoint first = result.route.geometry[0];
    const OpenRideRoutePoint last = result.route.geometry[result.route.geometry_count - 1U];
    assert(fabs(first.lat - start.lat) < 1e-9);
    assert(fabs(first.lon - start.lon) < 1e-9);
    assert(fabs(last.lat - start.lat) < 1e-9);
    assert(fabs(last.lon - start.lon) < 1e-9);

    request.direction = OPENRIDE_LOOP_DIRECTION_NORTH;
    request.seed = 999U;
    OpenRideLoopResult north = {0};
    assert(openride_loop_generator_generate(&graph,
                                            &request,
                                            &north,
                                            error,
                                            sizeof(error)));
    assert(north.stats.successful_candidates > 0U);

    assert(strcmp(openride_loop_direction_name(OPENRIDE_LOOP_DIRECTION_ANY), "libre") == 0);
    assert(openride_loop_direction_next(OPENRIDE_LOOP_DIRECTION_WEST)
           == OPENRIDE_LOOP_DIRECTION_ANY);

    openride_loop_result_destroy(&north);
    openride_loop_result_destroy(&result);
    openride_routing_graph_destroy(&graph);

    puts("Loop generator tests: OK");
    return 0;
}
