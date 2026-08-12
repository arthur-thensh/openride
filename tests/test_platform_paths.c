#include "openride/platform_paths.h"
#include "openride/region_manager.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    OpenRidePlatformPaths paths;
    char error[160] = {0};
    assert(openride_platform_paths_init(&paths,
                                        OPENRIDE_PLATFORM_DESKTOP,
                                        ".",
                                        error,
                                        sizeof(error)));
    assert(strcmp(paths.data_dir, "./data") == 0);
    assert(strcmp(paths.maps_dir, "./data/maps") == 0);
    assert(strcmp(paths.app_storage_path, "./data/openride-app.sqlite") == 0);

    char joined[64];
    assert(openride_platform_path_join(joined, sizeof(joined), "a/b", "c.dat"));
    assert(strcmp(joined, "a/b/c.dat") == 0);

    OpenRideRegionStatus status;
    assert(openride_region_get_status(&paths,
                                      openride_region_default(),
                                      &status,
                                      error,
                                      sizeof(error)));
    assert(strstr(status.ormap_path, "nord-pas-de-calais.ormap") != NULL);
    assert(strstr(status.legacy_map_path, "nord-pas-de-calais-shortbread.mbtiles") != NULL);

    if (status.ormap_installed) {
        assert(strcmp(status.map_path, status.ormap_path) == 0);
    } else if (status.legacy_map_installed) {
        assert(strcmp(status.map_path, status.legacy_map_path) == 0);
    } else {
        assert(status.map_path[0] == '\0');
    }
    assert(strstr(status.routing_path, "nord-pas-de-calais.orgraph") != NULL);
    assert(strstr(status.search_path, "nord-pas-de-calais.orplaces.sqlite") != NULL);

    puts("Platform paths tests: OK");
    return 0;
}
