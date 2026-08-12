#ifndef OPENRIDE_GPS_SIMULATOR_H
#define OPENRIDE_GPS_SIMULATOR_H

#include "openride/routing_engine.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct OpenRideGPSSample {
    bool valid;
    bool finished;
    double lat;
    double lon;
    double heading_deg;
    double speed_mps;
    double route_position_m;
} OpenRideGPSSample;

typedef struct OpenRideGPSSimulator {
    const OpenRideRoute *route;
    double *cumulative_geometry_m;
    uint32_t geometry_count;
    double geometry_distance_m;
    double position_m;
    double speed_mps;
    double lateral_offset_m;
    bool active;
    bool finished;
} OpenRideGPSSimulator;

void openride_gps_simulator_init(OpenRideGPSSimulator *simulator);
void openride_gps_simulator_destroy(OpenRideGPSSimulator *simulator);

bool openride_gps_simulator_set_route(OpenRideGPSSimulator *simulator,
                                      const OpenRideRoute *route,
                                      double speed_kph,
                                      char *error,
                                      size_t error_size);

void openride_gps_simulator_clear_route(OpenRideGPSSimulator *simulator);
void openride_gps_simulator_start(OpenRideGPSSimulator *simulator);
void openride_gps_simulator_stop(OpenRideGPSSimulator *simulator);
void openride_gps_simulator_restart(OpenRideGPSSimulator *simulator);
bool openride_gps_simulator_toggle(OpenRideGPSSimulator *simulator);

void openride_gps_simulator_set_speed_kph(OpenRideGPSSimulator *simulator,
                                          double speed_kph);
void openride_gps_simulator_set_lateral_offset_m(OpenRideGPSSimulator *simulator,
                                                 double offset_m);

bool openride_gps_simulator_update(OpenRideGPSSimulator *simulator,
                                   double delta_seconds,
                                   OpenRideGPSSample *sample);

bool openride_gps_simulator_sample(const OpenRideGPSSimulator *simulator,
                                   OpenRideGPSSample *sample);

#endif
