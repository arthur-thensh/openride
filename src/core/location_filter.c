#include "openride/location_filter.h"
#include "openride/map_selection.h"

#include <math.h>
#include <string.h>

static double normalize_heading(double heading)
{
    if (!isfinite(heading)) return 0.0;
    heading = fmod(heading, 360.0);
    if (heading < 0.0) heading += 360.0;
    return heading;
}

static double alpha_for(double delta_seconds, double time_constant_s)
{
    if (!(delta_seconds > 0.0) || !isfinite(delta_seconds)) return 1.0;
    if (!(time_constant_s > 0.0) || !isfinite(time_constant_s)) return 1.0;
    return 1.0 - exp(-delta_seconds / time_constant_s);
}

static double heading_blend(double current, double target, double alpha)
{
    double delta = normalize_heading(target) - normalize_heading(current);
    if (delta > 180.0) delta -= 360.0;
    if (delta < -180.0) delta += 360.0;
    return normalize_heading(current + delta * alpha);
}

OpenRideLocationFilterConfig openride_location_filter_config_default(void)
{
    OpenRideLocationFilterConfig config;
    config.position_time_constant_s = 0.35;
    config.speed_time_constant_s = 0.55;
    config.heading_time_constant_s = 0.45;
    config.reset_jump_distance_m = 350.0;
    return config;
}

void openride_location_filter_init(OpenRideLocationFilter *filter)
{
    if (!filter) return;
    memset(filter, 0, sizeof(*filter));
    filter->config = openride_location_filter_config_default();
}

void openride_location_filter_reset(OpenRideLocationFilter *filter)
{
    if (!filter) return;
    const OpenRideLocationFilterConfig config = filter->config;
    memset(filter, 0, sizeof(*filter));
    filter->config = config;
}

bool openride_location_filter_update(OpenRideLocationFilter *filter,
                                     double lat,
                                     double lon,
                                     double speed_mps,
                                     double heading_deg,
                                     double delta_seconds,
                                     OpenRideFilteredLocation *output)
{
    if (!filter || !isfinite(lat) || !isfinite(lon)) return false;
    if (!isfinite(speed_mps) || speed_mps < 0.0) speed_mps = 0.0;
    heading_deg = normalize_heading(heading_deg);

    bool reset = !filter->value.valid;
    if (!reset && filter->config.reset_jump_distance_m > 0.0) {
        const double jump = openride_geo_distance_m(filter->value.lat,
                                                    filter->value.lon,
                                                    lat,
                                                    lon);
        if (!isfinite(jump) || jump > filter->config.reset_jump_distance_m) reset = true;
    }

    if (reset) {
        filter->value.valid = true;
        filter->value.lat = lat;
        filter->value.lon = lon;
        filter->value.speed_mps = speed_mps;
        filter->value.heading_deg = heading_deg;
    } else {
        const double position_alpha = alpha_for(delta_seconds,
                                                filter->config.position_time_constant_s);
        const double speed_alpha = alpha_for(delta_seconds,
                                             filter->config.speed_time_constant_s);
        const double heading_alpha = alpha_for(delta_seconds,
                                               filter->config.heading_time_constant_s);
        filter->value.lat += (lat - filter->value.lat) * position_alpha;
        filter->value.lon += (lon - filter->value.lon) * position_alpha;
        filter->value.speed_mps += (speed_mps - filter->value.speed_mps) * speed_alpha;
        filter->value.heading_deg = heading_blend(filter->value.heading_deg,
                                                  heading_deg,
                                                  heading_alpha);
    }

    if (output) *output = filter->value;
    return true;
}
