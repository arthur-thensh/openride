#include "openride/ormap_landcover_mesh.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct MeshPoint {
    double x;
    double y;
} MeshPoint;

typedef struct BoundaryEdge {
    uint16_t x0;
    uint16_t y0;
    uint16_t x1;
    uint16_t y1;
    int32_t next;
    unsigned char used;
} BoundaryEdge;

typedef struct ArcPair {
    uint32_t start;
    uint32_t end;
} ArcPair;

static bool mesh_bit_get(const unsigned char *bits, uint32_t index)
{
    return bits
        && (bits[index >> 3U] & (unsigned char)(1U << (index & 7U))) != 0U;
}

static bool mesh_cell_get(const unsigned char *bits,
                          uint32_t grid,
                          int x,
                          int y)
{
    if (!bits || x < 0 || y < 0 || x >= (int)grid || y >= (int)grid) {
        return false;
    }
    return mesh_bit_get(bits, (uint32_t)y * grid + (uint32_t)x);
}

static bool mesh_reserve_triangles(OpenRideORMapLandcoverMesh *mesh,
                                   uint32_t needed)
{
    if (needed <= mesh->triangle_capacity) return true;
    uint32_t capacity = mesh->triangle_capacity ? mesh->triangle_capacity : 64U;
    while (capacity < needed) {
        if (capacity > UINT32_MAX / 2U) {
            capacity = needed;
            break;
        }
        capacity *= 2U;
    }
    OpenRideORMapLandcoverTriangle *triangles =
        realloc(mesh->triangles, (size_t)capacity * sizeof(*triangles));
    if (!triangles) return false;
    mesh->triangles = triangles;
    mesh->triangle_capacity = capacity;
    return true;
}

static bool mesh_append_triangle(OpenRideORMapLandcoverMesh *mesh,
                                 MeshPoint a,
                                 MeshPoint b,
                                 MeshPoint c,
                                 double inv_grid)
{
    if (!mesh_reserve_triangles(mesh, mesh->triangle_count + 1U)) return false;
    OpenRideORMapLandcoverTriangle *triangle =
        &mesh->triangles[mesh->triangle_count++];
    triangle->x0 = a.x * inv_grid;
    triangle->y0 = a.y * inv_grid;
    triangle->x1 = b.x * inv_grid;
    triangle->y1 = b.y * inv_grid;
    triangle->x2 = c.x * inv_grid;
    triangle->y2 = c.y * inv_grid;
    return true;
}

static bool mesh_add_edge(BoundaryEdge *edges,
                          uint32_t edge_capacity,
                          uint32_t *edge_count,
                          int32_t *heads,
                          uint32_t vertex_stride,
                          uint16_t x0,
                          uint16_t y0,
                          uint16_t x1,
                          uint16_t y1)
{
    if (*edge_count >= edge_capacity) return false;
    const uint32_t index = (*edge_count)++;
    BoundaryEdge *edge = &edges[index];
    edge->x0 = x0;
    edge->y0 = y0;
    edge->x1 = x1;
    edge->y1 = y1;
    edge->used = 0U;
    const uint32_t vertex = (uint32_t)y0 * vertex_stride + (uint32_t)x0;
    edge->next = heads[vertex];
    heads[vertex] = (int32_t)index;
    return true;
}

static int mesh_edge_direction(const BoundaryEdge *edge)
{
    if (edge->x1 > edge->x0) return 0; /* east */
    if (edge->y1 > edge->y0) return 1; /* south */
    if (edge->x1 < edge->x0) return 2; /* west */
    return 3;                           /* north */
}

static int mesh_turn_priority(int previous_direction, int direction)
{
    const int turn = (direction - previous_direction + 4) & 3;
    if (turn == 1) return 0; /* right: keep filled raster on our right */
    if (turn == 0) return 1; /* straight */
    if (turn == 3) return 2; /* left */
    return 3;                /* reverse, only as a last resort */
}

static int32_t mesh_choose_next(const BoundaryEdge *edges,
                                const int32_t *heads,
                                uint32_t vertex_stride,
                                uint16_t x,
                                uint16_t y,
                                int previous_direction)
{
    const uint32_t vertex = (uint32_t)y * vertex_stride + (uint32_t)x;
    int32_t best = -1;
    int best_priority = 99;
    for (int32_t index = heads[vertex]; index >= 0; index = edges[index].next) {
        if (edges[index].used) continue;
        const int priority = mesh_turn_priority(previous_direction,
                                                mesh_edge_direction(&edges[index]));
        if (priority < best_priority) {
            best = index;
            best_priority = priority;
        }
    }
    return best;
}

