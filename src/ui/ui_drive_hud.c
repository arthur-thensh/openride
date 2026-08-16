#include "openride/ui_drive_hud.h"
#include "openride/ui_icon.h"
#include "openride/ui_font.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define OPENRIDE_UI_DRIVE_MARGIN 10.0f
#define OPENRIDE_UI_DRIVE_TOP_HEIGHT 78.0f
#define OPENRIDE_UI_DRIVE_FOLLOWING_HEIGHT 26.0f
#define OPENRIDE_UI_DRIVE_STATS_HEIGHT 52.0f
#define OPENRIDE_UI_DRIVE_CONTROLS_HEIGHT 64.0f
#define OPENRIDE_UI_DRIVE_ATTRIBUTION_HEIGHT 14.0f
#define OPENRIDE_UI_DRIVE_CONTROL_COUNT 4U
#define OPENRIDE_UI_FONT_COMPAT_HEIGHT 10.5f

static void drive_draw_scaled_text(SDL_Renderer *renderer,
                                   float x,
                                   float y,
                                   float scale,
                                   const char *text)
{
    if (!renderer || !text || !text[0] || scale <= 0.0f) return;
    Uint8 r = 255U;
    Uint8 g = 255U;
    Uint8 b = 255U;
    Uint8 a = 255U;
    SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
    const OpenRideUIColor color = {r, g, b, a};
    (void)openride_ui_font_draw(renderer,
                                x,
                                y,
                                OPENRIDE_UI_FONT_COMPAT_HEIGHT * scale,
                                text,
                                color);
}

static float drive_fit_scale(const char *text,
                             float requested_scale,
                             float max_width)
{
    if (!text || !text[0] || requested_scale <= 0.0f || max_width <= 0.0f) {
        return requested_scale;
    }
    const float natural = openride_ui_font_measure_width(
        text, OPENRIDE_UI_FONT_COMPAT_HEIGHT * requested_scale);
    if (natural <= max_width || natural <= 0.0f) return requested_scale;
    float fitted = requested_scale * max_width / natural;
    if (fitted < 1.0f) fitted = 1.0f;
    return fitted;
}

static void drive_draw_text_fit(SDL_Renderer *renderer,
                                float x,
                                float y,
                                float max_width,
                                float requested_scale,
                                const char *text)
{
    drive_draw_scaled_text(renderer,
                           x,
                           y,
                           drive_fit_scale(text, requested_scale, max_width),
                           text);
}

static void drive_draw_thick_line(SDL_Renderer *renderer,
                                  float x1,
                                  float y1,
                                  float x2,
                                  float y2,
                                  float thickness)
{
    if (!renderer) return;
    int width = (int)ceilf(thickness);
    if (width < 1) width = 1;
    if (width == 1) {
        SDL_RenderLine(renderer, x1, y1, x2, y2);
        return;
    }

    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.001f) return;
    const float nx = -dy / length;
    const float ny = dx / length;
    const float half = ((float)width - 1.0f) * 0.5f;
    for (int i = 0; i < width; ++i) {
        const float offset = (float)i - half;
        SDL_RenderLine(renderer,
                       x1 + nx * offset,
                       y1 + ny * offset,
                       x2 + nx * offset,
                       y2 + ny * offset);
    }
}

