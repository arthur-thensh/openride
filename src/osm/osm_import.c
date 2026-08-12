#include "openride/osm_import.h"
#include "openride/place_search.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>
#include <zlib.h>

typedef struct ProtoReader {
    const unsigned char *cursor;
    const unsigned char *end;
} ProtoReader;

typedef struct ProtoSlice {
    const unsigned char *data;
    size_t size;
} ProtoSlice;

typedef struct OSMStringTable {
    ProtoSlice *items;
    uint32_t count;
    uint32_t capacity;
} OSMStringTable;

typedef struct U32Vector {
    uint32_t *items;
    uint32_t count;
    uint32_t capacity;
} U32Vector;

typedef struct I64Vector {
    int64_t *items;
    uint32_t count;
    uint32_t capacity;
} I64Vector;

typedef enum OSMWayDirection {
    OSM_WAY_BIDIRECTIONAL = 0,
    OSM_WAY_FORWARD = 1,
    OSM_WAY_REVERSE = -1
} OSMWayDirection;

typedef struct ImportedWay {
    uint64_t ref_offset;
    uint32_t ref_count;
    OpenRideRoutingEdgeAttributes attributes;
    int8_t direction;
} ImportedWay;

typedef struct ImportedWayVector {
    ImportedWay *items;
    uint32_t count;
    uint32_t capacity;
} ImportedWayVector;

typedef struct NeededNode {
    int64_t osm_id;
    int32_t lat_e7;
    int32_t lon_e7;
    OpenRideRoutingNodeId graph_id;
    unsigned char has_coordinates;
} NeededNode;

typedef struct ImportContext {
    ImportedWayVector ways;
    I64Vector way_refs;
    I64Vector needed_ids;
    NeededNode *nodes;
    uint32_t node_count;
    OpenRideOSMImportStats stats;
} ImportContext;

typedef enum ImportPass {
    IMPORT_PASS_WAYS = 1,
    IMPORT_PASS_NODES = 2
} ImportPass;

typedef struct WayScratch {
    U32Vector keys;
    U32Vector vals;
    I64Vector refs;
} WayScratch;

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "unknown error");
}

static void set_errorf(char *error,
                       size_t error_size,
                       const char *prefix,
                       const char *detail)
{
    if (!error || error_size == 0U) return;
    snprintf(error,
             error_size,
             "%s%s%s",
             prefix ? prefix : "",
             (prefix && detail) ? ": " : "",
             detail ? detail : "");
}

static bool proto_read_varint(ProtoReader *reader, uint64_t *value)
{
    if (!reader || !value) return false;

    uint64_t result = 0U;
    unsigned shift = 0U;

    while (reader->cursor < reader->end && shift < 64U) {
        const unsigned char byte = *reader->cursor++;
        result |= ((uint64_t)(byte & 0x7fU)) << shift;
        if ((byte & 0x80U) == 0U) {
            *value = result;
            return true;
        }
        shift += 7U;
    }

    return false;
}

static int64_t proto_zigzag64(uint64_t value)
{
    return (int64_t)((value >> 1U) ^ (uint64_t)(-(int64_t)(value & 1U)));
}

static bool proto_read_key(ProtoReader *reader, uint32_t *field, uint32_t *wire)
{
    uint64_t key = 0U;
    if (!proto_read_varint(reader, &key) || key == 0U) return false;
    *field = (uint32_t)(key >> 3U);
    *wire = (uint32_t)(key & 7U);
    return true;
}

static bool proto_read_slice(ProtoReader *reader, ProtoSlice *slice)
{
    uint64_t length = 0U;
    if (!proto_read_varint(reader, &length)) return false;
    if (length > (uint64_t)(reader->end - reader->cursor)) return false;
    if (length > SIZE_MAX) return false;

    slice->data = reader->cursor;
    slice->size = (size_t)length;
    reader->cursor += slice->size;
    return true;
}

static bool proto_skip(ProtoReader *reader, uint32_t wire)
{
    uint64_t value = 0U;
    ProtoSlice slice = {0};

    switch (wire) {
        case 0U:
            return proto_read_varint(reader, &value);
        case 1U:
            if ((size_t)(reader->end - reader->cursor) < 8U) return false;
            reader->cursor += 8U;
            return true;
        case 2U:
            return proto_read_slice(reader, &slice);
        case 5U:
            if ((size_t)(reader->end - reader->cursor) < 4U) return false;
            reader->cursor += 4U;
            return true;
        default:
            return false;
    }
}

static bool grow_u32(U32Vector *vector, uint32_t needed)
{
    if (needed <= vector->capacity) return true;
    uint32_t capacity = vector->capacity == 0U ? 16U : vector->capacity;
    while (capacity < needed) {
        if (capacity > UINT32_MAX / 2U) {
            capacity = needed;
            break;
        }
        capacity *= 2U;
    }
    uint32_t *items = realloc(vector->items, (size_t)capacity * sizeof(*items));
    if (!items) return false;
    vector->items = items;
    vector->capacity = capacity;
    return true;
}

static bool grow_i64(I64Vector *vector, uint32_t needed)
{
    if (needed <= vector->capacity) return true;
    uint32_t capacity = vector->capacity == 0U ? 64U : vector->capacity;
    while (capacity < needed) {
        if (capacity > UINT32_MAX / 2U) {
            capacity = needed;
            break;
        }
        capacity *= 2U;
    }
    int64_t *items = realloc(vector->items, (size_t)capacity * sizeof(*items));
    if (!items) return false;
    vector->items = items;
    vector->capacity = capacity;
    return true;
}

static bool u32_push(U32Vector *vector, uint32_t value)
{
    if (vector->count == UINT32_MAX) return false;
    if (!grow_u32(vector, vector->count + 1U)) return false;
    vector->items[vector->count++] = value;
    return true;
}

static bool i64_push(I64Vector *vector, int64_t value)
{
    if (vector->count == UINT32_MAX) return false;
    if (!grow_i64(vector, vector->count + 1U)) return false;
    vector->items[vector->count++] = value;
    return true;
}

static bool i64_append(I64Vector *destination, const int64_t *values, uint32_t count)
{
    if (count == 0U) return true;
    if (destination->count > UINT32_MAX - count) return false;
    const uint32_t needed = destination->count + count;
    if (!grow_i64(destination, needed)) return false;
    memcpy(destination->items + destination->count,
           values,
           (size_t)count * sizeof(*values));
    destination->count = needed;
    return true;
}

static void u32_destroy(U32Vector *vector)
{
    if (!vector) return;
    free(vector->items);
    memset(vector, 0, sizeof(*vector));
}

static void i64_destroy(I64Vector *vector)
{
    if (!vector) return;
    free(vector->items);
    memset(vector, 0, sizeof(*vector));
}

static bool imported_way_push(ImportedWayVector *vector, ImportedWay way)
{
    if (vector->count == UINT32_MAX) return false;
    if (vector->count == vector->capacity) {
        uint32_t capacity = vector->capacity == 0U ? 1024U : vector->capacity * 2U;
        if (capacity < vector->capacity) return false;
        ImportedWay *items = realloc(vector->items,
                                     (size_t)capacity * sizeof(*items));
        if (!items) return false;
        vector->items = items;
        vector->capacity = capacity;
    }
    vector->items[vector->count++] = way;
    return true;
}

static bool string_table_push(OSMStringTable *table, ProtoSlice string)
{
    if (table->count == UINT32_MAX) return false;
    if (table->count == table->capacity) {
        uint32_t capacity = table->capacity == 0U ? 64U : table->capacity * 2U;
        if (capacity < table->capacity) return false;
        ProtoSlice *items = realloc(table->items,
                                    (size_t)capacity * sizeof(*items));
        if (!items) return false;
        table->items = items;
        table->capacity = capacity;
    }
    table->items[table->count++] = string;
    return true;
}

static void string_table_destroy(OSMStringTable *table)
{
    if (!table) return;
    free(table->items);
    memset(table, 0, sizeof(*table));
}

static bool parse_string_table(ProtoSlice message,
                               OSMStringTable *table,
                               char *error,
                               size_t error_size)
{
    ProtoReader reader = {message.data, message.data + message.size};
    while (reader.cursor < reader.end) {
        uint32_t field = 0U;
        uint32_t wire = 0U;
        if (!proto_read_key(&reader, &field, &wire)) {
            set_error(error, error_size, "invalid PBF string table key");
            return false;
        }
        if (field == 1U && wire == 2U) {
            ProtoSlice string = {0};
            if (!proto_read_slice(&reader, &string)
                || !string_table_push(table, string)) {
                set_error(error, error_size, "unable to decode PBF string table");
                return false;
            }
        } else if (!proto_skip(&reader, wire)) {
            set_error(error, error_size, "invalid PBF string table field");
            return false;
        }
    }
    return true;
}

static bool slice_equals(ProtoSlice slice, const char *text)
{
    const size_t length = strlen(text);
    return slice.size == length && memcmp(slice.data, text, length) == 0;
}

static ProtoSlice table_string(const OSMStringTable *table, uint32_t index)
{
    static const unsigned char empty = 0U;
    if (!table || index >= table->count) {
        ProtoSlice result = {&empty, 0U};
        return result;
    }
    return table->items[index];
}

static bool parse_packed_u32(ProtoSlice packed, U32Vector *values)
{
    ProtoReader reader = {packed.data, packed.data + packed.size};
    while (reader.cursor < reader.end) {
        uint64_t value = 0U;
        if (!proto_read_varint(&reader, &value) || value > UINT32_MAX) return false;
        if (!u32_push(values, (uint32_t)value)) return false;
    }
    return true;
}

static bool parse_packed_sint64_deltas(ProtoSlice packed, I64Vector *values)
{
    ProtoReader reader = {packed.data, packed.data + packed.size};
    int64_t accumulator = 0;
    while (reader.cursor < reader.end) {
        uint64_t raw = 0U;
        if (!proto_read_varint(&reader, &raw)) return false;
        const int64_t delta = proto_zigzag64(raw);
        if ((delta > 0 && accumulator > INT64_MAX - delta)
            || (delta < 0 && accumulator < INT64_MIN - delta)) {
            return false;
        }
        accumulator += delta;
        if (!i64_push(values, accumulator)) return false;
    }
    return true;
}

static ProtoSlice way_tag_value(const OSMStringTable *table,
                                const U32Vector *keys,
                                const U32Vector *vals,
                                const char *key)
{
    static const unsigned char empty = 0U;
    ProtoSlice none = {&empty, 0U};
    const uint32_t count = keys->count < vals->count ? keys->count : vals->count;

    for (uint32_t i = 0U; i < count; ++i) {
        const ProtoSlice key_slice = table_string(table, keys->items[i]);
        if (slice_equals(key_slice, key)) {
            return table_string(table, vals->items[i]);
        }
    }
    return none;
}

static bool tag_is(ProtoSlice value, const char *text)
{
    return slice_equals(value, text);
}

static bool access_value_denied(ProtoSlice value)
{
    return tag_is(value, "no")
        || tag_is(value, "private")
        || tag_is(value, "agricultural")
        || tag_is(value, "forestry");
}

static bool access_value_allowed(ProtoSlice value)
{
    return tag_is(value, "yes")
        || tag_is(value, "permissive")
        || tag_is(value, "designated")
        || tag_is(value, "destination")
        || tag_is(value, "customers");
}

static bool way_access_denied(const OSMStringTable *table,
                              const U32Vector *keys,
                              const U32Vector *vals)
{
    const char *hierarchy[] = {"motorcycle", "motor_vehicle", "vehicle", "access"};
    for (size_t i = 0U; i < sizeof(hierarchy) / sizeof(hierarchy[0]); ++i) {
        const ProtoSlice value = way_tag_value(table, keys, vals, hierarchy[i]);
        if (value.size == 0U) continue;
        return access_value_denied(value);
    }
    return false;
}

static bool way_has_explicit_motor_access(const OSMStringTable *table,
                                          const U32Vector *keys,
                                          const U32Vector *vals)
{
    ProtoSlice value = way_tag_value(table, keys, vals, "motorcycle");
    if (value.size > 0U) return access_value_allowed(value);
    value = way_tag_value(table, keys, vals, "motor_vehicle");
    return value.size > 0U && access_value_allowed(value);
}

static OpenRideRoadClass road_class_from_highway(ProtoSlice highway)
{
    if (tag_is(highway, "motorway") || tag_is(highway, "motorway_link")) {
        return OPENRIDE_ROAD_MOTORWAY;
    }
    if (tag_is(highway, "trunk") || tag_is(highway, "trunk_link")) {
        return OPENRIDE_ROAD_TRUNK;
    }
    if (tag_is(highway, "primary") || tag_is(highway, "primary_link")) {
        return OPENRIDE_ROAD_PRIMARY;
    }
    if (tag_is(highway, "secondary") || tag_is(highway, "secondary_link")) {
        return OPENRIDE_ROAD_SECONDARY;
    }
    if (tag_is(highway, "tertiary") || tag_is(highway, "tertiary_link")) {
        return OPENRIDE_ROAD_TERTIARY;
    }
    if (tag_is(highway, "unclassified") || tag_is(highway, "road")) {
        return OPENRIDE_ROAD_UNCLASSIFIED;
    }
    if (tag_is(highway, "residential")) return OPENRIDE_ROAD_RESIDENTIAL;
    if (tag_is(highway, "service")) return OPENRIDE_ROAD_SERVICE;
    if (tag_is(highway, "living_street")) return OPENRIDE_ROAD_LIVING_STREET;
    if (tag_is(highway, "track")) return OPENRIDE_ROAD_TRACK;
    if (tag_is(highway, "path")) return OPENRIDE_ROAD_PATH;
    return OPENRIDE_ROAD_UNKNOWN;
}

