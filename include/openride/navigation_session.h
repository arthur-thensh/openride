#ifndef OPENRIDE_NAVIGATION_SESSION_H
#define OPENRIDE_NAVIGATION_SESSION_H

#include "openride/navigation_engine.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct OpenRideNavigationSessionConfig {
    bool auto_reroute_enabled;
    double off_route_confirm_s;
    double reroute_cooldown_s;
    double moving_speed_threshold_mps;
    double max_reasonable_speed_mps;
} OpenRideNavigationSessionConfig;

typedef struct OpenRideNavigationTripStats {
    double elapsed_s;
    double moving_s;
    double gps_distance_m;
    double average_speed_mps;
    double max_speed_mps;
    uint32_t reroute_count;
} OpenRideNavigationTripStats;

typedef struct OpenRideNavigationSession {
    OpenRideNavigationSessionConfig config;
    OpenRideNavigationTripStats stats;
    double off_route_elapsed_s;
    double reroute_cooldown_remaining_s;
    double last_lat;
    double last_lon;
    bool has_last_position;
    bool reroute_requested;
} OpenRideNavigationSession;

OpenRideNavigationSessionConfig openride_navigation_session_config_default(void);

void openride_navigation_session_init(OpenRideNavigationSession *session);
void openride_navigation_session_reset(OpenRideNavigationSession *session);

void openride_navigation_session_set_auto_reroute(OpenRideNavigationSession *session,
                                                  bool enabled);

void openride_navigation_session_update(OpenRideNavigationSession *session,
                                        const OpenRideNavigationState *navigation,
                                        double lat,
                                        double lon,
                                        double speed_mps,
                                        double delta_seconds);

bool openride_navigation_session_take_reroute_request(OpenRideNavigationSession *session);
void openride_navigation_session_mark_rerouted(OpenRideNavigationSession *session);

const OpenRideNavigationTripStats *openride_navigation_session_stats(
    const OpenRideNavigationSession *session);

#endif
