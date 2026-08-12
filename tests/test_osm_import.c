#include "openride/osm_import.h"
#include "openride/routing_engine.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct MapFeatureCheck {
    uint32_t water_area_count;
    uint32_t water_area_points;
} MapFeatureCheck;

static bool check_map_feature(OpenRideOSMMapFeatureKind kind,
                              const double *latitudes,
                              const double *longitudes,
                              uint32_t point_count,
                              void *userdata)
{
    MapFeatureCheck *check = userdata;
    assert(latitudes != NULL);
    assert(longitudes != NULL);
    if (kind == OPENRIDE_OSM_MAP_FEATURE_WATER_AREA) {
        ++check->water_area_count;
        check->water_area_points = point_count;
    }
    return true;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    OpenRideRoutingGraph graph = {0};
    OpenRideOSMImportStats stats = {0};
    char error[256] = {0};

    assert(openride_osm_pbf_import_graph(argv[1],
                                         &graph,
                                         &stats,
                                         error,
                                         sizeof(error)));
    assert(error[0] == '\0');
    assert(stats.osm_way_count == 5U);
    assert(stats.routable_way_count == 2U);
    assert(stats.referenced_node_count == 4U);
    assert(stats.found_node_count == 4U);
    assert(stats.missing_node_count == 0U);
    assert(graph.node_count == 4U);
    /* residential: 2 segments x 2 directions = 4; track one-way = 1 */
    assert(graph.edge_count == 5U);

    OpenRideRoutingRequest request = openride_routing_request_default();
    request.start = 0U;
    request.destination = 3U;
    request.profile = OPENRIDE_ROUTING_PROFILE_TRAIL;
    OpenRideRoute route = {0};
    assert(openride_routing_engine_calculate(&graph,
                                              &request,
                                              &route,
                                              error,
                                              sizeof(error)));
    assert(route.node_count == 4U);
    assert(route.nodes[0] == 0U);
    assert(route.nodes[3] == 3U);

    openride_route_destroy(&route);
    openride_routing_graph_destroy(&graph);

    MapFeatureCheck map_check = {0};
    OpenRideOSMMapFeatureStats map_stats = {0};
    assert(openride_osm_pbf_visit_map_features(argv[1],
                                                check_map_feature,
                                                &map_check,
                                                &map_stats,
                                                error,
                                                sizeof(error)));
    assert(error[0] == '\0');
    assert(map_stats.osm_way_count == 5U);
    assert(map_stats.osm_relation_count == 1U);
    assert(map_stats.selected_relation_count == 1U);
    assert(map_stats.relation_member_way_count == 2U);
    assert(map_stats.multipolygon_outer_ring_count == 1U);
    assert(map_stats.incomplete_multipolygon_count == 0U);
    assert(map_stats.multipolygon_inner_members_ignored == 0U);
    assert(map_check.water_area_count == 1U);
    assert(map_check.water_area_points == 5U);

    puts("OSM import tests: OK");
    return 0;
}
