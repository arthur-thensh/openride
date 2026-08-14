#include "openride/routing_gateway_index.h"

#include "openride/platform_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENRIDE_GATEWAY_INDEX_MAGIC_SIZE 8U
#define OPENRIDE_GATEWAY_INDEX_ID_SIZE 64U
#define OPENRIDE_GATEWAY_INDEX_MAX_RECORDS 10000000U

static const unsigned char GATEWAY_INDEX_MAGIC[OPENRIDE_GATEWAY_INDEX_MAGIC_SIZE] = {
    'O', 'R', 'G', 'A', 'T', 'E', '0', '1'
};

typedef struct GatewayHashEntry {
    int32_t lat_e7;
    int32_t lon_e7;
    OpenRideRoutingNodeId node_id;
    bool occupied;
} GatewayHashEntry;

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "");
}

static uint64_t fnv_mix_byte(uint64_t hash, uint8_t value)
{
    hash ^= (uint64_t)value;
    hash *= UINT64_C(1099511628211);
    return hash;
}

static uint64_t fnv_mix_u32(uint64_t hash, uint32_t value)
{
    hash = fnv_mix_byte(hash, (uint8_t)(value & 0xffU));
    hash = fnv_mix_byte(hash, (uint8_t)((value >> 8U) & 0xffU));
    hash = fnv_mix_byte(hash, (uint8_t)((value >> 16U) & 0xffU));
    hash = fnv_mix_byte(hash, (uint8_t)((value >> 24U) & 0xffU));
    return hash;
}

uint64_t openride_routing_gateway_graph_signature(
    const OpenRideRoutingGraph *graph)
{
    if (!graph) return 0U;

    uint64_t hash = UINT64_C(1469598103934665603);
    hash = fnv_mix_u32(hash, OPENRIDE_ROUTING_GRAPH_FORMAT_VERSION);
    hash = fnv_mix_u32(hash, graph->node_count);
    hash = fnv_mix_u32(hash, graph->edge_count);
    hash = fnv_mix_u32(hash, graph->spatial_index.cell_count);
    hash = fnv_mix_u32(hash, graph->segment_index.segment_count);
    hash = fnv_mix_u32(hash, graph->segment_index.ref_count);

    if (graph->nodes && graph->node_count > 0U) {
        const uint32_t samples = graph->node_count < 16U ? graph->node_count : 16U;
        for (uint32_t i = 0U; i < samples; ++i) {
            const uint32_t node_id = samples == 1U
                ? 0U
                : (uint32_t)(((uint64_t)i * (graph->node_count - 1U))
                             / (uint64_t)(samples - 1U));
            const OpenRideRoutingNode *node = &graph->nodes[node_id];
            hash = fnv_mix_u32(hash, node_id);
            hash = fnv_mix_u32(hash, (uint32_t)node->lat_e7);
            hash = fnv_mix_u32(hash, (uint32_t)node->lon_e7);
            hash = fnv_mix_u32(hash, node->first_edge);
            hash = fnv_mix_u32(hash, node->edge_count);
        }
    }

    if (graph->edges && graph->edge_count > 0U) {
        const uint32_t samples = graph->edge_count < 16U ? graph->edge_count : 16U;
        for (uint32_t i = 0U; i < samples; ++i) {
            const uint32_t edge_id = samples == 1U
                ? 0U
                : (uint32_t)(((uint64_t)i * (graph->edge_count - 1U))
                             / (uint64_t)(samples - 1U));
            const OpenRideRoutingEdge *edge = &graph->edges[edge_id];
            hash = fnv_mix_u32(hash, edge_id);
            hash = fnv_mix_u32(hash, edge->target);
            hash = fnv_mix_u32(hash, edge->length_cm);
            hash = fnv_mix_u32(hash, edge->flags);
            hash = fnv_mix_u32(hash, (uint32_t)edge->road_class);
            hash = fnv_mix_u32(hash, (uint32_t)edge->surface);
            hash = fnv_mix_u32(hash, (uint32_t)edge->max_speed_kph);
        }
    }

    return hash;
}

void openride_routing_gateway_index_init(OpenRideRoutingGatewayIndex *index)
{
    if (!index) return;
    memset(index, 0, sizeof(*index));
}

