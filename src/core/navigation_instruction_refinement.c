#include "openride/navigation_instructions.h"
#include "openride/map_selection.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define OPENRIDE_PI 3.14159265358979323846264338327950288
#define OPENRIDE_STABLE_TURN_WINDOW_M 30.0
#define OPENRIDE_TURN_MIN_DEG 25.0
#define OPENRIDE_TURN_NORMAL_DEG 55.0
#define OPENRIDE_TURN_SHARP_DEG 120.0
#define OPENRIDE_UTURN_MIN_DEG 165.0
#define OPENRIDE_CONTINUE_GROUP_MAX_GAP_M 300.0
#define OPENRIDE_ROUNDABOUT_CONTINUE_SUPPRESS_M 120.0

bool openride_navigation_instructions_build_legacy(
    const OpenRideRoutingGraph *graph,
    const OpenRideRoute *route,
    OpenRideNavigationInstructionList *instructions,
    char *error,
    size_t error_size);

static double deg_to_rad(double degrees)
{
    return degrees * OPENRIDE_PI / 180.0;
}

static double rad_to_deg(double radians)
{
    return radians * 180.0 / OPENRIDE_PI;
}

static double bearing_deg(const OpenRideRoutePoint *a,
                          const OpenRideRoutePoint *b)
{
    const double lat1 = deg_to_rad(a->lat);
    const double lat2 = deg_to_rad(b->lat);
    const double dlon = deg_to_rad(b->lon - a->lon);
    const double y = sin(dlon) * cos(lat2);
    const double x = cos(lat1) * sin(lat2)
                   - sin(lat1) * cos(lat2) * cos(dlon);
    double heading = rad_to_deg(atan2(y, x));
    if (heading < 0.0) heading += 360.0;
    return heading;
}

static double signed_turn_deg(const OpenRideRoutePoint *before,
                              const OpenRideRoutePoint *at,
                              const OpenRideRoutePoint *after)
{
    const double incoming = bearing_deg(before, at);
    const double outgoing = bearing_deg(at, after);
    double delta = outgoing - incoming;
    while (delta <= -180.0) delta += 360.0;
    while (delta > 180.0) delta -= 360.0;
    return delta;
}

static bool route_has_navigation_context(const OpenRideRoute *route)
{
    return route
        && route->navigation_context
        && route->navigation_context_count == route->geometry_count;
}

static bool context_is_roundabout(const OpenRideRoute *route,
                                  uint32_t geometry_index)
{
    if (!route_has_navigation_context(route)
        || geometry_index >= route->navigation_context_count) {
        return false;
    }

    const uint8_t flags = route->navigation_context[geometry_index].flags;
    return (flags & (OPENRIDE_ROUTE_NAV_INCOMING_ROUNDABOUT
                     | OPENRIDE_ROUTE_NAV_OUTGOING_ROUNDABOUT)) != 0U;
}

static uint32_t stable_before_index(const OpenRideRoute *route,
                                    uint32_t geometry_index)
{
    uint32_t index = geometry_index;
    double accumulated_m = 0.0;

    while (index > 0U && accumulated_m < OPENRIDE_STABLE_TURN_WINDOW_M) {
        if (context_is_roundabout(route, index)
            || context_is_roundabout(route, index - 1U)) {
            break;
        }

        accumulated_m += openride_geo_distance_m(
            route->geometry[index].lat,
            route->geometry[index].lon,
            route->geometry[index - 1U].lat,
            route->geometry[index - 1U].lon);
        --index;
    }

    return index;
}

static uint32_t stable_after_index(const OpenRideRoute *route,
                                   uint32_t geometry_index)
{
    uint32_t index = geometry_index;
    double accumulated_m = 0.0;

    while (index + 1U < route->geometry_count
           && accumulated_m < OPENRIDE_STABLE_TURN_WINDOW_M) {
        if (context_is_roundabout(route, index)
            || context_is_roundabout(route, index + 1U)) {
            break;
        }

        accumulated_m += openride_geo_distance_m(
            route->geometry[index].lat,
            route->geometry[index].lon,
            route->geometry[index + 1U].lat,
            route->geometry[index + 1U].lon);
        ++index;
    }

    return index;
}

