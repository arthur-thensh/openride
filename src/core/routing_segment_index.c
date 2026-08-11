#include "openride/routing_graph.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENRIDE_PI 3.14159265358979323846
#define OPENRIDE_EARTH_RADIUS_M 6371008.8

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

static double deg_to_rad(double degrees)
{
    return degrees * OPENRIDE_PI / 180.0;
}

static uint32_t clamp_grid_coord(int64_t value,
                                 int32_t minimum,
                                 uint32_t cell_size,
                                 uint32_t count)
{
    if (count == 0U) return 0U;
    const int64_t delta = value - (int64_t)minimum;
    if (delta <= 0) return 0U;
    uint64_t coord = (uint64_t)delta / cell_size;
    if (coord >= count) coord = count - 1U;
    return (uint32_t)coord;
}

static uint64_t segment_key(OpenRideRoutingNodeId a, OpenRideRoutingNodeId b)
{
    const uint32_t lo = a < b ? a : b;
    const uint32_t hi = a < b ? b : a;
    return ((uint64_t)lo << 32U) | (uint64_t)hi;
}

static int compare_u64(const void *a, const void *b)
{
    const uint64_t av = *(const uint64_t *)a;
    const uint64_t bv = *(const uint64_t *)b;
    return av < bv ? -1 : (av > bv ? 1 : 0);
}

void openride_routing_segment_index_destroy(OpenRideRoutingSegmentIndex *index)
{
    if (!index) return;
    free(index->segments);
    free(index->cell_offsets);
    free(index->segment_ids);
    memset(index, 0, sizeof(*index));
}

bool openride_routing_segment_index_validate(const OpenRideRoutingGraph *graph,
                                             char *error,
                                             size_t error_size)
{
    if (!graph) {
        set_error(error, error_size, "routing graph is null");
        return false;
    }

    const OpenRideRoutingSegmentIndex *index = &graph->segment_index;
    if (graph->edge_count == 0U) {
        if (index->segment_count != 0U || index->ref_count != 0U
            || index->segments || index->cell_offsets || index->segment_ids) {
            set_error(error, error_size, "edgeless graph has a non-empty segment index");
            return false;
        }
        set_error(error, error_size, "");
        return true;
    }

    if (!openride_routing_graph_has_spatial_index(graph)
        || (index->segment_count > 0U && !index->segments)
        || !index->cell_offsets
        || (index->ref_count > 0U && !index->segment_ids)) {
        set_error(error, error_size, "routing graph has no segment index");
        return false;
    }

    const uint32_t cell_count = graph->spatial_index.cell_count;
    if (index->cell_offsets[0] != 0U
        || index->cell_offsets[cell_count] != index->ref_count) {
        set_error(error, error_size, "segment index offset range is invalid");
        return false;
    }

    for (uint32_t i = 0U; i < cell_count; ++i) {
        if (index->cell_offsets[i] > index->cell_offsets[i + 1U]
            || index->cell_offsets[i + 1U] > index->ref_count) {
            set_error(error, error_size, "segment index offsets are not monotonic");
            return false;
        }
    }

    for (uint32_t i = 0U; i < index->segment_count; ++i) {
        const OpenRideRoutingSegment *segment = &index->segments[i];
        if (segment->a >= graph->node_count
            || segment->b >= graph->node_count
            || segment->a == segment->b) {
            set_error(error, error_size, "segment index contains an invalid segment");
            return false;
        }
    }

    for (uint32_t i = 0U; i < index->ref_count; ++i) {
        if (index->segment_ids[i] >= index->segment_count) {
            set_error(error, error_size, "segment index reference is out of bounds");
            return false;
        }
    }

    set_error(error, error_size, "");
    return true;
}

