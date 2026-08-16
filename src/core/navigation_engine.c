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

/*
 * Closed-loop navigation bootstrap
 * --------------------------------
 * A loop starts and ends at (almost) the same geographic position. On the
 * first GPS sample, a pure nearest-segment search can therefore select the
 * final segment and report 100% progress immediately.
 *
 * While navigation is still close to the beginning of a closed route, prefer
 * a plausible projection in the first part of the geometry. Once progress has
 * moved beyond that initial window, normal map matching takes over and the
 * final segment can be selected normally when the rider actually returns.
 */
static bool route_is_closed_loop(const OpenRideNavigationEngine *navigation)
{
    if (!navigation || !route_has_geometry(navigation->route)) return false;

    const OpenRideRoutePoint *first = &navigation->route->geometry[0];
    const OpenRideRoutePoint *last =
        &navigation->route->geometry[navigation->geometry_count - 1U];

    const double endpoint_distance_m =
        openride_geo_distance_m(first->lat, first->lon, last->lat, last->lon);
    const double closed_threshold_m =
        fmax(100.0, navigation->config.arrival_threshold_m * 4.0);

    return endpoint_distance_m <= closed_threshold_m;
}

static double loop_start_window_m(const OpenRideNavigationEngine *navigation)
{
    if (!navigation || navigation->geometry_distance_m <= 0.0) return 0.0;

    return clampd(navigation->geometry_distance_m * 0.05, 250.0, 1000.0);
}

static RouteProjection nearest_projection_loop_start(
    const OpenRideNavigationEngine *navigation,
    double lat,
    double lon,
    double window_m)
{
    RouteProjection best;
    memset(&best, 0, sizeof(best));
    best.distance_m = INFINITY;

    if (!navigation || !route_has_geometry(navigation->route)
        || !navigation->cumulative_geometry_m
        || navigation->geometry_count < 2U) {
        return best;
    }

    const uint32_t segment_count = navigation->geometry_count - 1U;
    uint32_t last_segment = 0U;

    while (last_segment + 1U < segment_count
           && navigation->cumulative_geometry_m[last_segment + 1U]
                  <= window_m) {
        ++last_segment;
    }

    return nearest_projection_range(navigation->route,
                                    lat,
                                    lon,
                                    0U,
                                    last_segment);
}

static bool loop_start_bias_active(const OpenRideNavigationEngine *navigation,
                                   double window_m)
{
    if (!navigation || !route_is_closed_loop(navigation)) return false;
    if (!navigation->has_last_segment) return true;
    if (!navigation->cumulative_geometry_m
        || navigation->last_segment_index >= navigation->geometry_count) {
        return false;
    }

    return navigation->cumulative_geometry_m[navigation->last_segment_index]
        <= window_m;
}

static uint32_t segment_index_distance(uint32_t a, uint32_t b)
{
    return a >= b ? a - b : b - a;
}

static double normalize_heading_deg(double heading_deg)
{
    if (!isfinite(heading_deg)) return 0.0;
    double normalized = fmod(heading_deg, 360.0);
    if (normalized < 0.0) normalized += 360.0;
    return normalized;
}

static double heading_difference_deg(double a_deg, double b_deg)
{
    double delta = fabs(normalize_heading_deg(a_deg)
                        - normalize_heading_deg(b_deg));
    if (delta > 180.0) delta = 360.0 - delta;
    return delta;
}

static double route_segment_heading_deg(const OpenRideRoutePoint *a,
                                        const OpenRideRoutePoint *b)
{
    if (!a || !b) return 0.0;

    const double lat1 = a->lat * OPENRIDE_DEG_TO_RAD;
    const double lat2 = b->lat * OPENRIDE_DEG_TO_RAD;
    const double dlon = (b->lon - a->lon) * OPENRIDE_DEG_TO_RAD;

    const double y = sin(dlon) * cos(lat2);
    const double x = cos(lat1) * sin(lat2)
                   - sin(lat1) * cos(lat2) * cos(dlon);

    return normalize_heading_deg(atan2(y, x) / OPENRIDE_DEG_TO_RAD);
}

/*
 * Self-crossing arbitration
 * -------------------------
 * GPS distance remains the primary signal.
 * Heading and segment continuity are only used when several nearby route
 * segments are genuinely plausible.
 */
