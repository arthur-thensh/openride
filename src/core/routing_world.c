#include "openride/routing_world.h"
#include "openride/routing_gateway_index.h"
#include "openride/region_network.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENRIDE_PI 3.14159265358979323846
#define OPENRIDE_EARTH_RADIUS_M 6371008.8

typedef struct OpenRideGatewayCandidate {
    OpenRideRoutingNodeId start_node;
    OpenRideRoutingNodeId destination_node;
    double score;
    double lat;
    double lon;
} OpenRideGatewayCandidate;

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
}

static double radians(double degrees)
{
    return degrees * OPENRIDE_PI / 180.0;
}

static double geo_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    const double phi1 = radians(lat1);
    const double phi2 = radians(lat2);
    const double dphi = radians(lat2 - lat1);
    const double dlambda = radians(lon2 - lon1);
    const double sin_dphi = sin(dphi * 0.5);
    const double sin_dlambda = sin(dlambda * 0.5);
    const double a = sin_dphi * sin_dphi
                   + cos(phi1) * cos(phi2) * sin_dlambda * sin_dlambda;
    const double clamped = a < 0.0 ? 0.0 : (a > 1.0 ? 1.0 : a);
    return OPENRIDE_EARTH_RADIUS_M
         * 2.0 * atan2(sqrt(clamped), sqrt(1.0 - clamped));
}

static uint8_t best_road_class(const OpenRideRoutingGraph *graph,
                               OpenRideRoutingNodeId node_id)
{
    if (!graph || node_id >= graph->node_count) return (uint8_t)OPENRIDE_ROAD_OTHER;
    const OpenRideRoutingNode *node = &graph->nodes[node_id];
    uint8_t best = (uint8_t)OPENRIDE_ROAD_OTHER;
    for (uint32_t i = 0U; i < node->edge_count; ++i) {
        const OpenRideRoutingEdge *edge = &graph->edges[node->first_edge + i];
        if (edge->road_class > 0U && edge->road_class < best) {
            best = edge->road_class;
        }
    }
    return best;
}

static double road_penalty(uint8_t road_class)
{
    if (road_class <= (uint8_t)OPENRIDE_ROAD_PRIMARY) return 0.0;
    if (road_class == (uint8_t)OPENRIDE_ROAD_SECONDARY) return 500.0;
    if (road_class == (uint8_t)OPENRIDE_ROAD_TERTIARY) return 1200.0;
    return 3000.0;
}

static bool inside_index_grid(const OpenRideRoutingGraph *graph,
                              const OpenRideRoutingNode *node)
{
    if (!graph || !node || !openride_routing_graph_has_spatial_index(graph)) return true;
    const OpenRideRoutingSpatialIndex *index = &graph->spatial_index;
    const int64_t max_lat = (int64_t)index->min_lat_e7
        + (int64_t)index->rows * (int64_t)index->cell_size_e7;
    const int64_t max_lon = (int64_t)index->min_lon_e7
        + (int64_t)index->columns * (int64_t)index->cell_size_e7;
    return (int64_t)node->lat_e7 >= index->min_lat_e7
        && (int64_t)node->lat_e7 <= max_lat
        && (int64_t)node->lon_e7 >= index->min_lon_e7
        && (int64_t)node->lon_e7 <= max_lon;
}

static void insert_candidate(
    OpenRideGatewayCandidate candidates[OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES],
    uint32_t *count,
    const OpenRideGatewayCandidate *candidate)
{
    if (!count || !candidate) return;

    for (uint32_t i = 0U; i < *count; ++i) {
        if (candidates[i].start_node == candidate->start_node
            && candidates[i].destination_node == candidate->destination_node) {
            return;
        }
    }

    uint32_t position = *count;
    if (position > OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES) {
        position = OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES;
    }
    while (position > 0U && candidates[position - 1U].score > candidate->score) {
        if (position < OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES) {
            candidates[position] = candidates[position - 1U];
        }
        --position;
    }
    if (position >= OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES) return;

    candidates[position] = *candidate;
    if (*count < OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES) ++(*count);
}

typedef struct OpenRideGatewayHashEntry {
    int32_t lat_e7;
    int32_t lon_e7;
    OpenRideRoutingNodeId node_id;
    bool occupied;
} OpenRideGatewayHashEntry;

static uint64_t gateway_coordinate_key(int32_t lat_e7, int32_t lon_e7)
{
    return ((uint64_t)(uint32_t)lat_e7 << 32U) | (uint64_t)(uint32_t)lon_e7;
}

static uint64_t gateway_hash_mix(uint64_t value)
{
    value ^= value >> 33U;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33U;
    value *= UINT64_C(0xc4ceb9fe1a85ec53);
    value ^= value >> 33U;
    return value;
}

