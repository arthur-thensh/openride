#include "openride/ormap_tile_pyramid.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TEST_TILE_CAPACITY 1024U

typedef struct TestTile {
    bool used;
    OpenRideORMapPyramidTileKey key;
    OpenRideORMapPyramidTileState state;
} TestTile;

typedef struct TestStore {
    TestTile tiles[TEST_TILE_CAPACITY];
    uint32_t count;
    uint32_t request_count;
} TestStore;

static bool key_equal(OpenRideORMapPyramidTileKey a,
                      OpenRideORMapPyramidTileKey b)
{
    return a.zoom == b.zoom
        && a.x == b.x
        && a.y == b.y;
}

static TestTile *store_find(TestStore *store,
                            OpenRideORMapPyramidTileKey key,
                            bool create)
{
    for (uint32_t i = 0U; i < store->count; ++i) {
        if (store->tiles[i].used
            && key_equal(store->tiles[i].key, key)) {
            return &store->tiles[i];
        }
    }

    if (!create || store->count >= TEST_TILE_CAPACITY) {
        return NULL;
    }

    TestTile *tile = &store->tiles[store->count++];
    *tile = (TestTile){
        .used = true,
        .key = key,
        .state = OPENRIDE_ORMAP_PYRAMID_TILE_UNKNOWN
    };
    return tile;
}

static void store_set(TestStore *store,
                      OpenRideORMapPyramidTileKey key,
                      OpenRideORMapPyramidTileState state)
{
    TestTile *tile = store_find(store, key, true);
    assert(tile);
    tile->state = state;
}

static OpenRideORMapPyramidTileState test_state(
    void *userdata,
    OpenRideORMapPyramidTileKey key)
{
    TestStore *store = userdata;
    TestTile *tile = store_find(store, key, false);
    return tile
        ? tile->state
        : OPENRIDE_ORMAP_PYRAMID_TILE_UNKNOWN;
}

static void test_request(void *userdata,
                         OpenRideORMapPyramidTileKey key)
{
    TestStore *store = userdata;
    ++store->request_count;
    TestTile *tile = store_find(store, key, true);
    assert(tile);
    if (tile->state == OPENRIDE_ORMAP_PYRAMID_TILE_UNKNOWN) {
        tile->state = OPENRIDE_ORMAP_PYRAMID_TILE_REQUESTED;
    }
}

static OpenRideORMapPyramidTileKey child(
    OpenRideORMapPyramidTileKey parent,
    int index)
{
    return (OpenRideORMapPyramidTileKey){
        .zoom = parent.zoom + 1,
        .x = parent.x * 2 + (index & 1),
        .y = parent.y * 2 + ((index >> 1) & 1)
    };
}

static void set_children(TestStore *store,
                         OpenRideORMapPyramidTileKey parent,
                         OpenRideORMapPyramidTileState state)
{
    for (int i = 0; i < 4; ++i) {
        store_set(store, child(parent, i), state);
    }
}

static double alpha_for(const OpenRideORMapPyramidPlan *plan,
                        OpenRideORMapPyramidTileKey key)
{
    for (uint32_t i = 0U; i < plan->count; ++i) {
        if (key_equal(plan->tiles[i].key, key)) {
            return plan->tiles[i].alpha;
        }
    }
    return 0.0;
}

static void test_defaults(void)
{
    const OpenRideORMapTilePyramidConfig config =
        openride_ormap_tile_pyramid_default_config();

    assert(OPENRIDE_ORMAP_PYRAMID_FORMAT_VERSION == 11U);
    assert(config.min_zoom == 9);
    assert(config.max_zoom == 14);
    assert(config.prefetch_start < config.blend_start);
    assert(config.blend_start < config.blend_end);
    assert(config.ready_ramp_ms == 160U);
}