static RouteProjection nearest_projection_range_continuous(
    const OpenRideRoute *route,
    double lat,
    double lon,
    uint32_t first_segment,
    uint32_t last_segment,
    uint32_t preferred_segment,
    double speed_mps,
    double heading_deg,
    double ambiguity_m)
{
    RouteProjection geometric = nearest_projection_range(route,
                                                         lat,
                                                         lon,
                                                         first_segment,
                                                         last_segment);
    if (!isfinite(geometric.distance_m) || !route_has_geometry(route)) {
        return geometric;
    }

    const uint32_t segment_count = route->geometry_count - 1U;
    if (segment_count == 0U) return geometric;
    if (first_segment >= segment_count) first_segment = segment_count - 1U;
    if (last_segment >= segment_count) last_segment = segment_count - 1U;
    if (last_segment < first_segment) return geometric;
    if (preferred_segment >= segment_count) preferred_segment = segment_count - 1U;

    const bool heading_reliable =
        isfinite(heading_deg) && isfinite(speed_mps) && speed_mps >= 2.0;

    const double geometric_heading =
        route_segment_heading_deg(&route->geometry[geometric.segment_index],
                                  &route->geometry[geometric.segment_index + 1U]);
    const double geometric_heading_error =
        heading_reliable
            ? heading_difference_deg(heading_deg, geometric_heading)
            : 0.0;

    /*
     * A near-perfect geometric match whose direction agrees with movement is
     * not ambiguous. Keep it immediately.
     */
    if (geometric.distance_m <= 2.0
        && (!heading_reliable || geometric_heading_error <= 35.0)) {
        return geometric;
    }

    const double effective_ambiguity_m =
        heading_reliable ? ambiguity_m : fmin(8.0, ambiguity_m);
    const double allowed_distance_m =
        geometric.distance_m + fmax(0.0, effective_ambiguity_m);

    RouteProjection best = geometric;
    double best_score = INFINITY;

    for (uint32_t i = first_segment; i <= last_segment; ++i) {
        const RouteProjection candidate =
            project_on_segment(&route->geometry[i],
                               &route->geometry[i + 1U],
                               lat,
                               lon,
                               i);
        if (!isfinite(candidate.distance_m)
            || candidate.distance_m > allowed_distance_m) {
            continue;
        }

        const uint32_t index_delta =
            segment_index_distance(i, preferred_segment);

        double score = candidate.distance_m;

        if (heading_reliable) {
            const double segment_heading =
                route_segment_heading_deg(&route->geometry[i],
                                          &route->geometry[i + 1U]);
            const double heading_error =
                heading_difference_deg(heading_deg, segment_heading);
            score += heading_error * 0.60;
        }

        /* Continuity is deliberately bounded. */
        score += fmin((double)index_delta * 1.5, 12.0);

        if (i < preferred_segment) {
            score += 2.0;
        }

        if (score < best_score) {
            best = candidate;
            best_score = score;
        }
    }

    return best;
}

static RouteProjection nearest_projection(OpenRideNavigationEngine *navigation,
                                          double lat,
                                          double lon,
                                          double speed_mps,
                                          double heading_deg)
{
    const uint32_t segment_count = navigation->geometry_count - 1U;

    if (!navigation->has_last_segment
        || navigation->config.local_search_radius_segments == 0U) {
        return nearest_projection_range(navigation->route,
                                        lat,
                                        lon,
                                        0U,
                                        segment_count - 1U);
    }

    const double ambiguity_m =
        clampd(navigation->config.off_route_threshold_m * 0.75,
               12.0,
               35.0);

    const uint32_t radius =
        navigation->config.local_search_radius_segments;

    if (segment_count <= radius * 2U + 1U) {
        return nearest_projection_range_continuous(
            navigation->route,
            lat,
            lon,
            0U,
            segment_count - 1U,
            navigation->last_segment_index,
            speed_mps,
            heading_deg,
            ambiguity_m);
    }

    const uint32_t first =
        navigation->last_segment_index > radius
            ? navigation->last_segment_index - radius
            : 0U;

    uint32_t last = navigation->last_segment_index + radius;
    if (last >= segment_count) {
        last = segment_count - 1U;
    }

    RouteProjection best = nearest_projection_range_continuous(
        navigation->route,
        lat,
        lon,
        first,
        last,
        navigation->last_segment_index,
        speed_mps,
        heading_deg,
        ambiguity_m);

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

    RouteProjection projection = nearest_projection(navigation,
                                                          lat,
                                                          lon,
                                                          speed_mps,
                                                          heading_deg);
    if (!isfinite(projection.distance_m)) return false;

    /*
     * At the beginning of a closed loop the last segment may be closer than
     * the first one because of GPS noise. Do not let that ambiguity initialize
     * the trip at ~100% progress.
     */
    const double start_window_m = loop_start_window_m(navigation);
    if (loop_start_bias_active(navigation, start_window_m)) {
        const RouteProjection start_projection =
            nearest_projection_loop_start(navigation,
                                          lat,
                                          lon,
                                          start_window_m);
        const double capture_distance_m =
            fmax(150.0, navigation->config.off_route_threshold_m * 3.0);

        if (isfinite(start_projection.distance_m)
            && start_projection.distance_m <= capture_distance_m) {
            projection = start_projection;
        }
    }

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
