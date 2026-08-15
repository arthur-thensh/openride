#include "openride/navigation_instructions.h"
#include "openride/map_selection.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENRIDE_PI 3.14159265358979323846264338327950288
#define OPENRIDE_TURN_MIN_DEG 25.0
#define OPENRIDE_TURN_NORMAL_DEG 55.0
#define OPENRIDE_TURN_FALLBACK_EMIT_DEG 45.0
#define OPENRIDE_TURN_SHARP_DEG 120.0
#define OPENRIDE_UTURN_MIN_DEG 165.0
#define OPENRIDE_CONTINUE_GROUP_MAX_GAP_M 300.0

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
}

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

static const OpenRideRoutingEdge *find_edge(const OpenRideRoutingGraph *graph,
                                            OpenRideRoutingNodeId from,
                                            OpenRideRoutingNodeId to)
{
    if (!graph || from >= graph->node_count || to >= graph->node_count) return NULL;
    const OpenRideRoutingNode *node = &graph->nodes[from];
    for (uint32_t i = 0U; i < node->edge_count; ++i) {
        const OpenRideRoutingEdge *edge = &graph->edges[node->first_edge + i];
        if (edge->target == to) return edge;
    }
    return NULL;
}

static bool edge_is_roundabout(const OpenRideRoutingEdge *edge)
{
    return edge && (edge->flags & OPENRIDE_EDGE_FLAG_ROUNDABOUT) != 0U;
}

static int32_t route_node_index_for_geometry(const OpenRideRoute *route,
                                             uint32_t geometry_index)
{
    if (!route || !route->nodes) return -1;
    if (route->geometry_count == route->node_count) {
        return geometry_index < route->node_count ? (int32_t)geometry_index : -1;
    }
    if (route->geometry_count == route->node_count + 2U) {
        if (geometry_index == 0U || geometry_index > route->node_count) return -1;
        return (int32_t)(geometry_index - 1U);
    }
    return -1;
}

static bool node_has_alternative(const OpenRideRoutingGraph *graph,
                                 OpenRideRoutingNodeId node_id,
                                 OpenRideRoutingNodeId previous,
                                 OpenRideRoutingNodeId next)
{
    if (!graph || node_id >= graph->node_count) return false;
    const OpenRideRoutingNode *node = &graph->nodes[node_id];
    for (uint32_t i = 0U; i < node->edge_count; ++i) {
        const OpenRideRoutingEdge *edge = &graph->edges[node->first_edge + i];
        if (edge->target != previous && edge->target != next) return true;
    }
    return false;
}

static bool node_has_roundabout_exit(const OpenRideRoutingGraph *graph,
                                     OpenRideRoutingNodeId node_id,
                                     OpenRideRoutingNodeId previous)
{
    if (!graph || node_id >= graph->node_count) return false;
    const OpenRideRoutingNode *node = &graph->nodes[node_id];
    for (uint32_t i = 0U; i < node->edge_count; ++i) {
        const OpenRideRoutingEdge *edge = &graph->edges[node->first_edge + i];
        if (edge->target == previous) continue;
        if (!edge_is_roundabout(edge)) return true;
    }
    return false;
}

static uint8_t roundabout_exit_number(const OpenRideRoutingGraph *graph,
                                      const OpenRideRoute *route,
                                      uint32_t entry_route_node_index)
{
    if (!graph || !route || !route->nodes || route->node_count < 3U) return 0U;
    uint32_t exits = 0U;

    for (uint32_t i = entry_route_node_index + 1U; i + 1U < route->node_count; ++i) {
        const OpenRideRoutingNodeId previous = route->nodes[i - 1U];
        const OpenRideRoutingNodeId current = route->nodes[i];
        const OpenRideRoutingNodeId next = route->nodes[i + 1U];
        const OpenRideRoutingEdge *incoming = find_edge(graph, previous, current);
        const OpenRideRoutingEdge *outgoing = find_edge(graph, current, next);
        if (!edge_is_roundabout(incoming)) break;

        if (!edge_is_roundabout(outgoing)) {
            if (exits < 255U) ++exits;
            return (uint8_t)exits;
        }

        if (node_has_roundabout_exit(graph, current, previous) && exits < 255U) {
            ++exits;
        }
    }
    return exits > 0U ? (uint8_t)exits : 0U;
}

