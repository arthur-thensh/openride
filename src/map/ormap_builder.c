#include "openride/ormap.h"
#include "openride/osm_import.h"

#include <sqlite3.h>
#include <zlib.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ORMAP_PI 3.14159265358979323846
#define ORMAP_ROAD_RECORD_SIZE 12U
#define ORMAP_WATER_RECORD_SIZE 10U
#define ORMAP_AREA_RECORD_SIZE 14U
#define ORMAP_MASK_LAYER_BYTES \
    (((OPENRIDE_ORMAP_MASK_GRID * OPENRIDE_ORMAP_MASK_GRID) + 7U) / 8U)

typedef struct RoadTileBucket {
    uint64_t key;
    OpenRideORMapRoadRecord *records;
    uint32_t count;
    uint32_t capacity;
    unsigned char used;
} RoadTileBucket;

typedef struct RoadTileMap {
    RoadTileBucket *buckets;
    uint32_t capacity;
    uint32_t count;
} RoadTileMap;

typedef struct MaskTileBucket {
    uint64_t key;
    unsigned char layers[ORMAP_MASK_LAYER_BYTES * 3U];
    unsigned char used;
} MaskTileBucket;

typedef struct MaskTileMap {
    MaskTileBucket *buckets;
    uint32_t capacity;
    uint32_t count;
} MaskTileMap;

typedef struct WaterTileBucket {
    uint64_t key;
    OpenRideORMapWaterRecord *records;
    uint32_t count;
    uint32_t capacity;
    unsigned char used;
} WaterTileBucket;

typedef struct WaterTileMap {
    WaterTileBucket *buckets;
    uint32_t capacity;
    uint32_t count;
} WaterTileMap;

typedef struct AreaTileBucket {
    uint64_t key;
    OpenRideORMapAreaTriangle *triangles;
    uint32_t count;
    uint32_t capacity;
    unsigned char used;
} AreaTileBucket;

typedef struct AreaTileMap {
    AreaTileBucket *buckets;
    uint32_t capacity;
    uint32_t count;
} AreaTileMap;

typedef struct ORMapPoint {
    double x;
    double y;
} ORMapPoint;

typedef struct BoundaryEdge {
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
    unsigned char used;
} BoundaryEdge;

typedef struct BoundaryEdgeVector {
    BoundaryEdge *items;
    uint32_t count;
    uint32_t capacity;
} BoundaryEdgeVector;

typedef struct MapFeatureContext {
    MaskTileMap masks;
    WaterTileMap waterways;
    AreaTileMap coarse_areas;
    AreaTileMap detail_areas;
    OpenRideORMapBuildStats *stats;
} MapFeatureContext;

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "unknown error");
}

static void write_u16_le(unsigned char *p, uint16_t value)
{
    p[0] = (unsigned char)(value & 0xffU);
    p[1] = (unsigned char)((value >> 8U) & 0xffU);
}

static void write_u32_le(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)(value & 0xffU);
    p[1] = (unsigned char)((value >> 8U) & 0xffU);
    p[2] = (unsigned char)((value >> 16U) & 0xffU);
    p[3] = (unsigned char)((value >> 24U) & 0xffU);
}

static uint64_t tile_key(int zoom, int x, int y)
{
    return ((uint64_t)(uint32_t)zoom << 58U)
        ^ ((uint64_t)(uint32_t)x << 29U)
        ^ (uint64_t)(uint32_t)y;
}

static uint32_t hash64(uint64_t value)
{
    value ^= value >> 33U;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33U;
    value *= UINT64_C(0xc4ceb9fe1a85ec53);
    value ^= value >> 33U;
    return (uint32_t)(value ^ (value >> 32U));
}

static bool road_map_rehash(RoadTileMap *map, uint32_t new_capacity)
{
    RoadTileBucket *new_buckets = calloc(new_capacity, sizeof(*new_buckets));
    if (!new_buckets) return false;
    for (uint32_t i = 0U; i < map->capacity; ++i) {
        RoadTileBucket old = map->buckets[i];
        if (!old.used) continue;
        uint32_t slot = hash64(old.key) & (new_capacity - 1U);
        while (new_buckets[slot].used) slot = (slot + 1U) & (new_capacity - 1U);
        new_buckets[slot] = old;
    }
    free(map->buckets);
    map->buckets = new_buckets;
    map->capacity = new_capacity;
    return true;
}

static RoadTileBucket *road_map_get(RoadTileMap *map,
                                    int zoom,
                                    int x,
                                    int y,
                                    bool create)
{
    if (map->capacity == 0U) {
        if (!create || !road_map_rehash(map, 1024U)) return NULL;
    }
    if (create && (map->count + 1U) * 10U >= map->capacity * 7U) {
        if (map->capacity > UINT32_MAX / 2U
            || !road_map_rehash(map, map->capacity * 2U)) return NULL;
    }
    const uint64_t key = tile_key(zoom, x, y);
    uint32_t slot = hash64(key) & (map->capacity - 1U);
    while (map->buckets[slot].used) {
        if (map->buckets[slot].key == key) return &map->buckets[slot];
        slot = (slot + 1U) & (map->capacity - 1U);
    }
    if (!create) return NULL;
    map->buckets[slot].used = 1U;
    map->buckets[slot].key = key;
    ++map->count;
    return &map->buckets[slot];
}

static bool road_bucket_push(RoadTileBucket *bucket, OpenRideORMapRoadRecord record)
{
    if (!bucket) return false;
    if (bucket->count == bucket->capacity) {
        uint32_t capacity = bucket->capacity == 0U ? 64U : bucket->capacity * 2U;
        if (capacity < bucket->capacity) return false;
        OpenRideORMapRoadRecord *records = realloc(bucket->records,
                                                   (size_t)capacity * sizeof(*records));
        if (!records) return false;
        bucket->records = records;
        bucket->capacity = capacity;
    }
    bucket->records[bucket->count++] = record;
    return true;
}

static void road_map_destroy(RoadTileMap *map)
{
    if (!map) return;
    for (uint32_t i = 0U; i < map->capacity; ++i) {
        free(map->buckets[i].records);
    }
    free(map->buckets);
    memset(map, 0, sizeof(*map));
}

static bool water_map_rehash(WaterTileMap *map, uint32_t new_capacity)
{
    WaterTileBucket *new_buckets = calloc(new_capacity, sizeof(*new_buckets));
    if (!new_buckets) return false;
    for (uint32_t i = 0U; i < map->capacity; ++i) {
        WaterTileBucket old = map->buckets[i];
        if (!old.used) continue;
        uint32_t slot = hash64(old.key) & (new_capacity - 1U);
        while (new_buckets[slot].used) slot = (slot + 1U) & (new_capacity - 1U);
        new_buckets[slot] = old;
    }
    free(map->buckets);
    map->buckets = new_buckets;
    map->capacity = new_capacity;
    return true;
}

static WaterTileBucket *water_map_get(WaterTileMap *map,
                                      int zoom,
                                      int x,
                                      int y,
                                      bool create)
{
    if (map->capacity == 0U) {
        if (!create || !water_map_rehash(map, 1024U)) return NULL;
    }
    if (create && (map->count + 1U) * 10U >= map->capacity * 7U) {
        if (map->capacity > UINT32_MAX / 2U
            || !water_map_rehash(map, map->capacity * 2U)) return NULL;
    }
    const uint64_t key = tile_key(zoom, x, y);
    uint32_t slot = hash64(key) & (map->capacity - 1U);
    while (map->buckets[slot].used) {
        if (map->buckets[slot].key == key) return &map->buckets[slot];
        slot = (slot + 1U) & (map->capacity - 1U);
    }
    if (!create) return NULL;
    map->buckets[slot].used = 1U;
    map->buckets[slot].key = key;
    ++map->count;
    return &map->buckets[slot];
}

static bool water_bucket_push(WaterTileBucket *bucket, OpenRideORMapWaterRecord record)
{
    if (!bucket) return false;
    if (bucket->count == bucket->capacity) {
        uint32_t capacity = bucket->capacity == 0U ? 32U : bucket->capacity * 2U;
        if (capacity < bucket->capacity) return false;
        OpenRideORMapWaterRecord *records = realloc(bucket->records,
                                                     (size_t)capacity * sizeof(*records));
        if (!records) return false;
        bucket->records = records;
        bucket->capacity = capacity;
    }
    bucket->records[bucket->count++] = record;
    return true;
}

static void water_map_destroy(WaterTileMap *map)
{
    if (!map) return;
    for (uint32_t i = 0U; i < map->capacity; ++i) free(map->buckets[i].records);
    free(map->buckets);
    memset(map, 0, sizeof(*map));
}

static bool area_map_rehash(AreaTileMap *map, uint32_t new_capacity)
{
    AreaTileBucket *new_buckets = calloc(new_capacity, sizeof(*new_buckets));
    if (!new_buckets) return false;
    for (uint32_t i = 0U; i < map->capacity; ++i) {
        AreaTileBucket old = map->buckets[i];
        if (!old.used) continue;
        uint32_t slot = hash64(old.key) & (new_capacity - 1U);
        while (new_buckets[slot].used) slot = (slot + 1U) & (new_capacity - 1U);
        new_buckets[slot] = old;
    }
    free(map->buckets);
    map->buckets = new_buckets;
    map->capacity = new_capacity;
    return true;
}

static AreaTileBucket *area_map_get(AreaTileMap *map,
                                    int zoom,
                                    int x,
                                    int y,
                                    bool create)
{
    if (map->capacity == 0U) {
        if (!create || !area_map_rehash(map, 1024U)) return NULL;
    }
    if (create && (map->count + 1U) * 10U >= map->capacity * 7U) {
        if (map->capacity > UINT32_MAX / 2U
            || !area_map_rehash(map, map->capacity * 2U)) return NULL;
    }
    const uint64_t key = tile_key(zoom, x, y);
    uint32_t slot = hash64(key) & (map->capacity - 1U);
    while (map->buckets[slot].used) {
        if (map->buckets[slot].key == key) return &map->buckets[slot];
        slot = (slot + 1U) & (map->capacity - 1U);
    }
    if (!create) return NULL;
    map->buckets[slot].used = 1U;
    map->buckets[slot].key = key;
    ++map->count;
    return &map->buckets[slot];
}

static bool area_bucket_push(AreaTileBucket *bucket,
                             OpenRideORMapAreaTriangle triangle)
{
    if (!bucket) return false;
    if (bucket->count == bucket->capacity) {
        uint32_t capacity = bucket->capacity == 0U ? 32U : bucket->capacity * 2U;
        if (capacity < bucket->capacity) return false;
        OpenRideORMapAreaTriangle *triangles = realloc(
            bucket->triangles,
            (size_t)capacity * sizeof(*triangles));
        if (!triangles) return false;
        bucket->triangles = triangles;
        bucket->capacity = capacity;
    }
    bucket->triangles[bucket->count++] = triangle;
    return true;
}

static void area_map_destroy(AreaTileMap *map)
{
    if (!map) return;
    for (uint32_t i = 0U; i < map->capacity; ++i) free(map->buckets[i].triangles);
    free(map->buckets);
    memset(map, 0, sizeof(*map));
}

static bool mask_map_rehash(MaskTileMap *map, uint32_t new_capacity)
{
    MaskTileBucket *new_buckets = calloc(new_capacity, sizeof(*new_buckets));
    if (!new_buckets) return false;
    for (uint32_t i = 0U; i < map->capacity; ++i) {
        MaskTileBucket old = map->buckets[i];
        if (!old.used) continue;
        uint32_t slot = hash64(old.key) & (new_capacity - 1U);
        while (new_buckets[slot].used) slot = (slot + 1U) & (new_capacity - 1U);
        new_buckets[slot] = old;
    }
    free(map->buckets);
    map->buckets = new_buckets;
    map->capacity = new_capacity;
    return true;
}

static MaskTileBucket *mask_map_get(MaskTileMap *map, int x, int y, bool create)
{
    if (map->capacity == 0U) {
        if (!create || !mask_map_rehash(map, 1024U)) return NULL;
    }
    if (create && (map->count + 1U) * 10U >= map->capacity * 7U) {
        if (map->capacity > UINT32_MAX / 2U
            || !mask_map_rehash(map, map->capacity * 2U)) return NULL;
    }
    const uint64_t key = tile_key(OPENRIDE_ORMAP_MASK_ZOOM, x, y);
    uint32_t slot = hash64(key) & (map->capacity - 1U);
    while (map->buckets[slot].used) {
        if (map->buckets[slot].key == key) return &map->buckets[slot];
        slot = (slot + 1U) & (map->capacity - 1U);
    }
    if (!create) return NULL;
    map->buckets[slot].used = 1U;
    map->buckets[slot].key = key;
    ++map->count;
    return &map->buckets[slot];
}

static void mask_map_destroy(MaskTileMap *map)
{
    if (!map) return;
    free(map->buckets);
    memset(map, 0, sizeof(*map));
}

static double mercator_x(double lon)
{
    return (lon + 180.0) / 360.0;
}