static size_t gateway_hash_capacity(uint32_t node_count)
{
    size_t wanted = (size_t)node_count;
    if (wanted > SIZE_MAX / 2U) return 0U;
    wanted *= 2U;
    if (wanted < 16U) wanted = 16U;

    size_t capacity = 16U;
    while (capacity < wanted) {
        if (capacity > SIZE_MAX / 2U) return 0U;
        capacity *= 2U;
    }
    return capacity;
}

static bool gateway_hash_insert(OpenRideGatewayHashEntry *table,
                                size_t capacity,
                                const OpenRideRoutingNode *node,
                                OpenRideRoutingNodeId node_id)
{
    if (!table || capacity == 0U || !node) return false;

    const size_t mask = capacity - 1U;
    const uint64_t key = gateway_coordinate_key(node->lat_e7, node->lon_e7);
    size_t slot = (size_t)(gateway_hash_mix(key) & (uint64_t)mask);

    for (size_t probe = 0U; probe < capacity; ++probe) {
        OpenRideGatewayHashEntry *entry = &table[slot];
        if (!entry->occupied) {
            entry->lat_e7 = node->lat_e7;
            entry->lon_e7 = node->lon_e7;
            entry->node_id = node_id;
            entry->occupied = true;
            return true;
        }

        if (entry->lat_e7 == node->lat_e7 && entry->lon_e7 == node->lon_e7) {
            return true;
        }

        slot = (slot + 1U) & mask;
    }

    return false;
}

static OpenRideRoutingNodeId gateway_hash_find(
    const OpenRideGatewayHashEntry *table,
    size_t capacity,
    int32_t lat_e7,
    int32_t lon_e7)
{
    if (!table || capacity == 0U) return OPENRIDE_ROUTING_NODE_NONE;

    const size_t mask = capacity - 1U;
    const uint64_t key = gateway_coordinate_key(lat_e7, lon_e7);
    size_t slot = (size_t)(gateway_hash_mix(key) & (uint64_t)mask);

    for (size_t probe = 0U; probe < capacity; ++probe) {
        const OpenRideGatewayHashEntry *entry = &table[slot];
        if (!entry->occupied) return OPENRIDE_ROUTING_NODE_NONE;
        if (entry->lat_e7 == lat_e7 && entry->lon_e7 == lon_e7) {
            return entry->node_id;
        }
        slot = (slot + 1U) & mask;
    }

    return OPENRIDE_ROUTING_NODE_NONE;
}

static uint32_t find_gateways(
    const OpenRideRoutingGraph *start_graph,
    const OpenRideRoutingGraph *destination_graph,
    double start_lat,
    double start_lon,
    double destination_lat,
    double destination_lon,
    OpenRideGatewayCandidate candidates[OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES])
{
    const bool scan_start = start_graph->node_count <= destination_graph->node_count;
    const OpenRideRoutingGraph *scan_graph = scan_start ? start_graph : destination_graph;
    const OpenRideRoutingGraph *lookup_graph = scan_start ? destination_graph : start_graph;

    const size_t capacity = gateway_hash_capacity(lookup_graph->node_count);
    if (capacity == 0U
        || capacity > SIZE_MAX / sizeof(OpenRideGatewayHashEntry)) {
        return 0U;
    }

    OpenRideGatewayHashEntry *table =
        calloc(capacity, sizeof(OpenRideGatewayHashEntry));
    if (!table) return 0U;

    for (uint32_t node_id = 0U; node_id < lookup_graph->node_count; ++node_id) {
        const OpenRideRoutingNode *node = &lookup_graph->nodes[node_id];
        if (node->edge_count == 0U) continue;
        if (!gateway_hash_insert(table, capacity, node, node_id)) {
            free(table);
            return 0U;
        }
    }

    uint32_t count = 0U;
    for (uint32_t scan_id = 0U; scan_id < scan_graph->node_count; ++scan_id) {
        const OpenRideRoutingNode *node = &scan_graph->nodes[scan_id];
        if (node->edge_count == 0U || !inside_index_grid(lookup_graph, node)) continue;

        const OpenRideRoutingNodeId match_id =
            gateway_hash_find(table, capacity, node->lat_e7, node->lon_e7);
        if (match_id == OPENRIDE_ROUTING_NODE_NONE) continue;

        double lat = 0.0;
        double lon = 0.0;
        openride_routing_node_geo(node, &lat, &lon);

        OpenRideGatewayCandidate candidate;
        memset(&candidate, 0, sizeof(candidate));
        candidate.start_node = scan_start ? scan_id : match_id;
        candidate.destination_node = scan_start ? match_id : scan_id;
        candidate.lat = lat;
        candidate.lon = lon;
        candidate.score = geo_distance_m(start_lat, start_lon, lat, lon)
                        + geo_distance_m(lat, lon, destination_lat, destination_lon)
                        + road_penalty(best_road_class(start_graph,
                                                       candidate.start_node))
                        + road_penalty(best_road_class(destination_graph,
                                                       candidate.destination_node));
        insert_candidate(candidates, &count, &candidate);
    }

    free(table);
    return count;
}