static void drive_draw_maneuver_icon(SDL_Renderer *renderer,
                                     OpenRideUIDriveHUDManeuver maneuver,
                                     float x,
                                     float y,
                                     float size,
                                     float thickness)
{
    if (!renderer || size <= 0.0f) return;
    const float cx = x + size * 0.5f;
    const float top = y + size * 0.12f;
    const float bottom = y + size * 0.88f;
    const float left = x + size * 0.18f;
    const float right = x + size * 0.82f;
    const float mid = y + size * 0.48f;

    SDL_SetRenderDrawColor(renderer, 47, 198, 181, 255);
    switch (maneuver) {
        case OPENRIDE_UI_DRIVE_MANEUVER_LEFT:
        case OPENRIDE_UI_DRIVE_MANEUVER_SHARP_LEFT:
        case OPENRIDE_UI_DRIVE_MANEUVER_SLIGHT_LEFT:
            drive_draw_thick_line(renderer, cx, bottom, cx, mid, thickness);
            drive_draw_thick_line(renderer, cx, mid, left, mid, thickness);
            drive_draw_thick_line(renderer,
                                  left,
                                  mid,
                                  left + size * 0.18f,
                                  mid - size * 0.18f,
                                  thickness);
            drive_draw_thick_line(renderer,
                                  left,
                                  mid,
                                  left + size * 0.18f,
                                  mid + size * 0.18f,
                                  thickness);
            break;
        case OPENRIDE_UI_DRIVE_MANEUVER_RIGHT:
        case OPENRIDE_UI_DRIVE_MANEUVER_SHARP_RIGHT:
        case OPENRIDE_UI_DRIVE_MANEUVER_SLIGHT_RIGHT:
            drive_draw_thick_line(renderer, cx, bottom, cx, mid, thickness);
            drive_draw_thick_line(renderer, cx, mid, right, mid, thickness);
            drive_draw_thick_line(renderer,
                                  right,
                                  mid,
                                  right - size * 0.18f,
                                  mid - size * 0.18f,
                                  thickness);
            drive_draw_thick_line(renderer,
                                  right,
                                  mid,
                                  right - size * 0.18f,
                                  mid + size * 0.18f,
                                  thickness);
            break;
        case OPENRIDE_UI_DRIVE_MANEUVER_UTURN:
            drive_draw_thick_line(renderer,
                                  cx + size * 0.18f,
                                  bottom,
                                  cx + size * 0.18f,
                                  mid,
                                  thickness);
            drive_draw_thick_line(renderer,
                                  cx + size * 0.18f,
                                  mid,
                                  cx - size * 0.17f,
                                  mid,
                                  thickness);
            drive_draw_thick_line(renderer,
                                  cx - size * 0.17f,
                                  mid,
                                  cx - size * 0.17f,
                                  top + size * 0.18f,
                                  thickness);
            drive_draw_thick_line(renderer,
                                  cx - size * 0.17f,
                                  top + size * 0.18f,
                                  cx + size * 0.02f,
                                  top + size * 0.35f,
                                  thickness);
            drive_draw_thick_line(renderer,
                                  cx - size * 0.17f,
                                  top + size * 0.18f,
                                  cx - size * 0.34f,
                                  top + size * 0.35f,
                                  thickness);
            break;
        case OPENRIDE_UI_DRIVE_MANEUVER_ROUNDABOUT: {
            const float radius = size * 0.23f;
            const int segments = 18;
            const float tau = 6.28318530717958647692f;
            for (int i = 0; i < segments; ++i) {
                const float a0 = (float)i * (tau / (float)segments);
                const float a1 = (float)(i + 1) * (tau / (float)segments);
                drive_draw_thick_line(renderer,
                                      cx + cosf(a0) * radius,
                                      mid + sinf(a0) * radius,
                                      cx + cosf(a1) * radius,
                                      mid + sinf(a1) * radius,
                                      thickness);
            }
            drive_draw_thick_line(renderer,
                                  cx,
                                  bottom,
                                  cx,
                                  mid + radius,
                                  thickness);
            drive_draw_thick_line(renderer,
                                  cx + radius,
                                  mid,
                                  right,
                                  mid,
                                  thickness);
            drive_draw_thick_line(renderer,
                                  right,
                                  mid,
                                  right - size * 0.15f,
                                  mid - size * 0.14f,
                                  thickness);
            drive_draw_thick_line(renderer,
                                  right,
                                  mid,
                                  right - size * 0.15f,
                                  mid + size * 0.14f,
                                  thickness);
            break;
        }
        case OPENRIDE_UI_DRIVE_MANEUVER_ARRIVE:
            drive_draw_thick_line(renderer, left, bottom, left, top, thickness);
            drive_draw_thick_line(renderer,
                                  left,
                                  top,
                                  right,
                                  top + size * 0.12f,
                                  thickness);
            drive_draw_thick_line(renderer,
                                  right,
                                  top + size * 0.12f,
                                  left,
                                  top + size * 0.28f,
                                  thickness);
            break;
        case OPENRIDE_UI_DRIVE_MANEUVER_DEPART:
        case OPENRIDE_UI_DRIVE_MANEUVER_CONTINUE:
        default:
            drive_draw_thick_line(renderer, cx, bottom, cx, top, thickness);
            drive_draw_thick_line(renderer,
                                  cx,
                                  top,
                                  cx - size * 0.18f,
                                  top + size * 0.20f,
                                  thickness);
            drive_draw_thick_line(renderer,
                                  cx,
                                  top,
                                  cx + size * 0.18f,
                                  top + size * 0.20f,
                                  thickness);
            break;
    }
}

