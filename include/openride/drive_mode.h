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
    double gps_age_s;
    double gps_accuracy_m;
    OpenRideGPSQuality gps_quality;
} OpenRideDriveModeState;

void openride_drive_mode_init(OpenRideDriveModeState *state);
void openride_drive_mode_set_active(OpenRideDriveModeState *state, bool active);
void openride_drive_mode_set_heading_up(OpenRideDriveModeState *state, bool heading_up);
void openride_drive_mode_set_auto_zoom(OpenRideDriveModeState *state, bool auto_zoom);

double openride_drive_mode_target_zoom(double speed_mps, double maneuver_distance_m);
double openride_drive_mode_lookahead_m(double speed_mps);
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
