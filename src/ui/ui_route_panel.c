#include "openride/ui_route_panel.h"
#include "openride/ui_icon.h"

#include <math.h>
#include <stdio.h>

#define OPENRIDE_UI_PLANNER_MAX_WIDTH 370.0f
#define OPENRIDE_UI_PLANNER_ROUTE_HEIGHT 610.0f
#define OPENRIDE_UI_PLANNER_LOOP_HEIGHT 555.0f

static OpenRideUIID planner_id(const char *name)
{
    return openride_ui_id(name);
}

static const char *profile_label(OpenRideRoutingProfile profile)
{
    switch (profile) {
        case OPENRIDE_ROUTING_PROFILE_FASTEST: return "Rapide";
        case OPENRIDE_ROUTING_PROFILE_TRAIL: return "Trail";
        case OPENRIDE_ROUTING_PROFILE_TOURING:
        default: return "Balade";
    }
}

OpenRideUIRoutePanelLayout openride_ui_route_panel_layout(
    const OpenRideUIContext *ui,
    OpenRideRidePlannerMode mode)
{
    OpenRideUIRoutePanelLayout layout = {0};
    if (!ui) return layout;

    OpenRideUIRect safe = openride_ui_inset(openride_ui_safe_rect(ui), 10.0f);
    if (safe.w < 180.0f || safe.h < 420.0f) return layout;

    const float panel_w = safe.w < OPENRIDE_UI_PLANNER_MAX_WIDTH
        ? safe.w : OPENRIDE_UI_PLANNER_MAX_WIDTH;
    const float desired_h = mode == OPENRIDE_RIDE_PLANNER_LOOP
        ? OPENRIDE_UI_PLANNER_LOOP_HEIGHT
        : OPENRIDE_UI_PLANNER_ROUTE_HEIGHT;
    float panel_h = desired_h;
    if (panel_h > safe.h * 0.94f) panel_h = safe.h * 0.94f;

    const float x = safe.x + (safe.w - panel_w) * 0.5f;
    const float y = safe.y + (safe.h - panel_h) * 0.45f;
    const float inner_x = x + 12.0f;
    const float inner_w = panel_w - 24.0f;
    layout.panel = openride_ui_rect(x, y, panel_w, panel_h);
    layout.title = openride_ui_rect(x + 52.0f, y + 10.0f, panel_w - 68.0f, 28.0f);
    layout.subtitle = openride_ui_rect(x + 18.0f, y + 39.0f, panel_w - 36.0f, 18.0f);

    const float mode_y = y + 66.0f;
    const float mode_gap = 6.0f;
    const float mode_w = (inner_w - mode_gap) * 0.5f;
    layout.mode_route = openride_ui_rect(inner_x, mode_y, mode_w, 38.0f);
    layout.mode_loop = openride_ui_rect(inner_x + mode_w + mode_gap,
                                        mode_y, mode_w, 38.0f);

    float cursor = mode_y + 62.0f;
    const float row_h = 42.0f;
    const float row_gap = 6.0f;
    for (uint32_t i = 0U; i < 3U; ++i) {
        layout.start[i] = openride_ui_rect(inner_x, cursor, inner_w, row_h);
        cursor += row_h + row_gap;
    }

    if (mode == OPENRIDE_RIDE_PLANNER_ROUTE) {
        cursor += 17.0f;
        for (uint32_t i = 0U; i < 2U; ++i) {
            layout.destination[i] = openride_ui_rect(inner_x, cursor, inner_w, row_h);
            cursor += row_h + row_gap;
        }
    }

    cursor += 20.0f;
    const float profile_gap = 6.0f;
    const float profile_w = (inner_w - profile_gap * 2.0f) / 3.0f;
    for (uint32_t i = 0U; i < 3U; ++i) {
        layout.profiles[i] = openride_ui_rect(inner_x + (profile_w + profile_gap) * (float)i,
                                              cursor, profile_w, 38.0f);
    }
    cursor += 48.0f;

    if (mode == OPENRIDE_RIDE_PLANNER_LOOP) {
        const float adjust_w = 46.0f;
        layout.distance_down = openride_ui_rect(inner_x, cursor, adjust_w, 42.0f);
        layout.distance_up = openride_ui_rect(inner_x + inner_w - adjust_w,
                                              cursor, adjust_w, 42.0f);
        layout.distance_value = openride_ui_rect(inner_x + adjust_w + 6.0f,
                                                 cursor,
                                                 inner_w - adjust_w * 2.0f - 12.0f,
                                                 42.0f);
        cursor += 50.0f;
        layout.direction = openride_ui_rect(inner_x, cursor, inner_w, 40.0f);
        cursor += 50.0f;
    }

    layout.primary = openride_ui_rect(inner_x, cursor, inner_w, 50.0f);
    layout.hint = openride_ui_rect(inner_x, cursor + 53.0f, inner_w, 18.0f);
    layout.back = openride_ui_rect(inner_x,
                                   y + panel_h - 50.0f,
                                   inner_w,
                                   40.0f);
    return layout;
}

