#include "openride/map_camera.h"
#include "openride/drive_mode.h"

#include <math.h>
#include <stdint.h>

#define OPENRIDE_PI 3.14159265358979323846
#define OPENRIDE_TILE_SIZE 256.0
#define OPENRIDE_MIN_LAT (-85.05112878)
#define OPENRIDE_MAX_LAT (85.05112878)
#define OPENRIDE_MIN_ZOOM 1.0
#define OPENRIDE_MAX_ZOOM 20.0
#define OPENRIDE_DRIVE_RIDER_Y_TOLERANCE 0.02

#ifdef __ANDROID__
extern uint64_t SDL_GetTicksNS(void);
extern void SDL_Log(const char *fmt, ...);
static uint64_t openride_drive_view_audit_last_log_ns = 0U;
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

static double deg_to_rad(double degrees)
{
    return degrees * OPENRIDE_PI / 180.0;
}

static double rad_to_deg(double radians)
{
    return radians * 180.0 / OPENRIDE_PI;
}

static void rotate_screen_forward(double bearing_deg, double *x, double *y)
{
    if (!x || !y || fabs(bearing_deg) < 1e-12) return;
    const double angle = deg_to_rad(bearing_deg);
    const double c = cos(angle);
    const double s = sin(angle);
    const double ox = *x;
    const double oy = *y;
    /* Positive bearing rotates the world counter-clockwise on screen so that
       the selected heading points toward the top of the display. */
    *x = ox * c + oy * s;
    *y = -ox * s + oy * c;
}

static void rotate_screen_inverse(double bearing_deg, double *x, double *y)
{
    if (!x || !y || fabs(bearing_deg) < 1e-12) return;
    const double angle = deg_to_rad(bearing_deg);
    const double c = cos(angle);
    const double s = sin(angle);
    const double ox = *x;
    const double oy = *y;
    *x = ox * c - oy * s;
    *y = ox * s + oy * c;
}

double openride_world_size_pixels(double zoom)
{
    return OPENRIDE_TILE_SIZE * pow(2.0, zoom);
}

OpenRidePointD openride_mercator_forward(double lat_deg, double lon_deg)
{
    const double lat = deg_to_rad(clampd(lat_deg, OPENRIDE_MIN_LAT, OPENRIDE_MAX_LAT));
    OpenRidePointD result;

    result.x = (lon_deg + 180.0) / 360.0;
    result.x = wrap01(result.x);
    result.y = (1.0 - asinh(tan(lat)) / OPENRIDE_PI) * 0.5;
    result.y = clampd(result.y, 0.0, 1.0);

    return result;
}

void openride_mercator_inverse(OpenRidePointD p, double *lat_deg, double *lon_deg)
{
    p.x = wrap01(p.x);
    p.y = clampd(p.y, 0.0, 1.0);

    if (lon_deg) {
        *lon_deg = p.x * 360.0 - 180.0;
    }

    if (lat_deg) {
        const double n = OPENRIDE_PI * (1.0 - 2.0 * p.y);
        *lat_deg = rad_to_deg(atan(sinh(n)));
    }
}

static void camera_point_delta_pixels(const OpenRideMapCamera *camera,
                                      double lat_deg,
                                      double lon_deg,
                                      double *out_dx,
                                      double *out_dy)
{
    if (!camera || !out_dx || !out_dy) return;

    const OpenRidePointD center =
        openride_mercator_forward(camera->center_lat, camera->center_lon);
    const OpenRidePointD point = openride_mercator_forward(lat_deg, lon_deg);
    const double world_size = openride_world_size_pixels(camera->zoom);

    double dx = point.x - center.x;
    if (dx > 0.5) dx -= 1.0;
    if (dx < -0.5) dx += 1.0;

    double screen_dx = dx * world_size;
    double screen_dy = (point.y - center.y) * world_size;
    rotate_screen_forward(camera->bearing_deg, &screen_dx, &screen_dy);

    *out_dx = screen_dx;
    *out_dy = screen_dy;
}

