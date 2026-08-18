#include "openride/drive_mode.h"
#include "openride/drive_perspective.h"
#include "openride/map_style.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void test_drive_map_style_lifecycle(void)
{
    OpenRideDriveModeState state;
    openride_map_style_set_drive_mode_active(false);
    openride_drive_mode_init(&state);
    assert(!openride_map_style_drive_mode_active());

    openride_drive_mode_set_active(&state, true);
    assert(openride_map_style_drive_mode_active());

    openride_drive_mode_set_active(&state, false);
    assert(!openride_map_style_drive_mode_active());
}

static void test_drive_perspective_projection(void)
{
    const OpenRideDrivePerspectiveConfig perspective =
        openride_drive_perspective_default_config();
    const double anchor = perspective.rider_anchor_y_ratio;

    assert(fabs(openride_drive_perspective_y_ratio(&perspective, 0.0)
                - perspective.horizon_y_ratio) < 1e-12);
    assert(fabs(openride_drive_perspective_y_ratio(&perspective, anchor)
                - anchor) < 1e-12);

    const double bottom_y =
        openride_drive_perspective_y_ratio(&perspective, 1.0);
    assert(bottom_y > 1.0);
    assert(bottom_y < 1.10);

    const double top_width =
        openride_drive_perspective_width_scale(&perspective, 0.0);
    const double bottom_width =
        openride_drive_perspective_width_scale(&perspective, 1.0);
    assert(top_width > 0.70 && top_width < 0.90);
    assert(fabs(openride_drive_perspective_width_scale(&perspective, anchor)
                - 1.0) < 1e-12);
    assert(bottom_width > 1.0 && bottom_width < 1.30);

    assert(fabs(openride_drive_perspective_x_ratio(
                    &perspective, 0.25, anchor) - 0.25) < 1e-12);
    assert(fabs(openride_drive_perspective_x_ratio(
                    &perspective, 0.75, anchor) - 0.75) < 1e-12);

    double previous_y = -1.0;
    double previous_width = 0.0;
    for (int i = 0; i <= 100; ++i) {
        const double source_y = (double)i / 100.0;
        const double projected_y =
            openride_drive_perspective_y_ratio(&perspective, source_y);
        const double width_scale =
            openride_drive_perspective_width_scale(&perspective, source_y);
        assert(projected_y >= previous_y - 1e-12);
        assert(width_scale >= previous_width - 1e-12);
        previous_y = projected_y;
        previous_width = width_scale;
    }

    /* The rider anchor is position-stable and locally 1:1 in both axes. */
    const double epsilon = 1e-4;
    const double just_before =
        openride_drive_perspective_y_ratio(&perspective, anchor - epsilon);
    const double just_after =
        openride_drive_perspective_y_ratio(&perspective, anchor + epsilon);
    assert(fabs((anchor - just_before) - epsilon) < 5e-8);
    assert(fabs((just_after - anchor) - epsilon) < 5e-8);

    /*
     * A true homography preserves collinearity. This is the regression that
     * guards against the V2.5 independent X/Y easing curves that visibly bent
     * long roads into a moving wave.
     */
    const double y1 = 0.15;
    const double y2 = 0.45;
    const double y3 = 0.75;
    const double x1 = 0.25 + 0.35 * y1;
    const double x2 = 0.25 + 0.35 * y2;
    const double x3 = 0.25 + 0.35 * y3;
    const double px1 =
        openride_drive_perspective_x_ratio(&perspective, x1, y1);
    const double py1 =
        openride_drive_perspective_y_ratio(&perspective, y1);
    const double px2 =
        openride_drive_perspective_x_ratio(&perspective, x2, y2);
    const double py2 =
        openride_drive_perspective_y_ratio(&perspective, y2);
    const double px3 =
        openride_drive_perspective_x_ratio(&perspective, x3, y3);
    const double py3 =
        openride_drive_perspective_y_ratio(&perspective, y3);
    const double cross =
        (px2 - px1) * (py3 - py1)
        - (py2 - py1) * (px3 - px1);
    assert(fabs(cross) < 1e-10);
}

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
    const double walking = openride_drive_mode_target_zoom(1.0, 1000.0);

    assert(city > cruise_60);
    assert(cruise_60 > motorway);
    assert(fabs(cruise_60 - 18.0) < 1e-9);
    assert(fabs(motorway - 16.8) < 1e-9);
    assert(fabs(walking - 18.9) < 1e-9);
    assert(near_turn > far_turn);
    assert(near_turn <= 19.0 + 1e-9);
}