static OpenRideUIRoutePanelAction hit_rows(const OpenRideUIRoutePanelLayout *layout,
                                           OpenRideRidePlannerMode mode,
                                           double x,
                                           double y)
{
    if (openride_ui_point_in_rect(x, y, layout->mode_route)) {
        return OPENRIDE_UI_ROUTE_PANEL_MODE_ROUTE;
    }
    if (openride_ui_point_in_rect(x, y, layout->mode_loop)) {
        return OPENRIDE_UI_ROUTE_PANEL_MODE_LOOP;
    }
    if (openride_ui_point_in_rect(x, y, layout->start[0])) {
        return OPENRIDE_UI_ROUTE_PANEL_GPS_START;
    }
    if (openride_ui_point_in_rect(x, y, layout->start[1])) {
        return OPENRIDE_UI_ROUTE_PANEL_SEARCH_START;
    }
    if (openride_ui_point_in_rect(x, y, layout->start[2])) {
        return OPENRIDE_UI_ROUTE_PANEL_MAP_START;
    }
    if (mode == OPENRIDE_RIDE_PLANNER_ROUTE) {
        if (openride_ui_point_in_rect(x, y, layout->destination[0])) {
            return OPENRIDE_UI_ROUTE_PANEL_SEARCH_DESTINATION;
        }
        if (openride_ui_point_in_rect(x, y, layout->destination[1])) {
            return OPENRIDE_UI_ROUTE_PANEL_MAP_DESTINATION;
        }
    }
    if (openride_ui_point_in_rect(x, y, layout->profiles[0])) {
        return OPENRIDE_UI_ROUTE_PANEL_PROFILE_FASTEST;
    }
    if (openride_ui_point_in_rect(x, y, layout->profiles[1])) {
        return OPENRIDE_UI_ROUTE_PANEL_PROFILE_TOURING;
    }
    if (openride_ui_point_in_rect(x, y, layout->profiles[2])) {
        return OPENRIDE_UI_ROUTE_PANEL_PROFILE_TRAIL;
    }
    if (mode == OPENRIDE_RIDE_PLANNER_LOOP) {
        if (openride_ui_point_in_rect(x, y, layout->distance_down)) {
            return OPENRIDE_UI_ROUTE_PANEL_LOOP_DISTANCE_DOWN;
        }
        if (openride_ui_point_in_rect(x, y, layout->distance_up)) {
            return OPENRIDE_UI_ROUTE_PANEL_LOOP_DISTANCE_UP;
        }
        if (openride_ui_point_in_rect(x, y, layout->direction)) {
            return OPENRIDE_UI_ROUTE_PANEL_LOOP_DIRECTION;
        }
    }
    if (openride_ui_point_in_rect(x, y, layout->primary)) {
        return OPENRIDE_UI_ROUTE_PANEL_CALCULATE;
    }
    if (openride_ui_point_in_rect(x, y, layout->back)) {
        return OPENRIDE_UI_ROUTE_PANEL_BACK;
    }
    return OPENRIDE_UI_ROUTE_PANEL_NONE;
}

OpenRideUIRoutePanelAction openride_ui_route_panel_hit_test(
    const OpenRideUIContext *ui,
    OpenRideRidePlannerMode mode,
    double x_px,
    double y_px)
{
    if (!ui) return OPENRIDE_UI_ROUTE_PANEL_NONE;
    const double scale = ui->scale > 0.0f ? (double)ui->scale : 1.0;
    const OpenRideUIRoutePanelLayout layout = openride_ui_route_panel_layout(ui, mode);
    return hit_rows(&layout, mode, x_px / scale, y_px / scale);
}

