#include "openride/simulated_location_provider.h"

#ifdef __ANDROID__
#include <SDL3/SDL.h>
#endif

#include <math.h>
#include <string.h>

#ifdef __ANDROID__
static Uint64 openride_simulated_location_audit_last_log_ns = 0U;
#endif

static bool simulated_location_start(void *userdata)
{
    OpenRideSimulatedLocationContext *context = userdata;
    if (!context || !context->simulator || !context->simulator->route) {
        return false;
    }

    openride_gps_simulator_restart(context->simulator);
#ifdef __ANDROID__
    openride_simulated_location_audit_last_log_ns = 0U;
    if (context->simulator->active) {
        SDL_Log("AUDIT_SIM_GPS_STARTED route_distance_m=%.1f speed_kph=%.1f time_scale=%.1f accuracy_m=%.1f",
                context->simulator->route->distance_m,
                fmax(0.0, context->simulator->speed_mps) * 3.6,
                context->time_scale,
                context->accuracy_m);
    }
#endif
    return context->simulator->active;
}

static void simulated_location_stop(void *userdata)
{
    OpenRideSimulatedLocationContext *context = userdata;
    if (!context || !context->simulator) return;
#ifdef __ANDROID__
    SDL_Log("AUDIT_SIM_GPS_STOPPED route_position_m=%.1f finished=%d",
            context->simulator->position_m,
            context->simulator->finished ? 1 : 0);
    openride_simulated_location_audit_last_log_ns = 0U;
#endif
    openride_gps_simulator_stop(context->simulator);
}

static bool simulated_location_poll(void *userdata,
                                    double delta_seconds,
                                    OpenRideLocationSample *sample)
{
    OpenRideSimulatedLocationContext *context = userdata;
    if (!context || !context->simulator || !sample
        || !context->simulator->route) {
        return false;
    }

    /*
     * prepare_navigation_session() can replace/reset the simulator route while
     * this provider remains selected. Resume automatically on the new route.
     */
    if (!context->simulator->active && !context->simulator->finished) {
        openride_gps_simulator_start(context->simulator);
    }

    OpenRideGPSSample gps = {0};
    if (!openride_gps_simulator_update(
            context->simulator,
            delta_seconds * context->time_scale,
            &gps)) {
        return false;
    }

    memset(sample, 0, sizeof(*sample));
    sample->valid = gps.valid;
    sample->lat = gps.lat;
    sample->lon = gps.lon;
    sample->speed_mps = gps.speed_mps;
    sample->heading_deg = gps.heading_deg;
    sample->accuracy_m = context->accuracy_m;

#ifdef __ANDROID__
    if (gps.valid) {
        const Uint64 now_ns = SDL_GetTicksNS();
        if (openride_simulated_location_audit_last_log_ns == 0U
            || now_ns - openride_simulated_location_audit_last_log_ns >= 1000000000ULL) {
            openride_simulated_location_audit_last_log_ns = now_ns;
            SDL_Log("AUDIT_SIM_GPS_SAMPLE route_position_m=%.1f route_distance_m=%.1f speed_kph=%.1f heading=%.1f lat=%.7f lon=%.7f",
                    gps.route_position_m,
                    context->simulator->route->distance_m,
                    fmax(0.0, gps.speed_mps) * 3.6,
                    gps.heading_deg,
                    gps.lat,
                    gps.lon);
        }
    }
#endif
    return true;
}

void openride_simulated_location_provider_set_time_scale(
    OpenRideSimulatedLocationContext *context,
    double time_scale)
{
    if (!context) return;
    if (!isfinite(time_scale) || time_scale <= 0.0) time_scale = 1.0;
    if (time_scale > 20.0) time_scale = 20.0;
    context->time_scale = time_scale;
}

void openride_simulated_location_provider_init(
    OpenRideLocationProvider *provider,
    OpenRideSimulatedLocationContext *context,
    OpenRideGPSSimulator *simulator,
    double time_scale,
    double accuracy_m)
{
    if (!provider || !context) return;

    memset(context, 0, sizeof(*context));
    context->simulator = simulator;
    openride_simulated_location_provider_set_time_scale(context, time_scale);
    context->accuracy_m =
        isfinite(accuracy_m) && accuracy_m >= 0.0 ? accuracy_m : 3.0;

    openride_location_provider_init(provider,
                                    context,
                                    simulated_location_start,
                                    simulated_location_stop,
                                    simulated_location_poll);
}
