#include "openride/region_network.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define OPENRIDE_PI 3.14159265358979323846
#define OPENRIDE_EARTH_RADIUS_M 6371008.8
#define OPENRIDE_REGION_INDEX_NONE UINT32_MAX

typedef struct OpenRideRegionNetworkNode {
    const char *id;
    double center_lat;
    double center_lon;
} OpenRideRegionNetworkNode;

typedef struct OpenRideRegionNetworkEdge {
    const char *first_id;
    const char *second_id;
} OpenRideRegionNetworkEdge;

/*
 * Historical Geofabrik France extracts used by OpenRide.
 * Centers are coarse planning anchors only; actual routing always uses OSM.
 */
static const OpenRideRegionNetworkNode NETWORK_NODES[] = {
    {"alsace", 48.25, 7.45},
    {"aquitaine", 44.65, -0.45},
    {"auvergne", 45.45, 3.10},
    {"basse-normandie", 49.00, -0.35},
    {"bourgogne", 47.05, 4.65},
    {"bretagne", 48.15, -2.85},
    {"centre", 47.45, 1.70},
    {"champagne-ardenne", 48.70, 4.35},
    {"corse", 42.15, 9.05},
    {"franche-comte", 47.10, 6.15},
    {"guadeloupe", 16.20, -61.55},
    {"guyane", 4.00, -53.00},
    {"haute-normandie", 49.45, 1.00},
    {"ile-de-france", 48.70, 2.50},
    {"languedoc-roussillon", 43.65, 3.25},
    {"limousin", 45.70, 1.65},
    {"lorraine", 48.65, 6.10},
    {"martinique", 14.65, -61.00},
    {"mayotte", -12.80, 45.15},
    {"midi-pyrenees", 43.75, 1.45},
    {"nord-pas-de-calais", 50.45, 2.50},
    {"pays-de-la-loire", 47.45, -0.80},
    {"picardie", 49.65, 2.75},
    {"poitou-charentes", 46.15, 0.20},
    {"provence-alpes-cote-d-azur", 43.90, 6.10},
    {"reunion", -21.15, 55.50},
    {"rhone-alpes", 45.45, 5.20}
};

static const OpenRideRegionNetworkEdge NETWORK_EDGES[] = {
    {"alsace", "lorraine"},
    {"alsace", "franche-comte"},

    {"aquitaine", "poitou-charentes"},
    {"aquitaine", "limousin"},
    {"aquitaine", "midi-pyrenees"},

    {"auvergne", "centre"},
    {"auvergne", "bourgogne"},
    {"auvergne", "rhone-alpes"},
    {"auvergne", "languedoc-roussillon"},
    {"auvergne", "midi-pyrenees"},
    {"auvergne", "limousin"},

    {"basse-normandie", "bretagne"},
    {"basse-normandie", "pays-de-la-loire"},
    {"basse-normandie", "centre"},
    {"basse-normandie", "haute-normandie"},

    {"bourgogne", "centre"},
    {"bourgogne", "ile-de-france"},
    {"bourgogne", "champagne-ardenne"},
    {"bourgogne", "franche-comte"},
    {"bourgogne", "rhone-alpes"},

    {"bretagne", "pays-de-la-loire"},

    {"centre", "pays-de-la-loire"},
    {"centre", "poitou-charentes"},
    {"centre", "limousin"},
    {"centre", "ile-de-france"},
    {"centre", "haute-normandie"},

    {"champagne-ardenne", "picardie"},
    {"champagne-ardenne", "ile-de-france"},
    {"champagne-ardenne", "lorraine"},
    {"champagne-ardenne", "franche-comte"},

    {"franche-comte", "lorraine"},
    {"franche-comte", "rhone-alpes"},

    {"haute-normandie", "picardie"},
    {"haute-normandie", "ile-de-france"},

    {"ile-de-france", "picardie"},

    {"languedoc-roussillon", "midi-pyrenees"},
    {"languedoc-roussillon", "rhone-alpes"},
    {"languedoc-roussillon", "provence-alpes-cote-d-azur"},

    {"limousin", "poitou-charentes"},
    {"limousin", "midi-pyrenees"},

    {"lorraine", "champagne-ardenne"},

    {"nord-pas-de-calais", "picardie"},

    {"pays-de-la-loire", "poitou-charentes"},

    {"provence-alpes-cote-d-azur", "rhone-alpes"}
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
}

