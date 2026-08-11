#ifndef OPENRIDE_OSM_IMPORT_H
#define OPENRIDE_OSM_IMPORT_H

#include "openride/routing_graph.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

#endif
