#include "openride/ui.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static float ui_clampf(float value, float min_value, float max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static OpenRideUIColor ui_color(uint8_t r,
                                uint8_t g,
                                uint8_t b,
                                uint8_t a)
{
    OpenRideUIColor color = {r, g, b, a};
    return color;
}

static OpenRideUIColor ui_color_scale(OpenRideUIColor color, float factor)
{
    color.r = (uint8_t)ui_clampf((float)color.r * factor, 0.0f, 255.0f);
    color.g = (uint8_t)ui_clampf((float)color.g * factor, 0.0f, 255.0f);
    color.b = (uint8_t)ui_clampf((float)color.b * factor, 0.0f, 255.0f);
    return color;
}

static void ui_set_draw_color(SDL_Renderer *renderer, OpenRideUIColor color)
{
    if (!renderer) return;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

static size_t ui_utf8_glyph_count(const char *text)
{
    if (!text) return 0U;
    size_t count = 0U;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) {
        if ((*cursor & 0xc0U) != 0x80U) ++count;
        ++cursor;
    }
    return count;
}

static void ui_draw_scaled_text(SDL_Renderer *renderer,
                                float x,
                                float y,
                                float scale,
                                const char *text)
{
    if (!renderer || !text || !text[0] || scale <= 0.0f) return;
    float old_x = 1.0f;
    float old_y = 1.0f;
    if (!SDL_GetRenderScale(renderer, &old_x, &old_y)) return;
    if (!SDL_SetRenderScale(renderer, old_x * scale, old_y * scale)) return;
    SDL_RenderDebugText(renderer, x / scale, y / scale, text);
    SDL_SetRenderScale(renderer, old_x, old_y);
}

static float ui_text_style_scale(const OpenRideUIContext *ui,
                                 OpenRideUITextStyle style)
{
    if (!ui) return 1.0f;
    float multiplier = 1.0f;
    switch (style) {
        case OPENRIDE_UI_TEXT_TITLE:
            multiplier = 1.30f;
            break;
        case OPENRIDE_UI_TEXT_CAPTION:
            multiplier = 0.86f;
            break;
        case OPENRIDE_UI_TEXT_BODY:
        default:
            break;
    }
    return ui_clampf(ui->text_scale * multiplier, 1.0f, 3.2f);
}

OpenRideUIID openride_ui_id(const char *text)
{
    if (!text) return 0U;
    uint32_t hash = UINT32_C(2166136261);
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) {
        hash ^= (uint32_t)*cursor++;
        hash *= UINT32_C(16777619);
    }
    return hash == 0U ? 1U : hash;
}

OpenRideUITheme openride_ui_theme_default(void)
{
    OpenRideUITheme theme;
    memset(&theme, 0, sizeof(theme));
    theme.background = ui_color(13U, 17U, 21U, 255U);
    theme.surface = ui_color(24U, 29U, 34U, 242U);
    theme.surface_elevated = ui_color(32U, 38U, 44U, 248U);
    theme.primary = ui_color(42U, 105U, 154U, 255U);
    theme.primary_pressed = ui_color(34U, 86U, 126U, 255U);
    theme.text = ui_color(244U, 247U, 249U, 255U);
    theme.text_secondary = ui_color(168U, 178U, 186U, 255U);
    theme.border = ui_color(255U, 255U, 255U, 58U);
    theme.danger = ui_color(148U, 67U, 67U, 255U);
    theme.disabled = ui_color(86U, 94U, 101U, 220U);
    theme.spacing_xs = 4.0f;
    theme.spacing_sm = 8.0f;
    theme.spacing_md = 12.0f;
    theme.spacing_lg = 18.0f;
    theme.radius_sm = 6.0f;
    theme.radius_md = 10.0f;
    theme.touch_target = 48.0f;
    theme.button_height = 56.0f;
    return theme;
}

void openride_ui_init(OpenRideUIContext *ui)
{
    if (!ui) return;
    memset(ui, 0, sizeof(*ui));
    ui->scale = 1.0f;
    ui->text_scale = 1.0f;
    ui->theme = openride_ui_theme_default();
}

bool openride_ui_begin(OpenRideUIContext *ui,
                       SDL_Renderer *renderer,
                       int viewport_width,
                       int viewport_height)
{
    if (!ui || !renderer || viewport_width <= 0 || viewport_height <= 0) {
        return false;
    }

    ui->renderer = renderer;
    ui->viewport_width = viewport_width;
    ui->viewport_height = viewport_height;
    ui->hot_id = 0U;
    ui->pointer_pressed = false;
    ui->pointer_released = false;
    ui->pointer_consumed = false;

    float scale = 1.0f;
    SDL_Window *window = SDL_GetRenderWindow(renderer);
    if (window) {
        const float queried = SDL_GetWindowDisplayScale(window);
        if (queried > 0.0f && isfinite(queried)) scale = queried;
    }
    ui->scale = ui_clampf(scale, 1.0f, 3.0f);
    ui->text_scale = ui_clampf(ui->scale, 1.0f, 2.5f);

    SDL_Rect safe = {0, 0, viewport_width, viewport_height};
    SDL_Rect queried_safe = {0};
    if (SDL_GetRenderSafeArea(renderer, &queried_safe)
        && queried_safe.w > 0
        && queried_safe.h > 0) {
        safe = queried_safe;
    }
    ui->safe_area_px = safe;
    ui->safe_area = openride_ui_rect((float)safe.x / ui->scale,
                                     (float)safe.y / ui->scale,
                                     (float)safe.w / ui->scale,
                                     (float)safe.h / ui->scale);
    return true;
}

