#include "openride/gpx.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void write_fixture(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs("<?xml version=\"1.0\"?>\n"
          "<gpx version=\"1.1\" creator=\"test\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
          "<metadata><name>Sortie &amp; test</name></metadata>\n"
          "<wpt lat=\"50.3708\" lon=\"3.0802\"><name>Depart</name></wpt>\n"
          "<rte><rtept lat=\"50.3710\" lon=\"3.0810\"/><rtept lat=\"50.3720\" lon=\"3.0820\"/></rte>\n"
          "<trk><trkseg>"
          "<trkpt lat=\"50.3730\" lon=\"3.0830\"><ele>42.5</ele></trkpt>"
          "<trkpt lat=\"50.3740\" lon=\"3.0840\"/>"
          "</trkseg><trkseg>"
          "<trkpt lat=\"50.3750\" lon=\"3.0850\"/>"
          "</trkseg></trk>\n"
          "</gpx>\n",
          file);
    fclose(file);
}

int main(void)
{
    const char *input = "/tmp/openride-test-input.gpx";
    const char *output = "/tmp/openride-test-output.gpx";
    write_fixture(input);

    OpenRideGPXDocument doc;
    openride_gpx_document_init(&doc);
    char error[256] = {0};
    assert(openride_gpx_load_file(input, &doc, error, sizeof(error)));
    assert(strcmp(doc.name, "Sortie & test") == 0);
    assert(doc.waypoints.count == 1U);
    assert(doc.route_points.count == 2U);
    assert(doc.track_points.count == 3U);
    assert(doc.route_count == 1U);
    assert(doc.track_segment_count == 2U);
    assert(doc.track_points.points[0].starts_new_segment);
    assert(!doc.track_points.points[1].starts_new_segment);
    assert(doc.track_points.points[2].starts_new_segment);
    assert(doc.track_points.points[0].has_elevation);
    assert(fabs(doc.track_points.points[0].elevation_m - 42.5) < 0.001);

    OpenRideGPXBounds bounds = openride_gpx_document_bounds(&doc);
    assert(bounds.valid);
    assert(bounds.min_lat < 50.371);
    assert(bounds.max_lat > 50.374);

    assert(openride_gpx_save_document(output, &doc, "OpenRide test", error, sizeof(error)));

    OpenRideGPXDocument loaded;
    openride_gpx_document_init(&loaded);
    assert(openride_gpx_load_file(output, &loaded, error, sizeof(error)));
    assert(loaded.waypoints.count == 1U);
    assert(loaded.route_points.count == 2U);
    assert(loaded.track_points.count == 3U);

    OpenRideRoute route = {0};
    OpenRideRoutePoint geometry[3] = {
        {50.3708, 3.0802},
        {50.3718, 3.0812},
        {50.3728, 3.0822}
    };
    route.geometry = geometry;
    route.geometry_count = 3U;
    assert(openride_gpx_save_route(output, &route, "Route exportee", error, sizeof(error)));

    OpenRideGPXDocument exported;
    openride_gpx_document_init(&exported);
    assert(openride_gpx_load_file(output, &exported, error, sizeof(error)));
    assert(exported.route_points.count == 3U);
    assert(exported.route_count == 1U);
    assert(exported.track_points.count == 3U);
    assert(exported.track_segment_count == 1U);

    openride_gpx_document_destroy(&exported);
    openride_gpx_document_destroy(&loaded);
    openride_gpx_document_destroy(&doc);
    remove(input);
    remove(output);

    puts("GPX tests: OK");
    return 0;
}
