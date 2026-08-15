#include "openride/ormap_landcover_mesh.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_cell(unsigned char *bits,
                     uint32_t grid,
                     uint32_t x,
                     uint32_t y)
{
    const uint32_t index = y * grid + x;
    bits[index >> 3U] |= (unsigned char)(1U << (index & 7U));
}

static double mesh_area(const OpenRideORMapLandcoverMesh *mesh)
{
    double area = 0.0;
    for (uint32_t i = 0U; i < mesh->triangle_count; ++i) {
        const OpenRideORMapLandcoverTriangle *t = &mesh->triangles[i];
        const double cross = (t->x1 - t->x0) * (t->y2 - t->y0)
            - (t->y1 - t->y0) * (t->x2 - t->x0);
        area += fabs(cross) * 0.5;
        assert(t->x0 >= 0.0 && t->x0 <= 1.0);
        assert(t->x1 >= 0.0 && t->x1 <= 1.0);
        assert(t->x2 >= 0.0 && t->x2 <= 1.0);
        assert(t->y0 >= 0.0 && t->y0 <= 1.0);
        assert(t->y1 >= 0.0 && t->y1 <= 1.0);
        assert(t->y2 >= 0.0 && t->y2 <= 1.0);
    }
    return area;
}

static void test_rectangle_is_generalized(void)
{
    enum { GRID = 16 };
    unsigned char bits[(GRID * GRID + 7) / 8] = {0};
    for (uint32_t y = 4U; y < 12U; ++y) {
        for (uint32_t x = 2U; x < 10U; ++x) set_cell(bits, GRID, x, y);
    }

    OpenRideORMapLandcoverMesh mesh = {0};
    assert(openride_ormap_landcover_mesh_build(bits, GRID, 1.0, &mesh));
    assert(mesh.triangle_count == 2U);
    assert(fabs(mesh_area(&mesh) - 64.0 / 256.0) < 1e-9);
    openride_ormap_landcover_mesh_destroy(&mesh);
}

static void test_staircase_loses_row_band_shape(void)
{
    enum { GRID = 16 };
    unsigned char bits[(GRID * GRID + 7) / 8] = {0};
    uint32_t row_runs = 0U;
    for (uint32_t y = 2U; y < 14U; ++y) {
        const uint32_t start = y - 1U;
        for (uint32_t x = start; x < start + 3U && x < GRID; ++x) {
            set_cell(bits, GRID, x, y);
        }
        ++row_runs;
    }

    OpenRideORMapLandcoverMesh mesh = {0};
    assert(openride_ormap_landcover_mesh_build(bits, GRID, 1.0, &mesh));
    assert(mesh.triangle_count < row_runs * 2U);
    assert(mesh.triangle_count >= 2U);
    (void)mesh_area(&mesh);
    openride_ormap_landcover_mesh_destroy(&mesh);
}

static void test_diagonal_islands_stay_separate(void)
{
    enum { GRID = 8 };
    unsigned char bits[(GRID * GRID + 7) / 8] = {0};
    set_cell(bits, GRID, 2U, 2U);
    set_cell(bits, GRID, 3U, 3U);

    OpenRideORMapLandcoverMesh mesh = {0};
    assert(openride_ormap_landcover_mesh_build(bits, GRID, 0.1, &mesh));
    assert(mesh.triangle_count == 4U);
    assert(fabs(mesh_area(&mesh) - 2.0 / 64.0) < 1e-9);
    openride_ormap_landcover_mesh_destroy(&mesh);
}

static void test_overview_closes_holes_once(void)
{
    enum { GRID = 12 };
    unsigned char bits[(GRID * GRID + 7) / 8] = {0};
    for (uint32_t y = 2U; y < 10U; ++y) {
        for (uint32_t x = 2U; x < 10U; ++x) {
            if (x >= 5U && x < 7U && y >= 5U && y < 7U) continue;
            set_cell(bits, GRID, x, y);
        }
    }

    OpenRideORMapLandcoverMesh mesh = {0};
    assert(openride_ormap_landcover_mesh_build(bits, GRID, 0.1, &mesh));
    assert(fabs(mesh_area(&mesh) - 64.0 / 144.0) < 1e-9);
    openride_ormap_landcover_mesh_destroy(&mesh);
}

int main(void)
{
    test_rectangle_is_generalized();
    test_staircase_loses_row_band_shape();
    test_diagonal_islands_stay_separate();
    test_overview_closes_holes_once();
    puts("ormap landcover mesh tests passed");
    return 0;
}