void openride_ui_end(OpenRideUIContext *ui)
{
    if (!ui) return;
    if (ui->pointer_released) ui->active_id = 0U;
}

static void ui_pointer_position(OpenRideUIContext *ui, double x_px, double y_px)
{
    if (!ui) return;
    const double scale = ui->scale > 0.0f ? (double)ui->scale : 1.0;
    ui->pointer_x = x_px / scale;
    ui->pointer_y = y_px / scale;
}

void openride_ui_pointer_move(OpenRideUIContext *ui, double x_px, double y_px)
{
    ui_pointer_position(ui, x_px, y_px);
}

void openride_ui_pointer_down(OpenRideUIContext *ui, double x_px, double y_px)
{
    if (!ui) return;
    ui_pointer_position(ui, x_px, y_px);
    ui->pointer_down = true;
    ui->pointer_pressed = true;
}

void openride_ui_pointer_up(OpenRideUIContext *ui, double x_px, double y_px)
{
    if (!ui) return;
    ui_pointer_position(ui, x_px, y_px);
    ui->pointer_down = false;
    ui->pointer_released = true;
}

void openride_ui_pointer_cancel(OpenRideUIContext *ui)
{
    if (!ui) return;
    ui->pointer_down = false;
    ui->pointer_pressed = false;
    ui->pointer_released = false;
    ui->pointer_consumed = false;
    ui->hot_id = 0U;
    ui->active_id = 0U;
}

bool openride_ui_pointer_consumed(const OpenRideUIContext *ui)
{
    return ui && ui->pointer_consumed;
}

OpenRideUIRect openride_ui_safe_rect(const OpenRideUIContext *ui)
{
    return ui ? ui->safe_area : (OpenRideUIRect){0};
}

OpenRideUIRect openride_ui_rect(float x, float y, float w, float h)
{
    OpenRideUIRect rect = {x, y, w, h};
    return rect;
}

OpenRideUIRect openride_ui_inset_xy(OpenRideUIRect rect, float x, float y)
{
    rect.x += x;
    rect.y += y;
    rect.w -= x * 2.0f;
    rect.h -= y * 2.0f;
    if (rect.w < 0.0f) rect.w = 0.0f;
    if (rect.h < 0.0f) rect.h = 0.0f;
    return rect;
}

OpenRideUIRect openride_ui_inset(OpenRideUIRect rect, float amount)
{
    return openride_ui_inset_xy(rect, amount, amount);
}

bool openride_ui_point_in_rect(double x, double y, OpenRideUIRect rect)
{
    return rect.w > 0.0f && rect.h > 0.0f
        && x >= (double)rect.x
        && y >= (double)rect.y
        && x < (double)(rect.x + rect.w)
        && y < (double)(rect.y + rect.h);
}

SDL_FRect openride_ui_rect_pixels(const OpenRideUIContext *ui,
                                  OpenRideUIRect rect)
{
    const float scale = ui && ui->scale > 0.0f ? ui->scale : 1.0f;
    SDL_FRect result = {
        rect.x * scale,
        rect.y * scale,
        rect.w * scale,
        rect.h * scale
    };
    return result;
}

void openride_ui_column_begin(OpenRideUILayout *layout,
                              OpenRideUIRect bounds,
                              float gap)
{
    if (!layout) return;
    layout->bounds = bounds;
    layout->cursor = bounds.y;
    layout->gap = gap > 0.0f ? gap : 0.0f;
}

OpenRideUIRect openride_ui_column_next(OpenRideUILayout *layout, float height)
{
    if (!layout || height <= 0.0f) return (OpenRideUIRect){0};
    const float bottom = layout->bounds.y + layout->bounds.h;
    if (layout->cursor >= bottom) return (OpenRideUIRect){0};
    float available = bottom - layout->cursor;
    if (height > available) height = available;
    OpenRideUIRect rect = {
        layout->bounds.x,
        layout->cursor,
        layout->bounds.w,
        height
    };
    layout->cursor += height + layout->gap;
    return rect;
}

OpenRideUIRect openride_ui_column_remaining(const OpenRideUILayout *layout)
{
    if (!layout) return (OpenRideUIRect){0};
    const float bottom = layout->bounds.y + layout->bounds.h;
    const float height = bottom > layout->cursor ? bottom - layout->cursor : 0.0f;
    return openride_ui_rect(layout->bounds.x,
                            layout->cursor,
                            layout->bounds.w,
                            height);
}

