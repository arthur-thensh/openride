#ifndef OPENRIDE_DRIVE_MODE_H
#define OPENRIDE_DRIVE_MODE_H

#include <stdbool.h>

typedef enum OpenRideGPSQuality {
    OPENRIDE_GPS_UNAVAILABLE = 0,
    OPENRIDE_GPS_LOST,
    OPENRIDE_GPS_POOR,
    OPENRIDE_GPS_FAIR,
    OPENRIDE_GPS_GOOD
} OpenRideGPSQuality;

typedef struct OpenRideDriveModeState {
    bool active;
    bool heading_up;
    bool auto_zoom;
    bool initialized;

    double camera_lat;
    double camera_lon;
    double camera_zoom;
    double camera_bearing_deg;

    double target_camera_lat;
    double target_camera_lon;
    double target_camera_zoom;
    double target_camera_bearing_deg;
    double lookahead_distance_m;

    double smoothed_speed_mps;
    double smoothed_heading_deg;

    bool framing_active;
    double rider_raw_x_ratio;
    double rider_raw_y_ratio;
    double rider_screen_x_ratio;
    double rider_screen_y_ratio;
    double framing_correction_x_ratio;
    double framing_correction_y_ratio;

    double gps_age_s;
    double gps_accuracy_m;
    OpenRideGPSQuality gps_quality;
} OpenRideDriveModeState;

void openride_drive_mode_init(OpenRideDriveModeState *state);
void openride_drive_mode_set_active(OpenRideDriveModeState *state, bool active);
void openride_drive_mode_set_heading_up(OpenRideDriveModeState *state, bool heading_up);
void openride_drive_mode_set_auto_zoom(OpenRideDriveModeState *state, bool auto_zoom);

/*
 * Publish the viewport and actual map zoom observed during rendering. Drive
 * uses this on the following update to keep the motorcycle in its lower-screen
 * framing band by moving the real geographic camera center. The map renderer
 * therefore remains the single source of truth for every layer.
 */
void openride_drive_mode_note_render_view(int viewport_width,
                                          int viewport_height,
                                          double render_zoom);

double openride_drive_mode_target_zoom(double speed_mps, double maneuver_distance_m);

/*
 * V2.5.2 visual scale calibration for scalable maps.
 *
 * Drive's semantic zoom curve predates the close 2.5D phone view and therefore
 * tops out visually too far from the rider. Keep the existing speed/maneuver
 * dynamics in the core, then map them to a closer render scale. The mapping is
 * monotonic and intentionally strongest at motorway speeds, where the old
 * curve was particularly far away, while preserving useful forward context.
 *
 * Reference points (semantic -> rendered):
 *   16.8 -> 18.0
 *   17.2 -> 18.2
 *   17.6 -> 18.5
 *   18.0 -> 18.8
 *   18.3 -> 19.0
 *   18.6 -> 19.2
 *   18.9 -> 19.3
 */
static inline double openride_drive_mode_render_zoom(double camera_zoom)
{
    if (camera_zoom <= 16.8) return camera_zoom + 1.2;
    if (camera_zoom <= 17.2) {
        return 18.0 + (camera_zoom - 16.8) * 0.5;
    }
    if (camera_zoom <= 17.6) {
        return 18.2 + (camera_zoom - 17.2) * 0.75;
    }
    if (camera_zoom <= 18.0) {
        return 18.5 + (camera_zoom - 17.6) * 0.75;
    }
    if (camera_zoom <= 18.3) {
        return 18.8 + (camera_zoom - 18.0) * (2.0 / 3.0);
    }
    if (camera_zoom <= 18.6) {
        return 19.0 + (camera_zoom - 18.3) * (2.0 / 3.0);
    }
    if (camera_zoom <= 18.9) {
        return 19.2 + (camera_zoom - 18.6) * (1.0 / 3.0);
    }
    return 19.3;
}

double openride_drive_mode_lookahead_m(double speed_mps);
double openride_drive_mode_target_lookahead_m(double speed_mps,
                                               double maneuver_distance_m);
OpenRideGPSQuality openride_drive_mode_gps_quality(bool gps_active,
                                                   bool has_sample,
                                                   double sample_age_s,
                                                   double accuracy_m);
const char *openride_drive_mode_gps_quality_name(OpenRideGPSQuality quality);

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
                                double delta_seconds);

#endif
