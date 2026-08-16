#ifndef OPENRIDE_FRANCE_LITE_H
#define OPENRIDE_FRANCE_LITE_H

#include "openride/place_search.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Tiny built-in France-wide place catalog.
 *
 * It makes important destinations searchable before their regional
 * .orplaces.sqlite has ever been downloaded and carries the historical
 * Geofabrik region id for the route planner.
 */
typedef struct OpenRideFranceLitePlace {
    const char *name;
    double lat;
    double lon;
    OpenRidePlaceKind kind;
    int rank;
    const char *region_id;
} OpenRideFranceLitePlace;

size_t openride_france_lite_place_count(void);
const OpenRideFranceLitePlace *openride_france_lite_place_at(size_t index);

bool openride_france_lite_search(const char *query,
                                 OpenRidePlaceSearchResult *results,
                                 uint32_t max_results,
                                 uint32_t *result_count,
                                 char *error,
                                 size_t error_size);

#endif