OpenRideUIDriveHUDLayout openride_ui_drive_hud_layout(
    const OpenRideUIContext *ui,
    bool show_following)
{
    OpenRideUIDriveHUDLayout layout = {0};
    if (!ui) return layout;

    OpenRideUIRect safe = openride_ui_safe_rect(ui);
    safe = openride_ui_inset(safe, OPENRIDE_UI_DRIVE_MARGIN);
    if (safe.w < 180.0f || safe.h < 230.0f) return layout;

    layout.top = openride_ui_rect(safe.x,
                                  safe.y,
                                  safe.w,
                                  OPENRIDE_UI_DRIVE_TOP_HEIGHT);
    if (show_following) {
        layout.following = openride_ui_rect(
            safe.x,
            layout.top.y + layout.top.h + OPENRIDE_UI_DRIVE_MARGIN,
            safe.w,
            OPENRIDE_UI_DRIVE_FOLLOWING_HEIGHT);
    }

    layout.controls = openride_ui_rect(
        safe.x,
        safe.y + safe.h - OPENRIDE_UI_DRIVE_ATTRIBUTION_HEIGHT
            - OPENRIDE_UI_DRIVE_CONTROLS_HEIGHT,
        safe.w,
        OPENRIDE_UI_DRIVE_CONTROLS_HEIGHT);
    layout.stats = openride_ui_rect(
        safe.x,
        layout.controls.y - OPENRIDE_UI_DRIVE_MARGIN - OPENRIDE_UI_DRIVE_STATS_HEIGHT,
        safe.w,
        OPENRIDE_UI_DRIVE_STATS_HEIGHT);

    const float item_width = layout.controls.w / (float)OPENRIDE_UI_DRIVE_CONTROL_COUNT;
    for (uint32_t i = 0U; i < OPENRIDE_UI_DRIVE_CONTROL_COUNT; ++i) {
        layout.control_items[i] = openride_ui_rect(
            layout.controls.x + item_width * (float)i,
            layout.controls.y,
            item_width,
            layout.controls.h);
    }
    return layout;
}

OpenRideUIDriveHUDAction openride_ui_drive_hud_hit_test(
    const OpenRideUIContext *ui,
    double x_px,
    double y_px)
{
    if (!ui) return OPENRIDE_UI_DRIVE_HUD_NONE;
    const double scale = ui->scale > 0.0f ? (double)ui->scale : 1.0;
    const double x = x_px / scale;
    const double y = y_px / scale;
    const OpenRideUIDriveHUDLayout layout =
        openride_ui_drive_hud_layout(ui, false);
    for (uint32_t i = 0U; i < OPENRIDE_UI_DRIVE_CONTROL_COUNT; ++i) {
        if (openride_ui_point_in_rect(x, y, layout.control_items[i])) {
            return (OpenRideUIDriveHUDAction)(OPENRIDE_UI_DRIVE_HUD_EXIT + (int)i);
        }
    }
    return OPENRIDE_UI_DRIVE_HUD_NONE;
}

