from pathlib import Path

p = Path("src/map/ormap_renderer.c")
s = p.read_text()

def rep(old, new):
    global s
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"expected one match, found {n}: {old[:80]!r}")
    s = s.replace(old, new, 1)

rep(
"#define ORMAP_ROAD_DRAW_LOAD_BUDGET 2U\n\ntypedef struct GeometryBatch",
"#define ORMAP_ROAD_DRAW_LOAD_BUDGET 2U\n\nstatic OpenRideORMapRoadDebugStats g_last_road_debug_stats;\nstatic const OpenRideORMapRenderer *g_last_road_debug_renderer = NULL;\n\ntypedef struct GeometryBatch")

rep(
"""    if (!victim) victim = oldest;\n    road_cache_entry_destroy(victim);\n""",
"""    if (!victim) {\n        victim = oldest;\n        /* Speculative prewarm must not evict a tile used by the immediately\n         * previous rendered frame. The real draw path may still replace it. */\n        if (prewarm && victim->occupied\n            && victim->last_used + 1U >= renderer->frame_counter) {\n            return NULL;\n        }\n    }\n    road_cache_entry_destroy(victim);\n""")

rep(
"""static uint32_t prewarm_road_level(OpenRideORMapRenderer *renderer,\n                                   const OpenRideMapCamera *camera,\n                                   int width,\n                                   int height,\n                                   int zoom,\n                                   uint32_t budget)\n{\n    if (!renderer || !camera || budget == 0U) return 0U;\n    const int count = 1 << zoom;\n    const double scale = pow(2.0, camera->zoom - zoom);\n""",
"""static uint32_t prewarm_road_level(OpenRideORMapRenderer *renderer,\n                                   const OpenRideMapCamera *camera,\n                                   int width,\n                                   int height,\n                                   int zoom,\n                                   double viewport_zoom,\n                                   uint32_t budget)\n{\n    if (!renderer || !camera || budget == 0U) return 0U;\n    const int count = 1 << zoom;\n    const double scale = pow(2.0, viewport_zoom - zoom);\n""")

rep(
"""            if (road_cache_contains(renderer, zoom, qx, ty)) continue;\n            (void)road_cache_slot(renderer, zoom, qx, ty, true, false);\n            if (++loaded >= budget) return loaded;\n""",
"""            if (road_cache_contains(renderer, zoom, qx, ty)) continue;\n            OpenRideORMapRoadCacheEntry *entry =\n                road_cache_slot(renderer, zoom, qx, ty, true, false);\n            if (!entry) continue;\n            if (++loaded >= budget) return loaded;\n""")

