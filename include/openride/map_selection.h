#ifndef OPENRIDE_MAP_SELECTION_H
#define OPENRIDE_MAP_SELECTION_H

#include <stdbool.h>

typedef struct OpenRideGeoPosition {
    double lat;
    double lon;
} OpenRideGeoPosition;

typedef enum OpenRideSelectionMarker {
    OPENRIDE_MARKER_NONE = 0,
    OPENRIDE_MARKER_START,
    OPENRIDE_MARKER_DESTINATION
} OpenRideSelectionMarker;

typedef struct OpenRideMapSelection {
    bool has_start;
    bool has_destination;
    OpenRideGeoPosition start;
    OpenRideGeoPosition destination;
} OpenRideMapSelection;

void openride_map_selection_init(OpenRideMapSelection *selection);
void openride_map_selection_clear(OpenRideMapSelection *selection);

/*
 * Add the next missing point.
 * Returns the marker that was filled, or OPENRIDE_MARKER_NONE if both points
 * were already defined.
 */
OpenRideSelectionMarker openride_map_selection_add(OpenRideMapSelection *selection,
                                                    double lat,
                                                    double lon);

void openride_map_selection_set(OpenRideMapSelection *selection,
                                OpenRideSelectionMarker marker,
                                double lat,
                                double lon);

void openride_map_selection_remove(OpenRideMapSelection *selection,
                                   OpenRideSelectionMarker marker);

bool openride_map_selection_complete(const OpenRideMapSelection *selection);

double openride_geo_distance_m(double lat1,
                               double lon1,
                               double lat2,
                               double lon2);

#endif
