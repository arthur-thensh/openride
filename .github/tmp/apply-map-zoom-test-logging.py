from pathlib import Path
import re

main_path = Path("src/main.c")
main = main_path.read_text()

old = '#include "map/ormap_renderer.h"\n'
new = old + '#include "map/map_zoom_test_logger.h"\n'
if main.count(old) != 1:
    raise SystemExit(f"ormap renderer include count: {main.count(old)}")
main = main.replace(old, new, 1)

zoom_defines = '''#define OPENRIDE_MAP_ZOOM_TEST_LAT 50.370800
#define OPENRIDE_MAP_ZOOM_TEST_LON 3.080200
#define OPENRIDE_MAP_ZOOM_TEST_MIN 9.0
#define OPENRIDE_MAP_ZOOM_TEST_MAX 17.0
#define OPENRIDE_MAP_ZOOM_TEST_SPEED 0.50
'''
if main.count(zoom_defines) != 1:
    raise SystemExit("zoom constants block not found exactly once")
main = main.replace(zoom_defines, "", 1)

zoom_impl = re.compile(
    r'typedef struct OpenRideMapZoomTest \{.*?\} OpenRideMapZoomTest;\n\n'
    r'static void openride_map_zoom_test_start\(.*?\n\}\n\n'
    r'static void openride_map_zoom_test_update\(.*?\n\}\n\n',
    re.S,
)
main, count = zoom_impl.subn("", main, count=1)
if count != 1:
    raise SystemExit(f"zoom implementation block replacements: {count}")

cancel_old = 'map_zoom_test.active = false;'
if main.count(cancel_old) != 2:
    raise SystemExit(f"zoom cancel sites: {main.count(cancel_old)}")
main = main.replace(cancel_old, 'openride_map_zoom_test_cancel(&map_zoom_test);')

start_old = 'openride_map_zoom_test_start(&map_zoom_test, &camera);'
if main.count(start_old) != 2:
    raise SystemExit(f"zoom start sites: {main.count(start_old)}")
main = main.replace(
    start_old,
    'openride_map_zoom_test_start(&map_zoom_test, &camera, &platform_paths);',
)

status_old = 'test zoom 9.000 -> 17.000 -> 9.000'
if main.count(status_old) != 2:
    raise SystemExit(f"zoom status sites: {main.count(status_old)}")
main = main.replace(
    status_old,
    'test zoom 9.000 -> 17.000 -> 9.000 | log data/map-zoom-test.csv',
)

present_old = '''        SDL_RenderPresent(renderer);
    }

#ifdef __ANDROID__
    if (region_download_started) {
'''
present_new = '''        SDL_RenderPresent(renderer);

        /* Benchmark timing is captured immediately after Present, before any
         * logging work. Samples stay in RAM and are written only after the
         * final z9 frame, so instrumentation does not add filesystem I/O to
         * the measured frames. */
        if (map_zoom_test.active) {
            const uint64_t map_zoom_present_ns = SDL_GetTicksNS();
            OpenRideORMapRoadDebugStats map_zoom_road_debug;
            OpenRideORMapAreaDebugStats map_zoom_area_debug;
            memset(&map_zoom_road_debug, 0, sizeof(map_zoom_road_debug));
            memset(&map_zoom_area_debug, 0, sizeof(map_zoom_area_debug));
            map_zoom_road_debug.prewarm_zoom = -1;
            if (ormap_map && renderer_initialized) {
                openride_ormap_renderer_get_road_debug_stats(
                    &ormap_renderer, &map_zoom_road_debug);
                openride_ormap_renderer_get_area_debug_stats(
                    &ormap_renderer, &map_zoom_area_debug);
            }
            (void)openride_map_zoom_test_record_present(
                &map_zoom_test,
                map_zoom_present_ns,
                &map_zoom_road_debug,
                &map_zoom_area_debug,
                route_status,
                sizeof(route_status));
        }
    }

    openride_map_zoom_test_destroy(&map_zoom_test);

#ifdef __ANDROID__
    if (region_download_started) {
'''
if main.count(present_old) != 1:
    raise SystemExit(f"present/shutdown anchor count: {main.count(present_old)}")
main = main.replace(present_old, present_new, 1)
main_path.write_text(main)

cmake_path = Path("CMakeLists.txt")
cmake = cmake_path.read_text()
cmake_old = '''    src/map/ormap_renderer.c
    src/map/ormap_renderer_v4.c
'''
cmake_new = '''    src/map/ormap_renderer.c
    src/map/map_zoom_test_logger.c
    src/map/ormap_renderer_v4.c
'''
if cmake.count(cmake_old) != 1:
    raise SystemExit(f"CMake app source anchor count: {cmake.count(cmake_old)}")
cmake = cmake.replace(cmake_old, cmake_new, 1)
cmake_path.write_text(cmake)

logger_path = Path("src/map/map_zoom_test_logger.c")
logger = logger_path.read_text()
status_bug = '''    const bool written = write_results(test);
    if (status && status_size > 0U) {
        snprintf(status,
                 status_size,
                 written
                     ? "test zoom termine: data/%s (%zu frames)"
                     : "test zoom termine mais log impossible (%zu frames)",
                 written ? OPENRIDE_MAP_ZOOM_TEST_LOG_FILENAME : "",
                 test->sample_count);
    }
'''
status_fix = '''    const bool written = write_results(test);
    if (status && status_size > 0U) {
        if (written) {
            snprintf(status,
                     status_size,
                     "test zoom termine: data/%s (%zu frames)",
                     OPENRIDE_MAP_ZOOM_TEST_LOG_FILENAME,
                     test->sample_count);
        } else {
            snprintf(status,
                     status_size,
                     "test zoom termine mais log impossible (%zu frames)",
                     test->sample_count);
        }
    }
'''
if logger.count(status_bug) != 1:
    raise SystemExit("logger completion status block not found exactly once")
logger = logger.replace(status_bug, status_fix, 1)
logger_path.write_text(logger)