static uint32_t find_gateways_from_persistent_index(
    const OpenRideRoutingGatewayIndex *index,
    bool reversed,
    double start_lat,
    double start_lon,
    double destination_lat,
    double destination_lon,
    OpenRideGatewayCandidate candidates[OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES])
{
    if (!index) return 0U;

    uint32_t count = 0U;
    for (uint32_t i = 0U; i < index->count; ++i) {
        const OpenRideRoutingGatewayRecord *record = &index->records[i];

        OpenRideGatewayCandidate candidate;
        memset(&candidate, 0, sizeof(candidate));
        candidate.start_node = reversed ? record->second_node : record->first_node;
        candidate.destination_node = reversed ? record->first_node : record->second_node;
        candidate.lat = (double)record->lat_e7 / 10000000.0;
        candidate.lon = (double)record->lon_e7 / 10000000.0;
        candidate.score = geo_distance_m(start_lat, start_lon,
                                         candidate.lat, candidate.lon)
                        + geo_distance_m(candidate.lat, candidate.lon,
                                         destination_lat, destination_lon)
                        + (double)record->road_penalty_m;
        insert_candidate(candidates, &count, &candidate);
    }
    return count;
}

static bool load_or_build_persistent_gateway_index(
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *start_region,
    const OpenRideRoutingGraph *start_graph,
    const OpenRideRegionDefinition *destination_region,
    const OpenRideRoutingGraph *destination_graph,
    OpenRideRoutingGatewayIndex *index,
    bool *reversed)
{
    if (reversed) *reversed = false;
    if (!paths || !start_region || !start_graph
        || !destination_region || !destination_graph || !index) {
        return false;
    }

    char path[512];
    if (!openride_routing_gateway_index_pair_path(
            path,
            sizeof(path),
            paths->routing_dir,
            start_region->id,
            destination_region->id)) {
        return false;
    }

    char local_error[192] = {0};
    if (openride_platform_file_exists(path)) {
        if (openride_routing_gateway_index_load(
                index, path, local_error, sizeof(local_error))) {
            bool loaded_reversed = false;
            if (openride_routing_gateway_index_matches(
                    index,
                    start_region->id,
                    start_graph,
                    destination_region->id,
                    destination_graph,
                    &loaded_reversed)) {
                if (reversed) *reversed = loaded_reversed;
                return true;
            }
        }

        openride_routing_gateway_index_destroy(index);
        openride_routing_gateway_index_init(index);
        remove(path);
    }

    if (!openride_routing_gateway_index_build(
            start_region->id,
            start_graph,
            destination_region->id,
            destination_graph,
            index,
            local_error,
            sizeof(local_error))) {
        openride_routing_gateway_index_destroy(index);
        openride_routing_gateway_index_init(index);
        return false;
    }

    (void)openride_routing_gateway_index_save(
        index, path, local_error, sizeof(local_error));
    if (reversed) *reversed = false;
    return true;
}

static bool calculate_nodes(const OpenRideRoutingGraph *graph,
                            OpenRideRoutingNodeId start,
                            OpenRideRoutingNodeId destination,
                            OpenRideRoutingProfile profile,
                            OpenRideRoute *route)
{
    OpenRideRoutingRequest request = openride_routing_request_default();
    request.start = start;
    request.destination = destination;
    request.profile = profile;
    char error[128] = {0};
    return openride_routing_engine_calculate(graph,
                                             &request,
                                             route,
                                             error,
                                             sizeof(error));
}

static bool append_point(OpenRideRoutePoint *geometry,
                         uint32_t capacity,
                         uint32_t *count,
                         double lat,
                         double lon)
{
    if (!geometry || !count || *count >= capacity) return false;
    if (*count > 0U) {
        const OpenRideRoutePoint *last = &geometry[*count - 1U];
        if (fabs(last->lat - lat) < 1e-10 && fabs(last->lon - lon) < 1e-10) {
            return true;
        }
    }
    geometry[*count] = (OpenRideRoutePoint){lat, lon};
    ++(*count);
    return true;
}