static OpenRideSurface surface_from_tag(ProtoSlice surface)
{
    if (surface.size == 0U) return OPENRIDE_SURFACE_UNKNOWN;
    if (tag_is(surface, "paved")) return OPENRIDE_SURFACE_PAVED;
    if (tag_is(surface, "asphalt")) return OPENRIDE_SURFACE_ASPHALT;
    if (tag_is(surface, "concrete")
        || tag_is(surface, "concrete:lanes")
        || tag_is(surface, "concrete:plates")) {
        return OPENRIDE_SURFACE_CONCRETE;
    }
    if (tag_is(surface, "paving_stones") || tag_is(surface, "sett")
        || tag_is(surface, "cobblestone")) {
        return OPENRIDE_SURFACE_PAVING_STONES;
    }
    if (tag_is(surface, "compacted")) return OPENRIDE_SURFACE_COMPACTED;
    if (tag_is(surface, "fine_gravel")) return OPENRIDE_SURFACE_FINE_GRAVEL;
    if (tag_is(surface, "gravel") || tag_is(surface, "pebblestone")) {
        return OPENRIDE_SURFACE_GRAVEL;
    }
    if (tag_is(surface, "dirt") || tag_is(surface, "earth")) {
        return OPENRIDE_SURFACE_DIRT;
    }
    if (tag_is(surface, "ground") || tag_is(surface, "grass")
        || tag_is(surface, "grass_paver")) {
        return OPENRIDE_SURFACE_GROUND;
    }
    if (tag_is(surface, "sand")) return OPENRIDE_SURFACE_SAND;
    if (tag_is(surface, "mud")) return OPENRIDE_SURFACE_MUD;
    return OPENRIDE_SURFACE_OTHER;
}

static bool surface_is_unpaved(OpenRideSurface surface)
{
    switch (surface) {
        case OPENRIDE_SURFACE_COMPACTED:
        case OPENRIDE_SURFACE_FINE_GRAVEL:
        case OPENRIDE_SURFACE_GRAVEL:
        case OPENRIDE_SURFACE_DIRT:
        case OPENRIDE_SURFACE_GROUND:
        case OPENRIDE_SURFACE_SAND:
        case OPENRIDE_SURFACE_MUD:
            return true;
        default:
            return false;
    }
}

static uint16_t maxspeed_from_tag(ProtoSlice maxspeed)
{
    if (maxspeed.size == 0U || maxspeed.size >= 64U) return 0U;

    char text[64];
    memcpy(text, maxspeed.data, maxspeed.size);
    text[maxspeed.size] = '\0';

    char *cursor = text;
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) ++cursor;
    if (!isdigit((unsigned char)*cursor)) return 0U;

    errno = 0;
    char *end = NULL;
    const double value = strtod(cursor, &end);
    if (errno != 0 || end == cursor || !isfinite(value) || value <= 0.0) return 0U;

    double kph = value;
    while (*end != '\0' && isspace((unsigned char)*end)) ++end;
    if (strncmp(end, "mph", 3U) == 0) kph *= 1.609344;
    if (kph > 65535.0) kph = 65535.0;
    return (uint16_t)llround(kph);
}

static OSMWayDirection way_direction(const OSMStringTable *table,
                                     const U32Vector *keys,
                                     const U32Vector *vals,
                                     OpenRideRoadClass road_class)
{
    const ProtoSlice oneway = way_tag_value(table, keys, vals, "oneway");
    if (tag_is(oneway, "-1") || tag_is(oneway, "reverse")) {
        return OSM_WAY_REVERSE;
    }
    if (tag_is(oneway, "yes") || tag_is(oneway, "1") || tag_is(oneway, "true")) {
        return OSM_WAY_FORWARD;
    }
    if (tag_is(oneway, "no") || tag_is(oneway, "0") || tag_is(oneway, "false")) {
        return OSM_WAY_BIDIRECTIONAL;
    }

    const ProtoSlice junction = way_tag_value(table, keys, vals, "junction");
    if (tag_is(junction, "roundabout") || tag_is(junction, "circular")) {
        return OSM_WAY_FORWARD;
    }
    if (road_class == OPENRIDE_ROAD_MOTORWAY) return OSM_WAY_FORWARD;
    return OSM_WAY_BIDIRECTIONAL;
}

static bool classify_way(const OSMStringTable *table,
                         const U32Vector *keys,
                         const U32Vector *vals,
                         OpenRideRoutingEdgeAttributes *attributes,
                         OSMWayDirection *direction)
{
    if (way_access_denied(table, keys, vals)) return false;

    const ProtoSlice highway = way_tag_value(table, keys, vals, "highway");
    const ProtoSlice route = way_tag_value(table, keys, vals, "route");
    OpenRideRoadClass road_class = road_class_from_highway(highway);
    uint32_t flags = OPENRIDE_EDGE_FLAG_NONE;

    if (road_class == OPENRIDE_ROAD_UNKNOWN) {
        if (!tag_is(route, "ferry")) return false;
        road_class = OPENRIDE_ROAD_OTHER;
        flags |= OPENRIDE_EDGE_FLAG_FERRY;
    }

    /* Generic paths are not assumed motor-legal. Require an explicit motor tag. */
    if (road_class == OPENRIDE_ROAD_PATH
        && !way_has_explicit_motor_access(table, keys, vals)) {
        return false;
    }

    const ProtoSlice surface_tag = way_tag_value(table, keys, vals, "surface");
    const OpenRideSurface surface = surface_from_tag(surface_tag);
    if (surface_is_unpaved(surface)) flags |= OPENRIDE_EDGE_FLAG_UNPAVED;

    const ProtoSlice toll = way_tag_value(table, keys, vals, "toll");
    if (tag_is(toll, "yes") || tag_is(toll, "1") || tag_is(toll, "true")) {
        flags |= OPENRIDE_EDGE_FLAG_TOLL;
    }

    const ProtoSlice junction = way_tag_value(table, keys, vals, "junction");
    if (tag_is(junction, "roundabout") || tag_is(junction, "circular")) {
        flags |= OPENRIDE_EDGE_FLAG_ROUNDABOUT;
    }

    *attributes = openride_routing_edge_attributes_default();
    attributes->road_class = road_class;
    attributes->surface = surface;
    attributes->flags = flags;
    attributes->max_speed_kph = maxspeed_from_tag(
        way_tag_value(table, keys, vals, "maxspeed"));
    *direction = way_direction(table, keys, vals, road_class);
    return true;
}

static bool parse_way_message(ProtoSlice message,
                              const OSMStringTable *table,
                              WayScratch *scratch,
                              ImportContext *context,
                              char *error,
                              size_t error_size)
{
    scratch->keys.count = 0U;
    scratch->vals.count = 0U;
    scratch->refs.count = 0U;

    ProtoReader reader = {message.data, message.data + message.size};
    while (reader.cursor < reader.end) {
        uint32_t field = 0U;
        uint32_t wire = 0U;
        if (!proto_read_key(&reader, &field, &wire)) {
            set_error(error, error_size, "invalid OSM way protobuf key");
            return false;
        }

        if ((field == 2U || field == 3U) && wire == 2U) {
            ProtoSlice packed = {0};
            if (!proto_read_slice(&reader, &packed)
                || !parse_packed_u32(packed,
                                     field == 2U ? &scratch->keys : &scratch->vals)) {
                set_error(error, error_size, "invalid packed OSM way tags");
                return false;
            }
        } else if ((field == 2U || field == 3U) && wire == 0U) {
            uint64_t value = 0U;
            if (!proto_read_varint(&reader, &value) || value > UINT32_MAX
                || !u32_push(field == 2U ? &scratch->keys : &scratch->vals,
                             (uint32_t)value)) {
                set_error(error, error_size, "invalid OSM way tag index");
                return false;
            }
        } else if (field == 8U && wire == 2U) {
            ProtoSlice packed = {0};
            if (!proto_read_slice(&reader, &packed)
                || !parse_packed_sint64_deltas(packed, &scratch->refs)) {
                set_error(error, error_size, "invalid packed OSM way node refs");
                return false;
            }
        } else if (!proto_skip(&reader, wire)) {
            set_error(error, error_size, "invalid OSM way field");
            return false;
        }
    }

    ++context->stats.osm_way_count;
    if (scratch->refs.count < 2U) return true;

    OpenRideRoutingEdgeAttributes attributes;
    OSMWayDirection direction = OSM_WAY_BIDIRECTIONAL;
    if (!classify_way(table,
                      &scratch->keys,
                      &scratch->vals,
                      &attributes,
                      &direction)) {
        return true;
    }

    if ((uint64_t)context->way_refs.count + scratch->refs.count > UINT32_MAX) {
        set_error(error, error_size, "OSM extract contains too many routable node refs");
        return false;
    }

    ImportedWay way;
    way.ref_offset = context->way_refs.count;
    way.ref_count = scratch->refs.count;
    way.attributes = attributes;
    way.direction = (int8_t)direction;

    if (!i64_append(&context->way_refs, scratch->refs.items, scratch->refs.count)
        || !i64_append(&context->needed_ids, scratch->refs.items, scratch->refs.count)
        || !imported_way_push(&context->ways, way)) {
        set_error(error, error_size, "out of memory while collecting routable OSM ways");
        return false;
    }

    ++context->stats.routable_way_count;
    return true;
}

static int compare_i64(const void *a, const void *b)
{
    const int64_t left = *(const int64_t *)a;
    const int64_t right = *(const int64_t *)b;
    return left < right ? -1 : (left > right ? 1 : 0);
}

static bool prepare_needed_nodes(ImportContext *context,
                                 char *error,
                                 size_t error_size)
{
    if (context->needed_ids.count == 0U) {
        set_error(error, error_size, "OSM extract contains no routable motorcycle ways");
        return false;
    }

    qsort(context->needed_ids.items,
          context->needed_ids.count,
          sizeof(context->needed_ids.items[0]),
          compare_i64);

    uint32_t unique_count = 0U;
    for (uint32_t i = 0U; i < context->needed_ids.count; ++i) {
        if (unique_count == 0U
            || context->needed_ids.items[i]
                != context->needed_ids.items[unique_count - 1U]) {
            context->needed_ids.items[unique_count++] = context->needed_ids.items[i];
        }
    }
    context->needed_ids.count = unique_count;

    context->nodes = calloc(unique_count, sizeof(*context->nodes));
    if (!context->nodes) {
        set_error(error, error_size, "unable to allocate OSM node index");
        return false;
    }
    context->node_count = unique_count;
    context->stats.referenced_node_count = unique_count;

    for (uint32_t i = 0U; i < unique_count; ++i) {
        context->nodes[i].osm_id = context->needed_ids.items[i];
        context->nodes[i].graph_id = OPENRIDE_ROUTING_NODE_NONE;
    }
    return true;
}

static uint32_t find_needed_node(const ImportContext *context, int64_t osm_id)
{
    uint32_t low = 0U;
    uint32_t high = context->node_count;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2U;
        const int64_t candidate = context->nodes[middle].osm_id;
        if (candidate < osm_id) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    if (low < context->node_count && context->nodes[low].osm_id == osm_id) {
        return low;
    }
    return UINT32_MAX;
}

static int32_t pbf_coordinate_e7(int64_t encoded,
                                 int64_t offset,
                                 int32_t granularity)
{
    const long double nanodegrees = (long double)offset
        + (long double)granularity * (long double)encoded;
    long double e7 = nanodegrees / 100.0L;
    if (e7 > INT32_MAX) e7 = INT32_MAX;
    if (e7 < INT32_MIN) e7 = INT32_MIN;
    return (int32_t)llroundl(e7);
}

static void store_node_coordinate(ImportContext *context,
                                  int64_t osm_id,
                                  int64_t encoded_lat,
                                  int64_t encoded_lon,
                                  int64_t lat_offset,
                                  int64_t lon_offset,
                                  int32_t granularity)
{
    const uint32_t index = find_needed_node(context, osm_id);
    if (index == UINT32_MAX) return;

    NeededNode *node = &context->nodes[index];
    if (!node->has_coordinates) ++context->stats.found_node_count;
    node->lat_e7 = pbf_coordinate_e7(encoded_lat, lat_offset, granularity);
    node->lon_e7 = pbf_coordinate_e7(encoded_lon, lon_offset, granularity);
    node->has_coordinates = 1U;
}