static bool route_has_navigation_context(const OpenRideRoute *route)
{
    return route
        && route->navigation_context
        && route->navigation_context_count == route->geometry_count;
}

static uint8_t roundabout_exit_number_from_context(
    const OpenRideRoute *route,
    uint32_t entry_geometry_index)
{
    if (!route_has_navigation_context(route)
        || entry_geometry_index + 1U >= route->geometry_count) {
        return 0U;
    }

    uint32_t exits = 0U;
    for (uint32_t i = entry_geometry_index + 1U;
         i + 1U < route->geometry_count;
         ++i) {
        const uint8_t flags = route->navigation_context[i].flags;
        if ((flags & OPENRIDE_ROUTE_NAV_INCOMING_ROUNDABOUT) == 0U) break;

        if ((flags & OPENRIDE_ROUTE_NAV_OUTGOING_ROUNDABOUT) == 0U) {
            if (exits < 255U) ++exits;
            return (uint8_t)exits;
        }

        if ((flags & OPENRIDE_ROUTE_NAV_HAS_ROUNDABOUT_EXIT) != 0U
            && exits < 255U) {
            ++exits;
        }
    }

    return exits > 0U ? (uint8_t)exits : 0U;
}

static OpenRideManeuverType classify_turn(double angle_deg)
{
    const double magnitude = fabs(angle_deg);
    if (magnitude >= OPENRIDE_UTURN_MIN_DEG) return OPENRIDE_MANEUVER_UTURN;
    if (angle_deg < 0.0) {
        if (magnitude >= OPENRIDE_TURN_SHARP_DEG) return OPENRIDE_MANEUVER_SHARP_LEFT;
        if (magnitude >= OPENRIDE_TURN_NORMAL_DEG) return OPENRIDE_MANEUVER_LEFT;
        return OPENRIDE_MANEUVER_SLIGHT_LEFT;
    }
    if (magnitude >= OPENRIDE_TURN_SHARP_DEG) return OPENRIDE_MANEUVER_SHARP_RIGHT;
    if (magnitude >= OPENRIDE_TURN_NORMAL_DEG) return OPENRIDE_MANEUVER_RIGHT;
    return OPENRIDE_MANEUVER_SLIGHT_RIGHT;
}

static bool append_instruction(OpenRideNavigationInstructionList *list,
                               uint32_t capacity,
                               OpenRideManeuverType maneuver,
                               uint32_t geometry_index,
                               const OpenRideRoutePoint *point,
                               double distance_from_start_m,
                               double turn_angle_deg,
                               uint8_t exit_number)
{
    if (!list || !list->items || !point) return false;

    OpenRideNavigationInstruction *item = NULL;
    if (maneuver == OPENRIDE_MANEUVER_CONTINUE && list->count > 0U) {
        OpenRideNavigationInstruction *previous = &list->items[list->count - 1U];
        const double gap_m = distance_from_start_m - previous->distance_from_start_m;
        if (previous->maneuver == OPENRIDE_MANEUVER_CONTINUE
            && gap_m >= 0.0
            && gap_m <= OPENRIDE_CONTINUE_GROUP_MAX_GAP_M) {
            item = previous;
        }
    }

    if (!item) {
        if (list->count >= capacity) return false;
        item = &list->items[list->count++];
    }

    item->maneuver = maneuver;
    item->geometry_index = geometry_index;
    item->lat = point->lat;
    item->lon = point->lon;
    item->distance_from_start_m = distance_from_start_m;
    item->turn_angle_deg = turn_angle_deg;
    item->roundabout_exit_number = exit_number;
    return true;
}

void openride_navigation_instructions_destroy(
    OpenRideNavigationInstructionList *instructions)
{
    if (!instructions) return;
    free(instructions->items);
    memset(instructions, 0, sizeof(*instructions));
}

