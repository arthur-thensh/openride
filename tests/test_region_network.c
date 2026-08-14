#include "openride/region_network.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int catalog_index(const char *id)
{
    for (size_t i = 0U; i < openride_region_count(); ++i) {
        const OpenRideRegionDefinition *region = openride_region_at(i);
        if (region && strcmp(region->id, id) == 0) return (int)i;
    }
    return -1;
}

static bool corridor_contains(const OpenRideRegionCorridor *corridor,
                              const char *id)
{
    if (!corridor || !id) return false;
    for (uint32_t i = 0U; i < corridor->count; ++i) {
        if (corridor->regions[i]
            && strcmp(corridor->regions[i]->id, id) == 0) {
            return true;
        }
    }
    return false;
}

int main(void)
{
    const OpenRideRegionDefinition *npdc =
        openride_region_find("nord-pas-de-calais");
    const OpenRideRegionDefinition *picardie =
        openride_region_find("picardie");
    const OpenRideRegionDefinition *idf =
        openride_region_find("ile-de-france");
    const OpenRideRegionDefinition *centre =
        openride_region_find("centre");
    const OpenRideRegionDefinition *bourgogne =
        openride_region_find("bourgogne");

    assert(npdc && picardie && idf && centre && bourgogne);
    assert(openride_region_network_adjacent(npdc, picardie));
    assert(!openride_region_network_adjacent(npdc, bourgogne));

    double lat = 0.0;
    double lon = 0.0;
    assert(openride_region_network_center(picardie, &lat, &lon));
    assert(lat > 49.0 && lat < 50.0);

    bool installed[OPENRIDE_REGION_NETWORK_MAX_REGIONS] = {false};
    assert(openride_region_count() <= OPENRIDE_REGION_NETWORK_MAX_REGIONS);

    const int npdc_i = catalog_index("nord-pas-de-calais");
    const int picardie_i = catalog_index("picardie");
    const int idf_i = catalog_index("ile-de-france");
    const int centre_i = catalog_index("centre");
    const int bourgogne_i = catalog_index("bourgogne");
    assert(npdc_i >= 0 && picardie_i >= 0 && idf_i >= 0
           && centre_i >= 0 && bourgogne_i >= 0);

    OpenRideRegionNetworkPlan unrestricted;
    char error[256] = {0};

    /*
     * Recommended corridor is independent of installed state. The exact
     * middle choice is heuristic, so assert topology rather than one hard-coded
     * Paris/Champagne choice.
     */
    assert(openride_region_network_plan(
        npdc,
        50.3708,
        3.0802,
        bourgogne,
        47.3220,
        5.0415,
        NULL,
        0U,
        &unrestricted,
        error,
        sizeof(error)));
    assert(unrestricted.recommended.count >= 3U);
    assert(unrestricted.recommended.regions[0] == npdc);
    assert(unrestricted.recommended.regions[
        unrestricted.recommended.count - 1U] == bourgogne);
    assert(corridor_contains(&unrestricted.recommended, "picardie"));
    assert(unrestricted.missing_count == 0U);
    assert(!unrestricted.has_installed_alternative);

    /*
     * Build an installed-only fallback deliberately different from the
     * recommended path: NPdC -> Picardie -> IDF -> Centre -> Bourgogne.
     * The recommended route must not change because of this mask.
     */
    installed[npdc_i] = true;
    installed[picardie_i] = true;
    installed[idf_i] = true;
    installed[centre_i] = true;
    installed[bourgogne_i] = true;

    OpenRideRegionNetworkPlan with_availability;
    assert(openride_region_network_plan(
        npdc,
        50.3708,
        3.0802,
        bourgogne,
        47.3220,
        5.0415,
        installed,
        openride_region_count(),
        &with_availability,
        error,
        sizeof(error)));

    assert(with_availability.recommended.count
           == unrestricted.recommended.count);
    for (uint32_t i = 0U; i < unrestricted.recommended.count; ++i) {
        assert(strcmp(with_availability.recommended.regions[i]->id,
                      unrestricted.recommended.regions[i]->id) == 0);
    }

    if (with_availability.missing_count > 0U) {
        assert(with_availability.has_installed_alternative);
        assert(with_availability.installed_alternative.count >= 3U);
        assert(with_availability.installed_alternative.regions[0] == npdc);
        assert(with_availability.installed_alternative.regions[
            with_availability.installed_alternative.count - 1U] == bourgogne);
        assert(corridor_contains(&with_availability.installed_alternative,
                                 "ile-de-france"));

        /*
         * Do not hard-code Centre here: Ile-de-France is directly adjacent to
         * Bourgogne in the built-in regional topology, so the installed-only
         * optimum may legitimately be:
         *
         * NPdC -> Picardie -> Ile-de-France -> Bourgogne
         *
         * The contract we actually need to protect is that every region in the
         * fallback is installed and that the recommended corridor itself did
         * not change because of download state.
         */
        for (uint32_t i = 0U;
             i < with_availability.installed_alternative.count;
             ++i) {
            const OpenRideRegionDefinition *region =
                with_availability.installed_alternative.regions[i];
            const int index = catalog_index(region->id);
            assert(index >= 0);
            assert(installed[index]);
        }
    }

    puts("Region network tests: OK");
    return 0;
}
