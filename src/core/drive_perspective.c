#include "openride/drive_perspective.h"

#include <math.h>

static double clamp01(double value)
{
    if (value <= 0.0) return 0.0;
    if (value >= 1.0) return 1.0;
    return value;
}

static bool config_valid(const OpenRideDrivePerspectiveConfig *config)
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

OpenRideDrivePerspectiveConfig openride_drive_perspective_default_config(void)
{
    return (OpenRideDrivePerspectiveConfig){
        .horizon_y_ratio = 0.10,
        .rider_anchor_y_ratio = 0.68,
        .top_width_scale = 0.60,
        .bottom_width_scale = 1.08
    };
}

double openride_drive_perspective_y_ratio(
    const OpenRideDrivePerspectiveConfig *config,
    double source_y_ratio)
{
    const double source = clamp01(source_y_ratio);
    if (!config_valid(config)) return source;

    const double anchor = config->rider_anchor_y_ratio;
    if (source >= anchor) {
        /* Preserve rider scale and the near field exactly. */
        return source;
    }

    const double horizon = config->horizon_y_ratio;
    const double output_span = anchor - horizon;

    /*
     * The exponent is chosen so the far-field curve reaches the rider anchor
     * with derivative 1.0. The image can therefore acquire a horizon and
     * vertical compression without a visible kink or size jump at the bike.
     */
    const double exponent = anchor / output_span;
    const double normalized = source / anchor;
    return horizon
        + output_span * pow(normalized, exponent);
}

double openride_drive_perspective_width_scale(
    const OpenRideDrivePerspectiveConfig *config,
    double source_y_ratio)
{
    const double source = clamp01(source_y_ratio);
    if (!config_valid(config)) return 1.0;

    const double anchor = config->rider_anchor_y_ratio;
    if (source <= anchor) {
        const double normalized = anchor > 0.0 ? source / anchor : 1.0;
        const double eased = pow(normalized, 0.90);
        return config->top_width_scale
            + (1.0 - config->top_width_scale) * eased;
    }

    const double normalized =
        (source - anchor) / (1.0 - anchor);
    return 1.0
        + (config->bottom_width_scale - 1.0) * normalized;
}
