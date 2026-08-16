#ifndef OPENRIDE_ORMAP_TILE_PYRAMID_H
#define OPENRIDE_ORMAP_TILE_PYRAMID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Experimental next-generation ORMap format.
 *
 * This constant is deliberately separate from OPENRIDE_ORMAP_FORMAT_VERSION:
 * the stable v8 reader/renderer remains the application path while the v11
 * tile pyramid is developed and validated in parallel.
 */
#define OPENRIDE_ORMAP_PYRAMID_FORMAT_VERSION 11U
#define OPENRIDE_ORMAP_PYRAMID_MIN_ZOOM 9
#define OPENRIDE_ORMAP_PYRAMID_MAX_ZOOM 14

#define OPENRIDE_ORMAP_PYRAMID_DEFAULT_PREFETCH_START 0.00
#define OPENRIDE_ORMAP_PYRAMID_DEFAULT_BLEND_START 0.35
#define OPENRIDE_ORMAP_PYRAMID_DEFAULT_BLEND_END 0.85
#define OPENRIDE_ORMAP_PYRAMID_DEFAULT_READY_RAMP_MS 160U

typedef struct OpenRideORMapPyramidTileKey {
    int zoom;
    int x;
    int y;
} OpenRideORMapPyramidTileKey;

typedef enum OpenRideORMapPyramidTileState {
    OPENRIDE_ORMAP_PYRAMID_TILE_UNKNOWN = 0,
    OPENRIDE_ORMAP_PYRAMID_TILE_REQUESTED,
    OPENRIDE_ORMAP_PYRAMID_TILE_READY,
    OPENRIDE_ORMAP_PYRAMID_TILE_EMPTY,
    OPENRIDE_ORMAP_PYRAMID_TILE_FAILED
} OpenRideORMapPyramidTileState;

typedef OpenRideORMapPyramidTileState
(*OpenRideORMapPyramidStateFn)(
    void *userdata,
    OpenRideORMapPyramidTileKey key);

typedef void
(*OpenRideORMapPyramidRequestFn)(
    void *userdata,
    OpenRideORMapPyramidTileKey key);

typedef struct OpenRideORMapTilePyramidConfig {
    int min_zoom;
    int max_zoom;

    /*
     * For a parent at data zoom Z:
     *
     * - children can be requested from camera zoom Z + prefetch_start;
     * - visual refinement starts at Z + blend_start;
     * - children fully own the parent footprint at Z + blend_end.
     *
     * These are camera-zoom positions, not global switch thresholds. Every
     * parent tile resolves independently.
     */
    double prefetch_start;
    double blend_start;
    double blend_end;

    /*
     * If children become ready late, availability itself ramps in over this
     * duration. This prevents a cold cache from causing an instantaneous local
     * pop even when camera zoom is already beyond blend_end.
     */
    uint32_t ready_ramp_ms;
} OpenRideORMapTilePyramidConfig;

typedef struct OpenRideORMapPyramidDrawTile {
    OpenRideORMapPyramidTileKey key;
    double alpha;
} OpenRideORMapPyramidDrawTile;

typedef struct OpenRideORMapPyramidPlan {
    OpenRideORMapPyramidDrawTile *tiles;
    uint32_t count;
    uint32_t capacity;

    uint32_t requests_issued;
    uint32_t pending_tiles;
    uint32_t blending_families;

    int min_draw_zoom;
    int max_draw_zoom;
    bool needs_followup_frame;
} OpenRideORMapPyramidPlan;

typedef struct OpenRideORMapPyramidFamilyState {
    /*
     * V3.8.7 stores family state directly in an open-addressed hash table.
     * An unoccupied slot is zero-initialized.
     */
    bool occupied;
    OpenRideORMapPyramidTileKey parent;
    bool children_ready;
    uint64_t ready_since_ms;
    uint64_t last_seen_generation;
} OpenRideORMapPyramidFamilyState;

typedef struct OpenRideORMapTilePyramid {
    OpenRideORMapTilePyramidConfig config;
    OpenRideORMapPyramidFamilyState *families;
    uint32_t family_count;
    uint32_t family_capacity;
    uint64_t generation;
} OpenRideORMapTilePyramid;

OpenRideORMapTilePyramidConfig
openride_ormap_tile_pyramid_default_config(void);

bool openride_ormap_tile_pyramid_init(
    OpenRideORMapTilePyramid *pyramid,
    const OpenRideORMapTilePyramidConfig *config);

void openride_ormap_tile_pyramid_destroy(
    OpenRideORMapTilePyramid *pyramid);

void openride_ormap_tile_pyramid_reset(
    OpenRideORMapTilePyramid *pyramid);

void openride_ormap_pyramid_plan_clear(
    OpenRideORMapPyramidPlan *plan);

void openride_ormap_pyramid_plan_destroy(
    OpenRideORMapPyramidPlan *plan);

/*
 * Build a draw/request plan for visible root tiles.
 *
 * roots are normally z9 tiles intersecting the viewport. Each root remains a
 * complete fallback until all four children are READY/EMPTY. Refinement then
 * happens locally and continuously according to camera_zoom.
 *
 * The planner does not know SQLite, SDL or map styling. It only decides which
 * tile geometry owns which quadtree footprint and at what alpha.
 */
bool openride_ormap_tile_pyramid_plan(
    OpenRideORMapTilePyramid *pyramid,
    double camera_zoom,
    const OpenRideORMapPyramidTileKey *roots,
    uint32_t root_count,
    uint64_t now_ms,
    OpenRideORMapPyramidStateFn state_fn,
    OpenRideORMapPyramidRequestFn request_fn,
    void *userdata,
    OpenRideORMapPyramidPlan *plan);

#endif