static double radians(double degrees)
{
    return degrees * OPENRIDE_PI / 180.0;
}

static double geo_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    const double phi1 = radians(lat1);
    const double phi2 = radians(lat2);
    const double dphi = radians(lat2 - lat1);
    const double dlambda = radians(lon2 - lon1);
    const double sin_dphi = sin(dphi * 0.5);
    const double sin_dlambda = sin(dlambda * 0.5);
    const double a = sin_dphi * sin_dphi
                   + cos(phi1) * cos(phi2) * sin_dlambda * sin_dlambda;
    const double clamped = a < 0.0 ? 0.0 : (a > 1.0 ? 1.0 : a);
    return OPENRIDE_EARTH_RADIUS_M
         * 2.0 * atan2(sqrt(clamped), sqrt(1.0 - clamped));
}

static uint32_t network_index_for_id(const char *id)
{
    if (!id) return OPENRIDE_REGION_INDEX_NONE;
    for (uint32_t i = 0U;
         i < (uint32_t)(sizeof(NETWORK_NODES) / sizeof(NETWORK_NODES[0]));
         ++i) {
        if (strcmp(NETWORK_NODES[i].id, id) == 0) return i;
    }
    return OPENRIDE_REGION_INDEX_NONE;
}

static const OpenRideRegionDefinition *region_for_network_index(uint32_t index)
{
    if (index >= (uint32_t)(sizeof(NETWORK_NODES) / sizeof(NETWORK_NODES[0]))) {
        return NULL;
    }
    return openride_region_find(NETWORK_NODES[index].id);
}

static bool nodes_adjacent(uint32_t first, uint32_t second)
{
    if (first == second) return true;
    if (first >= (uint32_t)(sizeof(NETWORK_NODES) / sizeof(NETWORK_NODES[0]))
        || second >= (uint32_t)(sizeof(NETWORK_NODES) / sizeof(NETWORK_NODES[0]))) {
        return false;
    }

    const char *first_id = NETWORK_NODES[first].id;
    const char *second_id = NETWORK_NODES[second].id;
    for (size_t i = 0U; i < sizeof(NETWORK_EDGES) / sizeof(NETWORK_EDGES[0]); ++i) {
        const OpenRideRegionNetworkEdge *edge = &NETWORK_EDGES[i];
        if ((strcmp(edge->first_id, first_id) == 0
             && strcmp(edge->second_id, second_id) == 0)
            || (strcmp(edge->first_id, second_id) == 0
                && strcmp(edge->second_id, first_id) == 0)) {
            return true;
        }
    }
    return false;
}

bool openride_region_network_center(const OpenRideRegionDefinition *region,
                                    double *lat,
                                    double *lon)
{
    if (!region) return false;
    const uint32_t index = network_index_for_id(region->id);
    if (index == OPENRIDE_REGION_INDEX_NONE) return false;
    if (lat) *lat = NETWORK_NODES[index].center_lat;
    if (lon) *lon = NETWORK_NODES[index].center_lon;
    return true;
}

bool openride_region_network_adjacent(const OpenRideRegionDefinition *first,
                                      const OpenRideRegionDefinition *second)
{
    if (!first || !second) return false;
    const uint32_t first_index = network_index_for_id(first->id);
    const uint32_t second_index = network_index_for_id(second->id);
    if (first_index == OPENRIDE_REGION_INDEX_NONE
        || second_index == OPENRIDE_REGION_INDEX_NONE) {
        return false;
    }
    return first_index != second_index && nodes_adjacent(first_index, second_index);
}

bool openride_region_network_installed_mask(
    const OpenRidePlatformPaths *paths,
    bool *installed,
    size_t installed_count,
    char *error,
    size_t error_size)
{
    if (!paths || !installed || installed_count < openride_region_count()) {
        set_error(error, error_size, "invalid region installed-mask request");
        return false;
    }

    memset(installed, 0, installed_count * sizeof(*installed));
    for (size_t i = 0U; i < openride_region_count(); ++i) {
        const OpenRideRegionDefinition *region = openride_region_at(i);
        OpenRideRegionStatus status;
        char local_error[128] = {0};
        if (region
            && openride_region_get_status(paths,
                                          region,
                                          &status,
                                          local_error,
                                          sizeof(local_error))) {
            /* Routing availability is independent from the cartographic
             * format revision. A v3 map may need rebuilding for display, but
             * its installed graph/search data remain fully routable. */
            installed[i] = status.map_installed
                && status.routing_installed
                && status.search_installed;
        }
    }

    set_error(error, error_size, "");
    return true;
}

