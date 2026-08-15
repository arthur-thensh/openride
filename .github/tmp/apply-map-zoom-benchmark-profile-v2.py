#!/usr/bin/env python3
from pathlib import Path
import re

def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 exact match, got {count}")
    return text.replace(old, new, 1)

def replace_count(text, old, new, expected, label):
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"{label}: expected {expected} matches, got {count}")
    return text.replace(old, new)

path = Path("src/map/map_world.h")
text = path.read_text()
text = replace_once(text, '#include <SDL3/SDL.h>\n\n#include "openride/map_camera.h"', '#include <SDL3/SDL.h>\n\n#include "map/ormap_renderer.h"\n#include "openride/map_camera.h"', "map_world include")
text = replace_once(text, 'typedef struct OpenRideMapWorld OpenRideMapWorld;\n', '''typedef struct OpenRideMapWorldDebugStats {
    bool overview_drawn;
    bool detail_drawn;
    bool ormap_stats_valid;
    uint32_t visible_detail_regions;
    double overview_ms;
    double detail_ms;
    double masks_ms;
    double areas_ms;
    double waterways_ms;
    double roads_ms;
    double labels_ms;
    OpenRideORMapRoadDebugStats road;
    OpenRideORMapAreaDebugStats area;
} OpenRideMapWorldDebugStats;

typedef struct OpenRideMapWorld OpenRideMapWorld;
''', "map_world debug struct")
text = replace_once(text, 'size_t openride_map_world_region_count(const OpenRideMapWorld *world);\n', '''void openride_map_world_debug_begin_frame(OpenRideMapWorld *world);
void openride_map_world_debug_end_frame(OpenRideMapWorld *world);
void openride_map_world_get_debug_stats(
    const OpenRideMapWorld *world,
    OpenRideMapWorldDebugStats *stats);

size_t openride_map_world_region_count(const OpenRideMapWorld *world);
''', "map_world debug declarations")
path.write_text(text)

