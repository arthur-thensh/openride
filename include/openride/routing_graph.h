#ifndef OPENRIDE_ROUTING_GRAPH_H
#define OPENRIDE_ROUTING_GRAPH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPENRIDE_ROUTING_NODE_NONE UINT32_MAX
#define OPENRIDE_ROUTING_SEGMENT_NONE UINT32_MAX
#define OPENRIDE_ROUTING_GRAPH_FORMAT_VERSION 3U

typedef uint32_t OpenRideRoutingNodeId;

typedef enum OpenRideRoadClass {
    OPENRIDE_ROAD_UNKNOWN = 0,
    OPENRIDE_ROAD_MOTORWAY,
    OPENRIDE_ROAD_TRUNK,
    OPENRIDE_ROAD_PRIMARY,
    OPENRIDE_ROAD_SECONDARY,
    OPENRIDE_ROAD_TERTIARY,
    OPENRIDE_ROAD_UNCLASSIFIED,
    OPENRIDE_ROAD_RESIDENTIAL,
    OPENRIDE_ROAD_SERVICE,
    OPENRIDE_ROAD_LIVING_STREET,
    OPENRIDE_ROAD_TRACK,
    OPENRIDE_ROAD_PATH,
    OPENRIDE_ROAD_OTHER
} OpenRideRoadClass;

typedef enum OpenRideSurface {
    OPENRIDE_SURFACE_UNKNOWN = 0,
    OPENRIDE_SURFACE_PAVED,
    OPENRIDE_SURFACE_ASPHALT,
    OPENRIDE_SURFACE_CONCRETE,
    OPENRIDE_SURFACE_PAVING_STONES,
    OPENRIDE_SURFACE_COMPACTED,
    OPENRIDE_SURFACE_FINE_GRAVEL,
    OPENRIDE_SURFACE_GRAVEL,
    OPENRIDE_SURFACE_DIRT,
    OPENRIDE_SURFACE_GROUND,
    OPENRIDE_SURFACE_SAND,
    OPENRIDE_SURFACE_MUD,
    OPENRIDE_SURFACE_OTHER
} OpenRideSurface;

typedef enum OpenRideRoutingEdgeFlags {
    OPENRIDE_EDGE_FLAG_NONE       = 0U,
    OPENRIDE_EDGE_FLAG_UNPAVED    = 1U << 0,
    OPENRIDE_EDGE_FLAG_TOLL       = 1U << 1,
    OPENRIDE_EDGE_FLAG_FERRY      = 1U << 2,
    OPENRIDE_EDGE_FLAG_ROUNDABOUT = 1U << 3
} OpenRideRoutingEdgeFlags;

/*
 * Compact fixed-width node record. Coordinates are degrees * 1e7.
 * 16 bytes per node keeps regional graphs practical on a phone.
 */
typedef struct OpenRideRoutingNode {
    int32_t lat_e7;
    int32_t lon_e7;
    uint32_t first_edge;
    uint32_t edge_count;
} OpenRideRoutingNode;

/* Compact fixed-width directed edge record: 16 bytes. */
typedef struct OpenRideRoutingEdge {
    uint32_t target;
    uint32_t length_cm;
    uint32_t flags;
    uint8_t road_class;
    uint8_t surface;
    uint16_t max_speed_kph;
} OpenRideRoutingEdge;

typedef struct OpenRideRoutingSpatialIndex {
    int32_t min_lat_e7;
    int32_t min_lon_e7;
    uint32_t cell_size_e7;
    uint32_t rows;
    uint32_t columns;
    uint32_t cell_count;
    uint32_t *cell_offsets; /* cell_count + 1 entries */
    uint32_t *node_ids;     /* node_count entries */
} OpenRideRoutingSpatialIndex;

typedef struct OpenRideRoutingSegment {
    uint32_t a;
    uint32_t b;
} OpenRideRoutingSegment;

typedef struct OpenRideRoutingSegmentIndex {
    uint32_t segment_count;
    uint32_t ref_count;
    OpenRideRoutingSegment *segments;
    uint32_t *cell_offsets; /* spatial_index.cell_count + 1 entries */
    uint32_t *segment_ids;  /* ref_count entries */
} OpenRideRoutingSegmentIndex;

typedef struct OpenRideRoutingSnap {
    uint32_t segment_id;
    OpenRideRoutingNodeId a;
    OpenRideRoutingNodeId b;
    double fraction; /* 0 = a, 1 = b */
    double lat;
    double lon;
    double distance_m;
} OpenRideRoutingSnap;