static int region_catalog_index(const OpenRideRegionDefinition *region)
{
    if (!region) return -1;
    for (size_t i = 0U; i < openride_region_count(); ++i) {
        const OpenRideRegionDefinition *candidate = openride_region_at(i);
        if (candidate && strcmp(candidate->id, region->id) == 0) return (int)i;
    }
    return -1;
}

static bool is_region_installed(const bool *installed,
                                size_t installed_count,
                                const OpenRideRegionDefinition *region)
{
    if (!installed || !region) return false;
    const int index = region_catalog_index(region);
    return index >= 0 && (size_t)index < installed_count && installed[index];
}

static double transition_cost(uint32_t from,
                              uint32_t to,
                              uint32_t start_index,
                              double start_lat,
                              double start_lon,
                              uint32_t destination_index,
                              double destination_lat,
                              double destination_lon)
{
    if (from == start_index && to == destination_index) {
        return geo_distance_m(start_lat,
                              start_lon,
                              destination_lat,
                              destination_lon);
    }
    if (from == start_index) {
        return geo_distance_m(start_lat,
                              start_lon,
                              NETWORK_NODES[to].center_lat,
                              NETWORK_NODES[to].center_lon);
    }
    if (to == destination_index) {
        return geo_distance_m(NETWORK_NODES[from].center_lat,
                              NETWORK_NODES[from].center_lon,
                              destination_lat,
                              destination_lon);
    }
    return geo_distance_m(NETWORK_NODES[from].center_lat,
                          NETWORK_NODES[from].center_lon,
                          NETWORK_NODES[to].center_lat,
                          NETWORK_NODES[to].center_lon);
}

static bool build_corridor(uint32_t start_index,
                           double start_lat,
                           double start_lon,
                           uint32_t destination_index,
                           double destination_lat,
                           double destination_lon,
                           const bool *installed,
                           size_t installed_count,
                           bool installed_only,
                           OpenRideRegionCorridor *corridor)
{
    const uint32_t node_count =
        (uint32_t)(sizeof(NETWORK_NODES) / sizeof(NETWORK_NODES[0]));
    if (!corridor || start_index >= node_count || destination_index >= node_count) {
        return false;
    }

    double distance[OPENRIDE_REGION_NETWORK_MAX_REGIONS];
    uint32_t previous[OPENRIDE_REGION_NETWORK_MAX_REGIONS];
    bool visited[OPENRIDE_REGION_NETWORK_MAX_REGIONS];
    if (node_count > OPENRIDE_REGION_NETWORK_MAX_REGIONS) return false;

    for (uint32_t i = 0U; i < node_count; ++i) {
        distance[i] = INFINITY;
        previous[i] = OPENRIDE_REGION_INDEX_NONE;
        visited[i] = false;
    }
    distance[start_index] = 0.0;

    for (uint32_t iteration = 0U; iteration < node_count; ++iteration) {
        uint32_t current = OPENRIDE_REGION_INDEX_NONE;
        double best = INFINITY;
        for (uint32_t i = 0U; i < node_count; ++i) {
            if (!visited[i] && distance[i] < best) {
                current = i;
                best = distance[i];
            }
        }
        if (current == OPENRIDE_REGION_INDEX_NONE) break;
        if (current == destination_index) break;
        visited[current] = true;

        for (uint32_t next = 0U; next < node_count; ++next) {
            if (visited[next] || next == current || !nodes_adjacent(current, next)) {
                continue;
            }

            const OpenRideRegionDefinition *next_region =
                region_for_network_index(next);
            if (!next_region) continue;

            if (installed_only
                && !is_region_installed(installed,
                                        installed_count,
                                        next_region)) {
                continue;
            }

            const double candidate =
                distance[current]
                + transition_cost(current,
                                  next,
                                  start_index,
                                  start_lat,
                                  start_lon,
                                  destination_index,
                                  destination_lat,
                                  destination_lon);
            if (candidate < distance[next]) {
                distance[next] = candidate;
                previous[next] = current;
            }
        }
    }

    if (!isfinite(distance[destination_index])) return false;

    uint32_t reverse[OPENRIDE_REGION_NETWORK_MAX_REGIONS];
    uint32_t reverse_count = 0U;
    uint32_t cursor = destination_index;
    while (true) {
        if (reverse_count >= OPENRIDE_REGION_NETWORK_MAX_REGIONS) return false;
        reverse[reverse_count++] = cursor;
        if (cursor == start_index) break;
        cursor = previous[cursor];
        if (cursor == OPENRIDE_REGION_INDEX_NONE) return false;
    }

    memset(corridor, 0, sizeof(*corridor));
    corridor->estimated_distance_m = distance[destination_index];
    for (uint32_t i = 0U; i < reverse_count; ++i) {
        const uint32_t network_index = reverse[reverse_count - 1U - i];
        corridor->regions[i] = region_for_network_index(network_index);
        if (!corridor->regions[i]) {
            memset(corridor, 0, sizeof(*corridor));
            return false;
        }
    }
    corridor->count = reverse_count;
    return true;
}