path = Path("src/map/map_world.c")
text = path.read_text()
text = replace_once(text, '''struct OpenRideMapWorld {
    SDL_Renderer *renderer;
    OpenRideMapWorldRegion *regions;
    size_t region_count;
    SDL_Vertex *vertices;
    int *indices;
    uint32_t vertex_capacity;
    uint32_t index_capacity;
};
''', '''struct OpenRideMapWorld {
    SDL_Renderer *renderer;
    OpenRideMapWorldRegion *regions;
    size_t region_count;
    SDL_Vertex *vertices;
    int *indices;
    uint32_t vertex_capacity;
    uint32_t index_capacity;
    bool debug_enabled;
    OpenRideMapWorldDebugStats debug;
};

static void map_world_accumulate_road_debug(
    OpenRideORMapRoadDebugStats *dst,
    const OpenRideORMapRoadDebugStats *src)
{
    if (!dst || !src) return;
    dst->roads_ms += src->roads_ms;
    dst->load_ms += src->load_ms;
    dst->cache_hits += src->cache_hits;
    dst->cache_misses += src->cache_misses;
    dst->prewarm_loads += src->prewarm_loads;
    dst->draw_loads += src->draw_loads;
    dst->deferred_loads += src->deferred_loads;
    dst->tiles_visited += src->tiles_visited;
    dst->segments_drawn += src->segments_drawn;
    dst->batches += src->batches;
    if (src->prewarm_zoom >= 0) {
        if (dst->prewarm_zoom < 0) dst->prewarm_zoom = src->prewarm_zoom;
        else if (dst->prewarm_zoom != src->prewarm_zoom) dst->prewarm_zoom = -2;
    }
}

static void map_world_accumulate_area_debug(
    OpenRideORMapAreaDebugStats *dst,
    const OpenRideORMapAreaDebugStats *src)
{
    if (!dst || !src) return;
    dst->areas_ms += src->areas_ms;
    dst->load_ms += src->load_ms;
    dst->tiles_visited += src->tiles_visited;
    dst->triangles_drawn += src->triangles_drawn;
    dst->batches += src->batches;
    dst->prewarm_loads += src->prewarm_loads;
    dst->draw_loads += src->draw_loads;
    dst->deferred_loads += src->deferred_loads;
}

void openride_map_world_debug_begin_frame(OpenRideMapWorld *world)
{
    if (!world) return;
    memset(&world->debug, 0, sizeof(world->debug));
    world->debug.road.prewarm_zoom = -1;
    world->debug_enabled = true;
}

void openride_map_world_debug_end_frame(OpenRideMapWorld *world)
{
    if (!world) return;
    world->debug_enabled = false;
}

void openride_map_world_get_debug_stats(
    const OpenRideMapWorld *world,
    OpenRideMapWorldDebugStats *stats)
{
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
    stats->road.prewarm_zoom = -1;
    if (world) *stats = world->debug;
}
''', "map_world struct/helpers")
text = replace_once(text, '''    const OpenRideMapPalette palette = openride_map_palette(style);
    const double overview_handoff =
''', '''    const bool debug_enabled = world->debug_enabled;
    const uint64_t debug_started_ns = debug_enabled ? SDL_GetTicksNS() : 0U;
    if (debug_enabled) world->debug.overview_drawn = true;

    const OpenRideMapPalette palette = openride_map_palette(style);
    const double overview_handoff =
''', "overview timing start")
text = replace_once(text, '''    draw_major_city_labels(world,
                           camera,
                           &palette,
                           overview_handoff,
                           viewport_width,
                           viewport_height);
}
''', '''    draw_major_city_labels(world,
                           camera,
                           &palette,
                           overview_handoff,
                           viewport_width,
                           viewport_height);

    if (debug_enabled) {
        world->debug.overview_ms +=
            (double)(SDL_GetTicksNS() - debug_started_ns) / 1000000.0;
    }
}
''', "overview timing end")
text = replace_once(text, '''    const OpenRideMapPalette palette = openride_map_palette(style);
    SDL_SetRenderDrawColor(world->renderer,
''', '''    const bool debug_enabled = world->debug_enabled;
    const uint64_t debug_started_ns = debug_enabled ? SDL_GetTicksNS() : 0U;
    if (debug_enabled) world->debug.detail_drawn = true;

    const OpenRideMapPalette palette = openride_map_palette(style);
    SDL_SetRenderDrawColor(world->renderer,
''', "detail timing start")
text = replace_once(text, '''    if (visible_count == 0U) return;

    /*
     * Render by cartographic layer across every visible .ormap instead of
''', '''    if (debug_enabled) world->debug.visible_detail_regions = (uint32_t)visible_count;
    if (visible_count == 0U) {
        if (debug_enabled) {
            world->debug.detail_ms +=
                (double)(SDL_GetTicksNS() - debug_started_ns) / 1000000.0;
        }
        return;
    }

    /*
     * Render by cartographic layer across every visible .ormap instead of
''', "detail visible count")
pattern = re.compile(r'''    for \(int layer = OPENRIDE_ORMAP_RENDER_LAYER_MASKS;\n         layer <= OPENRIDE_ORMAP_RENDER_LAYER_LABELS;\n         \+\+layer\) \{\n        for \(size_t i = 0U; i < world->region_count; \+\+i\) \{\n            OpenRideMapWorldRegion \*region = &world->regions\[i\];\n            if \(!region->detail_visible\) continue;\n\n            openride_ormap_renderer_draw_layer\(region->detail_renderer,\n                                               camera,\n                                               viewport_width,\n                                               viewport_height,\n                                               \(OpenRideORMapRenderLayer\)layer\);\n        \}\n\}\n\}\n\n\nsize_t openride_map_world_region_count''', re.MULTILINE)
replacement = '''    for (int layer = OPENRIDE_ORMAP_RENDER_LAYER_MASKS;
         layer <= OPENRIDE_ORMAP_RENDER_LAYER_LABELS;
         ++layer) {
        const uint64_t layer_started_ns = debug_enabled ? SDL_GetTicksNS() : 0U;
        for (size_t i = 0U; i < world->region_count; ++i) {
            OpenRideMapWorldRegion *region = &world->regions[i];
            if (!region->detail_visible) continue;
            openride_ormap_renderer_draw_layer(region->detail_renderer,
                                               camera,
                                               viewport_width,
                                               viewport_height,
                                               (OpenRideORMapRenderLayer)layer);
        }
        if (debug_enabled) {
            const double layer_ms = (double)(SDL_GetTicksNS() - layer_started_ns) / 1000000.0;
            switch ((OpenRideORMapRenderLayer)layer) {
                case OPENRIDE_ORMAP_RENDER_LAYER_MASKS: world->debug.masks_ms += layer_ms; break;
                case OPENRIDE_ORMAP_RENDER_LAYER_AREAS: world->debug.areas_ms += layer_ms; break;
                case OPENRIDE_ORMAP_RENDER_LAYER_WATERWAYS: world->debug.waterways_ms += layer_ms; break;
                case OPENRIDE_ORMAP_RENDER_LAYER_ROADS: world->debug.roads_ms += layer_ms; break;
                case OPENRIDE_ORMAP_RENDER_LAYER_LABELS: world->debug.labels_ms += layer_ms; break;
                default: break;
            }
        }
    }

    if (debug_enabled) {
        for (size_t i = 0U; i < world->region_count; ++i) {
            const OpenRideMapWorldRegion *region = &world->regions[i];
            if (!region->detail_visible || !region->detail_renderer) continue;
            map_world_accumulate_road_debug(&world->debug.road, &region->detail_renderer->road_debug);
            map_world_accumulate_area_debug(&world->debug.area, &region->detail_renderer->area_debug);
        }
        world->debug.ormap_stats_valid = true;
        world->debug.detail_ms +=
            (double)(SDL_GetTicksNS() - debug_started_ns) / 1000000.0;
    }
}


size_t openride_map_world_region_count'''
text, n = pattern.subn(replacement, text, count=1)
if n != 1:
    raise SystemExit(f"detail layer instrumentation: expected 1 match, got {n}")