void openride_routing_gateway_index_destroy(OpenRideRoutingGatewayIndex *index)
{
    if (!index) return;
    free(index->records);
    memset(index, 0, sizeof(*index));
}

static uint8_t best_road_class(const OpenRideRoutingGraph *graph,
                               OpenRideRoutingNodeId node_id)
{
    if (!graph || node_id >= graph->node_count) return (uint8_t)OPENRIDE_ROAD_OTHER;
    const OpenRideRoutingNode *node = &graph->nodes[node_id];
    uint8_t best = (uint8_t)OPENRIDE_ROAD_OTHER;

    for (uint32_t i = 0U; i < node->edge_count; ++i) {
        const OpenRideRoutingEdge *edge = &graph->edges[node->first_edge + i];
        if (edge->road_class > 0U && edge->road_class < best) {
            best = edge->road_class;
        }
    }
    return best;
}

static uint16_t road_penalty(uint8_t road_class)
{
    if (road_class <= (uint8_t)OPENRIDE_ROAD_PRIMARY) return 0U;
    if (road_class == (uint8_t)OPENRIDE_ROAD_SECONDARY) return 500U;
    if (road_class == (uint8_t)OPENRIDE_ROAD_TERTIARY) return 1200U;
    return 3000U;
}

static bool inside_index_grid(const OpenRideRoutingGraph *graph,
                              const OpenRideRoutingNode *node)
{
    if (!graph || !node || !openride_routing_graph_has_spatial_index(graph)) return true;

    const OpenRideRoutingSpatialIndex *spatial = &graph->spatial_index;
    const int64_t max_lat = (int64_t)spatial->min_lat_e7
        + (int64_t)spatial->rows * (int64_t)spatial->cell_size_e7;
    const int64_t max_lon = (int64_t)spatial->min_lon_e7
        + (int64_t)spatial->columns * (int64_t)spatial->cell_size_e7;

    return (int64_t)node->lat_e7 >= spatial->min_lat_e7
        && (int64_t)node->lat_e7 <= max_lat
        && (int64_t)node->lon_e7 >= spatial->min_lon_e7
        && (int64_t)node->lon_e7 <= max_lon;
}

static uint64_t coordinate_key(int32_t lat_e7, int32_t lon_e7)
{
    return ((uint64_t)(uint32_t)lat_e7 << 32U) | (uint64_t)(uint32_t)lon_e7;
}

static uint64_t hash_mix(uint64_t value)
{
    value ^= value >> 33U;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33U;
    value *= UINT64_C(0xc4ceb9fe1a85ec53);
    value ^= value >> 33U;
    return value;
}

static size_t hash_capacity(uint32_t node_count)
{
    size_t wanted = (size_t)node_count;
    if (wanted > SIZE_MAX / 2U) return 0U;
    wanted *= 2U;
    if (wanted < 16U) wanted = 16U;

    size_t capacity = 16U;
    while (capacity < wanted) {
        if (capacity > SIZE_MAX / 2U) return 0U;
        capacity *= 2U;
    }
    return capacity;
}

static bool hash_insert(GatewayHashEntry *table,
                        size_t capacity,
                        const OpenRideRoutingNode *node,
                        OpenRideRoutingNodeId node_id)
{
    if (!table || capacity == 0U || !node) return false;

    const size_t mask = capacity - 1U;
    const uint64_t key = coordinate_key(node->lat_e7, node->lon_e7);
    size_t slot = (size_t)(hash_mix(key) & (uint64_t)mask);

    for (size_t probe = 0U; probe < capacity; ++probe) {
        GatewayHashEntry *entry = &table[slot];
        if (!entry->occupied) {
            entry->lat_e7 = node->lat_e7;
            entry->lon_e7 = node->lon_e7;
            entry->node_id = node_id;
            entry->occupied = true;
            return true;
        }

        if (entry->lat_e7 == node->lat_e7 && entry->lon_e7 == node->lon_e7) {
            return true;
        }

        slot = (slot + 1U) & mask;
    }
    return false;
}