static double mercator_y(double lat)
{
    if (lat > 85.05112878) lat = 85.05112878;
    if (lat < -85.05112878) lat = -85.05112878;
    const double rad = lat * ORMAP_PI / 180.0;
    return (1.0 - asinh(tan(rad)) / ORMAP_PI) * 0.5;
}

static bool road_visible_at_zoom(OpenRideRoadClass road_class, int zoom)
{
    if (zoom <= OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM) {
        return road_class == OPENRIDE_ROAD_MOTORWAY
            || road_class == OPENRIDE_ROAD_TRUNK;
    }
    if (zoom <= OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM) {
        return road_class >= OPENRIDE_ROAD_MOTORWAY
            && road_class <= OPENRIDE_ROAD_PRIMARY;
    }
    if (zoom <= OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM) {
        return road_class >= OPENRIDE_ROAD_MOTORWAY
            && road_class <= OPENRIDE_ROAD_TERTIARY;
    }
    return road_class != OPENRIDE_ROAD_UNKNOWN;
}

static const OpenRideRoutingEdge *segment_edge(const OpenRideRoutingGraph *graph,
                                                uint32_t a,
                                                uint32_t b)
{
    if (!graph || a >= graph->node_count || b >= graph->node_count) return NULL;
    const OpenRideRoutingNode *node = &graph->nodes[a];
    for (uint32_t i = 0U; i < node->edge_count; ++i) {
        const OpenRideRoutingEdge *edge = &graph->edges[node->first_edge + i];
        if (edge->target == b) return edge;
    }
    node = &graph->nodes[b];
    for (uint32_t i = 0U; i < node->edge_count; ++i) {
        const OpenRideRoutingEdge *edge = &graph->edges[node->first_edge + i];
        if (edge->target == a) return edge;
    }
    return NULL;
}

static bool clip_test(double p, double q, double *u1, double *u2)
{
    if (fabs(p) < 1e-15) return q >= 0.0;
    const double r = q / p;
    if (p < 0.0) {
        if (r > *u2) return false;
        if (r > *u1) *u1 = r;
    } else {
        if (r < *u1) return false;
        if (r < *u2) *u2 = r;
    }
    return true;
}

static bool clip_line_to_tile(double x0,
                              double y0,
                              double x1,
                              double y1,
                              int tile_x,
                              int tile_y,
                              double *cx0,
                              double *cy0,
                              double *cx1,
                              double *cy1)
{
    double u1 = 0.0, u2 = 1.0;
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    if (!clip_test(-dx, x0 - (double)tile_x, &u1, &u2)
        || !clip_test(dx, (double)tile_x + 1.0 - x0, &u1, &u2)
        || !clip_test(-dy, y0 - (double)tile_y, &u1, &u2)
        || !clip_test(dy, (double)tile_y + 1.0 - y0, &u1, &u2)) {
        return false;
    }
    *cx0 = x0 + u1 * dx;
    *cy0 = y0 + u1 * dy;
    *cx1 = x0 + u2 * dx;
    *cy1 = y0 + u2 * dy;
    return true;
}

static uint16_t quantize_tile_coord(double value, int tile)
{
    double local = value - (double)tile;
    if (local < 0.0) local = 0.0;
    if (local > 1.0) local = 1.0;
    return (uint16_t)llround(local * 65535.0);
}

static uint16_t quantize_area_coord(double value, int tile)
{
    const double buffer = OPENRIDE_ORMAP_AREA_BUFFER_FRACTION;
    const double span = 1.0 + 2.0 * buffer;
    double normalized = (value - (double)tile + buffer) / span;
    if (normalized < 0.0) normalized = 0.0;
    if (normalized > 1.0) normalized = 1.0;
    return (uint16_t)llround(normalized * 65535.0);
}

static double point_cross(ORMapPoint a, ORMapPoint b, ORMapPoint c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static double polygon_signed_area(const ORMapPoint *points, uint32_t count)
{
    if (!points || count < 3U) return 0.0;
    double twice_area = 0.0;
    for (uint32_t i = 0U; i < count; ++i) {
        const ORMapPoint a = points[i];
        const ORMapPoint b = points[(i + 1U) % count];
        twice_area += a.x * b.y - b.x * a.y;
    }
    return twice_area * 0.5;
}

static double point_segment_distance_sq(ORMapPoint point,
                                        ORMapPoint a,
                                        ORMapPoint b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length_sq = dx * dx + dy * dy;
    if (length_sq <= 1e-24) {
        const double px = point.x - a.x;
        const double py = point.y - a.y;
        return px * px + py * py;
    }
    double t = ((point.x - a.x) * dx + (point.y - a.y) * dy) / length_sq;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const double px = point.x - (a.x + t * dx);
    const double py = point.y - (a.y + t * dy);
    return px * px + py * py;
}

static void rdp_mark(const ORMapPoint *points,
                     uint32_t first,
                     uint32_t last,
                     double tolerance_sq,
                     unsigned char *keep)
{
    if (!points || !keep || last <= first + 1U) return;
    double maximum = -1.0;
    uint32_t selected = first;
    for (uint32_t i = first + 1U; i < last; ++i) {
        const double distance = point_segment_distance_sq(points[i],
                                                          points[first],
                                                          points[last]);
        if (distance > maximum) {
            maximum = distance;
            selected = i;
        }
    }
    if (maximum > tolerance_sq && selected > first && selected < last) {
        keep[selected] = 1U;
        rdp_mark(points, first, selected, tolerance_sq, keep);
        rdp_mark(points, selected, last, tolerance_sq, keep);
    }
}

typedef struct RoadEndpointRef {
    uint32_t key;
    uint32_t record_index;
} RoadEndpointRef;

static uint32_t road_endpoint_key(uint16_t x, uint16_t y)
{
    return ((uint32_t)x << 16U) | (uint32_t)y;
}

static uint32_t road_record_endpoint_key(const OpenRideORMapRoadRecord *record,
                                         int endpoint)
{
    return endpoint == 0
        ? road_endpoint_key(record->x1, record->y1)
        : road_endpoint_key(record->x2, record->y2);
}

static bool road_records_compatible(const OpenRideORMapRoadRecord *a,
                                    const OpenRideORMapRoadRecord *b)
{
    return a && b
        && a->road_class == b->road_class
        && a->surface == b->surface
        && a->flags == b->flags;
}

static int compare_road_endpoint_ref(const void *left_ptr,
                                     const void *right_ptr)
{
    const RoadEndpointRef *left = left_ptr;
    const RoadEndpointRef *right = right_ptr;
    if (left->key != right->key) return left->key < right->key ? -1 : 1;
    if (left->record_index != right->record_index) {
        return left->record_index < right->record_index ? -1 : 1;
    }
    return 0;
}

static uint32_t road_endpoint_lower_bound(const RoadEndpointRef *refs,
                                          uint32_t count,
                                          uint32_t key)
{
    uint32_t low = 0U;
    uint32_t high = count;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2U;
        if (refs[middle].key < key) low = middle + 1U;
        else high = middle;
    }
    return low;
}

static uint32_t road_endpoint_degree(const RoadEndpointRef *refs,
                                     uint32_t ref_count,
                                     uint32_t key)
{
    uint32_t degree = 0U;
    for (uint32_t i = road_endpoint_lower_bound(refs, ref_count, key);
         i < ref_count && refs[i].key == key;
         ++i) {
        ++degree;
    }
    return degree;
}

static uint32_t road_endpoint_compatible_degree(
    const RoadEndpointRef *refs,
    uint32_t ref_count,
    uint32_t key,
    const OpenRideORMapRoadRecord *records,
    const OpenRideORMapRoadRecord *signature)
{
    uint32_t degree = 0U;
    for (uint32_t i = road_endpoint_lower_bound(refs, ref_count, key);
         i < ref_count && refs[i].key == key;
         ++i) {
        if (road_records_compatible(&records[refs[i].record_index], signature)) {
            ++degree;
        }
    }
    return degree;
}

static int32_t road_endpoint_next_unused(
    const RoadEndpointRef *refs,
    uint32_t ref_count,
    uint32_t key,
    const OpenRideORMapRoadRecord *records,
    const OpenRideORMapRoadRecord *signature,
    const unsigned char *used)
{
    for (uint32_t i = road_endpoint_lower_bound(refs, ref_count, key);
         i < ref_count && refs[i].key == key;
         ++i) {
        const uint32_t record_index = refs[i].record_index;
        if (!used[record_index]
            && road_records_compatible(&records[record_index], signature)) {
            return (int32_t)record_index;
        }
    }
    return -1;
}

static bool road_endpoint_is_chain_boundary(
    const RoadEndpointRef *refs,
    uint32_t ref_count,
    uint32_t key,
    const OpenRideORMapRoadRecord *records,
    const OpenRideORMapRoadRecord *signature)
{
    /* Never simplify through a real junction, even if only two of the roads
     * share the same class/surface. Keeping that node avoids visible gaps or
     * shifted T-junctions after RDP moves the compatible road through it. */
    return road_endpoint_degree(refs, ref_count, key) != 2U
        || road_endpoint_compatible_degree(refs,
                                           ref_count,
                                           key,
                                           records,
                                           signature) != 2U;
}

static double road_simplify_pixels_for_zoom(int zoom)
{
    /* Tolerance is expressed in pixels at the stored road LOD. With the
     * renderer's current handoff ranges this stays at or below ~1 screen
     * pixel at the upper end of each LOD, and below ~0.6 px for z14 detail. */
    if (zoom == OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM) return 0.10;
    if (zoom == OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM) return 0.14;
    if (zoom == OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM) return 0.18;
    if (zoom == OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM) return 0.06;
    return 0.0;
}

static bool road_emit_simplified_chain(
    const ORMapPoint *points,
    uint32_t point_count,
    double tolerance_sq,
    const OpenRideORMapRoadRecord *signature,
    unsigned char *keep,
    OpenRideORMapRoadRecord *output,
    uint32_t output_capacity,
    uint32_t *output_count)
{
    if (!points || point_count < 2U || !signature || !keep
        || !output || !output_count) {
        return false;
    }

    memset(keep, 0, point_count);
    keep[0] = 1U;
    keep[point_count - 1U] = 1U;
    if (point_count > 2U) {
        rdp_mark(points, 0U, point_count - 1U, tolerance_sq, keep);
    }

    uint32_t previous = 0U;
    for (uint32_t i = 1U; i < point_count; ++i) {
        if (!keep[i]) continue;
        if (*output_count >= output_capacity) return false;
        OpenRideORMapRoadRecord record = *signature;
        record.x1 = (uint16_t)points[previous].x;
        record.y1 = (uint16_t)points[previous].y;
        record.x2 = (uint16_t)points[i].x;
        record.y2 = (uint16_t)points[i].y;
        output[(*output_count)++] = record;
        previous = i;
    }
    return true;
}