static bool parse_dense_nodes(ProtoSlice message,
                              int64_t lat_offset,
                              int64_t lon_offset,
                              int32_t granularity,
                              ImportContext *context,
                              char *error,
                              size_t error_size)
{
    ProtoSlice ids = {0};
    ProtoSlice lats = {0};
    ProtoSlice lons = {0};
    ProtoReader reader = {message.data, message.data + message.size};

    while (reader.cursor < reader.end) {
        uint32_t field = 0U;
        uint32_t wire = 0U;
        if (!proto_read_key(&reader, &field, &wire)) {
            set_error(error, error_size, "invalid DenseNodes key");
            return false;
        }
        if ((field == 1U || field == 8U || field == 9U) && wire == 2U) {
            ProtoSlice packed = {0};
            if (!proto_read_slice(&reader, &packed)) {
                set_error(error, error_size, "invalid DenseNodes packed field");
                return false;
            }
            if (field == 1U) ids = packed;
            else if (field == 8U) lats = packed;
            else lons = packed;
        } else if (!proto_skip(&reader, wire)) {
            set_error(error, error_size, "invalid DenseNodes field");
            return false;
        }
    }

    ProtoReader id_reader = {ids.data, ids.data + ids.size};
    ProtoReader lat_reader = {lats.data, lats.data + lats.size};
    ProtoReader lon_reader = {lons.data, lons.data + lons.size};
    int64_t id = 0;
    int64_t lat = 0;
    int64_t lon = 0;

    while (id_reader.cursor < id_reader.end) {
        uint64_t raw_id = 0U;
        uint64_t raw_lat = 0U;
        uint64_t raw_lon = 0U;
        if (!proto_read_varint(&id_reader, &raw_id)
            || !proto_read_varint(&lat_reader, &raw_lat)
            || !proto_read_varint(&lon_reader, &raw_lon)) {
            set_error(error, error_size, "DenseNodes arrays have inconsistent lengths");
            return false;
        }
        id += proto_zigzag64(raw_id);
        lat += proto_zigzag64(raw_lat);
        lon += proto_zigzag64(raw_lon);
        store_node_coordinate(context,
                              id,
                              lat,
                              lon,
                              lat_offset,
                              lon_offset,
                              granularity);
    }

    if (lat_reader.cursor != lat_reader.end || lon_reader.cursor != lon_reader.end) {
        set_error(error, error_size, "DenseNodes arrays have inconsistent lengths");
        return false;
    }
    return true;
}

static bool parse_node_message(ProtoSlice message,
                               int64_t lat_offset,
                               int64_t lon_offset,
                               int32_t granularity,
                               ImportContext *context,
                               char *error,
                               size_t error_size)
{
    ProtoReader reader = {message.data, message.data + message.size};
    int64_t id = 0;
    int64_t lat = 0;
    int64_t lon = 0;
    bool has_id = false;
    bool has_lat = false;
    bool has_lon = false;

    while (reader.cursor < reader.end) {
        uint32_t field = 0U;
        uint32_t wire = 0U;
        uint64_t raw = 0U;
        if (!proto_read_key(&reader, &field, &wire)) {
            set_error(error, error_size, "invalid OSM node key");
            return false;
        }
        if ((field == 1U || field == 8U || field == 9U) && wire == 0U) {
            if (!proto_read_varint(&reader, &raw)) {
                set_error(error, error_size, "invalid OSM node coordinate");
                return false;
            }
            if (field == 1U) {
                id = proto_zigzag64(raw);
                has_id = true;
            } else if (field == 8U) {
                lat = proto_zigzag64(raw);
                has_lat = true;
            } else {
                lon = proto_zigzag64(raw);
                has_lon = true;
            }
        } else if (!proto_skip(&reader, wire)) {
            set_error(error, error_size, "invalid OSM node field");
            return false;
        }
    }

    if (has_id && has_lat && has_lon) {
        store_node_coordinate(context,
                              id,
                              lat,
                              lon,
                              lat_offset,
                              lon_offset,
                              granularity);
    }
    return true;
}

static bool parse_primitive_group(ProtoSlice message,
                                  ImportPass pass,
                                  const OSMStringTable *table,
                                  int64_t lat_offset,
                                  int64_t lon_offset,
                                  int32_t granularity,
                                  ImportContext *context,
                                  WayScratch *scratch,
                                  char *error,
                                  size_t error_size)
{
    ProtoReader reader = {message.data, message.data + message.size};
    while (reader.cursor < reader.end) {
        uint32_t field = 0U;
        uint32_t wire = 0U;
        if (!proto_read_key(&reader, &field, &wire)) {
            set_error(error, error_size, "invalid PrimitiveGroup key");
            return false;
        }

        if (wire == 2U && (field == 1U || field == 2U || field == 3U)) {
            ProtoSlice child = {0};
            if (!proto_read_slice(&reader, &child)) {
                set_error(error, error_size, "invalid PrimitiveGroup message");
                return false;
            }
            if (pass == IMPORT_PASS_WAYS && field == 3U) {
                if (!parse_way_message(child,
                                       table,
                                       scratch,
                                       context,
                                       error,
                                       error_size)) {
                    return false;
                }
            } else if (pass == IMPORT_PASS_NODES && field == 2U) {
                if (!parse_dense_nodes(child,
                                       lat_offset,
                                       lon_offset,
                                       granularity,
                                       context,
                                       error,
                                       error_size)) {
                    return false;
                }
            } else if (pass == IMPORT_PASS_NODES && field == 1U) {
                if (!parse_node_message(child,
                                        lat_offset,
                                        lon_offset,
                                        granularity,
                                        context,
                                        error,
                                        error_size)) {
                    return false;
                }
            }
        } else if (!proto_skip(&reader, wire)) {
            set_error(error, error_size, "invalid PrimitiveGroup field");
            return false;
        }
    }
    return true;
}

static bool parse_primitive_block(ProtoSlice block,
                                  ImportPass pass,
                                  ImportContext *context,
                                  char *error,
                                  size_t error_size)
{
    ProtoSlice string_table_message = {0};
    int32_t granularity = 100;
    int64_t lat_offset = 0;
    int64_t lon_offset = 0;

    /* First scan: protobuf fields may appear in any order. */
    ProtoReader metadata_reader = {block.data, block.data + block.size};
    while (metadata_reader.cursor < metadata_reader.end) {
        uint32_t field = 0U;
        uint32_t wire = 0U;
        if (!proto_read_key(&metadata_reader, &field, &wire)) {
            set_error(error, error_size, "invalid PrimitiveBlock key");
            return false;
        }

        if (field == 1U && wire == 2U) {
            if (!proto_read_slice(&metadata_reader, &string_table_message)) {
                set_error(error, error_size, "invalid PrimitiveBlock string table");
                return false;
            }
        } else if (field == 17U && wire == 0U) {
            uint64_t value = 0U;
            if (!proto_read_varint(&metadata_reader, &value) || value > INT32_MAX) {
                set_error(error, error_size, "invalid PBF granularity");
                return false;
            }
            granularity = (int32_t)value;
        } else if ((field == 19U || field == 20U) && wire == 0U) {
            uint64_t value = 0U;
            if (!proto_read_varint(&metadata_reader, &value)) {
                set_error(error, error_size, "invalid PBF coordinate offset");
                return false;
            }
            if (field == 19U) lat_offset = (int64_t)value;
            else lon_offset = (int64_t)value;
        } else if (!proto_skip(&metadata_reader, wire)) {
            set_error(error, error_size, "invalid PrimitiveBlock field");
            return false;
        }
    }

    OSMStringTable table = {0};
    if (string_table_message.data
        && !parse_string_table(string_table_message, &table, error, error_size)) {
        string_table_destroy(&table);
        return false;
    }

    WayScratch scratch = {0};
    ProtoReader group_reader = {block.data, block.data + block.size};
    bool ok = true;

    while (group_reader.cursor < group_reader.end) {
        uint32_t field = 0U;
        uint32_t wire = 0U;
        if (!proto_read_key(&group_reader, &field, &wire)) {
            set_error(error, error_size, "invalid PrimitiveBlock group key");
            ok = false;
            break;
        }
        if (field == 2U && wire == 2U) {
            ProtoSlice group = {0};
            if (!proto_read_slice(&group_reader, &group)
                || !parse_primitive_group(group,
                                          pass,
                                          &table,
                                          lat_offset,
                                          lon_offset,
                                          granularity,
                                          context,
                                          &scratch,
                                          error,
                                          error_size)) {
                ok = false;
                break;
            }
        } else if (!proto_skip(&group_reader, wire)) {
            set_error(error, error_size, "invalid PrimitiveBlock group field");
            ok = false;
            break;
        }
    }

    u32_destroy(&scratch.keys);
    u32_destroy(&scratch.vals);
    i64_destroy(&scratch.refs);
    string_table_destroy(&table);
    return ok;
}

static bool read_u32_be(FILE *file, uint32_t *value, bool *at_eof)
{
    unsigned char bytes[4];
    const size_t got = fread(bytes, 1U, sizeof(bytes), file);
    if (got == 0U && feof(file)) {
        *at_eof = true;
        return true;
    }
    if (got != sizeof(bytes)) return false;
    *at_eof = false;
    *value = ((uint32_t)bytes[0] << 24U)
           | ((uint32_t)bytes[1] << 16U)
           | ((uint32_t)bytes[2] << 8U)
           | (uint32_t)bytes[3];
    return true;
}

static bool parse_blob_header(const unsigned char *data,
                              size_t size,
                              char *type,
                              size_t type_size,
                              uint32_t *data_size)
{
    ProtoReader reader = {data, data + size};
    type[0] = '\0';
    *data_size = 0U;

    while (reader.cursor < reader.end) {
        uint32_t field = 0U;
        uint32_t wire = 0U;
        if (!proto_read_key(&reader, &field, &wire)) return false;
        if (field == 1U && wire == 2U) {
            ProtoSlice slice = {0};
            if (!proto_read_slice(&reader, &slice) || slice.size >= type_size) return false;
            memcpy(type, slice.data, slice.size);
            type[slice.size] = '\0';
        } else if (field == 3U && wire == 0U) {
            uint64_t value = 0U;
            if (!proto_read_varint(&reader, &value) || value > UINT32_MAX) return false;
            *data_size = (uint32_t)value;
        } else if (!proto_skip(&reader, wire)) {
            return false;
        }
    }

    return type[0] != '\0' && *data_size > 0U;
}

static bool decode_blob(const unsigned char *data,
                        size_t size,
                        unsigned char **owned_payload,
                        ProtoSlice *payload,
                        char *error,
                        size_t error_size)
{
    ProtoReader reader = {data, data + size};
    ProtoSlice raw = {0};
    ProtoSlice zlib_data = {0};
    uint64_t raw_size = 0U;

    while (reader.cursor < reader.end) {
        uint32_t field = 0U;
        uint32_t wire = 0U;
        if (!proto_read_key(&reader, &field, &wire)) {
            set_error(error, error_size, "invalid PBF Blob key");
            return false;
        }
        if ((field == 1U || field == 3U) && wire == 2U) {
            ProtoSlice slice = {0};
            if (!proto_read_slice(&reader, &slice)) {
                set_error(error, error_size, "invalid PBF Blob payload");
                return false;
            }
            if (field == 1U) raw = slice;
            else zlib_data = slice;
        } else if (field == 2U && wire == 0U) {
            if (!proto_read_varint(&reader, &raw_size)) {
                set_error(error, error_size, "invalid PBF Blob raw size");
                return false;
            }
        } else if ((field == 4U || field == 5U || field == 6U || field == 7U)
                   && wire == 2U) {
            set_error(error,
                      error_size,
                      "PBF compression is unsupported; expected raw or zlib data");
            return false;
        } else if (!proto_skip(&reader, wire)) {
            set_error(error, error_size, "invalid PBF Blob field");
            return false;
        }
    }

    *owned_payload = NULL;
    if (raw.data) {
        *payload = raw;
        return true;
    }
    if (!zlib_data.data || raw_size == 0U || raw_size > SIZE_MAX) {
        set_error(error, error_size, "PBF Blob has no supported payload");
        return false;
    }

    unsigned char *uncompressed = malloc((size_t)raw_size);
    if (!uncompressed) {
        set_error(error, error_size, "unable to allocate PBF decompression buffer");
        return false;
    }

    uLongf destination_size = (uLongf)raw_size;
    const int z_result = uncompress(uncompressed,
                                    &destination_size,
                                    zlib_data.data,
                                    (uLong)zlib_data.size);
    if (z_result != Z_OK || destination_size != (uLongf)raw_size) {
        free(uncompressed);
        set_error(error, error_size, "unable to decompress zlib PBF Blob");
        return false;
    }

    *owned_payload = uncompressed;
    payload->data = uncompressed;
    payload->size = (size_t)destination_size;
    return true;
}

