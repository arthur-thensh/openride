#ifndef OPENRIDE_MAP_STYLE_H
#define OPENRIDE_MAP_STYLE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Pure-C map styling policy. No SDL dependency.
 *
 * Keeping visibility decisions here makes the cartographic rules testable and
 * reusable later on Android/iOS without coupling them to the renderer.
 */
bool openride_map_place_label_visible(const char *kind,
                                      int64_t population,
                                      double zoom);

int openride_map_place_label_priority(const char *kind,
                                      int64_t population);

bool openride_map_road_visible(const char *kind, double zoom);

#endif