static void test_parent_fallback_and_prefetch(void)
{
    OpenRideORMapTilePyramid pyramid;
    assert(openride_ormap_tile_pyramid_init(&pyramid, NULL));

    TestStore store = {0};
    const OpenRideORMapPyramidTileKey root = {9, 256, 176};
    store_set(
        &store,
        root,
        OPENRIDE_ORMAP_PYRAMID_TILE_READY);

    OpenRideORMapPyramidPlan plan = {0};
    assert(openride_ormap_tile_pyramid_plan(
        &pyramid,
        9.10,
        &root,
        1U,
        1000U,
        test_state,
        test_request,
        &store,
        &plan));

    assert(plan.count == 1U);
    assert(fabs(alpha_for(&plan, root) - 1.0) < 1e-9);
    assert(plan.requests_issued == 4U);
    assert(plan.pending_tiles == 4U);
    assert(plan.needs_followup_frame);
    assert(store.request_count == 4U);

    openride_ormap_pyramid_plan_destroy(&plan);
    openride_ormap_tile_pyramid_destroy(&pyramid);
}

static void test_continuous_blend(void)
{
    OpenRideORMapTilePyramid pyramid;
    assert(openride_ormap_tile_pyramid_init(&pyramid, NULL));

    TestStore store = {0};
    const OpenRideORMapPyramidTileKey root = {9, 256, 176};
    store_set(&store, root, OPENRIDE_ORMAP_PYRAMID_TILE_READY);
    set_children(&store, root, OPENRIDE_ORMAP_PYRAMID_TILE_READY);

    OpenRideORMapPyramidPlan plan = {0};

    /* First resolved frame starts the availability ramp: parent remains solid. */
    assert(openride_ormap_tile_pyramid_plan(
        &pyramid,
        9.60,
        &root,
        1U,
        1000U,
        test_state,
        test_request,
        &store,
        &plan));
    assert(fabs(alpha_for(&plan, root) - 1.0) < 1e-9);
    assert(plan.needs_followup_frame);

    /* After 160 ms, only camera zoom controls the blend. */
    assert(openride_ormap_tile_pyramid_plan(
        &pyramid,
        9.60,
        &root,
        1U,
        1160U,
        test_state,
        test_request,
        &store,
        &plan));

    const double parent_alpha = alpha_for(&plan, root);
    const double child_alpha = alpha_for(&plan, child(root, 0));
    assert(parent_alpha > 0.0 && parent_alpha < 1.0);
    assert(child_alpha > 0.0 && child_alpha < 1.0);
    assert(fabs(parent_alpha + child_alpha - 1.0) < 1e-9);
    assert(plan.blending_families == 1U);

    /* Above blend_end, children own the complete parent footprint. */
    assert(openride_ormap_tile_pyramid_plan(
        &pyramid,
        9.90,
        &root,
        1U,
        1300U,
        test_state,
        test_request,
        &store,
        &plan));
    assert(alpha_for(&plan, root) == 0.0);
    for (int i = 0; i < 4; ++i) {
        assert(fabs(alpha_for(&plan, child(root, i)) - 1.0) < 1e-9);
    }

    openride_ormap_pyramid_plan_destroy(&plan);
    openride_ormap_tile_pyramid_destroy(&pyramid);
}

