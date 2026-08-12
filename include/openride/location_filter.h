#ifndef OPENRIDE_LOCATION_FILTER_H
#define OPENRIDE_LOCATION_FILTER_H

#include <stdbool.h>

typedef struct OpenRideLocationFilterConfig {
    double position_time_constant_s;
    double speed_time_constant_s;
    double heading_time_constant_s;
    double reset_jump_distance_m;
} OpenRideLocationFilterConfig;

typedef struct OpenRideFilteredLocation {
    bool valid;
    double lat;
    double lon;
    double speed_mps;
    double heading_deg;
} OpenRideFilteredLocation;

typedef struct OpenRideLocationFilter {
    OpenRideLocationFilterConfig config;
    OpenRideFilteredLocation value;
} OpenRideLocationFilter;

OpenRideLocationFilterConfig openride_location_filter_config_default(void);
void openride_location_filter_init(OpenRideLocationFilter *filter);
void openride_location_filter_reset(OpenRideLocationFilter *filter);

bool openride_location_filter_update(OpenRideLocationFilter *filter,
                                     double lat,
                                     double lon,
                                     double speed_mps,
                                     double heading_deg,
                                     double delta_seconds,
                                     OpenRideFilteredLocation *output);

#endif