static bool combine_routes(const OpenRideRoute *first,
                           const OpenRideRoute *second,
                           double start_lat,
                           double start_lon,
                           double destination_lat,
                           double destination_lon,
                           OpenRideRoute *route)
{
    if (!first || !second || !route
        || !first->geometry || first->geometry_count == 0U
        || !second->geometry || second->geometry_count == 0U) {
        return false;
    }

    const uint64_t capacity64 = (uint64_t)first->geometry_count
                              + (uint64_t)second->geometry_count + 2U;
    if (capacity64 > UINT32_MAX) return false;

    const uint32_t capacity = (uint32_t)capacity64;
    OpenRideRoutePoint *geometry = calloc(capacity, sizeof(*geometry));
    if (!geometry) return false;

    uint32_t count = 0U;
    if (!append_point(geometry, capacity, &count, start_lat, start_lon)) goto fail;
    for (uint32_t i = 0U; i < first->geometry_count; ++i) {
        if (!append_point(geometry, capacity, &count,
                          first->geometry[i].lat, first->geometry[i].lon)) goto fail;
    }
    for (uint32_t i = 0U; i < second->geometry_count; ++i) {
        if (!append_point(geometry, capacity, &count,
                          second->geometry[i].lat, second->geometry[i].lon)) goto fail;
    }
    if (!append_point(geometry, capacity, &count,
                      destination_lat, destination_lon)) goto fail;

    const double start_link_m = geo_distance_m(start_lat,
                                                start_lon,
                                                first->geometry[0].lat,
                                                first->geometry[0].lon);
    const OpenRideRoutePoint *last =
        &second->geometry[second->geometry_count - 1U];
    const double destination_link_m = geo_distance_m(last->lat,
                                                      last->lon,
                                                      destination_lat,
                                                      destination_lon);
    openride_route_destroy(route);
    route->geometry = geometry;
    route->geometry_count = count;
    route->nodes = NULL;
    route->node_count = 0U;
    route->distance_m = first->distance_m + second->distance_m
                      + start_link_m + destination_link_m;
    route->estimated_time_s = first->estimated_time_s + second->estimated_time_s
                            + (start_link_m + destination_link_m) / (50.0 / 3.6);
    route->weighted_cost_s = first->weighted_cost_s + second->weighted_cost_s
                           + (start_link_m + destination_link_m) / (50.0 / 3.6);
    return true;

fail:
    free(geometry);
    return false;
}

static bool calculate_graph_pair_internal(
    const OpenRideRoutingGraph *start_graph,
    const OpenRideRoutingGraph *destination_graph,
    const OpenRideRoutingGatewayIndex *gateway_index,
    bool gateway_index_reversed,
    double start_lat,
    double start_lon,
    double destination_lat,
    double destination_lon,
    double max_snap_distance_m,
    OpenRideRoutingProfile profile,
    OpenRideRoute *route,
    OpenRideRoutingWorldResult *result,
    char *error,
    size_t error_size)
{
    if (result) memset(result, 0, sizeof(*result));
    if (!start_graph || !destination_graph || !route
        || start_graph == destination_graph
        || !isfinite(max_snap_distance_m)
        || max_snap_distance_m <= 0.0) {
        set_error(error, error_size, "invalid routing-world graph pair");
        return false;
    }

    double start_distance = INFINITY;
    double destination_distance = INFINITY;
    const OpenRideRoutingNodeId start_node =
        openride_routing_graph_nearest_node(start_graph,
                                            start_lat,
                                            start_lon,
                                            &start_distance);
    const OpenRideRoutingNodeId destination_node =
        openride_routing_graph_nearest_node(destination_graph,
                                            destination_lat,
                                            destination_lon,
                                            &destination_distance);
    if (start_node == OPENRIDE_ROUTING_NODE_NONE
        || destination_node == OPENRIDE_ROUTING_NODE_NONE
        || start_distance > max_snap_distance_m
        || destination_distance > max_snap_distance_m) {
        set_error(error, error_size, "routing-world endpoint is too far from a road");
        return false;
    }

    OpenRideGatewayCandidate candidates[
        OPENRIDE_ROUTING_WORLD_MAX_GATEWAY_CANDIDATES];
    memset(candidates, 0, sizeof(candidates));

    const uint32_t gateway_count = gateway_index
        ? find_gateways_from_persistent_index(
              gateway_index,
              gateway_index_reversed,
              start_lat,
              start_lon,
              destination_lat,
              destination_lon,
              candidates)
        : find_gateways(start_graph,
                        destination_graph,
                        start_lat,
                        start_lon,
                        destination_lat,
                        destination_lon,
                        candidates);

    if (result) result->shared_gateway_count = gateway_count;
    if (gateway_count == 0U) {
        set_error(error, error_size, "no shared routing gateway between regions");
        return false;
    }

    for (uint32_t i = 0U; i < gateway_count; ++i) {
        if (result) result->attempted_gateways = i + 1U;

        OpenRideRoute first = {0};
        OpenRideRoute second = {0};
        const bool first_ok = calculate_nodes(start_graph,
                                              start_node,
                                              candidates[i].start_node,
                                              profile,
                                              &first);
        const bool second_ok = first_ok
            && calculate_nodes(destination_graph,
                               candidates[i].destination_node,
                               destination_node,
                               profile,
                               &second);

        if (first_ok && second_ok
            && combine_routes(&first,
                              &second,
                              start_lat,
                              start_lon,
                              destination_lat,
                              destination_lon,
                              route)) {
            if (result) {
                result->gateway_lat = candidates[i].lat;
                result->gateway_lon = candidates[i].lon;
            }
            openride_route_destroy(&first);
            openride_route_destroy(&second);
            set_error(error, error_size, "");
            return true;
        }

        openride_route_destroy(&first);
        openride_route_destroy(&second);
    }

    set_error(error, error_size, "shared gateways exist but none is routable");
    return false;
}

