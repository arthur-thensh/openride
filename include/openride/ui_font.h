#ifndef OPENRIDE_UI_FONT_H
#define OPENRIDE_UI_FONT_H

#include "openride/ui.h"

#include <stdbool.h>

/*
 * OpenRide UI text renderer.
 *
 * Font data is generated locally by scripts/bootstrap_ui_font.sh and embedded
 * into the application as C data. Runtime rendering is therefore completely
 * offline and identical on desktop, Android and future iOS builds.
 */

float openride_ui_font_measure_width(const char *text, float pixel_height);
float openride_ui_font_line_height(float pixel_height);

bool openride_ui_font_draw(SDL_Renderer *renderer,
                           float x,
                           float y,
                           float pixel_height,
                           const char *text,
                           OpenRideUIColor color);

#endif
