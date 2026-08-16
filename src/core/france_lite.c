#include "openride/france_lite.h"

#include "openride/region_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Important cities/towns only. Downloaded regional SQLite indexes remain the
 * richer source for local places and POIs.
 */
static const OpenRideFranceLitePlace PLACES[] = {
    {"Strasbourg", 48.5734, 7.7521, OPENRIDE_PLACE_CITY, 100, "alsace"},
    {"Mulhouse", 47.7508, 7.3359, OPENRIDE_PLACE_CITY, 85, "alsace"},
    {"Colmar", 48.0794, 7.3585, OPENRIDE_PLACE_TOWN, 78, "alsace"},
    {"Bordeaux", 44.8378, -0.5792, OPENRIDE_PLACE_CITY, 100, "aquitaine"},
    {"Pau", 43.2951, -0.3708, OPENRIDE_PLACE_CITY, 86, "aquitaine"},
    {"Bayonne", 43.4929, -1.4748, OPENRIDE_PLACE_CITY, 84, "aquitaine"},
    {"Agen", 44.2030, 0.6160, OPENRIDE_PLACE_TOWN, 76, "aquitaine"},
    {"Périgueux", 45.1840, 0.7211, OPENRIDE_PLACE_TOWN, 76, "aquitaine"},
    {"Clermont-Ferrand", 45.7772, 3.0870, OPENRIDE_PLACE_CITY, 95, "auvergne"},
    {"Aurillac", 44.9309, 2.4449, OPENRIDE_PLACE_TOWN, 72, "auvergne"},
    {"Moulins", 46.5657, 3.3334, OPENRIDE_PLACE_TOWN, 72, "auvergne"},
    {"Caen", 49.1829, -0.3707, OPENRIDE_PLACE_CITY, 94, "basse-normandie"},
    {"Cherbourg-en-Cotentin", 49.6337, -1.6221, OPENRIDE_PLACE_CITY, 82, "basse-normandie"},
    {"Alençon", 48.4329, 0.0913, OPENRIDE_PLACE_TOWN, 72, "basse-normandie"},
    {"Dijon", 47.3220, 5.0415, OPENRIDE_PLACE_CITY, 94, "bourgogne"},
    {"Auxerre", 47.7982, 3.5738, OPENRIDE_PLACE_TOWN, 74, "bourgogne"},
    {"Nevers", 46.9896, 3.1590, OPENRIDE_PLACE_TOWN, 72, "bourgogne"},
    {"Rennes", 48.1173, -1.6778, OPENRIDE_PLACE_CITY, 96, "bretagne"},
    {"Brest", 48.3904, -4.4861, OPENRIDE_PLACE_CITY, 90, "bretagne"},
    {"Quimper", 47.9960, -4.1025, OPENRIDE_PLACE_CITY, 80, "bretagne"},
    {"Vannes", 47.6582, -2.7608, OPENRIDE_PLACE_TOWN, 78, "bretagne"},
    {"Orléans", 47.9030, 1.9093, OPENRIDE_PLACE_CITY, 90, "centre"},
    {"Tours", 47.3941, 0.6848, OPENRIDE_PLACE_CITY, 91, "centre"},
    {"Bourges", 47.0810, 2.3988, OPENRIDE_PLACE_CITY, 78, "centre"},
    {"Chartres", 48.4469, 1.4890, OPENRIDE_PLACE_TOWN, 76, "centre"},
    {"Reims", 49.2583, 4.0317, OPENRIDE_PLACE_CITY, 94, "champagne-ardenne"},
    {"Troyes", 48.2973, 4.0744, OPENRIDE_PLACE_CITY, 82, "champagne-ardenne"},
    {"Charleville-Mézières", 49.7621, 4.7261, OPENRIDE_PLACE_TOWN, 74, "champagne-ardenne"},
    {"Ajaccio", 41.9192, 8.7386, OPENRIDE_PLACE_CITY, 88, "corse"},
    {"Bastia", 42.6973, 9.4509, OPENRIDE_PLACE_CITY, 82, "corse"},
    {"Besançon", 47.2378, 6.0241, OPENRIDE_PLACE_CITY, 90, "franche-comte"},
    {"Belfort", 47.6397, 6.8638, OPENRIDE_PLACE_TOWN, 76, "franche-comte"},
    {"Rouen", 49.4432, 1.0993, OPENRIDE_PLACE_CITY, 95, "haute-normandie"},
    {"Le Havre", 49.4944, 0.1079, OPENRIDE_PLACE_CITY, 92, "haute-normandie"},
    {"Évreux", 49.0241, 1.1508, OPENRIDE_PLACE_TOWN, 74, "haute-normandie"},
    {"Paris", 48.8566, 2.3522, OPENRIDE_PLACE_CITY, 120, "ile-de-france"},
    {"Versailles", 48.8014, 2.1301, OPENRIDE_PLACE_CITY, 84, "ile-de-france"},
    {"Créteil", 48.7904, 2.4556, OPENRIDE_PLACE_CITY, 78, "ile-de-france"},
    {"Montpellier", 43.6108, 3.8767, OPENRIDE_PLACE_CITY, 98, "languedoc-roussillon"},
    {"Nîmes", 43.8367, 4.3601, OPENRIDE_PLACE_CITY, 90, "languedoc-roussillon"},
    {"Perpignan", 42.6887, 2.8948, OPENRIDE_PLACE_CITY, 86, "languedoc-roussillon"},
    {"Carcassonne", 43.2122, 2.3537, OPENRIDE_PLACE_CITY, 80, "languedoc-roussillon"},
    {"Limoges", 45.8336, 1.2611, OPENRIDE_PLACE_CITY, 90, "limousin"},
    {"Brive-la-Gaillarde", 45.1589, 1.5333, OPENRIDE_PLACE_CITY, 76, "limousin"},
    {"Guéret", 46.1719, 1.8717, OPENRIDE_PLACE_TOWN, 68, "limousin"},
    {"Metz", 49.1193, 6.1757, OPENRIDE_PLACE_CITY, 92, "lorraine"},
    {"Nancy", 48.6921, 6.1844, OPENRIDE_PLACE_CITY, 92, "lorraine"},
    {"Épinal", 48.1742, 6.4494, OPENRIDE_PLACE_TOWN, 72, "lorraine"},
    {"Toulouse", 43.6047, 1.4442, OPENRIDE_PLACE_CITY, 105, "midi-pyrenees"},
    {"Tarbes", 43.2329, 0.0781, OPENRIDE_PLACE_CITY, 76, "midi-pyrenees"},
    {"Albi", 43.9298, 2.1480, OPENRIDE_PLACE_CITY, 76, "midi-pyrenees"},
    {"Rodez", 44.3510, 2.5730, OPENRIDE_PLACE_TOWN, 72, "midi-pyrenees"},
    {"Montauban", 44.0176, 1.3549, OPENRIDE_PLACE_CITY, 76, "midi-pyrenees"},
    {"Lille", 50.6292, 3.0573, OPENRIDE_PLACE_CITY, 100, "nord-pas-de-calais"},
    {"Douai", 50.3708, 3.0802, OPENRIDE_PLACE_CITY, 82, "nord-pas-de-calais"},
    {"Arras", 50.2910, 2.7775, OPENRIDE_PLACE_CITY, 80, "nord-pas-de-calais"},
    {"Lens", 50.4289, 2.8318, OPENRIDE_PLACE_CITY, 78, "nord-pas-de-calais"},
    {"Valenciennes", 50.3571, 3.5183, OPENRIDE_PLACE_CITY, 78, "nord-pas-de-calais"},
    {"Calais", 50.9513, 1.8587, OPENRIDE_PLACE_CITY, 80, "nord-pas-de-calais"},
    {"Dunkerque", 51.0344, 2.3768, OPENRIDE_PLACE_CITY, 80, "nord-pas-de-calais"},
    {"Nantes", 47.2184, -1.5536, OPENRIDE_PLACE_CITY, 100, "pays-de-la-loire"},
    {"Angers", 47.4784, -0.5632, OPENRIDE_PLACE_CITY, 90, "pays-de-la-loire"},
    {"Le Mans", 48.0061, 0.1996, OPENRIDE_PLACE_CITY, 88, "pays-de-la-loire"},
    {"Laval", 48.0706, -0.7734, OPENRIDE_PLACE_CITY, 74, "pays-de-la-loire"},
    {"Amiens", 49.8941, 2.2958, OPENRIDE_PLACE_CITY, 92, "picardie"},
    {"Beauvais", 49.4295, 2.0807, OPENRIDE_PLACE_CITY, 78, "picardie"},
    {"Compiègne", 49.4179, 2.8261, OPENRIDE_PLACE_TOWN, 74, "picardie"},
    {"Saint-Quentin", 49.8489, 3.2876, OPENRIDE_PLACE_CITY, 76, "picardie"},
    {"Poitiers", 46.5802, 0.3404, OPENRIDE_PLACE_CITY, 88, "poitou-charentes"},
    {"La Rochelle", 46.1603, -1.1511, OPENRIDE_PLACE_CITY, 86, "poitou-charentes"},
    {"Angoulême", 45.6484, 0.1562, OPENRIDE_PLACE_CITY, 78, "poitou-charentes"},
    {"Niort", 46.3237, -0.4588, OPENRIDE_PLACE_CITY, 76, "poitou-charentes"},
    {"Marseille", 43.2965, 5.3698, OPENRIDE_PLACE_CITY, 110, "provence-alpes-cote-d-azur"},
    {"Nice", 43.7102, 7.2620, OPENRIDE_PLACE_CITY, 102, "provence-alpes-cote-d-azur"},
    {"Toulon", 43.1242, 5.9280, OPENRIDE_PLACE_CITY, 90, "provence-alpes-cote-d-azur"},
    {"Aix-en-Provence", 43.5297, 5.4474, OPENRIDE_PLACE_CITY, 88, "provence-alpes-cote-d-azur"},
    {"Avignon", 43.9493, 4.8055, OPENRIDE_PLACE_CITY, 86, "provence-alpes-cote-d-azur"},
    {"Lyon", 45.7640, 4.8357, OPENRIDE_PLACE_CITY, 108, "rhone-alpes"},
    {"Grenoble", 45.1885, 5.7245, OPENRIDE_PLACE_CITY, 94, "rhone-alpes"},
    {"Saint-Étienne", 45.4397, 4.3872, OPENRIDE_PLACE_CITY, 90, "rhone-alpes"},
    {"Annecy", 45.8992, 6.1294, OPENRIDE_PLACE_CITY, 84, "rhone-alpes"},
    {"Chambéry", 45.5646, 5.9178, OPENRIDE_PLACE_CITY, 78, "rhone-alpes"},
    {"Valence", 44.9334, 4.8924, OPENRIDE_PLACE_CITY, 78, "rhone-alpes"},
    {"Pointe-à-Pitre", 16.2411, -61.5331, OPENRIDE_PLACE_CITY, 72, "guadeloupe"},
    {"Cayenne", 4.9224, -52.3135, OPENRIDE_PLACE_CITY, 80, "guyane"},
    {"Fort-de-France", 14.6161, -61.0588, OPENRIDE_PLACE_CITY, 80, "martinique"},
    {"Mamoudzou", -12.7806, 45.2278, OPENRIDE_PLACE_CITY, 76, "mayotte"},
    {"Saint-Denis de La Réunion", -20.8789, 55.4481, OPENRIDE_PLACE_CITY, 80, "reunion"}
};