static bool is_directional_maneuver(OpenRideManeuverType maneuver)
{
    switch (maneuver) {
        case OPENRIDE_MANEUVER_SLIGHT_LEFT:
        case OPENRIDE_MANEUVER_LEFT:
        case OPENRIDE_MANEUVER_SHARP_LEFT:
        case OPENRIDE_MANEUVER_SLIGHT_RIGHT:
        case OPENRIDE_MANEUVER_RIGHT:
        case OPENRIDE_MANEUVER_SHARP_RIGHT:
            return true;
        default:
            return false;
    }
}

static OpenRideManeuverType classify_stable_turn(double angle_deg)
{
    const double magnitude = fabs(angle_deg);
    if (magnitude < OPENRIDE_TURN_MIN_DEG) {
        return OPENRIDE_MANEUVER_CONTINUE;
    }
    if (magnitude >= OPENRIDE_UTURN_MIN_DEG) {
        return OPENRIDE_MANEUVER_UTURN;
    }

    if (angle_deg < 0.0) {
        if (magnitude >= OPENRIDE_TURN_SHARP_DEG) {
            return OPENRIDE_MANEUVER_SHARP_LEFT;
        }
        if (magnitude >= OPENRIDE_TURN_NORMAL_DEG) {
            return OPENRIDE_MANEUVER_LEFT;
        }
        return OPENRIDE_MANEUVER_SLIGHT_LEFT;
    }

    if (magnitude >= OPENRIDE_TURN_SHARP_DEG) {
        return OPENRIDE_MANEUVER_SHARP_RIGHT;
    }
    if (magnitude >= OPENRIDE_TURN_NORMAL_DEG) {
        return OPENRIDE_MANEUVER_RIGHT;
    }
    return OPENRIDE_MANEUVER_SLIGHT_RIGHT;
}

static void refine_directional_angles(
    const OpenRideRoute *route,
    OpenRideNavigationInstructionList *instructions)
{
    if (!route_has_navigation_context(route)
        || !instructions
        || !instructions->items) {
        return;
    }

    for (uint32_t i = 0U; i < instructions->count; ++i) {
        OpenRideNavigationInstruction *instruction = &instructions->items[i];
        if (!is_directional_maneuver(instruction->maneuver)) continue;
        if (instruction->geometry_index == 0U
            || instruction->geometry_index + 1U >= route->geometry_count) {
            continue;
        }

        const uint32_t before = stable_before_index(
            route, instruction->geometry_index);
        const uint32_t after = stable_after_index(
            route, instruction->geometry_index);

        if (before == instruction->geometry_index
            || after == instruction->geometry_index) {
            continue;
        }

        const double angle = signed_turn_deg(
            &route->geometry[before],
            &route->geometry[instruction->geometry_index],
            &route->geometry[after]);

        instruction->turn_angle_deg = angle;
        instruction->maneuver = classify_stable_turn(angle);
    }
}

static void compact_instructions(OpenRideNavigationInstructionList *instructions)
{
    if (!instructions || !instructions->items || instructions->count == 0U) {
        return;
    }

    uint32_t write_index = 0U;
    for (uint32_t read_index = 0U;
         read_index < instructions->count;
         ++read_index) {
        OpenRideNavigationInstruction current = instructions->items[read_index];

        if (current.maneuver == OPENRIDE_MANEUVER_CONTINUE
            && write_index > 0U) {
            OpenRideNavigationInstruction *previous =
                &instructions->items[write_index - 1U];
            const double gap_m =
                current.distance_from_start_m - previous->distance_from_start_m;

            if (previous->maneuver == OPENRIDE_MANEUVER_ROUNDABOUT
                && gap_m >= 0.0
                && gap_m <= OPENRIDE_ROUNDABOUT_CONTINUE_SUPPRESS_M) {
                continue;
            }

            if (previous->maneuver == OPENRIDE_MANEUVER_CONTINUE
                && gap_m >= 0.0
                && gap_m <= OPENRIDE_CONTINUE_GROUP_MAX_GAP_M) {
                *previous = current;
                continue;
            }
        }

        instructions->items[write_index++] = current;
    }

    instructions->count = write_index;
}

bool openride_navigation_instructions_build(
    const OpenRideRoutingGraph *graph,
    const OpenRideRoute *route,
    OpenRideNavigationInstructionList *instructions,
    char *error,
    size_t error_size)
{
    if (!openride_navigation_instructions_build_legacy(
            graph, route, instructions, error, error_size)) {
        return false;
    }

    refine_directional_angles(route, instructions);
    compact_instructions(instructions);
    return true;
}
