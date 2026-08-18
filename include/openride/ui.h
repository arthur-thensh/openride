#ifndef OPENRIDE_UI_H
#define OPENRIDE_UI_H

#include <SDL3/SDL.h>

#ifdef __ANDROID__
#include "openride/drive_perspective_renderer.h"
/* The first UI blend setup in a Drive frame happens after map, route and rider
 * rendering. Present the offscreen 2.5D scene at that exact boundary; later UI
 * blend changes are ordinary pass-through calls because capture is then off. */
#define SDL_SetRenderDrawBlendMode openride_drive_perspective_set_blend_mode
#endif

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t OpenRideUIID;

typedef struct OpenRideUIColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} OpenRideUIColor;

typedef struct OpenRideUIRect {
    float x;
    float y;
    float w;
    float h;
} OpenRideUIRect;

typedef struct OpenRideUITheme {
    OpenRideUIColor background;
    OpenRideUIColor surface;
    OpenRideUIColor surface_elevated;
    OpenRideUIColor primary;
    OpenRideUIColor primary_pressed;
    OpenRideUIColor primary_soft;
    OpenRideUIColor text;
    OpenRideUIColor text_secondary;
    OpenRideUIColor border;
    OpenRideUIColor danger;
    OpenRideUIColor success;
    OpenRideUIColor disabled;

    float spacing_xs;
    float spacing_sm;
    float spacing_md;
    float spacing_lg;
    float radius_sm;
    float radius_md;
    float radius_lg;
    float touch_target;
    float button_height;
    float icon_size;
} OpenRideUITheme;

typedef enum OpenRideUITextStyle {
    OPENRIDE_UI_TEXT_BODY = 0,
    OPENRIDE_UI_TEXT_TITLE,
    OPENRIDE_UI_TEXT_CAPTION
} OpenRideUITextStyle;

typedef enum OpenRideUITextAlign {
    OPENRIDE_UI_TEXT_ALIGN_LEFT = 0,
    OPENRIDE_UI_TEXT_ALIGN_CENTER,
    OPENRIDE_UI_TEXT_ALIGN_RIGHT
} OpenRideUITextAlign;

typedef enum OpenRideUIButtonStyle {
    OPENRIDE_UI_BUTTON_SECONDARY = 0,
    OPENRIDE_UI_BUTTON_PRIMARY,
    OPENRIDE_UI_BUTTON_DANGER,
    OPENRIDE_UI_BUTTON_GHOST
} OpenRideUIButtonStyle;

typedef struct OpenRideUILayout {
    OpenRideUIRect bounds;
    float cursor;
    float gap;
} OpenRideUILayout;

typedef struct OpenRideUIContext {
    SDL_Renderer *renderer;
    int viewport_width;
    int viewport_height;

    SDL_Rect safe_area_px;
    OpenRideUIRect safe_area;
    float scale;
    float text_scale;
    OpenRideUITheme theme;

    double pointer_x;
    double pointer_y;
    bool pointer_down;
    bool pointer_pressed;
    bool pointer_released;
    bool pointer_consumed;

    OpenRideUIID hot_id;
    OpenRideUIID active_id;
} OpenRideUIContext;

#define OPENRIDE_UI_ID(text) openride_ui_id(text)

OpenRideUIID openride_ui_id(const char *text);
OpenRideUITheme openride_ui_theme_default(void);

void openride_ui_init(OpenRideUIContext *ui);
bool openride_ui_begin(OpenRideUIContext *ui,
                       SDL_Renderer *renderer,
                       int viewport_width,
                       int viewport_height);
void openride_ui_end(OpenRideUIContext *ui);

void openride_ui_pointer_move(OpenRideUIContext *ui, double x_px, double y_px);
void openride_ui_pointer_down(OpenRideUIContext *ui, double x_px, double y_px);
void openride_ui_pointer_up(OpenRideUIContext *ui, double x_px, double y_px);
void openride_ui_pointer_cancel(OpenRideUIContext *ui);
bool openride_ui_pointer_consumed(const OpenRideUIContext *ui);

OpenRideUIRect openride_ui_safe_rect(const OpenRideUIContext *ui);
OpenRideUIRect openride_ui_rect(float x, float y, float w, float h);
OpenRideUIRect openride_ui_inset(OpenRideUIRect rect, float amount);
OpenRideUIRect openride_ui_inset_xy(OpenRideUIRect rect, float x, float y);
bool openride_ui_point_in_rect(double x, double y, OpenRideUIRect rect);
SDL_FRect openride_ui_rect_pixels(const OpenRideUIContext *ui,
                                  OpenRideUIRect rect);

void openride_ui_column_begin(OpenRideUILayout *layout,
                              OpenRideUIRect bounds,
                              float gap);
OpenRideUIRect openride_ui_column_next(OpenRideUILayout *layout, float height);
OpenRideUIRect openride_ui_column_remaining(const OpenRideUILayout *layout);

void openride_ui_panel(OpenRideUIContext *ui,
                       OpenRideUIRect rect,
                       bool elevated);
void openride_ui_text(OpenRideUIContext *ui,
                      OpenRideUIRect rect,
                      const char *text,
                      OpenRideUITextStyle style,
                      OpenRideUITextAlign align);
void openride_ui_text_color(OpenRideUIContext *ui,
                            OpenRideUIRect rect,
                            const char *text,
                            OpenRideUITextStyle style,
                            OpenRideUITextAlign align,
                            OpenRideUIColor color);
bool openride_ui_button(OpenRideUIContext *ui,
                        OpenRideUIID id,
                        OpenRideUIRect rect,
                        const char *label,
                        OpenRideUIButtonStyle style,
                        bool enabled,
                        bool selected);

#endif
