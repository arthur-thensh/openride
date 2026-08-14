#include "openride/osm_import.h"
#include "openride/place_world.h"
#include "openride/platform_paths.h"
#include "openride/region_manager.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    assert(argc == 2);

    char error[256] = {0};
    OpenRidePlatformPaths paths;
    assert(openride_platform_paths_init(&paths,
                                        OPENRIDE_PLATFORM_DESKTOP,
                                        "test-place-world-data",
                                        error,
                                        sizeof(error)));
    assert(openride_platform_paths_ensure_directories(&paths,
                                                       error,
                                                       sizeof(error)));

    const OpenRideRegionDefinition *a = openride_region_at(0U);
    const OpenRideRegionDefinition *b = openride_region_at(1U);
    assert(a != NULL);
    assert(b != NULL);

    char db_a[512];
    char db_b[512];
    assert(openride_platform_path_join(db_a,
                                       sizeof(db_a),
                                       paths.search_dir,
                                       a->search_filename));
    assert(openride_platform_path_join(db_b,
                                       sizeof(db_b),
                                       paths.search_dir,
                                       b->search_filename));
    remove(db_a);
    remove(db_b);

    OpenRideOSMPlaceImportStats stats = {0};
    assert(openride_osm_pbf_import_places(argv[1],
                                          db_a,
                                          &stats,
                                          error,
                                          sizeof(error)));
    memset(&stats, 0, sizeof(stats));
    assert(openride_osm_pbf_import_places(argv[1],
                                          db_b,
                                          &stats,
                                          error,
                                          sizeof(error)));

    OpenRidePlaceWorld *world = openride_place_world_create(&paths,
                                                             error,
                                                             sizeof(error));
    assert(world != NULL);
    assert(openride_place_world_region_count(world) == 2U);

    OpenRidePlaceSearchResult results[8];
    uint32_t count = 0U;
    assert(openride_place_world_search(world,
                                       "etaples",
                                       results,
                                       8U,
                                       &count,
                                       error,
                                       sizeof(error)));
    assert(count == 1U);
    assert(strcmp(results[0].name, "Étaples Test") == 0);

    assert(openride_place_world_search(world,
                                       "camping",
                                       results,
                                       8U,
                                       &count,
                                       error,
                                       sizeof(error)));
    assert(count == 1U);
    assert(results[0].kind == OPENRIDE_PLACE_CAMP_SITE);

    openride_place_world_destroy(world);
    remove(db_a);
    remove(db_b);

    puts("PlaceWorld tests: OK");
    return 0;
}
