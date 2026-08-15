#ifndef OPENRIDE_ORMAP_LANDCOVER_MESH_H
#define OPENRIDE_ORMAP_LANDCOVER_MESH_H

#include <stdbool.h>
#include <stdint.h>

typedef struct OpenRideORMapLandcoverTriangle {
    double x0;
    double y0;
    double x1;
    double y1;
    double x2;
    double y2;
} OpenRideORMapLandcoverTriangle;

typedef struct OpenRideORMapLandcoverMesh {
    OpenRideORMapLandcoverTriangle *triangles;
    uint32_t triangle_count;
    uint32_t triangle_capacity;
} OpenRideORMapLandcoverMesh;

/*
 * Convert a compact binary occupancy grid into generalized tile-local
 * triangles. Boundary loops are traced from the raster, simplified in grid
 * cell units, then triangulated. Coordinates in the resulting mesh are in
 * [0, 1] tile-local space.
 *
 * Coarse overview landcover intentionally closes holes. Large water holes are
 * subsequently covered by the water layer, while closing small holes prevents
 * noisy low-zoom checkerboard detail and overlapping-alpha artefacts.
 */
bool openride_ormap_landcover_mesh_build(const unsigned char *bits,
                                         uint32_t grid,
                                         double simplify_tolerance_cells,
                                         OpenRideORMapLandcoverMesh *mesh);

void openride_ormap_landcover_mesh_destroy(OpenRideORMapLandcoverMesh *mesh);

#endif
