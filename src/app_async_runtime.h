#ifndef OPENRIDE_APP_ASYNC_RUNTIME_H
#define OPENRIDE_APP_ASYNC_RUNTIME_H

#include "app_event_runtime.h"

typedef struct OpenRideAppAsyncContext {
    OpenRideAppEventContext *events;

    OpenRideMBTiles **map;
    OpenRideORMap **ormap;
    OpenRideMBTilesMetadata *metadata_storage;
    OpenRideMapRenderer *raster_renderer;
    OpenRidePlaceIndex **place_index;

#ifdef __ANDROID__
    OpenRideAndroidDownloadStatus *region_download_status;
#endif
} OpenRideAppAsyncContext;

void openride_app_async_update(OpenRideAppAsyncContext *context);

#endif
