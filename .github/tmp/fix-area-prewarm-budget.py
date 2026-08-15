from pathlib import Path

path = Path('src/map/ormap_renderer.c')
text = path.read_text()
old = '''        if (v6) {\n            if (!renderer->area_detail_ready\n                && camera->zoom >= ORMAP_WATER_DETAIL_PREWARM_START) {\n                const double detail_view_zoom =\n                    camera->zoom < ORMAP_WATER_DETAIL_START\n                        ? ORMAP_WATER_DETAIL_START\n                        : camera->zoom;\n                (void)prewarm_area_level(renderer,\n                                         camera,\n                                         width,\n                                         height,\n                                         metadata->area_detail_zoom,\n                                         detail_view_zoom,\n                                         ORMAP_AREA_PREWARM_TILE_BUDGET);\n                renderer->area_detail_ready = area_level_cache_ready(renderer,\n                                                                      camera,\n                                                                      width,\n                                                                      height,\n                                                                      metadata->area_detail_zoom);\n            }\n\n            const double nominal_detail_mix ='''
new = '''        if (v6) {\n            /* Detail prewarm is owned by draw_masks(), the first surface\n             * layer of the frame. Do not issue a second speculative load\n             * here: the one-tile/frame budget must remain a hard ceiling. */\n            const double nominal_detail_mix ='''
if text.count(old) != 1:
    raise SystemExit(f'expected one duplicate prewarm block, got {text.count(old)}')
text = text.replace(old, new, 1)
path.write_text(text)