bool openride_routing_world_calculate_graph_pair(
    const OpenRideRoutingGraph *start_graph,
    const OpenRideRoutingGraph *destination_graph,
    double start_lat,
    double start_lon,
    double destination_lat,
    double destination_lon,
    double max_snap_distance_m,
    OpenRideRoutingProfile profile,
    OpenRideRoute *route,
    OpenRideRoutingWorldResult *result,
    char *error,
    size_t error_size)
{
    return calculate_graph_pair_internal(
        start_graph,
        destination_graph,
        NULL,
        false,
        start_lat,
        start_lon,
        destination_lat,
        destination_lon,
        max_snap_distance_m,
        profile,
        route,
        result,
        error,
        error_size);
}

void openride_routing_world_cache_init(OpenRideRoutingWorldCache *cache)
{
    if (!cache) return;
    memset(cache, 0, sizeof(*cache));
}

void openride_routing_world_cache_destroy(OpenRideRoutingWorldCache *cache)
{
    if (!cache) return;
    if (cache->loaded) {
        openride_routing_graph_destroy(&cache->graph);
    }
    memset(cache, 0, sizeof(*cache));
}

static bool cache_matches_region(const OpenRideRoutingWorldCache *cache,
                                 const OpenRideRegionDefinition *region)
{
    return cache && cache->loaded && region
        && strcmp(cache->region_id, region->id) == 0;
}

static bool cache_load_region(OpenRideRoutingWorldCache *cache,
                              const OpenRideRegionDefinition *region,
                              const char *routing_path,
                              char *error,
                              size_t error_size)
{
    if (!cache || !region || !routing_path) {
        set_error(error, error_size, "invalid RoutingWorld cache request");
        return false;
    }

    openride_routing_world_cache_destroy(cache);
    if (!openride_routing_graph_load(&cache->graph,
                                     routing_path,
                                     error,
                                     error_size)) {
        return false;
    }

    snprintf(cache->region_id,
             sizeof(cache->region_id),
             "%s",
             region->id);
    cache->loaded = true;
    return true;
}


typedef struct OpenRideRoutingWorldLoadedGraph {
    const OpenRideRoutingGraph *graph;
    OpenRideRoutingGraph owned;
    bool owns_graph;
} OpenRideRoutingWorldLoadedGraph;

static void loaded_graph_destroy(OpenRideRoutingWorldLoadedGraph *loaded)
{
    if (!loaded) return;
    if (loaded->owns_graph) openride_routing_graph_destroy(&loaded->owned);
    memset(loaded, 0, sizeof(*loaded));
}

static bool region_point_in_poly(const char *path,
                                 double lat,
                                 double lon,
                                 bool *inside,
                                 char *error,
                                 size_t error_size)
{
    if (inside) *inside = false;
    if (!path || !inside) {
        set_error(error, error_size, "invalid region polygon lookup");
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        set_error(error, error_size, "unable to open region polygon");
        return false;
    }

    char line[512];
    bool first_line = true;
    bool ring_open = false;
    bool have_point = false;
    double first_lon = 0.0;
    double first_lat = 0.0;
    double previous_lon = 0.0;
    double previous_lat = 0.0;
    bool result = false;

    while (fgets(line, sizeof(line), file)) {
        char *cursor = line;
        while (*cursor == ' ' || *cursor == '\t'
               || *cursor == '\r' || *cursor == '\n') {
            ++cursor;
        }

        if (first_line) {
            first_line = false;
            continue;
        }
        if (*cursor == '\0') continue;

        if (strncmp(cursor, "END", 3U) == 0) {
            if (ring_open && have_point) {
                if ((previous_lat > lat) != (first_lat > lat)) {
                    const double crossing_lon =
                        previous_lon
                        + (first_lon - previous_lon)
                        * (lat - previous_lat)
                        / (first_lat - previous_lat);
                    if (lon < crossing_lon) result = !result;
                }
                ring_open = false;
                have_point = false;
            } else {
                break;
            }
            continue;
        }

        double point_lon = 0.0;
        double point_lat = 0.0;
        if (sscanf(cursor, "%lf %lf", &point_lon, &point_lat) == 2) {
            if (!ring_open) {
                ring_open = true;
                have_point = true;
                first_lon = point_lon;
                first_lat = point_lat;
                previous_lon = point_lon;
                previous_lat = point_lat;
            } else {
                if ((previous_lat > lat) != (point_lat > lat)) {
                    const double crossing_lon =
                        previous_lon
                        + (point_lon - previous_lon)
                        * (lat - previous_lat)
                        / (point_lat - previous_lat);
                    if (lon < crossing_lon) result = !result;
                }
                previous_lon = point_lon;
                previous_lat = point_lat;
            }
            continue;
        }

        ring_open = false;
        have_point = false;
    }

    fclose(file);
    *inside = result;
    set_error(error, error_size, "");
    return true;
}