static bool build_unique_segments(const OpenRideRoutingGraph *graph,
                                  OpenRideRoutingSegmentIndex *index,
                                  char *error,
                                  size_t error_size)
{
    if (graph->edge_count == 0U) return true;

    uint64_t *keys = malloc((size_t)graph->edge_count * sizeof(*keys));
    if (!keys) {
        set_error(error, error_size, "unable to allocate segment keys");
        return false;
    }

    uint32_t key_count = 0U;
    for (uint32_t source = 0U; source < graph->node_count; ++source) {
        const OpenRideRoutingNode *node = &graph->nodes[source];
        for (uint32_t j = 0U; j < node->edge_count; ++j) {
            const OpenRideRoutingEdge *edge = &graph->edges[node->first_edge + j];
            if (edge->target == source) continue;
            keys[key_count++] = segment_key(source, edge->target);
        }
    }

    qsort(keys, key_count, sizeof(*keys), compare_u64);
    uint32_t unique_count = 0U;
    for (uint32_t i = 0U; i < key_count; ++i) {
        if (i == 0U || keys[i] != keys[i - 1U]) ++unique_count;
    }

    index->segments = calloc(unique_count, sizeof(*index->segments));
    if (unique_count > 0U && !index->segments) {
        free(keys);
        set_error(error, error_size, "unable to allocate routing segments");
        return false;
    }

    uint32_t out = 0U;
    for (uint32_t i = 0U; i < key_count; ++i) {
        if (i > 0U && keys[i] == keys[i - 1U]) continue;
        index->segments[out].a = (uint32_t)(keys[i] >> 32U);
        index->segments[out].b = (uint32_t)keys[i];
        ++out;
    }
    index->segment_count = unique_count;
    free(keys);
    return true;
}

typedef bool (*CellVisitor)(uint32_t cell, void *context);

static bool visit_segment_cells(const OpenRideRoutingGraph *graph,
                                const OpenRideRoutingSegment *segment,
                                CellVisitor visitor,
                                void *context)
{
    const OpenRideRoutingSpatialIndex *grid = &graph->spatial_index;
    const OpenRideRoutingNode *a = &graph->nodes[segment->a];
    const OpenRideRoutingNode *b = &graph->nodes[segment->b];

    int x0 = (int)clamp_grid_coord(a->lon_e7,
                                   grid->min_lon_e7,
                                   grid->cell_size_e7,
                                   grid->columns);
    int y0 = (int)clamp_grid_coord(a->lat_e7,
                                   grid->min_lat_e7,
                                   grid->cell_size_e7,
                                   grid->rows);
    const int x1 = (int)clamp_grid_coord(b->lon_e7,
                                         grid->min_lon_e7,
                                         grid->cell_size_e7,
                                         grid->columns);
    const int y1 = (int)clamp_grid_coord(b->lat_e7,
                                         grid->min_lat_e7,
                                         grid->cell_size_e7,
                                         grid->rows);

    const int dx = abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        const uint32_t cell = (uint32_t)y0 * grid->columns + (uint32_t)x0;
        if (!visitor(cell, context)) return false;
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
    return true;
}

typedef struct CountContext {
    uint32_t *cell_offsets;
    uint64_t total;
    bool overflow;
} CountContext;

static bool count_cell(uint32_t cell, void *opaque)
{
    CountContext *context = opaque;
    if (context->cell_offsets[cell + 1U] == UINT32_MAX) {
        context->overflow = true;
        return false;
    }
    ++context->cell_offsets[cell + 1U];
    ++context->total;
    if (context->total > UINT32_MAX) {
        context->overflow = true;
        return false;
    }
    return true;
}

typedef struct FillContext {
    uint32_t *cursor;
    uint32_t *segment_ids;
    uint32_t segment_id;
} FillContext;

static bool fill_cell(uint32_t cell, void *opaque)
{
    FillContext *context = opaque;
    context->segment_ids[context->cursor[cell]++] = context->segment_id;
    return true;
}

