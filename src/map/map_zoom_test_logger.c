#include "map/map_zoom_test_logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_double_ascending(const void *lhs, const void *rhs)
{
    const double a = *(const double *)lhs;
    const double b = *(const double *)rhs;
    return a < b ? -1 : (a > b ? 1 : 0);
}

static double percentile_sorted(const double *values, size_t count, double q)
{
    if (!values || count == 0U) return 0.0;
    if (count == 1U) return values[0];
    if (q <= 0.0) return values[0];
    if (q >= 1.0) return values[count - 1U];

    const double position = q * (double)(count - 1U);
    const size_t lower = (size_t)position;
    const size_t upper = lower + 1U < count ? lower + 1U : lower;
    const double fraction = position - (double)lower;
    return values[lower] + (values[upper] - values[lower]) * fraction;
}

static void release_samples(OpenRideMapZoomTest *test)
{
    if (!test) return;
    free(test->samples);
    test->samples = NULL;
    test->sample_count = 0U;
    test->dropped_samples = 0U;
    test->first_present_ns = 0U;
    test->last_present_ns = 0U;
}

static bool write_results(const OpenRideMapZoomTest *test)
{
    if (!test || !test->samples || test->sample_count == 0U
        || test->output_path[0] == '\0') {
        return false;
    }

    double *frame_times = malloc(test->sample_count * sizeof(*frame_times));
    if (!frame_times) return false;

    double frame_sum_ms = 0.0;
    double max_frame_ms = 0.0;
    double max_road_ms = 0.0;
    double max_road_load_ms = 0.0;
    double max_area_ms = 0.0;
    double max_area_load_ms = 0.0;
    size_t max_frame_index = 0U;

    for (size_t i = 0U; i < test->sample_count; ++i) {
        const OpenRideMapZoomTestSample *sample = &test->samples[i];
        frame_times[i] = sample->frame_ms;
        frame_sum_ms += sample->frame_ms;
        if (sample->frame_ms > max_frame_ms) {
            max_frame_ms = sample->frame_ms;
            max_frame_index = i;
        }
        if (sample->road.roads_ms > max_road_ms) max_road_ms = sample->road.roads_ms;
        if (sample->road.load_ms > max_road_load_ms) max_road_load_ms = sample->road.load_ms;
        if (sample->area.areas_ms > max_area_ms) max_area_ms = sample->area.areas_ms;
        if (sample->area.load_ms > max_area_load_ms) max_area_load_ms = sample->area.load_ms;
    }
    qsort(frame_times,
          test->sample_count,
          sizeof(*frame_times),
          compare_double_ascending);

    FILE *file = fopen(test->output_path, "wb");
    if (!file) {
        free(frame_times);
        return false;
    }

    const double mean_frame_ms = frame_sum_ms / (double)test->sample_count;
    const double average_fps = frame_sum_ms > 0.0
        ? 1000.0 * (double)test->sample_count / frame_sum_ms
        : 0.0;
    const OpenRideMapZoomTestSample *worst = &test->samples[max_frame_index];

    fprintf(file, "# OpenRide map zoom benchmark\n");
    fprintf(file, "# format_version=1\n");
    fprintf(file, "# latitude=%.6f\n", OPENRIDE_MAP_ZOOM_TEST_LAT);
    fprintf(file, "# longitude=%.6f\n", OPENRIDE_MAP_ZOOM_TEST_LON);
    fprintf(file, "# zoom_min=%.3f\n", OPENRIDE_MAP_ZOOM_TEST_MIN);
    fprintf(file, "# zoom_max=%.3f\n", OPENRIDE_MAP_ZOOM_TEST_MAX);
    fprintf(file, "# zoom_speed=%.3f\n", OPENRIDE_MAP_ZOOM_TEST_SPEED);
    fprintf(file, "# samples=%zu\n", test->sample_count);
    fprintf(file, "# dropped_samples=%zu\n", test->dropped_samples);
    fprintf(file, "# duration_ms=%.3f\n", frame_sum_ms);
    fprintf(file, "# fps_average=%.3f\n", average_fps);
    fprintf(file, "# frame_ms_mean=%.3f\n", mean_frame_ms);
    fprintf(file, "# frame_ms_p50=%.3f\n", percentile_sorted(frame_times, test->sample_count, 0.50));
    fprintf(file, "# frame_ms_p75=%.3f\n", percentile_sorted(frame_times, test->sample_count, 0.75));
    fprintf(file, "# frame_ms_p90=%.3f\n", percentile_sorted(frame_times, test->sample_count, 0.90));
    fprintf(file, "# frame_ms_p95=%.3f\n", percentile_sorted(frame_times, test->sample_count, 0.95));
    fprintf(file, "# frame_ms_p99=%.3f\n", percentile_sorted(frame_times, test->sample_count, 0.99));
    fprintf(file, "# frame_ms_max=%.3f\n", max_frame_ms);
    fprintf(file, "# worst_frame=%zu\n", max_frame_index);
    fprintf(file, "# worst_zoom=%.5f\n", worst->zoom);
    fprintf(file, "# worst_direction=%d\n", worst->direction);
    fprintf(file, "# road_ms_max=%.3f\n", max_road_ms);
    fprintf(file, "# road_load_ms_max=%.3f\n", max_road_load_ms);
    fprintf(file, "# area_ms_max=%.3f\n", max_area_ms);
    fprintf(file, "# area_load_ms_max=%.3f\n", max_area_load_ms);
    fprintf(file,
            "frame,elapsed_ms,zoom,direction,frame_ms,"
            "road_ms,road_load_ms,road_hits,road_misses,road_prewarm_loads,"
            "road_draw_loads,road_deferred_loads,road_prewarm_zoom,road_tiles,"
            "road_segments,road_batches,area_ms,area_load_ms,area_tiles,"
            "area_triangles,area_batches,area_prewarm_loads,area_draw_loads,"
            "area_deferred_loads\n");

    for (size_t i = 0U; i < test->sample_count; ++i) {
        const OpenRideMapZoomTestSample *s = &test->samples[i];
        fprintf(file,
                "%zu,%.3f,%.5f,%d,%.3f,"
                "%.3f,%.3f,%u,%u,%u,%u,%u,%d,%u,%u,%u,"
                "%.3f,%.3f,%u,%u,%u,%u,%u,%u\n",
                i,
                s->elapsed_ms,
                s->zoom,
                s->direction,
                s->frame_ms,
                s->road.roads_ms,
                s->road.load_ms,
                (unsigned)s->road.cache_hits,
                (unsigned)s->road.cache_misses,
                (unsigned)s->road.prewarm_loads,
                (unsigned)s->road.draw_loads,
                (unsigned)s->road.deferred_loads,
                s->road.prewarm_zoom,
                (unsigned)s->road.tiles_visited,
                (unsigned)s->road.segments_drawn,
                (unsigned)s->road.batches,
                s->area.areas_ms,
                s->area.load_ms,
                (unsigned)s->area.tiles_visited,
                (unsigned)s->area.triangles_drawn,
                (unsigned)s->area.batches,
                (unsigned)s->area.prewarm_loads,
                (unsigned)s->area.draw_loads,
                (unsigned)s->area.deferred_loads);
    }

    const bool ok = fclose(file) == 0;
    free(frame_times);
    return ok;
}

