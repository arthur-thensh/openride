#include "openride/drive_mode.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#ifdef __ANDROID__
/* The Android application already links SDL3. Keep openride_core header-only
 * independent from SDL while using its existing Android log/timer symbols for
 * audit telemetry. */
extern uint64_t SDL_GetTicksNS(void);
extern void SDL_Log(const char *fmt, ...);
#endif

#define OPENRIDE_PI 3.14159265358979323846
#define OPENRIDE_EARTH_RADIUS_M 6371008.8
#define OPENRIDE_TILE_SIZE 256.0
#define OPENRIDE_MERCATOR_MIN_LAT (-85.05112878)
#define OPENRIDE_MERCATOR_MAX_LAT 85.05112878
#define OPENRIDE_DRIVE_RIDER_ANCHOR_X 0.50
#define OPENRIDE_DRIVE_RIDER_ANCHOR_Y 0.70
#define OPENRIDE_DRIVE_RIDER_Y_TOLERANCE 0.02

/*
 * Render dimensions are learned from the production map projection. Drive is
 * deliberately corrected one update later so camera geometry remains owned by
 * the real map camera instead of a renderer-specific screen translation.
 */
static int openride_drive_viewport_width = 0;
static int openride_drive_viewport_height = 0;
static double openride_drive_render_zoom = 0.0;
static bool openride_drive_session_active = false;
static bool openride_drive_render_view_ready = false;

#ifdef __ANDROID__
static uint64_t openride_drive_audit_last_log_ns = 0U;

static void openride_drive_audit_log_state(const OpenRideDriveModeState *state,
                                           double speed_mps,
                                           double heading_deg)
{
    if (!state || !state->active || !state->initialized) return;

    const uint64_t now_ns = SDL_GetTicksNS();
    if (openride_drive_audit_last_log_ns != 0U
        && now_ns - openride_drive_audit_last_log_ns < UINT64_C(1000000000)) {
        return;
    }
    openride_drive_audit_last_log_ns = now_ns;

    SDL_Log("AUDIT_DRIVE_STATE speed_kph=%.1f filtered_speed_kph=%.1f gps_heading=%.1f filtered_heading=%.1f camera_heading=%.1f target_heading=%.1f camera_zoom=%.3f target_zoom=%.3f lookahead_m=%.1f camera_lat=%.7f camera_lon=%.7f target_lat=%.7f target_lon=%.7f gps_quality=%d auto_zoom=%d heading_up=%d",
            fmax(0.0, speed_mps) * 3.6,
            state->smoothed_speed_mps * 3.6,
            heading_deg,
            state->smoothed_heading_deg,
            state->camera_bearing_deg,
            state->target_camera_bearing_deg,
            state->camera_zoom,
            state->target_camera_zoom,
            state->lookahead_distance_m,
            state->camera_lat,
            state->camera_lon,
            state->target_camera_lat,
            state->target_camera_lon,
            (int)state->gps_quality,
            state->auto_zoom ? 1 : 0,
            state->heading_up ? 1 : 0);

    if (state->framing_active) {
        SDL_Log("AUDIT_DRIVE_VIEW rider_x_pct=%.4f rider_y_pct=%.4f raw_rider_x_pct=%.4f raw_rider_y_pct=%.4f anchor_x_pct=%.4f anchor_y_pct=%.4f correction_x_pct=%.4f correction_y_pct=%.4f render_zoom=%.3f viewport=%dx%d",
                state->rider_screen_x_ratio,
                state->rider_screen_y_ratio,
                state->rider_raw_x_ratio,
                state->rider_raw_y_ratio,
                OPENRIDE_DRIVE_RIDER_ANCHOR_X,
                OPENRIDE_DRIVE_RIDER_ANCHOR_Y,
                state->framing_correction_x_ratio,
                state->framing_correction_y_ratio,
                openride_drive_render_zoom,
                openride_drive_viewport_width,
                openride_drive_viewport_height);
    }
}
#endif