static bool road_bucket_simplify(RoadTileBucket *bucket,
                                 double tolerance_pixels,
                                 uint32_t *removed_out)
{
    if (removed_out) *removed_out = 0U;
    if (!bucket || bucket->count < 2U || tolerance_pixels <= 0.0) return true;

    const uint32_t input_count = bucket->count;
    if (input_count > UINT32_MAX / 2U) return false;
    const uint32_t ref_count = input_count * 2U;

    RoadEndpointRef *refs = malloc((size_t)ref_count * sizeof(*refs));
    unsigned char *used = calloc(input_count, 1U);
    ORMapPoint *points = malloc(((size_t)input_count + 1U) * sizeof(*points));
    unsigned char *keep = malloc((size_t)input_count + 1U);
    OpenRideORMapRoadRecord *output =
        malloc((size_t)input_count * sizeof(*output));
    if (!refs || !used || !points || !keep || !output) {
        free(refs);
        free(used);
        free(points);
        free(keep);
        free(output);
        return false;
    }

    for (uint32_t i = 0U; i < input_count; ++i) {
        refs[i * 2U] = (RoadEndpointRef){
            .key = road_record_endpoint_key(&bucket->records[i], 0),
            .record_index = i
        };
        refs[i * 2U + 1U] = (RoadEndpointRef){
            .key = road_record_endpoint_key(&bucket->records[i], 1),
            .record_index = i
        };
    }
    qsort(refs, ref_count, sizeof(*refs), compare_road_endpoint_ref);

    const double tolerance = tolerance_pixels * 65535.0 / 256.0;
    const double tolerance_sq = tolerance * tolerance;
    uint32_t output_count = 0U;

    /* First consume open chains. Interior degree-2 records are deliberately
     * skipped until an endpoint/junction seed reaches them, so record order
     * cannot fragment a long road before RDP sees the complete tile chain. */
    for (uint32_t seed_index = 0U; seed_index < input_count; ++seed_index) {
        if (used[seed_index]) continue;
        const OpenRideORMapRoadRecord *seed = &bucket->records[seed_index];
        const uint32_t key0 = road_record_endpoint_key(seed, 0);
        const uint32_t key1 = road_record_endpoint_key(seed, 1);
        const bool boundary0 = road_endpoint_is_chain_boundary(
            refs, ref_count, key0, bucket->records, seed);
        const bool boundary1 = road_endpoint_is_chain_boundary(
            refs, ref_count, key1, bucket->records, seed);
        if (!boundary0 && !boundary1) continue;

        const int start_endpoint = boundary0 ? 0 : 1;
        uint32_t current_key =
            road_record_endpoint_key(seed, 1 - start_endpoint);
        uint32_t point_count = 0U;
        points[point_count++] = (ORMapPoint){
            start_endpoint == 0 ? seed->x1 : seed->x2,
            start_endpoint == 0 ? seed->y1 : seed->y2
        };
        points[point_count++] = (ORMapPoint){
            start_endpoint == 0 ? seed->x2 : seed->x1,
            start_endpoint == 0 ? seed->y2 : seed->y1
        };
        used[seed_index] = 1U;

        while (!road_endpoint_is_chain_boundary(refs,
                                                ref_count,
                                                current_key,
                                                bucket->records,
                                                seed)) {
            const int32_t next_index = road_endpoint_next_unused(
                refs,
                ref_count,
                current_key,
                bucket->records,
                seed,
                used);
            if (next_index < 0) break;

            const OpenRideORMapRoadRecord *next =
                &bucket->records[(uint32_t)next_index];
            const bool enter_at_zero =
                road_record_endpoint_key(next, 0) == current_key;
            current_key =
                road_record_endpoint_key(next, enter_at_zero ? 1 : 0);
            points[point_count++] = (ORMapPoint){
                enter_at_zero ? next->x2 : next->x1,
                enter_at_zero ? next->y2 : next->y1
            };
            used[(uint32_t)next_index] = 1U;
        }

        if (!road_emit_simplified_chain(points,
                                        point_count,
                                        tolerance_sq,
                                        seed,
                                        keep,
                                        output,
                                        input_count,
                                        &output_count)) {
            free(refs);
            free(used);
            free(points);
            free(keep);
            free(output);
            return false;
        }
    }

    /* The records left here are closed degree-2 loops. Preserve them exactly
     * in V2 rather than risk deforming roundabouts or other small closed ways. */
    for (uint32_t i = 0U; i < input_count; ++i) {
        if (used[i]) continue;
        if (output_count >= input_count) {
            free(refs);
            free(used);
            free(points);
            free(keep);
            free(output);
            return false;
        }
        output[output_count++] = bucket->records[i];
    }

    free(refs);
    free(used);
    free(points);
    free(keep);

    if (output_count < input_count) {
        if (removed_out) *removed_out = input_count - output_count;
        free(bucket->records);
        bucket->records = output;
        bucket->count = output_count;
        bucket->capacity = input_count;
    } else {
        free(output);
    }
    return true;
}

static void road_stats_remove_records(OpenRideORMapBuildStats *stats,
                                      int zoom,
                                      uint64_t removed)
{
    if (!stats || removed == 0U) return;
    if (stats->road_records_written >= removed) {
        stats->road_records_written -= removed;
    }
    if (zoom == OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM
        && stats->road_regional_records >= removed) {
        stats->road_regional_records -= removed;
    } else if (zoom == OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM
               && stats->road_overview_records >= removed) {
        stats->road_overview_records -= removed;
    } else if (zoom == OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM
               && stats->road_local_records >= removed) {
        stats->road_local_records -= removed;
    } else if (zoom == OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM
               && stats->road_detail_records >= removed) {
        stats->road_detail_records -= removed;
    }
}

static bool simplify_road_tiles(RoadTileMap *tiles,
                                int zoom,
                                OpenRideORMapBuildStats *stats,
                                char *error,
                                size_t error_size)
{
    if (!tiles) return false;
    const double tolerance_pixels = road_simplify_pixels_for_zoom(zoom);
    if (tolerance_pixels <= 0.0) return true;

    uint64_t removed = 0U;
    for (uint32_t i = 0U; i < tiles->capacity; ++i) {
        RoadTileBucket *bucket = &tiles->buckets[i];
        if (!bucket->used || bucket->count < 2U) continue;
        uint32_t bucket_removed = 0U;
        if (!road_bucket_simplify(bucket,
                                  tolerance_pixels,
                                  &bucket_removed)) {
            set_error(error,
                      error_size,
                      "out of memory simplifying road geometry");
            return false;
        }
        removed += bucket_removed;
    }
    road_stats_remove_records(stats, zoom, removed);
    return true;
}

static bool simplify_closed_ring(const ORMapPoint *input,
                                 uint32_t input_count,
                                 double tolerance,
                                 ORMapPoint **output,
                                 uint32_t *output_count)
{
    *output = NULL;
    *output_count = 0U;
    if (!input || input_count < 3U) return true;

    uint32_t unique_limit = input_count;
    if (input_count >= 2U
        && fabs(input[0].x - input[input_count - 1U].x) < 1e-12
        && fabs(input[0].y - input[input_count - 1U].y) < 1e-12) {
        --unique_limit;
    }
    ORMapPoint *ring = malloc(((size_t)unique_limit + 1U) * sizeof(*ring));
    if (!ring) return false;
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < unique_limit; ++i) {
        if (count > 0U
            && fabs(input[i].x - ring[count - 1U].x) < 1e-12
            && fabs(input[i].y - ring[count - 1U].y) < 1e-12) {
            continue;
        }
        ring[count++] = input[i];
    }
    if (count >= 2U
        && fabs(ring[0].x - ring[count - 1U].x) < 1e-12
        && fabs(ring[0].y - ring[count - 1U].y) < 1e-12) {
        --count;
    }
    if (count < 3U) {
        free(ring);
        return true;
    }

    uint32_t split = 1U;
    double farthest = -1.0;
    for (uint32_t i = 1U; i < count; ++i) {
        const double dx = ring[i].x - ring[0].x;
        const double dy = ring[i].y - ring[0].y;
        const double distance = dx * dx + dy * dy;
        if (distance > farthest) {
            farthest = distance;
            split = i;
        }
    }
    ring[count] = ring[0];
    unsigned char *keep = calloc((size_t)count + 1U, 1U);
    if (!keep) {
        free(ring);
        return false;
    }
    keep[0] = 1U;
    keep[split] = 1U;
    keep[count] = 1U;
    const double tolerance_sq = tolerance > 0.0 ? tolerance * tolerance : 0.0;
    rdp_mark(ring, 0U, split, tolerance_sq, keep);
    rdp_mark(ring, split, count, tolerance_sq, keep);

    ORMapPoint *simplified = malloc((size_t)count * sizeof(*simplified));
    if (!simplified) {
        free(keep);
        free(ring);
        return false;
    }
    uint32_t simplified_count = 0U;
    for (uint32_t i = 0U; i < count; ++i) {
        if (keep[i]) simplified[simplified_count++] = ring[i];
    }
    free(keep);
    free(ring);
    if (simplified_count < 3U) {
        free(simplified);
        return true;
    }
    *output = simplified;
    *output_count = simplified_count;
    return true;
}

/*
 * One Chaikin corner-cutting pass removes the visible stair-step signature
 * left by the temporary z16 built-up occupancy grid. The grid remains useful
 * for aggregation, but its square cells must not dictate the final geometry.
 */
static bool smooth_closed_ring(const ORMapPoint *input,
                               uint32_t input_count,
                               ORMapPoint **output,
                               uint32_t *output_count)
{
    *output = NULL;
    *output_count = 0U;
    if (!input || input_count < 3U) return true;

    uint32_t count = input_count;
    if (count >= 2U
        && fabs(input[0].x - input[count - 1U].x) < 1e-12
        && fabs(input[0].y - input[count - 1U].y) < 1e-12) {
        --count;
    }
    if (count < 3U || count > UINT32_MAX / 2U) return true;

    ORMapPoint *smoothed = malloc((size_t)count * 2U * sizeof(*smoothed));
    if (!smoothed) return false;
    for (uint32_t i = 0U; i < count; ++i) {
        const ORMapPoint a = input[i];
        const ORMapPoint b = input[(i + 1U) % count];
        smoothed[i * 2U] = (ORMapPoint){
            .x = a.x * 0.75 + b.x * 0.25,
            .y = a.y * 0.75 + b.y * 0.25
        };
        smoothed[i * 2U + 1U] = (ORMapPoint){
            .x = a.x * 0.25 + b.x * 0.75,
            .y = a.y * 0.25 + b.y * 0.75
        };
    }
    *output = smoothed;
    *output_count = count * 2U;
    return true;
}

static bool clip_inside(ORMapPoint point, unsigned edge, double bound)
{
    switch (edge) {
        case 0U: return point.x >= bound;
        case 1U: return point.x <= bound;
        case 2U: return point.y >= bound;
        default: return point.y <= bound;
    }
}

static ORMapPoint clip_intersection(ORMapPoint a,
                                    ORMapPoint b,
                                    unsigned edge,
                                    double bound)
{
    ORMapPoint result = a;
    if (edge <= 1U) {
        const double dx = b.x - a.x;
        const double t = fabs(dx) < 1e-18 ? 0.0 : (bound - a.x) / dx;
        result.x = bound;
        result.y = a.y + (b.y - a.y) * t;
    } else {
        const double dy = b.y - a.y;
        const double t = fabs(dy) < 1e-18 ? 0.0 : (bound - a.y) / dy;
        result.x = a.x + (b.x - a.x) * t;
        result.y = bound;
    }
    return result;
}

static uint32_t clip_polygon_edge(const ORMapPoint *input,
                                  uint32_t input_count,
                                  ORMapPoint *output,
                                  unsigned edge,
                                  double bound)
{
    if (!input || !output || input_count == 0U) return 0U;
    uint32_t output_count = 0U;
    ORMapPoint previous = input[input_count - 1U];
    bool previous_inside = clip_inside(previous, edge, bound);
    for (uint32_t i = 0U; i < input_count; ++i) {
        const ORMapPoint current = input[i];
        const bool current_inside = clip_inside(current, edge, bound);
        if (current_inside != previous_inside) {
            output[output_count++] = clip_intersection(previous,
                                                       current,
                                                       edge,
                                                       bound);
        }
        if (current_inside) output[output_count++] = current;
        previous = current;
        previous_inside = current_inside;
    }
    return output_count;
}

static bool emit_area_triangle(AreaTileMap *tiles,
                               int zoom,
                               uint8_t kind,
                               ORMapPoint a,
                               ORMapPoint b,
                               ORMapPoint c,
                               OpenRideORMapBuildStats *stats)
{
    const int tile_count = 1 << zoom;
    const double buffer = OPENRIDE_ORMAP_AREA_BUFFER_FRACTION;
    double min_x = fmin(a.x, fmin(b.x, c.x));
    double max_x = fmax(a.x, fmax(b.x, c.x));
    double min_y = fmin(a.y, fmin(b.y, c.y));
    double max_y = fmax(a.y, fmax(b.y, c.y));
    int first_x = (int)floor(min_x - buffer);
    int last_x = (int)floor(max_x + buffer);
    int first_y = (int)floor(min_y - buffer);
    int last_y = (int)floor(max_y + buffer);
    if (first_x < 0) first_x = 0;
    if (first_y < 0) first_y = 0;
    if (last_x >= tile_count) last_x = tile_count - 1;
    if (last_y >= tile_count) last_y = tile_count - 1;
    if (first_x > last_x || first_y > last_y) return true;

    for (int ty = first_y; ty <= last_y; ++ty) {
        for (int tx = first_x; tx <= last_x; ++tx) {
            ORMapPoint buffer_a[8] = {a, b, c};
            ORMapPoint buffer_b[8];
            uint32_t count = 3U;
            count = clip_polygon_edge(buffer_a,
                                      count,
                                      buffer_b,
                                      0U,
                                      (double)tx - buffer);
            if (count < 3U) continue;
            count = clip_polygon_edge(buffer_b,
                                      count,
                                      buffer_a,
                                      1U,
                                      (double)tx + 1.0 + buffer);
            if (count < 3U) continue;
            count = clip_polygon_edge(buffer_a,
                                      count,
                                      buffer_b,
                                      2U,
                                      (double)ty - buffer);
            if (count < 3U) continue;
            count = clip_polygon_edge(buffer_b,
                                      count,
                                      buffer_a,
                                      3U,
                                      (double)ty + 1.0 + buffer);
            if (count < 3U) continue;

            AreaTileBucket *bucket = area_map_get(tiles, zoom, tx, ty, true);
            if (!bucket) return false;
            for (uint32_t i = 1U; i + 1U < count; ++i) {
                if (fabs(point_cross(buffer_a[0], buffer_a[i], buffer_a[i + 1U])) < 1e-15) {
                    continue;
                }
                OpenRideORMapAreaTriangle triangle = {
                    .x1 = quantize_area_coord(buffer_a[0].x, tx),
                    .y1 = quantize_area_coord(buffer_a[0].y, ty),
                    .x2 = quantize_area_coord(buffer_a[i].x, tx),
                    .y2 = quantize_area_coord(buffer_a[i].y, ty),
                    .x3 = quantize_area_coord(buffer_a[i + 1U].x, tx),
                    .y3 = quantize_area_coord(buffer_a[i + 1U].y, ty),
                    .kind = kind,
                    .reserved = 0U
                };
                if (!area_bucket_push(bucket, triangle)) return false;
                ++stats->area_triangles_written;
            }
        }
    }
    return true;
}