static OpenRideRoutingNodeId hash_find(const GatewayHashEntry *table,
                                       size_t capacity,
                                       int32_t lat_e7,
                                       int32_t lon_e7)
{
    if (!table || capacity == 0U) return OPENRIDE_ROUTING_NODE_NONE;

    const size_t mask = capacity - 1U;
    const uint64_t key = coordinate_key(lat_e7, lon_e7);
    size_t slot = (size_t)(hash_mix(key) & (uint64_t)mask);

    for (size_t probe = 0U; probe < capacity; ++probe) {
        const GatewayHashEntry *entry = &table[slot];
        if (!entry->occupied) return OPENRIDE_ROUTING_NODE_NONE;

        if (entry->lat_e7 == lat_e7 && entry->lon_e7 == lon_e7) {
            return entry->node_id;
        }

        slot = (slot + 1U) & mask;
    }
    return OPENRIDE_ROUTING_NODE_NONE;
}

static bool reserve_records(OpenRideRoutingGatewayIndex *index, uint32_t required)
{
    if (!index) return false;
    if (required <= index->capacity) return true;

    uint32_t capacity = index->capacity > 0U ? index->capacity : 256U;
    while (capacity < required) {
        if (capacity > UINT32_MAX / 2U) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }

    if ((size_t)capacity > SIZE_MAX / sizeof(*index->records)) return false;

    OpenRideRoutingGatewayRecord *records =
        realloc(index->records, (size_t)capacity * sizeof(*records));
    if (!records) return false;

    index->records = records;
    index->capacity = capacity;
    return true;
}

bool openride_routing_gateway_index_build(
    const char *first_region_id,
    const OpenRideRoutingGraph *first_graph,
    const char *second_region_id,
    const OpenRideRoutingGraph *second_graph,
    OpenRideRoutingGatewayIndex *index,
    char *error,
    size_t error_size)
{
    if (!first_region_id || !second_region_id || !first_graph || !second_graph
        || !index || first_graph == second_graph
        || first_region_id[0] == '\0' || second_region_id[0] == '\0') {
        set_error(error, error_size, "invalid routing gateway index build request");
        return false;
    }

    if (strlen(first_region_id) >= sizeof(index->first_region_id)
        || strlen(second_region_id) >= sizeof(index->second_region_id)) {
        set_error(error, error_size, "routing gateway region id is too long");
        return false;
    }

    openride_routing_gateway_index_destroy(index);
    snprintf(index->first_region_id, sizeof(index->first_region_id), "%s", first_region_id);
    snprintf(index->second_region_id, sizeof(index->second_region_id), "%s", second_region_id);
    index->first_graph_signature = openride_routing_gateway_graph_signature(first_graph);
    index->second_graph_signature = openride_routing_gateway_graph_signature(second_graph);

    const bool scan_first = first_graph->node_count <= second_graph->node_count;
    const OpenRideRoutingGraph *scan_graph = scan_first ? first_graph : second_graph;
    const OpenRideRoutingGraph *lookup_graph = scan_first ? second_graph : first_graph;

    const size_t capacity = hash_capacity(lookup_graph->node_count);
    if (capacity == 0U || capacity > SIZE_MAX / sizeof(GatewayHashEntry)) {
        openride_routing_gateway_index_destroy(index);
        set_error(error, error_size, "routing gateway hash is too large");
        return false;
    }

    GatewayHashEntry *table = calloc(capacity, sizeof(*table));
    if (!table) {
        openride_routing_gateway_index_destroy(index);
        set_error(error, error_size, "unable to allocate routing gateway hash");
        return false;
    }

    for (uint32_t node_id = 0U; node_id < lookup_graph->node_count; ++node_id) {
        const OpenRideRoutingNode *node = &lookup_graph->nodes[node_id];
        if (node->edge_count == 0U) continue;
        if (!hash_insert(table, capacity, node, node_id)) {
            free(table);
            openride_routing_gateway_index_destroy(index);
            set_error(error, error_size, "unable to build routing gateway hash");
            return false;
        }
    }

    for (uint32_t scan_id = 0U; scan_id < scan_graph->node_count; ++scan_id) {
        const OpenRideRoutingNode *node = &scan_graph->nodes[scan_id];
        if (node->edge_count == 0U || !inside_index_grid(lookup_graph, node)) continue;

        const OpenRideRoutingNodeId match_id =
            hash_find(table, capacity, node->lat_e7, node->lon_e7);
        if (match_id == OPENRIDE_ROUTING_NODE_NONE) continue;

        if (index->count >= OPENRIDE_GATEWAY_INDEX_MAX_RECORDS
            || !reserve_records(index, index->count + 1U)) {
            free(table);
            openride_routing_gateway_index_destroy(index);
            set_error(error, error_size, "routing gateway index is too large");
            return false;
        }

        OpenRideRoutingGatewayRecord *record = &index->records[index->count++];
        memset(record, 0, sizeof(*record));
        record->first_node = scan_first ? scan_id : match_id;
        record->second_node = scan_first ? match_id : scan_id;
        record->lat_e7 = node->lat_e7;
        record->lon_e7 = node->lon_e7;

        const uint32_t penalty =
            (uint32_t)road_penalty(best_road_class(first_graph, record->first_node))
            + (uint32_t)road_penalty(best_road_class(second_graph, record->second_node));
        record->road_penalty_m = penalty > UINT16_MAX ? UINT16_MAX : (uint16_t)penalty;
    }

    free(table);
    set_error(error, error_size, "");
    return true;
}