static bool region_contains_point(const OpenRidePlatformPaths *paths,
                                  const OpenRideRegionDefinition *region,
                                  double lat,
                                  double lon,
                                  bool *inside)
{
    if (inside) *inside = false;
    if (!paths || !region || !inside) return false;

    OpenRideRegionStatus status;
    char local_error[128] = {0};
    if (!openride_region_get_status(paths,
                                    region,
                                    &status,
                                    local_error,
                                    sizeof(local_error))
        || !status.poly_present) {
        return false;
    }

    return region_point_in_poly(status.poly_path,
                                lat,
                                lon,
                                inside,
                                local_error,
                                sizeof(local_error));
}

static const OpenRideRegionDefinition *region_for_point(
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *preferred_region,
    double lat,
    double lon)
{
    if (preferred_region) {
        bool inside = false;
        if (region_contains_point(paths, preferred_region, lat, lon, &inside)
            && inside) {
            return preferred_region;
        }
    }

    for (size_t i = 0U; i < openride_region_count(); ++i) {
        const OpenRideRegionDefinition *region = openride_region_at(i);
        if (!region || region == preferred_region) continue;

        bool inside = false;
        if (region_contains_point(paths, region, lat, lon, &inside)
            && inside) {
            return region;
        }
    }
    return NULL;
}

static void copy_corridor_summary(
    const OpenRideRegionCorridor *source,
    OpenRideRoutingWorldCorridorSummary *destination)
{
    if (!destination) return;
    memset(destination, 0, sizeof(*destination));
    if (!source) return;

    destination->estimated_distance_m = source->estimated_distance_m;
    const uint32_t count =
        source->count < OPENRIDE_ROUTING_WORLD_MAX_CORRIDOR_REGIONS
            ? source->count
            : OPENRIDE_ROUTING_WORLD_MAX_CORRIDOR_REGIONS;
    destination->count = count;

    for (uint32_t i = 0U; i < count; ++i) {
        if (!source->regions[i]) continue;
        snprintf(destination->region_ids[i],
                 sizeof(destination->region_ids[i]),
                 "%s",
                 source->regions[i]->id);
    }
}

bool openride_routing_world_plan_regions(
    const OpenRideRegionDefinition *start_region,
    double start_lat,
    double start_lon,
    const OpenRideRegionDefinition *destination_region,
    double destination_lat,
    double destination_lon,
    const bool *installed,
    size_t installed_count,
    OpenRideRoutingWorldResult *result,
    char *error,
    size_t error_size)
{
    if (result) memset(result, 0, sizeof(*result));
    if (!start_region || !destination_region || !result
        || !isfinite(start_lat) || !isfinite(start_lon)
        || !isfinite(destination_lat) || !isfinite(destination_lon)) {
        set_error(error, error_size, "invalid RoutingWorld regional plan request");
        return false;
    }

    OpenRideRegionNetworkPlan plan;
    memset(&plan, 0, sizeof(plan));
    if (!openride_region_network_plan(start_region,
                                      start_lat,
                                      start_lon,
                                      destination_region,
                                      destination_lat,
                                      destination_lon,
                                      installed,
                                      installed_count,
                                      &plan,
                                      error,
                                      error_size)) {
        return false;
    }

    snprintf(result->start_region_id,
             sizeof(result->start_region_id),
             "%s",
             start_region->id);
    snprintf(result->destination_region_id,
             sizeof(result->destination_region_id),
             "%s",
             destination_region->id);
    result->multi_region =
        strcmp(start_region->id, destination_region->id) != 0;
    result->corridor_planned = true;

    copy_corridor_summary(&plan.recommended,
                          &result->recommended_corridor);

    const uint32_t missing_count =
        plan.missing_count < OPENRIDE_ROUTING_WORLD_MAX_CORRIDOR_REGIONS
            ? plan.missing_count
            : OPENRIDE_ROUTING_WORLD_MAX_CORRIDOR_REGIONS;
    result->missing_region_count = missing_count;
    result->download_required = missing_count > 0U;
    for (uint32_t i = 0U; i < missing_count; ++i) {
        if (!plan.missing_regions[i]) continue;
        snprintf(result->missing_region_ids[i],
                 sizeof(result->missing_region_ids[i]),
                 "%s",
                 plan.missing_regions[i]->id);
    }

    result->has_installed_alternative = plan.has_installed_alternative;
    if (plan.has_installed_alternative) {
        copy_corridor_summary(&plan.installed_alternative,
                              &result->installed_alternative);
    }

    set_error(error, error_size, "");
    return true;
}