static void draw_icon_row(OpenRideUIContext *ui,
                          OpenRideUIID id,
                          OpenRideUIRect rect,
                          OpenRideUIIcon icon,
                          const char *label,
                          bool enabled,
                          OpenRideUIRoutePanelAction action,
                          OpenRideUIRoutePanelAction *clicked)
{
    if (openride_ui_button(ui, id, rect, "", OPENRIDE_UI_BUTTON_GHOST,
                           enabled, false)) {
        *clicked = action;
    }
    OpenRideUIColor tint = enabled ? ui->theme.text_secondary : ui->theme.disabled;
    openride_ui_icon_draw(ui, icon,
                          openride_ui_rect(rect.x + 12.0f,
                                           rect.y + (rect.h - 21.0f) * 0.5f,
                                           21.0f, 21.0f),
                          tint, 1.6f);
    openride_ui_text_color(ui,
                           openride_ui_rect(rect.x + 44.0f, rect.y,
                                            rect.w - 56.0f, rect.h),
                           label,
                           OPENRIDE_UI_TEXT_BODY,
                           OPENRIDE_UI_TEXT_ALIGN_LEFT,
                           enabled ? ui->theme.text : ui->theme.text_secondary);
}

OpenRideUIRoutePanelAction openride_ui_route_panel_draw(
    OpenRideUIContext *ui,
    const OpenRideUIRoutePanelState *state)
{
    if (!ui || !ui->renderer) return OPENRIDE_UI_ROUTE_PANEL_NONE;
    const OpenRideUIRoutePanelState empty = {0};
    if (!state) state = &empty;

    const OpenRideUIRoutePanelLayout layout =
        openride_ui_route_panel_layout(ui, state->mode);
    if (layout.panel.w <= 0.0f || layout.panel.h <= 0.0f) {
        return OPENRIDE_UI_ROUTE_PANEL_NONE;
    }

    SDL_FRect screen = {0.0f, 0.0f,
                        (float)ui->viewport_width,
                        (float)ui->viewport_height};
    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 92);
    SDL_RenderFillRect(ui->renderer, &screen);
    openride_ui_panel(ui, layout.panel, true);

    openride_ui_icon_draw(ui,
                          state->mode == OPENRIDE_RIDE_PLANNER_LOOP
                              ? OPENRIDE_UI_ICON_LOOP : OPENRIDE_UI_ICON_ROUTE,
                          openride_ui_rect(layout.panel.x + 17.0f,
                                           layout.panel.y + 13.0f,
                                           24.0f, 24.0f),
                          ui->theme.primary,
                          1.8f);
    openride_ui_text(ui, layout.title,
                     state->mode == OPENRIDE_RIDE_PLANNER_LOOP
                         ? "Préparer une balade" : "Préparer un trajet",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    char subtitle[128];
    if (state->mode == OPENRIDE_RIDE_PLANNER_LOOP) {
        snprintf(subtitle, sizeof(subtitle),
                 "Départ %s  ·  boucle %.0f km",
                 state->has_start ? "choisi" : "à choisir",
                 state->loop_target_distance_m / 1000.0);
    } else {
        snprintf(subtitle, sizeof(subtitle),
                 "Départ %s  ·  Arrivée %s",
                 state->has_start ? "choisi" : "à choisir",
                 state->has_destination ? "choisie" : "à choisir");
    }
    openride_ui_text(ui, layout.subtitle, subtitle,
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    OpenRideUIRoutePanelAction clicked = OPENRIDE_UI_ROUTE_PANEL_NONE;
    const bool planner_busy = state->busy != OPENRIDE_RIDE_PLANNER_IDLE;
    const bool interactive = !planner_busy;
    if (openride_ui_button(ui, planner_id("planner-mode-route"),
                           layout.mode_route, "Trajet",
                           OPENRIDE_UI_BUTTON_GHOST, interactive,
                           state->mode == OPENRIDE_RIDE_PLANNER_ROUTE)) {
        clicked = OPENRIDE_UI_ROUTE_PANEL_MODE_ROUTE;
    }
    if (openride_ui_button(ui, planner_id("planner-mode-loop"),
                           layout.mode_loop, "Boucle",
                           OPENRIDE_UI_BUTTON_GHOST, interactive,
                           state->mode == OPENRIDE_RIDE_PLANNER_LOOP)) {
        clicked = OPENRIDE_UI_ROUTE_PANEL_MODE_LOOP;
    }

    openride_ui_text_color(ui,
                           openride_ui_rect(layout.start[0].x + 4.0f,
                                            layout.start[0].y - 17.0f,
                                            layout.start[0].w - 8.0f, 15.0f),
                           "DÉPART",
                           OPENRIDE_UI_TEXT_CAPTION,
                           OPENRIDE_UI_TEXT_ALIGN_LEFT,
                           ui->theme.primary);

    char gps_label[80];
    if (state->gps_valid && isfinite(state->gps_accuracy_m)) {
        snprintf(gps_label, sizeof(gps_label), "Ma position GPS  ·  %.0f m",
                 state->gps_accuracy_m);
    } else {
        snprintf(gps_label, sizeof(gps_label), "Ma position GPS");
    }
    draw_icon_row(ui, planner_id("planner-gps-start"), layout.start[0],
                  OPENRIDE_UI_ICON_GPS, gps_label, interactive,
                  OPENRIDE_UI_ROUTE_PANEL_GPS_START, &clicked);
    draw_icon_row(ui, planner_id("planner-search-start"), layout.start[1],
                  OPENRIDE_UI_ICON_SEARCH, "Rechercher un lieu", interactive,
                  OPENRIDE_UI_ROUTE_PANEL_SEARCH_START, &clicked);
    draw_icon_row(ui, planner_id("planner-map-start"), layout.start[2],
                  OPENRIDE_UI_ICON_MAP, "Choisir sur la carte", interactive,
                  OPENRIDE_UI_ROUTE_PANEL_MAP_START, &clicked);

    if (state->mode == OPENRIDE_RIDE_PLANNER_ROUTE) {
        openride_ui_text_color(ui,
                               openride_ui_rect(layout.destination[0].x + 4.0f,
                                                layout.destination[0].y - 17.0f,
                                                layout.destination[0].w - 8.0f, 15.0f),
                               "ARRIVÉE",
                               OPENRIDE_UI_TEXT_CAPTION,
                               OPENRIDE_UI_TEXT_ALIGN_LEFT,
                               ui->theme.primary);
        draw_icon_row(ui, planner_id("planner-search-destination"),
                      layout.destination[0], OPENRIDE_UI_ICON_SEARCH,
                      "Rechercher un lieu", interactive,
                      OPENRIDE_UI_ROUTE_PANEL_SEARCH_DESTINATION, &clicked);
        draw_icon_row(ui, planner_id("planner-map-destination"),
                      layout.destination[1], OPENRIDE_UI_ICON_MAP,
                      "Choisir sur la carte", interactive,
                      OPENRIDE_UI_ROUTE_PANEL_MAP_DESTINATION, &clicked);
    }

    openride_ui_text_color(ui,
                           openride_ui_rect(layout.profiles[0].x + 3.0f,
                                            layout.profiles[0].y - 17.0f,
                                            layout.profiles[2].x + layout.profiles[2].w
                                                - layout.profiles[0].x - 6.0f,
                                            15.0f),
                           "STYLE DE ROUTE",
                           OPENRIDE_UI_TEXT_CAPTION,
                           OPENRIDE_UI_TEXT_ALIGN_LEFT,
                           ui->theme.primary);
    const OpenRideRoutingProfile profile_values[3] = {
        OPENRIDE_ROUTING_PROFILE_FASTEST,
        OPENRIDE_ROUTING_PROFILE_TOURING,
        OPENRIDE_ROUTING_PROFILE_TRAIL
    };
    const OpenRideUIRoutePanelAction profile_actions[3] = {
        OPENRIDE_UI_ROUTE_PANEL_PROFILE_FASTEST,
        OPENRIDE_UI_ROUTE_PANEL_PROFILE_TOURING,
        OPENRIDE_UI_ROUTE_PANEL_PROFILE_TRAIL
    };
    const char *profile_ids[3] = {
        "planner-profile-fast", "planner-profile-touring", "planner-profile-trail"
    };
    for (uint32_t i = 0U; i < 3U; ++i) {
        if (openride_ui_button(ui, planner_id(profile_ids[i]), layout.profiles[i],
                               profile_label(profile_values[i]),
                               OPENRIDE_UI_BUTTON_GHOST, interactive,
                               state->profile == profile_values[i])) {
            clicked = profile_actions[i];
        }
    }

    if (state->mode == OPENRIDE_RIDE_PLANNER_LOOP) {
        openride_ui_text_color(ui,
                               openride_ui_rect(layout.distance_down.x + 3.0f,
                                                layout.distance_down.y - 17.0f,
                                                layout.distance_up.x + layout.distance_up.w
                                                    - layout.distance_down.x - 6.0f,
                                                15.0f),
                               "DISTANCE CIBLE",
                               OPENRIDE_UI_TEXT_CAPTION,
                               OPENRIDE_UI_TEXT_ALIGN_LEFT,
                               ui->theme.primary);
        if (openride_ui_button(ui, planner_id("planner-distance-down"),
                               layout.distance_down, "−",
                               OPENRIDE_UI_BUTTON_GHOST, interactive, false)) {
            clicked = OPENRIDE_UI_ROUTE_PANEL_LOOP_DISTANCE_DOWN;
        }
        char distance[48];
        snprintf(distance, sizeof(distance), "%.0f km",
                 state->loop_target_distance_m / 1000.0);
        openride_ui_panel(ui, layout.distance_value, false);
        openride_ui_text(ui, layout.distance_value, distance,
                         OPENRIDE_UI_TEXT_BODY,
                         OPENRIDE_UI_TEXT_ALIGN_CENTER);
        if (openride_ui_button(ui, planner_id("planner-distance-up"),
                               layout.distance_up, "+",
                               OPENRIDE_UI_BUTTON_GHOST, interactive, false)) {
            clicked = OPENRIDE_UI_ROUTE_PANEL_LOOP_DISTANCE_UP;
        }

        char direction[80];
        snprintf(direction, sizeof(direction), "Direction · %s",
                 openride_loop_direction_name(state->loop_direction));
        if (openride_ui_button(ui, planner_id("planner-direction"),
                               layout.direction, direction,
                               OPENRIDE_UI_BUTTON_GHOST, interactive, false)) {
            clicked = OPENRIDE_UI_ROUTE_PANEL_LOOP_DIRECTION;
        }
    }

    const bool can_generate = state->mode == OPENRIDE_RIDE_PLANNER_LOOP
        ? state->has_start
        : state->has_start && state->has_destination;
    const char *primary_label = state->mode == OPENRIDE_RIDE_PLANNER_LOOP
        ? "Proposer des balades" : "Calculer l’itinéraire";
    if (planner_busy) {
        (void)openride_ui_button(ui,
                                 planner_id("planner-primary"),
                                 layout.primary,
                                 "",
                                 OPENRIDE_UI_BUTTON_PRIMARY,
                                 false,
                                 false);
        const float angle = (float)(SDL_GetTicks() % 1200U) * (360.0f / 1200.0f);
        openride_ui_icon_draw_rotated(
            ui,
            OPENRIDE_UI_ICON_LOADING,
            openride_ui_rect(layout.primary.x + 18.0f,
                             layout.primary.y + (layout.primary.h - 22.0f) * 0.5f,
                             22.0f,
                             22.0f),
            ui->theme.text,
            1.8f,
            angle);
        openride_ui_text_color(
            ui,
            openride_ui_rect(layout.primary.x + 50.0f,
                             layout.primary.y,
                             layout.primary.w - 64.0f,
                             layout.primary.h),
            state->busy == OPENRIDE_RIDE_PLANNER_GENERATING_LOOPS
                ? "Recherche de balades..."
                : "Calcul de l’itinéraire...",
            OPENRIDE_UI_TEXT_BODY,
            OPENRIDE_UI_TEXT_ALIGN_LEFT,
            ui->theme.text);
    } else if (openride_ui_button(ui, planner_id("planner-primary"), layout.primary,
                                  primary_label,
                                  can_generate ? OPENRIDE_UI_BUTTON_PRIMARY
                                               : OPENRIDE_UI_BUTTON_SECONDARY,
                                  can_generate, false)) {
        clicked = OPENRIDE_UI_ROUTE_PANEL_CALCULATE;
    }

    if (!planner_busy && state->feedback && state->feedback[0]) {
        openride_ui_text_color(ui,
                               layout.hint,
                               state->feedback,
                               OPENRIDE_UI_TEXT_CAPTION,
                               OPENRIDE_UI_TEXT_ALIGN_CENTER,
                               ui->theme.danger);
    } else {
        openride_ui_text(ui, layout.hint,
                         planner_busy
                             ? "Le calcul continue en arrière-plan"
                             : state->mode == OPENRIDE_RIDE_PLANNER_LOOP
                                 ? "3 propositions seront comparées avant de partir"
                                 : "Le trajet sera affiché sur la carte avant le départ",
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_CENTER);
    }

    if (openride_ui_button(ui, planner_id("planner-back"), layout.back,
                           "Retour", OPENRIDE_UI_BUTTON_GHOST, interactive, false)) {
        clicked = OPENRIDE_UI_ROUTE_PANEL_BACK;
    }
    return clicked;
}
