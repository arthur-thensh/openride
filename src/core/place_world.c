#include "openride/place_world.h"

#include "openride/region_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENRIDE_PLACE_WORLD_MAX_REGIONS 32U

typedef struct OpenRidePlaceWorldEntry {
    const OpenRideRegionDefinition *region;
    OpenRidePlaceIndex *index;
} OpenRidePlaceWorldEntry;

struct OpenRidePlaceWorld {
    OpenRidePlatformPaths paths;
    OpenRidePlaceWorldEntry entries[OPENRIDE_PLACE_WORLD_MAX_REGIONS];
    size_t count;
};

typedef struct OpenRidePlaceWorldCandidate {
    OpenRidePlaceSearchResult place;
    int match_class;
    size_t normalized_name_length;
} OpenRidePlaceWorldCandidate;

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
}

static void close_entries(OpenRidePlaceWorld *world)
{
    if (!world) return;
    for (size_t i = 0U; i < world->count; ++i) {
        openride_place_index_close(world->entries[i].index);
        world->entries[i].index = NULL;
        world->entries[i].region = NULL;
    }
    world->count = 0U;
}

OpenRidePlaceWorld *openride_place_world_create(
    const OpenRidePlatformPaths *paths,
    char *error,
    size_t error_size)
{
    if (!paths) {
        set_error(error, error_size, "invalid PlaceWorld paths");
        return NULL;
    }

    OpenRidePlaceWorld *world = calloc(1U, sizeof(*world));
    if (!world) {
        set_error(error, error_size, "unable to allocate PlaceWorld");
        return NULL;
    }
    world->paths = *paths;

    if (!openride_place_world_refresh(world, error, error_size)) {
        openride_place_world_destroy(world);
        return NULL;
    }
    return world;
}

void openride_place_world_destroy(OpenRidePlaceWorld *world)
{
    if (!world) return;
    close_entries(world);
    free(world);
}

bool openride_place_world_refresh(
    OpenRidePlaceWorld *world,
    char *error,
    size_t error_size)
{
    if (!world) {
        set_error(error, error_size, "invalid PlaceWorld");
        return false;
    }

    close_entries(world);

    const size_t count = openride_region_count();
    for (size_t i = 0U;
         i < count && world->count < OPENRIDE_PLACE_WORLD_MAX_REGIONS;
         ++i) {
        const OpenRideRegionDefinition *region = openride_region_at(i);
        if (!region) continue;

        OpenRideRegionStatus status;
        char status_error[192] = {0};
        if (!openride_region_get_status(&world->paths,
                                        region,
                                        &status,
                                        status_error,
                                        sizeof(status_error))
            || !status.search_installed) {
            continue;
        }

        char open_error[192] = {0};
        OpenRidePlaceIndex *index = openride_place_index_open(
            status.search_path,
            open_error,
            sizeof(open_error));
        if (!index) {
            /*
             * A damaged regional index must not disable search in every other
             * installed region.
             */
            continue;
        }

        OpenRidePlaceWorldEntry *entry = &world->entries[world->count++];
        entry->region = region;
        entry->index = index;
    }

    set_error(error, error_size, "");
    return true;
}

size_t openride_place_world_region_count(const OpenRidePlaceWorld *world)
{
    return world ? world->count : 0U;
}

static int match_class(const char *normalized_query,
                       const char *name,
                       size_t *normalized_name_length)
{
    char normalized_name[192] = {0};
    if (!openride_place_normalize(name,
                                  normalized_name,
                                  sizeof(normalized_name))) {
        if (normalized_name_length) *normalized_name_length = strlen(name);
        return 3;
    }

    const size_t query_length = strlen(normalized_query);
    const size_t name_length = strlen(normalized_name);
    if (normalized_name_length) *normalized_name_length = name_length;

    if (strcmp(normalized_name, normalized_query) == 0) return 0;
    if (query_length > 0U
        && name_length >= query_length
        && strncmp(normalized_name, normalized_query, query_length) == 0) {
        return 1;
    }
    return 2;
}

