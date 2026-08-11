#include "openride/routing_graph.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENRIDE_PI 3.14159265358979323846
#define OPENRIDE_EARTH_RADIUS_M 6371008.8
#define OPENRIDE_GRAPH_MAGIC "ORGRAPH1"
#define OPENRIDE_SPATIAL_INDEX_MAGIC "ORIDX001"
#define OPENRIDE_GRAPH_LEGACY_VERSION 1U
#define OPENRIDE_GRAPH_FEATURE_SPATIAL_INDEX 1U
#define OPENRIDE_NODE_RECORD_SIZE 16U
#define OPENRIDE_EDGE_RECORD_SIZE 16U

typedef struct OpenRidePendingEdge {
    uint32_t from;
    uint32_t to;
    OpenRideRoutingEdgeAttributes attributes;
} OpenRidePendingEdge;

struct OpenRideRoutingGraphBuilder {
    int32_t *lat_e7;
    int32_t *lon_e7;
    uint32_t node_count;
    uint32_t node_capacity;

    OpenRidePendingEdge *edges;
    uint32_t edge_count;
    uint32_t edge_capacity;
};

_Static_assert(sizeof(OpenRideRoutingNode) == 16U,
               "OpenRideRoutingNode must stay compact");
_Static_assert(sizeof(OpenRideRoutingEdge) == 16U,
               "OpenRideRoutingEdge must stay compact");

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "unknown error");
}

static bool valid_geo(double lat, double lon)
{
    return isfinite(lat) && isfinite(lon)
        && lat >= -90.0 && lat <= 90.0
        && lon >= -180.0 && lon <= 180.0;
}

static int32_t degree_to_e7(double value)
{
    return (int32_t)llround(value * 10000000.0);
}

static double e7_to_degree(int32_t value)
{
    return (double)value / 10000000.0;
}

static double deg_to_rad(double degrees)
{
    return degrees * OPENRIDE_PI / 180.0;
}

static double geo_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    const double phi1 = deg_to_rad(lat1);
    const double phi2 = deg_to_rad(lat2);
    const double dphi = deg_to_rad(lat2 - lat1);
    const double dlambda = deg_to_rad(lon2 - lon1);
    const double sin_dphi = sin(dphi * 0.5);
    const double sin_dlambda = sin(dlambda * 0.5);
    const double a = sin_dphi * sin_dphi
                   + cos(phi1) * cos(phi2) * sin_dlambda * sin_dlambda;
    const double clamped = a < 0.0 ? 0.0 : (a > 1.0 ? 1.0 : a);
    return OPENRIDE_EARTH_RADIUS_M
         * (2.0 * atan2(sqrt(clamped), sqrt(1.0 - clamped)));
}

static bool grow_nodes(OpenRideRoutingGraphBuilder *builder)
{
    uint32_t new_capacity = builder->node_capacity == 0U
        ? 256U
        : builder->node_capacity * 2U;

    if (new_capacity < builder->node_capacity) return false;

    int32_t *new_lat = realloc(builder->lat_e7,
                               (size_t)new_capacity * sizeof(*new_lat));
    if (!new_lat) return false;
    builder->lat_e7 = new_lat;

    int32_t *new_lon = realloc(builder->lon_e7,
                               (size_t)new_capacity * sizeof(*new_lon));
    if (!new_lon) return false;
    builder->lon_e7 = new_lon;
    builder->node_capacity = new_capacity;
    return true;
}

static bool grow_edges(OpenRideRoutingGraphBuilder *builder)
{
    uint32_t new_capacity = builder->edge_capacity == 0U
        ? 512U
        : builder->edge_capacity * 2U;

    if (new_capacity < builder->edge_capacity) return false;

    OpenRidePendingEdge *new_edges = realloc(
        builder->edges,
        (size_t)new_capacity * sizeof(*new_edges));
    if (!new_edges) return false;

    builder->edges = new_edges;
    builder->edge_capacity = new_capacity;
    return true;
}

static int pending_edge_compare(const void *a, const void *b)
{
    const OpenRidePendingEdge *ea = a;
    const OpenRidePendingEdge *eb = b;

    if (ea->from < eb->from) return -1;
    if (ea->from > eb->from) return 1;
    if (ea->to < eb->to) return -1;
    if (ea->to > eb->to) return 1;
    return 0;
}

