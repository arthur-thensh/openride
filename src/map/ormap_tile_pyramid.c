#include "openride/ormap_tile_pyramid.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PYRAMID_ALPHA_EPSILON 0.0005

#define PYRAMID_FAMILY_INITIAL_CAPACITY 64U
#define PYRAMID_FAMILY_LOAD_NUMERATOR 7U
#define PYRAMID_FAMILY_LOAD_DENOMINATOR 10U

static bool key_equal(OpenRideORMapPyramidTileKey a,
                      OpenRideORMapPyramidTileKey b)
{
    return a.zoom == b.zoom
        && a.x == b.x
        && a.y == b.y;
}

static bool state_resolved(OpenRideORMapPyramidTileState state)
{
    return state == OPENRIDE_ORMAP_PYRAMID_TILE_READY
        || state == OPENRIDE_ORMAP_PYRAMID_TILE_EMPTY;
}

static bool state_drawable(OpenRideORMapPyramidTileState state)
{
    return state == OPENRIDE_ORMAP_PYRAMID_TILE_READY;
}

static double clamp01(double value)
{
    if (value <= 0.0) return 0.0;
    if (value >= 1.0) return 1.0;
    return value;
}

static double smoothstep(double value, double start, double end)
{
    if (value <= start) return 0.0;
    if (value >= end) return 1.0;
    const double t = (value - start) / (end - start);
    return t * t * (3.0 - 2.0 * t);
}

static bool key_valid(const OpenRideORMapTilePyramid *pyramid,
                      OpenRideORMapPyramidTileKey key)
{
    if (!pyramid) return false;
    if (key.zoom < pyramid->config.min_zoom
        || key.zoom > pyramid->config.max_zoom
        || key.zoom < 0
        || key.zoom >= 30) {
        return false;
    }

    const int count = 1 << key.zoom;
    return key.x >= 0 && key.x < count
        && key.y >= 0 && key.y < count;
}

static OpenRideORMapPyramidTileKey child_key(
    OpenRideORMapPyramidTileKey parent,
    int child_index)
{
    return (OpenRideORMapPyramidTileKey){
        .zoom = parent.zoom + 1,
        .x = parent.x * 2 + (child_index & 1),
        .y = parent.y * 2 + ((child_index >> 1) & 1)
    };
}

static bool plan_reserve(OpenRideORMapPyramidPlan *plan,
                         uint32_t required)
{
    if (!plan) return false;
    if (required <= plan->capacity) return true;

    uint32_t capacity = plan->capacity ? plan->capacity : 32U;
    while (capacity < required) {
        if (capacity > UINT32_MAX / 2U) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }

    OpenRideORMapPyramidDrawTile *grown =
        realloc(plan->tiles, (size_t)capacity * sizeof(*grown));
    if (!grown) return false;

    plan->tiles = grown;
    plan->capacity = capacity;
    return true;
}

static bool plan_add(OpenRideORMapPyramidPlan *plan,
                     OpenRideORMapPyramidTileKey key,
                     double alpha)
{
    if (!plan || alpha <= PYRAMID_ALPHA_EPSILON) return true;
    if (!plan_reserve(plan, plan->count + 1U)) return false;

    plan->tiles[plan->count++] = (OpenRideORMapPyramidDrawTile){
        .key = key,
        .alpha = clamp01(alpha)
    };

    if (plan->min_draw_zoom < 0 || key.zoom < plan->min_draw_zoom) {
        plan->min_draw_zoom = key.zoom;
    }
    if (plan->max_draw_zoom < 0 || key.zoom > plan->max_draw_zoom) {
        plan->max_draw_zoom = key.zoom;
    }
    return true;
}

static uint32_t family_hash(
    OpenRideORMapPyramidTileKey key)
{
    uint32_t value =
        (uint32_t)key.zoom * UINT32_C(0x9e3779b9);

    value ^=
        (uint32_t)key.x * UINT32_C(0x85ebca6b);
    value ^=
        (uint32_t)key.y * UINT32_C(0xc2b2ae35);

    value ^= value >> 16U;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15U;
    value *= UINT32_C(0x846ca68b);
    value ^= value >> 16U;
    return value;
}

static bool family_table_insert_existing(
    OpenRideORMapPyramidFamilyState *table,
    uint32_t capacity,
    OpenRideORMapPyramidFamilyState state)
{
    if (!table
        || capacity == 0U
        || (capacity & (capacity - 1U)) != 0U
        || !state.occupied) {
        return false;
    }

    uint32_t slot =
        family_hash(state.parent)
        & (capacity - 1U);

    for (uint32_t probe = 0U;
         probe < capacity;
         ++probe) {
        OpenRideORMapPyramidFamilyState *candidate =
            &table[slot];

        if (!candidate->occupied) {
            *candidate = state;
            return true;
        }

        slot =
            (slot + 1U)
            & (capacity - 1U);
    }

    return false;
}