path.write_text(text)

Path("src/map/map_zoom_test_logger.h").write_text(r'''#ifndef OPENRIDE_MAP_ZOOM_TEST_LOGGER_H
#define OPENRIDE_MAP_ZOOM_TEST_LOGGER_H

#include "map/ormap_renderer.h"
#include "openride/map_camera.h"
#include "openride/platform_paths.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPENRIDE_MAP_ZOOM_TEST_LAT 50.370800
#define OPENRIDE_MAP_ZOOM_TEST_LON 3.080200
#define OPENRIDE_MAP_ZOOM_TEST_MIN 9.0
#define OPENRIDE_MAP_ZOOM_TEST_MAX 17.0
#define OPENRIDE_MAP_ZOOM_TEST_SPEED 0.50
#define OPENRIDE_MAP_ZOOM_TEST_MAX_SAMPLES 8192U
#define OPENRIDE_MAP_ZOOM_TEST_LOG_FILENAME "map-zoom-test.csv"

typedef struct OpenRideMapZoomFrameProfile {
    double update_ms;
    double map_ms;
    double world_overview_ms;
    double world_detail_ms;
    double masks_ms;
    double areas_layer_ms;
    double waterways_ms;
    double roads_layer_ms;
    double labels_ms;
    double ui_ms;
    double present_ms;
    uint32_t visible_detail_regions;
    bool ormap_stats_valid;
} OpenRideMapZoomFrameProfile;

typedef struct OpenRideMapZoomTestSample {
    double elapsed_ms;
    double frame_ms;
    double zoom;
    int direction;
    OpenRideMapZoomFrameProfile profile;
    OpenRideORMapRoadDebugStats road;
    OpenRideORMapAreaDebugStats area;
} OpenRideMapZoomTestSample;

typedef struct OpenRideMapZoomTest {
    bool active;
    int direction;
    double zoom;
    uint64_t first_present_ns;
    uint64_t last_present_ns;
    OpenRideMapZoomTestSample *samples;
    size_t sample_count;
    size_t dropped_samples;
    char output_path[512];
} OpenRideMapZoomTest;

bool openride_map_zoom_test_start(OpenRideMapZoomTest *test, OpenRideMapCamera *camera, const OpenRidePlatformPaths *paths);
void openride_map_zoom_test_update(OpenRideMapZoomTest *test, OpenRideMapCamera *camera, double delta_seconds);
void openride_map_zoom_test_cancel(OpenRideMapZoomTest *test);
void openride_map_zoom_test_destroy(OpenRideMapZoomTest *test);
bool openride_map_zoom_test_record_present(OpenRideMapZoomTest *test, uint64_t present_ns, const OpenRideMapZoomFrameProfile *profile, const OpenRideORMapRoadDebugStats *road, const OpenRideORMapAreaDebugStats *area, char *status, size_t status_size);

#endif
''')

