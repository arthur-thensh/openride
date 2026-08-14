#include "openride/routing_world.h"
#include "openride/routing_gateway_index.h"
#include "openride/navigation_instructions.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static OpenRideRoutingGraph build_test_graph(const double (*points)[2],
                                                 uint32_t point_count)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder =
        openride_routing_graph_builder_create();
    assert(builder != NULL);

    OpenRideRoutingNodeId nodes[8];
    assert(point_count >= 2U && point_count <= 8U);
    for (uint32_t i = 0U; i < point_count; ++i) {
        nodes[i] = openride_routing_graph_builder_add_node(
            builder, points[i][0], points[i][1]);
        assert(nodes[i] != OPENRIDE_ROUTING_NODE_NONE);
    }

    OpenRideRoutingEdgeAttributes road =
        openride_routing_edge_attributes_default();
    road.length_m = 1000.0;
    road.road_class = OPENRIDE_ROAD_PRIMARY;
    road.surface = OPENRIDE_SURFACE_ASPHALT;
    road.max_speed_kph = 80U;

    for (uint32_t i = 1U; i < point_count; ++i) {
        assert(openride_routing_graph_builder_add_bidirectional_edge(
            builder, nodes[i - 1U], nodes[i], &road));
    }

    char error[256] = {0};
    assert(openride_routing_graph_builder_build(
        builder, &graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);
    return graph;
}

static void touch_file(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    fclose(file);
}

static void write_box_poly(const char *path,
                           const char *name,
                           double west,
                           double south,
                           double east,
                           double north)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    fprintf(file,
            "%s\n"
            "1\n"
            "  %.8f %.8f\n"
            "  %.8f %.8f\n"
            "  %.8f %.8f\n"
            "  %.8f %.8f\n"
            "  %.8f %.8f\n"
            "END\n"
            "END\n",
            name,
            west, south,
            east, south,
            east, north,
            west, north,
            west, south);
    fclose(file);
}

static void install_synthetic_region(
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *region,
    const OpenRideRoutingGraph *graph,
    double west,
    double south,
    double east,
    double north)
{
    OpenRideRegionStatus status;
    char error[256] = {0};
    assert(openride_region_get_status(
        paths, region, &status, error, sizeof(error)));
    assert(openride_routing_graph_save(
        graph, status.routing_path, error, sizeof(error)));
    touch_file(status.ormap_path);
    touch_file(status.search_path);
    write_box_poly(status.poly_path,
                   region->id,
                   west,
                   south,
                   east,
                   north);
}

static void test_installed_multi_hop(void)
{
    const OpenRideRegionDefinition *npdc =
        openride_region_find("nord-pas-de-calais");
    const OpenRideRegionDefinition *picardie =
        openride_region_find("picardie");
    const OpenRideRegionDefinition *haute_normandie =
        openride_region_find("haute-normandie");
    assert(npdc && picardie && haute_normandie);

    const double left_points[][2] = {
        {50.0000, 2.9000},
        {50.0000, 3.0000}
    };
    const double middle_points[][2] = {
        {50.0000, 3.0000},
        {49.7500, 2.0000},
        {49.5000, 1.2000}
    };
    const double right_points[][2] = {
        {49.5000, 1.2000},
        {49.4000, 1.0000}
    };

    OpenRideRoutingGraph left =
        build_test_graph(left_points, 2U);
    OpenRideRoutingGraph middle =
        build_test_graph(middle_points, 3U);
    OpenRideRoutingGraph right =
        build_test_graph(right_points, 2U);

    char root[256];
    snprintf(root,
             sizeof(root),
             "/tmp/openride-routing-world-%ld",
             (long)getpid());
    char command[320];
    snprintf(command, sizeof(command), "rm -rf '%s'", root);
    (void)system(command);
    assert(mkdir(root, 0700) == 0);

    OpenRidePlatformPaths paths;
    char error[512] = {0};
    assert(openride_platform_paths_init(&paths,
                                        OPENRIDE_PLATFORM_DESKTOP,
                                        root,
                                        error,
                                        sizeof(error)));
    assert(openride_platform_paths_ensure_directories(
        &paths, error, sizeof(error)));

    install_synthetic_region(&paths,
                             npdc,
                             &left,
                             2.70,
                             49.85,
                             3.10,
                             50.15);
    install_synthetic_region(&paths,
                             picardie,
                             &middle,
                             1.10,
                             49.40,
                             3.10,
                             50.10);
    install_synthetic_region(&paths,
                             haute_normandie,
                             &right,
                             0.80,
                             49.25,
                             1.40,
                             49.60);

    OpenRideRoutingWorldCache cache;
    openride_routing_world_cache_init(&cache);
    OpenRideRoute route = {0};
    OpenRideRoutingWorldResult result = {0};

    assert(openride_routing_world_calculate_installed_cached(
        &paths,
        npdc,
        &left,
        &cache,
        50.0000,
        2.9000,
        49.4000,
        1.0000,
        5000.0,
        OPENRIDE_ROUTING_PROFILE_FASTEST,
        &route,
        &result,
        error,
        sizeof(error)));

    assert(result.multi_region);
    assert(result.multi_hop);
    assert(result.routed_region_count == 3U);
    assert(result.recommended_corridor.count == 3U);
    assert(!result.download_required);
    assert(route.nodes == NULL);
    assert(route.node_count == 0U);
    assert(route.geometry != NULL);
    assert(route.geometry_count >= 5U);
    assert(fabs(route.geometry[0].lat - 50.0000) < 1e-8);
    assert(fabs(route.geometry[0].lon - 2.9000) < 1e-8);
    assert(fabs(route.geometry[route.geometry_count - 1U].lat - 49.4000)
           < 1e-8);
    assert(fabs(route.geometry[route.geometry_count - 1U].lon - 1.0000)
           < 1e-8);
    assert(route.distance_m > 3999.0 && route.distance_m < 4001.0);

    char gateway_path[512];
    assert(openride_routing_gateway_index_pair_path(
        gateway_path,
        sizeof(gateway_path),
        paths.routing_dir,
        npdc->id,
        picardie->id));
    assert(openride_platform_file_exists(gateway_path));
    assert(openride_routing_gateway_index_pair_path(
        gateway_path,
        sizeof(gateway_path),
        paths.routing_dir,
        picardie->id,
        haute_normandie->id));
    assert(openride_platform_file_exists(gateway_path));

    openride_route_destroy(&route);
    openride_routing_world_cache_destroy(&cache);
    openride_routing_graph_destroy(&left);
    openride_routing_graph_destroy(&middle);
    openride_routing_graph_destroy(&right);

    snprintf(command, sizeof(command), "rm -rf '%s'", root);
    (void)system(command);
}