static bool scan_pbf(const char *path,
                     ImportPass pass,
                     ImportContext *context,
                     char *error,
                     size_t error_size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        set_errorf(error, error_size, "unable to open OSM PBF", strerror(errno));
        return false;
    }

    bool ok = true;
    for (;;) {
        uint32_t header_size = 0U;
        bool at_eof = false;
        if (!read_u32_be(file, &header_size, &at_eof)) {
            set_error(error, error_size, "unable to read PBF block header length");
            ok = false;
            break;
        }
        if (at_eof) break;
        if (header_size == 0U || header_size > 65536U) {
            set_error(error, error_size, "invalid PBF block header size");
            ok = false;
            break;
        }

        unsigned char *header = malloc(header_size);
        if (!header || fread(header, 1U, header_size, file) != header_size) {
            free(header);
            set_error(error, error_size, "unable to read PBF block header");
            ok = false;
            break;
        }

        char type[32];
        uint32_t blob_size = 0U;
        const bool header_ok = parse_blob_header(header,
                                                 header_size,
                                                 type,
                                                 sizeof(type),
                                                 &blob_size);
        free(header);
        if (!header_ok || blob_size == 0U || blob_size > 64U * 1024U * 1024U) {
            set_error(error, error_size, "invalid PBF BlobHeader");
            ok = false;
            break;
        }

        unsigned char *blob = malloc(blob_size);
        if (!blob || fread(blob, 1U, blob_size, file) != blob_size) {
            free(blob);
            set_error(error, error_size, "unable to read PBF Blob");
            ok = false;
            break;
        }

        if (strcmp(type, "OSMData") == 0) {
            unsigned char *owned_payload = NULL;
            ProtoSlice payload = {0};
            if (!decode_blob(blob,
                             blob_size,
                             &owned_payload,
                             &payload,
                             error,
                             error_size)
                || !parse_primitive_block(payload,
                                          pass,
                                          context,
                                          error,
                                          error_size)) {
                free(owned_payload);
                free(blob);
                ok = false;
                break;
            }
            free(owned_payload);
        }

        free(blob);
    }

    fclose(file);
    return ok;
}

static double e7_to_degree(int32_t value)
{
    return (double)value / 10000000.0;
}

static bool build_graph(ImportContext *context,
                        OpenRideRoutingGraph *graph,
                        char *error,
                        size_t error_size)
{
    OpenRideRoutingGraphBuilder *builder = openride_routing_graph_builder_create();
    if (!builder) {
        set_error(error, error_size, "unable to allocate routing graph builder");
        return false;
    }

    bool ok = true;
    for (uint32_t i = 0U; i < context->node_count; ++i) {
        NeededNode *node = &context->nodes[i];
        if (!node->has_coordinates) continue;
        node->graph_id = openride_routing_graph_builder_add_node(
            builder,
            e7_to_degree(node->lat_e7),
            e7_to_degree(node->lon_e7));
        if (node->graph_id == OPENRIDE_ROUTING_NODE_NONE) {
            set_error(error, error_size, "unable to add imported OSM node to graph");
            ok = false;
            break;
        }
    }

    if (ok) {
        for (uint32_t w = 0U; w < context->ways.count && ok; ++w) {
            const ImportedWay *way = &context->ways.items[w];
            for (uint32_t i = 1U; i < way->ref_count; ++i) {
                const int64_t osm_a = context->way_refs.items[way->ref_offset + i - 1U];
                const int64_t osm_b = context->way_refs.items[way->ref_offset + i];
                const uint32_t index_a = find_needed_node(context, osm_a);
                const uint32_t index_b = find_needed_node(context, osm_b);
                if (index_a == UINT32_MAX || index_b == UINT32_MAX) continue;

                const OpenRideRoutingNodeId a = context->nodes[index_a].graph_id;
                const OpenRideRoutingNodeId b = context->nodes[index_b].graph_id;
                if (a == OPENRIDE_ROUTING_NODE_NONE
                    || b == OPENRIDE_ROUTING_NODE_NONE
                    || a == b) {
                    continue;
                }

                if (way->direction == OSM_WAY_FORWARD) {
                    ok = openride_routing_graph_builder_add_directed_edge(
                        builder, a, b, &way->attributes);
                } else if (way->direction == OSM_WAY_REVERSE) {
                    ok = openride_routing_graph_builder_add_directed_edge(
                        builder, b, a, &way->attributes);
                } else {
                    ok = openride_routing_graph_builder_add_bidirectional_edge(
                        builder, a, b, &way->attributes);
                }
                if (!ok) {
                    set_error(error, error_size, "unable to add imported OSM edge to graph");
                    break;
                }
            }
        }
    }

    if (ok) {
        ok = openride_routing_graph_builder_build(builder, graph, error, error_size);
    }
    openride_routing_graph_builder_destroy(builder);

    if (ok) {
        context->stats.graph_node_count = graph->node_count;
        context->stats.graph_edge_count = graph->edge_count;
        context->stats.graph_segment_count = graph->segment_index.segment_count;
    }
    return ok;
}

static void import_context_destroy(ImportContext *context)
{
    if (!context) return;
    free(context->ways.items);
    i64_destroy(&context->way_refs);
    i64_destroy(&context->needed_ids);
    free(context->nodes);
    memset(context, 0, sizeof(*context));
}

bool openride_osm_pbf_import_graph(const char *pbf_path,
                                   OpenRideRoutingGraph *graph,
                                   OpenRideOSMImportStats *stats,
                                   char *error,
                                   size_t error_size)
{
    if (!pbf_path || !graph) {
        set_error(error, error_size, "invalid OSM import arguments");
        return false;
    }

    ImportContext context;
    memset(&context, 0, sizeof(context));

    bool ok = scan_pbf(pbf_path,
                       IMPORT_PASS_WAYS,
                       &context,
                       error,
                       error_size);
    if (ok) ok = prepare_needed_nodes(&context, error, error_size);
    if (ok) {
        ok = scan_pbf(pbf_path,
                      IMPORT_PASS_NODES,
                      &context,
                      error,
                      error_size);
    }

    if (ok) {
        context.stats.missing_node_count = context.stats.referenced_node_count
            - context.stats.found_node_count;
        ok = build_graph(&context, graph, error, error_size);
    }

    if (stats) *stats = context.stats;
    import_context_destroy(&context);
    if (ok) set_error(error, error_size, "");
    return ok;
}

bool openride_osm_pbf_import_file(const char *pbf_path,
                                  const char *graph_path,
                                  OpenRideOSMImportStats *stats,
                                  char *error,
                                  size_t error_size)
{
    if (!graph_path) {
        set_error(error, error_size, "routing graph output path is null");
        return false;
    }

    OpenRideRoutingGraph graph = {0};
    OpenRideOSMImportStats local_stats = {0};
    bool ok = openride_osm_pbf_import_graph(pbf_path,
                                            &graph,
                                            &local_stats,
                                            error,
                                            error_size);
    if (ok) {
        ok = openride_routing_graph_save(&graph,
                                         graph_path,
                                         error,
                                         error_size);
    }

    if (stats) *stats = local_stats;
    openride_routing_graph_destroy(&graph);
    return ok;
}

typedef struct PlaceTags {
    ProtoSlice name;
    ProtoSlice name_fr;
    ProtoSlice place;
    ProtoSlice amenity;
    ProtoSlice tourism;
    ProtoSlice shop;
    ProtoSlice population;
} PlaceTags;

typedef struct PlaceImportContext {
    sqlite3 *db;
    sqlite3_stmt *insert_statement;
    OpenRideOSMPlaceImportStats stats;
} PlaceImportContext;

static void place_tags_add(PlaceTags *tags,
                           const OSMStringTable *table,
                           uint32_t key_index,
                           uint32_t value_index)
{
    if (!tags || !table) return;
    const ProtoSlice key = table_string(table, key_index);
    const ProtoSlice value = table_string(table, value_index);
    if (slice_equals(key, "name")) tags->name = value;
    else if (slice_equals(key, "name:fr")) tags->name_fr = value;
    else if (slice_equals(key, "place")) tags->place = value;
    else if (slice_equals(key, "amenity")) tags->amenity = value;
    else if (slice_equals(key, "tourism")) tags->tourism = value;
    else if (slice_equals(key, "shop")) tags->shop = value;
    else if (slice_equals(key, "population")) tags->population = value;
}

static bool slice_copy_text(ProtoSlice slice, char *text, size_t text_size)
{
    if (!text || text_size == 0U || !slice.data || slice.size == 0U) return false;
    size_t length = slice.size;
    if (length >= text_size) length = text_size - 1U;
    memcpy(text, slice.data, length);
    text[length] = '\0';
    return length > 0U;
}

static uint64_t slice_parse_population(ProtoSlice slice)
{
    char buffer[32];
    if (!slice_copy_text(slice, buffer, sizeof(buffer))) return 0U;
    uint64_t value = 0U;
    bool saw_digit = false;
    for (size_t i = 0U; buffer[i] != '\0'; ++i) {
        const unsigned char c = (unsigned char)buffer[i];
        if (isdigit(c)) {
            saw_digit = true;
            if (value <= UINT64_MAX / 10U) value = value * 10U + (uint64_t)(c - '0');
        } else if (c == ' ' || c == ',' || c == '.') {
            continue;
        } else {
            break;
        }
    }
    return saw_digit ? value : 0U;
}

static OpenRidePlaceKind place_kind_from_tags(const PlaceTags *tags, int *rank)
{
    if (rank) *rank = 0;
    if (!tags) return OPENRIDE_PLACE_UNKNOWN;

    OpenRidePlaceKind kind = OPENRIDE_PLACE_UNKNOWN;
    int base_rank = 0;
    if (tag_is(tags->place, "city")) {
        kind = OPENRIDE_PLACE_CITY; base_rank = 1000;
    } else if (tag_is(tags->place, "town")) {
        kind = OPENRIDE_PLACE_TOWN; base_rank = 850;
    } else if (tag_is(tags->place, "village")) {
        kind = OPENRIDE_PLACE_VILLAGE; base_rank = 700;
    } else if (tag_is(tags->place, "hamlet")) {
        kind = OPENRIDE_PLACE_HAMLET; base_rank = 500;
    } else if (tag_is(tags->place, "suburb")) {
        kind = OPENRIDE_PLACE_SUBURB; base_rank = 420;
    } else if (tag_is(tags->place, "quarter")) {
        kind = OPENRIDE_PLACE_QUARTER; base_rank = 380;
    } else if (tag_is(tags->amenity, "fuel")) {
        kind = OPENRIDE_PLACE_FUEL; base_rank = 620;
    } else if (tag_is(tags->tourism, "camp_site")) {
        kind = OPENRIDE_PLACE_CAMP_SITE; base_rank = 560;
    } else if (tag_is(tags->tourism, "viewpoint")) {
        kind = OPENRIDE_PLACE_VIEWPOINT; base_rank = 480;
    } else if (tag_is(tags->shop, "motorcycle")) {
        kind = OPENRIDE_PLACE_MOTORCYCLE_SHOP; base_rank = 540;
    }

    if (kind == OPENRIDE_PLACE_UNKNOWN) return kind;

    const uint64_t population = slice_parse_population(tags->population);
    if (population > 0U && base_rank >= 700) {
        uint64_t bonus = 0U;
        uint64_t n = population;
        while (n >= 10U) {
            n /= 10U;
            bonus += 20U;
        }
        if (bonus > 180U) bonus = 180U;
        base_rank += (int)bonus;
    }
    if (rank) *rank = base_rank;
    return kind;
}

static bool place_import_insert(PlaceImportContext *context,
                                int64_t osm_id,
                                int32_t lat_e7,
                                int32_t lon_e7,
                                const PlaceTags *tags,
                                char *error,
                                size_t error_size)
{
    if (!context || !tags) return false;
    const ProtoSlice display_name = tags->name_fr.size > 0U ? tags->name_fr : tags->name;
    if (display_name.size == 0U) return true;

    int rank = 0;
    const OpenRidePlaceKind kind = place_kind_from_tags(tags, &rank);
    if (kind == OPENRIDE_PLACE_UNKNOWN) return true;

    char name[192];
    char normalized[256];
    if (!slice_copy_text(display_name, name, sizeof(name))
        || !openride_place_normalize(name, normalized, sizeof(normalized))) {
        return true;
    }

    sqlite3_stmt *statement = context->insert_statement;
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    sqlite3_bind_int64(statement, 1, osm_id);
    sqlite3_bind_int(statement, 2, lat_e7);
    sqlite3_bind_int(statement, 3, lon_e7);
    sqlite3_bind_int(statement, 4, (int)kind);
    sqlite3_bind_int(statement, 5, rank);
    sqlite3_bind_text(statement, 6, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 7, normalized, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(statement) != SQLITE_DONE) {
        set_error(error, error_size, sqlite3_errmsg(context->db));
        return false;
    }
    ++context->stats.indexed_place_count;
    return true;
}

