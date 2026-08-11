#include "openride/map_camera.h"

#include <math.h>

#define OPENRIDE_PI 3.14159265358979323846
#define OPENRIDE_TILE_SIZE 256.0
#define OPENRIDE_MIN_LAT (-85.05112878)
#define OPENRIDE_MAX_LAT (85.05112878)
#define OPENRIDE_MIN_ZOOM 1.0
#define OPENRIDE_MAX_ZOOM 20.0

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

void openride_camera_pan(OpenRideMapCamera *camera, double drag_x, double drag_y)
{
    if (!camera) return;

    OpenRidePointD center = openride_mercator_forward(camera->center_lat, camera->center_lon);
    const double world_size = openride_world_size_pixels(camera->zoom);

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

    OpenRidePointD center = openride_mercator_forward(camera->center_lat, camera->center_lon);
    const double old_world = openride_world_size_pixels(old_zoom);
    const double new_world = openride_world_size_pixels(new_zoom);
    const double dx = cursor_x - (double)viewport_width * 0.5;
    const double dy = cursor_y - (double)viewport_height * 0.5;

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
    const OpenRidePointD center = openride_mercator_forward(camera->center_lat, camera->center_lon);
    const OpenRidePointD point = openride_mercator_forward(lat_deg, lon_deg);
    const double world_size = openride_world_size_pixels(camera->zoom);

    double dx = point.x - center.x;

    /* Choose the shortest horizontal path across the antimeridian. */
    if (dx > 0.5) dx -= 1.0;
    if (dx < -0.5) dx += 1.0;

    OpenRidePointD result = {
        (double)viewport_width * 0.5 + dx * world_size,
        (double)viewport_height * 0.5 + (point.y - center.y) * world_size
    };

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

    OpenRidePointD point = {
        center.x + (screen_x - (double)viewport_width * 0.5) / world_size,
        center.y + (screen_y - (double)viewport_height * 0.5) / world_size
    };

    openride_mercator_inverse(point, lat_deg, lon_deg);
}
