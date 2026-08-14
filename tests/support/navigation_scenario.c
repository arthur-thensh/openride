#include "navigation_scenario.h"
#include "openride/map_selection.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define OPENRIDE_TEST_PI 3.14159265358979323846

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
}

static double deg_to_rad(double degrees)
{
    return degrees * OPENRIDE_TEST_PI / 180.0;
}

static double rad_to_deg(double radians)
{
    return radians * 180.0 / OPENRIDE_TEST_PI;
}

static double heading_deg(double lat_a, double lon_a, double lat_b, double lon_b)
{
    const double lat1 = deg_to_rad(lat_a);
    const double lat2 = deg_to_rad(lat_b);
    const double dlon = deg_to_rad(lon_b - lon_a);
    const double y = sin(dlon) * cos(lat2);
    const double x = cos(lat1) * sin(lat2)
                   - sin(lat1) * cos(lat2) * cos(dlon);
    double heading = rad_to_deg(atan2(y, x));
    if (heading < 0.0) heading += 360.0;
    return heading;
}

static bool record_sample(
    OpenRideNavigationEngine *navigation,
    OpenRideNavigationSession *session,
    const OpenRideNavigationInstructionList *instructions,
    double lat,
    double lon,
    double speed_mps,
    double heading,
    double delta_s,
    bool verbose,
    OpenRideNavigationStatus *previous_status,
    bool *has_previous_status,
    const OpenRideNavigationInstruction **previous_instruction,
    OpenRideNavigationScenarioResult *result)
{
    OpenRideNavigationState state = {0};
    if (!openride_navigation_engine_update(navigation,
                                           lat,
                                           lon,
                                           speed_mps,
                                           heading,
                                           &state)) {
        return false;
    }

    openride_navigation_session_update(session,
                                       &state,
                                       lat,
                                       lon,
                                       speed_mps,
                                       delta_s);

    result->sample_count++;
    result->elapsed_s += delta_s;
    if (state.distance_from_route_m > result->max_distance_from_route_m) {
        result->max_distance_from_route_m = state.distance_from_route_m;
    }
    result->final_progress_ratio = state.progress_ratio;
    result->final_remaining_m = state.remaining_m;

    double distance_to_instruction_m = 0.0;
    const OpenRideNavigationInstruction *next_instruction =
        openride_navigation_instructions_next(
            instructions,
            state.traveled_m,
            &distance_to_instruction_m);

    if (next_instruction && next_instruction != *previous_instruction) {
        if (result->instruction_event_count
            < OPENRIDE_NAVIGATION_SCENARIO_MAX_INSTRUCTION_EVENTS) {
            OpenRideNavigationScenarioInstructionEvent *event =
                &result->instruction_events[result->instruction_event_count++];
            event->maneuver = next_instruction->maneuver;
            event->geometry_index = next_instruction->geometry_index;
            event->roundabout_exit_number =
                next_instruction->roundabout_exit_number;
            event->first_seen_traveled_m = state.traveled_m;
            event->first_seen_distance_m = distance_to_instruction_m;
        } else {
            result->instruction_event_overflow = true;
        }

        if (verbose) {
            if (next_instruction->maneuver == OPENRIDE_MANEUVER_ROUNDABOUT
                && next_instruction->roundabout_exit_number > 0U) {
                printf("  t=%6.1fs  >>> instruction: %s, sortie %u (dans %.1f m)\n",
                       result->elapsed_s,
                       openride_maneuver_name(next_instruction->maneuver),
                       (unsigned)next_instruction->roundabout_exit_number,
                       distance_to_instruction_m);
            } else {
                printf("  t=%6.1fs  >>> instruction: %s (dans %.1f m)\n",
                       result->elapsed_s,
                       openride_maneuver_name(next_instruction->maneuver),
                       distance_to_instruction_m);
            }
        }

        *previous_instruction = next_instruction;
    }

    if (state.status == OPENRIDE_NAVIGATION_ON_ROUTE) {
        result->on_route_sample_count++;
    } else if (state.status == OPENRIDE_NAVIGATION_OFF_ROUTE) {
        result->off_route_sample_count++;
        result->saw_off_route = true;
    } else if (state.status == OPENRIDE_NAVIGATION_ARRIVED) {
        result->arrived_sample_count++;
        result->saw_arrival = true;
    }

    const bool changed = !*has_previous_status || state.status != *previous_status;
    if (changed) {
        result->status_transition_count++;
        if (*has_previous_status
            && *previous_status == OPENRIDE_NAVIGATION_OFF_ROUTE
            && (state.status == OPENRIDE_NAVIGATION_ON_ROUTE
                || state.status == OPENRIDE_NAVIGATION_ARRIVED)) {
            result->saw_return_to_route = true;
        }
        if (verbose) {
            printf("  t=%6.1fs  %-16s  ecart=%6.1f m  progression=%6.2f %%\n",
                   result->elapsed_s,
                   openride_navigation_status_name(state.status),
                   state.distance_from_route_m,
                   state.progress_ratio * 100.0);
        }
        *previous_status = state.status;
        *has_previous_status = true;
    }

    if (openride_navigation_session_take_reroute_request(session)) {
        result->reroute_requested = true;
        if (verbose) {
            printf("  t=%6.1fs  >>> demande de recalcul automatique\n",
                   result->elapsed_s);
        }

        /*
         * This runner validates the trigger only. It does not calculate and
         * install a replacement route, so a real reroute has not happened.
         * Disable further automatic requests for the remainder of this trace;
         * otherwise the unchanged off-route fixture would request again after
         * every cooldown and make the scenario output misleading.
         */
        openride_navigation_session_set_auto_reroute(session, false);
    }

    return true;
}

