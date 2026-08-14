#include "openride/france_regions_lite.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect_region(double lat, double lon, const char *expected)
{
    const char *actual = openride_france_regions_lite_region_id(lat, lon);
    assert(actual != NULL);
    assert(strcmp(actual, expected) == 0);
}

int main(void)
{
    assert(openride_france_regions_lite_region_count() == 27U);
    assert(openride_france_regions_lite_ring_count() >= 27U);
    assert(openride_france_regions_lite_point_count() > 100U);

    expect_region(50.3708, 3.0802, "nord-pas-de-calais"); /* Douai */
    expect_region(49.1829, -0.3707, "basse-normandie");   /* Caen */
    expect_region(49.4432, 1.0993, "haute-normandie");   /* Rouen */
    expect_region(48.8566, 2.3522, "ile-de-france");     /* Paris */
    expect_region(44.8378, -0.5792, "aquitaine");        /* Bordeaux */
    expect_region(43.6047, 1.4442, "midi-pyrenees");     /* Toulouse */
    expect_region(43.2965, 5.3698,
                  "provence-alpes-cote-d-azur");          /* Marseille */
    expect_region(48.5734, 7.7521, "alsace");             /* Strasbourg */
    expect_region(41.9192, 8.7386, "corse");              /* Ajaccio */
    expect_region(4.9224, -52.3135, "guyane");            /* Cayenne */

    /* Clearly outside the French regional extracts. */
    assert(openride_france_regions_lite_region_id(51.5074, -0.1278) == NULL);

    puts("FranceRegionsLite tests: OK");
    return 0;
}
