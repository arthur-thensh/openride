#ifndef OPENRIDE_REGION_INSTALL_H
#define OPENRIDE_REGION_INSTALL_H

#include "openride/ormap.h"
#include "openride/osm_import.h"
#include "openride/platform_paths.h"
#include "openride/region_manager.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum OpenRideRegionPrepareStage {
    OPENRIDE_REGION_PREPARE_IDLE = 0,
    OPENRIDE_REGION_PREPARE_ROUTING,
    OPENRIDE_REGION_PREPARE_SEARCH,
    OPENRIDE_REGION_PREPARE_MAP,
    OPENRIDE_REGION_PREPARE_FINALIZING,
    OPENRIDE_REGION_PREPARE_COMPLETE,
    OPENRIDE_REGION_PREPARE_ERROR
} OpenRideRegionPrepareStage;

typedef struct OpenRideRegionPrepareStats {
    OpenRideOSMImportStats routing;
    OpenRideOSMPlaceImportStats places;
    OpenRideORMapBuildStats map;
} OpenRideRegionPrepareStats;

typedef void (*OpenRideRegionPrepareProgress)(OpenRideRegionPrepareStage stage,
                                              const char *message,
                                              void *userdata);

bool openride_region_prepare_from_pbf(const OpenRidePlatformPaths *paths,
                                      const OpenRideRegionDefinition *region,
                                      bool keep_source_pbf,
                                      OpenRideRegionPrepareProgress progress,
                                      void *userdata,
                                      OpenRideRegionPrepareStats *stats,
                                      char *error,
                                      size_t error_size);

#endif
