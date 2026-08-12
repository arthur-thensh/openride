#include "openride/app_lifecycle.h"

#include <string.h>

void openride_app_lifecycle_init(OpenRideAppLifecycle *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
}

void openride_app_lifecycle_enter_background(OpenRideAppLifecycle *state,
                                              bool gps_requested,
                                              bool drive_mode_active)
{
    if (!state) return;
    if (!state->in_background) {
        ++state->background_count;
    }
    state->in_background = true;
    state->resume_pending = false;
    state->gps_requested_before_background = gps_requested;
    state->drive_mode_before_background = drive_mode_active;
}

void openride_app_lifecycle_enter_foreground(OpenRideAppLifecycle *state)
{
    if (!state) return;
    if (state->in_background) {
        state->resume_pending = true;
    }
    state->in_background = false;
}

bool openride_app_lifecycle_take_resume(OpenRideAppLifecycle *state,
                                        bool *restart_gps,
                                        bool *restore_drive_mode)
{
    if (restart_gps) *restart_gps = false;
    if (restore_drive_mode) *restore_drive_mode = false;
    if (!state || !state->resume_pending) return false;

    if (restart_gps) *restart_gps = state->gps_requested_before_background;
    if (restore_drive_mode) *restore_drive_mode = state->drive_mode_before_background;
    state->resume_pending = false;
    return true;
}
