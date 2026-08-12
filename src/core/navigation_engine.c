#include "openride/navigation_engine.h"
#include "openride/map_selection.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENRIDE_EARTH_RADIUS_M 6371008.8
#define OPENRIDE_DEG_TO_RAD 0.017453292519943295769236907684886

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
}

static double clampd(double value, double minimum, double maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static bool route_has_geometry(const OpenRideRoute *route)
{
    return route && route->geometry && route->geometry_count >= 2U;
}

static bool build_cumulative_geometry(const OpenRideRoute *route,
                                      double **cumulative,
                                      double *total_m)
{
    if (!route_has_geometry(route) || !cumulative || !total_m) return false;

    double *values = calloc(route->geometry_count, sizeof(*values));
    if (!values) return false;

    double total = 0.0;
    values[0] = 0.0;
    for (uint32_t i = 1U; i < route->geometry_count; ++i) {
        total += openride_geo_distance_m(route->geometry[i - 1U].lat,
                                         route->geometry[i - 1U].lon,
                                         route->geometry[i].lat,
                                         route->geometry[i].lon);
        values[i] = total;
    }

    if (!(total > 0.0) || !isfinite(total)) {
        free(values);
        return false;
    }

    *cumulative = values;
    *total_m = total;
    return true;
}

typedef struct RouteProjection {
    uint32_t segment_index;
    double fraction;
    double lat;
    double lon;
    double distance_m;
} RouteProjection;

static RouteProjection project_on_segment(const OpenRideRoutePoint *a,
                                          const OpenRideRoutePoint *b,
                                          double lat,
                                          double lon,
                                          uint32_t segment_index)
{
    RouteProjection projection;
    memset(&projection, 0, sizeof(projection));
    projection.segment_index = segment_index;

    const double latitude_reference = (lat + a->lat + b->lat) / 3.0;
    const double cos_lat = cos(latitude_reference * OPENRIDE_DEG_TO_RAD);
    const double meters_per_degree_lat = OPENRIDE_EARTH_RADIUS_M * OPENRIDE_DEG_TO_RAD;
    const double meters_per_degree_lon = meters_per_degree_lat * cos_lat;

    const double ax = (a->lon - lon) * meters_per_degree_lon;
    const double ay = (a->lat - lat) * meters_per_degree_lat;
    const double bx = (b->lon - lon) * meters_per_degree_lon;
    const double by = (b->lat - lat) * meters_per_degree_lat;
    const double dx = bx - ax;
    const double dy = by - ay;
    const double length_sq = dx * dx + dy * dy;

    double fraction = 0.0;
    if (length_sq > 1e-9) {
        fraction = clampd((-(ax * dx + ay * dy)) / length_sq, 0.0, 1.0);
    }

    const double px = ax + dx * fraction;
    const double py = ay + dy * fraction;
    projection.fraction = fraction;
    projection.lat = a->lat + (b->lat - a->lat) * fraction;
    projection.lon = a->lon + (b->lon - a->lon) * fraction;
    projection.distance_m = sqrt(px * px + py * py);
    return projection;
}

static RouteProjection nearest_projection_range(const OpenRideRoute *route,
                                                double lat,
                                                double lon,
                                                uint32_t first_segment,
                                                uint32_t last_segment)
{
    RouteProjection best;
    memset(&best, 0, sizeof(best));
    best.distance_m = INFINITY;

    if (!route_has_geometry(route)) return best;
    const uint32_t segment_count = route->geometry_count - 1U;
    if (segment_count == 0U) return best;
    if (first_segment >= segment_count) first_segment = segment_count - 1U;
    if (last_segment >= segment_count) last_segment = segment_count - 1U;
    if (last_segment < first_segment) return best;

    for (uint32_t i = first_segment; i <= last_segment; ++i) {
        const RouteProjection candidate = project_on_segment(&route->geometry[i],
                                                             &route->geometry[i + 1U],
                                                             lat,
                                                             lon,
                                                             i);
        if (candidate.distance_m < best.distance_m) best = candidate;
    }
    return best;
}

static RouteProjection nearest_projection(OpenRideNavigationEngine *navigation,
                                          double lat,
                                          double lon)
{
    const uint32_t segment_count = navigation->geometry_count - 1U;
    if (!navigation->has_last_segment
        || navigation->config.local_search_radius_segments == 0U
        || segment_count <= navigation->config.local_search_radius_segments * 2U + 1U) {
        return nearest_projection_range(navigation->route,
                                        lat,
                                        lon,
                                        0U,
                                        segment_count - 1U);
    }

    const uint32_t radius = navigation->config.local_search_radius_segments;
    const uint32_t first = navigation->last_segment_index > radius
        ? navigation->last_segment_index - radius : 0U;
    uint32_t last = navigation->last_segment_index + radius;
    if (last >= segment_count) last = segment_count - 1U;

    RouteProjection best = nearest_projection_range(navigation->route,
                                                    lat,
                                                    lon,
                                                    first,
                                                    last);

    /* A large local miss can mean a GPS jump or a deliberate route deviation. */
    if (best.distance_m > navigation->config.off_route_threshold_m * 2.0) {
        best = nearest_projection_range(navigation->route,
                                        lat,
                                        lon,
                                        0U,
                                        segment_count - 1U);
    }
    return best;
}

OpenRideNavigationConfig openride_navigation_config_default(void)
{
    OpenRideNavigationConfig config;
    config.off_route_threshold_m = 45.0;
    config.return_to_route_threshold_m = 25.0;
    config.arrival_threshold_m = 25.0;
    config.local_search_radius_segments = 80U;
    return config;
}

void openride_navigation_engine_init(OpenRideNavigationEngine *navigation)
{
    if (!navigation) return;
    memset(navigation, 0, sizeof(*navigation));
    navigation->config = openride_navigation_config_default();
}

void openride_navigation_engine_clear_route(OpenRideNavigationEngine *navigation)
{
    if (!navigation) return;
    free(navigation->cumulative_geometry_m);
    navigation->cumulative_geometry_m = NULL;
    navigation->route = NULL;
    navigation->geometry_count = 0U;
    navigation->geometry_distance_m = 0.0;
    navigation->route_distance_m = 0.0;
    navigation->last_segment_index = 0U;
    navigation->has_last_segment = false;
    navigation->currently_off_route = false;
}

void openride_navigation_engine_destroy(OpenRideNavigationEngine *navigation)
{
    if (!navigation) return;
    openride_navigation_engine_clear_route(navigation);
    memset(navigation, 0, sizeof(*navigation));
}

bool openride_navigation_engine_set_route(OpenRideNavigationEngine *navigation,
                                          const OpenRideRoute *route,
                                          char *error,
                                          size_t error_size)
{
    if (!navigation || !route_has_geometry(route)) {
        set_error(error, error_size, "route geometry is required for navigation");
        return false;
    }

    double *cumulative = NULL;
    double total = 0.0;
    if (!build_cumulative_geometry(route, &cumulative, &total)) {
        set_error(error, error_size, "unable to prepare route geometry for navigation");
        return false;
    }

    openride_navigation_engine_clear_route(navigation);
    navigation->route = route;
    navigation->cumulative_geometry_m = cumulative;
    navigation->geometry_count = route->geometry_count;
    navigation->geometry_distance_m = total;
    navigation->route_distance_m = route->distance_m > 0.0 ? route->distance_m : total;
    navigation->config = openride_navigation_config_default();
    set_error(error, error_size, "");
    return true;
}

bool openride_navigation_engine_update(OpenRideNavigationEngine *navigation,
                                       double lat,
                                       double lon,
                                       double speed_mps,
                                       double heading_deg,
                                       OpenRideNavigationState *state)
{
    if (!navigation || !state || !route_has_geometry(navigation->route)
        || !navigation->cumulative_geometry_m) {
        return false;
    }

    const RouteProjection projection = nearest_projection(navigation, lat, lon);
    if (!isfinite(projection.distance_m)) return false;

    navigation->last_segment_index = projection.segment_index;
    navigation->has_last_segment = true;

    const double segment_start_m = navigation->cumulative_geometry_m[projection.segment_index];
    const double segment_end_m = navigation->cumulative_geometry_m[projection.segment_index + 1U];
    const double geometry_progress_m = segment_start_m
        + (segment_end_m - segment_start_m) * projection.fraction;
    const double progress_ratio = clampd(
        geometry_progress_m / navigation->geometry_distance_m,
        0.0,
        1.0);
    const double traveled_m = progress_ratio * navigation->route_distance_m;
    const double remaining_m = navigation->route_distance_m - traveled_m;

    if (navigation->currently_off_route) {
        navigation->currently_off_route =
            projection.distance_m > navigation->config.return_to_route_threshold_m;
    } else {
        navigation->currently_off_route =
            projection.distance_m > navigation->config.off_route_threshold_m;
    }

    OpenRideNavigationStatus status = navigation->currently_off_route
        ? OPENRIDE_NAVIGATION_OFF_ROUTE
        : OPENRIDE_NAVIGATION_ON_ROUTE;

    if (!navigation->currently_off_route
        && remaining_m <= navigation->config.arrival_threshold_m) {
        status = OPENRIDE_NAVIGATION_ARRIVED;
    }

    memset(state, 0, sizeof(*state));
    state->valid = true;
    state->status = status;
    state->gps_lat = lat;
    state->gps_lon = lon;
    state->matched_lat = projection.lat;
    state->matched_lon = projection.lon;
    state->distance_from_route_m = projection.distance_m;
    state->traveled_m = traveled_m;
    state->remaining_m = remaining_m > 0.0 ? remaining_m : 0.0;
    state->progress_ratio = progress_ratio;
    state->speed_mps = speed_mps > 0.0 ? speed_mps : 0.0;
    state->heading_deg = heading_deg;
    state->route_segment_index = projection.segment_index;
    state->route_segment_fraction = projection.fraction;
    return true;
}

const char *openride_navigation_status_name(OpenRideNavigationStatus status)
{
    switch (status) {
        case OPENRIDE_NAVIGATION_ON_ROUTE:  return "sur itineraire";
        case OPENRIDE_NAVIGATION_OFF_ROUTE: return "hors itineraire";
        case OPENRIDE_NAVIGATION_ARRIVED:   return "arrive";
        case OPENRIDE_NAVIGATION_INACTIVE:
        default:                            return "inactif";
    }
}