old = """static int road_prewarm_target_zoom(OpenRideORMapRenderer *renderer,\n                                    double camera_zoom)\n{\n    if (!renderer) return -1;\n    if (!renderer->road_has_previous_camera_zoom) {\n        renderer->road_previous_camera_zoom = camera_zoom;\n        renderer->road_has_previous_camera_zoom = true;\n        return -1;\n    }\n\n    const double delta = camera_zoom - renderer->road_previous_camera_zoom;\n    renderer->road_previous_camera_zoom = camera_zoom;\n    if (delta > 0.0001) {\n        renderer->road_zoom_direction = 1;\n    } else if (delta < -0.0001) {\n        renderer->road_zoom_direction = -1;\n    }\n\n    if (renderer->road_zoom_direction > 0) {\n        if (camera_zoom >= 12.80 && camera_zoom < 14.40) {\n            return OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM;\n        }\n        if (camera_zoom >= 11.15 && camera_zoom < 12.80) {\n            return OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM;\n        }\n        if (camera_zoom >= 9.95 && camera_zoom < 11.25) {\n            return OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM;\n        }\n    } else if (renderer->road_zoom_direction < 0) {\n        if (camera_zoom <= 15.00 && camera_zoom > 13.40) {\n            return OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM;\n        }\n        if (camera_zoom <= 13.40 && camera_zoom > 11.85) {\n            return OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM;\n        }\n        if (camera_zoom <= 11.85 && camera_zoom > 10.55) {\n            return OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM;\n        }\n    }\n    return -1;\n}\n"""
new = """static int road_prewarm_target_zoom(OpenRideORMapRenderer *renderer,\n                                    double camera_zoom,\n                                    double *viewport_zoom)\n{\n    if (viewport_zoom) *viewport_zoom = -1.0;\n    if (!renderer) return -1;\n    if (!renderer->road_has_previous_camera_zoom) {\n        renderer->road_previous_camera_zoom = camera_zoom;\n        renderer->road_has_previous_camera_zoom = true;\n        return -1;\n    }\n\n    const double delta = camera_zoom - renderer->road_previous_camera_zoom;\n    renderer->road_previous_camera_zoom = camera_zoom;\n    if (delta > 0.0001) {\n        renderer->road_zoom_direction = 1;\n    } else if (delta < -0.0001) {\n        renderer->road_zoom_direction = -1;\n    }\n\n    if (renderer->road_zoom_direction > 0) {\n        if (camera_zoom >= 12.80 && camera_zoom < 14.40) {\n            if (viewport_zoom) *viewport_zoom = 13.40;\n            return OPENRIDE_ORMAP_ROAD_DETAIL_ZOOM;\n        }\n        if (camera_zoom >= 11.15 && camera_zoom < 12.80) {\n            if (viewport_zoom) *viewport_zoom = 11.85;\n            return OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM;\n        }\n        if (camera_zoom >= 9.95 && camera_zoom < 11.25) {\n            if (viewport_zoom) *viewport_zoom = 10.55;\n            return OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM;\n        }\n    } else if (renderer->road_zoom_direction < 0) {\n        if (camera_zoom <= 15.00 && camera_zoom > 13.40) {\n            if (viewport_zoom) *viewport_zoom = 14.40;\n            return OPENRIDE_ORMAP_ROAD_LOCAL_ZOOM;\n        }\n        if (camera_zoom <= 13.40 && camera_zoom > 11.85) {\n            if (viewport_zoom) *viewport_zoom = 12.80;\n            return OPENRIDE_ORMAP_ROAD_OVERVIEW_ZOOM;\n        }\n        if (camera_zoom <= 11.85 && camera_zoom > 10.55) {\n            if (viewport_zoom) *viewport_zoom = 11.25;\n            return OPENRIDE_ORMAP_ROAD_REGIONAL_ZOOM;\n        }\n    }\n    return -1;\n}\n"""
rep(old, new)

rep(
"""    const int prewarm_zoom = road_prewarm_target_zoom(renderer, camera->zoom);\n    renderer->road_debug.prewarm_zoom = prewarm_zoom;\n    if (prewarm_zoom >= 0) {\n        (void)prewarm_road_level(renderer,\n                                 camera,\n                                 width,\n                                 height,\n                                 prewarm_zoom,\n                                 ORMAP_ROAD_PREWARM_TILE_BUDGET);\n    }\n""",
"""    double prewarm_view_zoom = -1.0;\n    const int prewarm_zoom =\n        road_prewarm_target_zoom(renderer, camera->zoom, &prewarm_view_zoom);\n    renderer->road_debug.prewarm_zoom = prewarm_zoom;\n    if (prewarm_zoom >= 0) {\n        (void)prewarm_road_level(renderer,\n                                 camera,\n                                 width,\n                                 height,\n                                 prewarm_zoom,\n                                 prewarm_view_zoom,\n                                 ORMAP_ROAD_PREWARM_TILE_BUDGET);\n    }\n""")

rep(
"""    renderer->road_debug.roads_ms =\n        (double)(SDL_GetTicksNS() - roads_started) / 1000000.0;\n}\n\nstatic const char *label_kind_name""",
"""    renderer->road_debug.roads_ms =\n        (double)(SDL_GetTicksNS() - roads_started) / 1000000.0;\n    g_last_road_debug_stats = renderer->road_debug;\n    g_last_road_debug_renderer = renderer;\n}\n\nstatic const char *label_kind_name""")

rep(
"""    memset(stats, 0, sizeof(*stats));\n    stats->prewarm_zoom = -1;\n    if (renderer) *stats = renderer->road_debug;\n""",
"""    memset(stats, 0, sizeof(*stats));\n    stats->prewarm_zoom = -1;\n    if (renderer && renderer == g_last_road_debug_renderer) {\n        *stats = renderer->road_debug;\n    } else if (g_last_road_debug_renderer) {\n        /* MapWorld owns the renderer actually used for installed regions.\n         * The DEV HUD asks the standalone renderer, so expose the most\n         * recently drawn ORMap road pass as a diagnostic fallback. */\n        *stats = g_last_road_debug_stats;\n    } else if (renderer) {\n        *stats = renderer->road_debug;\n    }\n""")

p.write_text(s)
print("road prewarm v1.1 applied")