static bool load_region_graph(const OpenRidePlatformPaths *paths,
                              const OpenRideRegionDefinition *region,
                              const OpenRideRegionDefinition *active_region,
                              const OpenRideRoutingGraph *active_graph,
                              OpenRideRoutingWorldCache *cache,
                              OpenRideRoutingWorldLoadedGraph *loaded,
                              char *error,
                              size_t error_size)
{
    if (!paths || !region || !loaded) {
        set_error(error, error_size, "invalid installed region graph request");
        return false;
    }

    memset(loaded, 0, sizeof(*loaded));
    if (active_region && active_graph
        && strcmp(region->id, active_region->id) == 0) {
        loaded->graph = active_graph;
        return true;
    }

    OpenRideRegionStatus status;
    if (!openride_region_get_status(paths,
                                    region,
                                    &status,
                                    error,
                                    error_size)
        || !status.routing_installed) {
        set_error(error, error_size, "routing graph is not installed for region");
        return false;
    }

    if (cache_matches_region(cache, region)) {
        loaded->graph = &cache->graph;
        return true;
    }

    if (cache
        && (!active_region || strcmp(region->id, active_region->id) != 0)) {
        if (!cache_load_region(cache,
                               region,
                               status.routing_path,
                               error,
                               error_size)) {
            return false;
        }
        loaded->graph = &cache->graph;
        return true;
    }

    if (!openride_routing_graph_load(&loaded->owned,
                                     status.routing_path,
                                     error,
                                     error_size)) {
        return false;
    }

    loaded->graph = &loaded->owned;
    loaded->owns_graph = true;
    return true;
}

static void detach_route_nodes(OpenRideRoute *route)
{
    if (!route) return;
    free(route->nodes);
    route->nodes = NULL;
    route->node_count = 0U;
}

static bool calculate_single_installed_region(
    const OpenRideRoutingGraph *graph,
    double start_lat,
    double start_lon,
    double destination_lat,
    double destination_lon,
    double max_snap_distance_m,
    OpenRideRoutingProfile profile,
    OpenRideRoute *route,
    char *error,
    size_t error_size)
{
    OpenRideRoutingSnap start_snap = {0};
    OpenRideRoutingSnap destination_snap = {0};
    start_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    destination_snap.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;

    if (!openride_routing_graph_snap_to_segment(graph,
                                                start_lat,
                                                start_lon,
                                                max_snap_distance_m,
                                                &start_snap)
        || !openride_routing_graph_snap_to_segment(graph,
                                                   destination_lat,
                                                   destination_lon,
                                                   max_snap_distance_m,
                                                   &destination_snap)) {
        set_error(error, error_size, "installed-region endpoint is too far from a road");
        return false;
    }

    OpenRideSnappedRoutingRequest request = openride_snapped_routing_request_default();
    request.start = start_snap;
    request.destination = destination_snap;
    request.profile = profile;

    if (!openride_routing_engine_calculate_snapped(graph,
                                                   &request,
                                                   route,
                                                   error,
                                                   error_size)) {
        return false;
    }

    detach_route_nodes(route);
    return true;
}