static bool write_exact(FILE *file, const void *data, size_t size)
{
    return fwrite(data, 1U, size, file) == size;
}

static bool read_exact(FILE *file, void *data, size_t size)
{
    return fread(data, 1U, size, file) == size;
}

static bool write_u32_le(FILE *file, uint32_t value)
{
    const unsigned char bytes[4] = {
        (unsigned char)(value & 0xffU),
        (unsigned char)((value >> 8U) & 0xffU),
        (unsigned char)((value >> 16U) & 0xffU),
        (unsigned char)((value >> 24U) & 0xffU)
    };
    return write_exact(file, bytes, sizeof(bytes));
}

static bool write_u16_le(FILE *file, uint16_t value)
{
    const unsigned char bytes[2] = {
        (unsigned char)(value & 0xffU),
        (unsigned char)((value >> 8U) & 0xffU)
    };
    return write_exact(file, bytes, sizeof(bytes));
}

static bool read_u32_le(FILE *file, uint32_t *value)
{
    unsigned char bytes[4];
    if (!read_exact(file, bytes, sizeof(bytes))) return false;
    *value = (uint32_t)bytes[0]
           | ((uint32_t)bytes[1] << 8U)
           | ((uint32_t)bytes[2] << 16U)
           | ((uint32_t)bytes[3] << 24U);
    return true;
}

