#ifndef OPENRIDE_NAVIGATION_ENGINE_H
#define OPENRIDE_NAVIGATION_ENGINE_H

#include "openride/routing_engine.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum OpenRideNavigationStatus {
    OPENRIDE_NAVIGATION_INACTIVE = 0,
    OPENRIDE_NAVIGATION_ON_ROUTE,
    OPENRIDE_NAVIGATION_OFF_ROUTE,
    OPENRIDE_NAVIGATION_ARRIVED
} OpenRideNavigationStatus;

typedef struct OpenRideNavigationConfig {
    double off_route_threshold_m;
    double return_to_route_threshold_m;
    double arrival_threshold_m;
    uint32_t local_search_radius_segments;
} OpenRideNavigationConfig;

typedef struct OpenRideNavigationState {
    bool valid;
    OpenRideNavigationStatus status;
    double gps_lat;
    double gps_lon;
    double matched_lat;
    double matched_lon;
    double distance_from_route_m;
    double traveled_m;
    double remaining_m;
    double progress_ratio;
    double speed_mps;
    double heading_deg;
    uint32_t route_segment_index;
    double route_segment_fraction;
} OpenRideNavigationState;

typedef struct OpenRideNavigationEngine {
    const OpenRideRoute *route;
    double *cumulative_geometry_m;
    uint32_t geometry_count;
    double geometry_distance_m;
    double route_distance_m;
    OpenRideNavigationConfig config;
    uint32_t last_segment_index;
    bool has_last_segment;
    bool currently_off_route;
} OpenRideNavigationEngine;

OpenRideNavigationConfig openride_navigation_config_default(void);

void openride_navigation_engine_init(OpenRideNavigationEngine *navigation);
void openride_navigation_engine_destroy(OpenRideNavigationEngine *navigation);

bool openride_navigation_engine_set_route(OpenRideNavigationEngine *navigation,
                                          const OpenRideRoute *route,
                                          char *error,
                                          size_t error_size);

void openride_navigation_engine_clear_route(OpenRideNavigationEngine *navigation);

bool openride_navigation_engine_update(OpenRideNavigationEngine *navigation,
                                       double lat,
                                       double lon,
                                       double speed_mps,
                                       double heading_deg,
                                       OpenRideNavigationState *state);

const char *openride_navigation_status_name(OpenRideNavigationStatus status);

#endif
