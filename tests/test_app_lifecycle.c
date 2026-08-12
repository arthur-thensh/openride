#include "openride/app_lifecycle.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    OpenRideAppLifecycle state;
    openride_app_lifecycle_init(&state);
    assert(!state.in_background);
    assert(state.background_count == 0U);

    openride_app_lifecycle_enter_background(&state, true, true);
    assert(state.in_background);
    assert(state.background_count == 1U);
    assert(state.gps_requested_before_background);
    assert(state.drive_mode_before_background);

    /* Repeated Android pause notifications must not count twice. */
    openride_app_lifecycle_enter_background(&state, true, true);
    assert(state.background_count == 1U);

    openride_app_lifecycle_enter_foreground(&state);
    assert(!state.in_background);

    bool gps = false;
    bool drive = false;
    assert(openride_app_lifecycle_take_resume(&state, &gps, &drive));
    assert(gps);
    assert(drive);
    assert(!openride_app_lifecycle_take_resume(&state, &gps, &drive));

    openride_app_lifecycle_enter_background(&state, false, false);
    openride_app_lifecycle_enter_foreground(&state);
    assert(openride_app_lifecycle_take_resume(&state, &gps, &drive));
    assert(!gps);
    assert(!drive);
    assert(state.background_count == 2U);

    puts("OpenRide app lifecycle tests: OK");
    return 0;
}
