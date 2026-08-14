#ifndef OPENRIDE_ROUTING_GATEWAY_INDEX_H
#define OPENRIDE_ROUTING_GATEWAY_INDEX_H

#include "openride/routing_graph.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPENRIDE_ROUTING_GATEWAY_INDEX_FORMAT_VERSION 1U

typedef struct OpenRideRoutingGatewayRecord {
    OpenRideRoutingNodeId first_node;
    OpenRideRoutingNodeId second_node;
    int32_t lat_e7;
    int32_t lon_e7;
    uint16_t road_penalty_m;
    uint16_t reserved;
} OpenRideRoutingGatewayRecord;

typedef struct OpenRideRoutingGatewayIndex {
    char first_region_id[64];
    char second_region_id[64];
    uint64_t first_graph_signature;
    uint64_t second_graph_signature;
    OpenRideRoutingGatewayRecord *records;
    uint32_t count;
    uint32_t capacity;
} OpenRideRoutingGatewayIndex;

void openride_routing_gateway_index_init(OpenRideRoutingGatewayIndex *index);
void openride_routing_gateway_index_destroy(OpenRideRoutingGatewayIndex *index);

uint64_t openride_routing_gateway_graph_signature(
    const OpenRideRoutingGraph *graph);

bool openride_routing_gateway_index_build(
    const char *first_region_id,
    const OpenRideRoutingGraph *first_graph,
    const char *second_region_id,
    const OpenRideRoutingGraph *second_graph,
    OpenRideRoutingGatewayIndex *index,
    char *error,
    size_t error_size);

bool openride_routing_gateway_index_save(
    const OpenRideRoutingGatewayIndex *index,
    const char *path,
    char *error,
    size_t error_size);

bool openride_routing_gateway_index_load(
    OpenRideRoutingGatewayIndex *index,
    const char *path,
    char *error,
    size_t error_size);

bool openride_routing_gateway_index_matches(
    const OpenRideRoutingGatewayIndex *index,
    const char *first_region_id,
    const OpenRideRoutingGraph *first_graph,
    const char *second_region_id,
    const OpenRideRoutingGraph *second_graph,
    bool *reversed);

bool openride_routing_gateway_index_pair_path(
    char *output,
    size_t output_size,
    const char *routing_dir,
    const char *first_region_id,
    const char *second_region_id);

#endif
