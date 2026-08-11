#include "openride/routing_graph.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENRIDE_PI 3.14159265358979323846
#define OPENRIDE_EARTH_RADIUS_M 6371008.8
#define OPENRIDE_SPATIAL_DEFAULT_CELL_E7 100000U /* 0.01 degree */
#define OPENRIDE_SPATIAL_MAX_CELLS 2000000U

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

static double distance_to_latitude_m(double lat, double boundary_lat)
{
    return fabs(deg_to_rad(lat - boundary_lat)) * OPENRIDE_EARTH_RADIUS_M;
}

/*
 * Conservative lower bound to any point beyond a longitude boundary.
 * A meridian is a great circle, so this is the cross-track distance to it.
 */
static double distance_to_meridian_m(double lat, double lon, double boundary_lon)
{
    double delta = deg_to_rad(lon - boundary_lon);
    while (delta > OPENRIDE_PI) delta -= 2.0 * OPENRIDE_PI;
    while (delta < -OPENRIDE_PI) delta += 2.0 * OPENRIDE_PI;

    double value = fabs(cos(deg_to_rad(lat)) * sin(delta));
    if (value > 1.0) value = 1.0;
    return asin(value) * OPENRIDE_EARTH_RADIUS_M;
}

void openride_routing_spatial_index_destroy(OpenRideRoutingSpatialIndex *index)
{
    if (!index) return;
    free(index->cell_offsets);
    free(index->node_ids);
    memset(index, 0, sizeof(*index));
}

bool openride_routing_spatial_index_validate(
    const OpenRideRoutingGraph *graph,
    char *error,
    size_t error_size)
{
    if (!graph) {
        set_error(error, error_size, "routing graph is null");
        return false;
    }

    const OpenRideRoutingSpatialIndex *index = &graph->spatial_index;
    if (graph->node_count == 0U) {
        if (index->cell_count != 0U || index->cell_offsets || index->node_ids) {
            set_error(error, error_size, "empty graph has a non-empty spatial index");
            return false;
        }
        set_error(error, error_size, "");
        return true;
    }

    if (index->cell_size_e7 == 0U
        || index->rows == 0U
        || index->columns == 0U
        || index->cell_count == 0U
        || !index->cell_offsets
        || !index->node_ids) {
        set_error(error, error_size, "routing graph has no spatial index");
        return false;
    }

    if ((uint64_t)index->rows * (uint64_t)index->columns != index->cell_count) {
        set_error(error, error_size, "spatial index dimensions are inconsistent");
        return false;
    }

    if (index->cell_offsets[0] != 0U
        || index->cell_offsets[index->cell_count] != graph->node_count) {
        set_error(error, error_size, "spatial index offset range is invalid");
        return false;
    }

    for (uint32_t i = 0U; i < index->cell_count; ++i) {
        if (index->cell_offsets[i] > index->cell_offsets[i + 1U]
            || index->cell_offsets[i + 1U] > graph->node_count) {
            set_error(error, error_size, "spatial index offsets are not monotonic");
            return false;
        }
    }

    for (uint32_t i = 0U; i < graph->node_count; ++i) {
        if (index->node_ids[i] >= graph->node_count) {
            set_error(error, error_size, "spatial index node id is out of bounds");
            return false;
        }
    }

    set_error(error, error_size, "");
    return true;
}

static bool grid_dimensions(int32_t min_lat_e7,
                            int32_t max_lat_e7,
                            int32_t min_lon_e7,
                            int32_t max_lon_e7,
                            uint32_t *cell_size_e7,
                            uint32_t *rows,
                            uint32_t *columns,
                            uint32_t *cell_count)
{
    uint64_t cell_size = OPENRIDE_SPATIAL_DEFAULT_CELL_E7;
    const int64_t lat_span = (int64_t)max_lat_e7 - (int64_t)min_lat_e7;
    const int64_t lon_span = (int64_t)max_lon_e7 - (int64_t)min_lon_e7;

    for (;;) {
        const uint64_t r = (uint64_t)(lat_span / (int64_t)cell_size) + 1U;
        const uint64_t c = (uint64_t)(lon_span / (int64_t)cell_size) + 1U;
        const uint64_t count = r * c;

        if (r <= UINT32_MAX && c <= UINT32_MAX
            && count <= OPENRIDE_SPATIAL_MAX_CELLS) {
            *cell_size_e7 = (uint32_t)cell_size;
            *rows = (uint32_t)r;
            *columns = (uint32_t)c;
            *cell_count = (uint32_t)count;
            return true;
        }

        cell_size *= 2U;
        if (cell_size > UINT32_MAX) return false;
    }
}

static uint32_t cell_for_node(const OpenRideRoutingSpatialIndex *index,
                              const OpenRideRoutingNode *node)
{
    const uint64_t row = (uint64_t)((int64_t)node->lat_e7 - index->min_lat_e7)
                       / index->cell_size_e7;
    const uint64_t column = (uint64_t)((int64_t)node->lon_e7 - index->min_lon_e7)
                          / index->cell_size_e7;
    return (uint32_t)(row * index->columns + column);
}

