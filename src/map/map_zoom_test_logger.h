#ifndef OPENRIDE_MAP_ZOOM_TEST_LOGGER_H
#define OPENRIDE_MAP_ZOOM_TEST_LOGGER_H

#include "map/ormap_renderer.h"
#include "openride/map_camera.h"
#include "openride/platform_paths.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPENRIDE_MAP_ZOOM_TEST_LAT 50.370800
#define OPENRIDE_MAP_ZOOM_TEST_LON 3.080200
#define OPENRIDE_MAP_ZOOM_TEST_MIN 9.0
#define OPENRIDE_MAP_ZOOM_TEST_MAX 17.0
#define OPENRIDE_MAP_ZOOM_TEST_SPEED 0.50
#define OPENRIDE_MAP_ZOOM_TEST_MAX_SAMPLES 8192U
#define OPENRIDE_MAP_ZOOM_TEST_LOG_FILENAME "map-zoom-test.csv"

typedef struct OpenRideMapZoomFrameProfile {
    double update_ms;
    double map_ms;
    double world_overview_ms;
    double world_detail_ms;
    double masks_ms;
    double areas_layer_ms;
    double waterways_ms;
    double roads_layer_ms;
    double labels_ms;
    double ui_ms;
    double present_ms;
    uint32_t visible_detail_regions;
    bool ormap_stats_valid;
} OpenRideMapZoomFrameProfile;

typedef struct OpenRideMapZoomTestSample {
    double elapsed_ms;
    double frame_ms;
    double zoom;
    int direction;
    OpenRideMapZoomFrameProfile profile;
    OpenRideORMapRoadDebugStats road;
    OpenRideORMapAreaDebugStats area;
} OpenRideMapZoomTestSample;

typedef struct OpenRideMapZoomTest {
    bool active;
    int direction;
    double zoom;
    uint64_t first_present_ns;
    uint64_t last_present_ns;
    OpenRideMapZoomTestSample *samples;
    size_t sample_count;
    size_t dropped_samples;
    char output_path[512];
} OpenRideMapZoomTest;

bool openride_map_zoom_test_start(OpenRideMapZoomTest *test, OpenRideMapCamera *camera, const OpenRidePlatformPaths *paths);
void openride_map_zoom_test_update(OpenRideMapZoomTest *test, OpenRideMapCamera *camera, double delta_seconds);
void openride_map_zoom_test_cancel(OpenRideMapZoomTest *test);
void openride_map_zoom_test_destroy(OpenRideMapZoomTest *test);
bool openride_map_zoom_test_record_present(OpenRideMapZoomTest *test, uint64_t present_ns, const OpenRideMapZoomFrameProfile *profile, const OpenRideORMapRoadDebugStats *road, const OpenRideORMapAreaDebugStats *area, char *status, size_t status_size);

#endif
