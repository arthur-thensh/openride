#ifndef OPENRIDE_SIMULATED_LOCATION_PROVIDER_H
#define OPENRIDE_SIMULATED_LOCATION_PROVIDER_H

#include "openride/gps_simulator.h"
#include "openride/location_provider.h"

typedef struct OpenRideSimulatedLocationContext {
    OpenRideGPSSimulator *simulator;
    double time_scale;
    double accuracy_m;
} OpenRideSimulatedLocationContext;

void openride_simulated_location_provider_init(
    OpenRideLocationProvider *provider,
    OpenRideSimulatedLocationContext *context,
    OpenRideGPSSimulator *simulator,
    double time_scale,
    double accuracy_m);

#endif
