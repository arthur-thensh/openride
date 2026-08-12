#include "openride/osm_import.h"
#include "openride/place_search.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *database = "test-openride-places.sqlite";
    remove(database);

    char error[256] = {0};
    OpenRideOSMPlaceImportStats stats = {0};
    assert(openride_osm_pbf_import_places(argv[1],
                                          database,
                                          &stats,
                                          error,
                                          sizeof(error)));
    assert(stats.osm_node_count == 4U);
    assert(stats.indexed_place_count == 4U);

    OpenRidePlaceIndex *index = openride_place_index_open(database,
                                                           error,
                                                           sizeof(error));
    assert(index != NULL);

    OpenRidePlaceSearchResult results[8];
    uint32_t count = 0U;
    assert(openride_place_index_search(index,
                                       "etaples",
                                       results,
                                       8U,
                                       &count,
                                       error,
                                       sizeof(error)));
    assert(count >= 1U);
    assert(strcmp(results[0].name, "Étaples Test") == 0);
    assert(results[0].kind == OPENRIDE_PLACE_VILLAGE);

    assert(openride_place_index_search(index,
                                       "station",
                                       results,
                                       8U,
                                       &count,
                                       error,
                                       sizeof(error)));
    assert(count == 1U);
    assert(results[0].kind == OPENRIDE_PLACE_FUEL);

    assert(openride_place_index_search(index,
                                       "camping",
                                       results,
                                       8U,
                                       &count,
                                       error,
                                       sizeof(error)));
    assert(count == 1U);
    assert(results[0].kind == OPENRIDE_PLACE_CAMP_SITE);

    char normalized[64];
    assert(openride_place_normalize("Étaples-sur-Mer", normalized, sizeof(normalized)));
    assert(strcmp(normalized, "etaples sur mer") == 0);

    openride_place_index_close(index);
    remove(database);
    puts("Place search tests: OK");
    return 0;
}
