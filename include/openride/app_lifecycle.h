#ifndef OPENRIDE_APP_LIFECYCLE_H
#define OPENRIDE_APP_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct OpenRideAppLifecycle {
    bool in_background;
    bool resume_pending;
    bool gps_requested_before_background;
    bool drive_mode_before_background;
    uint32_t background_count;
} OpenRideAppLifecycle;

void openride_app_lifecycle_init(OpenRideAppLifecycle *state);
void openride_app_lifecycle_enter_background(OpenRideAppLifecycle *state,
                                              bool gps_requested,
                                              bool drive_mode_active);
void openride_app_lifecycle_enter_foreground(OpenRideAppLifecycle *state);
bool openride_app_lifecycle_take_resume(OpenRideAppLifecycle *state,
                                        bool *restart_gps,
                                        bool *restore_drive_mode);

#endif
