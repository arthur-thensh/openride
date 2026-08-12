#ifndef OPENRIDE_REGION_MANAGER_H
#define OPENRIDE_REGION_MANAGER_H

#include "openride/platform_paths.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct OpenRideRegionDefinition {
    const char *id;
    const char *name;
    const char *map_filename;
    const char *routing_filename;
    const char *search_filename;
} OpenRideRegionDefinition;

typedef struct OpenRideRegionStatus {
    bool map_installed;
    bool routing_installed;
    bool search_installed;
    double map_size_mb;
    double routing_size_mb;
    double search_size_mb;
    double total_size_mb;
    char map_path[512];
    char routing_path[512];
    char search_path[512];
} OpenRideRegionStatus;

const OpenRideRegionDefinition *openride_region_default(void);

bool openride_region_get_status(const OpenRidePlatformPaths *paths,
                                const OpenRideRegionDefinition *region,
                                OpenRideRegionStatus *status,
                                char *error,
                                size_t error_size);

#endif
