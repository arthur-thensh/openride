#include "openride/drive_mode.h"

#include <math.h>
#include <string.h>

#define OPENRIDE_PI 3.14159265358979323846
#define OPENRIDE_EARTH_RADIUS_M 6371008.8

static double clampd(double value, double min_value, double max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static double normalize_bearing(double degrees)
{
    degrees = fmod(degrees, 360.0);
    if (degrees < 0.0) degrees += 360.0;
    return degrees;
}

static double shortest_angle_delta(double from_deg, double to_deg)
{
    double delta = normalize_bearing(to_deg) - normalize_bearing(from_deg);
    if (delta > 180.0) delta -= 360.0;
    if (delta < -180.0) delta += 360.0;
    return delta;
}

static void project_ahead(double lat_deg,
                          double lon_deg,
                          double bearing_deg,
                          double distance_m,
                          double *out_lat,
                          double *out_lon)
{
    if (!out_lat || !out_lon) return;
    if (distance_m <= 0.0) {
        *out_lat = lat_deg;
        *out_lon = lon_deg;
        return;
    }

    const double lat1 = lat_deg * OPENRIDE_PI / 180.0;
    const double lon1 = lon_deg * OPENRIDE_PI / 180.0;
    const double bearing = normalize_bearing(bearing_deg) * OPENRIDE_PI / 180.0;
    const double angular = distance_m / OPENRIDE_EARTH_RADIUS_M;

    const double sin_lat1 = sin(lat1);
    const double cos_lat1 = cos(lat1);
    const double sin_angular = sin(angular);
    const double cos_angular = cos(angular);

    const double lat2 = asin(sin_lat1 * cos_angular
                             + cos_lat1 * sin_angular * cos(bearing));
    const double lon2 = lon1 + atan2(sin(bearing) * sin_angular * cos_lat1,
                                     cos_angular - sin_lat1 * sin(lat2));

    *out_lat = lat2 * 180.0 / OPENRIDE_PI;
    *out_lon = lon2 * 180.0 / OPENRIDE_PI;
    while (*out_lon > 180.0) *out_lon -= 360.0;
    while (*out_lon < -180.0) *out_lon += 360.0;
}

void openride_drive_mode_init(OpenRideDriveModeState *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->heading_up = true;
    state->auto_zoom = true;
    state->camera_zoom = 16.0;
    state->gps_quality = OPENRIDE_GPS_UNAVAILABLE;
}

void openride_drive_mode_set_active(OpenRideDriveModeState *state, bool active)
{
    if (!state) return;
    state->active = active;
    if (!active) state->initialized = false;
}

void openride_drive_mode_set_heading_up(OpenRideDriveModeState *state, bool heading_up)
{
    if (!state) return;
    state->heading_up = heading_up;
    if (!heading_up) state->camera_bearing_deg = 0.0;
}

void openride_drive_mode_set_auto_zoom(OpenRideDriveModeState *state, bool auto_zoom)
{
    if (!state) return;
    state->auto_zoom = auto_zoom;
}

double openride_drive_mode_target_zoom(double speed_mps, double maneuver_distance_m)
{
    const double speed_kph = fmax(0.0, speed_mps) * 3.6;

    /*
     * Keep the rider closer to the road than the first Android DriveMode
     * tuning. The look-ahead is handled separately so map scale can stay
     * stable while the framing anticipates an upcoming maneuver.
     */
    double zoom = 17.8;

    if (speed_kph >= 110.0) zoom = 15.2;
    else if (speed_kph >= 90.0) zoom = 15.6;
    else if (speed_kph >= 70.0) zoom = 16.0;
    else if (speed_kph >= 50.0) zoom = 16.5;
    else if (speed_kph >= 30.0) zoom = 17.0;
    else if (speed_kph >= 15.0) zoom = 17.4;

    if (isfinite(maneuver_distance_m)) {
        if (maneuver_distance_m < 90.0) zoom += 0.8;
        else if (maneuver_distance_m < 250.0) zoom += 0.5;
        else if (maneuver_distance_m < 500.0) zoom += 0.25;
    }

    return clampd(zoom, 14.8, 18.0);
}

double openride_drive_mode_lookahead_m(double speed_mps)
{
    const double speed_kph = fmax(0.0, speed_mps) * 3.6;
    return clampd(22.0 + speed_kph * 1.35, 25.0, 180.0);
}

double openride_drive_mode_target_lookahead_m(double speed_mps,
                                               double maneuver_distance_m)
{
    const double base = openride_drive_mode_lookahead_m(speed_mps);
    if (!isfinite(maneuver_distance_m)
        || maneuver_distance_m < 0.0
        || maneuver_distance_m >= 450.0) {
        return base;
    }

    /*
     * Frame more of the road leading into the next maneuver while it is still
     * far enough away to be useful, then progressively pull the camera target
     * back toward the rider near the junction. This makes intersections and
     * roundabouts easier to read without rotating the map before the bike has
     * actually changed heading.
     *
     * At 60 km/h the ordinary look-ahead is about 103 m:
     *   400 m to maneuver -> about 149 m (anticipation)
     *   300 m             -> about 135 m
     *   200 m             -> about 90 m
     *   100 m             -> about 45 m
     *    50 m             -> about 35 m
     */
    const double maneuver_target = maneuver_distance_m * 0.45;
    return clampd(maneuver_target, 35.0, base * 1.45);
}

OpenRideGPSQuality openride_drive_mode_gps_quality(bool gps_active,
                                                   bool has_sample,
                                                   double sample_age_s,
                                                   double accuracy_m)
{
    if (!gps_active) return OPENRIDE_GPS_UNAVAILABLE;
    if (!has_sample || sample_age_s > 5.0) return OPENRIDE_GPS_LOST;
    if (accuracy_m <= 0.0 || accuracy_m > 50.0) return OPENRIDE_GPS_POOR;
    if (accuracy_m > 20.0) return OPENRIDE_GPS_FAIR;
    return OPENRIDE_GPS_GOOD;
}

const char *openride_drive_mode_gps_quality_name(OpenRideGPSQuality quality)
{
    switch (quality) {
        case OPENRIDE_GPS_GOOD: return "GPS BON";
        case OPENRIDE_GPS_FAIR: return "GPS MOYEN";
        case OPENRIDE_GPS_POOR: return "GPS FAIBLE";
        case OPENRIDE_GPS_LOST: return "GPS PERDU";
        case OPENRIDE_GPS_UNAVAILABLE:
        default: return "GPS OFF";
    }
}

void openride_drive_mode_update(OpenRideDriveModeState *state,
                                bool gps_active,
                                bool has_sample,
                                double sample_age_s,
                                double accuracy_m,
                                double lat,
                                double lon,
                                double speed_mps,
                                double heading_deg,
                                double maneuver_distance_m,
                                double delta_seconds)
{
    if (!state) return;

    state->gps_age_s = sample_age_s;
    state->gps_accuracy_m = accuracy_m;
    state->gps_quality = openride_drive_mode_gps_quality(gps_active,
                                                          has_sample,
                                                          sample_age_s,
                                                          accuracy_m);
    if (!state->active || !has_sample || state->gps_quality == OPENRIDE_GPS_LOST) return;

    if (delta_seconds < 0.0) delta_seconds = 0.0;
    if (delta_seconds > 0.25) delta_seconds = 0.25;

    double target_bearing = state->heading_up ? normalize_bearing(heading_deg) : 0.0;
    if (speed_mps < 1.5 && state->initialized) {
        target_bearing = state->heading_up ? state->camera_bearing_deg : 0.0;
    }

    const double lookahead =
        openride_drive_mode_target_lookahead_m(speed_mps,
                                               maneuver_distance_m);
    double target_lat = lat;
    double target_lon = lon;
    project_ahead(lat, lon, target_bearing, lookahead, &target_lat, &target_lon);
    const double target_zoom = openride_drive_mode_target_zoom(speed_mps,
                                                                maneuver_distance_m);

    if (!state->initialized) {
        state->camera_lat = target_lat;
        state->camera_lon = target_lon;
        state->camera_zoom = target_zoom;
        state->camera_bearing_deg = target_bearing;
        state->initialized = true;
        return;
    }

    const double position_alpha = 1.0 - exp(-delta_seconds * 4.5);
    const double zoom_alpha = 1.0 - exp(-delta_seconds * 3.0);
    const double bearing_alpha = 1.0 - exp(-delta_seconds * 5.0);

    state->camera_lat += (target_lat - state->camera_lat) * position_alpha;
    state->camera_lon += (target_lon - state->camera_lon) * position_alpha;
    if (state->auto_zoom) {
        state->camera_zoom += (target_zoom - state->camera_zoom) * zoom_alpha;
    }
    state->camera_bearing_deg = normalize_bearing(
        state->camera_bearing_deg
        + shortest_angle_delta(state->camera_bearing_deg, target_bearing) * bearing_alpha);
}
