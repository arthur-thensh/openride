#include "openride/map_camera.h"
#include "openride/drive_mode.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static int nearly_equal(double a, double b, double epsilon)
{
    return fabs(a - b) <= epsilon;
}

static void test_mercator_round_trip(void)
{
    const double lat = 48.8566;
    const double lon = 2.3522;
    const OpenRidePointD p = openride_mercator_forward(lat, lon);
    double result_lat = 0.0;
    double result_lon = 0.0;

    openride_mercator_inverse(p, &result_lat, &result_lon);

    assert(nearly_equal(lat, result_lat, 1e-7));
    assert(nearly_equal(lon, result_lon, 1e-7));
}

static void test_screen_round_trip(void)
{
    OpenRideMapCamera camera = {
        .center_lat = 48.8566,
        .center_lon = 2.3522,
        .zoom = 12.0
    };

    const double lat = 48.8600;
    const double lon = 2.3400;
    const OpenRidePointD screen = openride_geo_to_screen(&camera, lat, lon, 1200, 800);
    double result_lat = 0.0;
    double result_lon = 0.0;

    openride_screen_to_geo(&camera,
                           screen.x,
                           screen.y,
                           1200,
                           800,
                           &result_lat,
                           &result_lon);

    assert(nearly_equal(lat, result_lat, 1e-7));
    assert(nearly_equal(lon, result_lon, 1e-7));
}

static void test_bearing_screen_round_trip(void)
{
    OpenRideMapCamera camera = {
        .center_lat = 48.8566,
        .center_lon = 2.3522,
        .zoom = 13.0,
        .bearing_deg = 73.0
    };

    const double lat = 48.8620;
    const double lon = 2.3650;
    const OpenRidePointD screen = openride_geo_to_screen(&camera, lat, lon, 1200, 800);
    double result_lat = 0.0;
    double result_lon = 0.0;
    openride_screen_to_geo(&camera,
                           screen.x,
                           screen.y,
                           1200,
                           800,
                           &result_lat,
                           &result_lon);
    assert(nearly_equal(lat, result_lat, 1e-7));
    assert(nearly_equal(lon, result_lon, 1e-7));
}

static void test_bearing_heading_up(void)
{
    OpenRideMapCamera camera = {
        .center_lat = 50.0,
        .center_lon = 3.0,
        .zoom = 15.0,
        .bearing_deg = 90.0
    };
    const OpenRidePointD east = openride_geo_to_screen(&camera, 50.0, 3.01, 1000, 1000);
    assert(east.y < 500.0);
}

static void test_zoom_anchor(void)
{
    OpenRideMapCamera camera = {
        .center_lat = 48.8566,
        .center_lon = 2.3522,
        .zoom = 10.0
    };

    double before_lat = 0.0;
    double before_lon = 0.0;
    double after_lat = 0.0;
    double after_lon = 0.0;

    openride_screen_to_geo(&camera, 900.0, 300.0, 1200, 800, &before_lat, &before_lon);
    openride_camera_zoom_at(&camera, 2.0, 900.0, 300.0, 1200, 800);
    openride_screen_to_geo(&camera, 900.0, 300.0, 1200, 800, &after_lat, &after_lon);

    assert(nearly_equal(before_lat, after_lat, 1e-7));
    assert(nearly_equal(before_lon, after_lon, 1e-7));
}

static OpenRideMapCamera drive_camera(const OpenRideDriveModeState *drive,
                                      double render_zoom)
{
    OpenRideMapCamera camera = {
        .center_lat = drive->camera_lat,
        .center_lon = drive->camera_lon,
        .zoom = render_zoom,
        .bearing_deg = drive->heading_up ? drive->camera_bearing_deg : 0.0
    };
    return camera;
}

