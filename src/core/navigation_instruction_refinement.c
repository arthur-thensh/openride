#include "openride/navigation_instructions.h"
#include "openride/map_selection.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define OPENRIDE_PI 3.14159265358979323846264338327950288
#define OPENRIDE_STABLE_TURN_WINDOW_M 30.0
#define OPENRIDE_TURN_MIN_DEG 25.0
#define OPENRIDE_TURN_NORMAL_DEG 55.0
#define OPENRIDE_TURN_SHARP_DEG 120.0
#define OPENRIDE_UTURN_MIN_DEG 165.0
#define OPENRIDE_CONTINUE_GROUP_MAX_GAP_M 300.0
#define OPENRIDE_ROUNDABOUT_CONTINUE_SUPPRESS_M 120.0
#define OPENRIDE_CLOSE_MANEUVER_GAP_M 50.0
#define OPENRIDE_MEDIUM_MANEUVER_GAP_M 100.0
#define OPENRIDE_CLOSE_PASS_MARGIN_M 2.5
#define OPENRIDE_MEDIUM_PASS_MARGIN_M 3.5
#define OPENRIDE_DEFAULT_PASS_MARGIN_M 5.0

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

static void initialize_completion_distances(
    OpenRideNavigationInstructionList *instructions)
{
    if (!instructions || !instructions->items) return;

    for (uint32_t i = 0U; i < instructions->count; ++i) {
        instructions->items[i].completion_distance_from_start_m =
            instructions->items[i].distance_from_start_m;
    }
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

static void refine_roundabout_completion(
    const OpenRideRoute *route,
    OpenRideNavigationInstructionList *instructions)
{
    if (!route_has_navigation_context(route)
        || !route->geometry
        || route->geometry_count < 2U
        || !instructions
        || !instructions->items) {
        return;
    }

    double *cumulative = calloc(route->geometry_count, sizeof(*cumulative));
    if (!cumulative) return;

    double geometry_total_m = 0.0;
    for (uint32_t i = 1U; i < route->geometry_count; ++i) {
        geometry_total_m += openride_geo_distance_m(
            route->geometry[i - 1U].lat,
            route->geometry[i - 1U].lon,
            route->geometry[i].lat,
            route->geometry[i].lon);
        cumulative[i] = geometry_total_m;
    }

    if (!(geometry_total_m > 0.0) || !isfinite(geometry_total_m)) {
        free(cumulative);
        return;
    }

    const double route_total_m =
        route->distance_m > 0.0 ? route->distance_m : geometry_total_m;
    const double distance_scale = route_total_m / geometry_total_m;

    for (uint32_t i = 0U; i < instructions->count; ++i) {
        OpenRideNavigationInstruction *instruction = &instructions->items[i];
        if (instruction->maneuver != OPENRIDE_MANEUVER_ROUNDABOUT) continue;
        if (instruction->geometry_index >= route->geometry_count) continue;

        uint32_t exit_index = instruction->geometry_index;
        bool entered_roundabout = false;

        for (uint32_t g = instruction->geometry_index + 1U;
             g < route->geometry_count;
             ++g) {
            const uint8_t flags = route->navigation_context[g].flags;
            const bool incoming =
                (flags & OPENRIDE_ROUTE_NAV_INCOMING_ROUNDABOUT) != 0U;
            const bool outgoing =
                (flags & OPENRIDE_ROUTE_NAV_OUTGOING_ROUNDABOUT) != 0U;

            if (incoming) {
                entered_roundabout = true;
                exit_index = g;
                if (!outgoing) break;
                continue;
            }

            if (entered_roundabout) break;
            if (g > instruction->geometry_index + 1U) break;
        }

        if (exit_index > instruction->geometry_index) {
            const double completion_m = cumulative[exit_index] * distance_scale;
            if (isfinite(completion_m)
                && completion_m > instruction->distance_from_start_m) {
                instruction->completion_distance_from_start_m = completion_m;
            }
        }
    }

    free(cumulative);
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

static double instruction_completion_distance(
    const OpenRideNavigationInstruction *instruction)
{
    if (!instruction) return 0.0;

    const double entry_m = instruction->distance_from_start_m;
    const double completion_m = instruction->completion_distance_from_start_m;
    if (isfinite(completion_m) && completion_m >= entry_m) {
        return completion_m;
    }
    return entry_m;
}

static double instruction_pass_margin_m(
    const OpenRideNavigationInstructionList *instructions,
    uint32_t index)
{
    if (!instructions || !instructions->items || index >= instructions->count) {
        return OPENRIDE_DEFAULT_PASS_MARGIN_M;
    }

    const OpenRideNavigationInstruction *current = &instructions->items[index];
    if (current->maneuver == OPENRIDE_MANEUVER_ARRIVE) return 0.0;

    const double completion_m = instruction_completion_distance(current);
    for (uint32_t next_index = index + 1U;
         next_index < instructions->count;
         ++next_index) {
        const OpenRideNavigationInstruction *next = &instructions->items[next_index];
        if (next->maneuver == OPENRIDE_MANEUVER_DEPART) continue;

        const double gap_m = next->distance_from_start_m - completion_m;
        if (!isfinite(gap_m) || gap_m < 0.0) {
            return OPENRIDE_DEFAULT_PASS_MARGIN_M;
        }
        if (gap_m <= OPENRIDE_CLOSE_MANEUVER_GAP_M) {
            return OPENRIDE_CLOSE_PASS_MARGIN_M;
        }
        if (gap_m <= OPENRIDE_MEDIUM_MANEUVER_GAP_M) {
            return OPENRIDE_MEDIUM_PASS_MARGIN_M;
        }
        return OPENRIDE_DEFAULT_PASS_MARGIN_M;
    }

    return OPENRIDE_DEFAULT_PASS_MARGIN_M;
}

const OpenRideNavigationInstruction *openride_navigation_instructions_next_timed(
    const OpenRideNavigationInstructionList *instructions,
    double traveled_m,
    double *distance_to_instruction_m)
{
    if (distance_to_instruction_m) *distance_to_instruction_m = 0.0;
    if (!instructions || !instructions->items || instructions->count == 0U) {
        return NULL;
    }
    if (!isfinite(traveled_m) || traveled_m < 0.0) traveled_m = 0.0;

    for (uint32_t i = 0U; i < instructions->count; ++i) {
        const OpenRideNavigationInstruction *item = &instructions->items[i];
        if (item->maneuver == OPENRIDE_MANEUVER_DEPART) continue;

        const double completion_m = instruction_completion_distance(item);
        const double pass_margin_m = instruction_pass_margin_m(instructions, i);
        if (completion_m + pass_margin_m >= traveled_m) {
            if (distance_to_instruction_m) {
                const double delta = item->distance_from_start_m - traveled_m;
                *distance_to_instruction_m = delta > 0.0 ? delta : 0.0;
            }
            return item;
        }
    }

    const OpenRideNavigationInstruction *last =
        &instructions->items[instructions->count - 1U];
    return last->maneuver == OPENRIDE_MANEUVER_ARRIVE ? last : NULL;
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

    initialize_completion_distances(instructions);
    refine_directional_angles(route, instructions);
    refine_roundabout_completion(route, instructions);
    compact_instructions(instructions);
    return true;
}