static double clampd(double value, double min_value, double max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static double wrap01(double value)
{
    value = fmod(value, 1.0);
    if (value < 0.0) value += 1.0;
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

static double smoothing_alpha(double delta_seconds, double response_per_second)
{
    if (delta_seconds <= 0.0 || response_per_second <= 0.0) return 0.0;
    return 1.0 - exp(-delta_seconds * response_per_second);
}

static void rotate_screen_forward(double bearing_deg, double *x, double *y)
{
    if (!x || !y || fabs(bearing_deg) < 1e-12) return;
    const double angle = bearing_deg * OPENRIDE_PI / 180.0;
    const double c = cos(angle);
    const double s = sin(angle);
    const double ox = *x;
    const double oy = *y;
    *x = ox * c + oy * s;
    *y = -ox * s + oy * c;
}

static void rotate_screen_inverse(double bearing_deg, double *x, double *y)
{
    if (!x || !y || fabs(bearing_deg) < 1e-12) return;
    const double angle = bearing_deg * OPENRIDE_PI / 180.0;
    const double c = cos(angle);
    const double s = sin(angle);
    const double ox = *x;
    const double oy = *y;
    *x = ox * c - oy * s;
    *y = ox * s + oy * c;
}

static void mercator_forward(double lat_deg,
                             double lon_deg,
                             double *out_x,
                             double *out_y)
{
    if (!out_x || !out_y) return;
    const double clamped_lat = clampd(lat_deg,
                                      OPENRIDE_MERCATOR_MIN_LAT,
                                      OPENRIDE_MERCATOR_MAX_LAT);
    const double lat = clamped_lat * OPENRIDE_PI / 180.0;
    *out_x = wrap01((lon_deg + 180.0) / 360.0);
    *out_y = clampd((1.0 - asinh(tan(lat)) / OPENRIDE_PI) * 0.5,
                    0.0,
                    1.0);
}

static void mercator_inverse(double x,
                             double y,
                             double *out_lat,
                             double *out_lon)
{
    x = wrap01(x);
    y = clampd(y, 0.0, 1.0);
    if (out_lon) *out_lon = x * 360.0 - 180.0;
    if (out_lat) {
        const double n = OPENRIDE_PI * (1.0 - 2.0 * y);
        *out_lat = atan(sinh(n)) * 180.0 / OPENRIDE_PI;
    }
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

void openride_drive_mode_note_render_view(int viewport_width,
                                          int viewport_height,
                                          double render_zoom)
{
    if (viewport_width <= 0 || viewport_height <= 0 || !isfinite(render_zoom)) {
        return;
    }

    openride_drive_viewport_width = viewport_width;
    openride_drive_viewport_height = viewport_height;
    if (openride_drive_session_active) {
        openride_drive_render_zoom = render_zoom;
        openride_drive_render_view_ready = true;
    }
}

static void openride_drive_mode_apply_screen_framing(
    OpenRideDriveModeState *state,
    double rider_lat,
    double rider_lon)
{
    if (!state) return;

    state->framing_active = false;
    state->rider_raw_x_ratio = 0.5;
    state->rider_raw_y_ratio = 0.5;
    state->rider_screen_x_ratio = 0.5;
    state->rider_screen_y_ratio = 0.5;
    state->framing_correction_x_ratio = 0.0;
    state->framing_correction_y_ratio = 0.0;

    if (!state->heading_up
        || !openride_drive_render_view_ready
        || openride_drive_viewport_width <= 0
        || openride_drive_viewport_height <= 0
        || !isfinite(openride_drive_render_zoom)
        || !isfinite(state->camera_lat)
        || !isfinite(state->camera_lon)
        || !isfinite(rider_lat)
        || !isfinite(rider_lon)) {
        return;
    }

    const double world_size = OPENRIDE_TILE_SIZE
        * pow(2.0, openride_drive_render_zoom);
    if (!isfinite(world_size) || world_size <= 0.0) return;

    double center_x = 0.0;
    double center_y = 0.0;
    double rider_x = 0.0;
    double rider_y = 0.0;
    mercator_forward(state->camera_lat,
                      state->camera_lon,
                      &center_x,
                      &center_y);
    mercator_forward(rider_lat,
                      rider_lon,
                      &rider_x,
                      &rider_y);

    double dx = rider_x - center_x;
    if (dx > 0.5) dx -= 1.0;
    if (dx < -0.5) dx += 1.0;
    double screen_dx = dx * world_size;
    double screen_dy = (rider_y - center_y) * world_size;
    rotate_screen_forward(state->camera_bearing_deg,
                          &screen_dx,
                          &screen_dy);

    const double width = (double)openride_drive_viewport_width;
    const double height = (double)openride_drive_viewport_height;
    const double raw_screen_x = width * 0.5 + screen_dx;
    const double raw_screen_y = height * 0.5 + screen_dy;
    const double desired_screen_x = width * OPENRIDE_DRIVE_RIDER_ANCHOR_X;
    const double desired_screen_y = clampd(
        raw_screen_y,
        height * (OPENRIDE_DRIVE_RIDER_ANCHOR_Y
                  - OPENRIDE_DRIVE_RIDER_Y_TOLERANCE),
        height * (OPENRIDE_DRIVE_RIDER_ANCHOR_Y
                  + OPENRIDE_DRIVE_RIDER_Y_TOLERANCE));

    double drag_x = desired_screen_x - raw_screen_x;
    double drag_y = desired_screen_y - raw_screen_y;

    state->framing_active = true;
    state->rider_raw_x_ratio = raw_screen_x / width;
    state->rider_raw_y_ratio = raw_screen_y / height;
    state->rider_screen_x_ratio = desired_screen_x / width;
    state->rider_screen_y_ratio = desired_screen_y / height;
    state->framing_correction_x_ratio = drag_x / width;
    state->framing_correction_y_ratio = drag_y / height;

    if (fabs(drag_x) < 1e-9 && fabs(drag_y) < 1e-9) return;

    /* Same semantics as openride_camera_pan(): dragging the rendered map by
     * the correction vector moves the geographic center in the opposite
     * Mercator direction. Applying it here means every renderer sees the same
     * corrected camera center. */
    rotate_screen_inverse(state->camera_bearing_deg, &drag_x, &drag_y);
    center_x -= drag_x / world_size;
    center_y -= drag_y / world_size;
    center_x = wrap01(center_x);
    center_y = clampd(center_y, 0.0, 1.0);
    mercator_inverse(center_x,
                     center_y,
                     &state->camera_lat,
                     &state->camera_lon);
}

void openride_drive_mode_init(OpenRideDriveModeState *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->heading_up = true;
    state->auto_zoom = true;
    state->camera_zoom = 16.0;
    state->target_camera_zoom = 16.0;
    state->gps_quality = OPENRIDE_GPS_UNAVAILABLE;
    openride_drive_session_active = false;
    openride_drive_render_view_ready = false;
    openride_drive_viewport_width = 0;
    openride_drive_viewport_height = 0;
    openride_drive_render_zoom = 0.0;
}

void openride_drive_mode_set_active(OpenRideDriveModeState *state, bool active)
{
    if (!state) return;
#ifdef __ANDROID__
    if (state->active != active) {
        SDL_Log("AUDIT_DRIVE_MODE_ACTIVE active=%d heading_up=%d auto_zoom=%d",
                active ? 1 : 0,
                state->heading_up ? 1 : 0,
                state->auto_zoom ? 1 : 0);
    }
    if (!active) openride_drive_audit_last_log_ns = 0U;
#endif
    if (state->active != active) {
        openride_drive_session_active = active;
        if (active) {
            /* Wait for one actual Drive render before applying pixel framing;
             * the pre-Drive map may have used a very different zoom. */
            openride_drive_render_view_ready = false;
        }
    }
    state->active = active;
    if (!active) {
        state->initialized = false;
        state->framing_active = false;
    }
}

void openride_drive_mode_set_heading_up(OpenRideDriveModeState *state, bool heading_up)
{
    if (!state) return;
    state->heading_up = heading_up;
    if (!heading_up) {
        state->camera_bearing_deg = 0.0;
        state->target_camera_bearing_deg = 0.0;
        state->framing_active = false;
    }
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
     * Phone navigation should prioritize the road immediately around the
     * rider. Forward context is provided by the independent look-ahead, so
     * keep the map scale deliberately close even at normal road speeds.
     */
    double zoom = 18.9;

    if (speed_kph >= 110.0) zoom = 16.8;
    else if (speed_kph >= 90.0) zoom = 17.2;
    else if (speed_kph >= 70.0) zoom = 17.6;
    else if (speed_kph >= 50.0) zoom = 18.0;
    else if (speed_kph >= 30.0) zoom = 18.3;
    else if (speed_kph >= 15.0) zoom = 18.6;

    if (isfinite(maneuver_distance_m)) {
        if (maneuver_distance_m < 90.0) zoom += 0.6;
        else if (maneuver_distance_m < 250.0) zoom += 0.4;
        else if (maneuver_distance_m < 500.0) zoom += 0.2;
    }

    return clampd(zoom, 16.5, 19.0);
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

    state->framing_active = false;
    state->gps_age_s = sample_age_s;
    state->gps_accuracy_m = accuracy_m;
    state->gps_quality = openride_drive_mode_gps_quality(gps_active,
                                                          has_sample,
                                                          sample_age_s,
                                                          accuracy_m);
    if (!state->active || !has_sample || state->gps_quality == OPENRIDE_GPS_LOST) return;
    if (!isfinite(lat) || !isfinite(lon)) return;

    if (delta_seconds < 0.0) delta_seconds = 0.0;
    if (delta_seconds > 0.25) delta_seconds = 0.25;

    const double measured_speed = isfinite(speed_mps) ? fmax(0.0, speed_mps) : 0.0;
    const double measured_heading = isfinite(heading_deg)
        ? normalize_bearing(heading_deg)
        : (state->initialized ? state->smoothed_heading_deg : 0.0);

    if (!state->initialized) {
        state->smoothed_speed_mps = measured_speed;
        state->smoothed_heading_deg = measured_heading;
    } else {
        const double speed_input_alpha = smoothing_alpha(delta_seconds, 2.4);
        state->smoothed_speed_mps +=
            (measured_speed - state->smoothed_speed_mps) * speed_input_alpha;

        /*
         * GPS course can jump by several degrees from sample to sample. Filter
         * the course before it becomes a camera target, but keep the response
         * short enough for real motorcycle turns. At walking speed, preserve
         * the last trustworthy direction instead of rotating on GPS noise.
         */
        if (measured_speed >= 1.5) {
            const double heading_input_alpha = smoothing_alpha(delta_seconds, 6.0);
            state->smoothed_heading_deg = normalize_bearing(
                state->smoothed_heading_deg
                + shortest_angle_delta(state->smoothed_heading_deg,
                                       measured_heading) * heading_input_alpha);
        }
    }

    const double travel_bearing = state->smoothed_heading_deg;
    const double target_bearing = state->heading_up ? travel_bearing : 0.0;
    const double lookahead =
        openride_drive_mode_target_lookahead_m(state->smoothed_speed_mps,
                                               maneuver_distance_m);

    double target_lat = lat;
    double target_lon = lon;
    /*
     * The forward camera target follows the direction of travel even when the
     * map itself is north-up. Using target_bearing here would incorrectly move
     * a north-up camera toward geographic north instead of ahead of the rider.
     */
    project_ahead(lat,
                  lon,
                  travel_bearing,
                  lookahead,
                  &target_lat,
                  &target_lon);

    const double target_zoom = openride_drive_mode_target_zoom(
        state->smoothed_speed_mps,
        maneuver_distance_m);

    state->target_camera_lat = target_lat;
    state->target_camera_lon = target_lon;
    state->target_camera_zoom = target_zoom;
    state->target_camera_bearing_deg = target_bearing;
    state->lookahead_distance_m = lookahead;

    if (!state->initialized) {
        state->camera_lat = target_lat;
        state->camera_lon = target_lon;
        state->camera_zoom = target_zoom;
        state->camera_bearing_deg = target_bearing;
        state->initialized = true;
        openride_drive_mode_apply_screen_framing(state, lat, lon);
#ifdef __ANDROID__
        openride_drive_audit_log_state(state, measured_speed, measured_heading);
#endif
        return;
    }

    const double position_alpha = smoothing_alpha(delta_seconds, 4.5);
    const double zoom_alpha = smoothing_alpha(delta_seconds, 3.0);
    const double bearing_alpha = smoothing_alpha(delta_seconds, 5.0);

    state->camera_lat +=
        (state->target_camera_lat - state->camera_lat) * position_alpha;
    state->camera_lon +=
        (state->target_camera_lon - state->camera_lon) * position_alpha;
    if (state->auto_zoom) {
        state->camera_zoom +=
            (state->target_camera_zoom - state->camera_zoom) * zoom_alpha;
    }
    state->camera_bearing_deg = normalize_bearing(
        state->camera_bearing_deg
        + shortest_angle_delta(state->camera_bearing_deg,
                               state->target_camera_bearing_deg) * bearing_alpha);

    openride_drive_mode_apply_screen_framing(state, lat, lon);
#ifdef __ANDROID__
    openride_drive_audit_log_state(state, measured_speed, measured_heading);
#endif
}
