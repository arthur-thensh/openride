#include "openride/region_manager.h"

#include <stdio.h>
#include <string.h>

static const OpenRideRegionDefinition DEFAULT_REGION = {
    .id = "nord-pas-de-calais",
    .name = "Nord-Pas-de-Calais",
    .map_filename = "nord-pas-de-calais-shortbread.mbtiles",
    .routing_filename = "nord-pas-de-calais.orgraph",
    .search_filename = "nord-pas-de-calais.orplaces.sqlite"
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "unknown error");
}

const OpenRideRegionDefinition *openride_region_default(void)
{
    return &DEFAULT_REGION;
}

bool openride_region_get_status(const OpenRidePlatformPaths *paths,
                                const OpenRideRegionDefinition *region,
                                OpenRideRegionStatus *status,
                                char *error,
                                size_t error_size)
{
    if (!paths || !region || !status) {
        set_error(error, error_size, "invalid region status arguments");
        return false;
    }

    memset(status, 0, sizeof(*status));
    if (!openride_platform_path_join(status->map_path,
                                     sizeof(status->map_path),
                                     paths->maps_dir,
                                     region->map_filename)
        || !openride_platform_path_join(status->routing_path,
                                        sizeof(status->routing_path),
                                        paths->routing_dir,
                                        region->routing_filename)
        || !openride_platform_path_join(status->search_path,
                                        sizeof(status->search_path),
                                        paths->search_dir,
                                        region->search_filename)) {
        set_error(error, error_size, "region path is too long");
        return false;
    }

    status->map_size_mb = openride_platform_file_size_mb(status->map_path);
    status->routing_size_mb = openride_platform_file_size_mb(status->routing_path);
    status->search_size_mb = openride_platform_file_size_mb(status->search_path);
    status->map_installed = status->map_size_mb >= 0.0;
    status->routing_installed = status->routing_size_mb >= 0.0;
    status->search_installed = status->search_size_mb >= 0.0;
    status->total_size_mb = (status->map_installed ? status->map_size_mb : 0.0)
        + (status->routing_installed ? status->routing_size_mb : 0.0)
        + (status->search_installed ? status->search_size_mb : 0.0);

    set_error(error, error_size, "");
    return true;
}
