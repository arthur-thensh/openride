#include "openride/ormap.h"
#include "openride/platform_paths.h"
#include "openride/region_install.h"
#include "openride/region_manager.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void copy_file(const char *source, const char *destination)
{
    FILE *in = fopen(source, "rb");
    assert(in != NULL);
    FILE *out = fopen(destination, "wb");
    assert(out != NULL);
    unsigned char buffer[8192];
    size_t n = 0U;
    while ((n = fread(buffer, 1U, sizeof(buffer), in)) > 0U) {
        assert(fwrite(buffer, 1U, n, out) == n);
    }
    assert(ferror(in) == 0);
    fclose(in);
    fclose(out);
}

typedef struct ProgressLog {
    OpenRideRegionPrepareStage stages[8];
    size_t count;
} ProgressLog;

static void record_progress(OpenRideRegionPrepareStage stage,
                            const char *message,
                            void *userdata)
{
    ProgressLog *log = userdata;
    assert(log);
    assert(message && message[0] != '\0');
    assert(log->count < sizeof(log->stages) / sizeof(log->stages[0]));
    log->stages[log->count++] = stage;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    char root[256];
    snprintf(root, sizeof(root), "/tmp/openride-region-install-%ld", (long)getpid());
    char command[320];
    snprintf(command, sizeof(command), "rm -rf '%s'", root);
    (void)system(command);
    assert(mkdir(root, 0700) == 0);

    OpenRidePlatformPaths paths;
    char error[512] = {0};
    assert(openride_platform_paths_init(&paths,
                                        OPENRIDE_PLATFORM_DESKTOP,
                                        root,
                                        error,
                                        sizeof(error)));
    assert(openride_platform_paths_ensure_directories(&paths, error, sizeof(error)));

    const OpenRideRegionDefinition region = {
        .id = "tiny",
        .name = "Tiny test region",
        .ormap_filename = "tiny.ormap",
        .legacy_map_filename = "tiny-legacy.mbtiles",
        .routing_filename = "tiny.orgraph",
        .search_filename = "tiny.orplaces.sqlite",
        .pbf_filename = "tiny.osm.pbf",
        .pbf_url = "https://invalid.example/tiny.osm.pbf"
    };

    char pbf_path[512];
    assert(openride_platform_path_join(pbf_path,
                                       sizeof(pbf_path),
                                       paths.downloads_dir,
                                       region.pbf_filename));

    FILE *empty_pbf = fopen(pbf_path, "wb");
    assert(empty_pbf != NULL);
    fclose(empty_pbf);
    OpenRideRegionPrepareStats empty_stats = {0};
    ProgressLog empty_progress = {0};
    assert(!openride_region_prepare_from_pbf(&paths,
                                             &region,
                                             false,
                                             record_progress,
                                             &empty_progress,
                                             &empty_stats,
                                             error,
                                             sizeof(error)));
    assert(strstr(error, "PBF is empty") != NULL);
    assert(empty_progress.count == 0U);

    OpenRideRegionStatus empty_status;
    assert(openride_region_get_status(
        &paths, &region, &empty_status, error, sizeof(error)));
    assert(empty_status.source_pbf_present);
    assert(!empty_status.ormap_installed);
    assert(!empty_status.ormap11_installed);
    assert(!empty_status.routing_installed);
    assert(!empty_status.search_installed);

    copy_file(argv[1], pbf_path);

    OpenRideRegionPrepareStats stats = {0};
    ProgressLog progress = {0};
    assert(openride_region_prepare_from_pbf(&paths,
                                            &region,
                                            true,
                                            record_progress,
                                            &progress,
                                            &stats,
                                            error,
                                            sizeof(error)));
    assert(stats.routing.graph_node_count > 0U);
    assert(stats.map.road_tiles_written > 0U);
    assert(progress.count == 6U);
    assert(progress.stages[0] == OPENRIDE_REGION_PREPARE_ROUTING);
    assert(progress.stages[1] == OPENRIDE_REGION_PREPARE_SEARCH);
    assert(progress.stages[2] == OPENRIDE_REGION_PREPARE_MAP);
    assert(progress.stages[3] == OPENRIDE_REGION_PREPARE_PYRAMID);
    assert(progress.stages[4] == OPENRIDE_REGION_PREPARE_FINALIZING);
    assert(progress.stages[5] == OPENRIDE_REGION_PREPARE_COMPLETE);

    OpenRideRegionStatus status;
    assert(openride_region_get_status(&paths, &region, &status, error, sizeof(error)));
    assert(status.ormap_installed);
    assert(status.ormap_current);
    assert(status.ormap11_installed);
    assert(strstr(status.ormap11_path, "tiny.ormap11") != NULL);
    assert(status.routing_installed);
    assert(status.search_installed);
    assert(status.source_pbf_present);

    OpenRideORMap *map = openride_ormap_open(status.ormap_path, error, sizeof(error));
    assert(map != NULL);
    const OpenRideORMapMetadata *metadata = openride_ormap_metadata(map);
    assert(metadata != NULL);
    assert(metadata->format_version == (int)OPENRIDE_ORMAP_FORMAT_VERSION);
    openride_ormap_close(map);

    OpenRideORMapPyramidSurfaceMap *pyramid =
        openride_ormap_pyramid_surface_open(
            status.ormap11_path, error, sizeof(error));
    assert(pyramid != NULL);
    const OpenRideORMapPyramidSurfaceMetadata *pyramid_metadata =
        openride_ormap_pyramid_surface_metadata(pyramid);
    assert(pyramid_metadata != NULL);
    assert(pyramid_metadata->format_version
           == (int)OPENRIDE_ORMAP_PYRAMID_FORMAT_VERSION);
    openride_ormap_pyramid_surface_close(pyramid);

    OpenRideORMapPyramidOverlayInspectStats overlay = {0};
    assert(openride_ormap_pyramid_overlay_inspect(
        status.ormap11_path, &overlay, error, sizeof(error)));
    assert(overlay.malformed_tiles == 0U);
    assert(overlay.invalid_records == 0U);

    assert(remove(status.ormap11_path) == 0);
    assert(openride_region_get_status(
        &paths, &region, &status, error, sizeof(error)));
    assert(openride_region_status_ready(&status));
    assert(!status.ormap11_installed);
    assert(status.source_pbf_present);

    OpenRideRegionPrepareStats retry_stats = {0};
    ProgressLog retry_progress = {0};
    assert(openride_region_prepare_from_pbf(&paths,
                                            &region,
                                            false,
                                            record_progress,
                                            &retry_progress,
                                            &retry_stats,
                                            error,
                                            sizeof(error)));
    assert(retry_progress.count == 3U);
    assert(retry_progress.stages[0] == OPENRIDE_REGION_PREPARE_PYRAMID);
    assert(retry_progress.stages[1] == OPENRIDE_REGION_PREPARE_FINALIZING);
    assert(retry_progress.stages[2] == OPENRIDE_REGION_PREPARE_COMPLETE);
    assert(retry_stats.routing.graph_node_count == 0U);
    assert(retry_stats.map.road_tiles_written == 0U);

    assert(openride_region_get_status(
        &paths, &region, &status, error, sizeof(error)));
    assert(status.ormap11_installed);
    assert(!status.source_pbf_present);

    assert(openride_region_remove_generated(
        &paths, &region, error, sizeof(error)));
    assert(!openride_platform_file_exists(status.ormap_path));
    assert(!openride_platform_file_exists(status.ormap11_path));
    assert(!openride_platform_file_exists(status.routing_path));
    assert(!openride_platform_file_exists(status.search_path));

    snprintf(command, sizeof(command), "rm -rf '%s'", root);
    (void)system(command);
    puts("Region install tests: OK");
    return 0;
}