bool openride_routing_graph_build_spatial_index(OpenRideRoutingGraph *graph,
                                                char *error,
                                                size_t error_size)
{
    if (!graph || (graph->node_count > 0U && !graph->nodes)) {
        set_error(error, error_size, "invalid graph for spatial index");
        return false;
    }

    openride_routing_spatial_index_destroy(&graph->spatial_index);
    if (graph->node_count == 0U) {
        set_error(error, error_size, "");
        return true;
    }

    int32_t min_lat = graph->nodes[0].lat_e7;
    int32_t max_lat = graph->nodes[0].lat_e7;
    int32_t min_lon = graph->nodes[0].lon_e7;
    int32_t max_lon = graph->nodes[0].lon_e7;

    for (uint32_t i = 1U; i < graph->node_count; ++i) {
        const OpenRideRoutingNode *node = &graph->nodes[i];
        if (node->lat_e7 < min_lat) min_lat = node->lat_e7;
        if (node->lat_e7 > max_lat) max_lat = node->lat_e7;
        if (node->lon_e7 < min_lon) min_lon = node->lon_e7;
        if (node->lon_e7 > max_lon) max_lon = node->lon_e7;
    }

    OpenRideRoutingSpatialIndex index;
    memset(&index, 0, sizeof(index));
    index.min_lat_e7 = min_lat;
    index.min_lon_e7 = min_lon;

    if (!grid_dimensions(min_lat,
                         max_lat,
                         min_lon,
                         max_lon,
                         &index.cell_size_e7,
                         &index.rows,
                         &index.columns,
                         &index.cell_count)) {
        set_error(error, error_size, "unable to choose spatial index grid size");
        return false;
    }

    index.cell_offsets = calloc((size_t)index.cell_count + 1U,
                                sizeof(*index.cell_offsets));
    index.node_ids = malloc((size_t)graph->node_count * sizeof(*index.node_ids));
    if (!index.cell_offsets || !index.node_ids) {
        openride_routing_spatial_index_destroy(&index);
        set_error(error, error_size, "unable to allocate spatial index");
        return false;
    }

    for (uint32_t i = 0U; i < graph->node_count; ++i) {
        const uint32_t cell = cell_for_node(&index, &graph->nodes[i]);
        if (cell >= index.cell_count) {
            openride_routing_spatial_index_destroy(&index);
            set_error(error, error_size, "node falls outside spatial index grid");
            return false;
        }
        ++index.cell_offsets[cell + 1U];
    }

    for (uint32_t i = 1U; i <= index.cell_count; ++i) {
        index.cell_offsets[i] += index.cell_offsets[i - 1U];
    }

    uint32_t *cursor = malloc((size_t)index.cell_count * sizeof(*cursor));
    if (!cursor) {
        openride_routing_spatial_index_destroy(&index);
        set_error(error, error_size, "unable to allocate spatial index cursor");
        return false;
    }
    memcpy(cursor,
           index.cell_offsets,
           (size_t)index.cell_count * sizeof(*cursor));

    for (uint32_t i = 0U; i < graph->node_count; ++i) {
        const uint32_t cell = cell_for_node(&index, &graph->nodes[i]);
        index.node_ids[cursor[cell]++] = i;
    }
    free(cursor);

    graph->spatial_index = index;
    if (!openride_routing_spatial_index_validate(graph, error, error_size)) {
        openride_routing_spatial_index_destroy(&graph->spatial_index);
        return false;
    }

    set_error(error, error_size, "");
    return true;
}

bool openride_routing_graph_has_spatial_index(const OpenRideRoutingGraph *graph)
{
    if (!graph) return false;
    if (graph->node_count == 0U) return true;
    return graph->spatial_index.cell_count > 0U
        && graph->spatial_index.cell_offsets
        && graph->spatial_index.node_ids;
}

static uint32_t clamp_cell_coordinate(int64_t coordinate,
                                      int32_t minimum,
                                      uint32_t cell_size,
                                      uint32_t count)
{
    int64_t delta = coordinate - (int64_t)minimum;
    if (delta <= 0) return 0U;

    uint64_t value = (uint64_t)delta / cell_size;
    if (value >= count) value = count - 1U;
    return (uint32_t)value;
}

static void scan_cell(const OpenRideRoutingGraph *graph,
                      uint32_t row,
                      uint32_t column,
                      double lat,
                      double lon,
                      OpenRideRoutingNodeId *best_id,
                      double *best_distance)
{
    const OpenRideRoutingSpatialIndex *index = &graph->spatial_index;
    const uint32_t cell = row * index->columns + column;
    const uint32_t begin = index->cell_offsets[cell];
    const uint32_t end = index->cell_offsets[cell + 1U];

    for (uint32_t i = begin; i < end; ++i) {
        const OpenRideRoutingNodeId node_id = index->node_ids[i];
        const OpenRideRoutingNode *node = &graph->nodes[node_id];
        const double distance = geo_distance_m(lat,
                                               lon,
                                               e7_to_degree(node->lat_e7),
                                               e7_to_degree(node->lon_e7));
        if (distance < *best_distance) {
            *best_distance = distance;
            *best_id = node_id;
        }
    }
}