static bool point_strictly_inside_triangle(ORMapPoint point,
                                           ORMapPoint a,
                                           ORMapPoint b,
                                           ORMapPoint c,
                                           double orientation)
{
    const double epsilon = 1e-14;
    return orientation * point_cross(a, b, point) > epsilon
        && orientation * point_cross(b, c, point) > epsilon
        && orientation * point_cross(c, a, point) > epsilon;
}

static bool triangulate_ring_to_area_map(const ORMapPoint *points,
                                         uint32_t count,
                                         int zoom,
                                         uint8_t kind,
                                         AreaTileMap *tiles,
                                         OpenRideORMapBuildStats *stats)
{
    if (!points || count < 3U) {
        ++stats->area_polygons_skipped;
        return true;
    }
    const double signed_area = polygon_signed_area(points, count);
    if (fabs(signed_area) < 1e-14) {
        ++stats->area_polygons_skipped;
        return true;
    }
    const double orientation = signed_area > 0.0 ? 1.0 : -1.0;
    uint32_t *indices = malloc((size_t)count * sizeof(*indices));
    if (!indices) return false;
    for (uint32_t i = 0U; i < count; ++i) indices[i] = i;

    uint32_t remaining = count;
    uint64_t guard = (uint64_t)count * (uint64_t)count + 1U;
    while (remaining > 3U && guard-- > 0U) {
        bool clipped_ear = false;
        for (uint32_t i = 0U; i < remaining; ++i) {
            const uint32_t prev = indices[(i + remaining - 1U) % remaining];
            const uint32_t curr = indices[i];
            const uint32_t next = indices[(i + 1U) % remaining];
            const ORMapPoint a = points[prev];
            const ORMapPoint b = points[curr];
            const ORMapPoint c = points[next];
            if (orientation * point_cross(a, b, c) <= 1e-14) continue;

            bool contains = false;
            for (uint32_t j = 0U; j < remaining; ++j) {
                const uint32_t candidate = indices[j];
                if (candidate == prev || candidate == curr || candidate == next) continue;
                if (point_strictly_inside_triangle(points[candidate],
                                                   a,
                                                   b,
                                                   c,
                                                   orientation)) {
                    contains = true;
                    break;
                }
            }
            if (contains) continue;
            if (!emit_area_triangle(tiles, zoom, kind, a, b, c, stats)) {
                free(indices);
                return false;
            }
            memmove(indices + i,
                    indices + i + 1U,
                    (size_t)(remaining - i - 1U) * sizeof(*indices));
            --remaining;
            clipped_ear = true;
            break;
        }
        if (!clipped_ear) {
            ++stats->area_polygons_skipped;
            free(indices);
            return true;
        }
    }
    if (remaining == 3U) {
        const ORMapPoint a = points[indices[0]];
        const ORMapPoint b = points[indices[1]];
        const ORMapPoint c = points[indices[2]];
        if (!emit_area_triangle(tiles, zoom, kind, a, b, c, stats)) {
            free(indices);
            return false;
        }
    }
    free(indices);
    return true;
}

static bool collect_area_ring(const double *latitudes,
                              const double *longitudes,
                              uint32_t point_count,
                              int zoom,
                              uint8_t kind,
                              double simplify_pixels,
                              double minimum_area_pixels,
                              AreaTileMap *tiles,
                              OpenRideORMapBuildStats *stats)
{
    if (!latitudes || !longitudes || point_count < 4U) return true;
    ORMapPoint *ring = malloc((size_t)point_count * sizeof(*ring));
    if (!ring) return false;
    const double scale = (double)(1U << zoom);
    for (uint32_t i = 0U; i < point_count; ++i) {
        ring[i].x = mercator_x(longitudes[i]) * scale;
        ring[i].y = mercator_y(latitudes[i]) * scale;
    }
    ORMapPoint *simplified = NULL;
    uint32_t simplified_count = 0U;
    const double tolerance = simplify_pixels / 256.0;
    const bool ok = simplify_closed_ring(ring,
                                         point_count,
                                         tolerance,
                                         &simplified,
                                         &simplified_count);
    free(ring);
    if (!ok) return false;
    if (!simplified || simplified_count < 3U) {
        free(simplified);
        ++stats->area_polygons_skipped;
        return true;
    }
    const double pixel_area = fabs(polygon_signed_area(simplified, simplified_count))
        * 256.0 * 256.0;
    if (pixel_area < minimum_area_pixels) {
        free(simplified);
        return true;
    }
    const bool triangulated = triangulate_ring_to_area_map(simplified,
                                                           simplified_count,
                                                           zoom,
                                                           kind,
                                                           tiles,
                                                           stats);
    free(simplified);
    return triangulated;
}

static void count_road_record_for_lod(OpenRideORMapBuildStats *stats, int zoom)
{
    if (!stats) return;
    if (zoom == OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM) {
        ++stats->road_regional_records;
    } else if (zoom == OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM) {
        ++stats->road_overview_records;
    } else if (zoom == OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM) {
        ++stats->road_local_records;
    } else if (zoom == OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM) {
        ++stats->road_detail_records;
    }
}

static bool collect_roads_at_zoom(const OpenRideRoutingGraph *graph,
                                  int zoom,
                                  RoadTileMap *tiles,
                                  OpenRideORMapBuildStats *stats,
                                  char *error,
                                  size_t error_size)
{
    for (uint32_t s = 0U; s < graph->segment_index.segment_count; ++s) {
        const OpenRideRoutingSegment *segment = &graph->segment_index.segments[s];
        if (segment->a >= graph->node_count || segment->b >= graph->node_count) continue;
        const OpenRideRoutingEdge *edge = segment_edge(graph, segment->a, segment->b);
        if (!edge) continue;
        if (zoom == OPENRIDE_ORMAP_MAX_ROAD_ZOOM) ++stats->routing_segments_seen;
        double lat0 = 0.0, lon0 = 0.0, lat1 = 0.0, lon1 = 0.0;
        openride_routing_node_geo(&graph->nodes[segment->a], &lat0, &lon0);
        openride_routing_node_geo(&graph->nodes[segment->b], &lat1, &lon1);
        const double mx0 = mercator_x(lon0);
        const double my0 = mercator_y(lat0);
        const double mx1 = mercator_x(lon1);
        const double my1 = mercator_y(lat1);

        if (!road_visible_at_zoom((OpenRideRoadClass)edge->road_class, zoom)) continue;
        const int n = 1 << zoom;
            const double x0 = mx0 * n, y0 = my0 * n;
            const double x1 = mx1 * n, y1 = my1 * n;
            int min_x = (int)floor(fmin(x0, x1));
            int max_x = (int)floor(fmax(x0, x1));
            int min_y = (int)floor(fmin(y0, y1));
            int max_y = (int)floor(fmax(y0, y1));
            if (min_x < 0) min_x = 0;
            if (min_y < 0) min_y = 0;
            if (max_x >= n) max_x = n - 1;
            if (max_y >= n) max_y = n - 1;
            /* OSM routing nodes make road segments short. Guard malformed data. */
            if (max_x - min_x > 8 || max_y - min_y > 8) continue;
            for (int ty = min_y; ty <= max_y; ++ty) {
                for (int tx = min_x; tx <= max_x; ++tx) {
                    double cx0 = 0.0, cy0 = 0.0, cx1 = 0.0, cy1 = 0.0;
                    if (!clip_line_to_tile(x0, y0, x1, y1, tx, ty,
                                           &cx0, &cy0, &cx1, &cy1)) continue;
                    RoadTileBucket *bucket = road_map_get(tiles, zoom, tx, ty, true);
                    if (!bucket) {
                        set_error(error, error_size, "out of memory collecting road tiles");
                        return false;
                    }
                    OpenRideORMapRoadRecord record = {
                        .x1 = quantize_tile_coord(cx0, tx),
                        .y1 = quantize_tile_coord(cy0, ty),
                        .x2 = quantize_tile_coord(cx1, tx),
                        .y2 = quantize_tile_coord(cy1, ty),
                        .road_class = edge->road_class,
                        .surface = edge->surface,
                        .flags = (uint16_t)(edge->flags & 0xffffU)
                    };
                    if (!road_bucket_push(bucket, record)) {
                        set_error(error, error_size, "out of memory appending road record");
                        return false;
                    }
                    ++stats->road_records_written;
                    count_road_record_for_lod(stats, zoom);
                }
            }
    }
    return true;
}

static bool bit_get(const unsigned char *bits, uint32_t index)
{
    return (bits[index >> 3U] & (unsigned char)(1U << (index & 7U))) != 0U;
}

static void bit_set(unsigned char *bits, uint32_t index)
{
    bits[index >> 3U] |= (unsigned char)(1U << (index & 7U));
}

static int compare_double(const void *a, const void *b)
{
    const double av = *(const double *)a;
    const double bv = *(const double *)b;
    return av < bv ? -1 : (av > bv ? 1 : 0);
}

static bool mask_set_global(MaskTileMap *map,
                            unsigned layer,
                            int64_t gx,
                            int64_t gy)
{
    if (!map || layer >= 3U || gx < 0 || gy < 0) return true;
    const int tile_count = 1 << OPENRIDE_ORMAP_MASK_ZOOM;
    const int64_t global_cells = (int64_t)tile_count * OPENRIDE_ORMAP_MASK_GRID;
    if (gx >= global_cells || gy >= global_cells) return true;
    const int tx = (int)(gx / OPENRIDE_ORMAP_MASK_GRID);
    const int ty = (int)(gy / OPENRIDE_ORMAP_MASK_GRID);
    MaskTileBucket *bucket = mask_map_get(map, tx, ty, true);
    if (!bucket) return false;
    const uint32_t x = (uint32_t)(gx % OPENRIDE_ORMAP_MASK_GRID);
    const uint32_t y = (uint32_t)(gy % OPENRIDE_ORMAP_MASK_GRID);
    bit_set(bucket->layers + layer * ORMAP_MASK_LAYER_BYTES,
            y * OPENRIDE_ORMAP_MASK_GRID + x);
    return true;
}

static bool mask_get_global(const MaskTileMap *map,
                            unsigned layer,
                            int64_t gx,
                            int64_t gy)
{
    if (!map || layer >= 3U || gx < 0 || gy < 0) return false;
    const int tile_count = 1 << OPENRIDE_ORMAP_MASK_ZOOM;
    const int64_t global_cells = (int64_t)tile_count * OPENRIDE_ORMAP_MASK_GRID;
    if (gx >= global_cells || gy >= global_cells || map->capacity == 0U) return false;
    const int tx = (int)(gx / OPENRIDE_ORMAP_MASK_GRID);
    const int ty = (int)(gy / OPENRIDE_ORMAP_MASK_GRID);
    const uint64_t key = tile_key(OPENRIDE_ORMAP_MASK_ZOOM, tx, ty);
    uint32_t slot = hash64(key) & (map->capacity - 1U);
    while (map->buckets[slot].used) {
        const MaskTileBucket *bucket = &map->buckets[slot];
        if (bucket->key == key) {
            const uint32_t x = (uint32_t)(gx % OPENRIDE_ORMAP_MASK_GRID);
            const uint32_t y = (uint32_t)(gy % OPENRIDE_ORMAP_MASK_GRID);
            return bit_get(bucket->layers + layer * ORMAP_MASK_LAYER_BYTES,
                           y * OPENRIDE_ORMAP_MASK_GRID + x);
        }
        slot = (slot + 1U) & (map->capacity - 1U);
    }
    return false;
}

static bool rasterize_area_scanline(MaskTileMap *tiles,
                                    unsigned layer,
                                    const double *latitudes,
                                    const double *longitudes,
                                    uint32_t point_count)
{
    if (!tiles || !latitudes || !longitudes || point_count < 4U) return true;
    double *xs = malloc((size_t)point_count * sizeof(*xs));
    double *ys = malloc((size_t)point_count * sizeof(*ys));
    double *intersections = malloc((size_t)point_count * sizeof(*intersections));
    if (!xs || !ys || !intersections) {
        free(xs); free(ys); free(intersections);
        return false;
    }

    const double cells = (double)(1U << OPENRIDE_ORMAP_MASK_ZOOM)
        * OPENRIDE_ORMAP_MASK_GRID;
    double min_y = HUGE_VAL;
    double max_y = -HUGE_VAL;
    for (uint32_t i = 0U; i < point_count; ++i) {
        xs[i] = mercator_x(longitudes[i]) * cells;
        ys[i] = mercator_y(latitudes[i]) * cells;
        if (ys[i] < min_y) min_y = ys[i];
        if (ys[i] > max_y) max_y = ys[i];
    }

    int64_t row0 = (int64_t)floor(min_y);
    int64_t row1 = (int64_t)floor(max_y);
    const int64_t max_cell = (int64_t)(1U << OPENRIDE_ORMAP_MASK_ZOOM)
        * OPENRIDE_ORMAP_MASK_GRID - 1;
    if (row0 < 0) row0 = 0;
    if (row1 > max_cell) row1 = max_cell;

    bool ok = true;
    for (int64_t gy = row0; gy <= row1 && ok; ++gy) {
        const double scan_y = (double)gy + 0.5;
        uint32_t count = 0U;
        for (uint32_t i = 0U, j = point_count - 1U; i < point_count; j = i++) {
            const double y0 = ys[j];
            const double y1 = ys[i];
            if ((y0 > scan_y) == (y1 > scan_y)) continue;
            const double x = xs[j] + (scan_y - y0) * (xs[i] - xs[j]) / (y1 - y0);
            intersections[count++] = x;
        }
        if (count < 2U) continue;
        qsort(intersections, count, sizeof(*intersections), compare_double);
        for (uint32_t i = 0U; i + 1U < count; i += 2U) {
            double left = intersections[i];
            double right = intersections[i + 1U];
            if (right < left) { const double tmp = left; left = right; right = tmp; }
            int64_t gx0 = (int64_t)ceil(left - 0.5);
            int64_t gx1 = (int64_t)floor(right - 0.5);
            if (gx0 < 0) gx0 = 0;
            if (gx1 > max_cell) gx1 = max_cell;
            for (int64_t gx = gx0; gx <= gx1; ++gx) {
                if (!mask_set_global(tiles, layer, gx, gy)) { ok = false; break; }
            }
        }
    }

    free(xs);
    free(ys);
    free(intersections);
    return ok;
}

