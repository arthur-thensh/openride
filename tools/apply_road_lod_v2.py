from pathlib import Path

path = Path("src/map/ormap_builder.c")
text = path.read_text()

marker = "static double road_simplify_pixels_for_zoom(int zoom)"
if marker in text:
    raise SystemExit("Road LOD V2 is already present")

anchor = "static bool simplify_closed_ring(const ORMapPoint *input,\n"
if anchor not in text:
    raise SystemExit("road simplifier insertion anchor missing")

code = r'''typedef struct RoadEndpointRef {
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

'''

text = text.replace(anchor, code + anchor, 1)

old = '''            ok = collect_roads_at_zoom(&graph, zoom, &road_tiles, &stats, error, error_size)\n                && write_road_tiles(db, &road_tiles, &stats, error, error_size);'''
new = '''            ok = collect_roads_at_zoom(&graph, zoom, &road_tiles, &stats, error, error_size)\n                && simplify_road_tiles(&road_tiles, zoom, &stats, error, error_size)\n                && write_road_tiles(db, &road_tiles, &stats, error, error_size);'''
if old not in text:
    raise SystemExit("road build insertion anchor missing")
text = text.replace(old, new, 1)

path.write_text(text)