static bool family_table_rehash(
    OpenRideORMapTilePyramid *pyramid,
    uint32_t new_capacity)
{
    if (!pyramid
        || new_capacity < PYRAMID_FAMILY_INITIAL_CAPACITY
        || (new_capacity & (new_capacity - 1U)) != 0U) {
        return false;
    }

    OpenRideORMapPyramidFamilyState *new_table =
        calloc(
            new_capacity,
            sizeof(*new_table));

    if (!new_table) return false;

    for (uint32_t i = 0U;
         i < pyramid->family_capacity;
         ++i) {
        const OpenRideORMapPyramidFamilyState state =
            pyramid->families[i];

        if (!state.occupied) continue;

        if (!family_table_insert_existing(
                new_table,
                new_capacity,
                state)) {
            free(new_table);
            return false;
        }
    }

    free(pyramid->families);
    pyramid->families = new_table;
    pyramid->family_capacity = new_capacity;
    return true;
}

static bool family_table_ensure_insert_capacity(
    OpenRideORMapTilePyramid *pyramid)
{
    if (!pyramid) return false;

    if (pyramid->family_capacity == 0U) {
        return family_table_rehash(
            pyramid,
            PYRAMID_FAMILY_INITIAL_CAPACITY);
    }

    /*
     * Grow before insertion at a 70% load factor. This keeps probe chains
     * short even during a long zoom/pan session while preserving every
     * family's ready-ramp state.
     */
    if ((uint64_t)(pyramid->family_count + 1U)
            * PYRAMID_FAMILY_LOAD_DENOMINATOR
        <= (uint64_t)pyramid->family_capacity
            * PYRAMID_FAMILY_LOAD_NUMERATOR) {
        return true;
    }

    if (pyramid->family_capacity > UINT32_MAX / 2U) {
        return false;
    }

    return family_table_rehash(
        pyramid,
        pyramid->family_capacity * 2U);
}

static OpenRideORMapPyramidFamilyState *family_get(
    OpenRideORMapTilePyramid *pyramid,
    OpenRideORMapPyramidTileKey parent)
{
    if (!pyramid) return NULL;

    /*
     * Look up first so an existing family at the load threshold does not
     * trigger an unnecessary resize.
     */
    if (pyramid->family_capacity > 0U) {
        uint32_t slot =
            family_hash(parent)
            & (pyramid->family_capacity - 1U);

        for (uint32_t probe = 0U;
             probe < pyramid->family_capacity;
             ++probe) {
            OpenRideORMapPyramidFamilyState *state =
                &pyramid->families[slot];

            if (!state->occupied) {
                break;
            }

            if (key_equal(state->parent, parent)) {
                state->last_seen_generation =
                    pyramid->generation;
                return state;
            }

            slot =
                (slot + 1U)
                & (pyramid->family_capacity - 1U);
        }
    }

    if (!family_table_ensure_insert_capacity(pyramid)) {
        return NULL;
    }

    uint32_t slot =
        family_hash(parent)
        & (pyramid->family_capacity - 1U);

    for (uint32_t probe = 0U;
         probe < pyramid->family_capacity;
         ++probe) {
        OpenRideORMapPyramidFamilyState *state =
            &pyramid->families[slot];

        if (!state->occupied) {
            *state = (OpenRideORMapPyramidFamilyState){
                .occupied = true,
                .parent = parent,
                .children_ready = false,
                .ready_since_ms = 0U,
                .last_seen_generation =
                    pyramid->generation
            };

            ++pyramid->family_count;
            return state;
        }

        /*
         * The rehash path above should make this redundant, but keeping the
         * equality check here makes family_get robust if a matching state was
         * inserted by a future caller between lookup and insertion.
         */
        if (key_equal(state->parent, parent)) {
            state->last_seen_generation =
                pyramid->generation;
            return state;
        }

        slot =
            (slot + 1U)
            & (pyramid->family_capacity - 1U);
    }

    return NULL;
}

static void family_mark_not_ready(
    OpenRideORMapTilePyramid *pyramid,
    OpenRideORMapPyramidTileKey parent)
{
    OpenRideORMapPyramidFamilyState *family =
        family_get(pyramid, parent);
    if (!family) return;
    family->children_ready = false;
    family->ready_since_ms = 0U;
}

