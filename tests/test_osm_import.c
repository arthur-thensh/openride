#include "openride/osm_import.h"
#include "openride/routing_engine.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

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
    assert(stats.osm_way_count == 3U);
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
    puts("OSM import tests: OK");
    return 0;
}