static double unsearched_lower_bound_m(const OpenRideRoutingSpatialIndex *index,
                                       double lat,
                                       double lon,
                                       uint32_t row_min,
                                       uint32_t row_max,
                                       uint32_t col_min,
                                       uint32_t col_max)
{
    double lower = INFINITY;
    const double cell_deg = (double)index->cell_size_e7 / 10000000.0;
    const double min_lat = e7_to_degree(index->min_lat_e7);
    const double min_lon = e7_to_degree(index->min_lon_e7);

    if (row_min > 0U) {
        const double south = min_lat + (double)row_min * cell_deg;
        const double d = distance_to_latitude_m(lat, south);
        if (d < lower) lower = d;
    }
    if (row_max + 1U < index->rows) {
        const double north = min_lat + (double)(row_max + 1U) * cell_deg;
        const double d = distance_to_latitude_m(lat, north);
        if (d < lower) lower = d;
    }
    if (col_min > 0U) {
        const double west = min_lon + (double)col_min * cell_deg;
        const double d = distance_to_meridian_m(lat, lon, west);
        if (d < lower) lower = d;
    }
    if (col_max + 1U < index->columns) {
        const double east = min_lon + (double)(col_max + 1U) * cell_deg;
        const double d = distance_to_meridian_m(lat, lon, east);
        if (d < lower) lower = d;
    }

    return lower;
}

OpenRideRoutingNodeId openride_routing_spatial_index_nearest_node(
    const OpenRideRoutingGraph *graph,
    double lat,
    double lon,
    double *distance_m)
{
    if (distance_m) *distance_m = INFINITY;
    if (!graph || graph->node_count == 0U || !valid_geo(lat, lon)
        || !openride_routing_graph_has_spatial_index(graph)) {
        return OPENRIDE_ROUTING_NODE_NONE;
    }

    const OpenRideRoutingSpatialIndex *index = &graph->spatial_index;
    const int32_t lat_e7 = degree_to_e7(lat);
    const int32_t lon_e7 = degree_to_e7(lon);
    const uint32_t center_row = clamp_cell_coordinate(lat_e7,
                                                      index->min_lat_e7,
                                                      index->cell_size_e7,
                                                      index->rows);
    const uint32_t center_col = clamp_cell_coordinate(lon_e7,
                                                      index->min_lon_e7,
                                                      index->cell_size_e7,
                                                      index->columns);
    const uint32_t max_ring = index->rows > index->columns
        ? index->rows
        : index->columns;

    OpenRideRoutingNodeId best_id = OPENRIDE_ROUTING_NODE_NONE;
    double best_distance = INFINITY;

    for (uint32_t ring = 0U; ring < max_ring; ++ring) {
        const uint32_t row_min = center_row > ring ? center_row - ring : 0U;
        const uint32_t col_min = center_col > ring ? center_col - ring : 0U;
        const uint32_t row_max = center_row + ring < index->rows
            ? center_row + ring
            : index->rows - 1U;
        const uint32_t col_max = center_col + ring < index->columns
            ? center_col + ring
            : index->columns - 1U;

        if (ring == 0U) {
            scan_cell(graph,
                      center_row,
                      center_col,
                      lat,
                      lon,
                      &best_id,
                      &best_distance);
        } else {
            for (uint32_t col = col_min; col <= col_max; ++col) {
                scan_cell(graph,
                          row_min,
                          col,
                          lat,
                          lon,
                          &best_id,
                          &best_distance);
                if (row_max != row_min) {
                    scan_cell(graph,
                              row_max,
                              col,
                              lat,
                              lon,
                              &best_id,
                              &best_distance);
                }
            }

            if (row_max > row_min + 1U) {
                for (uint32_t row = row_min + 1U; row < row_max; ++row) {
                    scan_cell(graph,
                              row,
                              col_min,
                              lat,
                              lon,
                              &best_id,
                              &best_distance);
                    if (col_max != col_min) {
                        scan_cell(graph,
                                  row,
                                  col_max,
                                  lat,
                                  lon,
                                  &best_id,
                                  &best_distance);
                    }
                }
            }
        }

        if (best_id != OPENRIDE_ROUTING_NODE_NONE) {
            const double lower = unsearched_lower_bound_m(index,
                                                          lat,
                                                          lon,
                                                          row_min,
                                                          row_max,
                                                          col_min,
                                                          col_max);
            if (!isfinite(lower) || best_distance <= lower) break;
        }
    }

    if (distance_m) *distance_m = best_distance;
    return best_id;
}