static double family_availability(
    OpenRideORMapTilePyramid *pyramid,
    OpenRideORMapPyramidTileKey parent,
    uint64_t now_ms)
{
    OpenRideORMapPyramidFamilyState *family =
        family_get(pyramid, parent);
    if (!family) return 0.0;

    if (!family->children_ready) {
        family->children_ready = true;
        family->ready_since_ms = now_ms;
    }

    if (pyramid->config.ready_ramp_ms == 0U) {
        return 1.0;
    }

    const uint64_t elapsed =
        now_ms >= family->ready_since_ms
            ? now_ms - family->ready_since_ms
            : 0U;
    return clamp01(
        (double)elapsed
        / (double)pyramid->config.ready_ramp_ms);
}

static void request_if_unknown(
    OpenRideORMapPyramidTileKey key,
    OpenRideORMapPyramidStateFn state_fn,
    OpenRideORMapPyramidRequestFn request_fn,
    void *userdata,
    OpenRideORMapPyramidPlan *plan)
{
    if (!state_fn || !plan) return;

    const OpenRideORMapPyramidTileState state =
        state_fn(userdata, key);

    if (state == OPENRIDE_ORMAP_PYRAMID_TILE_UNKNOWN) {
        if (request_fn) {
            request_fn(userdata, key);
            ++plan->requests_issued;
        }
        ++plan->pending_tiles;
        plan->needs_followup_frame = true;
    } else if (state == OPENRIDE_ORMAP_PYRAMID_TILE_REQUESTED) {
        ++plan->pending_tiles;
        plan->needs_followup_frame = true;
    }
}

static bool children_states(
    OpenRideORMapPyramidTileKey parent,
    OpenRideORMapPyramidStateFn state_fn,
    void *userdata,
    OpenRideORMapPyramidTileState states[4])
{
    if (!state_fn) return false;
    bool all_resolved = true;
    for (int i = 0; i < 4; ++i) {
        states[i] =
            state_fn(userdata, child_key(parent, i));
        if (!state_resolved(states[i])) {
            all_resolved = false;
        }
    }
    return all_resolved;
}

static bool plan_node(
    OpenRideORMapTilePyramid *pyramid,
    double camera_zoom,
    OpenRideORMapPyramidTileKey key,
    uint64_t now_ms,
    OpenRideORMapPyramidStateFn state_fn,
    OpenRideORMapPyramidRequestFn request_fn,
    void *userdata,
    double inherited_alpha,
    OpenRideORMapPyramidPlan *plan)
{
    if (!pyramid || !state_fn || !plan) return false;

    const OpenRideORMapPyramidTileState own_state =
        state_fn(userdata, key);

    if (!state_resolved(own_state)) {
        request_if_unknown(
            key,
            state_fn,
            request_fn,
            userdata,
            plan);
        return true;
    }

    if (key.zoom >= pyramid->config.max_zoom) {
        return !state_drawable(own_state)
            || plan_add(plan, key, inherited_alpha);
    }

    const double prefetch_at =
        (double)key.zoom + pyramid->config.prefetch_start;
    const double blend_start =
        (double)key.zoom + pyramid->config.blend_start;
    const double blend_end =
        (double)key.zoom + pyramid->config.blend_end;

    if (camera_zoom >= prefetch_at) {
        for (int i = 0; i < 4; ++i) {
            request_if_unknown(
                child_key(key, i),
                state_fn,
                request_fn,
                userdata,
                plan);
        }
    }

    const double desired_refinement =
        smoothstep(camera_zoom, blend_start, blend_end);

    if (desired_refinement <= PYRAMID_ALPHA_EPSILON) {
        family_mark_not_ready(pyramid, key);
        return !state_drawable(own_state)
            || plan_add(plan, key, inherited_alpha);
    }

    OpenRideORMapPyramidTileState child_states[4];
    const bool all_children_resolved =
        children_states(
            key,
            state_fn,
            userdata,
            child_states);

    if (!all_children_resolved) {
        family_mark_not_ready(pyramid, key);
        plan->needs_followup_frame = true;
        return !state_drawable(own_state)
            || plan_add(plan, key, inherited_alpha);
    }

    const double availability =
        family_availability(
            pyramid,
            key,
            now_ms);
    const double refinement =
        desired_refinement * availability;

    if (availability < 1.0 - PYRAMID_ALPHA_EPSILON) {
        plan->needs_followup_frame = true;
    }

    if (refinement < 1.0 - PYRAMID_ALPHA_EPSILON) {
        ++plan->blending_families;

        if (state_drawable(own_state)
            && !plan_add(
                plan,
                key,
                inherited_alpha * (1.0 - refinement))) {
            return false;
        }

        for (int i = 0; i < 4; ++i) {
            if (state_drawable(child_states[i])
                && !plan_add(
                    plan,
                    child_key(key, i),
                    inherited_alpha * refinement)) {
                return false;
            }
        }
        return true;
    }

    /*
     * Children fully own this footprint. Recurse so each child family can
     * independently prefetch/refine toward the next integer data zoom.
     */
    for (int i = 0; i < 4; ++i) {
        if (!plan_node(
                pyramid,
                camera_zoom,
                child_key(key, i),
                now_ms,
                state_fn,
                request_fn,
                userdata,
                inherited_alpha,
                plan)) {
            return false;
        }
    }
    return true;
}