static uint8_t waterway_kind_from_feature(OpenRideOSMMapFeatureKind kind)
{
    switch (kind) {
        case OPENRIDE_OSM_MAP_FEATURE_WATERWAY_RIVER:
            return OPENRIDE_ORMAP_WATERWAY_RIVER;
        case OPENRIDE_OSM_MAP_FEATURE_WATERWAY_CANAL:
            return OPENRIDE_ORMAP_WATERWAY_CANAL;
        case OPENRIDE_OSM_MAP_FEATURE_WATERWAY_STREAM:
            return OPENRIDE_ORMAP_WATERWAY_STREAM;
        case OPENRIDE_OSM_MAP_FEATURE_WATERWAY_DRAIN:
            return OPENRIDE_ORMAP_WATERWAY_DRAIN;
        default:
            return 0U;
    }
}

static bool collect_waterway(MapFeatureContext *context,
                             OpenRideOSMMapFeatureKind kind,
                             const double *latitudes,
                             const double *longitudes,
                             uint32_t point_count)
{
    const uint8_t water_kind = waterway_kind_from_feature(kind);
    if (!context || !water_kind || point_count < 2U) return true;
    const int zoom = OPENRIDE_ORMAP_WATER_ZOOM;
    const int n = 1 << zoom;
    ++context->stats->waterway_features;

    for (uint32_t i = 1U; i < point_count; ++i) {
        const double x0 = mercator_x(longitudes[i - 1U]) * n;
        const double y0 = mercator_y(latitudes[i - 1U]) * n;
        const double x1 = mercator_x(longitudes[i]) * n;
        const double y1 = mercator_y(latitudes[i]) * n;
        int min_x = (int)floor(fmin(x0, x1));
        int max_x = (int)floor(fmax(x0, x1));
        int min_y = (int)floor(fmin(y0, y1));
        int max_y = (int)floor(fmax(y0, y1));
        if (min_x < 0) min_x = 0;
        if (min_y < 0) min_y = 0;
        if (max_x >= n) max_x = n - 1;
        if (max_y >= n) max_y = n - 1;
        if (max_x - min_x > 64 || max_y - min_y > 64) continue;
        for (int ty = min_y; ty <= max_y; ++ty) {
            for (int tx = min_x; tx <= max_x; ++tx) {
                double cx0 = 0.0, cy0 = 0.0, cx1 = 0.0, cy1 = 0.0;
                if (!clip_line_to_tile(x0, y0, x1, y1, tx, ty,
                                       &cx0, &cy0, &cx1, &cy1)) continue;
                WaterTileBucket *bucket = water_map_get(&context->waterways,
                                                        zoom,
                                                        tx,
                                                        ty,
                                                        true);
                if (!bucket) return false;
                OpenRideORMapWaterRecord record = {
                    .x1 = quantize_tile_coord(cx0, tx),
                    .y1 = quantize_tile_coord(cy0, ty),
                    .x2 = quantize_tile_coord(cx1, tx),
                    .y2 = quantize_tile_coord(cy1, ty),
                    .kind = water_kind,
                    .reserved = 0U
                };
                if (!water_bucket_push(bucket, record)) return false;
                ++context->stats->water_records_written;
            }
        }
    }
    return true;
}

static bool collect_map_feature(OpenRideOSMMapFeatureKind kind,
                                const double *latitudes,
                                const double *longitudes,
                                uint32_t point_count,
                                void *userdata)
{
    MapFeatureContext *context = userdata;
    if (!context || point_count == 0U) return true;
    ++context->stats->map_features_seen;

    if (kind >= OPENRIDE_OSM_MAP_FEATURE_WATERWAY_RIVER) {
        return collect_waterway(context, kind, latitudes, longitudes, point_count);
    }

    if (kind == OPENRIDE_OSM_MAP_FEATURE_WATER_AREA) {
        ++context->stats->water_polygons;
        return collect_area_ring(latitudes,
                                 longitudes,
                                 point_count,
                                 OPENRIDE_ORMAP_AREA_COARSE_ZOOM,
                                 OPENRIDE_ORMAP_AREA_WATER,
                                 1.25,
                                 4.0,
                                 &context->coarse_areas,
                                 context->stats)
            && collect_area_ring(latitudes,
                                 longitudes,
                                 point_count,
                                 OPENRIDE_ORMAP_AREA_DETAIL_ZOOM,
                                 OPENRIDE_ORMAP_AREA_WATER,
                                 0.35,
                                 0.20,
                                 &context->detail_areas,
                                 context->stats);
    }

    unsigned layer = 0U;
    if (kind == OPENRIDE_OSM_MAP_FEATURE_BUILTUP_AREA) {
        ++context->stats->builtup_polygons;
        layer = 0U;
    } else if (kind == OPENRIDE_OSM_MAP_FEATURE_FOREST_AREA) {
        ++context->stats->forest_polygons;
        layer = 2U;
    } else {
        return true;
    }

    /* Individual buildings arrive as a single representative point. */
    if (kind == OPENRIDE_OSM_MAP_FEATURE_BUILTUP_AREA && point_count == 1U) {
        const double cells = (double)(1U << OPENRIDE_ORMAP_MASK_ZOOM)
            * OPENRIDE_ORMAP_MASK_GRID;
        const int64_t gx = (int64_t)floor(mercator_x(longitudes[0]) * cells);
        const int64_t gy = (int64_t)floor(mercator_y(latitudes[0]) * cells);
        return mask_set_global(&context->masks, 0U, gx, gy);
    }

    return rasterize_area_scanline(&context->masks,
                                   layer,
                                   latitudes,
                                   longitudes,
                                   point_count);
}

static bool merge_builtup_cells(MaskTileMap *map)
{
    if (!map || map->capacity == 0U) return true;
    MaskTileMap dilated = {0};

    /* Dilation in global cell coordinates so tile boundaries are invisible. */
    for (uint32_t b = 0U; b < map->capacity; ++b) {
        const MaskTileBucket *bucket = &map->buckets[b];
        if (!bucket->used) continue;
        const int tx = (int)((bucket->key >> 29U) & 0x1fffffffU);
        const int ty = (int)(bucket->key & 0x1fffffffU);
        const unsigned char *bits = bucket->layers;
        for (uint32_t y = 0U; y < OPENRIDE_ORMAP_MASK_GRID; ++y) {
            for (uint32_t x = 0U; x < OPENRIDE_ORMAP_MASK_GRID; ++x) {
                if (!bit_get(bits, y * OPENRIDE_ORMAP_MASK_GRID + x)) continue;
                const int64_t gx = (int64_t)tx * OPENRIDE_ORMAP_MASK_GRID + x;
                const int64_t gy = (int64_t)ty * OPENRIDE_ORMAP_MASK_GRID + y;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!mask_set_global(&dilated, 0U, gx + dx, gy + dy)) {
                            mask_map_destroy(&dilated);
                            return false;
                        }
                    }
                }
            }
        }
    }

    /* Preserve water/forest but rebuild the built-up layer from the closing. */
    for (uint32_t b = 0U; b < map->capacity; ++b) {
        if (map->buckets[b].used) memset(map->buckets[b].layers, 0, ORMAP_MASK_LAYER_BYTES);
    }
    for (uint32_t b = 0U; b < dilated.capacity; ++b) {
        const MaskTileBucket *bucket = &dilated.buckets[b];
        if (!bucket->used) continue;
        const int tx = (int)((bucket->key >> 29U) & 0x1fffffffU);
        const int ty = (int)(bucket->key & 0x1fffffffU);
        const unsigned char *bits = bucket->layers;
        for (uint32_t y = 0U; y < OPENRIDE_ORMAP_MASK_GRID; ++y) {
            for (uint32_t x = 0U; x < OPENRIDE_ORMAP_MASK_GRID; ++x) {
                if (!bit_get(bits, y * OPENRIDE_ORMAP_MASK_GRID + x)) continue;
                const int64_t gx = (int64_t)tx * OPENRIDE_ORMAP_MASK_GRID + x;
                const int64_t gy = (int64_t)ty * OPENRIDE_ORMAP_MASK_GRID + y;
                bool keep = true;
                for (int dy = -1; dy <= 1 && keep; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!mask_get_global(&dilated, 0U, gx + dx, gy + dy)) {
                            keep = false;
                            break;
                        }
                    }
                }
                if (keep && !mask_set_global(map, 0U, gx, gy)) {
                    mask_map_destroy(&dilated);
                    return false;
                }
            }
        }
    }
    mask_map_destroy(&dilated);
    return true;
}

static bool filter_sparse_builtup_cells(MaskTileMap *map)
{
    if (!map || map->capacity == 0U) return true;
    MaskTileMap filtered = {0};
    for (uint32_t b = 0U; b < map->capacity; ++b) {
        const MaskTileBucket *bucket = &map->buckets[b];
        if (!bucket->used) continue;
        const int tx = (int)((bucket->key >> 29U) & 0x1fffffffU);
        const int ty = (int)(bucket->key & 0x1fffffffU);
        const unsigned char *bits = bucket->layers;
        for (uint32_t y = 0U; y < OPENRIDE_ORMAP_MASK_GRID; ++y) {
            for (uint32_t x = 0U; x < OPENRIDE_ORMAP_MASK_GRID; ++x) {
                if (!bit_get(bits, y * OPENRIDE_ORMAP_MASK_GRID + x)) continue;
                const int64_t gx = (int64_t)tx * OPENRIDE_ORMAP_MASK_GRID + x;
                const int64_t gy = (int64_t)ty * OPENRIDE_ORMAP_MASK_GRID + y;
                unsigned neighbours = 0U;
                for (int dy = -1; dy <= 1 && neighbours < 3U; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (mask_get_global(map, 0U, gx + dx, gy + dy)) ++neighbours;
                        if (neighbours >= 3U) break;
                    }
                }
                if (neighbours >= 3U
                    && !mask_set_global(&filtered, 0U, gx, gy)) {
                    mask_map_destroy(&filtered);
                    return false;
                }
            }
        }
    }

    for (uint32_t b = 0U; b < map->capacity; ++b) {
        if (map->buckets[b].used) memset(map->buckets[b].layers, 0, ORMAP_MASK_LAYER_BYTES);
    }
    for (uint32_t b = 0U; b < filtered.capacity; ++b) {
        const MaskTileBucket *bucket = &filtered.buckets[b];
        if (!bucket->used) continue;
        const int tx = (int)((bucket->key >> 29U) & 0x1fffffffU);
        const int ty = (int)(bucket->key & 0x1fffffffU);
        const unsigned char *bits = bucket->layers;
        for (uint32_t y = 0U; y < OPENRIDE_ORMAP_MASK_GRID; ++y) {
            for (uint32_t x = 0U; x < OPENRIDE_ORMAP_MASK_GRID; ++x) {
                if (!bit_get(bits, y * OPENRIDE_ORMAP_MASK_GRID + x)) continue;
                const int64_t gx = (int64_t)tx * OPENRIDE_ORMAP_MASK_GRID + x;
                const int64_t gy = (int64_t)ty * OPENRIDE_ORMAP_MASK_GRID + y;
                if (!mask_set_global(map, 0U, gx, gy)) {
                    mask_map_destroy(&filtered);
                    return false;
                }
            }
        }
    }
    mask_map_destroy(&filtered);
    return true;
}