static bool camera_screen_origin(const OpenRideMapCamera *camera,
                                 int viewport_width,
                                 int viewport_height,
                                 double *out_origin_x,
                                 double *out_origin_y,
                                 double *out_rider_lat,
                                 double *out_rider_lon,
                                 double *out_anchor_x_ratio,
                                 double *out_anchor_y_ratio,
                                 double *out_raw_rider_x_ratio,
                                 double *out_raw_rider_y_ratio)
{
    if (!camera || !out_origin_x || !out_origin_y) return false;

    const double default_origin_x = (double)viewport_width * 0.5;
    const double default_origin_y = (double)viewport_height * 0.5;
    *out_origin_x = default_origin_x;
    *out_origin_y = default_origin_y;

    double rider_lat = 0.0;
    double rider_lon = 0.0;
    double anchor_x_ratio = 0.5;
    double anchor_y_ratio = 0.5;
    if (!openride_drive_mode_get_screen_anchor(&rider_lat,
                                                &rider_lon,
                                                &anchor_x_ratio,
                                                &anchor_y_ratio)) {
        return false;
    }

    double rider_dx = 0.0;
    double rider_dy = 0.0;
    camera_point_delta_pixels(camera,
                              rider_lat,
                              rider_lon,
                              &rider_dx,
                              &rider_dy);

    const double raw_rider_x = default_origin_x + rider_dx;
    const double raw_rider_y = default_origin_y + rider_dy;
    const double min_y_ratio = anchor_y_ratio - OPENRIDE_DRIVE_RIDER_Y_TOLERANCE;
    const double max_y_ratio = anchor_y_ratio + OPENRIDE_DRIVE_RIDER_Y_TOLERANCE;
    const double min_y = (double)viewport_height * min_y_ratio;
    const double max_y = (double)viewport_height * max_y_ratio;

    /*
     * Horizontal stability is exact: heading-up navigation should keep the
     * motorcycle on the screen centerline. Vertically, correct only when the
     * geographic look-ahead would move the rider outside the 68..72% band.
     * This preserves useful anticipation changes instead of cancelling them.
     */
    *out_origin_x += (double)viewport_width * anchor_x_ratio - raw_rider_x;
    if (raw_rider_y < min_y) {
        *out_origin_y += min_y - raw_rider_y;
    } else if (raw_rider_y > max_y) {
        *out_origin_y += max_y - raw_rider_y;
    }

    if (out_rider_lat) *out_rider_lat = rider_lat;
    if (out_rider_lon) *out_rider_lon = rider_lon;
    if (out_anchor_x_ratio) *out_anchor_x_ratio = anchor_x_ratio;
    if (out_anchor_y_ratio) *out_anchor_y_ratio = anchor_y_ratio;
    if (out_raw_rider_x_ratio) {
        *out_raw_rider_x_ratio = viewport_width > 0
            ? raw_rider_x / (double)viewport_width : 0.5;
    }
    if (out_raw_rider_y_ratio) {
        *out_raw_rider_y_ratio = viewport_height > 0
            ? raw_rider_y / (double)viewport_height : 0.5;
    }
    return true;
}

void openride_camera_pan(OpenRideMapCamera *camera, double drag_x, double drag_y)
{
    if (!camera) return;

    OpenRidePointD center = openride_mercator_forward(camera->center_lat, camera->center_lon);
    const double world_size = openride_world_size_pixels(camera->zoom);

    /* Convert the screen drag back to north-up world axes before panning. */
    rotate_screen_inverse(camera->bearing_deg, &drag_x, &drag_y);

    /* Dragging the map right/down moves the camera center left/up. */
    center.x -= drag_x / world_size;
    center.y -= drag_y / world_size;

    center.x = wrap01(center.x);
    center.y = clampd(center.y, 0.0, 1.0);

    openride_mercator_inverse(center, &camera->center_lat, &camera->center_lon);
}

void openride_camera_zoom_at(OpenRideMapCamera *camera,
                             double zoom_delta,
                             double cursor_x,
                             double cursor_y,
                             int viewport_width,
                             int viewport_height)
{
    if (!camera || viewport_width <= 0 || viewport_height <= 0) return;

    const double old_zoom = camera->zoom;
    const double new_zoom = clampd(old_zoom + zoom_delta, OPENRIDE_MIN_ZOOM, OPENRIDE_MAX_ZOOM);

    if (fabs(new_zoom - old_zoom) < 1e-9) return;

    /*
     * In heading-up Drive, the geographic center is continuously owned by the
     * Drive controller and the rider framing transform owns the screen origin.
     * A pinch therefore changes scale only; repanning around the physical
     * viewport center would create a one-frame jump before Drive restores its
     * camera center on the next update.
     */
    if (openride_drive_mode_get_screen_anchor(NULL, NULL, NULL, NULL)) {
        camera->zoom = new_zoom;
        return;
    }

    OpenRidePointD center = openride_mercator_forward(camera->center_lat, camera->center_lon);
    const double old_world = openride_world_size_pixels(old_zoom);
    const double new_world = openride_world_size_pixels(new_zoom);
    double dx = cursor_x - (double)viewport_width * 0.5;
    double dy = cursor_y - (double)viewport_height * 0.5;
    rotate_screen_inverse(camera->bearing_deg, &dx, &dy);

    /* Geographic point currently under the cursor. */
    OpenRidePointD anchor = {
        center.x + dx / old_world,
        center.y + dy / old_world
    };

    /* Move center so the same geographic point stays under the cursor. */
    center.x = anchor.x - dx / new_world;
    center.y = anchor.y - dy / new_world;
    center.x = wrap01(center.x);
    center.y = clampd(center.y, 0.0, 1.0);

    camera->zoom = new_zoom;
    openride_mercator_inverse(center, &camera->center_lat, &camera->center_lon);
}