void openride_ui_drive_hud_draw(OpenRideUIContext *ui,
                                const OpenRideUIDriveHUDState *state)
{
    if (!ui || !ui->renderer || !state) return;
    const OpenRideUIDriveHUDLayout layout =
        openride_ui_drive_hud_layout(ui, state->show_following);
    if (layout.top.w <= 0.0f || layout.controls.w <= 0.0f) return;

    SDL_Renderer *renderer = ui->renderer;
    const float ui_scale = ui->scale > 0.0f ? ui->scale : 1.0f;
    const float big_scale = ui_scale > 2.2f ? 3.0f : ui_scale * 1.35f;
    const float normal_scale = ui_scale > 2.2f ? 2.2f : ui_scale;
    const float small_scale = ui_scale > 1.8f ? 1.8f : ui_scale;
    const float value_scale = ui_scale > 2.2f ? 2.35f : ui_scale * 1.05f;
    const float label_scale = ui_scale > 1.65f ? 1.65f : ui_scale;

    const SDL_FRect top = openride_ui_rect_pixels(ui, layout.top);
    openride_ui_panel(ui, layout.top, true);

    const float icon_size = 58.0f * ui_scale;
    const float content_x = top.x + 72.0f * ui_scale;
    if (state->status != OPENRIDE_UI_DRIVE_HUD_OFF_ROUTE) {
        drive_draw_maneuver_icon(renderer,
                                 state->status == OPENRIDE_UI_DRIVE_HUD_ARRIVED
                                     ? OPENRIDE_UI_DRIVE_MANEUVER_ARRIVE
                                     : state->maneuver,
                                 top.x + 6.0f * ui_scale,
                                 top.y + 11.0f * ui_scale,
                                 icon_size,
                                 2.2f * ui_scale);
    }

    const float text_max_width = top.x + top.w - content_x - 12.0f * ui_scale;
    if (state->status == OPENRIDE_UI_DRIVE_HUD_OFF_ROUTE) {
        SDL_SetRenderDrawColor(renderer, 242, 92, 72, 255);
        drive_draw_text_fit(renderer,
                            content_x,
                            top.y + 10.0f * ui_scale,
                            text_max_width,
                            big_scale,
                            state->auto_reroute ? "HORS ITINERAIRE" : "HORS ROUTE");
        SDL_SetRenderDrawColor(renderer, 245, 225, 220, 255);
        drive_draw_text_fit(renderer,
                            content_x,
                            top.y + 48.0f * ui_scale,
                            text_max_width,
                            normal_scale,
                            state->auto_reroute
                                ? "Recalcul automatique..."
                                : "Recalcul manuel disponible");
    } else if (state->status == OPENRIDE_UI_DRIVE_HUD_ARRIVED) {
        SDL_SetRenderDrawColor(renderer, 92, 210, 126, 255);
        drive_draw_text_fit(renderer,
                            content_x,
                            top.y + 14.0f * ui_scale,
                            text_max_width,
                            big_scale,
                            "ARRIVEE");
    } else {
        SDL_SetRenderDrawColor(renderer, 47, 198, 181, 255);
        drive_draw_text_fit(renderer,
                            content_x,
                            top.y + 8.0f * ui_scale,
                            text_max_width,
                            big_scale,
                            state->primary_text ? state->primary_text : "DANS --");
        SDL_SetRenderDrawColor(renderer, 245, 247, 248, 255);
        drive_draw_text_fit(renderer,
                            content_x,
                            top.y + 47.0f * ui_scale,
                            text_max_width,
                            normal_scale,
                            state->maneuver_text
                                ? state->maneuver_text
                                : "Suivre l'itineraire");
    }

    if (state->show_following && layout.following.w > 0.0f) {
        const SDL_FRect following = openride_ui_rect_pixels(ui, layout.following);
        openride_ui_panel(ui, layout.following, false);
        SDL_SetRenderDrawColor(renderer, 190, 226, 221, 255);
        drive_draw_text_fit(renderer,
                            following.x + 10.0f * ui_scale,
                            following.y + 8.0f * ui_scale,
                            following.w - 20.0f * ui_scale,
                            small_scale,
                            state->following_text ? state->following_text : "");
    }

    if (state->gps_text && state->gps_text[0]) {
        switch (state->gps_quality) {
            case OPENRIDE_UI_DRIVE_GPS_GOOD:
                SDL_SetRenderDrawColor(renderer, 98, 211, 128, 255);
                break;
            case OPENRIDE_UI_DRIVE_GPS_FAIR:
                SDL_SetRenderDrawColor(renderer, 255, 207, 77, 255);
                break;
            default:
                SDL_SetRenderDrawColor(renderer, 240, 96, 76, 255);
                break;
        }
        float gps_scale = small_scale;
        const float max_gps_width = top.w * 0.48f;
        gps_scale = drive_fit_scale(state->gps_text, gps_scale, max_gps_width);
        const float gps_width =
            openride_ui_font_measure_width(state->gps_text, OPENRIDE_UI_FONT_COMPAT_HEIGHT * gps_scale);
        const float gps_y = state->status == OPENRIDE_UI_DRIVE_HUD_OFF_ROUTE
            ? top.y + 67.0f * ui_scale
            : top.y + 10.0f * ui_scale;
        drive_draw_scaled_text(renderer,
                               top.x + top.w - gps_width - 10.0f * ui_scale,
                               gps_y,
                               gps_scale,
                               state->gps_text);
    }

    const SDL_FRect stats = openride_ui_rect_pixels(ui, layout.stats);
    openride_ui_panel(ui, layout.stats, false);

    const float col_width = stats.w / 3.0f;
    char value[48];
    SDL_SetRenderDrawColor(renderer, 245, 247, 248, 255);
    snprintf(value, sizeof(value), "%.0f km/h", state->speed_kph);
    drive_draw_text_fit(renderer,
                        stats.x + 8.0f * ui_scale,
                        stats.y + 8.0f * ui_scale,
                        col_width - 16.0f * ui_scale,
                        value_scale,
                        value);
    snprintf(value, sizeof(value), "%.1f km", state->remaining_m / 1000.0);
    drive_draw_text_fit(renderer,
                        stats.x + col_width + 8.0f * ui_scale,
                        stats.y + 8.0f * ui_scale,
                        col_width - 16.0f * ui_scale,
                        value_scale,
                        value);
    drive_draw_text_fit(renderer,
                        stats.x + col_width * 2.0f + 8.0f * ui_scale,
                        stats.y + 8.0f * ui_scale,
                        col_width - 16.0f * ui_scale,
                        value_scale,
                        state->arrival_text ? state->arrival_text : "--:--");

    SDL_SetRenderDrawColor(renderer, 160, 170, 179, 255);
    drive_draw_scaled_text(renderer,
                           stats.x + 8.0f * ui_scale,
                           stats.y + 34.0f * ui_scale,
                           label_scale,
                           "VITESSE");
    drive_draw_scaled_text(renderer,
                           stats.x + col_width + 8.0f * ui_scale,
                           stats.y + 34.0f * ui_scale,
                           label_scale,
                           "RESTANT");
    drive_draw_scaled_text(renderer,
                           stats.x + col_width * 2.0f + 8.0f * ui_scale,
                           stats.y + 34.0f * ui_scale,
                           label_scale,
                           "ARRIVEE");

    if (state->reroute_count > 0U) {
        char reroutes[32];
        snprintf(reroutes, sizeof(reroutes), "recalcul %u", state->reroute_count);
        const float reroute_width =
            openride_ui_font_measure_width(reroutes, OPENRIDE_UI_FONT_COMPAT_HEIGHT * label_scale);
        SDL_SetRenderDrawColor(renderer, 170, 178, 185, 255);
        drive_draw_scaled_text(renderer,
                               stats.x + stats.w - reroute_width - 6.0f * ui_scale,
                               stats.y - 14.0f * ui_scale,
                               label_scale,
                               reroutes);
    }

    const SDL_FRect controls = openride_ui_rect_pixels(ui, layout.controls);
    openride_ui_panel(ui, layout.controls, true);

    if (state->show_attribution) {
        SDL_SetRenderDrawColor(renderer, 155, 163, 167, 70);
        drive_draw_scaled_text(renderer,
                               controls.x + 4.0f * ui_scale,
                               controls.y + controls.h + 2.0f * ui_scale,
                               ui_scale > 1.15f ? 1.15f : ui_scale,
                               "(c) OpenStreetMap contributors | ODbL");
    }

    const char *labels[OPENRIDE_UI_DRIVE_CONTROL_COUNT] = {
        "CARTE",
        "CENTRER",
        state->heading_up ? "NORD" : "CAP",
        "GPS"
    };
    static const OpenRideUIIcon control_icons[OPENRIDE_UI_DRIVE_CONTROL_COUNT] = {
        OPENRIDE_UI_ICON_MAP,
        OPENRIDE_UI_ICON_LOCATION,
        OPENRIDE_UI_ICON_COMPASS,
        OPENRIDE_UI_ICON_GPS
    };
    for (uint32_t i = 0U; i < OPENRIDE_UI_DRIVE_CONTROL_COUNT; ++i) {
        const OpenRideUIRect item = layout.control_items[i];
        const OpenRideUIColor tint = i == 1U
            ? ui->theme.primary
            : ui->theme.text_secondary;
        openride_ui_icon_draw(ui,
                              control_icons[i],
                              openride_ui_rect(item.x + (item.w - 22.0f) * 0.5f,
                                               item.y + 8.0f,
                                               22.0f,
                                               22.0f),
                              tint,
                              1.65f);
        openride_ui_text_color(ui,
                               openride_ui_rect(item.x + 2.0f,
                                                item.y + item.h - 22.0f,
                                                item.w - 4.0f,
                                                16.0f),
                               labels[i],
                               OPENRIDE_UI_TEXT_CAPTION,
                               OPENRIDE_UI_TEXT_ALIGN_CENTER,
                               tint);
    }
}