static bool parse_dense_nodes_places(ProtoSlice message,
                                     const OSMStringTable *table,
                                     int64_t lat_offset,
                                     int64_t lon_offset,
                                     int32_t granularity,
                                     PlaceImportContext *context,
                                     char *error,
                                     size_t error_size)
{
    ProtoSlice ids = {0};
    ProtoSlice lats = {0};
    ProtoSlice lons = {0};
    ProtoSlice keys_vals_slice = {0};
    ProtoReader reader = {message.data, message.data + message.size};

    while (reader.cursor < reader.end) {
        uint32_t field = 0U;
        uint32_t wire = 0U;
        if (!proto_read_key(&reader, &field, &wire)) {
            set_error(error, error_size, "invalid DenseNodes key");
            return false;
        }
        if ((field == 1U || field == 8U || field == 9U || field == 10U) && wire == 2U) {
            ProtoSlice packed = {0};
            if (!proto_read_slice(&reader, &packed)) {
                set_error(error, error_size, "invalid DenseNodes packed field");
                return false;
            }
            if (field == 1U) ids = packed;
            else if (field == 8U) lats = packed;
            else if (field == 9U) lons = packed;
            else keys_vals_slice = packed;
        } else if (!proto_skip(&reader, wire)) {
            set_error(error, error_size, "invalid DenseNodes field");
            return false;
        }
    }

    U32Vector keys_vals = {0};
    if (keys_vals_slice.data && !parse_packed_u32(keys_vals_slice, &keys_vals)) {
        set_error(error, error_size, "invalid DenseNodes tags");
        u32_destroy(&keys_vals);
        return false;
    }

    ProtoReader id_reader = {ids.data, ids.data + ids.size};
    ProtoReader lat_reader = {lats.data, lats.data + lats.size};
    ProtoReader lon_reader = {lons.data, lons.data + lons.size};
    int64_t id = 0;
    int64_t lat = 0;
    int64_t lon = 0;
    uint32_t tag_index = 0U;
    bool ok = true;

    while (id_reader.cursor < id_reader.end) {
        uint64_t raw_id = 0U;
        uint64_t raw_lat = 0U;
        uint64_t raw_lon = 0U;
        if (!proto_read_varint(&id_reader, &raw_id)
            || !proto_read_varint(&lat_reader, &raw_lat)
            || !proto_read_varint(&lon_reader, &raw_lon)) {
            set_error(error, error_size, "DenseNodes arrays have inconsistent lengths");
            ok = false;
            break;
        }
        id += proto_zigzag64(raw_id);
        lat += proto_zigzag64(raw_lat);
        lon += proto_zigzag64(raw_lon);
        ++context->stats.osm_node_count;

        PlaceTags tags = {0};
        while (tag_index < keys_vals.count && keys_vals.items[tag_index] != 0U) {
            if (tag_index + 1U >= keys_vals.count) {
                set_error(error, error_size, "DenseNodes tag pair is truncated");
                ok = false;
                break;
            }
            place_tags_add(&tags,
                           table,
                           keys_vals.items[tag_index],
                           keys_vals.items[tag_index + 1U]);
            tag_index += 2U;
        }
        if (!ok) break;
        if (tag_index < keys_vals.count && keys_vals.items[tag_index] == 0U) ++tag_index;

        if (!place_import_insert(context,
                                 id,
                                 pbf_coordinate_e7(lat, lat_offset, granularity),
                                 pbf_coordinate_e7(lon, lon_offset, granularity),
                                 &tags,
                                 error,
                                 error_size)) {
            ok = false;
            break;
        }
    }

    if (ok && (lat_reader.cursor != lat_reader.end || lon_reader.cursor != lon_reader.end)) {
        set_error(error, error_size, "DenseNodes arrays have inconsistent lengths");
        ok = false;
    }

    u32_destroy(&keys_vals);
    return ok;
}

static bool parse_node_places(ProtoSlice message,
                              const OSMStringTable *table,
                              int64_t lat_offset,
                              int64_t lon_offset,
                              int32_t granularity,
                              PlaceImportContext *context,
                              char *error,
                              size_t error_size)
{
    ProtoReader reader = {message.data, message.data + message.size};
    int64_t id = 0;
    int64_t lat = 0;
    int64_t lon = 0;
    bool has_id = false;
    bool has_lat = false;
    bool has_lon = false;
    U32Vector keys = {0};
    U32Vector vals = {0};
    bool ok = true;

    while (reader.cursor < reader.end) {
        uint32_t field = 0U;
        uint32_t wire = 0U;
        if (!proto_read_key(&reader, &field, &wire)) { ok = false; break; }
        if ((field == 1U || field == 8U || field == 9U) && wire == 0U) {
            uint64_t raw = 0U;
            if (!proto_read_varint(&reader, &raw)) { ok = false; break; }
            if (field == 1U) { id = proto_zigzag64(raw); has_id = true; }
            else if (field == 8U) { lat = proto_zigzag64(raw); has_lat = true; }
            else { lon = proto_zigzag64(raw); has_lon = true; }
        } else if ((field == 2U || field == 3U) && wire == 2U) {
            ProtoSlice packed = {0};
            if (!proto_read_slice(&reader, &packed)
                || !parse_packed_u32(packed, field == 2U ? &keys : &vals)) {
                ok = false;
                break;
            }
        } else if (!proto_skip(&reader, wire)) {
            ok = false;
            break;
        }
    }

    if (!ok || keys.count != vals.count) {
        set_error(error, error_size, "invalid tagged OSM node");
        u32_destroy(&keys);
        u32_destroy(&vals);
        return false;
    }

    if (has_id && has_lat && has_lon) {
        ++context->stats.osm_node_count;
        PlaceTags tags = {0};
        for (uint32_t i = 0U; i < keys.count; ++i) {
            place_tags_add(&tags, table, keys.items[i], vals.items[i]);
        }
        ok = place_import_insert(context,
                                 id,
                                 pbf_coordinate_e7(lat, lat_offset, granularity),
                                 pbf_coordinate_e7(lon, lon_offset, granularity),
                                 &tags,
                                 error,
                                 error_size);
    }

    u32_destroy(&keys);
    u32_destroy(&vals);
    return ok;
}

static bool parse_primitive_group_places(ProtoSlice message,
                                         const OSMStringTable *table,
                                         int64_t lat_offset,
                                         int64_t lon_offset,
                                         int32_t granularity,
                                         PlaceImportContext *context,
                                         char *error,
                                         size_t error_size)
{
    ProtoReader reader = {message.data, message.data + message.size};
    while (reader.cursor < reader.end) {
        uint32_t field = 0U;
        uint32_t wire = 0U;
        if (!proto_read_key(&reader, &field, &wire)) {
            set_error(error, error_size, "invalid PrimitiveGroup key");
            return false;
        }
        if (wire == 2U && (field == 1U || field == 2U)) {
            ProtoSlice child = {0};
            if (!proto_read_slice(&reader, &child)) {
                set_error(error, error_size, "invalid PrimitiveGroup node");
                return false;
            }
            if (field == 1U) {
                if (!parse_node_places(child,
                                       table,
                                       lat_offset,
                                       lon_offset,
                                       granularity,
                                       context,
                                       error,
                                       error_size)) return false;
            } else {
                if (!parse_dense_nodes_places(child,
                                              table,
                                              lat_offset,
                                              lon_offset,
                                              granularity,
                                              context,
                                              error,
                                              error_size)) return false;
            }
        } else if (!proto_skip(&reader, wire)) {
            set_error(error, error_size, "invalid PrimitiveGroup field");
            return false;
        }
    }
    return true;
}

static bool parse_primitive_block_places(ProtoSlice block,
                                         PlaceImportContext *context,
                                         char *error,
                                         size_t error_size)
{
    ProtoSlice string_table_message = {0};
    int32_t granularity = 100;
    int64_t lat_offset = 0;
    int64_t lon_offset = 0;

    ProtoReader metadata_reader = {block.data, block.data + block.size};
    while (metadata_reader.cursor < metadata_reader.end) {
        uint32_t field = 0U;
        uint32_t wire = 0U;
        if (!proto_read_key(&metadata_reader, &field, &wire)) return false;
        if (field == 1U && wire == 2U) {
            if (!proto_read_slice(&metadata_reader, &string_table_message)) return false;
        } else if (field == 17U && wire == 0U) {
            uint64_t value = 0U;
            if (!proto_read_varint(&metadata_reader, &value) || value > INT32_MAX) return false;
            granularity = (int32_t)value;
        } else if ((field == 19U || field == 20U) && wire == 0U) {
            uint64_t value = 0U;
            if (!proto_read_varint(&metadata_reader, &value)) return false;
            if (field == 19U) lat_offset = (int64_t)value;
            else lon_offset = (int64_t)value;
        } else if (!proto_skip(&metadata_reader, wire)) {
            return false;
        }
    }

    OSMStringTable table = {0};
    if (string_table_message.data
        && !parse_string_table(string_table_message, &table, error, error_size)) {
        string_table_destroy(&table);
        return false;
    }

    ProtoReader group_reader = {block.data, block.data + block.size};
    bool ok = true;
    while (group_reader.cursor < group_reader.end) {
        uint32_t field = 0U;
        uint32_t wire = 0U;
        if (!proto_read_key(&group_reader, &field, &wire)) { ok = false; break; }
        if (field == 2U && wire == 2U) {
            ProtoSlice group = {0};
            if (!proto_read_slice(&group_reader, &group)
                || !parse_primitive_group_places(group,
                                                 &table,
                                                 lat_offset,
                                                 lon_offset,
                                                 granularity,
                                                 context,
                                                 error,
                                                 error_size)) {
                ok = false;
                break;
            }
        } else if (!proto_skip(&group_reader, wire)) {
            ok = false;
            break;
        }
    }

    string_table_destroy(&table);
    if (!ok && (!error || error[0] == '\0')) {
        set_error(error, error_size, "invalid PrimitiveBlock while importing places");
    }
    return ok;
}

static bool scan_pbf_places(const char *path,
                            PlaceImportContext *context,
                            char *error,
                            size_t error_size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        set_errorf(error, error_size, "unable to open OSM PBF", strerror(errno));
        return false;
    }

    bool ok = true;
    for (;;) {
        uint32_t header_size = 0U;
        bool at_eof = false;
        if (!read_u32_be(file, &header_size, &at_eof)) { ok = false; break; }
        if (at_eof) break;
        if (header_size == 0U || header_size > 65536U) { ok = false; break; }

        unsigned char *header = malloc(header_size);
        if (!header || fread(header, 1U, header_size, file) != header_size) {
            free(header); ok = false; break;
        }
        char type[32];
        uint32_t blob_size = 0U;
        const bool header_ok = parse_blob_header(header,
                                                 header_size,
                                                 type,
                                                 sizeof(type),
                                                 &blob_size);
        free(header);
        if (!header_ok || blob_size == 0U || blob_size > 64U * 1024U * 1024U) {
            ok = false; break;
        }

        unsigned char *blob = malloc(blob_size);
        if (!blob || fread(blob, 1U, blob_size, file) != blob_size) {
            free(blob); ok = false; break;
        }
        if (strcmp(type, "OSMData") == 0) {
            unsigned char *owned_payload = NULL;
            ProtoSlice payload = {0};
            if (!decode_blob(blob,
                             blob_size,
                             &owned_payload,
                             &payload,
                             error,
                             error_size)
                || !parse_primitive_block_places(payload,
                                                 context,
                                                 error,
                                                 error_size)) {
                free(owned_payload);
                free(blob);
                ok = false;
                break;
            }
            free(owned_payload);
        }
        free(blob);
    }

    if (!ok && (!error || error[0] == '\0')) {
        set_error(error, error_size, "unable to scan OSM PBF for places");
    }
    fclose(file);
    return ok;
}

