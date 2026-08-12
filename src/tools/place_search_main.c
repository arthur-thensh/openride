#include "openride/place_search.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s index.sqlite recherche [max]\n", argv[0]);
        return 2;
    }

    unsigned max_results = 10U;
    if (argc >= 4) {
        unsigned parsed = 0U;
        if (sscanf(argv[3], "%u", &parsed) == 1 && parsed > 0U && parsed <= 100U) {
            max_results = parsed;
        }
    }

    char error[256] = {0};
    OpenRidePlaceIndex *index = openride_place_index_open(argv[1], error, sizeof(error));
    if (!index) {
        fprintf(stderr, "Unable to open place index: %s\n", error);
        return 1;
    }

    OpenRidePlaceSearchResult results[100];
    uint32_t count = 0U;
    if (!openride_place_index_search(index,
                                     argv[2],
                                     results,
                                     max_results,
                                     &count,
                                     error,
                                     sizeof(error))) {
        fprintf(stderr, "Search failed: %s\n", error);
        openride_place_index_close(index);
        return 1;
    }

    for (uint32_t i = 0U; i < count; ++i) {
        printf("%2u. %-28s %-16s %.6f %.6f\n",
               i + 1U,
               results[i].name,
               openride_place_kind_name(results[i].kind),
               results[i].lat,
               results[i].lon);
    }

    openride_place_index_close(index);
    return 0;
}