OpenRidePointD openride_geo_to_screen(const OpenRideMapCamera *camera,
                                      double lat_deg,
                                      double lon_deg,
                                      int viewport_width,
                                      int viewport_height)
{
    double screen_dx = 0.0;
    double screen_dy = 0.0;
    camera_point_delta_pixels(camera,
                              lat_deg,
                              lon_deg,
                              &screen_dx,
                              &screen_dy);

    double origin_x = (double)viewport_width * 0.5;
    double origin_y = (double)viewport_height * 0.5;
    double rider_lat = 0.0;
    double rider_lon = 0.0;
    double anchor_x_ratio = 0.5;
    double anchor_y_ratio = 0.5;
    double raw_rider_x_ratio = 0.5;
    double raw_rider_y_ratio = 0.5;
    const bool drive_anchor = camera_screen_origin(camera,
                                                    viewport_width,
                                                    viewport_height,
                                                    &origin_x,
                                                    &origin_y,
                                                    &rider_lat,
                                                    &rider_lon,
                                                    &anchor_x_ratio,
                                                    &anchor_y_ratio,
                                                    &raw_rider_x_ratio,
                                                    &raw_rider_y_ratio);

    OpenRidePointD result = {
        origin_x + screen_dx,
        origin_y + screen_dy
    };

#ifdef __ANDROID__
    if (drive_anchor
        && viewport_width > 0
        && viewport_height > 0
        && fabs(lat_deg - rider_lat) < 1e-10
        && fabs(lon_deg - rider_lon) < 1e-10) {
        const uint64_t now_ns = SDL_GetTicksNS();
        if (openride_drive_view_audit_last_log_ns == 0U
            || now_ns - openride_drive_view_audit_last_log_ns
                >= UINT64_C(1000000000)) {
            openride_drive_view_audit_last_log_ns = now_ns;
            SDL_Log("AUDIT_DRIVE_VIEW rider_x_pct=%.4f rider_y_pct=%.4f raw_rider_x_pct=%.4f raw_rider_y_pct=%.4f anchor_x_pct=%.4f anchor_y_pct=%.4f correction_x_pct=%.4f correction_y_pct=%.4f viewport=%dx%d",
                    result.x / (double)viewport_width,
                    result.y / (double)viewport_height,
                    raw_rider_x_ratio,
                    raw_rider_y_ratio,
                    anchor_x_ratio,
                    anchor_y_ratio,
                    (origin_x - (double)viewport_width * 0.5)
                        / (double)viewport_width,
                    (origin_y - (double)viewport_height * 0.5)
                        / (double)viewport_height,
                    viewport_width,
                    viewport_height);
        }
    }
#else
    (void)drive_anchor;
    (void)rider_lat;
    (void)rider_lon;
    (void)anchor_x_ratio;
    (void)anchor_y_ratio;
    (void)raw_rider_x_ratio;
    (void)raw_rider_y_ratio;
#endif

    return result;
}

void openride_screen_to_geo(const OpenRideMapCamera *camera,
                            double screen_x,
                            double screen_y,
                            int viewport_width,
                            int viewport_height,
                            double *lat_deg,
                            double *lon_deg)
{
    OpenRidePointD center = openride_mercator_forward(camera->center_lat, camera->center_lon);
    const double world_size = openride_world_size_pixels(camera->zoom);

    double origin_x = (double)viewport_width * 0.5;
    double origin_y = (double)viewport_height * 0.5;
    (void)camera_screen_origin(camera,
                               viewport_width,
                               viewport_height,
                               &origin_x,
                               &origin_y,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               NULL);

    double dx = screen_x - origin_x;
    double dy = screen_y - origin_y;
    rotate_screen_inverse(camera->bearing_deg, &dx, &dy);

    OpenRidePointD point = {
        center.x + dx / world_size,
        center.y + dy / world_size
    };

    openride_mercator_inverse(point, lat_deg, lon_deg);
}