bool openride_navigation_instructions_build(
    const OpenRideRoutingGraph *graph,
    const OpenRideRoute *route,
    OpenRideNavigationInstructionList *instructions,
    char *error,
    size_t error_size)
{
    if (!route || !instructions || !route->geometry || route->geometry_count < 2U) {
        set_error(error, error_size, "route geometry is required for instructions");
        return false;
    }

    double *cumulative = calloc(route->geometry_count, sizeof(*cumulative));
    if (!cumulative) {
        set_error(error, error_size, "unable to allocate instruction distances");
        return false;
    }

    double geometry_total_m = 0.0;
    for (uint32_t i = 1U; i < route->geometry_count; ++i) {
        geometry_total_m += openride_geo_distance_m(route->geometry[i - 1U].lat,
                                                     route->geometry[i - 1U].lon,
                                                     route->geometry[i].lat,
                                                     route->geometry[i].lon);
        cumulative[i] = geometry_total_m;
    }
    if (!(geometry_total_m > 0.0) || !isfinite(geometry_total_m)) {
        free(cumulative);
        set_error(error, error_size, "route is too short for instructions");
        return false;
    }

    const double route_total_m = route->distance_m > 0.0 ? route->distance_m : geometry_total_m;
    const double distance_scale = route_total_m / geometry_total_m;
    const uint32_t capacity = route->geometry_count + 2U;
    OpenRideNavigationInstructionList built;
    memset(&built, 0, sizeof(built));
    built.items = calloc(capacity, sizeof(*built.items));
    if (!built.items) {
        free(cumulative);
        set_error(error, error_size, "unable to allocate navigation instructions");
        return false;
    }
    built.route_distance_m = route_total_m;

    if (!append_instruction(&built,
                            capacity,
                            OPENRIDE_MANEUVER_DEPART,
                            0U,
                            &route->geometry[0],
                            0.0,
                            0.0,
                            0U)) {
        free(cumulative);
        openride_navigation_instructions_destroy(&built);
        set_error(error, error_size, "unable to build departure instruction");
        return false;
    }

    for (uint32_t i = 1U; i + 1U < route->geometry_count; ++i) {
        const double angle = signed_turn_deg(&route->geometry[i - 1U],
                                             &route->geometry[i],
                                             &route->geometry[i + 1U]);
        const double magnitude = fabs(angle);
        const int32_t route_node_index = route_node_index_for_geometry(route, i);
        bool topology_known = false;
        bool has_alternative = false;
        bool roundabout_entry = false;
        bool roundabout_member = false;
        uint8_t exit_number = 0U;

        if (route_has_navigation_context(route)) {
            topology_known = true;
            const uint8_t flags = route->navigation_context[i].flags;
            const bool incoming_roundabout =
                (flags & OPENRIDE_ROUTE_NAV_INCOMING_ROUNDABOUT) != 0U;
            const bool outgoing_roundabout =
                (flags & OPENRIDE_ROUTE_NAV_OUTGOING_ROUNDABOUT) != 0U;
            has_alternative =
                (flags & OPENRIDE_ROUTE_NAV_HAS_ALTERNATIVE) != 0U;
            roundabout_member = incoming_roundabout || outgoing_roundabout;
            roundabout_entry = !incoming_roundabout && outgoing_roundabout;
            if (roundabout_entry) {
                exit_number = roundabout_exit_number_from_context(route, i);
            }
        } else if (graph && route_node_index >= 0
                   && (uint32_t)route_node_index < route->node_count) {
            const uint32_t ni = (uint32_t)route_node_index;
            const OpenRideRoutingNodeId current = route->nodes[ni];
            const OpenRideRoutingNodeId previous = ni > 0U
                ? route->nodes[ni - 1U] : OPENRIDE_ROUTING_NODE_NONE;
            const OpenRideRoutingNodeId next = ni + 1U < route->node_count
                ? route->nodes[ni + 1U] : OPENRIDE_ROUTING_NODE_NONE;

            if (previous != OPENRIDE_ROUTING_NODE_NONE
                && next != OPENRIDE_ROUTING_NODE_NONE) {
                const OpenRideRoutingEdge *incoming = find_edge(graph, previous, current);
                const OpenRideRoutingEdge *outgoing = find_edge(graph, current, next);
                topology_known = incoming != NULL && outgoing != NULL;
                const bool incoming_roundabout = edge_is_roundabout(incoming);
                const bool outgoing_roundabout = edge_is_roundabout(outgoing);
                roundabout_member = incoming_roundabout || outgoing_roundabout;
                roundabout_entry = !incoming_roundabout && outgoing_roundabout;
                if (roundabout_entry) {
                    exit_number = roundabout_exit_number(graph, route, ni);
                }
                if (topology_known) {
                    has_alternative = node_has_alternative(graph,
                                                           current,
                                                           previous,
                                                           next);
                }
            }
        }

        OpenRideManeuverType maneuver;
        bool emit = false;
        if (roundabout_entry) {
            maneuver = OPENRIDE_MANEUVER_ROUNDABOUT;
            emit = true;
        } else if (roundabout_member) {
            emit = false;
        } else if (magnitude >= OPENRIDE_TURN_MIN_DEG) {
            maneuver = classify_turn(angle);
            if (maneuver == OPENRIDE_MANEUVER_UTURN) {
                emit = true;
            } else if (topology_known) {
                emit = has_alternative;
            } else {
                emit = magnitude >= OPENRIDE_TURN_FALLBACK_EMIT_DEG;
            }
        } else if (topology_known && has_alternative) {
            maneuver = OPENRIDE_MANEUVER_CONTINUE;
            emit = true;
        }

        if (emit) {
            append_instruction(&built,
                               capacity,
                               maneuver,
                               i,
                               &route->geometry[i],
                               cumulative[i] * distance_scale,
                               angle,
                               exit_number);
        }
    }

    append_instruction(&built,
                       capacity,
                       OPENRIDE_MANEUVER_ARRIVE,
                       route->geometry_count - 1U,
                       &route->geometry[route->geometry_count - 1U],
                       route_total_m,
                       0.0,
                       0U);

    free(cumulative);
    openride_navigation_instructions_destroy(instructions);
    *instructions = built;
    set_error(error, error_size, "");
    return true;
}