bool openride_osm_pbf_import_places(const char *pbf_path,
                                    const char *database_path,
                                    OpenRideOSMPlaceImportStats *stats,
                                    char *error,
                                    size_t error_size)
{
    if (!pbf_path || !database_path) {
        set_error(error, error_size, "invalid place import arguments");
        return false;
    }

    remove(database_path);
    PlaceImportContext context = {0};
    if (sqlite3_open(database_path, &context.db) != SQLITE_OK) {
        set_error(error,
                  error_size,
                  context.db ? sqlite3_errmsg(context.db) : "unable to create place index");
        if (context.db) sqlite3_close(context.db);
        return false;
    }

    const char *schema =
        "PRAGMA journal_mode=OFF;"
        "PRAGMA synchronous=OFF;"
        "PRAGMA temp_store=MEMORY;"
        "CREATE TABLE metadata(name TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "INSERT INTO metadata(name,value) VALUES('schema_version','1');"
        "CREATE TABLE places("
        "osm_id INTEGER PRIMARY KEY,"
        "lat_e7 INTEGER NOT NULL,"
        "lon_e7 INTEGER NOT NULL,"
        "kind INTEGER NOT NULL,"
        "rank INTEGER NOT NULL,"
        "name TEXT NOT NULL,"
        "normalized TEXT NOT NULL);";

    char *sqlite_error = NULL;
    bool ok = sqlite3_exec(context.db, schema, NULL, NULL, &sqlite_error) == SQLITE_OK;
    if (!ok) {
        set_error(error, error_size, sqlite_error ? sqlite_error : sqlite3_errmsg(context.db));
        sqlite3_free(sqlite_error);
    }

    if (ok) {
        static const char *insert_sql =
            "INSERT OR REPLACE INTO places(osm_id,lat_e7,lon_e7,kind,rank,name,normalized) "
            "VALUES(?1,?2,?3,?4,?5,?6,?7)";
        ok = sqlite3_prepare_v2(context.db,
                                insert_sql,
                                -1,
                                &context.insert_statement,
                                NULL) == SQLITE_OK;
        if (!ok) set_error(error, error_size, sqlite3_errmsg(context.db));
    }

    if (ok) ok = sqlite3_exec(context.db, "BEGIN", NULL, NULL, NULL) == SQLITE_OK;
    if (ok) ok = scan_pbf_places(pbf_path, &context, error, error_size);
    if (ok) ok = sqlite3_exec(context.db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK;
    else sqlite3_exec(context.db, "ROLLBACK", NULL, NULL, NULL);

    if (context.insert_statement) sqlite3_finalize(context.insert_statement);

    if (ok) {
        const char *indexes =
            "CREATE INDEX idx_places_normalized ON places(normalized);"
            "CREATE INDEX idx_places_kind_rank ON places(kind,rank DESC);";
        ok = sqlite3_exec(context.db, indexes, NULL, NULL, &sqlite_error) == SQLITE_OK;
        if (!ok) {
            set_error(error, error_size, sqlite_error ? sqlite_error : sqlite3_errmsg(context.db));
            sqlite3_free(sqlite_error);
        }
    }

    if (stats) *stats = context.stats;
    sqlite3_close(context.db);
    if (!ok) {
        remove(database_path);
        return false;
    }
    set_error(error, error_size, "");
    return true;
}

/* ------------------------------------------------------------------------- */
/* Lightweight cartographic feature visitor used by the .ormap builder.      */
/* ------------------------------------------------------------------------- */

typedef struct MapPolygonWay {
    int64_t osm_id;
    uint64_t ref_offset;
    uint32_t ref_count;
    uint8_t kind;
} MapPolygonWay;

typedef struct MapPolygonWayVector {
    MapPolygonWay *items;
    uint32_t count;
    uint32_t capacity;
} MapPolygonWayVector;

typedef struct MapRelationMember {
    int64_t way_id;
} MapRelationMember;

typedef struct MapRelationMemberVector {
    MapRelationMember *items;
    uint32_t count;
    uint32_t capacity;
} MapRelationMemberVector;

typedef struct MapPolygonRelation {
    uint64_t member_offset;
    uint32_t member_count;
    uint8_t kind;
} MapPolygonRelation;

typedef struct MapPolygonRelationVector {
    MapPolygonRelation *items;
    uint32_t count;
    uint32_t capacity;
} MapPolygonRelationVector;

typedef struct MapPolygonNode {
    int64_t osm_id;
    int32_t lat_e7;
    int32_t lon_e7;
    unsigned char has_coordinates;
} MapPolygonNode;

typedef struct MapPolygonContext {
    MapPolygonWayVector ways;
    MapPolygonRelationVector relations;
    MapRelationMemberVector relation_members;
    I64Vector relation_way_ids;
    I64Vector refs;
    I64Vector needed_ids;
    MapPolygonNode *nodes;
    uint32_t node_count;
    OpenRideOSMMapFeatureVisitor visitor;
    void *userdata;
    OpenRideOSMMapFeatureStats stats;
} MapPolygonContext;

typedef enum MapPolygonPass {
    MAP_POLYGON_PASS_RELATIONS = 1,
    MAP_POLYGON_PASS_WAYS = 2,
    MAP_POLYGON_PASS_NODES = 3
} MapPolygonPass;

static bool map_polygon_way_push(MapPolygonWayVector *vector, MapPolygonWay way)
{
    if (!vector || vector->count == UINT32_MAX) return false;
    if (vector->count == vector->capacity) {
        uint32_t capacity = vector->capacity == 0U ? 1024U : vector->capacity * 2U;
        if (capacity < vector->capacity) return false;
        MapPolygonWay *items = realloc(vector->items,
                                       (size_t)capacity * sizeof(*items));
        if (!items) return false;
        vector->items = items;
        vector->capacity = capacity;
    }
    vector->items[vector->count++] = way;
    return true;
}

static bool map_relation_member_push(MapRelationMemberVector *vector,
                                     MapRelationMember member)
{
    if (!vector || vector->count == UINT32_MAX) return false;
    if (vector->count == vector->capacity) {
        uint32_t capacity = vector->capacity == 0U ? 256U : vector->capacity * 2U;
        if (capacity < vector->capacity) return false;
        MapRelationMember *items = realloc(vector->items,
                                           (size_t)capacity * sizeof(*items));
        if (!items) return false;
        vector->items = items;
        vector->capacity = capacity;
    }
    vector->items[vector->count++] = member;
    return true;
}

static bool map_polygon_relation_push(MapPolygonRelationVector *vector,
                                      MapPolygonRelation relation)
{
    if (!vector || vector->count == UINT32_MAX) return false;
    if (vector->count == vector->capacity) {
        uint32_t capacity = vector->capacity == 0U ? 128U : vector->capacity * 2U;
        if (capacity < vector->capacity) return false;
        MapPolygonRelation *items = realloc(vector->items,
                                            (size_t)capacity * sizeof(*items));
        if (!items) return false;
        vector->items = items;
        vector->capacity = capacity;
    }
    vector->items[vector->count++] = relation;
    return true;
}

static void map_polygon_context_destroy(MapPolygonContext *context)
{
    if (!context) return;
    free(context->ways.items);
    free(context->relations.items);
    free(context->relation_members.items);
    i64_destroy(&context->relation_way_ids);
    i64_destroy(&context->refs);
    i64_destroy(&context->needed_ids);
    free(context->nodes);
    memset(context, 0, sizeof(*context));
}

static bool slice_nonempty(ProtoSlice slice)
{
    return slice.data != NULL && slice.size > 0U;
}

static OpenRideOSMMapFeatureKind classify_map_area_way(const OSMStringTable *table,
                                                        const U32Vector *keys,
                                                        const U32Vector *vals)
{
    const ProtoSlice natural = way_tag_value(table, keys, vals, "natural");
    const ProtoSlice water = way_tag_value(table, keys, vals, "water");
    const ProtoSlice landuse = way_tag_value(table, keys, vals, "landuse");
    const ProtoSlice waterway = way_tag_value(table, keys, vals, "waterway");
    const ProtoSlice leisure = way_tag_value(table, keys, vals, "leisure");
    const ProtoSlice building = way_tag_value(table, keys, vals, "building");

    if (tag_is(natural, "water")
        || (slice_nonempty(water) && !tag_is(water, "no"))
        || tag_is(landuse, "reservoir")
        || tag_is(landuse, "basin")
        || tag_is(waterway, "riverbank")) {
        return OPENRIDE_OSM_MAP_FEATURE_WATER_AREA;
    }
    if (tag_is(natural, "wood")
        || tag_is(landuse, "forest")
        || tag_is(leisure, "nature_reserve")) {
        return OPENRIDE_OSM_MAP_FEATURE_FOREST_AREA;
    }
    if (tag_is(landuse, "residential")
        || tag_is(landuse, "commercial")
        || tag_is(landuse, "industrial")
        || tag_is(landuse, "retail")) {
        return OPENRIDE_OSM_MAP_FEATURE_BUILTUP_AREA;
    }
    if (slice_nonempty(building)
        && !tag_is(building, "no")
        && !tag_is(building, "0")) {
        return OPENRIDE_OSM_MAP_FEATURE_BUILTUP_AREA;
    }
    return 0;
}

static OpenRideOSMMapFeatureKind classify_map_waterway_way(const OSMStringTable *table,
                                                            const U32Vector *keys,
                                                            const U32Vector *vals)
{
    const ProtoSlice waterway = way_tag_value(table, keys, vals, "waterway");
    if (tag_is(waterway, "river")) return OPENRIDE_OSM_MAP_FEATURE_WATERWAY_RIVER;
    if (tag_is(waterway, "canal")) return OPENRIDE_OSM_MAP_FEATURE_WATERWAY_CANAL;
    if (tag_is(waterway, "stream")) return OPENRIDE_OSM_MAP_FEATURE_WATERWAY_STREAM;
    if (tag_is(waterway, "drain") || tag_is(waterway, "ditch")) {
        return OPENRIDE_OSM_MAP_FEATURE_WATERWAY_DRAIN;
    }
    return 0;
}

static bool relation_way_id_needed(const MapPolygonContext *context, int64_t osm_id)
{
    if (!context || context->relation_way_ids.count == 0U) return false;
    uint32_t low = 0U;
    uint32_t high = context->relation_way_ids.count;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2U;
        const int64_t candidate = context->relation_way_ids.items[middle];
        if (candidate < osm_id) low = middle + 1U;
        else high = middle;
    }
    return low < context->relation_way_ids.count
        && context->relation_way_ids.items[low] == osm_id;
}

static void prepare_relation_way_ids(MapPolygonContext *context)
{
    if (!context || context->relation_way_ids.count == 0U) return;
    qsort(context->relation_way_ids.items,
          context->relation_way_ids.count,
          sizeof(context->relation_way_ids.items[0]),
          compare_i64);
    uint32_t unique_count = 0U;
    for (uint32_t i = 0U; i < context->relation_way_ids.count; ++i) {
        if (unique_count == 0U
            || context->relation_way_ids.items[i]
                != context->relation_way_ids.items[unique_count - 1U]) {
            context->relation_way_ids.items[unique_count++] =
                context->relation_way_ids.items[i];
        }
    }
    context->relation_way_ids.count = unique_count;
}

static bool parse_map_relation(ProtoSlice message,
                               const OSMStringTable *table,
                               MapPolygonContext *context,
                               char *error,
                               size_t error_size)
{
    U32Vector keys = {0};
    U32Vector vals = {0};
    U32Vector roles = {0};
    U32Vector types = {0};
    I64Vector memids = {0};
    ProtoReader reader = {message.data, message.data + message.size};
    bool ok = true;

    while (reader.cursor < reader.end) {
        uint32_t field = 0U;
        uint32_t wire = 0U;
        if (!proto_read_key(&reader, &field, &wire)) {
            ok = false;
            break;
        }
        if ((field == 2U || field == 3U || field == 8U || field == 10U)
            && wire == 2U) {
            ProtoSlice packed = {0};
            U32Vector *target = field == 2U ? &keys
                : (field == 3U ? &vals : (field == 8U ? &roles : &types));
            if (!proto_read_slice(&reader, &packed)
                || !parse_packed_u32(packed, target)) {
                ok = false;
                break;
            }
        } else if (field == 9U && wire == 2U) {
            ProtoSlice packed = {0};
            if (!proto_read_slice(&reader, &packed)
                || !parse_packed_sint64_deltas(packed, &memids)) {
                ok = false;
                break;
            }
        } else if (!proto_skip(&reader, wire)) {
            ok = false;
            break;
        }
    }

    ++context->stats.osm_relation_count;
    if (!ok || keys.count != vals.count
        || roles.count != memids.count
        || types.count != memids.count) {
        set_error(error, error_size, "invalid OSM multipolygon relation");
        u32_destroy(&keys);
        u32_destroy(&vals);
        u32_destroy(&roles);
        u32_destroy(&types);
        i64_destroy(&memids);
        return false;
    }

    const ProtoSlice type = way_tag_value(table, &keys, &vals, "type");
    const OpenRideOSMMapFeatureKind kind =
        tag_is(type, "multipolygon")
            ? classify_map_area_way(table, &keys, &vals)
            : 0;
    if (kind != 0) {
        const uint64_t member_offset = context->relation_members.count;
        uint32_t outer_count = 0U;
        for (uint32_t i = 0U; i < memids.count; ++i) {
            if (types.items[i] != 1U) continue; /* Relation.MemberType.WAY */
            const ProtoSlice role = table_string(table, roles.items[i]);
            if (tag_is(role, "inner")) {
                ++context->stats.multipolygon_inner_members_ignored;
                continue;
            }
            if (role.size != 0U && !tag_is(role, "outer")) continue;
            if (!map_relation_member_push(&context->relation_members,
                                          (MapRelationMember){memids.items[i]})
                || !i64_push(&context->relation_way_ids, memids.items[i])) {
                set_error(error, error_size, "out of memory collecting multipolygon members");
                ok = false;
                break;
            }
            ++outer_count;
        }
        if (ok && outer_count > 0U) {
            MapPolygonRelation relation = {
                .member_offset = member_offset,
                .member_count = outer_count,
                .kind = (uint8_t)kind
            };
            if (!map_polygon_relation_push(&context->relations, relation)) {
                set_error(error, error_size, "out of memory collecting multipolygon relation");
                ok = false;
            } else {
                ++context->stats.selected_relation_count;
            }
        }
    }

    u32_destroy(&keys);
    u32_destroy(&vals);
    u32_destroy(&roles);
    u32_destroy(&types);
    i64_destroy(&memids);
    return ok;
}

