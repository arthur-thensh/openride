#ifndef OPENRIDE_DRIVE_PERSPECTIVE_H
#define OPENRIDE_DRIVE_PERSPECTIVE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OpenRideDrivePerspectiveConfig {
    double horizon_y_ratio;
    double rider_anchor_y_ratio;
    double top_width_scale;
    double bottom_width_scale;
} OpenRideDrivePerspectiveConfig;

OpenRideDrivePerspectiveConfig openride_drive_perspective_default_config(void);

double openride_drive_perspective_y_ratio(
    const OpenRideDrivePerspectiveConfig *config,
    double source_y_ratio);

double openride_drive_perspective_width_scale(
    const OpenRideDrivePerspectiveConfig *config,
    double source_y_ratio);

#ifdef __cplusplus
}
#endif

#endif