bool openride_routing_graph_build_segment_index(OpenRideRoutingGraph *graph,
                                                char *error,
                                                size_t error_size)
{
    if (!graph || (graph->node_count > 0U && !graph->nodes)
        || (graph->edge_count > 0U && !graph->edges)) {
        set_error(error, error_size, "invalid graph for segment index");
        return false;
    }
    if (!openride_routing_graph_has_spatial_index(graph)) {
        set_error(error, error_size, "segment index requires the node spatial index");
        return false;
    }

    openride_routing_segment_index_destroy(&graph->segment_index);
    if (graph->edge_count == 0U) {
        set_error(error, error_size, "");
        return true;
    }

    OpenRideRoutingSegmentIndex index;
    memset(&index, 0, sizeof(index));
    if (!build_unique_segments(graph, &index, error, error_size)) {
        openride_routing_segment_index_destroy(&index);
        return false;
    }

    const uint32_t cell_count = graph->spatial_index.cell_count;
    index.cell_offsets = calloc((size_t)cell_count + 1U,
                                sizeof(*index.cell_offsets));
    if (!index.cell_offsets) {
        openride_routing_segment_index_destroy(&index);
        set_error(error, error_size, "unable to allocate segment cell offsets");
        return false;
    }

    CountContext count = {index.cell_offsets, 0U, false};
    for (uint32_t i = 0U; i < index.segment_count; ++i) {
        if (!visit_segment_cells(graph, &index.segments[i], count_cell, &count)) {
            break;
        }
    }
    if (count.overflow) {
        openride_routing_segment_index_destroy(&index);
        set_error(error, error_size, "segment spatial index is too large");
        return false;
    }

    for (uint32_t i = 1U; i <= cell_count; ++i) {
        index.cell_offsets[i] += index.cell_offsets[i - 1U];
    }
    index.ref_count = index.cell_offsets[cell_count];

    if (index.ref_count > 0U) {
        index.segment_ids = malloc((size_t)index.ref_count * sizeof(*index.segment_ids));
        if (!index.segment_ids) {
            openride_routing_segment_index_destroy(&index);
            set_error(error, error_size, "unable to allocate segment references");
            return false;
        }
    }

    uint32_t *cursor = malloc((size_t)cell_count * sizeof(*cursor));
    if (!cursor) {
        openride_routing_segment_index_destroy(&index);
        set_error(error, error_size, "unable to allocate segment index cursor");
        return false;
    }
    memcpy(cursor, index.cell_offsets, (size_t)cell_count * sizeof(*cursor));

    for (uint32_t i = 0U; i < index.segment_count; ++i) {
        FillContext fill = {cursor, index.segment_ids, i};
        (void)visit_segment_cells(graph, &index.segments[i], fill_cell, &fill);
    }
    free(cursor);

    graph->segment_index = index;
    if (!openride_routing_segment_index_validate(graph, error, error_size)) {
        openride_routing_segment_index_destroy(&graph->segment_index);
        return false;
    }

    set_error(error, error_size, "");
    return true;
}

bool openride_routing_graph_has_segment_index(const OpenRideRoutingGraph *graph)
{
    if (!graph) return false;
    if (graph->edge_count == 0U) return true;
    return graph->segment_index.cell_offsets
        && (graph->segment_index.segment_count == 0U
            || graph->segment_index.segments);
}

static void project_to_segment(const OpenRideRoutingGraph *graph,
                               const OpenRideRoutingSegment *segment,
                               double lat,
                               double lon,
                               OpenRideRoutingSnap *snap,
                               uint32_t segment_id)
{
    double a_lat = 0.0;
    double a_lon = 0.0;
    double b_lat = 0.0;
    double b_lon = 0.0;
    openride_routing_node_geo(&graph->nodes[segment->a], &a_lat, &a_lon);
    openride_routing_node_geo(&graph->nodes[segment->b], &b_lat, &b_lon);

    const double lat0 = deg_to_rad(lat);
    const double meters_per_deg_lat = OPENRIDE_EARTH_RADIUS_M * OPENRIDE_PI / 180.0;
    double meters_per_deg_lon = meters_per_deg_lat * cos(lat0);
    if (fabs(meters_per_deg_lon) < 1e-9) meters_per_deg_lon = 1e-9;

    const double ax = (a_lon - lon) * meters_per_deg_lon;
    const double ay = (a_lat - lat) * meters_per_deg_lat;
    const double bx = (b_lon - lon) * meters_per_deg_lon;
    const double by = (b_lat - lat) * meters_per_deg_lat;
    const double vx = bx - ax;
    const double vy = by - ay;
    const double denom = vx * vx + vy * vy;

    double t = 0.0;
    if (denom > 1e-12) {
        t = -(ax * vx + ay * vy) / denom;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
    }

    const double px = ax + t * vx;
    const double py = ay + t * vy;
    const double distance = sqrt(px * px + py * py);

    snap->segment_id = segment_id;
    snap->a = segment->a;
    snap->b = segment->b;
    snap->fraction = t;
    snap->lat = a_lat + (b_lat - a_lat) * t;
    snap->lon = a_lon + (b_lon - a_lon) * t;
    snap->distance_m = distance;
}

