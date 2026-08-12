#ifndef OPENRIDE_GPX_H
#define OPENRIDE_GPX_H

#include "openride/routing_engine.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum OpenRideGPXPointKind {
    OPENRIDE_GPX_POINT_WAYPOINT = 0,
    OPENRIDE_GPX_POINT_ROUTE,
    OPENRIDE_GPX_POINT_TRACK
} OpenRideGPXPointKind;

typedef struct OpenRideGPXPoint {
    double lat;
    double lon;
    double elevation_m;
    bool has_elevation;
    bool starts_new_segment;
    char name[96];
} OpenRideGPXPoint;

typedef struct OpenRideGPXPointList {
    OpenRideGPXPoint *points;
    uint32_t count;
    uint32_t capacity;
} OpenRideGPXPointList;

typedef struct OpenRideGPXDocument {
    OpenRideGPXPointList waypoints;
    OpenRideGPXPointList route_points;
    OpenRideGPXPointList track_points;
    uint32_t route_count;
    uint32_t track_segment_count;
    char name[128];
} OpenRideGPXDocument;

typedef struct OpenRideGPXBounds {
    bool valid;
    double min_lat;
    double min_lon;
    double max_lat;
    double max_lon;
} OpenRideGPXBounds;

void openride_gpx_document_init(OpenRideGPXDocument *document);
void openride_gpx_document_destroy(OpenRideGPXDocument *document);
void openride_gpx_document_clear(OpenRideGPXDocument *document);

bool openride_gpx_document_append(OpenRideGPXDocument *document,
                                  OpenRideGPXPointKind kind,
                                  const OpenRideGPXPoint *point);

bool openride_gpx_load_file(const char *path,
                            OpenRideGPXDocument *document,
                            char *error,
                            size_t error_size);

bool openride_gpx_save_document(const char *path,
                                const OpenRideGPXDocument *document,
                                const char *creator,
                                char *error,
                                size_t error_size);

bool openride_gpx_save_route(const char *path,
                             const OpenRideRoute *route,
                             const char *name,
                             char *error,
                             size_t error_size);

OpenRideGPXBounds openride_gpx_document_bounds(const OpenRideGPXDocument *document);

#endif