static bool write_bytes(FILE *file, const void *data, size_t size)
{
    return file && data && fwrite(data, 1U, size, file) == size;
}

static bool read_bytes(FILE *file, void *data, size_t size)
{
    return file && data && fread(data, 1U, size, file) == size;
}

static bool write_u16(FILE *file, uint16_t value)
{
    const unsigned char bytes[2] = {
        (unsigned char)(value & 0xffU),
        (unsigned char)((value >> 8U) & 0xffU)
    };
    return write_bytes(file, bytes, sizeof(bytes));
}

static bool write_u32(FILE *file, uint32_t value)
{
    const unsigned char bytes[4] = {
        (unsigned char)(value & 0xffU),
        (unsigned char)((value >> 8U) & 0xffU),
        (unsigned char)((value >> 16U) & 0xffU),
        (unsigned char)((value >> 24U) & 0xffU)
    };
    return write_bytes(file, bytes, sizeof(bytes));
}

static bool write_u64(FILE *file, uint64_t value)
{
    return write_u32(file, (uint32_t)(value & UINT64_C(0xffffffff)))
        && write_u32(file, (uint32_t)(value >> 32U));
}

static bool read_u16(FILE *file, uint16_t *value)
{
    unsigned char bytes[2];
    if (!value || !read_bytes(file, bytes, sizeof(bytes))) return false;
    *value = (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
    return true;
}

static bool read_u32(FILE *file, uint32_t *value)
{
    unsigned char bytes[4];
    if (!value || !read_bytes(file, bytes, sizeof(bytes))) return false;
    *value = (uint32_t)bytes[0]
           | ((uint32_t)bytes[1] << 8U)
           | ((uint32_t)bytes[2] << 16U)
           | ((uint32_t)bytes[3] << 24U);
    return true;
}

static bool read_u64(FILE *file, uint64_t *value)
{
    uint32_t low = 0U;
    uint32_t high = 0U;
    if (!value || !read_u32(file, &low) || !read_u32(file, &high)) return false;
    *value = (uint64_t)low | ((uint64_t)high << 32U);
    return true;
}

bool openride_routing_gateway_index_save(
    const OpenRideRoutingGatewayIndex *index,
    const char *path,
    char *error,
    size_t error_size)
{
    if (!index || !path || path[0] == '\0'
        || index->first_region_id[0] == '\0'
        || index->second_region_id[0] == '\0'
        || (index->count > 0U && !index->records)) {
        set_error(error, error_size, "invalid routing gateway index save request");
        return false;
    }

    char part_path[640];
    const int part_written = snprintf(part_path, sizeof(part_path), "%s.part", path);
    if (part_written < 0 || (size_t)part_written >= sizeof(part_path)) {
        set_error(error, error_size, "routing gateway index path is too long");
        return false;
    }

    remove(part_path);
    FILE *file = fopen(part_path, "wb");
    if (!file) {
        set_error(error, error_size, "unable to create routing gateway index");
        return false;
    }

    char first_id[OPENRIDE_GATEWAY_INDEX_ID_SIZE] = {0};
    char second_id[OPENRIDE_GATEWAY_INDEX_ID_SIZE] = {0};
    snprintf(first_id, sizeof(first_id), "%s", index->first_region_id);
    snprintf(second_id, sizeof(second_id), "%s", index->second_region_id);

    bool ok =
        write_bytes(file, GATEWAY_INDEX_MAGIC, sizeof(GATEWAY_INDEX_MAGIC))
        && write_u32(file, OPENRIDE_ROUTING_GATEWAY_INDEX_FORMAT_VERSION)
        && write_bytes(file, first_id, sizeof(first_id))
        && write_bytes(file, second_id, sizeof(second_id))
        && write_u64(file, index->first_graph_signature)
        && write_u64(file, index->second_graph_signature)
        && write_u32(file, index->count);

    for (uint32_t i = 0U; ok && i < index->count; ++i) {
        const OpenRideRoutingGatewayRecord *record = &index->records[i];
        ok = write_u32(file, record->first_node)
          && write_u32(file, record->second_node)
          && write_u32(file, (uint32_t)record->lat_e7)
          && write_u32(file, (uint32_t)record->lon_e7)
          && write_u16(file, record->road_penalty_m)
          && write_u16(file, 0U);
    }

    if (fclose(file) != 0) ok = false;

    if (!ok) {
        remove(part_path);
        set_error(error, error_size, "unable to write routing gateway index");
        return false;
    }

    remove(path);
    if (rename(part_path, path) != 0) {
        remove(part_path);
        set_error(error, error_size, "unable to finalize routing gateway index");
        return false;
    }

    set_error(error, error_size, "");
    return true;
}

bool openride_routing_gateway_index_load(
    OpenRideRoutingGatewayIndex *index,
    const char *path,
    char *error,
    size_t error_size)
{
    if (!index || !path || path[0] == '\0') {
        set_error(error, error_size, "invalid routing gateway index load request");
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        set_error(error, error_size, "routing gateway index is missing");
        return false;
    }

    openride_routing_gateway_index_destroy(index);

    unsigned char magic[OPENRIDE_GATEWAY_INDEX_MAGIC_SIZE];
    uint32_t version = 0U;
    uint32_t count = 0U;
    char first_id[OPENRIDE_GATEWAY_INDEX_ID_SIZE];
    char second_id[OPENRIDE_GATEWAY_INDEX_ID_SIZE];
    uint64_t first_signature = 0U;
    uint64_t second_signature = 0U;

    bool ok =
        read_bytes(file, magic, sizeof(magic))
        && read_u32(file, &version)
        && read_bytes(file, first_id, sizeof(first_id))
        && read_bytes(file, second_id, sizeof(second_id))
        && read_u64(file, &first_signature)
        && read_u64(file, &second_signature)
        && read_u32(file, &count);

    if (!ok || memcmp(magic, GATEWAY_INDEX_MAGIC, sizeof(magic)) != 0
        || version != OPENRIDE_ROUTING_GATEWAY_INDEX_FORMAT_VERSION
        || count > OPENRIDE_GATEWAY_INDEX_MAX_RECORDS) {
        fclose(file);
        openride_routing_gateway_index_destroy(index);
        set_error(error, error_size, "invalid routing gateway index header");
        return false;
    }

    first_id[sizeof(first_id) - 1U] = '\0';
    second_id[sizeof(second_id) - 1U] = '\0';
    if (first_id[0] == '\0' || second_id[0] == '\0') {
        fclose(file);
        openride_routing_gateway_index_destroy(index);
        set_error(error, error_size, "routing gateway index has invalid region ids");
        return false;
    }

    snprintf(index->first_region_id, sizeof(index->first_region_id), "%s", first_id);
    snprintf(index->second_region_id, sizeof(index->second_region_id), "%s", second_id);
    index->first_graph_signature = first_signature;
    index->second_graph_signature = second_signature;

    if (count > 0U) {
        if (!reserve_records(index, count)) {
            fclose(file);
            openride_routing_gateway_index_destroy(index);
            set_error(error, error_size, "unable to allocate routing gateway index");
            return false;
        }

        for (uint32_t i = 0U; i < count; ++i) {
            OpenRideRoutingGatewayRecord *record = &index->records[i];
            uint32_t lat_bits = 0U;
            uint32_t lon_bits = 0U;
            uint16_t reserved = 0U;
            if (!read_u32(file, &record->first_node)
                || !read_u32(file, &record->second_node)
                || !read_u32(file, &lat_bits)
                || !read_u32(file, &lon_bits)
                || !read_u16(file, &record->road_penalty_m)
                || !read_u16(file, &reserved)) {
                fclose(file);
                openride_routing_gateway_index_destroy(index);
                set_error(error, error_size, "truncated routing gateway index");
                return false;
            }
            record->lat_e7 = (int32_t)lat_bits;
            record->lon_e7 = (int32_t)lon_bits;
            record->reserved = 0U;
        }
    }

    index->count = count;
    fclose(file);
    set_error(error, error_size, "");
    return true;
}

static bool records_match_graphs(
    const OpenRideRoutingGatewayIndex *index,
    const OpenRideRoutingGraph *first_graph,
    const OpenRideRoutingGraph *second_graph,
    bool reversed)
{
    if (!index || !first_graph || !second_graph) return false;

    for (uint32_t i = 0U; i < index->count; ++i) {
        const OpenRideRoutingGatewayRecord *record = &index->records[i];
        const OpenRideRoutingNodeId first_id =
            reversed ? record->second_node : record->first_node;
        const OpenRideRoutingNodeId second_id =
            reversed ? record->first_node : record->second_node;

        if (first_id >= first_graph->node_count
            || second_id >= second_graph->node_count) {
            return false;
        }

        const OpenRideRoutingNode *first_node = &first_graph->nodes[first_id];
        const OpenRideRoutingNode *second_node = &second_graph->nodes[second_id];
        if (first_node->lat_e7 != record->lat_e7
            || first_node->lon_e7 != record->lon_e7
            || second_node->lat_e7 != record->lat_e7
            || second_node->lon_e7 != record->lon_e7) {
            return false;
        }
    }
    return true;
}

bool openride_routing_gateway_index_matches(
    const OpenRideRoutingGatewayIndex *index,
    const char *first_region_id,
    const OpenRideRoutingGraph *first_graph,
    const char *second_region_id,
    const OpenRideRoutingGraph *second_graph,
    bool *reversed)
{
    if (reversed) *reversed = false;
    if (!index || !first_region_id || !second_region_id
        || !first_graph || !second_graph) {
        return false;
    }

    const uint64_t first_signature =
        openride_routing_gateway_graph_signature(first_graph);
    const uint64_t second_signature =
        openride_routing_gateway_graph_signature(second_graph);

    if (strcmp(index->first_region_id, first_region_id) == 0
        && strcmp(index->second_region_id, second_region_id) == 0
        && index->first_graph_signature == first_signature
        && index->second_graph_signature == second_signature
        && records_match_graphs(index, first_graph, second_graph, false)) {
        return true;
    }

    if (strcmp(index->first_region_id, second_region_id) == 0
        && strcmp(index->second_region_id, first_region_id) == 0
        && index->first_graph_signature == second_signature
        && index->second_graph_signature == first_signature
        && records_match_graphs(index, first_graph, second_graph, true)) {
        if (reversed) *reversed = true;
        return true;
    }

    return false;
}

bool openride_routing_gateway_index_pair_path(
    char *output,
    size_t output_size,
    const char *routing_dir,
    const char *first_region_id,
    const char *second_region_id)
{
    if (!output || output_size == 0U || !routing_dir
        || !first_region_id || !second_region_id
        || first_region_id[0] == '\0' || second_region_id[0] == '\0') {
        return false;
    }

    const char *a = first_region_id;
    const char *b = second_region_id;
    if (strcmp(a, b) > 0) {
        a = second_region_id;
        b = first_region_id;
    }

    char filename[192];
    const int written = snprintf(filename, sizeof(filename),
                                 "gateway-%s--%s.orgateway", a, b);
    if (written < 0 || (size_t)written >= sizeof(filename)) return false;

    return openride_platform_path_join(output,
                                       output_size,
                                       routing_dir,
                                       filename);
}