bool openride_routing_graph_snap_to_segment_linear(const OpenRideRoutingGraph *graph,
                                                   double lat,
                                                   double lon,
                                                   double max_distance_m,
                                                   OpenRideRoutingSnap *snap)
{
    if (snap) memset(snap, 0, sizeof(*snap));
    if (!graph || !snap || !valid_geo(lat, lon)
        || max_distance_m < 0.0 || !isfinite(max_distance_m)
        || !openride_routing_graph_has_segment_index(graph)) {
        if (snap) snap->segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
        return false;
    }

    OpenRideRoutingSnap best;
    memset(&best, 0, sizeof(best));
    best.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    best.distance_m = INFINITY;

    for (uint32_t i = 0U; i < graph->segment_index.segment_count; ++i) {
        OpenRideRoutingSnap candidate;
        project_to_segment(graph, &graph->segment_index.segments[i], lat, lon, &candidate, i);
        if (candidate.distance_m < best.distance_m) best = candidate;
    }

    if (best.segment_id == OPENRIDE_ROUTING_SEGMENT_NONE
        || best.distance_m > max_distance_m) {
        snap->segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
        return false;
    }
    *snap = best;
    return true;
}

bool openride_routing_graph_snap_to_segment(const OpenRideRoutingGraph *graph,
                                            double lat,
                                            double lon,
                                            double max_distance_m,
                                            OpenRideRoutingSnap *snap)
{
    if (snap) memset(snap, 0, sizeof(*snap));
    if (!graph || !snap || !valid_geo(lat, lon)
        || max_distance_m < 0.0 || !isfinite(max_distance_m)
        || !openride_routing_graph_has_segment_index(graph)) {
        if (snap) snap->segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
        return false;
    }

    const OpenRideRoutingSpatialIndex *grid = &graph->spatial_index;
    const OpenRideRoutingSegmentIndex *index = &graph->segment_index;
    const double cell_deg = (double)grid->cell_size_e7 / 10000000.0;
    const double meters_per_deg_lat = OPENRIDE_EARTH_RADIUS_M * OPENRIDE_PI / 180.0;
    double meters_per_deg_lon = meters_per_deg_lat * fabs(cos(deg_to_rad(lat)));
    if (meters_per_deg_lon < 1.0) meters_per_deg_lon = 1.0;

    const uint32_t center_row = clamp_grid_coord(
        (int64_t)llround(lat * 10000000.0),
        grid->min_lat_e7,
        grid->cell_size_e7,
        grid->rows);
    const uint32_t center_col = clamp_grid_coord(
        (int64_t)llround(lon * 10000000.0),
        grid->min_lon_e7,
        grid->cell_size_e7,
        grid->columns);

    const uint32_t row_radius = (uint32_t)ceil(max_distance_m / (cell_deg * meters_per_deg_lat)) + 1U;
    const uint32_t col_radius = (uint32_t)ceil(max_distance_m / (cell_deg * meters_per_deg_lon)) + 1U;

    const uint32_t row_min = center_row > row_radius ? center_row - row_radius : 0U;
    const uint32_t col_min = center_col > col_radius ? center_col - col_radius : 0U;
    const uint32_t row_max = center_row + row_radius < grid->rows
        ? center_row + row_radius : grid->rows - 1U;
    const uint32_t col_max = center_col + col_radius < grid->columns
        ? center_col + col_radius : grid->columns - 1U;

    OpenRideRoutingSnap best;
    memset(&best, 0, sizeof(best));
    best.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    best.distance_m = INFINITY;

    for (uint32_t row = row_min; row <= row_max; ++row) {
        for (uint32_t col = col_min; col <= col_max; ++col) {
            const uint32_t cell = row * grid->columns + col;
            const uint32_t begin = index->cell_offsets[cell];
            const uint32_t end = index->cell_offsets[cell + 1U];
            for (uint32_t ref = begin; ref < end; ++ref) {
                const uint32_t segment_id = index->segment_ids[ref];
                OpenRideRoutingSnap candidate;
                project_to_segment(graph,
                                   &index->segments[segment_id],
                                   lat,
                                   lon,
                                   &candidate,
                                   segment_id);
                if (candidate.distance_m < best.distance_m) best = candidate;
            }
        }
    }

    if (best.segment_id == OPENRIDE_ROUTING_SEGMENT_NONE
        || best.distance_m > max_distance_m) {
        snap->segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
        return false;
    }
    *snap = best;
    return true;
}
