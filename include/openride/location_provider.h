#ifndef OPENRIDE_LOCATION_PROVIDER_H
#define OPENRIDE_LOCATION_PROVIDER_H

#include <stdbool.h>

typedef struct OpenRideLocationSample {
    bool valid;
    double lat;
    double lon;
    double speed_mps;
    double heading_deg;
    double accuracy_m;
} OpenRideLocationSample;

typedef bool (*OpenRideLocationProviderStartFn)(void *context);
typedef void (*OpenRideLocationProviderStopFn)(void *context);
typedef bool (*OpenRideLocationProviderPollFn)(void *context,
                                               double delta_seconds,
                                               OpenRideLocationSample *sample);

typedef struct OpenRideLocationProvider {
    void *context;
    OpenRideLocationProviderStartFn start;
    OpenRideLocationProviderStopFn stop;
    OpenRideLocationProviderPollFn poll;
    bool started;
} OpenRideLocationProvider;

void openride_location_provider_init(OpenRideLocationProvider *provider,
                                     void *context,
                                     OpenRideLocationProviderStartFn start,
                                     OpenRideLocationProviderStopFn stop,
                                     OpenRideLocationProviderPollFn poll);
bool openride_location_provider_start(OpenRideLocationProvider *provider);
void openride_location_provider_stop(OpenRideLocationProvider *provider);
bool openride_location_provider_poll(OpenRideLocationProvider *provider,
                                     double delta_seconds,
                                     OpenRideLocationSample *sample);

#endif
