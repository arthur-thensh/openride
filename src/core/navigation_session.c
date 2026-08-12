#include "openride/navigation_session.h"
#include "openride/map_selection.h"

#include <math.h>
#include <string.h>

static double clamp_nonnegative(double value)
{
    return isfinite(value) && value > 0.0 ? value : 0.0;
}

OpenRideNavigationSessionConfig openride_navigation_session_config_default(void)
{
    OpenRideNavigationSessionConfig config;
    config.auto_reroute_enabled = true;
    config.off_route_confirm_s = 2.0;
    config.reroute_cooldown_s = 8.0;
    config.moving_speed_threshold_mps = 1.0;
    config.max_reasonable_speed_mps = 70.0;
    return config;
}

void openride_navigation_session_init(OpenRideNavigationSession *session)
{
    if (!session) return;
    memset(session, 0, sizeof(*session));
    session->config = openride_navigation_session_config_default();
}

void openride_navigation_session_reset(OpenRideNavigationSession *session)
{
    if (!session) return;
    const OpenRideNavigationSessionConfig config = session->config;
    memset(session, 0, sizeof(*session));
    session->config = config;
}

void openride_navigation_session_set_auto_reroute(OpenRideNavigationSession *session,
                                                  bool enabled)
{
    if (!session) return;
    session->config.auto_reroute_enabled = enabled;
    if (!enabled) {
        session->off_route_elapsed_s = 0.0;
        session->reroute_requested = false;
    }
}

static void update_trip_distance(OpenRideNavigationSession *session,
                                 double lat,
                                 double lon,
                                 double speed_mps,
                                 double delta_seconds)
{
    if (!session || !isfinite(lat) || !isfinite(lon)) return;

    if (session->has_last_position) {
        const double distance_m = openride_geo_distance_m(session->last_lat,
                                                          session->last_lon,
                                                          lat,
                                                          lon);
        const double allowed_speed_mps = session->config.max_reasonable_speed_mps > 0.0
            ? session->config.max_reasonable_speed_mps : 70.0;
        const double allowance_m = fmax(35.0, allowed_speed_mps * fmax(delta_seconds, 0.2));
        if (isfinite(distance_m) && distance_m >= 0.0 && distance_m <= allowance_m) {
            session->stats.gps_distance_m += distance_m;
        }
    }

    session->last_lat = lat;
    session->last_lon = lon;
    session->has_last_position = true;

    if (isfinite(speed_mps) && speed_mps > session->stats.max_speed_mps) {
        session->stats.max_speed_mps = speed_mps;
    }
}

void openride_navigation_session_update(OpenRideNavigationSession *session,
                                        const OpenRideNavigationState *navigation,
                                        double lat,
                                        double lon,
                                        double speed_mps,
                                        double delta_seconds)
{
    if (!session) return;
    delta_seconds = clamp_nonnegative(delta_seconds);
    if (delta_seconds > 5.0) delta_seconds = 5.0;

    session->stats.elapsed_s += delta_seconds;
    if (isfinite(speed_mps) && speed_mps >= session->config.moving_speed_threshold_mps) {
        session->stats.moving_s += delta_seconds;
    }
    update_trip_distance(session, lat, lon, speed_mps, delta_seconds);

    if (session->stats.moving_s > 0.0) {
        session->stats.average_speed_mps = session->stats.gps_distance_m
            / session->stats.moving_s;
    }

    if (session->reroute_cooldown_remaining_s > 0.0) {
        session->reroute_cooldown_remaining_s -= delta_seconds;
        if (session->reroute_cooldown_remaining_s < 0.0) {
            session->reroute_cooldown_remaining_s = 0.0;
        }
    }

    if (!navigation || !navigation->valid
        || navigation->status == OPENRIDE_NAVIGATION_ARRIVED) {
        session->off_route_elapsed_s = 0.0;
        return;
    }

    if (navigation->status == OPENRIDE_NAVIGATION_OFF_ROUTE) {
        session->off_route_elapsed_s += delta_seconds;
    } else {
        session->off_route_elapsed_s = 0.0;
    }

    if (session->config.auto_reroute_enabled
        && !session->reroute_requested
        && session->reroute_cooldown_remaining_s <= 0.0
        && session->off_route_elapsed_s >= session->config.off_route_confirm_s) {
        session->reroute_requested = true;
    }
}

bool openride_navigation_session_take_reroute_request(OpenRideNavigationSession *session)
{
    if (!session || !session->reroute_requested) return false;
    session->reroute_requested = false;
    return true;
}

void openride_navigation_session_mark_rerouted(OpenRideNavigationSession *session)
{
    if (!session) return;
    ++session->stats.reroute_count;
    session->off_route_elapsed_s = 0.0;
    session->reroute_requested = false;
    session->reroute_cooldown_remaining_s = session->config.reroute_cooldown_s;
    session->has_last_position = false;
}

const OpenRideNavigationTripStats *openride_navigation_session_stats(
    const OpenRideNavigationSession *session)
{
    return session ? &session->stats : NULL;
}
