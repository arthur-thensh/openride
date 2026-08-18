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
    double top_width_scale;
    double bottom_width_scale;
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
    return config
        && isfinite(config->horizon_y_ratio)
        && isfinite(config->rider_anchor_y_ratio)
        && isfinite(config->top_width_scale)
        && isfinite(config->bottom_width_scale)
        && config->horizon_y_ratio >= 0.0
        && config->horizon_y_ratio < config->rider_anchor_y_ratio
        && config->rider_anchor_y_ratio > 0.0
        && config->rider_anchor_y_ratio < 1.0
        && config->top_width_scale > 0.0
        && config->bottom_width_scale > 0.0;
}

static inline OpenRideDrivePerspectiveConfig
openride_drive_perspective_default_config(void)
{
    return (OpenRideDrivePerspectiveConfig){
        .horizon_y_ratio = 0.10,
        .rider_anchor_y_ratio = 0.68,
        .top_width_scale = 0.60,
        .bottom_width_scale = 1.08
    };
}

static inline double openride_drive_perspective_y_ratio(
    const OpenRideDrivePerspectiveConfig *config,
    double source_y_ratio)
{
    const double source = openride_drive_perspective_clamp01(source_y_ratio);
    if (!openride_drive_perspective_config_valid(config)) return source;

    const double anchor = config->rider_anchor_y_ratio;
    if (source >= anchor) {
        /* Preserve rider scale and the near field exactly. */
        return source;
    }

    const double horizon = config->horizon_y_ratio;
    const double output_span = anchor - horizon;

    /*
     * The exponent makes the far-field curve meet the rider anchor with a
     * derivative of 1.0. The map therefore gains depth without a visible kink
     * or a sudden size change at the motorcycle.
     */
    const double exponent = anchor / output_span;
    const double normalized = source / anchor;
    return horizon + output_span * pow(normalized, exponent);
}

static inline double openride_drive_perspective_width_scale(
    const OpenRideDrivePerspectiveConfig *config,
    double source_y_ratio)
{
    const double source = openride_drive_perspective_clamp01(source_y_ratio);
    if (!openride_drive_perspective_config_valid(config)) return 1.0;

    const double anchor = config->rider_anchor_y_ratio;
    if (source <= anchor) {
        const double normalized = source / anchor;
        const double eased = pow(normalized, 0.90);
        return config->top_width_scale
            + (1.0 - config->top_width_scale) * eased;
    }

    const double normalized = (source - anchor) / (1.0 - anchor);
    return 1.0
        + (config->bottom_width_scale - 1.0) * normalized;
}

#ifdef __cplusplus
}
#endif

#endif
