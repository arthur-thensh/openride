#include "openride/location_provider.h"

#include <string.h>

void openride_location_provider_init(OpenRideLocationProvider *provider,
                                     void *context,
                                     OpenRideLocationProviderStartFn start,
                                     OpenRideLocationProviderStopFn stop,
                                     OpenRideLocationProviderPollFn poll)
{
    if (!provider) return;
    memset(provider, 0, sizeof(*provider));
    provider->context = context;
    provider->start = start;
    provider->stop = stop;
    provider->poll = poll;
}

bool openride_location_provider_start(OpenRideLocationProvider *provider)
{
    if (!provider || !provider->poll) return false;
    if (provider->started) return true;
    if (provider->start && !provider->start(provider->context)) return false;
    provider->started = true;
    return true;
}

void openride_location_provider_stop(OpenRideLocationProvider *provider)
{
    if (!provider || !provider->started) return;
    if (provider->stop) provider->stop(provider->context);
    provider->started = false;
}

bool openride_location_provider_poll(OpenRideLocationProvider *provider,
                                     double delta_seconds,
                                     OpenRideLocationSample *sample)
{
    if (sample) memset(sample, 0, sizeof(*sample));
    if (!provider || !provider->started || !provider->poll || !sample) return false;
    return provider->poll(provider->context, delta_seconds, sample);
}
