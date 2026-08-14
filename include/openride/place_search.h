#ifndef OPENRIDE_PLACE_SEARCH_H
#define OPENRIDE_PLACE_SEARCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum OpenRidePlaceKind {
    OPENRIDE_PLACE_UNKNOWN = 0,
    OPENRIDE_PLACE_CITY,
    OPENRIDE_PLACE_TOWN,
    OPENRIDE_PLACE_VILLAGE,
    OPENRIDE_PLACE_HAMLET,
    OPENRIDE_PLACE_SUBURB,
    OPENRIDE_PLACE_QUARTER,
    OPENRIDE_PLACE_FUEL,
    OPENRIDE_PLACE_CAMP_SITE,
    OPENRIDE_PLACE_VIEWPOINT,
    OPENRIDE_PLACE_MOTORCYCLE_SHOP
} OpenRidePlaceKind;

typedef struct OpenRidePlaceSearchResult {
    int64_t osm_id;
    double lat;
    double lon;
    OpenRidePlaceKind kind;
    int rank;
    char name[128];
    char region_id[64];
    bool bundled_lite;
} OpenRidePlaceSearchResult;

typedef struct OpenRidePlaceIndex OpenRidePlaceIndex;

OpenRidePlaceIndex *openride_place_index_open(const char *path,
                                               char *error,
                                               size_t error_size);
void openride_place_index_close(OpenRidePlaceIndex *index);

bool openride_place_index_search(OpenRidePlaceIndex *index,
                                 const char *query,
                                 OpenRidePlaceSearchResult *results,
                                 uint32_t max_results,
                                 uint32_t *result_count,
                                 char *error,
                                 size_t error_size);

const char *openride_place_kind_name(OpenRidePlaceKind kind);

/*
 * Convert UTF-8 place names to a compact comparison key suitable for offline
 * search. Common French/Western-European accents are folded to ASCII, text is
 * lower-cased and punctuation is reduced to spaces.
 */
bool openride_place_normalize(const char *text,
                              char *normalized,
                              size_t normalized_size);

#endif