static bool boundary_edge_push(BoundaryEdgeVector *vector,
                               int64_t x1,
                               int64_t y1,
                               int64_t x2,
                               int64_t y2)
{
    if (!vector
        || x1 < INT32_MIN || x1 > INT32_MAX
        || y1 < INT32_MIN || y1 > INT32_MAX
        || x2 < INT32_MIN || x2 > INT32_MAX
        || y2 < INT32_MIN || y2 > INT32_MAX) {
        return false;
    }
    if (vector->count == vector->capacity) {
        uint32_t capacity = vector->capacity == 0U ? 4096U : vector->capacity * 2U;
        if (capacity < vector->capacity) return false;
        BoundaryEdge *items = realloc(vector->items,
                                      (size_t)capacity * sizeof(*items));
        if (!items) return false;
        vector->items = items;
        vector->capacity = capacity;
    }
    vector->items[vector->count++] = (BoundaryEdge){
        .x1 = (int32_t)x1,
        .y1 = (int32_t)y1,
        .x2 = (int32_t)x2,
        .y2 = (int32_t)y2,
        .used = 0U
    };
    return true;
}

static int compare_boundary_edge(const void *left_ptr, const void *right_ptr)
{
    const BoundaryEdge *left = left_ptr;
    const BoundaryEdge *right = right_ptr;
    if (left->x1 != right->x1) return left->x1 < right->x1 ? -1 : 1;
    if (left->y1 != right->y1) return left->y1 < right->y1 ? -1 : 1;
    if (left->x2 != right->x2) return left->x2 < right->x2 ? -1 : 1;
    if (left->y2 != right->y2) return left->y2 < right->y2 ? -1 : 1;
    return 0;
}

static uint32_t boundary_lower_bound(const BoundaryEdgeVector *vector,
                                     int32_t x,
                                     int32_t y)
{
    uint32_t low = 0U;
    uint32_t high = vector ? vector->count : 0U;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2U;
        const BoundaryEdge *edge = &vector->items[middle];
        if (edge->x1 < x || (edge->x1 == x && edge->y1 < y)) low = middle + 1U;
        else high = middle;
    }
    return low;
}

static int boundary_direction(const BoundaryEdge *edge)
{
    const int dx = edge->x2 - edge->x1;
    const int dy = edge->y2 - edge->y1;
    if (dx > 0) return 0;  /* east */
    if (dy > 0) return 1;  /* south */
    if (dx < 0) return 2;  /* west */
    return 3;              /* north */
}

static int boundary_turn_rank(int incoming, int outgoing)
{
    const int delta = (outgoing - incoming + 4) & 3;
    if (delta == 1) return 0; /* keep filled cells on the right */
    if (delta == 0) return 1;
    if (delta == 3) return 2;
    return 3;
}

static int32_t find_next_boundary_edge(BoundaryEdgeVector *vector,
                                       int32_t x,
                                       int32_t y,
                                       int incoming_direction)
{
    const uint32_t begin = boundary_lower_bound(vector, x, y);
    int32_t selected = -1;
    int selected_rank = 99;
    for (uint32_t i = begin; i < vector->count; ++i) {
        BoundaryEdge *edge = &vector->items[i];
        if (edge->x1 != x || edge->y1 != y) break;
        if (edge->used) continue;
        const int rank = boundary_turn_rank(incoming_direction,
                                            boundary_direction(edge));
        if (rank < selected_rank) {
            selected = (int32_t)i;
            selected_rank = rank;
        }
    }
    return selected;
}

static bool build_builtup_boundary_edges(const MaskTileMap *map,
                                         BoundaryEdgeVector *edges)
{
    if (!map || !edges) return false;
    for (uint32_t b = 0U; b < map->capacity; ++b) {
        const MaskTileBucket *bucket = &map->buckets[b];
        if (!bucket->used) continue;
        const int tx = (int)((bucket->key >> 29U) & 0x1fffffffU);
        const int ty = (int)(bucket->key & 0x1fffffffU);
        const unsigned char *bits = bucket->layers;
        for (uint32_t y = 0U; y < OPENRIDE_ORMAP_MASK_GRID; ++y) {
            for (uint32_t x = 0U; x < OPENRIDE_ORMAP_MASK_GRID; ++x) {
                if (!bit_get(bits, y * OPENRIDE_ORMAP_MASK_GRID + x)) continue;
                const int64_t gx = (int64_t)tx * OPENRIDE_ORMAP_MASK_GRID + x;
                const int64_t gy = (int64_t)ty * OPENRIDE_ORMAP_MASK_GRID + y;
                if (!mask_get_global(map, 0U, gx, gy - 1)
                    && !boundary_edge_push(edges, gx, gy, gx + 1, gy)) return false;
                if (!mask_get_global(map, 0U, gx + 1, gy)
                    && !boundary_edge_push(edges, gx + 1, gy, gx + 1, gy + 1)) return false;
                if (!mask_get_global(map, 0U, gx, gy + 1)
                    && !boundary_edge_push(edges, gx + 1, gy + 1, gx, gy + 1)) return false;
                if (!mask_get_global(map, 0U, gx - 1, gy)
                    && !boundary_edge_push(edges, gx, gy + 1, gx, gy)) return false;
            }
        }
    }
    if (edges->count > 1U) {
        qsort(edges->items, edges->count, sizeof(*edges->items), compare_boundary_edge);
    }
    return true;
}

static bool ring_point_push(ORMapPoint **points,
                            uint32_t *count,
                            uint32_t *capacity,
                            double x,
                            double y)
{
    if (*count == *capacity) {
        uint32_t next = *capacity == 0U ? 128U : *capacity * 2U;
        if (next < *capacity) return false;
        ORMapPoint *grown = realloc(*points, (size_t)next * sizeof(*grown));
        if (!grown) return false;
        *points = grown;
        *capacity = next;
    }
    (*points)[(*count)++] = (ORMapPoint){x, y};
    return true;
}

static bool vectorize_builtup_mask(MaskTileMap *map,
                                   AreaTileMap *areas,
                                   OpenRideORMapBuildStats *stats)
{
    if (!map || !areas || !stats || map->capacity == 0U) return true;
    BoundaryEdgeVector edges = {0};
    if (!build_builtup_boundary_edges(map, &edges)) {
        free(edges.items);
        return false;
    }

    const double global_cells = (double)(1U << OPENRIDE_ORMAP_MASK_ZOOM)
        * OPENRIDE_ORMAP_MASK_GRID;
    const double detail_scale = (double)(1U << OPENRIDE_ORMAP_AREA_DETAIL_ZOOM)
        / global_cells;
    const double simplify_tolerance = detail_scale * 0.65;
    bool ok = true;

    for (uint32_t start_index = 0U; start_index < edges.count && ok; ++start_index) {
        BoundaryEdge *start = &edges.items[start_index];
        if (start->used) continue;

        ORMapPoint *ring = NULL;
        uint32_t ring_count = 0U;
        uint32_t ring_capacity = 0U;
        const int32_t start_x = start->x1;
        const int32_t start_y = start->y1;
        int32_t current_index = (int32_t)start_index;
        uint64_t guard = (uint64_t)edges.count + 1U;
        bool closed = false;

        if (!ring_point_push(&ring,
                             &ring_count,
                             &ring_capacity,
                             start_x * detail_scale,
                             start_y * detail_scale)) {
            free(ring);
            ok = false;
            break;
        }

        while (current_index >= 0 && guard-- > 0U) {
            BoundaryEdge *edge = &edges.items[(uint32_t)current_index];
            if (edge->used) break;
            edge->used = 1U;
            if (!ring_point_push(&ring,
                                 &ring_count,
                                 &ring_capacity,
                                 edge->x2 * detail_scale,
                                 edge->y2 * detail_scale)) {
                ok = false;
                break;
            }
            if (edge->x2 == start_x && edge->y2 == start_y) {
                closed = true;
                break;
            }
            current_index = find_next_boundary_edge(&edges,
                                                     edge->x2,
                                                     edge->y2,
                                                     boundary_direction(edge));
        }

        if (ok && closed && ring_count >= 4U) {
            const uint32_t unique_count = ring_count - 1U;
            const double signed_area = polygon_signed_area(ring, unique_count);
            const double grid_area = fabs(signed_area) / (detail_scale * detail_scale);
            /* Ignore hole rings and tiny isolated building samples. */
            if (signed_area > 0.0 && grid_area >= 4.0) {
                ORMapPoint *smoothed = NULL;
                uint32_t smoothed_count = 0U;
                ORMapPoint *simplified = NULL;
                uint32_t simplified_count = 0U;
                ok = smooth_closed_ring(ring,
                                        ring_count,
                                        &smoothed,
                                        &smoothed_count);
                if (ok && smoothed && smoothed_count >= 3U) {
                    ok = simplify_closed_ring(smoothed,
                                              smoothed_count,
                                              simplify_tolerance,
                                              &simplified,
                                              &simplified_count);
                }
                if (ok && simplified && simplified_count >= 3U) {
                    ++stats->builtup_contours;
                    ok = triangulate_ring_to_area_map(simplified,
                                                      simplified_count,
                                                      OPENRIDE_ORMAP_AREA_DETAIL_ZOOM,
                                                      OPENRIDE_ORMAP_AREA_BUILTUP,
                                                      areas,
                                                      stats);
                }
                free(smoothed);
                free(simplified);
            }
        }
        free(ring);
    }

    free(edges.items);
    if (!ok) return false;

    /* The built-up bitmap was only an intermediate representation in v3. */
    for (uint32_t b = 0U; b < map->capacity; ++b) {
        if (map->buckets[b].used) {
            memset(map->buckets[b].layers, 0, ORMAP_MASK_LAYER_BYTES);
        }
    }
    return true;
}

static bool make_compressed_blob(const unsigned char *raw,
                                 size_t raw_size,
                                 unsigned char **blob,
                                 size_t *blob_size)
{
    uLongf capacity = compressBound((uLong)raw_size);
    unsigned char *output = malloc((size_t)capacity + 4U);
    if (!output) return false;
    write_u32_le(output, (uint32_t)raw_size);
    const int rc = compress2(output + 4U,
                             &capacity,
                             raw,
                             (uLong)raw_size,
                             Z_BEST_SPEED);
    if (rc != Z_OK) { free(output); return false; }
    *blob = output;
    *blob_size = (size_t)capacity + 4U;
    return true;
}

static void decode_tile_key(uint64_t key, int *zoom, int *x, int *y)
{
    *zoom = (int)((key >> 58U) & 0x3fU);
    *x = (int)((key >> 29U) & 0x1fffffffU);
    *y = (int)(key & 0x1fffffffU);
}

static bool sqlite_exec(sqlite3 *db, const char *sql, char *error, size_t error_size)
{
    char *message = NULL;
    const int rc = sqlite3_exec(db, sql, NULL, NULL, &message);
    if (rc == SQLITE_OK) return true;
    set_error(error, error_size, message ? message : sqlite3_errmsg(db));
    sqlite3_free(message);
    return false;
}

static bool write_road_tiles(sqlite3 *db,
                             const RoadTileMap *tiles,
                             OpenRideORMapBuildStats *stats,
                             char *error,
                             size_t error_size)
{
    sqlite3_stmt *insert = NULL;
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO road_tiles(zoom_level,tile_column,tile_row,tile_data) "
                           "VALUES(?1,?2,?3,?4)",
                           -1,
                           &insert,
                           NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(db));
        return false;
    }
    bool ok = true;
    for (uint32_t i = 0U; i < tiles->capacity && ok; ++i) {
        const RoadTileBucket *bucket = &tiles->buckets[i];
        if (!bucket->used || bucket->count == 0U) continue;
        const size_t raw_size = 12U + (size_t)bucket->count * ORMAP_ROAD_RECORD_SIZE;
        unsigned char *raw = malloc(raw_size);
        if (!raw) { ok = false; set_error(error, error_size, "out of memory writing road tile"); break; }
        memcpy(raw, "ORR1", 4U);
        write_u16_le(raw + 4U, 1U);
        write_u16_le(raw + 6U, ORMAP_ROAD_RECORD_SIZE);
        write_u32_le(raw + 8U, bucket->count);
        unsigned char *p = raw + 12U;
        for (uint32_t r = 0U; r < bucket->count; ++r, p += ORMAP_ROAD_RECORD_SIZE) {
            const OpenRideORMapRoadRecord *record = &bucket->records[r];
            write_u16_le(p + 0U, record->x1);
            write_u16_le(p + 2U, record->y1);
            write_u16_le(p + 4U, record->x2);
            write_u16_le(p + 6U, record->y2);
            p[8] = record->road_class;
            p[9] = record->surface;
            write_u16_le(p + 10U, record->flags);
        }
        unsigned char *blob = NULL;
        size_t blob_size = 0U;
        if (!make_compressed_blob(raw, raw_size, &blob, &blob_size)) {
            free(raw); ok = false; set_error(error, error_size, "unable to compress road tile"); break;
        }
        free(raw);
        int zoom = 0, x = 0, y = 0;
        decode_tile_key(bucket->key, &zoom, &x, &y);
        sqlite3_reset(insert);
        sqlite3_bind_int(insert, 1, zoom);
        sqlite3_bind_int(insert, 2, x);
        sqlite3_bind_int(insert, 3, y);
        sqlite3_bind_blob(insert, 4, blob, (int)blob_size, SQLITE_TRANSIENT);
        if (sqlite3_step(insert) != SQLITE_DONE) {
            set_error(error, error_size, sqlite3_errmsg(db));
            ok = false;
        } else {
            ++stats->road_tiles_written;
        }
        free(blob);
    }
    sqlite3_finalize(insert);
    return ok;
}