bool openride_routing_world_calculate_installed_cached(
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *active_region,
    const OpenRideRoutingGraph *active_graph,
    OpenRideRoutingWorldCache *cache,
    double start_lat,
    double start_lon,
    double destination_lat,
    double destination_lon,
    double max_snap_distance_m,
    OpenRideRoutingProfile profile,
    OpenRideRoute *route,
    OpenRideRoutingWorldResult *result,
    char *error,
    size_t error_size)
{
    if (result) memset(result, 0, sizeof(*result));
    if (!paths || !route
        || !isfinite(start_lat) || !isfinite(start_lon)
        || !isfinite(destination_lat) || !isfinite(destination_lon)) {
        set_error(error, error_size, "invalid installed routing-world request");
        return false;
    }

    /*
     * Region identification is based on every .poly already known locally,
     * including a contour downloaded for a region whose heavy offline package
     * has not been generated yet. Graph availability is checked separately.
     */
    const OpenRideRegionDefinition *start_region =
        region_for_point(paths, active_region, start_lat, start_lon);
    const OpenRideRegionDefinition *destination_region =
        region_for_point(paths, active_region, destination_lat, destination_lon);

    if (!start_region) {
        set_error(error, error_size, "start is outside known regional coverage");
        return false;
    }
    if (!destination_region) {
        set_error(error, error_size, "destination is outside known regional coverage");
        return false;
    }

    if (openride_region_count() > OPENRIDE_REGION_NETWORK_MAX_REGIONS) {
        set_error(error, error_size, "regional catalog exceeds RoutingWorld capacity");
        return false;
    }

    bool installed[OPENRIDE_REGION_NETWORK_MAX_REGIONS] = {false};
    if (!openride_region_network_installed_mask(paths,
                                                installed,
                                                OPENRIDE_REGION_NETWORK_MAX_REGIONS,
                                                error,
                                                error_size)) {
        return false;
    }

    OpenRideRoutingWorldResult local_result;
    memset(&local_result, 0, sizeof(local_result));
    if (!openride_routing_world_plan_regions(start_region,
                                             start_lat,
                                             start_lon,
                                             destination_region,
                                             destination_lat,
                                             destination_lon,
                                             installed,
                                             openride_region_count(),
                                             &local_result,
                                             error,
                                             error_size)) {
        return false;
    }

    /*
     * Missing regions stop the recommended route here. We deliberately do NOT
     * auto-select the installed-only alternative: it is a fallback choice for
     * the user, while the recommended corridor remains independent of download
     * state.
     */
    if (local_result.download_required) {
        if (result) *result = local_result;
        set_error(error, error_size, "additional regional download required");
        return false;
    }

    /*
     * RegionNetwork is now authoritative for corridor selection. Direct
     * routing remains implemented for one or two regions. Corridors with 3+
     * installed regions are surfaced distinctly so the next milestone can
     * execute them sequentially rather than incorrectly trying a direct
     * start/destination graph hand-off.
     */
    if (local_result.recommended_corridor.count > 2U) {
        if (result) *result = local_result;
        set_error(error, error_size, "multi-hop regional corridor ready");
        return false;
    }

    OpenRideRoutingWorldLoadedGraph start_loaded;
    OpenRideRoutingWorldLoadedGraph destination_loaded;
    memset(&start_loaded, 0, sizeof(start_loaded));
    memset(&destination_loaded, 0, sizeof(destination_loaded));

    if (!load_region_graph(paths,
                           start_region,
                           active_region,
                           active_graph,
                           cache,
                           &start_loaded,
                           error,
                           error_size)) {
        if (result) *result = local_result;
        return false;
    }

    bool ok = false;
    if (!local_result.multi_region) {
        ok = calculate_single_installed_region(start_loaded.graph,
                                               start_lat,
                                               start_lon,
                                               destination_lat,
                                               destination_lon,
                                               max_snap_distance_m,
                                               profile,
                                               route,
                                               error,
                                               error_size);
    } else {
        if (!load_region_graph(paths,
                               destination_region,
                               active_region,
                               active_graph,
                               cache,
                               &destination_loaded,
                               error,
                               error_size)) {
            loaded_graph_destroy(&start_loaded);
            if (result) *result = local_result;
            return false;
        }

        OpenRideRoutingGatewayIndex gateway_index;
        openride_routing_gateway_index_init(&gateway_index);
        bool gateway_index_reversed = false;
        const bool gateway_index_ready =
            load_or_build_persistent_gateway_index(
                paths,
                start_region,
                start_loaded.graph,
                destination_region,
                destination_loaded.graph,
                &gateway_index,
                &gateway_index_reversed);

        OpenRideRoutingWorldResult gateway_result;
        memset(&gateway_result, 0, sizeof(gateway_result));
        ok = calculate_graph_pair_internal(start_loaded.graph,
                                           destination_loaded.graph,
                                           gateway_index_ready ? &gateway_index : NULL,
                                           gateway_index_reversed,
                                           start_lat,
                                           start_lon,
                                           destination_lat,
                                           destination_lon,
                                           max_snap_distance_m,
                                           profile,
                                           route,
                                           &gateway_result,
                                           error,
                                           error_size);
        openride_routing_gateway_index_destroy(&gateway_index);
        local_result.shared_gateway_count = gateway_result.shared_gateway_count;
        local_result.attempted_gateways = gateway_result.attempted_gateways;
        local_result.gateway_lat = gateway_result.gateway_lat;
        local_result.gateway_lon = gateway_result.gateway_lon;
    }

    loaded_graph_destroy(&destination_loaded);
    loaded_graph_destroy(&start_loaded);
    if (result) *result = local_result;
    return ok;
}

bool openride_routing_world_calculate_installed(
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *active_region,
    const OpenRideRoutingGraph *active_graph,
    double start_lat,
    double start_lon,
    double destination_lat,
    double destination_lon,
    double max_snap_distance_m,
    OpenRideRoutingProfile profile,
    OpenRideRoute *route,
    OpenRideRoutingWorldResult *result,
    char *error,
    size_t error_size)
{
    return openride_routing_world_calculate_installed_cached(
        paths,
        active_region,
        active_graph,
        NULL,
        start_lat,
        start_lon,
        destination_lat,
        destination_lon,
        max_snap_distance_m,
        profile,
        route,
        result,
        error,
        error_size);
}
