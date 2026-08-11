#include "openride/map_camera.h"

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

int main(void)
{
    test_mercator_round_trip();
    test_screen_round_trip();
    test_zoom_anchor();

    puts("OpenRide map camera tests: OK");
    return 0;
}
