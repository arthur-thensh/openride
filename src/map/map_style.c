#include "openride/map_style.h"

#include <string.h>

static bool is_kind(const char *kind, const char *expected)
{
    return kind && expected && strcmp(kind, expected) == 0;
}

static int population_bonus(int64_t population)
{
    if (population <= 0) return 0;

    int bonus = 0;
    while (population >= 10 && bonus < 180) {
        population /= 10;
        bonus += 30;
    }
    return bonus;
}

bool openride_map_place_label_visible(const char *kind,
                                      int64_t population,
                                      double zoom)
{
    if (!kind || kind[0] == '\0') return zoom >= 13.0;

    if (is_kind(kind, "capital")) return zoom >= 4.0;

    if (is_kind(kind, "city")) {
        if (population >= 100000) return zoom >= 6.0;
        if (population >= 50000) return zoom >= 7.0;
        return zoom >= 8.5;
    }

    if (is_kind(kind, "town")) {
        if (population >= 30000) return zoom >= 8.5;
        if (population >= 10000) return zoom >= 9.5;
        return zoom >= 10.5;
    }

    if (is_kind(kind, "village")) {
        if (population >= 5000) return zoom >= 11.0;
        if (population >= 1500) return zoom >= 12.0;
        return zoom >= 12.75;
    }

    if (is_kind(kind, "suburb")) return zoom >= 12.75;
    if (is_kind(kind, "borough")) return zoom >= 12.75;
    if (is_kind(kind, "hamlet")) return zoom >= 13.5;
    if (is_kind(kind, "quarter")) return zoom >= 13.75;
    if (is_kind(kind, "neighbourhood")) return zoom >= 13.75;
    if (is_kind(kind, "locality")) return zoom >= 14.0;
    if (is_kind(kind, "isolated_dwelling")) return zoom >= 14.0;

    return zoom >= 13.5;
}

int openride_map_place_label_priority(const char *kind,
                                      int64_t population)
{
    int base = 100;

    if (is_kind(kind, "capital")) base = 1200;
    else if (is_kind(kind, "city")) base = 1000;
    else if (is_kind(kind, "town")) base = 850;
    else if (is_kind(kind, "village")) base = 650;
    else if (is_kind(kind, "borough")) base = 520;
    else if (is_kind(kind, "suburb")) base = 500;
    else if (is_kind(kind, "hamlet")) base = 360;
    else if (is_kind(kind, "quarter")) base = 300;
    else if (is_kind(kind, "neighbourhood")) base = 280;
    else if (is_kind(kind, "locality")) base = 180;
    else if (is_kind(kind, "isolated_dwelling")) base = 160;

    return base + population_bonus(population);
}

bool openride_map_road_visible(const char *kind, double zoom)
{
    if (!kind || kind[0] == '\0') return zoom >= 12.0;

    if (is_kind(kind, "motorway") || is_kind(kind, "trunk")) return zoom >= 5.0;
    if (is_kind(kind, "primary")) return zoom >= 8.0;
    if (is_kind(kind, "secondary")) return zoom >= 9.0;
    if (is_kind(kind, "tertiary")) return zoom >= 10.5;

    if (is_kind(kind, "unclassified") ||
        is_kind(kind, "residential") ||
        is_kind(kind, "living_street")) {
        return zoom >= 12.0;
    }

    if (is_kind(kind, "service")) return zoom >= 12.75;
    if (is_kind(kind, "track")) return zoom >= 12.5;

    if (is_kind(kind, "path") ||
        is_kind(kind, "footway") ||
        is_kind(kind, "cycleway") ||
        is_kind(kind, "steps")) {
        return zoom >= 13.5;
    }

    return zoom >= 12.5;
}