OpenRideORMapTilePyramidConfig
openride_ormap_tile_pyramid_default_config(void)
{
    return (OpenRideORMapTilePyramidConfig){
        .min_zoom = OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM,
        .max_zoom = OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM,
        .prefetch_start =
            OPENRIDE_ORMAP_PYRAMID_DEFAULT_PREFETCH_START,
        .blend_start =
            OPENRIDE_ORMAP_PYRAMID_DEFAULT_BLEND_START,
        .blend_end =
            OPENRIDE_ORMAP_PYRAMID_DEFAULT_BLEND_END,
        .ready_ramp_ms =
            OPENRIDE_ORMAP_PYRAMID_DEFAULT_READY_RAMP_MS
    };
}

bool openride_ormap_tile_pyramid_init(
    OpenRideORMapTilePyramid *pyramid,
    const OpenRideORMapTilePyramidConfig *config)
{
    if (!pyramid) return false;

    const OpenRideORMapTilePyramidConfig effective =
        config
            ? *config
            : openride_ormap_tile_pyramid_default_config();

    if (effective.min_zoom < 0
        || effective.max_zoom < effective.min_zoom
        || effective.max_zoom >= 30
        || !isfinite(effective.prefetch_start)
        || !isfinite(effective.blend_start)
        || !isfinite(effective.blend_end)
        || effective.prefetch_start > effective.blend_start
        || effective.blend_start >= effective.blend_end) {
        return false;
    }

    memset(pyramid, 0, sizeof(*pyramid));
    pyramid->config = effective;
    return true;
}

void openride_ormap_tile_pyramid_destroy(
    OpenRideORMapTilePyramid *pyramid)
{
    if (!pyramid) return;
    free(pyramid->families);
    memset(pyramid, 0, sizeof(*pyramid));
}

void openride_ormap_tile_pyramid_reset(
    OpenRideORMapTilePyramid *pyramid)
{
    if (!pyramid) return;

    if (pyramid->families
        && pyramid->family_capacity > 0U) {
        memset(
            pyramid->families,
            0,
            (size_t)pyramid->family_capacity
                * sizeof(*pyramid->families));
    }

    pyramid->family_count = 0U;
    pyramid->generation = 0U;
}

void openride_ormap_pyramid_plan_clear(
    OpenRideORMapPyramidPlan *plan)
{
    if (!plan) return;
    plan->count = 0U;
    plan->requests_issued = 0U;
    plan->pending_tiles = 0U;
    plan->blending_families = 0U;
    plan->min_draw_zoom = -1;
    plan->max_draw_zoom = -1;
    plan->needs_followup_frame = false;
}

void openride_ormap_pyramid_plan_destroy(
    OpenRideORMapPyramidPlan *plan)
{
    if (!plan) return;
    free(plan->tiles);
    memset(plan, 0, sizeof(*plan));
}

bool openride_ormap_tile_pyramid_plan(
    OpenRideORMapTilePyramid *pyramid,
    double camera_zoom,
    const OpenRideORMapPyramidTileKey *roots,
    uint32_t root_count,
    uint64_t now_ms,
    OpenRideORMapPyramidStateFn state_fn,
    OpenRideORMapPyramidRequestFn request_fn,
    void *userdata,
    OpenRideORMapPyramidPlan *plan)
{
    if (!pyramid
        || !isfinite(camera_zoom)
        || (!roots && root_count > 0U)
        || !state_fn
        || !plan) {
        return false;
    }

    openride_ormap_pyramid_plan_clear(plan);
    ++pyramid->generation;

    for (uint32_t i = 0U; i < root_count; ++i) {
        if (!key_valid(pyramid, roots[i])
            || roots[i].zoom != pyramid->config.min_zoom) {
            return false;
        }

        if (!plan_node(
                pyramid,
                camera_zoom,
                roots[i],
                now_ms,
                state_fn,
                request_fn,
                userdata,
                1.0,
                plan)) {
            return false;
        }
    }

    return true;
}
