#include "openride/region_manager.h"

#include <stdio.h>
#include <string.h>

#define REGION(slug, display_name) { \
    .id = slug, \
    .name = display_name, \
    .ormap_filename = slug ".ormap", \
    .legacy_map_filename = slug "-shortbread.mbtiles", \
    .routing_filename = slug ".orgraph", \
    .search_filename = slug ".orplaces.sqlite", \
    .pbf_filename = slug "-latest.osm.pbf", \
    .pbf_url = "https://download.geofabrik.de/europe/france/" slug "-latest.osm.pbf" \
}

/* Geofabrik France currently exposes these historical regional extracts. */
static const OpenRideRegionDefinition REGIONS[] = {
    REGION("alsace", "Alsace"),
    REGION("aquitaine", "Aquitaine"),
    REGION("auvergne", "Auvergne"),
    REGION("basse-normandie", "Basse-Normandie"),
    REGION("bourgogne", "Bourgogne"),
    REGION("bretagne", "Bretagne"),
    REGION("centre", "Centre"),
    REGION("champagne-ardenne", "Champagne-Ardenne"),
    REGION("corse", "Corse"),
    REGION("franche-comte", "Franche-Comte"),
    REGION("guadeloupe", "Guadeloupe"),
    REGION("guyane", "Guyane"),
    REGION("haute-normandie", "Haute-Normandie"),
    REGION("ile-de-france", "Ile-de-France"),
    REGION("languedoc-roussillon", "Languedoc-Roussillon"),
    REGION("limousin", "Limousin"),
    REGION("lorraine", "Lorraine"),
    REGION("martinique", "Martinique"),
    REGION("mayotte", "Mayotte"),
    REGION("midi-pyrenees", "Midi-Pyrenees"),
    REGION("nord-pas-de-calais", "Nord-Pas-de-Calais"),
    REGION("pays-de-la-loire", "Pays de la Loire"),
    REGION("picardie", "Picardie"),
    REGION("poitou-charentes", "Poitou-Charentes"),
    REGION("provence-alpes-cote-d-azur", "Provence-Alpes-Cote d'Azur"),
    REGION("reunion", "Reunion"),
    REGION("rhone-alpes", "Rhone-Alpes")
};

#undef REGION

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "unknown error");
}

size_t openride_region_count(void)
{
    return sizeof(REGIONS) / sizeof(REGIONS[0]);
}

const OpenRideRegionDefinition *openride_region_at(size_t index)
{
    return index < openride_region_count() ? &REGIONS[index] : NULL;
}

const OpenRideRegionDefinition *openride_region_find(const char *id)
{
    if (!id || id[0] == '\0') return NULL;
    for (size_t i = 0U; i < openride_region_count(); ++i) {
        if (strcmp(REGIONS[i].id, id) == 0) return &REGIONS[i];
    }
    return NULL;
}

const OpenRideRegionDefinition *openride_region_default(void)
{
    const OpenRideRegionDefinition *region = openride_region_find("nord-pas-de-calais");
    return region ? region : &REGIONS[0];
}

bool openride_region_status_ready(const OpenRideRegionStatus *status)
{
    return status
        && status->ormap_installed
        && status->routing_installed
        && status->search_installed;
}

bool openride_region_get_status(const OpenRidePlatformPaths *paths,
                                const OpenRideRegionDefinition *region,
                                OpenRideRegionStatus *status,
                                char *error,
                                size_t error_size)
{
    if (!paths || !region || !status) {
        set_error(error, error_size, "invalid region status arguments");
        return false;
    }
    memset(status, 0, sizeof(*status));
    if (!openride_platform_path_join(status->ormap_path,
                                     sizeof(status->ormap_path),
                                     paths->maps_dir,
                                     region->ormap_filename)
        || !openride_platform_path_join(status->legacy_map_path,
                                        sizeof(status->legacy_map_path),
                                        paths->maps_dir,
                                        region->legacy_map_filename)
        || !openride_platform_path_join(status->routing_path,
                                        sizeof(status->routing_path),
                                        paths->routing_dir,
                                        region->routing_filename)
        || !openride_platform_path_join(status->search_path,
                                        sizeof(status->search_path),
                                        paths->search_dir,
                                        region->search_filename)
        || !openride_platform_path_join(status->source_pbf_path,
                                        sizeof(status->source_pbf_path),
                                        paths->downloads_dir,
                                        region->pbf_filename)) {
        set_error(error, error_size, "region path is too long");
        return false;
    }

    const double ormap_size = openride_platform_file_size_mb(status->ormap_path);
    const double legacy_size = openride_platform_file_size_mb(status->legacy_map_path);
    status->ormap_installed = ormap_size >= 0.0;
    status->legacy_map_installed = legacy_size >= 0.0;
    status->map_installed = status->ormap_installed || status->legacy_map_installed;
    if (status->ormap_installed) {
        snprintf(status->map_path, sizeof(status->map_path), "%s", status->ormap_path);
        status->map_size_mb = ormap_size;
    } else if (status->legacy_map_installed) {
        snprintf(status->map_path, sizeof(status->map_path), "%s", status->legacy_map_path);
        status->map_size_mb = legacy_size;
    }

    status->routing_size_mb = openride_platform_file_size_mb(status->routing_path);
    status->search_size_mb = openride_platform_file_size_mb(status->search_path);
    status->source_pbf_size_mb = openride_platform_file_size_mb(status->source_pbf_path);
    status->routing_installed = status->routing_size_mb >= 0.0;
    status->search_installed = status->search_size_mb >= 0.0;
    status->source_pbf_present = status->source_pbf_size_mb >= 0.0;
    status->total_size_mb = (status->map_installed ? status->map_size_mb : 0.0)
        + (status->routing_installed ? status->routing_size_mb : 0.0)
        + (status->search_installed ? status->search_size_mb : 0.0)
        + (status->source_pbf_present ? status->source_pbf_size_mb : 0.0);
    set_error(error, error_size, "");
    return true;
}

bool openride_region_remove_generated(const OpenRidePlatformPaths *paths,
                                      const OpenRideRegionDefinition *region,
                                      char *error,
                                      size_t error_size)
{
    OpenRideRegionStatus status;
    if (!openride_region_get_status(paths, region, &status, error, error_size)) return false;
    const char *files[] = {
        status.ormap_path,
        status.routing_path,
        status.search_path,
        status.source_pbf_path
    };
    for (size_t i = 0U; i < sizeof(files) / sizeof(files[0]); ++i) {
        if (openride_platform_file_exists(files[i]) && remove(files[i]) != 0) {
            set_error(error, error_size, "unable to remove region file");
            return false;
        }
    }
    set_error(error, error_size, "");
    return true;
}
