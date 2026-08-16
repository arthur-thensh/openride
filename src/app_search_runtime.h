#ifndef OPENRIDE_APP_SEARCH_RUNTIME_H
#define OPENRIDE_APP_SEARCH_RUNTIME_H

#include <SDL3/SDL.h>

#include "openride/app_storage.h"
#include "openride/gps_simulator.h"
#include "openride/map_selection.h"
#include "openride/place_search.h"
#include "openride/place_world.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void openride_app_search_utf8_backspace(char *text);

bool openride_app_search_refresh(
    OpenRidePlaceWorld *world,
    const char *query,
    OpenRidePlaceSearchResult *results,
    uint32_t *result_count,
    uint32_t *selected_result,
    char *status,
    size_t status_size);

void openride_app_search_set_destination_from_place(
    OpenRideMapSelection *selection,
    const OpenRideGPSSample *gps,
    bool gps_valid,
    double lat,
    double lon,
    const char *name,
    bool *route_dirty,
    char *status,
    size_t status_size);

void openride_app_search_refresh_stored_places(
    OpenRideAppStorage *storage,
    bool favorites,
    OpenRideStoredPlace *places,
    uint32_t *count);

void openride_app_search_open(
    SDL_Window *window,
    OpenRidePlaceWorld *place_world,
    bool *active,
    char *query,
    uint32_t *result_count,
    uint32_t *selected,
    char *status,
    size_t status_size);

#endif
