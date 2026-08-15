#ifndef OPENRIDE_NAVIGATION_INSTRUCTIONS_H
#define OPENRIDE_NAVIGATION_INSTRUCTIONS_H

#include "openride/routing_engine.h"
#include "openride/routing_graph.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * navigation_instructions.c is compiled with this build symbol renamed so the
 * refinement layer can wrap the public builder. Reuse that marker to leave its
 * original next-instruction implementation untouched while every normal caller
 * is transparently routed through the v0.30 timing selector below.
 */
#if defined(openride_navigation_instructions_build)
#define OPENRIDE_NAVIGATION_INSTRUCTIONS_LEGACY_SOURCE 1
#endif

typedef enum OpenRideManeuverType {
    OPENRIDE_MANEUVER_DEPART = 0,
    OPENRIDE_MANEUVER_CONTINUE,
    OPENRIDE_MANEUVER_SLIGHT_LEFT,
    OPENRIDE_MANEUVER_LEFT,
    OPENRIDE_MANEUVER_SHARP_LEFT,
    OPENRIDE_MANEUVER_SLIGHT_RIGHT,
    OPENRIDE_MANEUVER_RIGHT,
    OPENRIDE_MANEUVER_SHARP_RIGHT,
    OPENRIDE_MANEUVER_UTURN,
    OPENRIDE_MANEUVER_ROUNDABOUT,
    OPENRIDE_MANEUVER_ARRIVE
} OpenRideManeuverType;

typedef struct OpenRideNavigationInstruction {
    OpenRideManeuverType maneuver;
    uint32_t geometry_index;
    double lat;
    double lon;
    double distance_from_start_m;
    /*
     * Along-route distance where the maneuver is actually complete. For most
     * instructions this equals distance_from_start_m. A roundabout keeps its
     * entry as the announcement point but completes at the selected exit.
     */
    double completion_distance_from_start_m;
    double turn_angle_deg;
    uint8_t roundabout_exit_number;
} OpenRideNavigationInstruction;

typedef struct OpenRideNavigationInstructionList {
    OpenRideNavigationInstruction *items;
    uint32_t count;
    double route_distance_m;
} OpenRideNavigationInstructionList;

void openride_navigation_instructions_destroy(
    OpenRideNavigationInstructionList *instructions);

bool openride_navigation_instructions_build(
    const OpenRideRoutingGraph *graph,
    const OpenRideRoute *route,
    OpenRideNavigationInstructionList *instructions,
    char *error,
    size_t error_size);

const OpenRideNavigationInstruction *openride_navigation_instructions_next(
    const OpenRideNavigationInstructionList *instructions,
    double traveled_m,
    double *distance_to_instruction_m);

/*
 * v0.30 selector. It keeps compound maneuvers active until their completion
 * point and shortens the post-maneuver hold when another maneuver follows very
 * closely. Normal translation units are aliased to this function below.
 */
const OpenRideNavigationInstruction *openride_navigation_instructions_next_timed(
    const OpenRideNavigationInstructionList *instructions,
    double traveled_m,
    double *distance_to_instruction_m);

/*
 * Return the first instruction strictly after a route distance.
 * distance_after_m receives the along-route gap from after_distance_m.
 *
 * This is intentionally independent from traveled_m so DriveMode can preview
 * a maneuver that follows the currently displayed one.
 */
const OpenRideNavigationInstruction *openride_navigation_instructions_after(
    const OpenRideNavigationInstructionList *instructions,
    double after_distance_m,
    double *distance_after_m);

const char *openride_maneuver_name(OpenRideManeuverType maneuver);

void openride_navigation_instruction_text_fr(
    const OpenRideNavigationInstruction *instruction,
    char *text,
    size_t text_size);

void openride_navigation_distance_text_fr(double distance_m,
                                           char *text,
                                           size_t text_size);

#ifndef OPENRIDE_NAVIGATION_INSTRUCTIONS_LEGACY_SOURCE
#define openride_navigation_instructions_next \
    openride_navigation_instructions_next_timed
#endif

#endif