Path("src/map/map_zoom_test_logger.c").write_text(r'''#include "map/map_zoom_test_logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_double_ascending(const void *lhs, const void *rhs) { const double a = *(const double *)lhs; const double b = *(const double *)rhs; return a < b ? -1 : (a > b ? 1 : 0); }
static double percentile_sorted(const double *values, size_t count, double q) { if (!values || count == 0U) return 0.0; if (count == 1U) return values[0]; if (q <= 0.0) return values[0]; if (q >= 1.0) return values[count - 1U]; const double p=q*(double)(count-1U); const size_t lo=(size_t)p; const size_t hi=lo+1U<count?lo+1U:lo; const double f=p-(double)lo; return values[lo]+(values[hi]-values[lo])*f; }
static double world_total_ms(const OpenRideMapZoomFrameProfile *p) { return p ? p->world_overview_ms + p->world_detail_ms : 0.0; }
static double sample_unaccounted_ms(const OpenRideMapZoomTestSample *s) { if (!s) return 0.0; const double v=s->frame_ms-s->profile.update_ms-s->profile.map_ms-s->profile.ui_ms-s->profile.present_ms; return v>0.0?v:0.0; }
static void release_samples(OpenRideMapZoomTest *t) { if(!t)return; free(t->samples); t->samples=NULL; t->sample_count=0U; t->dropped_samples=0U; t->first_present_ns=0U; t->last_present_ns=0U; }

static bool write_results(const OpenRideMapZoomTest *test)
{
    if (!test || !test->samples || test->sample_count == 0U || test->output_path[0] == '\0') return false;
    double *frame_times = malloc(test->sample_count * sizeof(*frame_times));
    if (!frame_times) return false;
    double frame_sum_ms=0.0,max_frame_ms=0.0,max_update_ms=0.0,max_map_ms=0.0,max_world_ms=0.0,max_masks_ms=0.0,max_areas_layer_ms=0.0,max_waterways_ms=0.0,max_roads_layer_ms=0.0,max_labels_ms=0.0,max_ui_ms=0.0,max_present_ms=0.0,max_unaccounted_ms=0.0,max_road_ms=0.0,max_road_load_ms=0.0,max_area_ms=0.0,max_area_load_ms=0.0;
    size_t max_frame_index=0U;
    for(size_t i=0U;i<test->sample_count;++i){const OpenRideMapZoomTestSample*s=&test->samples[i];const double w=world_total_ms(&s->profile),u=sample_unaccounted_ms(s);frame_times[i]=s->frame_ms;frame_sum_ms+=s->frame_ms;if(s->frame_ms>max_frame_ms){max_frame_ms=s->frame_ms;max_frame_index=i;}if(s->profile.update_ms>max_update_ms)max_update_ms=s->profile.update_ms;if(s->profile.map_ms>max_map_ms)max_map_ms=s->profile.map_ms;if(w>max_world_ms)max_world_ms=w;if(s->profile.masks_ms>max_masks_ms)max_masks_ms=s->profile.masks_ms;if(s->profile.areas_layer_ms>max_areas_layer_ms)max_areas_layer_ms=s->profile.areas_layer_ms;if(s->profile.waterways_ms>max_waterways_ms)max_waterways_ms=s->profile.waterways_ms;if(s->profile.roads_layer_ms>max_roads_layer_ms)max_roads_layer_ms=s->profile.roads_layer_ms;if(s->profile.labels_ms>max_labels_ms)max_labels_ms=s->profile.labels_ms;if(s->profile.ui_ms>max_ui_ms)max_ui_ms=s->profile.ui_ms;if(s->profile.present_ms>max_present_ms)max_present_ms=s->profile.present_ms;if(u>max_unaccounted_ms)max_unaccounted_ms=u;if(s->road.roads_ms>max_road_ms)max_road_ms=s->road.roads_ms;if(s->road.load_ms>max_road_load_ms)max_road_load_ms=s->road.load_ms;if(s->area.areas_ms>max_area_ms)max_area_ms=s->area.areas_ms;if(s->area.load_ms>max_area_load_ms)max_area_load_ms=s->area.load_ms;}
    qsort(frame_times,test->sample_count,sizeof(*frame_times),compare_double_ascending);
    FILE *file=fopen(test->output_path,"wb");if(!file){free(frame_times);return false;}
    const double mean=frame_sum_ms/(double)test->sample_count;const double fps=frame_sum_ms>0.0?1000.0*(double)test->sample_count/frame_sum_ms:0.0;const OpenRideMapZoomTestSample*worst=&test->samples[max_frame_index];
    fprintf(file,"# OpenRide map zoom benchmark\n# format_version=2\n");
    fprintf(file,"# latitude=%.6f\n# longitude=%.6f\n# zoom_min=%.3f\n# zoom_max=%.3f\n# zoom_speed=%.3f\n",OPENRIDE_MAP_ZOOM_TEST_LAT,OPENRIDE_MAP_ZOOM_TEST_LON,OPENRIDE_MAP_ZOOM_TEST_MIN,OPENRIDE_MAP_ZOOM_TEST_MAX,OPENRIDE_MAP_ZOOM_TEST_SPEED);
    fprintf(file,"# samples=%zu\n# dropped_samples=%zu\n# duration_ms=%.3f\n# fps_average=%.3f\n# frame_ms_mean=%.3f\n",test->sample_count,test->dropped_samples,frame_sum_ms,fps,mean);
    fprintf(file,"# frame_ms_p50=%.3f\n# frame_ms_p75=%.3f\n# frame_ms_p90=%.3f\n# frame_ms_p95=%.3f\n# frame_ms_p99=%.3f\n# frame_ms_max=%.3f\n",percentile_sorted(frame_times,test->sample_count,0.50),percentile_sorted(frame_times,test->sample_count,0.75),percentile_sorted(frame_times,test->sample_count,0.90),percentile_sorted(frame_times,test->sample_count,0.95),percentile_sorted(frame_times,test->sample_count,0.99),max_frame_ms);
    fprintf(file,"# worst_frame=%zu\n# worst_zoom=%.5f\n# worst_direction=%d\n",max_frame_index,worst->zoom,worst->direction);
    fprintf(file,"# worst_update_ms=%.3f\n# worst_map_ms=%.3f\n# worst_world_ms=%.3f\n# worst_masks_ms=%.3f\n# worst_areas_layer_ms=%.3f\n# worst_waterways_ms=%.3f\n# worst_roads_layer_ms=%.3f\n# worst_labels_ms=%.3f\n# worst_ui_ms=%.3f\n# worst_present_ms=%.3f\n# worst_unaccounted_ms=%.3f\n",worst->profile.update_ms,worst->profile.map_ms,world_total_ms(&worst->profile),worst->profile.masks_ms,worst->profile.areas_layer_ms,worst->profile.waterways_ms,worst->profile.roads_layer_ms,worst->profile.labels_ms,worst->profile.ui_ms,worst->profile.present_ms,sample_unaccounted_ms(worst));
    fprintf(file,"# update_ms_max=%.3f\n# map_ms_max=%.3f\n# world_ms_max=%.3f\n# masks_ms_max=%.3f\n# areas_layer_ms_max=%.3f\n# waterways_ms_max=%.3f\n# roads_layer_ms_max=%.3f\n# labels_ms_max=%.3f\n# ui_ms_max=%.3f\n# present_ms_max=%.3f\n# unaccounted_ms_max=%.3f\n# road_ms_max=%.3f\n# road_load_ms_max=%.3f\n# area_ms_max=%.3f\n# area_load_ms_max=%.3f\n",max_update_ms,max_map_ms,max_world_ms,max_masks_ms,max_areas_layer_ms,max_waterways_ms,max_roads_layer_ms,max_labels_ms,max_ui_ms,max_present_ms,max_unaccounted_ms,max_road_ms,max_road_load_ms,max_area_ms,max_area_load_ms);
    fprintf(file,"frame,elapsed_ms,zoom,direction,frame_ms,update_ms,map_ms,world_ms,world_overview_ms,world_detail_ms,masks_ms,areas_layer_ms,waterways_ms,roads_layer_ms,labels_ms,ui_ms,present_ms,unaccounted_ms,visible_detail_regions,ormap_stats_valid,road_ms,road_load_ms,road_hits,road_misses,road_prewarm_loads,road_draw_loads,road_deferred_loads,road_prewarm_zoom,road_tiles,road_segments,road_batches,area_ms,area_load_ms,area_tiles,area_triangles,area_batches,area_prewarm_loads,area_draw_loads,area_deferred_loads\n");
    for(size_t i=0U;i<test->sample_count;++i){const OpenRideMapZoomTestSample*s=&test->samples[i];fprintf(file,"%zu,%.3f,%.5f,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%.3f,%.3f,%u,%u,%u,%u,%u,%d,%u,%u,%u,%.3f,%.3f,%u,%u,%u,%u,%u,%u\n",i,s->elapsed_ms,s->zoom,s->direction,s->frame_ms,s->profile.update_ms,s->profile.map_ms,world_total_ms(&s->profile),s->profile.world_overview_ms,s->profile.world_detail_ms,s->profile.masks_ms,s->profile.areas_layer_ms,s->profile.waterways_ms,s->profile.roads_layer_ms,s->profile.labels_ms,s->profile.ui_ms,s->profile.present_ms,sample_unaccounted_ms(s),(unsigned)s->profile.visible_detail_regions,s->profile.ormap_stats_valid?1U:0U,s->road.roads_ms,s->road.load_ms,(unsigned)s->road.cache_hits,(unsigned)s->road.cache_misses,(unsigned)s->road.prewarm_loads,(unsigned)s->road.draw_loads,(unsigned)s->road.deferred_loads,s->road.prewarm_zoom,(unsigned)s->road.tiles_visited,(unsigned)s->road.segments_drawn,(unsigned)s->road.batches,s->area.areas_ms,s->area.load_ms,(unsigned)s->area.tiles_visited,(unsigned)s->area.triangles_drawn,(unsigned)s->area.batches,(unsigned)s->area.prewarm_loads,(unsigned)s->area.draw_loads,(unsigned)s->area.deferred_loads);}
    const bool ok=fclose(file)==0;free(frame_times);return ok;
}

bool openride_map_zoom_test_start(OpenRideMapZoomTest*t,OpenRideMapCamera*c,const OpenRidePlatformPaths*p){if(!t||!c||!p)return false;openride_map_zoom_test_cancel(t);t->samples=calloc(OPENRIDE_MAP_ZOOM_TEST_MAX_SAMPLES,sizeof(*t->samples));if(!t->samples)return false;if(!openride_platform_path_join(t->output_path,sizeof(t->output_path),p->data_dir,OPENRIDE_MAP_ZOOM_TEST_LOG_FILENAME)){release_samples(t);return false;}(void)remove(t->output_path);t->active=true;t->direction=1;t->zoom=OPENRIDE_MAP_ZOOM_TEST_MIN;c->center_lat=OPENRIDE_MAP_ZOOM_TEST_LAT;c->center_lon=OPENRIDE_MAP_ZOOM_TEST_LON;c->zoom=t->zoom;c->bearing_deg=0.0;return true;}
void openride_map_zoom_test_update(OpenRideMapZoomTest*t,OpenRideMapCamera*c,double d){if(!t||!c||!t->active||t->direction==0)return;if(d<0.0)d=0.0;t->zoom+=(double)t->direction*OPENRIDE_MAP_ZOOM_TEST_SPEED*d;if(t->direction>0&&t->zoom>=OPENRIDE_MAP_ZOOM_TEST_MAX){t->zoom=OPENRIDE_MAP_ZOOM_TEST_MAX;t->direction=-1;}else if(t->direction<0&&t->zoom<=OPENRIDE_MAP_ZOOM_TEST_MIN){t->zoom=OPENRIDE_MAP_ZOOM_TEST_MIN;t->direction=0;}c->center_lat=OPENRIDE_MAP_ZOOM_TEST_LAT;c->center_lon=OPENRIDE_MAP_ZOOM_TEST_LON;c->zoom=t->zoom;c->bearing_deg=0.0;}
void openride_map_zoom_test_cancel(OpenRideMapZoomTest*t){if(!t)return;release_samples(t);t->active=false;t->direction=0;t->zoom=OPENRIDE_MAP_ZOOM_TEST_MIN;}
void openride_map_zoom_test_destroy(OpenRideMapZoomTest*t){openride_map_zoom_test_cancel(t);}
bool openride_map_zoom_test_record_present(OpenRideMapZoomTest*t,uint64_t ns,const OpenRideMapZoomFrameProfile*p,const OpenRideORMapRoadDebugStats*r,const OpenRideORMapAreaDebugStats*a,char*status,size_t status_size){if(!t||!t->active)return false;if(t->last_present_ns==0U){t->first_present_ns=ns;t->last_present_ns=ns;}else{if(t->sample_count<OPENRIDE_MAP_ZOOM_TEST_MAX_SAMPLES){OpenRideMapZoomTestSample*s=&t->samples[t->sample_count++];s->elapsed_ms=(double)(ns-t->first_present_ns)/1000000.0;s->frame_ms=(double)(ns-t->last_present_ns)/1000000.0;s->zoom=t->zoom;s->direction=t->direction;if(p)s->profile=*p;if(r)s->road=*r;if(a)s->area=*a;}else ++t->dropped_samples;t->last_present_ns=ns;}if(t->direction!=0)return false;const bool written=write_results(t);if(status&&status_size>0U){if(written)snprintf(status,status_size,"test zoom termine: data/%s (%zu frames)",OPENRIDE_MAP_ZOOM_TEST_LOG_FILENAME,t->sample_count);else snprintf(status,status_size,"test zoom termine mais log impossible (%zu frames)",t->sample_count);}release_samples(t);t->active=false;return true;}
''')

