#include "openride/map_selection.h"

#include <math.h>
#include <string.h>

#define OPENRIDE_EARTH_RADIUS_M 6371008.8
#define OPENRIDE_PI 3.14159265358979323846

static double deg_to_rad(double degrees)
{
    return degrees * OPENRIDE_PI / 180.0;
}

void openride_map_selection_init(OpenRideMapSelection *selection)
{
    if (!selection) return;
    memset(selection, 0, sizeof(*selection));
}

void openride_map_selection_clear(OpenRideMapSelection *selection)
{
    openride_map_selection_init(selection);
}

OpenRideSelectionMarker openride_map_selection_add(OpenRideMapSelection *selection,
                                                    double lat,
                                                    double lon)
{
    if (!selection) return OPENRIDE_MARKER_NONE;

    if (!selection->has_start) {
        openride_map_selection_set(selection, OPENRIDE_MARKER_START, lat, lon);
        return OPENRIDE_MARKER_START;
    }

    if (!selection->has_destination) {
        openride_map_selection_set(selection, OPENRIDE_MARKER_DESTINATION, lat, lon);
        return OPENRIDE_MARKER_DESTINATION;
    }

    return OPENRIDE_MARKER_NONE;
}

void openride_map_selection_set(OpenRideMapSelection *selection,
                                OpenRideSelectionMarker marker,
                                double lat,
                                double lon)
{
    if (!selection) return;

    switch (marker) {
        case OPENRIDE_MARKER_START:
            selection->start.lat = lat;
            selection->start.lon = lon;
            selection->has_start = true;
            break;

        case OPENRIDE_MARKER_DESTINATION:
            selection->destination.lat = lat;
            selection->destination.lon = lon;
            selection->has_destination = true;
            break;

        case OPENRIDE_MARKER_NONE:
        default:
            break;
    }
}

void openride_map_selection_remove(OpenRideMapSelection *selection,
                                   OpenRideSelectionMarker marker)
{
    if (!selection) return;

    if (marker == OPENRIDE_MARKER_START) {
        selection->has_start = false;
    } else if (marker == OPENRIDE_MARKER_DESTINATION) {
        selection->has_destination = false;
    }
}

bool openride_map_selection_complete(const OpenRideMapSelection *selection)
{
    return selection && selection->has_start && selection->has_destination;
}

double openride_geo_distance_m(double lat1,
                               double lon1,
                               double lat2,
                               double lon2)
{
    const double phi1 = deg_to_rad(lat1);
    const double phi2 = deg_to_rad(lat2);
    const double dphi = deg_to_rad(lat2 - lat1);
    const double dlambda = deg_to_rad(lon2 - lon1);

    const double sin_dphi = sin(dphi * 0.5);
    const double sin_dlambda = sin(dlambda * 0.5);
    const double a = sin_dphi * sin_dphi
                   + cos(phi1) * cos(phi2) * sin_dlambda * sin_dlambda;
    const double clamped_a = a < 0.0 ? 0.0 : (a > 1.0 ? 1.0 : a);
    const double c = 2.0 * atan2(sqrt(clamped_a), sqrt(1.0 - clamped_a));

    return OPENRIDE_EARTH_RADIUS_M * c;
}
