#include "openride/gps_simulator.h"
#include "openride/map_selection.h"

#ifdef __ANDROID__
#include <SDL3/SDL.h>
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENRIDE_EARTH_RADIUS_M 6371008.8
#define OPENRIDE_PI 3.14159265358979323846

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
}

static double deg_to_rad(double degrees)
{
    return degrees * OPENRIDE_PI / 180.0;
}

static double rad_to_deg(double radians)
{
    return radians * 180.0 / OPENRIDE_PI;
}

static bool route_valid(const OpenRideRoute *route)
{
    return route && route->geometry && route->geometry_count >= 2U;
}

static double bearing_deg(const OpenRideRoutePoint *a,
                          const OpenRideRoutePoint *b)
{
    const double lat1 = deg_to_rad(a->lat);
    const double lat2 = deg_to_rad(b->lat);
    const double dlon = deg_to_rad(b->lon - a->lon);
    const double y = sin(dlon) * cos(lat2);
    const double x = cos(lat1) * sin(lat2)
                   - sin(lat1) * cos(lat2) * cos(dlon);
    double heading = rad_to_deg(atan2(y, x));
    if (heading < 0.0) heading += 360.0;
    return heading;
}

static void offset_perpendicular(double lat,
                                 double lon,
                                 double heading_deg,
                                 double offset_m,
                                 double *out_lat,
                                 double *out_lon)
{
    if (fabs(offset_m) < 1e-9) {
        *out_lat = lat;
        *out_lon = lon;
        return;
    }

    const double perpendicular = deg_to_rad(heading_deg + 90.0);
    const double north_m = cos(perpendicular) * offset_m;
    const double east_m = sin(perpendicular) * offset_m;
    const double meters_per_degree_lat = OPENRIDE_EARTH_RADIUS_M * OPENRIDE_PI / 180.0;
    const double cos_lat = cos(deg_to_rad(lat));
    const double meters_per_degree_lon = meters_per_degree_lat
        * (fabs(cos_lat) > 1e-6 ? cos_lat : 1e-6);

    *out_lat = lat + north_m / meters_per_degree_lat;
    *out_lon = lon + east_m / meters_per_degree_lon;
}

void openride_gps_simulator_init(OpenRideGPSSimulator *simulator)
{
    if (!simulator) return;
    memset(simulator, 0, sizeof(*simulator));
}

void openride_gps_simulator_clear_route(OpenRideGPSSimulator *simulator)
{
    if (!simulator) return;
    free(simulator->cumulative_geometry_m);
    simulator->cumulative_geometry_m = NULL;
    simulator->route = NULL;
    simulator->geometry_count = 0U;
    simulator->geometry_distance_m = 0.0;
    simulator->position_m = 0.0;
    simulator->active = false;
    simulator->finished = false;
    simulator->lateral_offset_m = 0.0;
}

void openride_gps_simulator_destroy(OpenRideGPSSimulator *simulator)
{
    if (!simulator) return;
    openride_gps_simulator_clear_route(simulator);
    memset(simulator, 0, sizeof(*simulator));
}

bool openride_gps_simulator_set_route(OpenRideGPSSimulator *simulator,
                                      const OpenRideRoute *route,
                                      double speed_kph,
                                      char *error,
                                      size_t error_size)
{
    if (!simulator || !route_valid(route)) {
        set_error(error, error_size, "route geometry is required for GPS simulation");
        return false;
    }

    double *cumulative = calloc(route->geometry_count, sizeof(*cumulative));
    if (!cumulative) {
        set_error(error, error_size, "unable to allocate GPS simulator geometry");
        return false;
    }

    double total = 0.0;
    for (uint32_t i = 1U; i < route->geometry_count; ++i) {
        total += openride_geo_distance_m(route->geometry[i - 1U].lat,
                                         route->geometry[i - 1U].lon,
                                         route->geometry[i].lat,
                                         route->geometry[i].lon);
        cumulative[i] = total;
    }
    if (!(total > 0.0)) {
        free(cumulative);
        set_error(error, error_size, "route is too short for GPS simulation");
        return false;
    }

    openride_gps_simulator_clear_route(simulator);
    simulator->route = route;
    simulator->cumulative_geometry_m = cumulative;
    simulator->geometry_count = route->geometry_count;
    simulator->geometry_distance_m = total;
    openride_gps_simulator_set_speed_kph(simulator, speed_kph);
#ifdef __ANDROID__
    SDL_Log("AUDIT_ROUTE_SIM_READY route_distance_m=%.1f geometry_distance_m=%.1f geometry_points=%u speed_kph=%.1f",
            route->distance_m,
            total,
            route->geometry_count,
            simulator->speed_mps * 3.6);
#endif
    set_error(error, error_size, "");
    return true;
}