static bool parse_map_polygon_way(ProtoSlice message,
                                  const OSMStringTable *table,
                                  WayScratch *scratch,
                                  MapPolygonContext *context,
                                  char *error,
                                  size_t error_size)
{
    scratch->keys.count = 0U;
    scratch->vals.count = 0U;
    scratch->refs.count = 0U;
    int64_t osm_id = 0;
    bool has_id = false;

    ProtoReader reader = {message.data, message.data + message.size};
    while (reader.cursor < reader.end) {
        uint32_t field = 0U;
        uint32_t wire = 0U;
        if (!proto_read_key(&reader, &field, &wire)) {
            set_error(error, error_size, "invalid map way protobuf key");
            return false;
        }
        if (field == 1U && wire == 0U) {
            uint64_t value = 0U;
            if (!proto_read_varint(&reader, &value) || value > INT64_MAX) {
                set_error(error, error_size, "invalid map way id");
                return false;
            }
            osm_id = (int64_t)value;
            has_id = true;
        } else if ((field == 2U || field == 3U) && wire == 2U) {
            ProtoSlice packed = {0};
            if (!proto_read_slice(&reader, &packed)
                || !parse_packed_u32(packed,
                                     field == 2U ? &scratch->keys : &scratch->vals)) {
                set_error(error, error_size, "invalid map way tags");
                return false;
            }
        } else if ((field == 2U || field == 3U) && wire == 0U) {
            uint64_t value = 0U;
            if (!proto_read_varint(&reader, &value) || value > UINT32_MAX
                || !u32_push(field == 2U ? &scratch->keys : &scratch->vals,
                             (uint32_t)value)) {
                set_error(error, error_size, "invalid map way tag index");
                return false;
            }
        } else if (field == 8U && wire == 2U) {
            ProtoSlice packed = {0};
            if (!proto_read_slice(&reader, &packed)
                || !parse_packed_sint64_deltas(packed, &scratch->refs)) {
                set_error(error, error_size, "invalid map way node refs");
                return false;
            }
        } else if (!proto_skip(&reader, wire)) {
            set_error(error, error_size, "invalid map way field");
            return false;
        }
    }

    ++context->stats.osm_way_count;
    if (!has_id || scratch->refs.count < 2U) return true;

    const bool relation_member = relation_way_id_needed(context, osm_id);
    const bool closed = scratch->refs.count >= 4U
        && scratch->refs.items[0] == scratch->refs.items[scratch->refs.count - 1U];
    OpenRideOSMMapFeatureKind kind = 0;
    if (closed) {
        kind = classify_map_area_way(table, &scratch->keys, &scratch->vals);
    }
    if (kind == 0) {
        kind = classify_map_waterway_way(table, &scratch->keys, &scratch->vals);
    }
    if (kind == 0 && !relation_member) return true;

    const ProtoSlice building = way_tag_value(table, &scratch->keys, &scratch->vals, "building");
    const ProtoSlice landuse = way_tag_value(table, &scratch->keys, &scratch->vals, "landuse");
    const bool individual_building = !relation_member
        && kind == OPENRIDE_OSM_MAP_FEATURE_BUILTUP_AREA
        && closed
        && slice_nonempty(building)
        && !tag_is(building, "no")
        && !tag_is(building, "0")
        && !(tag_is(landuse, "residential")
             || tag_is(landuse, "commercial")
             || tag_is(landuse, "industrial")
             || tag_is(landuse, "retail"));
    const uint32_t stored_ref_count = individual_building ? 1U : scratch->refs.count;
    if ((uint64_t)context->refs.count + stored_ref_count > UINT32_MAX) {
        set_error(error, error_size, "too many OSM map polygon refs");
        return false;
    }

    MapPolygonWay way = {
        .osm_id = osm_id,
        .ref_offset = context->refs.count,
        .ref_count = stored_ref_count,
        .kind = (uint8_t)kind
    };
    if (!i64_append(&context->refs, scratch->refs.items, stored_ref_count)
        || !i64_append(&context->needed_ids, scratch->refs.items, stored_ref_count)
        || !map_polygon_way_push(&context->ways, way)) {
        set_error(error, error_size, "out of memory collecting map polygons");
        return false;
    }
    if (kind != 0) ++context->stats.selected_way_count;
    if (relation_member) ++context->stats.relation_member_way_count;
    return true;
}

static int compare_map_polygon_way(const void *left_ptr, const void *right_ptr)
{
    const MapPolygonWay *left = left_ptr;
    const MapPolygonWay *right = right_ptr;
    return left->osm_id < right->osm_id ? -1 : (left->osm_id > right->osm_id ? 1 : 0);
}

static const MapPolygonWay *find_map_polygon_way(const MapPolygonContext *context,
                                                  int64_t osm_id)
{
    uint32_t low = 0U;
    uint32_t high = context ? context->ways.count : 0U;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2U;
        const int64_t candidate = context->ways.items[middle].osm_id;
        if (candidate < osm_id) low = middle + 1U;
        else high = middle;
    }
    if (context && low < context->ways.count
        && context->ways.items[low].osm_id == osm_id) {
        return &context->ways.items[low];
    }
    return NULL;
}

static bool prepare_map_polygon_nodes(MapPolygonContext *context,
                                      char *error,
                                      size_t error_size)
{
    if (!context) return false;
    if (context->ways.count > 1U) {
        qsort(context->ways.items,
              context->ways.count,
              sizeof(context->ways.items[0]),
              compare_map_polygon_way);
    }
    if (context->needed_ids.count == 0U) return true;
    qsort(context->needed_ids.items,
          context->needed_ids.count,
          sizeof(context->needed_ids.items[0]),
          compare_i64);
    uint32_t unique_count = 0U;
    for (uint32_t i = 0U; i < context->needed_ids.count; ++i) {
        if (unique_count == 0U
            || context->needed_ids.items[i] != context->needed_ids.items[unique_count - 1U]) {
            context->needed_ids.items[unique_count++] = context->needed_ids.items[i];
        }
    }
    context->needed_ids.count = unique_count;
    context->nodes = calloc(unique_count, sizeof(*context->nodes));
    if (!context->nodes && unique_count > 0U) {
        set_error(error, error_size, "unable to allocate map polygon node index");
        return false;
    }
    context->node_count = unique_count;
    context->stats.referenced_node_count = unique_count;
    for (uint32_t i = 0U; i < unique_count; ++i) {
        context->nodes[i].osm_id = context->needed_ids.items[i];
    }
    return true;
}

static uint32_t find_map_polygon_node(const MapPolygonContext *context, int64_t osm_id)
{
    uint32_t low = 0U;
    uint32_t high = context ? context->node_count : 0U;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2U;
        const int64_t candidate = context->nodes[middle].osm_id;
        if (candidate < osm_id) low = middle + 1U;
        else high = middle;
    }
    if (context && low < context->node_count && context->nodes[low].osm_id == osm_id) {
        return low;
    }
    return UINT32_MAX;
}

static void store_map_polygon_node(MapPolygonContext *context,
                                   int64_t osm_id,
                                   int64_t encoded_lat,
                                   int64_t encoded_lon,
                                   int64_t lat_offset,
                                   int64_t lon_offset,
                                   int32_t granularity)
{
    const uint32_t index = find_map_polygon_node(context, osm_id);
    if (index == UINT32_MAX) return;
    MapPolygonNode *node = &context->nodes[index];
    if (!node->has_coordinates) ++context->stats.found_node_count;
    node->lat_e7 = pbf_coordinate_e7(encoded_lat, lat_offset, granularity);
    node->lon_e7 = pbf_coordinate_e7(encoded_lon, lon_offset, granularity);
    node->has_coordinates = 1U;
}

static bool parse_dense_nodes_map(ProtoSlice message,
                                  int64_t lat_offset,
                                  int64_t lon_offset,
                                  int32_t granularity,
                                  MapPolygonContext *context,
                                  char *error,
                                  size_t error_size)
{
    ProtoSlice ids = {0};
    ProtoSlice lats = {0};
    ProtoSlice lons = {0};
    ProtoReader reader = {message.data, message.data + message.size};
    while (reader.cursor < reader.end) {
        uint32_t field = 0U, wire = 0U;
        if (!proto_read_key(&reader, &field, &wire)) {
            set_error(error, error_size, "invalid DenseNodes map key");
            return false;
        }
        if ((field == 1U || field == 8U || field == 9U) && wire == 2U) {
            ProtoSlice packed = {0};
            if (!proto_read_slice(&reader, &packed)) return false;
            if (field == 1U) ids = packed;
            else if (field == 8U) lats = packed;
            else lons = packed;
        } else if (!proto_skip(&reader, wire)) {
            return false;
        }
    }

    ProtoReader id_reader = {ids.data, ids.data + ids.size};
    ProtoReader lat_reader = {lats.data, lats.data + lats.size};
    ProtoReader lon_reader = {lons.data, lons.data + lons.size};
    int64_t id = 0, lat = 0, lon = 0;
    while (id_reader.cursor < id_reader.end
           && lat_reader.cursor < lat_reader.end
           && lon_reader.cursor < lon_reader.end) {
        uint64_t raw_id = 0U, raw_lat = 0U, raw_lon = 0U;
        if (!proto_read_varint(&id_reader, &raw_id)
            || !proto_read_varint(&lat_reader, &raw_lat)
            || !proto_read_varint(&lon_reader, &raw_lon)) {
            set_error(error, error_size, "invalid DenseNodes map arrays");
            return false;
        }
        id += proto_zigzag64(raw_id);
        lat += proto_zigzag64(raw_lat);
        lon += proto_zigzag64(raw_lon);
        store_map_polygon_node(context,
                               id,
                               lat,
                               lon,
                               lat_offset,
                               lon_offset,
                               granularity);
    }
    if (id_reader.cursor != id_reader.end
        || lat_reader.cursor != lat_reader.end
        || lon_reader.cursor != lon_reader.end) {
        set_error(error, error_size, "DenseNodes map arrays have inconsistent lengths");
        return false;
    }
    return true;
}

static bool parse_node_message_map(ProtoSlice message,
                                   int64_t lat_offset,
                                   int64_t lon_offset,
                                   int32_t granularity,
                                   MapPolygonContext *context,
                                   char *error,
                                   size_t error_size)
{
    ProtoReader reader = {message.data, message.data + message.size};
    int64_t id = 0, lat = 0, lon = 0;
    bool has_id = false, has_lat = false, has_lon = false;
    while (reader.cursor < reader.end) {
        uint32_t field = 0U, wire = 0U;
        if (!proto_read_key(&reader, &field, &wire)) return false;
        if ((field == 1U || field == 8U || field == 9U) && wire == 0U) {
            uint64_t raw = 0U;
            if (!proto_read_varint(&reader, &raw)) return false;
            if (field == 1U) { id = proto_zigzag64(raw); has_id = true; }
            else if (field == 8U) { lat = proto_zigzag64(raw); has_lat = true; }
            else { lon = proto_zigzag64(raw); has_lon = true; }
        } else if (!proto_skip(&reader, wire)) {
            return false;
        }
    }
    if (has_id && has_lat && has_lon) {
        store_map_polygon_node(context,
                               id,
                               lat,
                               lon,
                               lat_offset,
                               lon_offset,
                               granularity);
    }
    (void)error;
    (void)error_size;
    return true;
}

static bool parse_primitive_group_map(ProtoSlice message,
                                      MapPolygonPass pass,
                                      const OSMStringTable *table,
                                      int64_t lat_offset,
                                      int64_t lon_offset,
                                      int32_t granularity,
                                      MapPolygonContext *context,
                                      WayScratch *scratch,
                                      char *error,
                                      size_t error_size)
{
    ProtoReader reader = {message.data, message.data + message.size};
    while (reader.cursor < reader.end) {
        uint32_t field = 0U, wire = 0U;
        if (!proto_read_key(&reader, &field, &wire)) return false;
        if (field == 4U && wire == 2U) {
            ProtoSlice relation = {0};
            if (!proto_read_slice(&reader, &relation)) return false;
            if (pass == MAP_POLYGON_PASS_RELATIONS
                && !parse_map_relation(relation,
                                       table,
                                       context,
                                       error,
                                       error_size)) {
                return false;
            }
        } else if (field == 3U && wire == 2U) {
            ProtoSlice child = {0};
            if (!proto_read_slice(&reader, &child)) return false;
            if (pass == MAP_POLYGON_PASS_WAYS
                && !parse_map_polygon_way(child,
                                          table,
                                          scratch,
                                          context,
                                          error,
                                          error_size)) {
                return false;
            }
        } else if (field == 2U && wire == 2U) {
            ProtoSlice dense = {0};
            if (!proto_read_slice(&reader, &dense)) return false;
            if (pass == MAP_POLYGON_PASS_NODES
                && !parse_dense_nodes_map(dense,
                                          lat_offset,
                                          lon_offset,
                                          granularity,
                                          context,
                                          error,
                                          error_size)) {
                return false;
            }
        } else if (field == 1U && wire == 2U) {
            ProtoSlice node = {0};
            if (!proto_read_slice(&reader, &node)) return false;
            if (pass == MAP_POLYGON_PASS_NODES
                && !parse_node_message_map(node,
                                           lat_offset,
                                           lon_offset,
                                           granularity,
                                           context,
                                           error,
                                           error_size)) {
                return false;
            }
        } else if (!proto_skip(&reader, wire)) {
            return false;
        }
    }
    return true;
}

