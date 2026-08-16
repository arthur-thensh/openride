#include "openride/osm_import.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRANCE_OVERVIEW_COORD_MAX 65535U
#define FRANCE_OVERVIEW_PI 3.14159265358979323846

typedef struct EdgeSet {
    uint64_t *keys;
    unsigned char *used;
    size_t count;
    size_t capacity;
} EdgeSet;

typedef struct OverviewBuild {
    EdgeSet coastline;
    EdgeSet motorway;
    EdgeSet trunk;
    EdgeSet primary;
    uint64_t source_features;
    uint64_t source_segments;
} OverviewBuild;

static uint64_t hash64(uint64_t value)
{
    value ^= value >> 33U;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33U;
    value *= UINT64_C(0xc4ceb9fe1a85ec53);
    value ^= value >> 33U;
    return value;
}

static void edge_set_destroy(EdgeSet *set)
{
    if (!set) return;
    free(set->keys);
    free(set->used);
    memset(set, 0, sizeof(*set));
}

static bool edge_set_rehash(EdgeSet *set, size_t new_capacity)
{
    uint64_t *new_keys =
        calloc(new_capacity, sizeof(*new_keys));
    unsigned char *new_used =
        calloc(new_capacity, sizeof(*new_used));
    if (!new_keys || !new_used) {
        free(new_keys);
        free(new_used);
        return false;
    }

    if (set->capacity > 0U) {
        for (size_t i = 0U; i < set->capacity; ++i) {
            if (!set->used[i]) continue;
            const uint64_t key = set->keys[i];
            size_t slot =
                (size_t)hash64(key) & (new_capacity - 1U);
            while (new_used[slot]) {
                slot = (slot + 1U) & (new_capacity - 1U);
            }
            new_used[slot] = 1U;
            new_keys[slot] = key;
        }
    }

    free(set->keys);
    free(set->used);
    set->keys = new_keys;
    set->used = new_used;
    set->capacity = new_capacity;
    return true;
}

static bool edge_set_insert(EdgeSet *set, uint64_t key)
{
    if (!set) return false;

    if (set->capacity == 0U) {
        if (!edge_set_rehash(set, 4096U)) return false;
    } else if ((set->count + 1U) * 10U
               >= set->capacity * 7U) {
        if (set->capacity > SIZE_MAX / 2U) return false;
        if (!edge_set_rehash(set, set->capacity * 2U)) {
            return false;
        }
    }

    size_t slot =
        (size_t)hash64(key) & (set->capacity - 1U);
    while (set->used[slot]) {
        if (set->keys[slot] == key) return true;
        slot = (slot + 1U) & (set->capacity - 1U);
    }

    set->used[slot] = 1U;
    set->keys[slot] = key;
    ++set->count;
    return true;
}

static double mercator_x(double lon)
{
    return (lon + 180.0) / 360.0;
}

static double mercator_y(double lat)
{
    if (lat > 85.05112878) lat = 85.05112878;
    if (lat < -85.05112878) lat = -85.05112878;

    const double radians =
        lat * FRANCE_OVERVIEW_PI / 180.0;
    return (1.0 - asinh(tan(radians))
            / FRANCE_OVERVIEW_PI) * 0.5;
}

static uint16_t quantize(double value)
{
    if (value <= 0.0) return 0U;
    if (value >= 1.0) {
        return (uint16_t)FRANCE_OVERVIEW_COORD_MAX;
    }
    return (uint16_t)llround(
        value * (double)FRANCE_OVERVIEW_COORD_MAX);
}

static uint32_t pack_point(double lat, double lon)
{
    const uint16_t x = quantize(mercator_x(lon));
    const uint16_t y = quantize(mercator_y(lat));
    return ((uint32_t)y << 16U) | (uint32_t)x;
}

static EdgeSet *set_for_kind(
    OverviewBuild *build,
    OpenRideOSMMapFeatureKind kind)
{
    if (!build) return NULL;
    switch (kind) {
        case OPENRIDE_OSM_MAP_FEATURE_OVERVIEW_COASTLINE:
            return &build->coastline;
        case OPENRIDE_OSM_MAP_FEATURE_OVERVIEW_MOTORWAY:
            return &build->motorway;
        case OPENRIDE_OSM_MAP_FEATURE_OVERVIEW_TRUNK:
            return &build->trunk;
        case OPENRIDE_OSM_MAP_FEATURE_OVERVIEW_PRIMARY:
            return &build->primary;
        default:
            return NULL;
    }
}

static bool collect_overview_line(
    OpenRideOSMMapFeatureKind kind,
    const double *latitudes,
    const double *longitudes,
    uint32_t point_count,
    void *userdata)
{
    OverviewBuild *build = userdata;
    EdgeSet *set = set_for_kind(build, kind);
    if (!build || !set || !latitudes || !longitudes
        || point_count < 2U) {
        return true;
    }

    ++build->source_features;
    uint32_t previous =
        pack_point(latitudes[0], longitudes[0]);

    for (uint32_t i = 1U; i < point_count; ++i) {
        const uint32_t current =
            pack_point(latitudes[i], longitudes[i]);

        if (previous != current) {
            uint32_t a = previous;
            uint32_t b = current;
            if (b < a) {
                const uint32_t tmp = a;
                a = b;
                b = tmp;
            }

            const uint64_t key =
                ((uint64_t)a << 32U) | (uint64_t)b;
            if (!edge_set_insert(set, key)) return false;
            ++build->source_segments;
        }

        previous = current;
    }

    return true;
}