bool openride_map_zoom_test_start(OpenRideMapZoomTest *test,
                                  OpenRideMapCamera *camera,
                                  const OpenRidePlatformPaths *paths)
{
    if (!test || !camera || !paths) return false;

    openride_map_zoom_test_cancel(test);
    test->samples = calloc(OPENRIDE_MAP_ZOOM_TEST_MAX_SAMPLES,
                           sizeof(*test->samples));
    if (!test->samples) return false;
    if (!openride_platform_path_join(test->output_path,
                                     sizeof(test->output_path),
                                     paths->data_dir,
                                     OPENRIDE_MAP_ZOOM_TEST_LOG_FILENAME)) {
        release_samples(test);
        return false;
    }

    /* Remove a stale result before the benchmark starts. No benchmark frame
     * exists yet, so this filesystem operation cannot contaminate timings. */
    (void)remove(test->output_path);

    test->active = true;
    test->direction = 1;
    test->zoom = OPENRIDE_MAP_ZOOM_TEST_MIN;
    camera->center_lat = OPENRIDE_MAP_ZOOM_TEST_LAT;
    camera->center_lon = OPENRIDE_MAP_ZOOM_TEST_LON;
    camera->zoom = test->zoom;
    camera->bearing_deg = 0.0;
    return true;
}

