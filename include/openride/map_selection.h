#ifndef OPENRIDE_MAP_SELECTION_H
#define OPENRIDE_MAP_SELECTION_H

#include <stdbool.h>

#define OPENRIDE_MAP_SELECTION_REGION_ID_SIZE 64U

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
    char start_region_id[OPENRIDE_MAP_SELECTION_REGION_ID_SIZE];
    char destination_region_id[OPENRIDE_MAP_SELECTION_REGION_ID_SIZE];
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

/*
 * Trusted optional region metadata for endpoints selected via PlaceWorld.
 * NULL/empty clears the hint. Direct coordinate edits clear the corresponding
 * hint automatically so map/GPS changes cannot keep stale metadata.
 */
void openride_map_selection_set_region_hint(OpenRideMapSelection *selection,
                                            OpenRideSelectionMarker marker,
                                            const char *region_id);

const char *openride_map_selection_region_hint(
    const OpenRideMapSelection *selection,
    OpenRideSelectionMarker marker);

bool openride_map_selection_complete(const OpenRideMapSelection *selection);

double openride_geo_distance_m(double lat1,
                               double lon1,
                               double lat2,
                               double lon2);

#endif