bool openride_test_navigation_scenario_run(
    const char *name,
    const OpenRideRoute *route,
    const OpenRideNavigationScenarioWaypoint *waypoints,
    uint32_t waypoint_count,
    double sample_step_m,
    bool verbose,
    OpenRideNavigationScenarioResult *result,
    char *error,
    size_t error_size)
{
    if (!route || !waypoints || waypoint_count < 2U || !result) {
        set_error(error, error_size, "invalid navigation scenario");
        return false;
    }
    if (!isfinite(sample_step_m) || sample_step_m <= 0.0) {
        set_error(error, error_size, "sample_step_m must be positive");
        return false;
    }

    memset(result, 0, sizeof(*result));

    OpenRideNavigationEngine navigation;
    OpenRideNavigationSession session;
    OpenRideNavigationInstructionList instructions = {0};
    openride_navigation_engine_init(&navigation);
    openride_navigation_session_init(&session);

    char instruction_error[160] = {0};
    if (!openride_navigation_instructions_build(
            NULL,
            route,
            &instructions,
            instruction_error,
            sizeof(instruction_error))) {
        openride_navigation_engine_destroy(&navigation);
        set_error(error,
                  error_size,
                  instruction_error[0]
                      ? instruction_error
                      : "unable to build scenario navigation instructions");
        return false;
    }

    char navigation_error[160] = {0};
    if (!openride_navigation_engine_set_route(&navigation,
                                              route,
                                              navigation_error,
                                              sizeof(navigation_error))) {
        openride_navigation_engine_destroy(&navigation);
        openride_navigation_instructions_destroy(&instructions);
        set_error(error, error_size,
                  navigation_error[0] ? navigation_error
                                      : "unable to initialize navigation engine");
        return false;
    }

    if (verbose) printf("\n[scenario] %s\n", name && name[0] ? name : "sans nom");

    OpenRideNavigationStatus previous_status = OPENRIDE_NAVIGATION_INACTIVE;
    bool has_previous_status = false;
    const OpenRideNavigationInstruction *previous_instruction = NULL;

    double initial_speed_kph = waypoints[0].speed_kph;
    if (!isfinite(initial_speed_kph) || initial_speed_kph <= 0.0) initial_speed_kph = 50.0;
    const double initial_speed_mps = initial_speed_kph / 3.6;
    const double initial_heading = heading_deg(waypoints[0].lat, waypoints[0].lon,
                                               waypoints[1].lat, waypoints[1].lon);

    bool ok = record_sample(&navigation,
                            &session,
                            &instructions,
                            waypoints[0].lat,
                            waypoints[0].lon,
                            initial_speed_mps,
                            initial_heading,
                            0.0,
                            verbose,
                            &previous_status,
                            &has_previous_status,
                            &previous_instruction,
                            result);

    for (uint32_t leg = 0U; ok && leg + 1U < waypoint_count; ++leg) {
        const OpenRideNavigationScenarioWaypoint *a = &waypoints[leg];
        const OpenRideNavigationScenarioWaypoint *b = &waypoints[leg + 1U];

        const double leg_distance_m = openride_geo_distance_m(a->lat, a->lon, b->lat, b->lon);
        if (!isfinite(leg_distance_m) || leg_distance_m < 0.0) {
            ok = false;
            break;
        }

        double speed_kph = a->speed_kph;
        if (!isfinite(speed_kph) || speed_kph <= 0.0) speed_kph = 50.0;
        if (speed_kph > 160.0) speed_kph = 160.0;
        const double speed_mps = speed_kph / 3.6;

        uint32_t steps = (uint32_t)ceil(leg_distance_m / sample_step_m);
        if (steps < 1U) steps = 1U;

        const double heading = heading_deg(a->lat, a->lon, b->lat, b->lon);
        const double step_distance_m = leg_distance_m / (double)steps;
        const double delta_s = speed_mps > 1e-9 ? step_distance_m / speed_mps : 0.0;

        if (verbose && a->label && a->label[0]) printf("  -- %s\n", a->label);

        for (uint32_t step = 1U; step <= steps; ++step) {
            const double fraction = (double)step / (double)steps;
            const double lat = a->lat + (b->lat - a->lat) * fraction;
            const double lon = a->lon + (b->lon - a->lon) * fraction;
            if (!record_sample(&navigation,
                               &session,
                               &instructions,
                               lat,
                               lon,
                               speed_mps,
                               heading,
                               delta_s,
                               verbose,
                               &previous_status,
                               &has_previous_status,
                               &previous_instruction,
                               result)) {
                ok = false;
                break;
            }
        }
    }

    if (verbose && ok) {
        printf("  -> %u echantillons | %.1f s | ecart max %.1f m | "
               "progression %.2f %% | recalcul %s | instructions %u\n",
               result->sample_count,
               result->elapsed_s,
               result->max_distance_from_route_m,
               result->final_progress_ratio * 100.0,
               result->reroute_requested ? "OUI" : "NON",
               result->instruction_event_count);
    }

    openride_navigation_engine_destroy(&navigation);
    openride_navigation_instructions_destroy(&instructions);

    if (!ok) {
        set_error(error, error_size, "navigation scenario update failed");
        return false;
    }

    set_error(error, error_size, "");
    return true;
}