static bool write_water_tiles(sqlite3 *db,
                              const WaterTileMap *tiles,
                              OpenRideORMapBuildStats *stats,
                              char *error,
                              size_t error_size)
{
    sqlite3_stmt *insert = NULL;
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO water_tiles(zoom_level,tile_column,tile_row,tile_data) "
                           "VALUES(?1,?2,?3,?4)",
                           -1,
                           &insert,
                           NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(db));
        return false;
    }
    bool ok = true;
    for (uint32_t i = 0U; i < tiles->capacity && ok; ++i) {
        const WaterTileBucket *bucket = &tiles->buckets[i];
        if (!bucket->used || bucket->count == 0U) continue;
        const size_t raw_size = 12U + (size_t)bucket->count * ORMAP_WATER_RECORD_SIZE;
        unsigned char *raw = malloc(raw_size);
        if (!raw) {
            ok = false;
            set_error(error, error_size, "out of memory writing water tile");
            break;
        }
        memcpy(raw, "ORW1", 4U);
        write_u16_le(raw + 4U, 1U);
        write_u16_le(raw + 6U, ORMAP_WATER_RECORD_SIZE);
        write_u32_le(raw + 8U, bucket->count);
        unsigned char *q = raw + 12U;
        for (uint32_t r = 0U; r < bucket->count; ++r, q += ORMAP_WATER_RECORD_SIZE) {
            const OpenRideORMapWaterRecord *record = &bucket->records[r];
            write_u16_le(q + 0U, record->x1);
            write_u16_le(q + 2U, record->y1);
            write_u16_le(q + 4U, record->x2);
            write_u16_le(q + 6U, record->y2);
            q[8] = record->kind;
            q[9] = 0U;
        }
        unsigned char *blob = NULL;
        size_t blob_size = 0U;
        if (!make_compressed_blob(raw, raw_size, &blob, &blob_size)) {
            free(raw);
            ok = false;
            set_error(error, error_size, "unable to compress water tile");
            break;
        }
        free(raw);
        int zoom = 0, x = 0, y = 0;
        decode_tile_key(bucket->key, &zoom, &x, &y);
        sqlite3_reset(insert);
        sqlite3_bind_int(insert, 1, zoom);
        sqlite3_bind_int(insert, 2, x);
        sqlite3_bind_int(insert, 3, y);
        sqlite3_bind_blob(insert, 4, blob, (int)blob_size, SQLITE_TRANSIENT);
        if (sqlite3_step(insert) != SQLITE_DONE) {
            set_error(error, error_size, sqlite3_errmsg(db));
            ok = false;
        } else {
            ++stats->water_tiles_written;
        }
        free(blob);
    }
    sqlite3_finalize(insert);
    return ok;
}

static bool write_area_tiles(sqlite3 *db,
                             const AreaTileMap *tiles,
                             OpenRideORMapBuildStats *stats,
                             char *error,
                             size_t error_size)
{
    sqlite3_stmt *insert = NULL;
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO area_tiles(zoom_level,tile_column,tile_row,tile_data) "
                           "VALUES(?1,?2,?3,?4)",
                           -1,
                           &insert,
                           NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(db));
        return false;
    }
    bool ok = true;
    for (uint32_t i = 0U; i < tiles->capacity && ok; ++i) {
        const AreaTileBucket *bucket = &tiles->buckets[i];
        if (!bucket->used || bucket->count == 0U) continue;
        const size_t raw_size = 12U + (size_t)bucket->count * ORMAP_AREA_RECORD_SIZE;
        unsigned char *raw = malloc(raw_size);
        if (!raw) {
            ok = false;
            set_error(error, error_size, "out of memory writing area tile");
            break;
        }
        memcpy(raw, "ORA1", 4U);
        write_u16_le(raw + 4U, 1U);
        write_u16_le(raw + 6U, ORMAP_AREA_RECORD_SIZE);
        write_u32_le(raw + 8U, bucket->count);
        unsigned char *q = raw + 12U;
        for (uint32_t r = 0U; r < bucket->count; ++r, q += ORMAP_AREA_RECORD_SIZE) {
            const OpenRideORMapAreaTriangle *triangle = &bucket->triangles[r];
            write_u16_le(q + 0U, triangle->x1);
            write_u16_le(q + 2U, triangle->y1);
            write_u16_le(q + 4U, triangle->x2);
            write_u16_le(q + 6U, triangle->y2);
            write_u16_le(q + 8U, triangle->x3);
            write_u16_le(q + 10U, triangle->y3);
            q[12] = triangle->kind;
            q[13] = 0U;
        }
        unsigned char *blob = NULL;
        size_t blob_size = 0U;
        if (!make_compressed_blob(raw, raw_size, &blob, &blob_size)) {
            free(raw);
            ok = false;
            set_error(error, error_size, "unable to compress area tile");
            break;
        }
        free(raw);
        int zoom = 0, x = 0, y = 0;
        decode_tile_key(bucket->key, &zoom, &x, &y);
        sqlite3_reset(insert);
        sqlite3_bind_int(insert, 1, zoom);
        sqlite3_bind_int(insert, 2, x);
        sqlite3_bind_int(insert, 3, y);
        sqlite3_bind_blob(insert, 4, blob, (int)blob_size, SQLITE_TRANSIENT);
        if (sqlite3_step(insert) != SQLITE_DONE) {
            set_error(error, error_size, sqlite3_errmsg(db));
            ok = false;
        } else {
            ++stats->area_tiles_written;
        }
        free(blob);
    }
    sqlite3_finalize(insert);
    return ok;
}

static bool write_mask_tiles(sqlite3 *db,
                             const MaskTileMap *tiles,
                             OpenRideORMapBuildStats *stats,
                             char *error,
                             size_t error_size)
{
    sqlite3_stmt *insert = NULL;
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO mask_tiles(zoom_level,tile_column,tile_row,tile_data) "
                           "VALUES(?1,?2,?3,?4)",
                           -1,
                           &insert,
                           NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(db));
        return false;
    }
    bool ok = true;
    for (uint32_t i = 0U; i < tiles->capacity && ok; ++i) {
        const MaskTileBucket *bucket = &tiles->buckets[i];
        if (!bucket->used) continue;
        bool has_data = false;
        for (size_t b = 0U; b < ORMAP_MASK_LAYER_BYTES * 3U; ++b) {
            if (bucket->layers[b] != 0U) {
                has_data = true;
                break;
            }
        }
        if (!has_data) continue;
        const size_t raw_size = 12U + ORMAP_MASK_LAYER_BYTES * 3U;
        unsigned char raw[12U + ORMAP_MASK_LAYER_BYTES * 3U];
        memcpy(raw, "ORM1", 4U);
        write_u16_le(raw + 4U, 1U);
        raw[6] = OPENRIDE_ORMAP_MASK_GRID;
        raw[7] = 3U;
        write_u32_le(raw + 8U, ORMAP_MASK_LAYER_BYTES);
        memcpy(raw + 12U, bucket->layers, ORMAP_MASK_LAYER_BYTES * 3U);
        unsigned char *blob = NULL;
        size_t blob_size = 0U;
        if (!make_compressed_blob(raw, raw_size, &blob, &blob_size)) {
            ok = false; set_error(error, error_size, "unable to compress mask tile"); break;
        }
        int zoom = 0, x = 0, y = 0;
        decode_tile_key(bucket->key, &zoom, &x, &y);
        sqlite3_reset(insert);
        sqlite3_bind_int(insert, 1, zoom);
        sqlite3_bind_int(insert, 2, x);
        sqlite3_bind_int(insert, 3, y);
        sqlite3_bind_blob(insert, 4, blob, (int)blob_size, SQLITE_TRANSIENT);
        if (sqlite3_step(insert) != SQLITE_DONE) {
            set_error(error, error_size, sqlite3_errmsg(db));
            ok = false;
        } else {
            ++stats->mask_tiles_written;
        }
        free(blob);
    }
    sqlite3_finalize(insert);
    return ok;
}

static uint8_t label_lod_from_kind(int kind)
{
    switch (kind) {
        case 1: /* city */
            return OPENRIDE_ORMAP_LABEL_LOD_REGIONAL;
        case 2: /* town */
            return OPENRIDE_ORMAP_LABEL_LOD_OVERVIEW;
        case 3: /* village */
        case 5: /* suburb */
            return OPENRIDE_ORMAP_LABEL_LOD_LOCAL;
        case 4: /* hamlet */
        case 6: /* quarter */
        default:
            return OPENRIDE_ORMAP_LABEL_LOD_DETAIL;
    }
}

static void count_label_lod(OpenRideORMapBuildStats *stats, uint8_t lod)
{
    if (!stats) return;
    if (lod == OPENRIDE_ORMAP_LABEL_LOD_REGIONAL) ++stats->label_regional_count;
    else if (lod == OPENRIDE_ORMAP_LABEL_LOD_OVERVIEW) ++stats->label_overview_count;
    else if (lod == OPENRIDE_ORMAP_LABEL_LOD_LOCAL) ++stats->label_local_count;
    else ++stats->label_detail_count;
}

static bool copy_labels(sqlite3 *db,
                        const char *places_path,
                        OpenRideORMapBuildStats *stats,
                        char *error,
                        size_t error_size)
{
    sqlite3 *places = NULL;
    if (sqlite3_open_v2(places_path,
                        &places,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX,
                        NULL) != SQLITE_OK) {
        set_error(error, error_size, places ? sqlite3_errmsg(places) : "unable to open places DB");
        if (places) sqlite3_close(places);
        return false;
    }
    sqlite3_stmt *select = NULL;
    sqlite3_stmt *insert = NULL;
    bool ok = sqlite3_prepare_v2(places,
                                 "SELECT lat_e7,lon_e7,kind,rank,name FROM places "
                                 "WHERE kind BETWEEN 1 AND 6 ORDER BY rank DESC",
                                 -1,
                                 &select,
                                 NULL) == SQLITE_OK
        && sqlite3_prepare_v2(db,
                             "INSERT INTO labels(lat_e7,lon_e7,kind,rank,lod,name) "
                             "VALUES(?1,?2,?3,?4,?5,?6)",
                             -1,
                             &insert,
                             NULL) == SQLITE_OK;
    if (!ok) set_error(error, error_size, "unable to prepare label copy");
    while (ok && sqlite3_step(select) == SQLITE_ROW) {
        const int kind = sqlite3_column_int(select, 2);
        const uint8_t lod = label_lod_from_kind(kind);
        sqlite3_reset(insert);
        sqlite3_bind_int(insert, 1, sqlite3_column_int(select, 0));
        sqlite3_bind_int(insert, 2, sqlite3_column_int(select, 1));
        sqlite3_bind_int(insert, 3, kind);
        sqlite3_bind_int(insert, 4, sqlite3_column_int(select, 3));
        sqlite3_bind_int(insert, 5, lod);
        sqlite3_bind_text(insert,
                          6,
                          (const char *)sqlite3_column_text(select, 4),
                          -1,
                          SQLITE_TRANSIENT);
        if (sqlite3_step(insert) != SQLITE_DONE) {
            ok = false;
            set_error(error, error_size, sqlite3_errmsg(db));
        } else {
            ++stats->labels_written;
            count_label_lod(stats, lod);
        }
    }
    if (select) sqlite3_finalize(select);
    if (insert) sqlite3_finalize(insert);
    sqlite3_close(places);
    return ok;
}

#define ORMAP_METADATA_BIN_DEG 0.05
#define ORMAP_METADATA_LON_BINS 7200U
#define ORMAP_METADATA_LAT_BINS 3600U
#define ORMAP_METADATA_TRIM_DIVISOR 10000U
#define ORMAP_METADATA_MARGIN_DEG 0.10

static uint32_t metadata_histogram_index(double value,
                                         double minimum,
                                         uint32_t bin_count)
{
    long index = (long)floor((value - minimum) / ORMAP_METADATA_BIN_DEG);
    if (index < 0L) return 0U;
    if ((uint64_t)index >= (uint64_t)bin_count) return bin_count - 1U;
    return (uint32_t)index;
}

static uint32_t metadata_lower_bin(const uint32_t *histogram,
                                   uint32_t bin_count,
                                   uint64_t trim_count)
{
    uint64_t accumulated = 0U;
    for (uint32_t i = 0U; i < bin_count; ++i) {
        accumulated += histogram[i];
        if (accumulated > trim_count) return i;
    }
    return bin_count - 1U;
}

static uint32_t metadata_upper_bin(const uint32_t *histogram,
                                   uint32_t bin_count,
                                   uint64_t trim_count)
{
    uint64_t accumulated = 0U;
    for (uint32_t i = bin_count; i > 0U; --i) {
        const uint32_t index = i - 1U;
        accumulated += histogram[index];
        if (accumulated > trim_count) return index;
    }
    return 0U;
}