typedef struct OpenRideRoutingGraph {
    OpenRideRoutingNode *nodes;
    OpenRideRoutingEdge *edges;
    uint32_t node_count;
    uint32_t edge_count;
    OpenRideRoutingSpatialIndex spatial_index;
    OpenRideRoutingSegmentIndex segment_index;
} OpenRideRoutingGraph;

typedef struct OpenRideRoutingEdgeAttributes {
    double length_m; /* <= 0: compute from node coordinates */
    uint32_t flags;
    OpenRideRoadClass road_class;
    OpenRideSurface surface;
    uint16_t max_speed_kph; /* 0: unknown */
} OpenRideRoutingEdgeAttributes;

typedef struct OpenRideRoutingGraphBuilder OpenRideRoutingGraphBuilder;

OpenRideRoutingEdgeAttributes openride_routing_edge_attributes_default(void);

OpenRideRoutingGraphBuilder *openride_routing_graph_builder_create(void);
void openride_routing_graph_builder_destroy(OpenRideRoutingGraphBuilder *builder);

OpenRideRoutingNodeId openride_routing_graph_builder_add_node(
    OpenRideRoutingGraphBuilder *builder,
    double lat,
    double lon);

bool openride_routing_graph_builder_add_directed_edge(
    OpenRideRoutingGraphBuilder *builder,
    OpenRideRoutingNodeId from,
    OpenRideRoutingNodeId to,
    const OpenRideRoutingEdgeAttributes *attributes);

bool openride_routing_graph_builder_add_bidirectional_edge(
    OpenRideRoutingGraphBuilder *builder,
    OpenRideRoutingNodeId a,
    OpenRideRoutingNodeId b,
    const OpenRideRoutingEdgeAttributes *attributes);

bool openride_routing_graph_builder_build(
    OpenRideRoutingGraphBuilder *builder,
    OpenRideRoutingGraph *graph,
    char *error,
    size_t error_size);

void openride_routing_graph_destroy(OpenRideRoutingGraph *graph);

bool openride_routing_graph_validate(const OpenRideRoutingGraph *graph,
                                     char *error,
                                     size_t error_size);

void openride_routing_node_geo(const OpenRideRoutingNode *node,
                               double *lat,
                               double *lon);

bool openride_routing_graph_build_spatial_index(OpenRideRoutingGraph *graph,
                                                char *error,
                                                size_t error_size);

bool openride_routing_graph_has_spatial_index(const OpenRideRoutingGraph *graph);

bool openride_routing_spatial_index_validate(const OpenRideRoutingGraph *graph,
                                             char *error,
                                             size_t error_size);

void openride_routing_spatial_index_destroy(OpenRideRoutingSpatialIndex *index);

OpenRideRoutingNodeId openride_routing_spatial_index_nearest_node(
    const OpenRideRoutingGraph *graph,
    double lat,
    double lon,
    double *distance_m);

/* Production lookup: uses the spatial index when available. */
OpenRideRoutingNodeId openride_routing_graph_nearest_node(
    const OpenRideRoutingGraph *graph,
    double lat,
    double lon,
    double *distance_m);

/* Reference implementation kept for tests and performance measurements. */
OpenRideRoutingNodeId openride_routing_graph_nearest_node_linear(
    const OpenRideRoutingGraph *graph,
    double lat,
    double lon,
    double *distance_m);

bool openride_routing_graph_build_segment_index(OpenRideRoutingGraph *graph,
                                                char *error,
                                                size_t error_size);

bool openride_routing_graph_has_segment_index(const OpenRideRoutingGraph *graph);

bool openride_routing_segment_index_validate(const OpenRideRoutingGraph *graph,
                                             char *error,
                                             size_t error_size);

void openride_routing_segment_index_destroy(OpenRideRoutingSegmentIndex *index);

bool openride_routing_graph_snap_to_segment(
    const OpenRideRoutingGraph *graph,
    double lat,
    double lon,
    double max_distance_m,
    OpenRideRoutingSnap *snap);

bool openride_routing_graph_snap_to_segment_linear(
    const OpenRideRoutingGraph *graph,
    double lat,
    double lon,
    double max_distance_m,
    OpenRideRoutingSnap *snap);

/* Deterministic little-endian on-disk format (.orgraph). */
bool openride_routing_graph_save(const OpenRideRoutingGraph *graph,
                                 const char *path,
                                 char *error,
                                 size_t error_size);

bool openride_routing_graph_load(OpenRideRoutingGraph *graph,
                                 const char *path,
                                 char *error,
                                 size_t error_size);

#endif