static void test_render_zoom_calibration(void)
{
    assert(fabs(openride_drive_mode_render_zoom(16.8) - 18.0) < 1e-12);
    assert(fabs(openride_drive_mode_render_zoom(17.2) - 18.2) < 1e-12);
    assert(fabs(openride_drive_mode_render_zoom(17.6) - 18.5) < 1e-12);
    assert(fabs(openride_drive_mode_render_zoom(18.0) - 18.8) < 1e-12);
    assert(fabs(openride_drive_mode_render_zoom(18.3) - 19.0) < 1e-12);
    assert(fabs(openride_drive_mode_render_zoom(18.6) - 19.2) < 1e-12);
    assert(fabs(openride_drive_mode_render_zoom(18.9) - 19.3) < 1e-12);
    assert(fabs(openride_drive_mode_render_zoom(19.0) - 19.3) < 1e-12);

    const double cruise_60 =
        openride_drive_mode_target_zoom(60.0 / 3.6, 1000.0);
    assert(fabs(openride_drive_mode_render_zoom(cruise_60) - 18.8) < 1e-12);

    double previous = openride_drive_mode_render_zoom(16.5);
    for (int i = 1; i <= 250; ++i) {
        const double camera_zoom = 16.5 + (double)i * 0.01;
        const double rendered = openride_drive_mode_render_zoom(camera_zoom);
        assert(rendered >= previous - 1e-12);
        assert(rendered <= 19.3 + 1e-12);
        previous = rendered;
    }
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
    assert(fabs(state.camera_lat - state.target_camera_lat) < 1e-12);
    assert(fabs(state.camera_lon - state.target_camera_lon) < 1e-12);
    assert(fabs(state.camera_zoom - state.target_camera_zoom) < 1e-12);
    assert(fabs(state.camera_bearing_deg - state.target_camera_bearing_deg) < 1e-12);
    assert(state.lookahead_distance_m > 0.0);
    assert(!state.framing_active);
    openride_drive_mode_set_active(&state, false);
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
    openride_drive_mode_set_active(&near_state, false);
}

static void test_heading_input_smoothing(void)
{
    OpenRideDriveModeState state;
    openride_drive_mode_init(&state);
    openride_drive_mode_set_active(&state, true);

    openride_drive_mode_update(&state,
                               true,
                               true,
                               0.0,
                               5.0,
                               50.0,
                               3.0,
                               60.0 / 3.6,
                               0.0,
                               1000.0,
                               0.016);
    assert(fabs(state.smoothed_heading_deg) < 1e-9);

    openride_drive_mode_update(&state,
                               true,
                               true,
                               0.0,
                               5.0,
                               50.0,
                               3.0,
                               60.0 / 3.6,
                               90.0,
                               1000.0,
                               0.100);

    assert(state.smoothed_heading_deg > 0.0);
    assert(state.smoothed_heading_deg < 90.0);
    assert(state.target_camera_bearing_deg > 0.0);
    assert(state.target_camera_bearing_deg < 90.0);
    assert(state.camera_bearing_deg > 0.0);
    assert(state.camera_bearing_deg < state.target_camera_bearing_deg);
    openride_drive_mode_set_active(&state, false);
}