path = Path("src/main.c")
text = path.read_text()
text = replace_once(text, '''    while (running) {
        SDL_Event event;
''', '''    while (running) {
        uint64_t map_zoom_loop_started_ns =
            map_zoom_test.active ? SDL_GetTicksNS() : 0U;
        SDL_Event event;
''', "main loop start timing")
start_call='openride_map_zoom_test_start(&map_zoom_test, &camera, &platform_paths);'
start_replacement='''openride_map_zoom_test_start(&map_zoom_test, &camera, &platform_paths);
                                    map_zoom_loop_started_ns = SDL_GetTicksNS();'''
text=replace_count(text,start_call,start_replacement,2,"benchmark starts")
text=replace_once(text,'''        const bool world_available = map_world
            && openride_map_world_region_count(map_world) > 0U;
        const bool world_overview_only = world_available
            && camera.zoom < OPENRIDE_MAP_WORLD_DETAIL_ZOOM;
        if (world_overview_only) {
''','''        const bool world_available = map_world
            && openride_map_world_region_count(map_world) > 0U;
        const bool world_overview_only = world_available
            && camera.zoom < OPENRIDE_MAP_WORLD_DETAIL_ZOOM;
        OpenRideMapZoomFrameProfile map_zoom_profile;
        OpenRideMapWorldDebugStats map_zoom_world_debug;
        memset(&map_zoom_profile, 0, sizeof(map_zoom_profile));
        memset(&map_zoom_world_debug, 0, sizeof(map_zoom_world_debug));
        map_zoom_world_debug.road.prewarm_zoom = -1;
        if (map_zoom_test.active && map_world) openride_map_world_debug_begin_frame(map_world);
        const uint64_t map_zoom_map_started_ns = map_zoom_test.active ? SDL_GetTicksNS() : 0U;
        if (world_overview_only) {
''',"map profile start")
text=replace_once(text,'''            }
        }

        if (gpx_loaded) {
''','''            }
        }
        const uint64_t map_zoom_map_finished_ns = map_zoom_test.active ? SDL_GetTicksNS() : 0U;
        if (map_zoom_test.active) {
            if (map_zoom_loop_started_ns != 0U) map_zoom_profile.update_ms=(double)(map_zoom_map_started_ns-map_zoom_loop_started_ns)/1000000.0;
            map_zoom_profile.map_ms=(double)(map_zoom_map_finished_ns-map_zoom_map_started_ns)/1000000.0;
            if (map_world) {
                openride_map_world_get_debug_stats(map_world,&map_zoom_world_debug);
                map_zoom_profile.world_overview_ms=map_zoom_world_debug.overview_ms;
                map_zoom_profile.world_detail_ms=map_zoom_world_debug.detail_ms;
                map_zoom_profile.masks_ms=map_zoom_world_debug.masks_ms;
                map_zoom_profile.areas_layer_ms=map_zoom_world_debug.areas_ms;
                map_zoom_profile.waterways_ms=map_zoom_world_debug.waterways_ms;
                map_zoom_profile.roads_layer_ms=map_zoom_world_debug.roads_ms;
                map_zoom_profile.labels_ms=map_zoom_world_debug.labels_ms;
                map_zoom_profile.visible_detail_regions=map_zoom_world_debug.visible_detail_regions;
                map_zoom_profile.ormap_stats_valid=map_zoom_world_debug.ormap_stats_valid;
            }
        }

        if (gpx_loaded) {
''',"map profile finish")
text=replace_once(text,'''            if (ormap_map && renderer_initialized) {
                openride_ormap_renderer_get_road_debug_stats(&ormap_renderer,
                                                              &road_debug);
                openride_ormap_renderer_get_area_debug_stats(&ormap_renderer,
                                                              &area_debug);
            }
''','''            if (map_zoom_world_debug.ormap_stats_valid) {
                road_debug = map_zoom_world_debug.road;
                area_debug = map_zoom_world_debug.area;
            } else if (!world_available && ormap_map && renderer_initialized) {
                openride_ormap_renderer_get_road_debug_stats(&ormap_renderer,
                                                              &road_debug);
                openride_ormap_renderer_get_area_debug_stats(&ormap_renderer,
                                                              &area_debug);
            }
''',"HUD current renderer stats")
text=replace_once(text,'''        SDL_RenderPresent(renderer);

        /* Benchmark timing is captured immediately after Present, before any
         * logging work. Samples stay in RAM and are written only after the
         * final z9 frame, so instrumentation does not add filesystem I/O to
         * the measured frames. */
        if (map_zoom_test.active) {
            const uint64_t map_zoom_present_ns = SDL_GetTicksNS();
            OpenRideORMapRoadDebugStats map_zoom_road_debug;
            OpenRideORMapAreaDebugStats map_zoom_area_debug;
            memset(&map_zoom_road_debug, 0, sizeof(map_zoom_road_debug));
            memset(&map_zoom_area_debug, 0, sizeof(map_zoom_area_debug));
            map_zoom_road_debug.prewarm_zoom = -1;
            if (ormap_map && renderer_initialized) {
                openride_ormap_renderer_get_road_debug_stats(
                    &ormap_renderer, &map_zoom_road_debug);
                openride_ormap_renderer_get_area_debug_stats(
                    &ormap_renderer, &map_zoom_area_debug);
            }
            (void)openride_map_zoom_test_record_present(
                &map_zoom_test,
                map_zoom_present_ns,
                &map_zoom_road_debug,
                &map_zoom_area_debug,
                route_status,
                sizeof(route_status));
        }
''','''        const uint64_t map_zoom_ui_finished_ns = map_zoom_test.active ? SDL_GetTicksNS() : 0U;
        if (map_zoom_test.active) map_zoom_profile.ui_ms=(double)(map_zoom_ui_finished_ns-map_zoom_map_finished_ns)/1000000.0;
        SDL_RenderPresent(renderer);
        if (map_zoom_test.active) {
            const uint64_t map_zoom_present_ns=SDL_GetTicksNS();
            map_zoom_profile.present_ms=(double)(map_zoom_present_ns-map_zoom_ui_finished_ns)/1000000.0;
            OpenRideORMapRoadDebugStats map_zoom_road_debug;OpenRideORMapAreaDebugStats map_zoom_area_debug;
            memset(&map_zoom_road_debug,0,sizeof(map_zoom_road_debug));memset(&map_zoom_area_debug,0,sizeof(map_zoom_area_debug));map_zoom_road_debug.prewarm_zoom=-1;
            if(map_zoom_world_debug.ormap_stats_valid){map_zoom_road_debug=map_zoom_world_debug.road;map_zoom_area_debug=map_zoom_world_debug.area;}
            else if(!world_available&&ormap_map&&renderer_initialized){openride_ormap_renderer_get_road_debug_stats(&ormap_renderer,&map_zoom_road_debug);openride_ormap_renderer_get_area_debug_stats(&ormap_renderer,&map_zoom_area_debug);map_zoom_profile.ormap_stats_valid=true;}
            if(map_world)openride_map_world_debug_end_frame(map_world);
            (void)openride_map_zoom_test_record_present(&map_zoom_test,map_zoom_present_ns,&map_zoom_profile,&map_zoom_road_debug,&map_zoom_area_debug,route_status,sizeof(route_status));
        }
''',"present/profile recording")
path.write_text(text)
print("benchmark profiling V2 patch applied")
