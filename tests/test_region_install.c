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
    copy_file(argv[1], pbf_path);

    OpenRideRegionPrepareStats stats = {0};
    assert(openride_region_prepare_from_pbf(&paths,
                                            &region,
                                            false,
                                            NULL,
                                            NULL,
                                            &stats,
                                            error,
                                            sizeof(error)));
    assert(stats.routing.graph_node_count > 0U);
    assert(stats.map.road_tiles_written > 0U);

    OpenRideRegionStatus status;
    assert(openride_region_get_status(&paths, &region, &status, error, sizeof(error)));
    assert(status.ormap_installed);
    assert(status.routing_installed);
    assert(status.search_installed);
    assert(!status.source_pbf_present);

    OpenRideORMap *map = openride_ormap_open(status.ormap_path, error, sizeof(error));
    assert(map != NULL);
    openride_ormap_close(map);

    snprintf(command, sizeof(command), "rm -rf '%s'", root);
    (void)system(command);
    puts("Region install tests: OK");
    return 0;
}
