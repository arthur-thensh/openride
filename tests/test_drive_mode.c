#include "openride/drive_mode.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void test_gps_quality(void)
{
    assert(openride_drive_mode_gps_quality(false, false, 0.0, 0.0) == OPENRIDE_GPS_UNAVAILABLE);
    assert(openride_drive_mode_gps_quality(true, false, 0.0, 0.0) == OPENRIDE_GPS_LOST);
    assert(openride_drive_mode_gps_quality(true, true, 6.0, 5.0) == OPENRIDE_GPS_LOST);
    assert(openride_drive_mode_gps_quality(true, true, 0.5, 12.0) == OPENRIDE_GPS_GOOD);
    assert(openride_drive_mode_gps_quality(true, true, 0.5, 30.0) == OPENRIDE_GPS_FAIR);
    assert(openride_drive_mode_gps_quality(true, true, 0.5, 80.0) == OPENRIDE_GPS_POOR);
}

static void test_auto_zoom(void)
{
    const double city = openride_drive_mode_target_zoom(5.0, 1000.0);
    const double cruise_60 =
        openride_drive_mode_target_zoom(60.0 / 3.6, 1000.0);
    const double motorway = openride_drive_mode_target_zoom(33.0, 1000.0);
    const double near_turn = openride_drive_mode_target_zoom(20.0, 50.0);
    const double far_turn = openride_drive_mode_target_zoom(20.0, 1000.0);

    assert(city > cruise_60);
    assert(cruise_60 > motorway);
    assert(fabs(cruise_60 - 17.2) < 1e-9);
    assert(fabs(motorway - 16.0) < 1e-9);
    assert(near_turn > far_turn);
}

static void test_maneuver_lookahead(void)
{
    const double speed = 60.0 / 3.6;
    const double base = openride_drive_mode_lookahead_m(speed);
    const double far = openride_drive_mode_target_lookahead_m(speed, 1000.0);
    const double approaching = openride_drive_mode_target_lookahead_m(speed, 400.0);
    const double medium = openride_drive_mode_target_lookahead_m(speed, 300.0);
    const double near = openride_drive_mode_target_lookahead_m(speed, 100.0);
    const double immediate = openride_drive_mode_target_lookahead_m(speed, 50.0);

    assert(fabs(base - 103.0) < 0.1);
    assert(fabs(far - base) < 1e-9);
    assert(approaching > base);
    assert(medium > base);
    assert(approaching > medium);
    assert(near < base);
    assert(immediate <= near);
    assert(immediate >= 35.0 - 1e-9);
}

static void test_camera_lookahead(void)
{
    OpenRideDriveModeState state;
    openride_drive_mode_init(&state);
    openride_drive_mode_set_active(&state, true);
    openride_drive_mode_update(&state,
                               true,
                               true,
                               0.0,
                               5.0,
                               50.3708,
                               3.0802,
                               20.0,
                               90.0,
                               1000.0,
                               0.016);
    assert(state.initialized);
    assert(state.camera_lon > 3.0802);
    assert(fabs(state.camera_bearing_deg - 90.0) < 0.01);
}

static void test_camera_maneuver_anticipation(void)
{
    OpenRideDriveModeState far_state;
    OpenRideDriveModeState approach_state;
    OpenRideDriveModeState near_state;
    openride_drive_mode_init(&far_state);
    openride_drive_mode_init(&approach_state);
    openride_drive_mode_init(&near_state);
    openride_drive_mode_set_active(&far_state, true);
    openride_drive_mode_set_active(&approach_state, true);
    openride_drive_mode_set_active(&near_state, true);

    const double speed = 60.0 / 3.6;
    openride_drive_mode_update(&far_state,
                               true,
                               true,
                               0.0,
                               5.0,
                               50.0000,
                               3.0000,
                               speed,
                               90.0,
                               1000.0,
                               0.016);
    openride_drive_mode_update(&approach_state,
                               true,
                               true,
                               0.0,
                               5.0,
                               50.0000,
                               3.0000,
                               speed,
                               90.0,
                               400.0,
                               0.016);
    openride_drive_mode_update(&near_state,
                               true,
                               true,
                               0.0,
                               5.0,
                               50.0000,
                               3.0000,
                               speed,
                               90.0,
                               80.0,
                               0.016);

    assert(approach_state.camera_lon > far_state.camera_lon);
    assert(near_state.camera_lon < far_state.camera_lon);
    assert(fabs(approach_state.camera_bearing_deg - 90.0) < 0.01);
    assert(fabs(near_state.camera_bearing_deg - 90.0) < 0.01);
}

int main(void)
{
    test_gps_quality();
    test_auto_zoom();
    test_maneuver_lookahead();
    test_camera_lookahead();
    test_camera_maneuver_anticipation();
    puts("OpenRide drive mode tests: OK");
    return 0;
}
