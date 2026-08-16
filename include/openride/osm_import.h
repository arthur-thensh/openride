#ifndef OPENRIDE_OSM_IMPORT_H
#define OPENRIDE_OSM_IMPORT_H

#include "openride/routing_graph.h"
#include "openride/place_search.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


typedef struct OpenRideOSMPlaceImportStats {
    uint64_t osm_node_count;
    uint64_t indexed_place_count;
} OpenRideOSMPlaceImportStats;


typedef enum OpenRideOSMMapFeatureKind {
    OPENRIDE_OSM_MAP_FEATURE_BUILTUP_AREA = 1,
    OPENRIDE_OSM_MAP_FEATURE_WATER_AREA = 2,
    OPENRIDE_OSM_MAP_FEATURE_FOREST_AREA = 3,
    OPENRIDE_OSM_MAP_FEATURE_WATERWAY_RIVER = 4,
    OPENRIDE_OSM_MAP_FEATURE_WATERWAY_CANAL = 5,
    OPENRIDE_OSM_MAP_FEATURE_WATERWAY_STREAM = 6,
    OPENRIDE_OSM_MAP_FEATURE_WATERWAY_DRAIN = 7,

    /*
     * Development-time France Overview line classes.
     * They are deliberately kept out of normal regional .ormap generation.
     */
    OPENRIDE_OSM_MAP_FEATURE_OVERVIEW_COASTLINE = 8,
    OPENRIDE_OSM_MAP_FEATURE_OVERVIEW_MOTORWAY = 9,
    OPENRIDE_OSM_MAP_FEATURE_OVERVIEW_TRUNK = 10,
    OPENRIDE_OSM_MAP_FEATURE_OVERVIEW_PRIMARY = 11
} OpenRideOSMMapFeatureKind;

typedef struct OpenRideOSMMapFeatureStats {
    uint64_t osm_way_count;
    uint64_t selected_way_count;
    uint64_t osm_relation_count;
    uint64_t selected_relation_count;
    uint64_t relation_member_way_count;
    uint64_t referenced_node_count;
    uint64_t found_node_count;
    uint64_t emitted_feature_count;
    uint64_t multipolygon_outer_ring_count;
    uint64_t incomplete_multipolygon_count;
    uint64_t multipolygon_inner_members_ignored;
} OpenRideOSMMapFeatureStats;

typedef bool (*OpenRideOSMMapFeatureVisitor)(
    OpenRideOSMMapFeatureKind kind,
    const double *latitudes,
    const double *longitudes,
    uint32_t point_count,
    void *userdata);

typedef struct OpenRideOSMImportStats {
    uint64_t osm_way_count;
    uint64_t routable_way_count;
    uint64_t referenced_node_count;
    uint64_t found_node_count;
    uint64_t missing_node_count;
    uint64_t graph_node_count;
    uint64_t graph_edge_count;
    uint64_t graph_segment_count;
} OpenRideOSMImportStats;

/*
 * Import a standard OpenStreetMap .osm.pbf extract into OpenRide's compact
 * directed routing graph. The importer currently handles zlib-compressed PBF
 * blobs, DenseNodes/Node records and Way records. Turn-restriction relations
 * are intentionally deferred to a later version.
 */
bool openride_osm_pbf_import_graph(const char *pbf_path,
                                   OpenRideRoutingGraph *graph,
                                   OpenRideOSMImportStats *stats,
                                   char *error,
                                   size_t error_size);

bool openride_osm_pbf_import_file(const char *pbf_path,
                                  const char *graph_path,
                                  OpenRideOSMImportStats *stats,
                                  char *error,
                                  size_t error_size);

/* Build the offline place/POI search database from OSM node tags. */
bool openride_osm_pbf_import_places(const char *pbf_path,
                                    const char *database_path,
                                    OpenRideOSMPlaceImportStats *stats,
                                    char *error,
                                    size_t error_size);

/*
 * Visit the lightweight cartographic OSM geometry needed by .ormap. Closed
 * built-up/water/forest ways, linear waterways and relevant multipolygon
 * outer rings are emitted with bounded memory in three streaming PBF passes.
 * Individual buildings may be reduced to one representative point. Inner
 * multipolygon members are currently ignored by the generalized background
 * layer and are reported in the returned statistics.
 */
bool openride_osm_pbf_visit_map_features(
    const char *pbf_path,
    OpenRideOSMMapFeatureVisitor visitor,
    void *userdata,
    OpenRideOSMMapFeatureStats *stats,
    char *error,
    size_t error_size);

/*
 * Visit only the linear features needed to generate OpenRide's bundled
 * national overview: real OSM coastline plus motorway/trunk/primary roads.
 *
 * This uses two streaming PBF passes (ways, then referenced nodes) and does
 * not retain unrelated local roads, landcover or multipolygon relations.
 */
bool openride_osm_pbf_visit_overview_lines(
    const char *pbf_path,
    OpenRideOSMMapFeatureVisitor visitor,
    void *userdata,
    OpenRideOSMMapFeatureStats *stats,
    char *error,
    size_t error_size);

#endif