static void test_speed_input_smoothing(void)
{
    OpenRideDriveModeState state;
    openride_drive_mode_init(&state);
    openride_drive_mode_set_active(&state, true);

    openride_drive_mode_update(&state,
                               true,
                               true,
                               0.0,
                               5.0,
                               50.0,
                               3.0,
                               5.0,
                               0.0,
                               1000.0,
                               0.016);
    assert(fabs(state.smoothed_speed_mps - 5.0) < 1e-9);

    openride_drive_mode_update(&state,
                               true,
                               true,
                               0.0,
                               5.0,
                               50.0,
                               3.0,
                               30.0,
                               0.0,
                               1000.0,
                               0.100);

    assert(state.smoothed_speed_mps > 5.0);
    assert(state.smoothed_speed_mps < 30.0);
    assert(state.target_camera_zoom
           > openride_drive_mode_target_zoom(30.0, 1000.0));
    openride_drive_mode_set_active(&state, false);
}

static void test_north_up_lookahead_follows_travel_direction(void)
{
    OpenRideDriveModeState state;
    openride_drive_mode_init(&state);
    openride_drive_mode_set_active(&state, true);
    openride_drive_mode_set_heading_up(&state, false);

    const double lat = 50.0;
    const double lon = 3.0;
    openride_drive_mode_update(&state,
                               true,
                               true,
                               0.0,
                               5.0,
                               lat,
                               lon,
                               60.0 / 3.6,
                               90.0,
                               1000.0,
                               0.016);

    assert(state.initialized);
    assert(fabs(state.camera_bearing_deg) < 1e-9);
    assert(fabs(state.target_camera_bearing_deg) < 1e-9);
    assert(state.camera_lon > lon);
    assert(fabs(state.camera_lat - lat) < 0.001);
    assert(!state.framing_active);
    openride_drive_mode_set_active(&state, false);
}

static void test_render_view_framing_lifecycle(void)
{
    const double lat = 50.3708;
    const double lon = 3.0802;
    const double speed = 60.0 / 3.6;

    OpenRideDriveModeState state;
    openride_drive_mode_init(&state);
    openride_drive_mode_set_active(&state, true);

    openride_drive_mode_update(&state,
                               true,
                               true,
                               0.0,
                               5.0,
                               lat,
                               lon,
                               speed,
                               90.0,
                               1000.0,
                               0.016);
    assert(!state.framing_active);

    openride_drive_mode_note_render_view(1000, 1000, 18.0);
    openride_drive_mode_update(&state,
                               true,
                               true,
                               0.0,
                               5.0,
                               lat,
                               lon,
                               speed,
                               90.0,
                               1000.0,
                               0.016);
    assert(state.framing_active);
    assert(fabs(state.rider_screen_x_ratio - 0.50) < 1e-12);
    assert(state.rider_screen_y_ratio >= 0.68 - 1e-12);
    assert(state.rider_screen_y_ratio <= 0.72 + 1e-12);

    openride_drive_mode_set_heading_up(&state, false);
    openride_drive_mode_update(&state,
                               true,
                               true,
                               0.0,
                               5.0,
                               lat,
                               lon,
                               speed,
                               90.0,
                               1000.0,
                               0.016);
    assert(!state.framing_active);

    openride_drive_mode_set_heading_up(&state, true);
    openride_drive_mode_update(&state,
                               true,
                               true,
                               0.0,
                               5.0,
                               lat,
                               lon,
                               speed,
                               90.0,
                               1000.0,
                               0.016);
    assert(state.framing_active);

    openride_drive_mode_update(&state,
                               true,
                               true,
                               6.0,
                               5.0,
                               lat,
                               lon,
                               speed,
                               90.0,
                               1000.0,
                               0.016);
    assert(!state.framing_active);

    openride_drive_mode_set_active(&state, false);
    assert(!state.framing_active);
    assert(!openride_map_style_drive_mode_active());
}

int main(void)
{
    test_drive_map_style_lifecycle();
    test_drive_perspective_projection();
    test_gps_quality();
    test_auto_zoom();
    test_render_zoom_calibration();
    test_maneuver_lookahead();
    test_camera_lookahead();
    test_camera_maneuver_anticipation();
    test_heading_input_smoothing();
    test_speed_input_smoothing();
    test_north_up_lookahead_follows_travel_direction();
    test_render_view_framing_lifecycle();
    assert(!openride_map_style_drive_mode_active());
    puts("OpenRide drive mode tests: OK");
    return 0;
}
