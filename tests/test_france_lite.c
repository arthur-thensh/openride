#include "openride/france_lite.h"
#include "openride/region_manager.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect_region(const char *query,
                          const char *expected_name,
                          const char *expected_region)
{
    OpenRidePlaceSearchResult results[8];
    uint32_t count = 0U;
    char error[256] = {0};

    assert(openride_france_lite_search(query,
                                       results,
                                       8U,
                                       &count,
                                       error,
                                       sizeof(error)));
    assert(count > 0U);
    assert(strcmp(results[0].name, expected_name) == 0);
    assert(strcmp(results[0].region_id, expected_region) == 0);
    assert(results[0].bundled_lite);
    assert(openride_region_find(results[0].region_id) != NULL);
}

int main(void)
{
    assert(openride_france_lite_place_count() >= 80U);

    expect_region("bordeaux", "Bordeaux", "aquitaine");
    expect_region("caen", "Caen", "basse-normandie");
    expect_region("douai", "Douai", "nord-pas-de-calais");
    expect_region("perigueux", "Périgueux", "aquitaine");
    expect_region("strasbourg", "Strasbourg", "alsace");
    expect_region("ajaccio", "Ajaccio", "corse");

    puts("France-lite tests: OK");
    return 0;
}
