#ifndef OPENRIDE_ANDROID_LOCATION_PROVIDER_H
#define OPENRIDE_ANDROID_LOCATION_PROVIDER_H

#include "openride/location_provider.h"

#include <stdbool.h>

typedef struct OpenRideAndroidLocationContext {
    double last_timestamp_s;
    bool started;
} OpenRideAndroidLocationContext;

void openride_android_location_provider_init(OpenRideLocationProvider *provider,
                                             OpenRideAndroidLocationContext *context);

#endif