const OpenRideNavigationInstruction *openride_navigation_instructions_next(
    const OpenRideNavigationInstructionList *instructions,
    double traveled_m,
    double *distance_to_instruction_m)
{
    if (distance_to_instruction_m) *distance_to_instruction_m = 0.0;
    if (!instructions || !instructions->items || instructions->count == 0U) return NULL;
    if (!isfinite(traveled_m) || traveled_m < 0.0) traveled_m = 0.0;

    for (uint32_t i = 0U; i < instructions->count; ++i) {
        const OpenRideNavigationInstruction *item = &instructions->items[i];
        if (item->maneuver == OPENRIDE_MANEUVER_DEPART) continue;
        if (item->distance_from_start_m + 5.0 >= traveled_m) {
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

const OpenRideNavigationInstruction *openride_navigation_instructions_after(
    const OpenRideNavigationInstructionList *instructions,
    double after_distance_m,
    double *distance_after_m)
{
    if (distance_after_m) *distance_after_m = 0.0;
    if (!instructions || !instructions->items || instructions->count == 0U) {
        return NULL;
    }
    if (!isfinite(after_distance_m) || after_distance_m < 0.0) {
        after_distance_m = 0.0;
    }

    for (uint32_t i = 0U; i < instructions->count; ++i) {
        const OpenRideNavigationInstruction *item = &instructions->items[i];
        if (item->maneuver == OPENRIDE_MANEUVER_DEPART) continue;
        if (item->distance_from_start_m > after_distance_m + 0.5) {
            if (distance_after_m) {
                *distance_after_m = item->distance_from_start_m - after_distance_m;
            }
            return item;
        }
    }
    return NULL;
}

const char *openride_maneuver_name(OpenRideManeuverType maneuver)
{
    switch (maneuver) {
        case OPENRIDE_MANEUVER_DEPART:       return "depart";
        case OPENRIDE_MANEUVER_CONTINUE:     return "continuer";
        case OPENRIDE_MANEUVER_SLIGHT_LEFT:  return "legerement a gauche";
        case OPENRIDE_MANEUVER_LEFT:         return "gauche";
        case OPENRIDE_MANEUVER_SHARP_LEFT:   return "fortement a gauche";
        case OPENRIDE_MANEUVER_SLIGHT_RIGHT: return "legerement a droite";
        case OPENRIDE_MANEUVER_RIGHT:        return "droite";
        case OPENRIDE_MANEUVER_SHARP_RIGHT:  return "fortement a droite";
        case OPENRIDE_MANEUVER_UTURN:        return "demi-tour";
        case OPENRIDE_MANEUVER_ROUNDABOUT:   return "rond-point";
        case OPENRIDE_MANEUVER_ARRIVE:       return "arrivee";
        default:                             return "inconnu";
    }
}

void openride_navigation_instruction_text_fr(
    const OpenRideNavigationInstruction *instruction,
    char *text,
    size_t text_size)
{
    if (!text || text_size == 0U) return;
    if (!instruction) {
        snprintf(text, text_size, "-");
        return;
    }

    switch (instruction->maneuver) {
        case OPENRIDE_MANEUVER_DEPART:
            snprintf(text, text_size, "Demarrez");
            break;
        case OPENRIDE_MANEUVER_CONTINUE:
            snprintf(text, text_size, "Continuez tout droit");
            break;
        case OPENRIDE_MANEUVER_SLIGHT_LEFT:
            snprintf(text, text_size, "Legerement a gauche");
            break;
        case OPENRIDE_MANEUVER_LEFT:
            snprintf(text, text_size, "Tournez a gauche");
            break;
        case OPENRIDE_MANEUVER_SHARP_LEFT:
            snprintf(text, text_size, "Tournez fortement a gauche");
            break;
        case OPENRIDE_MANEUVER_SLIGHT_RIGHT:
            snprintf(text, text_size, "Legerement a droite");
            break;
        case OPENRIDE_MANEUVER_RIGHT:
            snprintf(text, text_size, "Tournez a droite");
            break;
        case OPENRIDE_MANEUVER_SHARP_RIGHT:
            snprintf(text, text_size, "Tournez fortement a droite");
            break;
        case OPENRIDE_MANEUVER_UTURN:
            snprintf(text, text_size, "Faites demi-tour");
            break;
        case OPENRIDE_MANEUVER_ROUNDABOUT:
            if (instruction->roundabout_exit_number > 0U) {
                snprintf(text,
                         text_size,
                         "Au rond-point, prenez la sortie %u",
                         (unsigned)instruction->roundabout_exit_number);
            } else {
                snprintf(text, text_size, "Entrez dans le rond-point");
            }
            break;
        case OPENRIDE_MANEUVER_ARRIVE:
            snprintf(text, text_size, "Vous etes arrive");
            break;
        default:
            snprintf(text, text_size, "Continuez");
            break;
    }
}

void openride_navigation_distance_text_fr(double distance_m,
                                           char *text,
                                           size_t text_size)
{
    if (!text || text_size == 0U) return;
    if (!isfinite(distance_m) || distance_m < 0.0) distance_m = 0.0;

    if (distance_m < 100.0) {
        const unsigned rounded = (unsigned)(floor((distance_m + 5.0) / 10.0) * 10.0);
        snprintf(text, text_size, "%u m", rounded);
    } else if (distance_m < 1000.0) {
        const unsigned rounded = (unsigned)(floor((distance_m + 25.0) / 50.0) * 50.0);
        snprintf(text, text_size, "%u m", rounded);
    } else if (distance_m < 10000.0) {
        snprintf(text, text_size, "%.1f km", distance_m / 1000.0);
    } else {
        snprintf(text, text_size, "%.0f km", distance_m / 1000.0);
    }
}
