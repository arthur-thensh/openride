#ifndef OPENRIDE_NAVIGATION_INSTRUCTIONS_H
#define OPENRIDE_NAVIGATION_INSTRUCTIONS_H

#include "openride/routing_engine.h"
#include "openride/routing_graph.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

const char *openride_maneuver_name(OpenRideManeuverType maneuver);

void openride_navigation_instruction_text_fr(
    const OpenRideNavigationInstruction *instruction,
    char *text,
    size_t text_size);

void openride_navigation_distance_text_fr(double distance_m,
                                           char *text,
                                           size_t text_size);

#endif