static double mesh_distance_sq(MeshPoint a, MeshPoint b)
{
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

static double mesh_segment_distance_sq(MeshPoint point,
                                       MeshPoint a,
                                       MeshPoint b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length_sq = dx * dx + dy * dy;
    if (length_sq <= 1e-12) return mesh_distance_sq(point, a);
    const double cross = dx * (a.y - point.y) - (a.x - point.x) * dy;
    return (cross * cross) / length_sq;
}

static uint32_t mesh_farthest_point(const MeshPoint *points,
                                    uint32_t count,
                                    uint32_t from)
{
    uint32_t best = from;
    double best_distance = -1.0;
    for (uint32_t i = 0U; i < count; ++i) {
        const double distance = mesh_distance_sq(points[from], points[i]);
        if (distance > best_distance) {
            best = i;
            best_distance = distance;
        }
    }
    return best;
}

static bool mesh_mark_dp_arc(const MeshPoint *points,
                             uint32_t count,
                             uint32_t start,
                             uint32_t end,
                             double tolerance_sq,
                             unsigned char *keep,
                             ArcPair *stack)
{
    uint32_t stack_count = 0U;
    stack[stack_count++] = (ArcPair){start, end};
    while (stack_count > 0U) {
        const ArcPair arc = stack[--stack_count];
        uint32_t best = arc.start;
        double best_distance = tolerance_sq;
        for (uint32_t i = (arc.start + 1U) % count;
             i != arc.end;
             i = (i + 1U) % count) {
            const double distance = mesh_segment_distance_sq(points[i],
                                                             points[arc.start],
                                                             points[arc.end]);
            if (distance > best_distance) {
                best = i;
                best_distance = distance;
            }
        }
        if (best == arc.start) continue;
        keep[best] = 1U;
        if (stack_count + 2U > count) return false;
        stack[stack_count++] = (ArcPair){arc.start, best};
        stack[stack_count++] = (ArcPair){best, arc.end};
    }
    return true;
}

static uint32_t mesh_simplify_closed(MeshPoint *points,
                                     uint32_t count,
                                     double tolerance)
{
    if (count <= 4U || tolerance <= 0.0) return count;

    uint32_t a = mesh_farthest_point(points, count, 0U);
    uint32_t b = mesh_farthest_point(points, count, a);
    a = mesh_farthest_point(points, count, b);
    if (a == b) return count;

    unsigned char *keep = calloc(count, sizeof(*keep));
    ArcPair *stack = malloc((size_t)count * sizeof(*stack));
    if (!keep || !stack) {
        free(keep);
        free(stack);
        return count;
    }
    keep[a] = 1U;
    keep[b] = 1U;
    const double tolerance_sq = tolerance * tolerance;
    if (!mesh_mark_dp_arc(points, count, a, b, tolerance_sq, keep, stack)
        || !mesh_mark_dp_arc(points, count, b, a, tolerance_sq, keep, stack)) {
        free(keep);
        free(stack);
        return count;
    }

    uint32_t output_count = 0U;
    for (uint32_t i = 0U; i < count; ++i) {
        if (keep[i]) points[output_count++] = points[i];
    }
    free(keep);
    free(stack);
    return output_count >= 3U ? output_count : count;
}

static double mesh_cross(MeshPoint a, MeshPoint b, MeshPoint c)
{
    return (b.x - a.x) * (c.y - a.y)
        - (b.y - a.y) * (c.x - a.x);
}

static double mesh_signed_area(const MeshPoint *points, uint32_t count)
{
    double area = 0.0;
    for (uint32_t i = 0U; i < count; ++i) {
        const uint32_t j = (i + 1U) % count;
        area += points[i].x * points[j].y - points[j].x * points[i].y;
    }
    return area * 0.5;
}

static bool mesh_point_in_triangle(MeshPoint p,
                                   MeshPoint a,
                                   MeshPoint b,
                                   MeshPoint c,
                                   double orientation)
{
    const double e0 = mesh_cross(a, b, p) * orientation;
    const double e1 = mesh_cross(b, c, p) * orientation;
    const double e2 = mesh_cross(c, a, p) * orientation;
    const double epsilon = 1e-9;
    return e0 > epsilon && e1 > epsilon && e2 > epsilon;
}

static bool mesh_triangulate_loop(const MeshPoint *points,
                                  uint32_t count,
                                  double inv_grid,
                                  OpenRideORMapLandcoverMesh *mesh)
{
    if (count < 3U) return true;
    const double area = mesh_signed_area(points, count);
    if (fabs(area) <= 1e-9) return true;

    /* Boundary edges are emitted with filled cells on the right. In screen
     * coordinates (y grows downward), outer rings therefore have positive
     * area and hole rings negative area. Overview landcover intentionally
     * closes holes; skipping negative rings avoids double-alpha darkening. */
    if (area < 0.0) return true;
    const double orientation = 1.0;

    uint32_t *indices = malloc((size_t)count * sizeof(*indices));
    if (!indices) return false;
    for (uint32_t i = 0U; i < count; ++i) indices[i] = i;

    uint32_t remaining = count;
    uint32_t guard = 0U;
    while (remaining > 3U) {
        bool clipped = false;
        for (uint32_t i = 0U; i < remaining; ++i) {
            const uint32_t prev_pos = i == 0U ? remaining - 1U : i - 1U;
            const uint32_t next_pos = i + 1U == remaining ? 0U : i + 1U;
            const MeshPoint a = points[indices[prev_pos]];
            const MeshPoint b = points[indices[i]];
            const MeshPoint c = points[indices[next_pos]];
            if (mesh_cross(a, b, c) * orientation <= 1e-9) continue;

            bool contains = false;
            for (uint32_t j = 0U; j < remaining; ++j) {
                if (j == prev_pos || j == i || j == next_pos) continue;
                if (mesh_point_in_triangle(points[indices[j]],
                                           a,
                                           b,
                                           c,
                                           orientation)) {
                    contains = true;
                    break;
                }
            }
            if (contains) continue;
            if (!mesh_append_triangle(mesh, a, b, c, inv_grid)) {
                free(indices);
                return false;
            }
            memmove(&indices[i],
                    &indices[i + 1U],
                    (size_t)(remaining - i - 1U) * sizeof(*indices));
            --remaining;
            clipped = true;
            break;
        }
        if (!clipped || ++guard > count * 2U) {
            free(indices);
            return false;
        }
    }

    if (remaining == 3U
        && !mesh_append_triangle(mesh,
                                 points[indices[0]],
                                 points[indices[1]],
                                 points[indices[2]],
                                 inv_grid)) {
        free(indices);
        return false;
    }
    free(indices);
    return true;
}

static bool mesh_emit_fallback_rows(const unsigned char *bits,
                                    uint32_t grid,
                                    OpenRideORMapLandcoverMesh *mesh)
{
    const double inv_grid = 1.0 / (double)grid;
    for (uint32_t y = 0U; y < grid; ++y) {
        uint32_t x = 0U;
        while (x < grid) {
            while (x < grid && !mesh_cell_get(bits, grid, (int)x, (int)y)) ++x;
            if (x >= grid) break;
            const uint32_t start = x;
            while (x < grid && mesh_cell_get(bits, grid, (int)x, (int)y)) ++x;
            const MeshPoint a = {(double)start, (double)y};
            const MeshPoint b = {(double)x, (double)y};
            const MeshPoint c = {(double)x, (double)(y + 1U)};
            const MeshPoint d = {(double)start, (double)(y + 1U)};
            if (!mesh_append_triangle(mesh, a, b, c, inv_grid)
                || !mesh_append_triangle(mesh, a, c, d, inv_grid)) {
                return false;
            }
        }
    }
    return true;
}

void openride_ormap_landcover_mesh_destroy(OpenRideORMapLandcoverMesh *mesh)
{
    if (!mesh) return;
    free(mesh->triangles);
    memset(mesh, 0, sizeof(*mesh));
}

bool openride_ormap_landcover_mesh_build(const unsigned char *bits,
                                         uint32_t grid,
                                         double simplify_tolerance_cells,
                                         OpenRideORMapLandcoverMesh *mesh)
{
    if (!bits || !mesh || grid == 0U || grid > UINT16_MAX - 1U) return false;
    openride_ormap_landcover_mesh_destroy(mesh);

    if (grid > UINT32_MAX / grid || grid * grid > UINT32_MAX / 4U) return false;
    const uint32_t edge_capacity = grid * grid * 4U;
    const uint32_t vertex_stride = grid + 1U;
    if (vertex_stride > UINT32_MAX / vertex_stride) return false;
    const uint32_t vertex_count = vertex_stride * vertex_stride;

    BoundaryEdge *edges = calloc(edge_capacity, sizeof(*edges));
    int32_t *heads = malloc((size_t)vertex_count * sizeof(*heads));
    MeshPoint *loop = malloc(((size_t)edge_capacity + 1U) * sizeof(*loop));
    if (!edges || !heads || !loop) {
        free(edges);
        free(heads);
        free(loop);
        return false;
    }
    for (uint32_t i = 0U; i < vertex_count; ++i) heads[i] = -1;

    uint32_t edge_count = 0U;
    bool ok = true;
    for (uint32_t y = 0U; y < grid && ok; ++y) {
        for (uint32_t x = 0U; x < grid && ok; ++x) {
            if (!mesh_cell_get(bits, grid, (int)x, (int)y)) continue;
            if (!mesh_cell_get(bits, grid, (int)x, (int)y - 1)) {
                ok = mesh_add_edge(edges, edge_capacity, &edge_count, heads,
                                   vertex_stride,
                                   (uint16_t)x, (uint16_t)y,
                                   (uint16_t)(x + 1U), (uint16_t)y);
            }
            if (ok && !mesh_cell_get(bits, grid, (int)x + 1, (int)y)) {
                ok = mesh_add_edge(edges, edge_capacity, &edge_count, heads,
                                   vertex_stride,
                                   (uint16_t)(x + 1U), (uint16_t)y,
                                   (uint16_t)(x + 1U), (uint16_t)(y + 1U));
            }
            if (ok && !mesh_cell_get(bits, grid, (int)x, (int)y + 1)) {
                ok = mesh_add_edge(edges, edge_capacity, &edge_count, heads,
                                   vertex_stride,
                                   (uint16_t)(x + 1U), (uint16_t)(y + 1U),
                                   (uint16_t)x, (uint16_t)(y + 1U));
            }
            if (ok && !mesh_cell_get(bits, grid, (int)x - 1, (int)y)) {
                ok = mesh_add_edge(edges, edge_capacity, &edge_count, heads,
                                   vertex_stride,
                                   (uint16_t)x, (uint16_t)(y + 1U),
                                   (uint16_t)x, (uint16_t)y);
            }
        }
    }

    const double inv_grid = 1.0 / (double)grid;
    for (uint32_t start_edge = 0U; start_edge < edge_count && ok; ++start_edge) {
        if (edges[start_edge].used) continue;
        const uint16_t start_x = edges[start_edge].x0;
        const uint16_t start_y = edges[start_edge].y0;
        uint32_t loop_count = 0U;
        int32_t current = (int32_t)start_edge;
        bool closed = false;
        while (current >= 0 && loop_count <= edge_count) {
            BoundaryEdge *edge = &edges[current];
            if (edge->used) break;
            edge->used = 1U;
            if (loop_count == 0U) {
                loop[loop_count++] = (MeshPoint){edge->x0, edge->y0};
            }
            loop[loop_count++] = (MeshPoint){edge->x1, edge->y1};
            if (edge->x1 == start_x && edge->y1 == start_y) {
                closed = true;
                break;
            }
            current = mesh_choose_next(edges,
                                       heads,
                                       vertex_stride,
                                       edge->x1,
                                       edge->y1,
                                       mesh_edge_direction(edge));
        }
        if (!closed || loop_count < 4U) {
            ok = false;
            break;
        }

        --loop_count; /* final point repeats the first */
        loop_count = mesh_simplify_closed(loop,
                                          loop_count,
                                          simplify_tolerance_cells);
        if (loop_count > 768U) {
            double tolerance = simplify_tolerance_cells > 0.0
                ? simplify_tolerance_cells : 0.5;
            for (unsigned pass = 0U; pass < 4U && loop_count > 768U; ++pass) {
                tolerance *= 1.5;
                loop_count = mesh_simplify_closed(loop, loop_count, tolerance);
            }
        }

        const uint32_t before = mesh->triangle_count;
        if (!mesh_triangulate_loop(loop, loop_count, inv_grid, mesh)) {
            mesh->triangle_count = before;
            ok = false;
            break;
        }
    }

    free(edges);
    free(heads);
    free(loop);

    if (!ok) {
        /* Preserve correctness for pathological contour topology. This is the
         * old row-run representation and therefore a safe per-tile fallback. */
        openride_ormap_landcover_mesh_destroy(mesh);
        return mesh_emit_fallback_rows(bits, grid, mesh);
    }
    return true;
}
