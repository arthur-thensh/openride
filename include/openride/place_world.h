#ifndef OPENRIDE_PLACE_WORLD_H
#define OPENRIDE_PLACE_WORLD_H

#include "openride/place_search.h"
#include "openride/platform_paths.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct OpenRidePlaceWorld OpenRidePlaceWorld;

OpenRidePlaceWorld *openride_place_world_create(
    const OpenRidePlatformPaths *paths,
    char *error,
    size_t error_size);

void openride_place_world_destroy(OpenRidePlaceWorld *world);

bool openride_place_world_refresh(
    OpenRidePlaceWorld *world,
    char *error,
    size_t error_size);

size_t openride_place_world_region_count(const OpenRidePlaceWorld *world);

bool openride_place_world_search(
    OpenRidePlaceWorld *world,
    const char *query,
    OpenRidePlaceSearchResult *results,
    uint32_t max_results,
    uint32_t *result_count,
    char *error,
    size_t error_size);

#endif