static void test_region_hints_without_local_poly(void)
{
    const OpenRideRegionDefinition *npdc =
        openride_region_find("nord-pas-de-calais");
    const OpenRideRegionDefinition *aquitaine =
        openride_region_find("aquitaine");
    assert(npdc && aquitaine);

    char root[256];
    snprintf(root,
             sizeof(root),
             "/tmp/openride-routing-world-hints-%ld",
             (long)getpid());
    char command[320];
    snprintf(command, sizeof(command), "rm -rf '%s'", root);
    (void)system(command);
    assert(mkdir(root, 0700) == 0);

    OpenRidePlatformPaths paths;
    char error[512] = {0};
    assert(openride_platform_paths_init(&paths,
                                        OPENRIDE_PLATFORM_DESKTOP,
                                        root,
                                        error,
                                        sizeof(error)));
    assert(openride_platform_paths_ensure_directories(
        &paths, error, sizeof(error)));

    /*
     * Deliberately install no .poly and no regional package. Endpoint identity
     * comes exclusively from trusted PlaceWorld-style region hints.
     */
    OpenRideMapSelection selection;
    openride_map_selection_init(&selection);
    openride_map_selection_set(&selection,
                               OPENRIDE_MARKER_START,
                               50.3708,
                               3.0802);
    openride_map_selection_set_region_hint(&selection,
                                           OPENRIDE_MARKER_START,
                                           npdc->id);
    openride_map_selection_set(&selection,
                               OPENRIDE_MARKER_DESTINATION,
                               44.8378,
                               -0.5792);
    openride_map_selection_set_region_hint(&selection,
                                           OPENRIDE_MARKER_DESTINATION,
                                           aquitaine->id);

    OpenRideRoutingWorldCache cache;
    openride_routing_world_cache_init(&cache);
    OpenRideRoute route = {0};
    OpenRideRoutingWorldResult result = {0};

    const bool ok = openride_routing_world_calculate_selection_cached(
        &paths,
        npdc,
        NULL,
        &cache,
        &selection,
        2000.0,
        OPENRIDE_ROUTING_PROFILE_TOURING,
        &route,
        &result,
        error,
        sizeof(error));

    assert(!ok);
    assert(result.corridor_planned);
    assert(result.download_required);
    assert(result.missing_region_count > 0U);
    assert(strcmp(result.start_region_id, npdc->id) == 0);
    assert(strcmp(result.destination_region_id, aquitaine->id) == 0);
    assert(result.recommended_corridor.count >= 2U);
    assert(strcmp(result.recommended_corridor.region_ids[0],
                  npdc->id) == 0);
    assert(strcmp(result.recommended_corridor.region_ids[
                      result.recommended_corridor.count - 1U],
                  aquitaine->id) == 0);

    openride_route_destroy(&route);
    openride_routing_world_cache_destroy(&cache);
    snprintf(command, sizeof(command), "rm -rf '%s'", root);
    (void)system(command);
}

