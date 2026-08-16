#include "app_search_runtime.h"

#include <stdio.h>
#include <string.h>

#define OPENRIDE_SEARCH_MAX_RESULTS 8U
#define OPENRIDE_APP_LIST_MAX 12U

void openride_app_search_utf8_backspace(char *text)
{
    if (!text || text[0] == '\0') return;
    size_t length = strlen(text);
    do {
        --length;
    } while (length > 0U && (((unsigned char)text[length] & 0xC0U) == 0x80U));
    text[length] = '\0';
}

bool openride_app_search_refresh(OpenRidePlaceWorld *world,
                                 const char *query,
                                 OpenRidePlaceSearchResult *results,
                                 uint32_t *result_count,
                                 uint32_t *selected_result,
                                 char *status,
                                 size_t status_size)
{
    char error[192] = {0};
    uint32_t count = 0U;
    const bool ok = openride_place_world_search(world,
                                                 query,
                                                 results,
                                                 OPENRIDE_SEARCH_MAX_RESULTS,
                                                 &count,
                                                 error,
                                                 sizeof(error));
    if (!ok) {
        if (status && status_size > 0U) {
            snprintf(status,
                     status_size,
                     "recherche impossible: %.150s",
                     error[0] ? error : "erreur inconnue");
        }
        return false;
    }
    if (result_count) *result_count = count;
    if (selected_result && (*selected_result >= count || count == 0U)) *selected_result = 0U;
    return true;
}

void openride_app_search_set_destination_from_place(OpenRideMapSelection *selection,
                                       const OpenRideGPSSample *gps,
                                       bool gps_valid,
                                       double lat,
                                       double lon,
                                       const char *name,
                                       bool *route_dirty,
                                       char *status,
                                       size_t status_size)
{
    if (!selection) return;
#ifdef __ANDROID__
    if (gps_valid && gps) {
        openride_map_selection_set(selection,
                                   OPENRIDE_MARKER_START,
                                   gps->lat,
                                   gps->lon);
    }
#else
    (void)gps;
    (void)gps_valid;
#endif
    openride_map_selection_set(selection, OPENRIDE_MARKER_DESTINATION, lat, lon);
    if (route_dirty) *route_dirty = openride_map_selection_complete(selection);
    if (status && status_size > 0U) {
        snprintf(status,
                 status_size,
                 openride_map_selection_complete(selection)
                     ? "destination %.120s | calcul itineraire"
                     : "destination %.120s | choisis le depart",
                 name && name[0] ? name : "selectionnee");
    }
}

void openride_app_search_refresh_stored_places(OpenRideAppStorage *storage,
                                  bool favorites,
                                  OpenRideStoredPlace *places,
                                  uint32_t *count)
{
    char error[160] = {0};
    if (!storage || !places || !count) return;
    *count = 0U;
    if (favorites) {
        openride_app_storage_list_favorites(storage,
                                            places,
                                            OPENRIDE_APP_LIST_MAX,
                                            count,
                                            error,
                                            sizeof(error));
    } else {
        openride_app_storage_list_history(storage,
                                          places,
                                          OPENRIDE_APP_LIST_MAX,
                                          count,
                                          error,
                                          sizeof(error));
    }
}

void openride_app_search_open(SDL_Window *window,
                              OpenRidePlaceWorld *place_world,
                              bool *active,
                              char *query,
                              uint32_t *result_count,
                              uint32_t *selected,
                              char *status,
                              size_t status_size)
{
    if (!active || !query || !result_count || !selected) return;
    if (!place_world) {
        snprintf(status, status_size,
                 "recherche hors ligne indisponible");
        return;
    }
    *active = true;
    query[0] = '\0';
    *result_count = 0U;
    *selected = 0U;
    /* Text input is layout-aware: important for '/' on AZERTY keyboards. */
    SDL_StartTextInput(window);
}