static void test_late_readiness_never_pops(void)
{
    OpenRideORMapTilePyramid pyramid;
    assert(openride_ormap_tile_pyramid_init(&pyramid, NULL));

    TestStore store = {0};
    const OpenRideORMapPyramidTileKey root = {9, 256, 176};
    store_set(&store, root, OPENRIDE_ORMAP_PYRAMID_TILE_READY);
    set_children(
        &store,
        root,
        OPENRIDE_ORMAP_PYRAMID_TILE_REQUESTED);

    OpenRideORMapPyramidPlan plan = {0};

    /* Camera is already beyond blend_end, but cold children cannot remove parent. */
    assert(openride_ormap_tile_pyramid_plan(
        &pyramid,
        10.10,
        &root,
        1U,
        2000U,
        test_state,
        test_request,
        &store,
        &plan));
    assert(fabs(alpha_for(&plan, root) - 1.0) < 1e-9);

    set_children(&store, root, OPENRIDE_ORMAP_PYRAMID_TILE_READY);

    /* Exact readiness frame: availability=0, still parent only. */
    assert(openride_ormap_tile_pyramid_plan(
        &pyramid,
        10.10,
        &root,
        1U,
        2100U,
        test_state,
        test_request,
        &store,
        &plan));
    assert(fabs(alpha_for(&plan, root) - 1.0) < 1e-9);

    /* Half ramp: local 50/50 transition despite camera already being far in. */
    assert(openride_ormap_tile_pyramid_plan(
        &pyramid,
        10.10,
        &root,
        1U,
        2180U,
        test_state,
        test_request,
        &store,
        &plan));
    assert(fabs(alpha_for(&plan, root) - 0.5) < 0.02);
    assert(fabs(alpha_for(&plan, child(root, 0)) - 0.5) < 0.02);

    /* Full ramp: child ownership. */
    assert(openride_ormap_tile_pyramid_plan(
        &pyramid,
        10.10,
        &root,
        1U,
        2260U,
        test_state,
        test_request,
        &store,
        &plan));
    assert(alpha_for(&plan, root) == 0.0);

    openride_ormap_pyramid_plan_destroy(&plan);
    openride_ormap_tile_pyramid_destroy(&pyramid);
}

static void test_zoom_out_is_symmetric(void)
{
    OpenRideORMapTilePyramid pyramid;
    assert(openride_ormap_tile_pyramid_init(&pyramid, NULL));

    TestStore store = {0};
    const OpenRideORMapPyramidTileKey root = {9, 256, 176};
    store_set(&store, root, OPENRIDE_ORMAP_PYRAMID_TILE_READY);
    set_children(&store, root, OPENRIDE_ORMAP_PYRAMID_TILE_READY);

    OpenRideORMapPyramidPlan plan = {0};

    /* Warm family state. */
    assert(openride_ormap_tile_pyramid_plan(
        &pyramid,
        9.90,
        &root,
        1U,
        1000U,
        test_state,
        test_request,
        &store,
        &plan));
    assert(openride_ormap_tile_pyramid_plan(
        &pyramid,
        9.90,
        &root,
        1U,
        1160U,
        test_state,
        test_request,
        &store,
        &plan));

    /* Moving backwards through the same fractional range reverses smoothly. */
    assert(openride_ormap_tile_pyramid_plan(
        &pyramid,
        9.60,
        &root,
        1U,
        1200U,
        test_state,
        test_request,
        &store,
        &plan));

    const double parent_alpha = alpha_for(&plan, root);
    const double child_alpha = alpha_for(&plan, child(root, 0));
    assert(parent_alpha > 0.0 && parent_alpha < 1.0);
    assert(child_alpha > 0.0 && child_alpha < 1.0);
    assert(fabs(parent_alpha + child_alpha - 1.0) < 1e-9);

    openride_ormap_pyramid_plan_destroy(&plan);
    openride_ormap_tile_pyramid_destroy(&pyramid);
}

