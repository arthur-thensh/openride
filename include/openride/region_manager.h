#ifndef OPENRIDE_REGION_MANAGER_H
#define OPENRIDE_REGION_MANAGER_H

#include "openride/platform_paths.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct OpenRideRegionDefinition {
    const char *id;
    const char *name;
    const char *ormap_filename;
    const char *legacy_map_filename;
    const char *routing_filename;
    const char *search_filename;
    const char *poly_filename;
    const char *poly_url;
    const char *pbf_filename;
    const char *pbf_url;
} OpenRideRegionDefinition;

typedef struct OpenRideRegionStatus {
    bool map_installed;
    bool ormap_installed;
    bool legacy_map_installed;
    bool routing_installed;
    bool search_installed;
    bool poly_present;
    bool source_pbf_present;
    double map_size_mb;
    double routing_size_mb;
    double search_size_mb;
    double poly_size_mb;
    double source_pbf_size_mb;
    double total_size_mb;
    char map_path[512];
    char ormap_path[512];
    char legacy_map_path[512];
    char routing_path[512];
    char search_path[512];
    char poly_path[512];
    char source_pbf_path[512];
} OpenRideRegionStatus;

size_t openride_region_count(void);
const OpenRideRegionDefinition *openride_region_at(size_t index);
const OpenRideRegionDefinition *openride_region_find(const char *id);
const OpenRideRegionDefinition *openride_region_default(void);

bool openride_region_status_ready(const OpenRideRegionStatus *status);

bool openride_region_get_status(const OpenRidePlatformPaths *paths,
                                const OpenRideRegionDefinition *region,
                                OpenRideRegionStatus *status,
                                char *error,
                                size_t error_size);

bool openride_region_remove_gateway_indexes(
    const OpenRidePlatformPaths *paths,
    const OpenRideRegionDefinition *region,
    char *error,
    size_t error_size);

bool openride_region_remove_generated(const OpenRidePlatformPaths *paths,
                                      const OpenRideRegionDefinition *region,
                                      char *error,
                                      size_t error_size);

#endif