static int compare_u64(const void *left, const void *right)
{
    const uint64_t a = *(const uint64_t *)left;
    const uint64_t b = *(const uint64_t *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

static uint64_t *edge_set_sorted(
    const EdgeSet *set,
    size_t *count_out)
{
    if (count_out) *count_out = 0U;
    if (!set || set->count == 0U) return NULL;

    uint64_t *result =
        malloc(set->count * sizeof(*result));
    if (!result) return NULL;

    size_t count = 0U;
    for (size_t i = 0U; i < set->capacity; ++i) {
        if (set->used[i]) {
            result[count++] = set->keys[i];
        }
    }
    qsort(result, count, sizeof(*result), compare_u64);

    if (count_out) *count_out = count;
    return result;
}

static bool write_edge_array(
    FILE *file,
    const char *symbol,
    const EdgeSet *set)
{
    size_t count = 0U;
    uint64_t *edges = edge_set_sorted(set, &count);
    if (set && set->count > 0U && !edges) return false;

    fprintf(
        file,
        "static const uint64_t %s[] = {\n",
        symbol);

    if (count == 0U) {
        fprintf(file, "    UINT64_C(0)\n");
    } else {
        for (size_t i = 0U; i < count; ++i) {
            fprintf(
                file,
                "    UINT64_C(0x%016llx)%s\n",
                (unsigned long long)edges[i],
                i + 1U == count ? "" : ",");
        }
    }

    fprintf(file, "};\n");
    fprintf(
        file,
        "#define %s_COUNT %zuU\n\n",
        symbol,
        count);

    free(edges);
    return ferror(file) == 0;
}

static const char *basename_portable(const char *path)
{
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool write_output(
    const char *output_path,
    const char *source_path,
    const OverviewBuild *build,
    const OpenRideOSMMapFeatureStats *stats)
{
    FILE *file = fopen(output_path, "wb");
    if (!file) {
        fprintf(
            stderr,
            "Unable to create %s: %s\n",
            output_path,
            strerror(errno));
        return false;
    }

    fprintf(
        file,
        "/*\n"
        " * Generated OpenRide France Overview network.\n"
        " * Source: %s\n"
        " * Quantization: 16-bit WebMercator x/y (~611 m world grid).\n"
        " * Source ways: %llu; selected ways: %llu; emitted features: %llu.\n"
        " * Source segments before grid deduplication: %llu.\n"
        " * DO NOT EDIT BY HAND. Regenerate with scripts/prepare_france_overview.sh.\n"
        " */\n\n",
        basename_portable(source_path),
        (unsigned long long)(stats ? stats->osm_way_count : 0U),
        (unsigned long long)(stats ? stats->selected_way_count : 0U),
        (unsigned long long)(stats ? stats->emitted_feature_count : 0U),
        (unsigned long long)(build ? build->source_segments : 0U));

    bool ok =
        write_edge_array(
            file,
            "OPENRIDE_FRANCE_OVERVIEW_COAST_EDGES",
            &build->coastline)
        && write_edge_array(
            file,
            "OPENRIDE_FRANCE_OVERVIEW_MOTORWAY_EDGES",
            &build->motorway)
        && write_edge_array(
            file,
            "OPENRIDE_FRANCE_OVERVIEW_TRUNK_EDGES",
            &build->trunk)
        && write_edge_array(
            file,
            "OPENRIDE_FRANCE_OVERVIEW_PRIMARY_EDGES",
            &build->primary);

    if (fclose(file) != 0) ok = false;
    return ok;
}

static void build_destroy(OverviewBuild *build)
{
    if (!build) return;
    edge_set_destroy(&build->coastline);
    edge_set_destroy(&build->motorway);
    edge_set_destroy(&build->trunk);
    edge_set_destroy(&build->primary);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(
            stderr,
            "Usage: %s <france.osm.pbf> <france_overview_network_data.inc>\n",
            argv[0]);
        return 2;
    }

    OverviewBuild build = {0};
    OpenRideOSMMapFeatureStats stats = {0};
    char error[512] = {0};

    fprintf(
        stdout,
        "OpenRide France Overview generator\n"
        "Source : %s\n"
        "Output : %s\n",
        argv[1],
        argv[2]);

    const bool imported =
        openride_osm_pbf_visit_overview_lines(
            argv[1],
            collect_overview_line,
            &build,
            &stats,
            error,
            sizeof(error));

    if (!imported) {
        fprintf(
            stderr,
            "Overview import failed: %s\n",
            error[0] ? error : "unknown error");
        build_destroy(&build);
        return 1;
    }

    if (!write_output(
            argv[2],
            argv[1],
            &build,
            &stats)) {
        fprintf(stderr, "Unable to write generated overview atlas\n");
        build_destroy(&build);
        return 1;
    }

    fprintf(
        stdout,
        "\nOSM ways scanned     : %llu\n"
        "Overview ways        : %llu\n"
        "Referenced nodes     : %llu\n"
        "Emitted features     : %llu\n"
        "Raw source segments  : %llu\n"
        "Coast edges          : %zu\n"
        "Motorway edges       : %zu\n"
        "Trunk edges          : %zu\n"
        "Primary edges        : %zu\n"
        "Total embedded edges : %zu\n",
        (unsigned long long)stats.osm_way_count,
        (unsigned long long)stats.selected_way_count,
        (unsigned long long)stats.referenced_node_count,
        (unsigned long long)stats.emitted_feature_count,
        (unsigned long long)build.source_segments,
        build.coastline.count,
        build.motorway.count,
        build.trunk.count,
        build.primary.count,
        build.coastline.count
            + build.motorway.count
            + build.trunk.count
            + build.primary.count);

    build_destroy(&build);
    return 0;
}