typedef struct OpenRideFranceLiteCandidate {
    const OpenRideFranceLitePlace *entry;
    int match_class;
    size_t normalized_name_length;
} OpenRideFranceLiteCandidate;

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
}

size_t openride_france_lite_place_count(void)
{
    return sizeof(PLACES) / sizeof(PLACES[0]);
}

const OpenRideFranceLitePlace *openride_france_lite_place_at(size_t index)
{
    if (index >= openride_france_lite_place_count()) return NULL;
    return &PLACES[index];
}

static int compare_candidate(const void *left, const void *right)
{
    const OpenRideFranceLiteCandidate *a = left;
    const OpenRideFranceLiteCandidate *b = right;
    if (a->match_class != b->match_class) {
        return a->match_class < b->match_class ? -1 : 1;
    }
    if (a->entry->rank != b->entry->rank) {
        return a->entry->rank > b->entry->rank ? -1 : 1;
    }
    if (a->normalized_name_length != b->normalized_name_length) {
        return a->normalized_name_length < b->normalized_name_length ? -1 : 1;
    }
    return strcmp(a->entry->name, b->entry->name);
}

bool openride_france_lite_search(const char *query,
                                 OpenRidePlaceSearchResult *results,
                                 uint32_t max_results,
                                 uint32_t *result_count,
                                 char *error,
                                 size_t error_size)
{
    if (result_count) *result_count = 0U;
    if (!query || (!results && max_results > 0U)) {
        set_error(error, error_size, "invalid France-lite search arguments");
        return false;
    }
    if (max_results == 0U) {
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

    OpenRideFranceLiteCandidate candidates[
        sizeof(PLACES) / sizeof(PLACES[0])];
    size_t candidate_count = 0U;
    const size_t query_length = strlen(normalized_query);

    for (size_t i = 0U; i < openride_france_lite_place_count(); ++i) {
        char normalized_name[192] = {0};
        if (!openride_place_normalize(PLACES[i].name,
                                      normalized_name,
                                      sizeof(normalized_name))) {
            continue;
        }

        const char *match = strstr(normalized_name, normalized_query);
        if (!match) continue;

        int match_class = 2;
        if (strcmp(normalized_name, normalized_query) == 0) {
            match_class = 0;
        } else if ((size_t)(match - normalized_name) == 0U
                   && strlen(normalized_name) >= query_length) {
            match_class = 1;
        }

        candidates[candidate_count++] = (OpenRideFranceLiteCandidate){
            .entry = &PLACES[i],
            .match_class = match_class,
            .normalized_name_length = strlen(normalized_name)
        };
    }

    qsort(candidates,
          candidate_count,
          sizeof(candidates[0]),
          compare_candidate);

    const uint32_t count =
        candidate_count < (size_t)max_results
            ? (uint32_t)candidate_count
            : max_results;

    for (uint32_t i = 0U; i < count; ++i) {
        const OpenRideFranceLitePlace *entry = candidates[i].entry;
        OpenRidePlaceSearchResult *out = &results[i];
        memset(out, 0, sizeof(*out));
        out->osm_id = -(int64_t)(i + 1U);
        out->lat = entry->lat;
        out->lon = entry->lon;
        out->kind = entry->kind;
        out->rank = entry->rank;
        out->bundled_lite = true;
        snprintf(out->name, sizeof(out->name), "%s", entry->name);
        snprintf(out->region_id, sizeof(out->region_id), "%s", entry->region_id);

        if (!openride_region_find(out->region_id)) {
            set_error(error, error_size,
                      "France-lite place references unknown region");
            return false;
        }
    }

    if (result_count) *result_count = count;
    set_error(error, error_size, "");
    return true;
}