void openride_gps_simulator_start(OpenRideGPSSimulator *simulator)
{
    if (!simulator || !simulator->route) return;
    if (simulator->finished) {
        simulator->position_m = 0.0;
        simulator->finished = false;
    }
    simulator->active = true;
}

void openride_gps_simulator_stop(OpenRideGPSSimulator *simulator)
{
    if (!simulator) return;
    simulator->active = false;
}

void openride_gps_simulator_restart(OpenRideGPSSimulator *simulator)
{
    if (!simulator || !simulator->route) return;
    simulator->position_m = 0.0;
    simulator->finished = false;
    simulator->active = true;
}

bool openride_gps_simulator_toggle(OpenRideGPSSimulator *simulator)
{
    if (!simulator || !simulator->route) return false;
    if (simulator->active) {
        simulator->active = false;
    } else {
        openride_gps_simulator_start(simulator);
    }
    return simulator->active;
}

void openride_gps_simulator_set_speed_kph(OpenRideGPSSimulator *simulator,
                                          double speed_kph)
{
    if (!simulator) return;
    if (!isfinite(speed_kph)) speed_kph = 60.0;
    if (speed_kph < 1.0) speed_kph = 1.0;
    if (speed_kph > 160.0) speed_kph = 160.0;
    simulator->speed_mps = speed_kph / 3.6;
}

void openride_gps_simulator_set_lateral_offset_m(OpenRideGPSSimulator *simulator,
                                                 double offset_m)
{
    if (!simulator) return;
    if (!isfinite(offset_m)) offset_m = 0.0;
    simulator->lateral_offset_m = offset_m;
}

static uint32_t segment_for_position(const OpenRideGPSSimulator *simulator,
                                     double position_m)
{
    uint32_t low = 0U;
    uint32_t high = simulator->geometry_count - 1U;
    while (low + 1U < high) {
        const uint32_t middle = low + (high - low) / 2U;
        if (simulator->cumulative_geometry_m[middle] <= position_m) {
            low = middle;
        } else {
            high = middle;
        }
    }
    if (low >= simulator->geometry_count - 1U) {
        low = simulator->geometry_count - 2U;
    }
    return low;
}

bool openride_gps_simulator_sample(const OpenRideGPSSimulator *simulator,
                                   OpenRideGPSSample *sample)
{
    if (!simulator || !sample || !route_valid(simulator->route)
        || !simulator->cumulative_geometry_m) {
        return false;
    }

    double position = simulator->position_m;
    if (position < 0.0) position = 0.0;
    if (position > simulator->geometry_distance_m) {
        position = simulator->geometry_distance_m;
    }

    const uint32_t segment = segment_for_position(simulator, position);
    const double start_m = simulator->cumulative_geometry_m[segment];
    const double end_m = simulator->cumulative_geometry_m[segment + 1U];
    const double length_m = end_m - start_m;
    const double fraction = length_m > 1e-9
        ? (position - start_m) / length_m : 0.0;
    const OpenRideRoutePoint *a = &simulator->route->geometry[segment];
    const OpenRideRoutePoint *b = &simulator->route->geometry[segment + 1U];
    const double base_lat = a->lat + (b->lat - a->lat) * fraction;
    const double base_lon = a->lon + (b->lon - a->lon) * fraction;
    const double heading = bearing_deg(a, b);
    double lat = base_lat;
    double lon = base_lon;
    offset_perpendicular(base_lat,
                         base_lon,
                         heading,
                         simulator->lateral_offset_m,
                         &lat,
                         &lon);

    memset(sample, 0, sizeof(*sample));
    sample->valid = true;
    sample->finished = simulator->finished;
    sample->lat = lat;
    sample->lon = lon;
    sample->heading_deg = heading;
    sample->speed_mps = simulator->active ? simulator->speed_mps : 0.0;
    sample->route_position_m = position;
    return true;
}

bool openride_gps_simulator_update(OpenRideGPSSimulator *simulator,
                                   double delta_seconds,
                                   OpenRideGPSSample *sample)
{
    if (!simulator || !simulator->route) return false;
    if (!isfinite(delta_seconds) || delta_seconds < 0.0) delta_seconds = 0.0;
    if (delta_seconds > 1.0) delta_seconds = 1.0;

    if (simulator->active && !simulator->finished) {
        simulator->position_m += simulator->speed_mps * delta_seconds;
        if (simulator->position_m >= simulator->geometry_distance_m) {
            simulator->position_m = simulator->geometry_distance_m;
            simulator->finished = true;
            simulator->active = false;
        }
    }
    return openride_gps_simulator_sample(simulator, sample);
}
