#ifndef OPENRIDE_DRIVE_PERSPECTIVE_H
#define OPENRIDE_DRIVE_PERSPECTIVE_H

#include <math.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OpenRideDrivePerspectiveConfig {
    double horizon_y_ratio;
    double rider_anchor_y_ratio;
} OpenRideDrivePerspectiveConfig;

static inline double openride_drive_perspective_clamp01(double value)
{
    if (value <= 0.0) return 0.0;
    if (value >= 1.0) return 1.0;
    return value;
}

static inline bool openride_drive_perspective_config_valid(
    const OpenRideDrivePerspectiveConfig *config)
{
    if (!config
        || !isfinite(config->horizon_y_ratio)
        || !isfinite(config->rider_anchor_y_ratio)) {
        return false;
    }

    const double horizon = config->horizon_y_ratio;
    const double anchor = config->rider_anchor_y_ratio;

    /*
     * The projective denominator is 1 - horizon*y/(anchor*anchor).
     * Keep it positive across the whole source image so the view never
     * crosses the projective horizon inside the rendered map.
     */
    return horizon >= 0.0
        && anchor > 0.0
        && anchor < 1.0
        && horizon < anchor * anchor;
}

static inline OpenRideDrivePerspectiveConfig
openride_drive_perspective_default_config(void)
{
    return (OpenRideDrivePerspectiveConfig){
        .horizon_y_ratio = 0.15,
        .rider_anchor_y_ratio = 0.68
    };
}

static inline double openride_drive_perspective_denominator(
    const OpenRideDrivePerspectiveConfig *config,
    double source_y_ratio)
{
    if (!openride_drive_perspective_config_valid(config)) return 1.0;

    const double source = openride_drive_perspective_clamp01(source_y_ratio);
    const double anchor = config->rider_anchor_y_ratio;
    const double projective_c =
        -config->horizon_y_ratio / (anchor * anchor);
    return 1.0 + projective_c * source;
}

static inline double openride_drive_perspective_y_ratio(
    const OpenRideDrivePerspectiveConfig *config,
    double source_y_ratio)
{
    const double source = openride_drive_perspective_clamp01(source_y_ratio);
    if (!openride_drive_perspective_config_valid(config)) return source;

    const double horizon = config->horizon_y_ratio;
    const double anchor = config->rider_anchor_y_ratio;
    const double numerator_scale = 1.0 - 2.0 * horizon / anchor;
    const double denominator =
        openride_drive_perspective_denominator(config, source);

    /*
     * Möbius/projective Y transform. It maps source y=0 to the visual horizon,
     * fixes the rider anchor exactly, and has derivative 1.0 at that anchor.
     * Combined with width_scale() below it forms one homography, rather than
     * the independent easing curves used by V2.5 that bent straight roads.
     */
    return (numerator_scale * source + horizon) / denominator;
}

static inline double openride_drive_perspective_width_scale(
    const OpenRideDrivePerspectiveConfig *config,
    double source_y_ratio)
{
    if (!openride_drive_perspective_config_valid(config)) return 1.0;

    const double anchor = config->rider_anchor_y_ratio;
    const double horizon = config->horizon_y_ratio;
    const double numerator = 1.0 - horizon / anchor;
    const double denominator =
        openride_drive_perspective_denominator(config, source_y_ratio);
    return numerator / denominator;
}

static inline double openride_drive_perspective_x_ratio(
    const OpenRideDrivePerspectiveConfig *config,
    double source_x_ratio,
    double source_y_ratio)
{
    const double source_x = openride_drive_perspective_clamp01(source_x_ratio);
    if (!openride_drive_perspective_config_valid(config)) return source_x;

    const double scale =
        openride_drive_perspective_width_scale(config, source_y_ratio);
    return 0.5 + (source_x - 0.5) * scale;
}

#ifdef __cplusplus
}
#endif

#endif