static bool same_place(const OpenRidePlaceSearchResult *a,
                       const OpenRidePlaceSearchResult *b)
{
    if (!a || !b) return false;
    if (a->osm_id == b->osm_id && a->lat == b->lat && a->lon == b->lon) {
        return true;
    }
    return a->lat == b->lat
        && a->lon == b->lon
        && strcmp(a->name, b->name) == 0;
}

static int compare_candidates(const void *left, const void *right)
{
    const OpenRidePlaceWorldCandidate *a = left;
    const OpenRidePlaceWorldCandidate *b = right;

    if (a->match_class != b->match_class) {
        return a->match_class < b->match_class ? -1 : 1;
    }
    if (a->place.rank != b->place.rank) {
        return a->place.rank > b->place.rank ? -1 : 1;
    }
    if (a->normalized_name_length != b->normalized_name_length) {
        return a->normalized_name_length < b->normalized_name_length ? -1 : 1;
    }
    return strcmp(a->place.name, b->place.name);
}

bool openride_place_world_search(
    OpenRidePlaceWorld *world,
    const char *query,
    OpenRidePlaceSearchResult *results,
    uint32_t max_results,
    uint32_t *result_count,
    char *error,
    size_t error_size)
{
    if (result_count) *result_count = 0U;
    if (!world || !query || (!results && max_results > 0U)) {
        set_error(error, error_size, "invalid PlaceWorld search arguments");
        return false;
    }
    if (max_results == 0U || world->count == 0U) {
        set_error(error, error_size, "");
        return true;
    }

    char normalized_query[192] = {0};
    if (!openride_place_normalize(query,
                                  normalized_query,
                                  sizeof(normalized_query))
        || strlen(normalized_query) < 2U) {
        set_error(error, error_size, "");
        return true;
    }

    const size_t capacity = world->count * (size_t)max_results;
    OpenRidePlaceWorldCandidate *candidates =
        calloc(capacity, sizeof(*candidates));
    if (!candidates) {
        set_error(error, error_size, "unable to allocate PlaceWorld results");
        return false;
    }

    size_t candidate_count = 0U;
    for (size_t r = 0U; r < world->count; ++r) {
        OpenRidePlaceSearchResult *local =
            calloc(max_results, sizeof(*local));
        if (!local) {
            free(candidates);
            set_error(error,
                      error_size,
                      "unable to allocate regional search results");
            return false;
        }

        uint32_t local_count = 0U;
        char local_error[192] = {0};
        const bool ok = openride_place_index_search(
            world->entries[r].index,
            query,
            local,
            max_results,
            &local_count,
            local_error,
            sizeof(local_error));
        if (!ok) {
            free(local);
            free(candidates);
            set_error(error,
                      error_size,
                      local_error[0] ? local_error
                                     : "regional place search failed");
            return false;
        }

        for (uint32_t i = 0U; i < local_count; ++i) {
            bool duplicate = false;
            for (size_t j = 0U; j < candidate_count; ++j) {
                if (same_place(&candidates[j].place, &local[i])) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate || candidate_count >= capacity) continue;

            OpenRidePlaceWorldCandidate *candidate =
                &candidates[candidate_count++];
            candidate->place = local[i];
            candidate->match_class = match_class(
                normalized_query,
                local[i].name,
                &candidate->normalized_name_length);
        }
        free(local);
    }

    qsort(candidates,
          candidate_count,
          sizeof(*candidates),
          compare_candidates);

    const uint32_t output_count =
        candidate_count < (size_t)max_results
            ? (uint32_t)candidate_count
            : max_results;
    for (uint32_t i = 0U; i < output_count; ++i) {
        results[i] = candidates[i].place;
    }

    free(candidates);
    if (result_count) *result_count = output_count;
    set_error(error, error_size, "");
    return true;
}