static bool parse_primitive_block_map(ProtoSlice block,
                                      MapPolygonPass pass,
                                      MapPolygonContext *context,
                                      char *error,
                                      size_t error_size)
{
    ProtoSlice string_table_message = {0};
    int64_t lat_offset = 0, lon_offset = 0;
    int32_t granularity = 100;
    ProtoReader metadata_reader = {block.data, block.data + block.size};
    while (metadata_reader.cursor < metadata_reader.end) {
        uint32_t field = 0U, wire = 0U;
        if (!proto_read_key(&metadata_reader, &field, &wire)) return false;
        if (field == 1U && wire == 2U) {
            if (!proto_read_slice(&metadata_reader, &string_table_message)) return false;
        } else if (field == 17U && wire == 0U) {
            uint64_t value = 0U;
            if (!proto_read_varint(&metadata_reader, &value) || value > INT32_MAX) return false;
            granularity = (int32_t)value;
        } else if ((field == 19U || field == 20U) && wire == 0U) {
            uint64_t value = 0U;
            if (!proto_read_varint(&metadata_reader, &value)) return false;
            if (field == 19U) lat_offset = (int64_t)value;
            else lon_offset = (int64_t)value;
        } else if (!proto_skip(&metadata_reader, wire)) {
            return false;
        }
    }

    OSMStringTable table = {0};
    if (string_table_message.data
        && !parse_string_table(string_table_message, &table, error, error_size)) {
        string_table_destroy(&table);
        return false;
    }
    WayScratch scratch = {0};
    bool ok = true;
    ProtoReader group_reader = {block.data, block.data + block.size};
    while (group_reader.cursor < group_reader.end) {
        uint32_t field = 0U, wire = 0U;
        if (!proto_read_key(&group_reader, &field, &wire)) { ok = false; break; }
        if (field == 2U && wire == 2U) {
            ProtoSlice group = {0};
            if (!proto_read_slice(&group_reader, &group)
                || !parse_primitive_group_map(group,
                                              pass,
                                              &table,
                                              lat_offset,
                                              lon_offset,
                                              granularity,
                                              context,
                                              &scratch,
                                              error,
                                              error_size)) {
                ok = false;
                break;
            }
        } else if (!proto_skip(&group_reader, wire)) {
            ok = false;
            break;
        }
    }
    u32_destroy(&scratch.keys);
    u32_destroy(&scratch.vals);
    i64_destroy(&scratch.refs);
    string_table_destroy(&table);
    if (!ok && (!error || error[0] == '\0')) {
        set_error(error, error_size, "invalid map PrimitiveBlock");
    }
    return ok;
}

static bool scan_pbf_map(const char *path,
                         MapPolygonPass pass,
                         MapPolygonContext *context,
                         char *error,
                         size_t error_size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        set_errorf(error, error_size, "unable to open OSM PBF", strerror(errno));
        return false;
    }
    bool ok = true;
    for (;;) {
        uint32_t header_size = 0U;
        bool at_eof = false;
        if (!read_u32_be(file, &header_size, &at_eof)) {
            set_error(error, error_size, "unable to read map PBF header length");
            ok = false;
            break;
        }
        if (at_eof) break;
        if (header_size == 0U || header_size > 65536U) { ok = false; break; }
        unsigned char *header = malloc(header_size);
        if (!header || fread(header, 1U, header_size, file) != header_size) {
            free(header); ok = false; break;
        }
        char type[32];
        uint32_t blob_size = 0U;
        const bool header_ok = parse_blob_header(header,
                                                 header_size,
                                                 type,
                                                 sizeof(type),
                                                 &blob_size);
        free(header);
        if (!header_ok || blob_size == 0U || blob_size > 64U * 1024U * 1024U) {
            ok = false; break;
        }
        unsigned char *blob = malloc(blob_size);
        if (!blob || fread(blob, 1U, blob_size, file) != blob_size) {
            free(blob); ok = false; break;
        }
        if (strcmp(type, "OSMData") == 0) {
            unsigned char *owned = NULL;
            ProtoSlice payload = {0};
            if (!decode_blob(blob, blob_size, &owned, &payload, error, error_size)
                || !parse_primitive_block_map(payload,
                                              pass,
                                              context,
                                              error,
                                              error_size)) {
                free(owned); free(blob); ok = false; break;
            }
            free(owned);
        }
        free(blob);
    }
    fclose(file);
    if (!ok && (!error || error[0] == '\0')) {
        set_error(error, error_size, "unable to scan OSM map polygons");
    }
    return ok;
}

static bool map_feature_coordinates(const MapPolygonContext *context,
                                    const int64_t *refs,
                                    uint32_t ref_count,
                                    double **latitudes,
                                    double **longitudes,
                                    uint32_t *capacity,
                                    uint32_t *point_count,
                                    char *error,
                                    size_t error_size)
{
    *point_count = 0U;
    if (ref_count > *capacity) {
        double *new_latitudes = realloc(*latitudes,
                                        (size_t)ref_count * sizeof(**latitudes));
        if (!new_latitudes) {
            set_error(error, error_size, "unable to allocate map polygon coordinates");
            return false;
        }
        *latitudes = new_latitudes;
        double *new_longitudes = realloc(*longitudes,
                                         (size_t)ref_count * sizeof(**longitudes));
        if (!new_longitudes) {
            set_error(error, error_size, "unable to allocate map polygon coordinates");
            return false;
        }
        *longitudes = new_longitudes;
        *capacity = ref_count;
    }

    for (uint32_t i = 0U; i < ref_count; ++i) {
        const uint32_t index = find_map_polygon_node(context, refs[i]);
        if (index == UINT32_MAX || !context->nodes[index].has_coordinates) {
            *point_count = 0U;
            return true;
        }
        (*latitudes)[i] = e7_to_degree(context->nodes[index].lat_e7);
        (*longitudes)[i] = e7_to_degree(context->nodes[index].lon_e7);
    }
    *point_count = ref_count;
    return true;
}

static bool emit_standalone_map_ways(MapPolygonContext *context,
                                     double **latitudes,
                                     double **longitudes,
                                     uint32_t *point_capacity,
                                     char *error,
                                     size_t error_size)
{
    for (uint32_t w = 0U; w < context->ways.count; ++w) {
        const MapPolygonWay *way = &context->ways.items[w];
        const OpenRideOSMMapFeatureKind feature_kind =
            (OpenRideOSMMapFeatureKind)way->kind;
        if (feature_kind == 0) continue;
        uint32_t point_count = 0U;
        if (!map_feature_coordinates(context,
                                     context->refs.items + way->ref_offset,
                                     way->ref_count,
                                     latitudes,
                                     longitudes,
                                     point_capacity,
                                     &point_count,
                                     error,
                                     error_size)) {
            return false;
        }
        const bool linear_waterway =
            feature_kind >= OPENRIDE_OSM_MAP_FEATURE_WATERWAY_RIVER;
        if (!((linear_waterway && point_count >= 2U)
              || (!linear_waterway && point_count >= 4U)
              || (feature_kind == OPENRIDE_OSM_MAP_FEATURE_BUILTUP_AREA
                  && point_count == 1U))) {
            continue;
        }
        if (!context->visitor(feature_kind,
                              *latitudes,
                              *longitudes,
                              point_count,
                              context->userdata)) {
            set_error(error, error_size, "map feature visitor aborted import");
            return false;
        }
        ++context->stats.emitted_feature_count;
    }
    return true;
}

static bool ring_refs_append_way(I64Vector *ring,
                                 const MapPolygonContext *context,
                                 const MapPolygonWay *way,
                                 bool reverse)
{
    if (!ring || !context || !way || way->ref_count < 2U) return false;
    if (!reverse) {
        const uint32_t start = ring->count == 0U ? 0U : 1U;
        return i64_append(ring,
                          context->refs.items + way->ref_offset + start,
                          way->ref_count - start);
    }
    const uint32_t start = ring->count == 0U ? 0U : 1U;
    for (uint32_t i = start; i < way->ref_count; ++i) {
        const uint32_t source = way->ref_count - 1U - i;
        if (!i64_push(ring, context->refs.items[way->ref_offset + source])) return false;
    }
    return true;
}

static bool emit_multipolygon_relations(MapPolygonContext *context,
                                        double **latitudes,
                                        double **longitudes,
                                        uint32_t *point_capacity,
                                        char *error,
                                        size_t error_size)
{
    for (uint32_t r = 0U; r < context->relations.count; ++r) {
        const MapPolygonRelation *relation = &context->relations.items[r];
        unsigned char *used = calloc(relation->member_count, 1U);
        if (!used && relation->member_count > 0U) {
            set_error(error, error_size, "out of memory assembling multipolygon");
            return false;
        }

        bool relation_incomplete = false;
        for (uint32_t start = 0U; start < relation->member_count; ++start) {
            if (used[start]) continue;
            const MapRelationMember *member =
                &context->relation_members.items[relation->member_offset + start];
            const MapPolygonWay *way = find_map_polygon_way(context, member->way_id);
            if (!way || way->ref_count < 2U) {
                used[start] = 1U;
                relation_incomplete = true;
                continue;
            }

            I64Vector ring = {0};
            if (!ring_refs_append_way(&ring, context, way, false)) {
                i64_destroy(&ring);
                free(used);
                set_error(error, error_size, "out of memory assembling multipolygon ring");
                return false;
            }
            used[start] = 1U;

            uint64_t guard = (uint64_t)relation->member_count + 1U;
            while (ring.count >= 2U
                   && ring.items[0] != ring.items[ring.count - 1U]
                   && guard-- > 0U) {
                const int64_t endpoint = ring.items[ring.count - 1U];
                bool matched = false;
                for (uint32_t i = 0U; i < relation->member_count; ++i) {
                    if (used[i]) continue;
                    const MapRelationMember *next_member =
                        &context->relation_members.items[relation->member_offset + i];
                    const MapPolygonWay *next =
                        find_map_polygon_way(context, next_member->way_id);
                    if (!next || next->ref_count < 2U) continue;
                    const int64_t first = context->refs.items[next->ref_offset];
                    const int64_t last =
                        context->refs.items[next->ref_offset + next->ref_count - 1U];
                    if (first == endpoint || last == endpoint) {
                        if (!ring_refs_append_way(&ring,
                                                  context,
                                                  next,
                                                  last == endpoint)) {
                            i64_destroy(&ring);
                            free(used);
                            set_error(error,
                                      error_size,
                                      "out of memory assembling multipolygon ring");
                            return false;
                        }
                        used[i] = 1U;
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    relation_incomplete = true;
                    break;
                }
            }

            if (ring.count >= 4U && ring.items[0] == ring.items[ring.count - 1U]) {
                uint32_t point_count = 0U;
                if (!map_feature_coordinates(context,
                                             ring.items,
                                             ring.count,
                                             latitudes,
                                             longitudes,
                                             point_capacity,
                                             &point_count,
                                             error,
                                             error_size)) {
                    i64_destroy(&ring);
                    free(used);
                    return false;
                }
                if (point_count >= 4U) {
                    if (!context->visitor((OpenRideOSMMapFeatureKind)relation->kind,
                                          *latitudes,
                                          *longitudes,
                                          point_count,
                                          context->userdata)) {
                        i64_destroy(&ring);
                        free(used);
                        set_error(error,
                                  error_size,
                                  "map feature visitor aborted multipolygon import");
                        return false;
                    }
                    ++context->stats.multipolygon_outer_ring_count;
                    ++context->stats.emitted_feature_count;
                } else {
                    relation_incomplete = true;
                }
            } else {
                relation_incomplete = true;
            }
            i64_destroy(&ring);
        }

        if (relation_incomplete) ++context->stats.incomplete_multipolygon_count;
        free(used);
    }
    return true;
}

bool openride_osm_pbf_visit_map_features(
    const char *pbf_path,
    OpenRideOSMMapFeatureVisitor visitor,
    void *userdata,
    OpenRideOSMMapFeatureStats *stats,
    char *error,
    size_t error_size)
{
    if (!pbf_path || !visitor) {
        set_error(error, error_size, "invalid map feature import arguments");
        return false;
    }

    MapPolygonContext context;
    memset(&context, 0, sizeof(context));
    context.visitor = visitor;
    context.userdata = userdata;

    /*
     * Three streaming passes keep mobile peak memory bounded:
     *  1. collect only relevant multipolygon relation member way IDs;
     *  2. collect standalone cartographic ways plus those relation members;
     *  3. resolve only the referenced nodes.
     * This avoids retaining millions of unrelated OSM ways on the phone.
     */
    bool ok = scan_pbf_map(pbf_path,
                           MAP_POLYGON_PASS_RELATIONS,
                           &context,
                           error,
                           error_size);
    if (ok) prepare_relation_way_ids(&context);
    if (ok) {
        ok = scan_pbf_map(pbf_path,
                          MAP_POLYGON_PASS_WAYS,
                          &context,
                          error,
                          error_size);
    }
    if (ok) ok = prepare_map_polygon_nodes(&context, error, error_size);
    if (ok && context.node_count > 0U) {
        ok = scan_pbf_map(pbf_path,
                          MAP_POLYGON_PASS_NODES,
                          &context,
                          error,
                          error_size);
    }

    double *latitudes = NULL;
    double *longitudes = NULL;
    uint32_t point_capacity = 0U;
    if (ok) {
        ok = emit_standalone_map_ways(&context,
                                      &latitudes,
                                      &longitudes,
                                      &point_capacity,
                                      error,
                                      error_size);
    }
    if (ok) {
        ok = emit_multipolygon_relations(&context,
                                         &latitudes,
                                         &longitudes,
                                         &point_capacity,
                                         error,
                                         error_size);
    }

    free(latitudes);
    free(longitudes);
    if (stats) *stats = context.stats;
    map_polygon_context_destroy(&context);
    if (ok) set_error(error, error_size, "");
    return ok;
}
