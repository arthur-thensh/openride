#include "openride/location_provider.h"

#include <assert.h>
#include <stdio.h>

typedef struct FakeProvider {
    int starts;
    int stops;
    int polls;
} FakeProvider;

static bool fake_start(void *context)
{
    FakeProvider *fake = context;
    ++fake->starts;
    return true;
}

static void fake_stop(void *context)
{
    FakeProvider *fake = context;
    ++fake->stops;
}

static bool fake_poll(void *context, double delta_seconds, OpenRideLocationSample *sample)
{
    FakeProvider *fake = context;
    ++fake->polls;
    sample->valid = delta_seconds > 0.0;
    sample->lat = 50.37;
    sample->lon = 3.08;
    return true;
}

int main(void)
{
    FakeProvider fake = {0};
    OpenRideLocationProvider provider;
    OpenRideLocationSample sample;
    openride_location_provider_init(&provider, &fake, fake_start, fake_stop, fake_poll);
    assert(openride_location_provider_start(&provider));
    assert(fake.starts == 1);
    assert(openride_location_provider_poll(&provider, 0.1, &sample));
    assert(sample.valid);
    assert(fake.polls == 1);
    openride_location_provider_stop(&provider);
    assert(fake.stops == 1);

    puts("Location provider tests: OK");
    return 0;
}
