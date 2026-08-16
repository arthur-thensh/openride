#ifndef OPENRIDE_ORMAP_PYRAMID_SURFACE_INTERNAL_H
#define OPENRIDE_ORMAP_PYRAMID_SURFACE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct OpenRideORMapPyramidPoint {
    double x;
    double y;
} OpenRideORMapPyramidPoint;

/*
 * Returned vertices are always selected from the input ring. The v11 builder
 * calls this sequentially z14 -> z13 -> ... -> z9, so every coarse vertex is
 * guaranteed to exist in every finer level.
 */
bool openride_ormap_pyramid_simplify_closed_ring(
    const OpenRideORMapPyramidPoint *input,
    uint32_t input_count,
    double tolerance,
    OpenRideORMapPyramidPoint **output,
    uint32_t *output_count);

double openride_ormap_pyramid_polygon_signed_area(
    const OpenRideORMapPyramidPoint *points,
    uint32_t count);

#endif
