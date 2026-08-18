#include "openride/region_install.h"

#include <stdio.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "unknown error");
}

static void report(OpenRideRegionPrepareProgress progress,
                   OpenRideRegionPrepareStage stage,
                   const char *message,
                   void *userdata)
{
    if (progress) progress(stage, message, userdata);
}

static bool replace_file(const char *part,
                         const char *final_path,
                         char *error,
                         size_t error_size)
{
#ifdef _WIN32
    /* POSIX rename replaces atomically; Windows requires removing the target. */
    remove(final_path);
#endif
    if (rename(part, final_path) != 0) {
        set_error(error, error_size, "unable to finalize generated region file");
        return false;
    }
    return true;
}

bool openride_region_prepare_from_pbf(const OpenRidePlatformPaths *paths,
                                      const OpenRideRegionDefinition *region,
                                      bool keep_source_pbf,
                                      OpenRideRegionPrepareProgress progress,
                                      void *userdata,
                                      OpenRideRegionPrepareStats *stats_out,
                                      char *error,
                                      size_t error_size)
{
    if (!paths || !region) {
        set_error(error, error_size, "invalid region preparation arguments");
        return false;
    }
    OpenRideRegionStatus status;
    if (!openride_region_get_status(paths, region, &status, error, error_size)) return false;
    if (!status.source_pbf_present) {
        set_error(error, error_size, "regional OSM PBF is missing");
        return false;
    }
    if (status.source_pbf_size_mb <= 0.0) {
        set_error(error, error_size, "regional OSM PBF is empty");
        return false;
    }

    char graph_part[544], search_part[544], map_part[544], pyramid_part[544];
    snprintf(graph_part, sizeof(graph_part), "%s.part", status.routing_path);
    snprintf(search_part, sizeof(search_part), "%s.part", status.search_path);
    snprintf(map_part, sizeof(map_part), "%s.part", status.ormap_path);
    snprintf(pyramid_part, sizeof(pyramid_part), "%s.part", status.ormap11_path);
    remove(graph_part); remove(search_part); remove(map_part); remove(pyramid_part);

    OpenRideRegionPrepareStats stats;
    memset(&stats, 0, sizeof(stats));
    const bool stable_ready = openride_region_status_ready(&status);
    bool ok = true;

    if (!stable_ready) {
        report(progress,
               OPENRIDE_REGION_PREPARE_ROUTING,
               "Construction du graphe routier hors ligne",
               userdata);
        ok = openride_osm_pbf_import_file(status.source_pbf_path,
                                          graph_part,
                                          &stats.routing,
                                          error,
                                          error_size);
        if (ok) {
            ok = replace_file(
                graph_part, status.routing_path, error, error_size);
        }
    }

    if (ok && !stable_ready) {
        char gateway_error[160] = {0};
        (void)openride_region_remove_gateway_indexes(
            paths, region, gateway_error, sizeof(gateway_error));
    }

    if (ok && !stable_ready) {
        report(progress,
               OPENRIDE_REGION_PREPARE_SEARCH,
               "Construction de l'index de recherche",
               userdata);
        ok = openride_osm_pbf_import_places(status.source_pbf_path,
                                            search_part,
                                            &stats.places,
                                            error,
                                            error_size);
        if (ok) ok = replace_file(search_part, status.search_path, error, error_size);
    }

    if (ok && !stable_ready) {
        report(progress,
               OPENRIDE_REGION_PREPARE_MAP,
               "Construction de la carte OpenRide",
               userdata);
        ok = openride_ormap_build(status.source_pbf_path,
                                  status.routing_path,
                                  status.search_path,
                                  map_part,
                                  region->name,
                                  &stats.map,
                                  error,
                                  error_size);
        if (ok) ok = replace_file(map_part, status.ormap_path, error, error_size);
        /* Shortbread was only a transition dependency. Once our own map is
         * valid, release that storage automatically. An already-open legacy
         * SQLite handle remains usable until OpenRide restarts. */
        if (ok && status.legacy_map_installed) remove(status.legacy_map_path);
    }

    if (ok) {
        report(progress,
               OPENRIDE_REGION_PREPARE_PYRAMID,
               "Construction de la carte detaillee .ormap11",
               userdata);
        ok = openride_ormap_pyramid_surface_build(
            status.source_pbf_path,
            pyramid_part,
            region->name,
            &stats.pyramid_surface,
            error,
            error_size);
        if (ok) {
            ok = openride_ormap_pyramid_buildings_append(
                status.source_pbf_path,
                pyramid_part,
                &stats.pyramid_buildings,
                error,
                error_size);
        }
        if (ok) {
            ok = openride_ormap_pyramid_overlay_append(
                status.ormap_path,
                pyramid_part,
                &stats.pyramid_overlay,
                error,
                error_size);
        }
        if (ok) {
            ok = replace_file(
                pyramid_part,
                status.ormap11_path,
                error,
                error_size);
        } else if (!stable_ready) {
            /* A stale sibling must never decorate a newly generated v8 map. */
            remove(status.ormap11_path);
        }
    }

    if (ok) {
        report(progress,
               OPENRIDE_REGION_PREPARE_FINALIZING,
               "Finalisation de la region",
               userdata);
        if (!keep_source_pbf) remove(status.source_pbf_path);
        report(progress,
               OPENRIDE_REGION_PREPARE_COMPLETE,
               "Region hors ligne prete",
               userdata);
        set_error(error, error_size, "");
    } else {
        remove(graph_part); remove(search_part); remove(map_part); remove(pyramid_part);
        report(progress,
               OPENRIDE_REGION_PREPARE_ERROR,
               error && error[0] ? error : "Preparation de region impossible",
               userdata);
    }
    if (stats_out) *stats_out = stats;
    return ok;
}