static bool read_u16_le(FILE *file, uint16_t *value)
{
    unsigned char bytes[2];
    if (!read_exact(file, bytes, sizeof(bytes))) return false;
    *value = (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
    return true;
}

OpenRideRoutingEdgeAttributes openride_routing_edge_attributes_default(void)
{
    OpenRideRoutingEdgeAttributes attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.road_class = OPENRIDE_ROAD_UNKNOWN;
    attributes.surface = OPENRIDE_SURFACE_UNKNOWN;
    return attributes;
}

OpenRideRoutingGraphBuilder *openride_routing_graph_builder_create(void)
{
    return calloc(1U, sizeof(OpenRideRoutingGraphBuilder));
}

void openride_routing_graph_builder_destroy(OpenRideRoutingGraphBuilder *builder)
{
    if (!builder) return;
    free(builder->lat_e7);
    free(builder->lon_e7);
    free(builder->edges);
    free(builder);
}

OpenRideRoutingNodeId openride_routing_graph_builder_add_node(
    OpenRideRoutingGraphBuilder *builder,
    double lat,
    double lon)
{
    if (!builder || !valid_geo(lat, lon)) return OPENRIDE_ROUTING_NODE_NONE;
    if (builder->node_count == UINT32_MAX) return OPENRIDE_ROUTING_NODE_NONE;

    if (builder->node_count == builder->node_capacity && !grow_nodes(builder)) {
        return OPENRIDE_ROUTING_NODE_NONE;
    }

    const uint32_t id = builder->node_count++;
    builder->lat_e7[id] = degree_to_e7(lat);
    builder->lon_e7[id] = degree_to_e7(lon);
    return id;
}

bool openride_routing_graph_builder_add_directed_edge(
    OpenRideRoutingGraphBuilder *builder,
    OpenRideRoutingNodeId from,
    OpenRideRoutingNodeId to,
    const OpenRideRoutingEdgeAttributes *attributes)
{
    if (!builder || from >= builder->node_count || to >= builder->node_count) {
        return false;
    }
    if (builder->edge_count == UINT32_MAX) return false;

    if (builder->edge_count == builder->edge_capacity && !grow_edges(builder)) {
        return false;
    }

    OpenRideRoutingEdgeAttributes resolved = attributes
        ? *attributes
        : openride_routing_edge_attributes_default();

    if (!isfinite(resolved.length_m) || resolved.length_m <= 0.0) {
        resolved.length_m = geo_distance_m(
            e7_to_degree(builder->lat_e7[from]),
            e7_to_degree(builder->lon_e7[from]),
            e7_to_degree(builder->lat_e7[to]),
            e7_to_degree(builder->lon_e7[to]));
    }

    if (resolved.length_m < 0.0 || resolved.length_m > 42949672.95) {
        return false;
    }

    OpenRidePendingEdge *edge = &builder->edges[builder->edge_count++];
    edge->from = from;
    edge->to = to;
    edge->attributes = resolved;
    return true;
}

bool openride_routing_graph_builder_add_bidirectional_edge(
    OpenRideRoutingGraphBuilder *builder,
    OpenRideRoutingNodeId a,
    OpenRideRoutingNodeId b,
    const OpenRideRoutingEdgeAttributes *attributes)
{
    const uint32_t edge_count_before = builder ? builder->edge_count : 0U;

    if (!openride_routing_graph_builder_add_directed_edge(builder, a, b, attributes)) {
        return false;
    }

    if (!openride_routing_graph_builder_add_directed_edge(builder, b, a, attributes)) {
        if (builder) builder->edge_count = edge_count_before;
        return false;
    }

    return true;
}

bool openride_routing_graph_builder_build(
    OpenRideRoutingGraphBuilder *builder,
    OpenRideRoutingGraph *graph,
    char *error,
    size_t error_size)
{
    if (!builder || !graph) {
        set_error(error, error_size, "invalid graph builder");
        return false;
    }

    OpenRideRoutingGraph result;
    memset(&result, 0, sizeof(result));

    if (builder->edge_count > 1U) {
        qsort(builder->edges,
              builder->edge_count,
              sizeof(builder->edges[0]),
              pending_edge_compare);
    }

    if (builder->node_count > 0U) {
        result.nodes = calloc(builder->node_count, sizeof(*result.nodes));
        if (!result.nodes) {
            set_error(error, error_size, "unable to allocate routing nodes");
            return false;
        }
    }

    if (builder->edge_count > 0U) {
        result.edges = calloc(builder->edge_count, sizeof(*result.edges));
        if (!result.edges) {
            free(result.nodes);
            set_error(error, error_size, "unable to allocate routing edges");
            return false;
        }
    }

    result.node_count = builder->node_count;
    result.edge_count = builder->edge_count;

    for (uint32_t i = 0U; i < builder->node_count; ++i) {
        result.nodes[i].lat_e7 = builder->lat_e7[i];
        result.nodes[i].lon_e7 = builder->lon_e7[i];
    }

    uint32_t cursor = 0U;
    for (uint32_t node_id = 0U; node_id < builder->node_count; ++node_id) {
        OpenRideRoutingNode *node = &result.nodes[node_id];
        node->first_edge = cursor;

        while (cursor < builder->edge_count
               && builder->edges[cursor].from == node_id) {
            const OpenRidePendingEdge *pending = &builder->edges[cursor];
            OpenRideRoutingEdge *edge = &result.edges[cursor];
            double length_cm = pending->attributes.length_m * 100.0;
            if (length_cm < 0.0) length_cm = 0.0;
            if (length_cm > 4294967295.0) length_cm = 4294967295.0;

            edge->target = pending->to;
            edge->length_cm = (uint32_t)llround(length_cm);
            edge->flags = pending->attributes.flags;
            edge->road_class = (uint8_t)pending->attributes.road_class;
            edge->surface = (uint8_t)pending->attributes.surface;
            edge->max_speed_kph = pending->attributes.max_speed_kph;

            ++node->edge_count;
            ++cursor;
        }
    }

    if (!openride_routing_graph_build_spatial_index(&result, error, error_size)) {
        openride_routing_graph_destroy(&result);
        return false;
    }

    if (!openride_routing_graph_validate(&result, error, error_size)) {
        openride_routing_graph_destroy(&result);
        return false;
    }

    openride_routing_graph_destroy(graph);
    *graph = result;
    set_error(error, error_size, "");
    return true;
}

void openride_routing_graph_destroy(OpenRideRoutingGraph *graph)
{
    if (!graph) return;
    free(graph->nodes);
    free(graph->edges);
    openride_routing_spatial_index_destroy(&graph->spatial_index);
    memset(graph, 0, sizeof(*graph));
}

bool openride_routing_graph_validate(const OpenRideRoutingGraph *graph,
                                     char *error,
                                     size_t error_size)
{
    if (!graph) {
        set_error(error, error_size, "routing graph is null");
        return false;
    }
    if (graph->node_count > 0U && !graph->nodes) {
        set_error(error, error_size, "routing graph has no node array");
        return false;
    }
    if (graph->edge_count > 0U && !graph->edges) {
        set_error(error, error_size, "routing graph has no edge array");
        return false;
    }

    uint32_t expected_first = 0U;
    for (uint32_t i = 0U; i < graph->node_count; ++i) {
        const OpenRideRoutingNode *node = &graph->nodes[i];
        if (node->first_edge != expected_first) {
            set_error(error, error_size, "routing node edge ranges are not contiguous");
            return false;
        }
        if (node->edge_count > graph->edge_count - expected_first) {
            set_error(error, error_size, "routing node edge range is out of bounds");
            return false;
        }
        expected_first += node->edge_count;
    }

    if (expected_first != graph->edge_count) {
        set_error(error, error_size, "routing graph contains unowned edges");
        return false;
    }

    for (uint32_t i = 0U; i < graph->edge_count; ++i) {
        if (graph->edges[i].target >= graph->node_count) {
            set_error(error, error_size, "routing edge target is out of bounds");
            return false;
        }
    }

    if (!openride_routing_spatial_index_validate(graph, error, error_size)) {
        return false;
    }

    set_error(error, error_size, "");
    return true;
}

void openride_routing_node_geo(const OpenRideRoutingNode *node,
                               double *lat,
                               double *lon)
{
    if (!node) return;
    if (lat) *lat = e7_to_degree(node->lat_e7);
    if (lon) *lon = e7_to_degree(node->lon_e7);
}

OpenRideRoutingNodeId openride_routing_graph_nearest_node_linear(
    const OpenRideRoutingGraph *graph,
    double lat,
    double lon,
    double *distance_m)
{
    if (distance_m) *distance_m = INFINITY;
    if (!graph || graph->node_count == 0U || !valid_geo(lat, lon)) {
        return OPENRIDE_ROUTING_NODE_NONE;
    }

    OpenRideRoutingNodeId best_id = OPENRIDE_ROUTING_NODE_NONE;
    double best_distance = INFINITY;

    for (uint32_t i = 0U; i < graph->node_count; ++i) {
        double node_lat = 0.0;
        double node_lon = 0.0;
        openride_routing_node_geo(&graph->nodes[i], &node_lat, &node_lon);
        const double distance = geo_distance_m(lat, lon, node_lat, node_lon);
        if (distance < best_distance) {
            best_distance = distance;
            best_id = i;
        }
    }

    if (distance_m) *distance_m = best_distance;
    return best_id;
}

OpenRideRoutingNodeId openride_routing_graph_nearest_node(
    const OpenRideRoutingGraph *graph,
    double lat,
    double lon,
    double *distance_m)
{
    if (openride_routing_graph_has_spatial_index(graph)) {
        return openride_routing_spatial_index_nearest_node(
            graph, lat, lon, distance_m);
    }
    return openride_routing_graph_nearest_node_linear(graph, lat, lon, distance_m);
}

static bool write_spatial_index(FILE *file,
                                const OpenRideRoutingGraph *graph)
{
    const OpenRideRoutingSpatialIndex *index = &graph->spatial_index;
    bool ok = write_exact(file, OPENRIDE_SPATIAL_INDEX_MAGIC, 8U)
           && write_u32_le(file, (uint32_t)index->min_lat_e7)
           && write_u32_le(file, (uint32_t)index->min_lon_e7)
           && write_u32_le(file, index->cell_size_e7)
           && write_u32_le(file, index->rows)
           && write_u32_le(file, index->columns)
           && write_u32_le(file, index->cell_count)
           && write_u32_le(file, graph->node_count);

    if (index->cell_count > 0U) {
        for (uint32_t i = 0U; ok && i <= index->cell_count; ++i) {
            ok = write_u32_le(file, index->cell_offsets[i]);
        }
    }
    for (uint32_t i = 0U; ok && i < graph->node_count; ++i) {
        ok = write_u32_le(file, index->node_ids[i]);
    }
    return ok;
}

static bool read_spatial_index(FILE *file,
                               OpenRideRoutingGraph *graph,
                               char *error,
                               size_t error_size)
{
    char magic[8];
    uint32_t min_lat_raw = 0U;
    uint32_t min_lon_raw = 0U;
    uint32_t node_ref_count = 0U;
    OpenRideRoutingSpatialIndex index;
    memset(&index, 0, sizeof(index));

    bool ok = read_exact(file, magic, sizeof(magic))
           && read_u32_le(file, &min_lat_raw)
           && read_u32_le(file, &min_lon_raw)
           && read_u32_le(file, &index.cell_size_e7)
           && read_u32_le(file, &index.rows)
           && read_u32_le(file, &index.columns)
           && read_u32_le(file, &index.cell_count)
           && read_u32_le(file, &node_ref_count);

    index.min_lat_e7 = (int32_t)min_lat_raw;
    index.min_lon_e7 = (int32_t)min_lon_raw;

    if (!ok || memcmp(magic, OPENRIDE_SPATIAL_INDEX_MAGIC, 8U) != 0) {
        set_error(error, error_size, "invalid routing spatial index header");
        return false;
    }
    if (node_ref_count != graph->node_count
        || (uint64_t)index.rows * (uint64_t)index.columns != index.cell_count
        || index.cell_count > 2000000U) {
        set_error(error, error_size, "invalid routing spatial index dimensions");
        return false;
    }

    if (index.cell_count > 0U) {
        index.cell_offsets = calloc((size_t)index.cell_count + 1U,
                                    sizeof(*index.cell_offsets));
        if (!index.cell_offsets) ok = false;
    }
    if (ok && graph->node_count > 0U) {
        index.node_ids = malloc((size_t)graph->node_count * sizeof(*index.node_ids));
        if (!index.node_ids) ok = false;
    }

    if (index.cell_count > 0U) {
        for (uint32_t i = 0U; ok && i <= index.cell_count; ++i) {
            ok = read_u32_le(file, &index.cell_offsets[i]);
        }
    }
    for (uint32_t i = 0U; ok && i < graph->node_count; ++i) {
        ok = read_u32_le(file, &index.node_ids[i]);
    }

    if (!ok) {
        openride_routing_spatial_index_destroy(&index);
        set_error(error, error_size, "routing spatial index is truncated");
        return false;
    }

    graph->spatial_index = index;
    if (!openride_routing_spatial_index_validate(graph, error, error_size)) {
        openride_routing_spatial_index_destroy(&graph->spatial_index);
        return false;
    }
    return true;
}

bool openride_routing_graph_save(const OpenRideRoutingGraph *graph,
                                 const char *path,
                                 char *error,
                                 size_t error_size)
{
    if (!path || !openride_routing_graph_validate(graph, error, error_size)) {
        if (!path) set_error(error, error_size, "graph output path is null");
        return false;
    }

    FILE *file = fopen(path, "wb");
    if (!file) {
        set_error(error, error_size, "unable to create routing graph file");
        return false;
    }

    bool ok = write_exact(file, OPENRIDE_GRAPH_MAGIC, 8U)
           && write_u32_le(file, OPENRIDE_ROUTING_GRAPH_FORMAT_VERSION)
           && write_u32_le(file, graph->node_count)
           && write_u32_le(file, graph->edge_count)
           && write_u32_le(file, OPENRIDE_NODE_RECORD_SIZE)
           && write_u32_le(file, OPENRIDE_EDGE_RECORD_SIZE)
           && write_u32_le(file, OPENRIDE_GRAPH_FEATURE_SPATIAL_INDEX);

    for (uint32_t i = 0U; ok && i < graph->node_count; ++i) {
        const OpenRideRoutingNode *node = &graph->nodes[i];
        ok = write_u32_le(file, (uint32_t)node->lat_e7)
          && write_u32_le(file, (uint32_t)node->lon_e7)
          && write_u32_le(file, node->first_edge)
          && write_u32_le(file, node->edge_count);
    }

    for (uint32_t i = 0U; ok && i < graph->edge_count; ++i) {
        const OpenRideRoutingEdge *edge = &graph->edges[i];
        const unsigned char class_and_surface[2] = {
            edge->road_class,
            edge->surface
        };
        ok = write_u32_le(file, edge->target)
          && write_u32_le(file, edge->length_cm)
          && write_u32_le(file, edge->flags)
          && write_exact(file, class_and_surface, sizeof(class_and_surface))
          && write_u16_le(file, edge->max_speed_kph);
    }

    if (ok) ok = write_spatial_index(file, graph);
    if (fclose(file) != 0) ok = false;

    if (!ok) {
        remove(path);
        set_error(error, error_size, "unable to write complete routing graph file");
        return false;
    }

    set_error(error, error_size, "");
    return true;
}

bool openride_routing_graph_load(OpenRideRoutingGraph *graph,
                                 const char *path,
                                 char *error,
                                 size_t error_size)
{
    if (!graph || !path) {
        set_error(error, error_size, "invalid routing graph load arguments");
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        set_error(error, error_size, "unable to open routing graph file");
        return false;
    }

    char magic[8];
    uint32_t version = 0U;
    uint32_t node_count = 0U;
    uint32_t edge_count = 0U;
    uint32_t node_record_size = 0U;
    uint32_t edge_record_size = 0U;
    uint32_t features = 0U;

    bool ok = read_exact(file, magic, sizeof(magic))
           && read_u32_le(file, &version)
           && read_u32_le(file, &node_count)
           && read_u32_le(file, &edge_count)
           && read_u32_le(file, &node_record_size)
           && read_u32_le(file, &edge_record_size)
           && read_u32_le(file, &features);

    if (!ok || memcmp(magic, OPENRIDE_GRAPH_MAGIC, 8U) != 0) {
        fclose(file);
        set_error(error, error_size, "invalid routing graph header");
        return false;
    }

    if ((version != OPENRIDE_GRAPH_LEGACY_VERSION
         && version != OPENRIDE_ROUTING_GRAPH_FORMAT_VERSION)
        || node_record_size != OPENRIDE_NODE_RECORD_SIZE
        || edge_record_size != OPENRIDE_EDGE_RECORD_SIZE) {
        fclose(file);
        set_error(error, error_size, "unsupported routing graph format version");
        return false;
    }

    OpenRideRoutingGraph loaded;
    memset(&loaded, 0, sizeof(loaded));
    loaded.node_count = node_count;
    loaded.edge_count = edge_count;

    if (node_count > 0U) {
        loaded.nodes = calloc(node_count, sizeof(*loaded.nodes));
        if (!loaded.nodes) ok = false;
    }
    if (ok && edge_count > 0U) {
        loaded.edges = calloc(edge_count, sizeof(*loaded.edges));
        if (!loaded.edges) ok = false;
    }

    for (uint32_t i = 0U; ok && i < node_count; ++i) {
        uint32_t lat_raw = 0U;
        uint32_t lon_raw = 0U;
        OpenRideRoutingNode *node = &loaded.nodes[i];
        ok = read_u32_le(file, &lat_raw)
          && read_u32_le(file, &lon_raw)
          && read_u32_le(file, &node->first_edge)
          && read_u32_le(file, &node->edge_count);
        node->lat_e7 = (int32_t)lat_raw;
        node->lon_e7 = (int32_t)lon_raw;
    }

    for (uint32_t i = 0U; ok && i < edge_count; ++i) {
        OpenRideRoutingEdge *edge = &loaded.edges[i];
        unsigned char class_and_surface[2];
        ok = read_u32_le(file, &edge->target)
          && read_u32_le(file, &edge->length_cm)
          && read_u32_le(file, &edge->flags)
          && read_exact(file, class_and_surface, sizeof(class_and_surface))
          && read_u16_le(file, &edge->max_speed_kph);
        edge->road_class = class_and_surface[0];
        edge->surface = class_and_surface[1];
    }

    if (ok && version == OPENRIDE_ROUTING_GRAPH_FORMAT_VERSION) {
        if ((features & OPENRIDE_GRAPH_FEATURE_SPATIAL_INDEX) == 0U) {
            ok = false;
            set_error(error, error_size, "v2 routing graph has no spatial index");
        } else {
            ok = read_spatial_index(file, &loaded, error, error_size);
        }
    }

    if (fclose(file) != 0) ok = false;

    if (!ok) {
        openride_routing_graph_destroy(&loaded);
        if (!error || !error[0]) {
            set_error(error, error_size, "routing graph file is truncated or unreadable");
        }
        return false;
    }

    /* v0.9 files remain readable. Their index is rebuilt once at load time. */
    if (version == OPENRIDE_GRAPH_LEGACY_VERSION) {
        if (!openride_routing_graph_build_spatial_index(&loaded, error, error_size)) {
            openride_routing_graph_destroy(&loaded);
            return false;
        }
    }

    if (!openride_routing_graph_validate(&loaded, error, error_size)) {
        openride_routing_graph_destroy(&loaded);
        return false;
    }

    openride_routing_graph_destroy(graph);
    *graph = loaded;
    set_error(error, error_size, "");
    return true;
}
