#ifndef OPENRIDE_MAP_CAMERA_H
#define OPENRIDE_MAP_CAMERA_H

typedef struct OpenRideMapCamera {
    double center_lat;
    double center_lon;
    double zoom;
    /* Clockwise degrees from north. 0 keeps the classic north-up view. */
    double bearing_deg;
} OpenRideMapCamera;

typedef struct OpenRidePointD {
    double x;
    double y;
} OpenRidePointD;

/* Web Mercator helpers. x/y are normalized to the [0, 1] world. */
OpenRidePointD openride_mercator_forward(double lat_deg, double lon_deg);
void openride_mercator_inverse(OpenRidePointD p, double *lat_deg, double *lon_deg);

/* Pan by a drag vector expressed in render pixels. */
void openride_camera_pan(OpenRideMapCamera *camera, double drag_x, double drag_y);

/* Zoom while keeping the geographic point under the cursor fixed. */
void openride_camera_zoom_at(OpenRideMapCamera *camera,
                             double zoom_delta,
                             double cursor_x,
                             double cursor_y,
                             int viewport_width,
                             int viewport_height);

/* Convert a geographic position to render pixels. */
OpenRidePointD openride_geo_to_screen(const OpenRideMapCamera *camera,
                                      double lat_deg,
                                      double lon_deg,
                                      int viewport_width,
                                      int viewport_height);

/* Convert render pixels to a geographic position. */
void openride_screen_to_geo(const OpenRideMapCamera *camera,
                            double screen_x,
                            double screen_y,
                            int viewport_width,
                            int viewport_height,
                            double *lat_deg,
                            double *lon_deg);

double openride_world_size_pixels(double zoom);

#endif