static void test_drive_rider_screen_framing(void)
{
    const double rider_lat = 50.3708;
    const double rider_lon = 3.0802;
    const double speed = 60.0 / 3.6;

    OpenRideDriveModeState drive;
    openride_drive_mode_init(&drive);
    openride_drive_mode_set_active(&drive, true);

    /* First update has no Drive render yet, so it keeps the pure look-ahead. */
    openride_drive_mode_update(&drive,
                               true,
                               true,
                               0.0,
                               5.0,
                               rider_lat,
                               rider_lon,
                               speed,
                               90.0,
                               1000.0,
                               0.016);
    assert(drive.initialized);
    assert(!drive.framing_active);

    /* The ordinary projection publishes the actual viewport/zoom. */
    OpenRideMapCamera camera = drive_camera(&drive, 18.0);
    (void)openride_geo_to_screen(&camera,
                                 rider_lat,
                                 rider_lon,
                                 1000,
                                 1000);

    /* The following Drive update moves its real geographic camera center. */
    openride_drive_mode_update(&drive,
                               true,
                               true,
                               0.0,
                               5.0,
                               rider_lat,
                               rider_lon,
                               speed,
                               90.0,
                               1000.0,
                               0.016);
    assert(drive.framing_active);
    assert(drive.rider_screen_x_ratio >= 0.49);
    assert(drive.rider_screen_x_ratio <= 0.51);
    assert(drive.rider_screen_y_ratio >= 0.68 - 1e-9);
    assert(drive.rider_screen_y_ratio <= 0.72 + 1e-9);

    camera = drive_camera(&drive, 18.0);
    const OpenRidePointD rider = openride_geo_to_screen(&camera,
                                                         rider_lat,
                                                         rider_lon,
                                                         1000,
                                                         1000);
    assert(nearly_equal(rider.x, 500.0, 1e-5));
    assert(rider.y >= 680.0 - 1e-4);
    assert(rider.y <= 720.0 + 1e-4);

    double roundtrip_lat = 0.0;
    double roundtrip_lon = 0.0;
    openride_screen_to_geo(&camera,
                           rider.x,
                           rider.y,
                           1000,
                           1000,
                           &roundtrip_lat,
                           &roundtrip_lon);
    assert(nearly_equal(roundtrip_lat, rider_lat, 1e-7));
    assert(nearly_equal(roundtrip_lon, rider_lon, 1e-7));

    /* Manual Drive zoom is learned from the real rendered camera. */
    openride_drive_mode_set_auto_zoom(&drive, false);
    camera = drive_camera(&drive, 17.5);
    (void)openride_geo_to_screen(&camera,
                                 rider_lat,
                                 rider_lon,
                                 1000,
                                 1000);
    openride_drive_mode_update(&drive,
                               true,
                               true,
                               0.0,
                               5.0,
                               rider_lat,
                               rider_lon,
                               speed,
                               90.0,
                               1000.0,
                               0.016);
    assert(drive.framing_active);
    camera = drive_camera(&drive, 17.5);
    const OpenRidePointD rider_manual_zoom = openride_geo_to_screen(&camera,
                                                                     rider_lat,
                                                                     rider_lon,
                                                                     1000,
                                                                     1000);
    assert(nearly_equal(rider_manual_zoom.x, 500.0, 1e-5));
    assert(rider_manual_zoom.y >= 680.0 - 1e-4);
    assert(rider_manual_zoom.y <= 720.0 + 1e-4);

    /* North-up deliberately disables lower-screen framing. */
    openride_drive_mode_set_heading_up(&drive, false);
    openride_drive_mode_update(&drive,
                               true,
                               true,
                               0.0,
                               5.0,
                               rider_lat,
                               rider_lon,
                               speed,
                               90.0,
                               1000.0,
                               0.016);
    assert(!drive.framing_active);

    /* A stale GPS fix also drops framing immediately. */
    openride_drive_mode_set_heading_up(&drive, true);
    openride_drive_mode_update(&drive,
                               true,
                               true,
                               6.0,
                               5.0,
                               rider_lat,
                               rider_lon,
                               speed,
                               90.0,
                               1000.0,
                               0.016);
    assert(!drive.framing_active);

    openride_drive_mode_set_active(&drive, false);
}

int main(void)
{
    test_mercator_round_trip();
    test_screen_round_trip();
    test_bearing_screen_round_trip();
    test_bearing_heading_up();
    test_zoom_anchor();
    test_drive_rider_screen_framing();

    puts("OpenRide map camera tests: OK");
    return 0;
}