static bool corridors_equal(const OpenRideRegionCorridor *first,
                            const OpenRideRegionCorridor *second)
{
    if (!first || !second || first->count != second->count) return false;
    for (uint32_t i = 0U; i < first->count; ++i) {
        if (!first->regions[i] || !second->regions[i]
            || strcmp(first->regions[i]->id, second->regions[i]->id) != 0) {
            return false;
        }
    }
    return true;
}

bool openride_region_network_plan(
    const OpenRideRegionDefinition *start_region,
    double start_lat,
    double start_lon,
    const OpenRideRegionDefinition *destination_region,
    double destination_lat,
    double destination_lon,
    const bool *installed,
    size_t installed_count,
    OpenRideRegionNetworkPlan *plan,
    char *error,
    size_t error_size)
{
    if (plan) memset(plan, 0, sizeof(*plan));
    if (!start_region || !destination_region || !plan
        || !isfinite(start_lat) || !isfinite(start_lon)
        || !isfinite(destination_lat) || !isfinite(destination_lon)
        || (installed && installed_count < openride_region_count())) {
        set_error(error, error_size, "invalid region network plan request");
        return false;
    }

    const uint32_t start_index = network_index_for_id(start_region->id);
    const uint32_t destination_index =
        network_index_for_id(destination_region->id);
    if (start_index == OPENRIDE_REGION_INDEX_NONE
        || destination_index == OPENRIDE_REGION_INDEX_NONE) {
        set_error(error, error_size, "region is outside the built-in network");
        return false;
    }

    if (start_index == destination_index) {
        plan->recommended.regions[0] = start_region;
        plan->recommended.count = 1U;
        plan->recommended.estimated_distance_m =
            geo_distance_m(start_lat,
                           start_lon,
                           destination_lat,
                           destination_lon);
    } else if (!build_corridor(start_index,
                               start_lat,
                               start_lon,
                               destination_index,
                               destination_lat,
                               destination_lon,
                               installed,
                               installed_count,
                               false,
                               &plan->recommended)) {
        set_error(error, error_size, "no regional corridor exists");
        return false;
    }

    if (installed) {
        for (uint32_t i = 0U; i < plan->recommended.count; ++i) {
            const OpenRideRegionDefinition *region =
                plan->recommended.regions[i];
            if (!is_region_installed(installed, installed_count, region)) {
                if (plan->missing_count < OPENRIDE_REGION_NETWORK_MAX_REGIONS) {
                    plan->missing_regions[plan->missing_count++] = region;
                }
            }
        }

        if (plan->missing_count > 0U
            && is_region_installed(installed, installed_count, start_region)
            && is_region_installed(installed, installed_count, destination_region)
            && build_corridor(start_index,
                              start_lat,
                              start_lon,
                              destination_index,
                              destination_lat,
                              destination_lon,
                              installed,
                              installed_count,
                              true,
                              &plan->installed_alternative)
            && !corridors_equal(&plan->recommended,
                                &plan->installed_alternative)) {
            plan->has_installed_alternative = true;
        }
    }

    set_error(error, error_size, "");
    return true;
}