static void test_family_hash_growth_preserves_ready_ramps(void)
{
    OpenRideORMapTilePyramid pyramid;
    assert(openride_ormap_tile_pyramid_init(&pyramid, NULL));

    TestStore store = {0};

    enum { ROOT_COUNT = 80 };
    OpenRideORMapPyramidTileKey roots[ROOT_COUNT];

    for (int i = 0; i < ROOT_COUNT; ++i) {
        roots[i] = (OpenRideORMapPyramidTileKey){
            .zoom = 9,
            .x = 120 + i,
            .y = 176
        };

        store_set(
            &store,
            roots[i],
            OPENRIDE_ORMAP_PYRAMID_TILE_READY);
        set_children(
            &store,
            roots[i],
            OPENRIDE_ORMAP_PYRAMID_TILE_READY);
    }

    OpenRideORMapPyramidPlan plan = {0};

    /*
     * 80 simultaneous families force the table beyond its initial 64 slots.
     * All families start their availability ramp on this frame.
     */
    assert(openride_ormap_tile_pyramid_plan(
        &pyramid,
        9.60,
        roots,
        ROOT_COUNT,
        1000U,
        test_state,
        test_request,
        &store,
        &plan));

    assert(pyramid.family_count == ROOT_COUNT);
    assert(pyramid.family_capacity >= 128U);

    /*
     * Rehashing must preserve ready_since_ms. At +80 ms, availability is 0.5
     * while desired camera refinement is also 0.5, so child ownership is
     * approximately 0.25 rather than restarting at zero.
     */
    assert(openride_ormap_tile_pyramid_plan(
        &pyramid,
        9.60,
        roots,
        ROOT_COUNT,
        1080U,
        test_state,
        test_request,
        &store,
        &plan));

    const double first_child =
        alpha_for(&plan, child(roots[0], 0));
    const double last_child =
        alpha_for(
            &plan,
            child(roots[ROOT_COUNT - 1], 3));

    assert(first_child > 0.23 && first_child < 0.27);
    assert(last_child > 0.23 && last_child < 0.27);

    /*
     * Reset clears hash occupancy without reallocating the table, making the
     * same pyramid reusable without stale family matches.
     */
    const uint32_t retained_capacity =
        pyramid.family_capacity;
    openride_ormap_tile_pyramid_reset(&pyramid);

    assert(pyramid.family_count == 0U);
    assert(pyramid.family_capacity == retained_capacity);

    assert(openride_ormap_tile_pyramid_plan(
        &pyramid,
        9.60,
        &roots[0],
        1U,
        2000U,
        test_state,
        test_request,
        &store,
        &plan));

    assert(pyramid.family_count == 1U);
    assert(fabs(alpha_for(&plan, roots[0]) - 1.0) < 1e-9);

    openride_ormap_pyramid_plan_destroy(&plan);
    openride_ormap_tile_pyramid_destroy(&pyramid);
}

static void test_empty_child_counts_as_resolved(void)
{
    OpenRideORMapTilePyramid pyramid;
    OpenRideORMapTilePyramidConfig config =
        openride_ormap_tile_pyramid_default_config();
    config.ready_ramp_ms = 0U;
    assert(openride_ormap_tile_pyramid_init(&pyramid, &config));

    TestStore store = {0};
    const OpenRideORMapPyramidTileKey root = {9, 256, 176};
    store_set(&store, root, OPENRIDE_ORMAP_PYRAMID_TILE_READY);
    for (int i = 0; i < 4; ++i) {
        store_set(
            &store,
            child(root, i),
            i == 0
                ? OPENRIDE_ORMAP_PYRAMID_TILE_EMPTY
                : OPENRIDE_ORMAP_PYRAMID_TILE_READY);
    }

    OpenRideORMapPyramidPlan plan = {0};
    assert(openride_ormap_tile_pyramid_plan(
        &pyramid,
        9.90,
        &root,
        1U,
        1000U,
        test_state,
        test_request,
        &store,
        &plan));

    assert(alpha_for(&plan, root) == 0.0);
    assert(alpha_for(&plan, child(root, 0)) == 0.0);
    for (int i = 1; i < 4; ++i) {
        assert(fabs(alpha_for(&plan, child(root, i)) - 1.0) < 1e-9);
    }

    openride_ormap_pyramid_plan_destroy(&plan);
    openride_ormap_tile_pyramid_destroy(&pyramid);
}

int main(void)
{
    test_defaults();
    test_parent_fallback_and_prefetch();
    test_continuous_blend();
    test_late_readiness_never_pops();
    test_zoom_out_is_symmetric();
    test_family_hash_growth_preserves_ready_ramps();
    test_empty_child_counts_as_resolved();

    puts("ormap_tile_pyramid: ok");
    return 0;
}
