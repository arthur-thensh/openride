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