void openride_ui_panel(OpenRideUIContext *ui,
                       OpenRideUIRect rect,
                       bool elevated)
{
    if (!ui || !ui->renderer || rect.w <= 0.0f || rect.h <= 0.0f) return;
    const SDL_FRect pixels = openride_ui_rect_pixels(ui, rect);
    ui_set_draw_color(ui->renderer,
                      elevated ? ui->theme.surface_elevated : ui->theme.surface);
    SDL_RenderFillRect(ui->renderer, &pixels);
    ui_set_draw_color(ui->renderer, ui->theme.border);
    SDL_RenderRect(ui->renderer, &pixels);
}

void openride_ui_text(OpenRideUIContext *ui,
                      OpenRideUIRect rect,
                      const char *text,
                      OpenRideUITextStyle style,
                      OpenRideUITextAlign align)
{
    if (!ui || !ui->renderer || !text || !text[0]
        || rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }

    const float scale = ui_text_style_scale(ui, style);
    const float width = (float)ui_utf8_glyph_count(text) * 8.0f * scale;
    const float height = 8.0f * scale;
    const SDL_FRect pixels = openride_ui_rect_pixels(ui, rect);
    float x = pixels.x;
    if (align == OPENRIDE_UI_TEXT_ALIGN_CENTER) {
        x += (pixels.w - width) * 0.5f;
    } else if (align == OPENRIDE_UI_TEXT_ALIGN_RIGHT) {
        x += pixels.w - width;
    }
    const float y = pixels.y + (pixels.h - height) * 0.5f;
    ui_set_draw_color(ui->renderer,
                      style == OPENRIDE_UI_TEXT_CAPTION
                          ? ui->theme.text_secondary
                          : ui->theme.text);
    ui_draw_scaled_text(ui->renderer, x, y, scale, text);
}

bool openride_ui_button(OpenRideUIContext *ui,
                        OpenRideUIID id,
                        OpenRideUIRect rect,
                        const char *label,
                        OpenRideUIButtonStyle style,
                        bool enabled,
                        bool selected)
{
    if (!ui || !ui->renderer || id == 0U
        || rect.w <= 0.0f || rect.h <= 0.0f) {
        return false;
    }

    const bool inside = openride_ui_point_in_rect(ui->pointer_x,
                                                  ui->pointer_y,
                                                  rect);
    if (inside && enabled) ui->hot_id = id;

    if (enabled && inside && ui->pointer_pressed) {
        ui->active_id = id;
        ui->pointer_consumed = true;
    }

    const bool active = ui->active_id == id;
    if (active && ui->pointer_down) ui->pointer_consumed = true;

    bool clicked = false;
    if (active && ui->pointer_released) {
        ui->pointer_consumed = true;
        clicked = enabled && inside;
    }

    OpenRideUIColor fill = ui->theme.surface_elevated;
    OpenRideUIColor border = ui->theme.border;
    OpenRideUIColor text = ui->theme.text;

    if (!enabled) {
        fill = ui->theme.disabled;
        text = ui_color_scale(ui->theme.text_secondary, 0.85f);
    } else {
        switch (style) {
            case OPENRIDE_UI_BUTTON_PRIMARY:
                fill = active ? ui->theme.primary_pressed : ui->theme.primary;
                break;
            case OPENRIDE_UI_BUTTON_DANGER:
                fill = active
                    ? ui_color_scale(ui->theme.danger, 0.82f)
                    : ui->theme.danger;
                break;
            case OPENRIDE_UI_BUTTON_GHOST:
                fill = ui->theme.surface;
                fill.a = selected || active || inside ? 150U : 40U;
                break;
            case OPENRIDE_UI_BUTTON_SECONDARY:
            default:
                if (selected) fill = ui_color_scale(ui->theme.primary, 0.78f);
                else if (active) fill = ui_color_scale(fill, 0.80f);
                else if (inside) fill = ui_color_scale(fill, 1.12f);
                break;
        }
    }

    const SDL_FRect pixels = openride_ui_rect_pixels(ui, rect);
    ui_set_draw_color(ui->renderer, fill);
    SDL_RenderFillRect(ui->renderer, &pixels);
    ui_set_draw_color(ui->renderer, border);
    SDL_RenderRect(ui->renderer, &pixels);

    if (label && label[0]) {
        float text_scale = ui->text_scale;
        const float natural_width =
            (float)ui_utf8_glyph_count(label) * 8.0f * text_scale;
        const float max_width = pixels.w - 24.0f * ui->scale;
        if (natural_width > max_width && natural_width > 0.0f) {
            text_scale *= max_width / natural_width;
            if (text_scale < 1.0f) text_scale = 1.0f;
        }
        const float text_width =
            (float)ui_utf8_glyph_count(label) * 8.0f * text_scale;
        const float text_height = 8.0f * text_scale;
        ui_set_draw_color(ui->renderer, text);
        ui_draw_scaled_text(ui->renderer,
                            pixels.x + (pixels.w - text_width) * 0.5f,
                            pixels.y + (pixels.h - text_height) * 0.5f,
                            text_scale,
                            label);
    }

    return clicked;
}