static bool write_metadata(sqlite3 *db,
                           const char *region_name,
                           const OpenRideRoutingGraph *graph,
                           char *error,
                           size_t error_size)
{
    if (!graph || graph->node_count == 0U) {
        set_error(error, error_size, "routing graph has no nodes");
        return false;
    }

    uint32_t *lon_histogram = calloc(ORMAP_METADATA_LON_BINS,
                                     sizeof(*lon_histogram));
    uint32_t *lat_histogram = calloc(ORMAP_METADATA_LAT_BINS,
                                     sizeof(*lat_histogram));
    if (!lon_histogram || !lat_histogram) {
        free(lon_histogram);
        free(lat_histogram);
        set_error(error, error_size, "unable to allocate .ormap metadata histogram");
        return false;
    }

    double raw_west = 180.0, raw_east = -180.0;
    double raw_south = 90.0, raw_north = -90.0;
    uint64_t valid_count = 0U;
    for (uint32_t i = 0U; i < graph->node_count; ++i) {
        double lat = 0.0, lon = 0.0;
        openride_routing_node_geo(&graph->nodes[i], &lat, &lon);
        if (!isfinite(lat) || !isfinite(lon)
            || lat < -90.0 || lat > 90.0
            || lon < -180.0 || lon > 180.0) {
            continue;
        }
        if (lon < raw_west) raw_west = lon;
        if (lon > raw_east) raw_east = lon;
        if (lat < raw_south) raw_south = lat;
        if (lat > raw_north) raw_north = lat;
        ++lon_histogram[metadata_histogram_index(lon,
                                                 -180.0,
                                                 ORMAP_METADATA_LON_BINS)];
        ++lat_histogram[metadata_histogram_index(lat,
                                                 -90.0,
                                                 ORMAP_METADATA_LAT_BINS)];
        ++valid_count;
    }
    if (valid_count == 0U) {
        free(lon_histogram);
        free(lat_histogram);
        set_error(error, error_size, "routing graph has no valid geographic nodes");
        return false;
    }

    /*
     * A regional graph can contain a handful of OSM nodes far outside the
     * extract's useful footprint. Using absolute min/max made one such node
     * move the initial camera hundreds of kilometres away. For sufficiently
     * large graphs, trim only the outermost 0.01% of node coordinates, then
     * add a small margin. Tiny/test graphs keep their exact bounds.
     */
    const uint64_t trim_count = valid_count / ORMAP_METADATA_TRIM_DIVISOR;
    double west = raw_west;
    double east = raw_east;
    double south = raw_south;
    double north = raw_north;
    if (trim_count > 0U) {
        const uint32_t west_bin = metadata_lower_bin(lon_histogram,
                                                     ORMAP_METADATA_LON_BINS,
                                                     trim_count);
        const uint32_t east_bin = metadata_upper_bin(lon_histogram,
                                                     ORMAP_METADATA_LON_BINS,
                                                     trim_count);
        const uint32_t south_bin = metadata_lower_bin(lat_histogram,
                                                      ORMAP_METADATA_LAT_BINS,
                                                      trim_count);
        const uint32_t north_bin = metadata_upper_bin(lat_histogram,
                                                      ORMAP_METADATA_LAT_BINS,
                                                      trim_count);
        const double robust_west = -180.0
            + (double)west_bin * ORMAP_METADATA_BIN_DEG
            - ORMAP_METADATA_MARGIN_DEG;
        const double robust_east = -180.0
            + (double)(east_bin + 1U) * ORMAP_METADATA_BIN_DEG
            + ORMAP_METADATA_MARGIN_DEG;
        const double robust_south = -90.0
            + (double)south_bin * ORMAP_METADATA_BIN_DEG
            - ORMAP_METADATA_MARGIN_DEG;
        const double robust_north = -90.0
            + (double)(north_bin + 1U) * ORMAP_METADATA_BIN_DEG
            + ORMAP_METADATA_MARGIN_DEG;
        if (robust_west > west) west = robust_west;
        if (robust_east < east) east = robust_east;
        if (robust_south > south) south = robust_south;
        if (robust_north < north) north = robust_north;
    }
    free(lon_histogram);
    free(lat_histogram);

    char center[128], bounds[160], version[32], minzoom[16], maxzoom[16];
    char roadmaxzoom[16], maskzoom[16], waterzoom[16], areacoarsezoom[16], areadetailzoom[16];
    snprintf(center, sizeof(center), "%.8f,%.8f,12", (west + east) * 0.5, (south + north) * 0.5);
    snprintf(bounds, sizeof(bounds), "%.8f,%.8f,%.8f,%.8f", west, south, east, north);
    snprintf(version, sizeof(version), "%u", OPENRIDE_ORMAP_FORMAT_VERSION);
    snprintf(minzoom, sizeof(minzoom), "%d", OPENRIDE_ORMAP_MIN_ROAD_ZOOM);
    snprintf(maxzoom, sizeof(maxzoom), "%d", OPENRIDE_ORMAP_MAX_ZOOM);
    snprintf(roadmaxzoom, sizeof(roadmaxzoom), "%d", OPENRIDE_ORMAP_ROAD_DATA_MAX_ZOOM);
    snprintf(maskzoom, sizeof(maskzoom), "%d", OPENRIDE_ORMAP_MASK_ZOOM);
    snprintf(waterzoom, sizeof(waterzoom), "%d", OPENRIDE_ORMAP_WATER_ZOOM);
    snprintf(areacoarsezoom, sizeof(areacoarsezoom), "%d", OPENRIDE_ORMAP_AREA_COARSE_ZOOM);
    snprintf(areadetailzoom, sizeof(areadetailzoom), "%d", OPENRIDE_ORMAP_AREA_DETAIL_ZOOM);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO metadata(name,value) VALUES(?1,?2)",
                           -1,
                           &stmt,
                           NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(db));
        return false;
    }
    const char *pairs[][2] = {
        {"format", "OpenRide ORMap"},
        {"format_version", version},
        {"name", region_name ? region_name : "OpenRide region"},
        {"attribution", "OpenStreetMap contributors"},
        {"minzoom", minzoom},
        {"maxzoom", maxzoom},
        {"roadmaxzoom", roadmaxzoom},
        {"maskzoom", maskzoom},
        {"waterzoom", waterzoom},
        {"areacoarsezoom", areacoarsezoom},
        {"areadetailzoom", areadetailzoom},
        {"center", center},
        {"bounds", bounds}
    };
    bool ok = true;
    for (size_t i = 0U; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, pairs[i][0], -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pairs[i][1], -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) { ok = false; break; }
    }
    if (!ok) set_error(error, error_size, sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return ok;
}

bool openride_ormap_build(const char *pbf_path,
                          const char *routing_graph_path,
                          const char *places_database_path,
                          const char *output_path,
                          const char *region_name,
                          OpenRideORMapBuildStats *stats_out,
                          char *error,
                          size_t error_size)
{
    if (!pbf_path || !routing_graph_path || !places_database_path || !output_path) {
        set_error(error, error_size, "invalid .ormap build arguments");
        return false;
    }
    OpenRideORMapBuildStats stats = {0};
    OpenRideRoutingGraph graph = {0};
    if (!openride_routing_graph_load(&graph,
                                     routing_graph_path,
                                     error,
                                     error_size)) {
        return false;
    }

    /*
     * Mobile memory policy: never keep all zoom levels in RAM at once. A full
     * regional graph is already sizeable; road tiles are therefore generated
     * and flushed one zoom at a time. The graph is released before scanning
     * millions of OSM building/landuse references for semantic masks.
     */
    bool ok = true;
    remove(output_path);
    sqlite3 *db = NULL;
    if (ok && sqlite3_open_v2(output_path,
                              &db,
                              SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                              NULL) != SQLITE_OK) {
        set_error(error, error_size, db ? sqlite3_errmsg(db) : "unable to create .ormap");
        ok = false;
    }
    if (ok) {
        const char *schema =
            "PRAGMA journal_mode=OFF;"
            "PRAGMA synchronous=OFF;"
            "PRAGMA temp_store=MEMORY;"
            "CREATE TABLE metadata(name TEXT PRIMARY KEY,value TEXT NOT NULL);"
            "CREATE TABLE road_tiles(zoom_level INTEGER NOT NULL,tile_column INTEGER NOT NULL,tile_row INTEGER NOT NULL,tile_data BLOB NOT NULL,PRIMARY KEY(zoom_level,tile_column,tile_row));"
            "CREATE TABLE water_tiles(zoom_level INTEGER NOT NULL,tile_column INTEGER NOT NULL,tile_row INTEGER NOT NULL,tile_data BLOB NOT NULL,PRIMARY KEY(zoom_level,tile_column,tile_row));"
            "CREATE TABLE area_tiles(zoom_level INTEGER NOT NULL,tile_column INTEGER NOT NULL,tile_row INTEGER NOT NULL,tile_data BLOB NOT NULL,PRIMARY KEY(zoom_level,tile_column,tile_row));"
            "CREATE TABLE mask_tiles(zoom_level INTEGER NOT NULL,tile_column INTEGER NOT NULL,tile_row INTEGER NOT NULL,tile_data BLOB NOT NULL,PRIMARY KEY(zoom_level,tile_column,tile_row));"
            "CREATE TABLE labels(lat_e7 INTEGER NOT NULL,lon_e7 INTEGER NOT NULL,kind INTEGER NOT NULL,rank INTEGER NOT NULL,lod INTEGER NOT NULL,name TEXT NOT NULL);";
        ok = sqlite_exec(db, schema, error, error_size)
            && sqlite_exec(db, "BEGIN", error, error_size)
            && write_metadata(db, region_name, &graph, error, error_size);

        const int road_lods[] = {
            OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM,
            OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM,
            OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM,
            OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM
        };
        for (size_t road_lod = 0U;
             ok && road_lod < sizeof(road_lods) / sizeof(road_lods[0]);
             ++road_lod) {
            const int zoom = road_lods[road_lod];
            RoadTileMap road_tiles = {0};
            ok = collect_roads_at_zoom(&graph, zoom, &road_tiles, &stats, error, error_size)
                && simplify_road_tiles(&road_tiles, zoom, &stats, error, error_size)
                && write_road_tiles(db, &road_tiles, &stats, error, error_size);
            road_map_destroy(&road_tiles);
        }

        /* Free ~regional-graph memory before the PBF cartographic pass. */
        openride_routing_graph_destroy(&graph);

        MapFeatureContext features = {0};
        features.stats = &stats;
        OpenRideOSMMapFeatureStats feature_stats = {0};
        if (ok) {
            ok = openride_osm_pbf_visit_map_features(pbf_path,
                                                      collect_map_feature,
                                                      &features,
                                                      &feature_stats,
                                                      error,
                                                      error_size);
            if (ok) {
                stats.map_relations_seen = feature_stats.osm_relation_count;
                stats.multipolygon_relations = feature_stats.selected_relation_count;
                stats.multipolygon_outer_rings = feature_stats.multipolygon_outer_ring_count;
                stats.incomplete_multipolygons = feature_stats.incomplete_multipolygon_count;
                stats.multipolygon_inner_members_ignored =
                    feature_stats.multipolygon_inner_members_ignored;
            }
        }
        /* Coarse water is flushed before built-up contour extraction to keep
         * the Android peak-memory footprint bounded. */
        if (ok) ok = write_area_tiles(db,
                                      &features.coarse_areas,
                                      &stats,
                                      error,
                                      error_size);
        area_map_destroy(&features.coarse_areas);
        if (ok && !merge_builtup_cells(&features.masks)) {
            set_error(error, error_size, "out of memory merging built-up areas");
            ok = false;
        }
        if (ok && !filter_sparse_builtup_cells(&features.masks)) {
            set_error(error, error_size, "out of memory filtering built-up areas");
            ok = false;
        }
        if (ok && !vectorize_builtup_mask(&features.masks,
                                          &features.detail_areas,
                                          &stats)) {
            set_error(error, error_size, "unable to vectorize built-up mask");
            ok = false;
        }
        if (ok) ok = write_area_tiles(db,
                                      &features.detail_areas,
                                      &stats,
                                      error,
                                      error_size);
        if (ok) ok = write_mask_tiles(db, &features.masks, &stats, error, error_size);
        if (ok) ok = write_water_tiles(db, &features.waterways, &stats, error, error_size);
        mask_map_destroy(&features.masks);
        water_map_destroy(&features.waterways);
        area_map_destroy(&features.coarse_areas);
        area_map_destroy(&features.detail_areas);
        if (ok) ok = copy_labels(db, places_database_path, &stats, error, error_size);

        if (ok) ok = sqlite_exec(db, "COMMIT", error, error_size);
        else sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        if (ok) {
            /* The database is append-only during creation, so VACUUM would
             * only duplicate large amounts of data and temporary storage on a
             * phone. Keep finalisation lightweight. */
            ok = sqlite_exec(db,
                             "CREATE INDEX idx_labels_lod_rank ON labels(lod,rank DESC);",
                             error,
                             error_size);
        }
    }
    if (db) sqlite3_close(db);
    openride_routing_graph_destroy(&graph);
    if (!ok) remove(output_path);
    if (stats_out) *stats_out = stats;
    if (ok) set_error(error, error_size, "");
    return ok;
}