static OpenRideRoutingGraph build_navigation_context_left(void)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder =
        openride_routing_graph_builder_create();
    assert(builder != NULL);

    const OpenRideRoutingNodeId start =
        openride_routing_graph_builder_add_node(builder, 50.0000, 2.9990);
    const OpenRideRoutingNodeId gateway =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0000);

    OpenRideRoutingEdgeAttributes road =
        openride_routing_edge_attributes_default();
    road.length_m = 100.0;
    road.road_class = OPENRIDE_ROAD_PRIMARY;
    road.surface = OPENRIDE_SURFACE_ASPHALT;
    road.max_speed_kph = 50U;

    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, start, gateway, &road));

    char error[256] = {0};
    assert(openride_routing_graph_builder_build(
        builder, &graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);
    return graph;
}

static OpenRideRoutingGraph build_navigation_context_right(void)
{
    OpenRideRoutingGraph graph = {0};
    OpenRideRoutingGraphBuilder *builder =
        openride_routing_graph_builder_create();
    assert(builder != NULL);

    const OpenRideRoutingNodeId gateway =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0000);
    const OpenRideRoutingNodeId entry =
        openride_routing_graph_builder_add_node(builder, 50.0010, 3.0000);
    const OpenRideRoutingNodeId roundabout_a =
        openride_routing_graph_builder_add_node(builder, 50.0015, 3.0005);
    const OpenRideRoutingNodeId roundabout_b =
        openride_routing_graph_builder_add_node(builder, 50.0010, 3.0010);
    const OpenRideRoutingNodeId destination =
        openride_routing_graph_builder_add_node(builder, 50.0000, 3.0010);
    const OpenRideRoutingNodeId first_exit =
        openride_routing_graph_builder_add_node(builder, 50.0022, 3.0005);

    OpenRideRoutingEdgeAttributes normal =
        openride_routing_edge_attributes_default();
    normal.length_m = 100.0;
    normal.road_class = OPENRIDE_ROAD_PRIMARY;
    normal.surface = OPENRIDE_SURFACE_ASPHALT;
    normal.max_speed_kph = 50U;

    OpenRideRoutingEdgeAttributes roundabout = normal;
    roundabout.flags |= OPENRIDE_EDGE_FLAG_ROUNDABOUT;

    assert(openride_routing_graph_builder_add_bidirectional_edge(
        builder, gateway, entry, &normal));
    assert(openride_routing_graph_builder_add_directed_edge(
        builder, entry, roundabout_a, &roundabout));
    assert(openride_routing_graph_builder_add_directed_edge(
        builder, roundabout_a, roundabout_b, &roundabout));
    assert(openride_routing_graph_builder_add_directed_edge(
        builder, roundabout_a, first_exit, &normal));
    assert(openride_routing_graph_builder_add_directed_edge(
        builder, roundabout_b, destination, &normal));

    char error[256] = {0};
    assert(openride_routing_graph_builder_build(
        builder, &graph, error, sizeof(error)));
    openride_routing_graph_builder_destroy(builder);
    return graph;
}

static void test_multi_region_navigation_context(void)
{
    OpenRideRoutingGraph left = build_navigation_context_left();
    OpenRideRoutingGraph right = build_navigation_context_right();
    OpenRideRoute route = {0};
    OpenRideRoutingWorldResult result = {0};
    char error[256] = {0};

    assert(openride_routing_world_calculate_graph_pair(
        &left,
        &right,
        50.0000,
        2.9990,
        50.0000,
        3.0010,
        50.0,
        OPENRIDE_ROUTING_PROFILE_FASTEST,
        &route,
        &result,
        error,
        sizeof(error)));

    assert(route.nodes == NULL);
    assert(route.node_count == 0U);
    assert(route.navigation_context != NULL);
    assert(route.navigation_context_count == route.geometry_count);

    OpenRideNavigationInstructionList instructions = {0};
    assert(openride_navigation_instructions_build(
        NULL,
        &route,
        &instructions,
        error,
        sizeof(error)));

    bool found_roundabout = false;
    for (uint32_t i = 0U; i < instructions.count; ++i) {
        if (instructions.items[i].maneuver == OPENRIDE_MANEUVER_ROUNDABOUT) {
            found_roundabout = true;
            assert(instructions.items[i].roundabout_exit_number == 2U);
        }
    }
    assert(found_roundabout);

    openride_navigation_instructions_destroy(&instructions);
    openride_route_destroy(&route);
    openride_routing_graph_destroy(&left);
    openride_routing_graph_destroy(&right);
}

int main(void)
{
    test_region_planning();
    test_region_hints_without_local_poly();
    test_installed_multi_hop();
    test_multi_region_navigation_context();

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
