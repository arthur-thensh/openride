#include "openride/routing_world.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static OpenRideRoutingGraph build_left(void)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder = openride_routing_graph_builder_create();
    assert(builder != NULL);

    const OpenRideRoutingNodeId start =
        openride_routing_graph_builder_add_node(builder, 50.0000, 2.9900);
    const OpenRideRoutingNodeId gateway =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0000);

    OpenRideRoutingEdgeAttributes road = openride_routing_edge_attributes_default();
    road.length_m = 1000.0;
    road.road_class = OPENRIDE_ROAD_PRIMARY;
    road.surface = OPENRIDE_SURFACE_ASPHALT;
    road.max_speed_kph = 80U;
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, start, gateway, &road));

    char error[256] = {0};
    assert(openride_routing_graph_builder_build(builder, &graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);
    return graph;
}

static OpenRideRoutingGraph build_right(void)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder = openride_routing_graph_builder_create();
    assert(builder != NULL);

    const OpenRideRoutingNodeId gateway =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0000);
    const OpenRideRoutingNodeId destination =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0100);

    OpenRideRoutingEdgeAttributes road = openride_routing_edge_attributes_default();
    road.length_m = 1000.0;
    road.road_class = OPENRIDE_ROAD_PRIMARY;
    road.surface = OPENRIDE_SURFACE_ASPHALT;
    road.max_speed_kph = 80U;
    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, gateway, destination, &road));

    char error[256] = {0};
    assert(openride_routing_graph_builder_build(builder, &graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);
    return graph;
}

static int catalog_index(const char *id)
{
    if (!id) return -1;
    for (size_t i = 0U; i < openride_region_count(); ++i) {
        const OpenRideRegionDefinition *region = openride_region_at(i);
        if (region && strcmp(region->id, id) == 0) return (int)i;
    }
    return -1;
}

static void test_region_planning(void)
{
    const OpenRideRegionDefinition *npdc =
        openride_region_find("nord-pas-de-calais");
    const OpenRideRegionDefinition *picardie =
        openride_region_find("picardie");
    const OpenRideRegionDefinition *basse_normandie =
        openride_region_find("basse-normandie");
    assert(npdc && picardie && basse_normandie);
    assert(openride_region_count() <= OPENRIDE_REGION_NETWORK_MAX_REGIONS);

    bool installed[OPENRIDE_REGION_NETWORK_MAX_REGIONS] = {false};
    const int npdc_i = catalog_index(npdc->id);
    const int picardie_i = catalog_index(picardie->id);
    const int basse_i = catalog_index(basse_normandie->id);
    assert(npdc_i >= 0 && picardie_i >= 0 && basse_i >= 0);
    installed[npdc_i] = true;
    installed[picardie_i] = true;
    installed[basse_i] = true;

    OpenRideRoutingWorldResult planned = {0};
    char error[256] = {0};
    assert(openride_routing_world_plan_regions(
        npdc,
        50.3708,
        3.0802,
        basse_normandie,
        49.1829,
        -0.3707,
        installed,
        openride_region_count(),
        &planned,
        error,
        sizeof(error)));

    assert(planned.corridor_planned);
    assert(planned.multi_region);
    assert(planned.recommended_corridor.count > 2U);
    assert(planned.download_required);
    assert(planned.missing_region_count > 0U);
    assert(!planned.has_installed_alternative);
    assert(strcmp(planned.recommended_corridor.region_ids[0],
                  "nord-pas-de-calais") == 0);
    assert(strcmp(planned.recommended_corridor.region_ids[
                      planned.recommended_corridor.count - 1U],
                  "basse-normandie") == 0);

    /*
     * Once every region of the recommended corridor is marked installed, the
     * recommended corridor must stay identical and the download gate vanish.
     */
    for (uint32_t i = 0U; i < planned.recommended_corridor.count; ++i) {
        const int index =
            catalog_index(planned.recommended_corridor.region_ids[i]);
        assert(index >= 0);
        installed[index] = true;
    }

    OpenRideRoutingWorldResult ready = {0};
    assert(openride_routing_world_plan_regions(
        npdc,
        50.3708,
        3.0802,
        basse_normandie,
        49.1829,
        -0.3707,
        installed,
        openride_region_count(),
        &ready,
        error,
        sizeof(error)));
    assert(ready.corridor_planned);
    assert(!ready.download_required);
    assert(ready.missing_region_count == 0U);
    assert(ready.recommended_corridor.count
           == planned.recommended_corridor.count);
    for (uint32_t i = 0U; i < ready.recommended_corridor.count; ++i) {
        assert(strcmp(ready.recommended_corridor.region_ids[i],
                      planned.recommended_corridor.region_ids[i]) == 0);
    }
}

int main(void)
{
    test_region_planning();

    OpenRideRoutingGraph left = build_left();
    OpenRideRoutingGraph right = build_right();
    OpenRideRoute route = {0};
    OpenRideRoutingWorldResult result = {0};
    char error[256] = {0};

    assert(openride_routing_world_calculate_graph_pair(
        &left,
        &right,
        50.0000,
        2.9900,
        50.0000,
        3.0100,
        50.0,
        OPENRIDE_ROUTING_PROFILE_FASTEST,
        &route,
        &result,
        error,
        sizeof(error)));

    assert(result.shared_gateway_count >= 1U);
    assert(result.attempted_gateways >= 1U);
    assert(fabs(result.gateway_lon - 3.0000) < 1e-8);
    assert(route.nodes == NULL);
    assert(route.node_count == 0U);
    assert(route.geometry != NULL);
    assert(route.geometry_count >= 3U);
    assert(fabs(route.geometry[0].lon - 2.9900) < 1e-8);
    assert(fabs(route.geometry[route.geometry_count - 1U].lon - 3.0100) < 1e-8);
    assert(route.distance_m > 1999.0 && route.distance_m < 2001.0);

    openride_route_destroy(&route);
    openride_routing_graph_destroy(&left);
    openride_routing_graph_destroy(&right);
    puts("Routing world tests: OK");
    return 0;
}