void openride_map_zoom_test_update(OpenRideMapZoomTest *test,
                                   OpenRideMapCamera *camera,
                                   double delta_seconds)
{
    if (!test || !camera || !test->active || test->direction == 0) return;
    if (delta_seconds < 0.0) delta_seconds = 0.0;
    const double delta = OPENRIDE_MAP_ZOOM_TEST_SPEED * delta_seconds;
    test->zoom += (double)test->direction * delta;
    if (test->direction > 0 && test->zoom >= OPENRIDE_MAP_ZOOM_TEST_MAX) {
        test->zoom = OPENRIDE_MAP_ZOOM_TEST_MAX;
        test->direction = -1;
    } else if (test->direction < 0
               && test->zoom <= OPENRIDE_MAP_ZOOM_TEST_MIN) {
        test->zoom = OPENRIDE_MAP_ZOOM_TEST_MIN;
        test->direction = 0;
    }
    camera->center_lat = OPENRIDE_MAP_ZOOM_TEST_LAT;
    camera->center_lon = OPENRIDE_MAP_ZOOM_TEST_LON;
    camera->zoom = test->zoom;
    camera->bearing_deg = 0.0;
}

void openride_map_zoom_test_cancel(OpenRideMapZoomTest *test)
{
    if (!test) return;
    release_samples(test);
    test->active = false;
    test->direction = 0;
    test->zoom = OPENRIDE_MAP_ZOOM_TEST_MIN;
}

void openride_map_zoom_test_destroy(OpenRideMapZoomTest *test)
{
    openride_map_zoom_test_cancel(test);
}

bool openride_map_zoom_test_record_present(
    OpenRideMapZoomTest *test,
    uint64_t present_ns,
    const OpenRideORMapRoadDebugStats *road,
    const OpenRideORMapAreaDebugStats *area,
    char *status,
    size_t status_size)
{
    if (!test || !test->active) return false;

    if (test->last_present_ns == 0U) {
        test->first_present_ns = present_ns;
        test->last_present_ns = present_ns;
    } else {
        if (test->sample_count < OPENRIDE_MAP_ZOOM_TEST_MAX_SAMPLES) {
            OpenRideMapZoomTestSample *sample = &test->samples[test->sample_count++];
            sample->elapsed_ms =
                (double)(present_ns - test->first_present_ns) / 1000000.0;
            sample->frame_ms =
                (double)(present_ns - test->last_present_ns) / 1000000.0;
            sample->zoom = test->zoom;
            sample->direction = test->direction;
            if (road) sample->road = *road;
            if (area) sample->area = *area;
        } else {
            ++test->dropped_samples;
        }
        test->last_present_ns = present_ns;
    }

    if (test->direction != 0) return false;

    const bool written = write_results(test);
    if (status && status_size > 0U) {
        snprintf(status,
                 status_size,
                 written
                     ? "test zoom termine: data/%s (%zu frames)"
                     : "test zoom termine mais log impossible (%zu frames)",
                 written ? OPENRIDE_MAP_ZOOM_TEST_LOG_FILENAME : "",
                 test->sample_count);
    }
    release_samples(test);
    test->active = false;
    return true;
}
