#ifndef OPENRIDE_UI_H
#define OPENRIDE_UI_TOOLBAR_STANDALONE 1
#endif

#include "openride/ui_toolbar.h"

#ifdef OPENRIDE_UI_TOOLBAR_STANDALONE

#include <stddef.h>

#define OPENRIDE_UI_TOOLBAR_MARGIN 10.0f
#define OPENRIDE_UI_TOOLBAR_HEIGHT 72.0f
#define OPENRIDE_UI_TOOLBAR_ITEMS 5U

static const char *toolbar_label(OpenRideToolbarAction action, bool route_ready)
{
    if (action == OPENRIDE_TOOLBAR_ROUTE && route_ready) return "Demarrer";
    return openride_toolbar_action_label(action);
}

static OpenRideUIID toolbar_id(OpenRideToolbarAction action)
{
    static const char *ids[OPENRIDE_UI_TOOLBAR_ITEMS] = {
        "toolbar-menu",
        "toolbar-search",
        "toolbar-route",
        "toolbar-loop",
        "toolbar-gps"
    };
    if (action < OPENRIDE_TOOLBAR_MENU || action > OPENRIDE_TOOLBAR_GPS) return 0U;
    return OPENRIDE_UI_ID(ids[(size_t)(action - OPENRIDE_TOOLBAR_MENU)]);
}

OpenRideUIToolbarLayout openride_ui_toolbar_layout(const OpenRideUIContext *ui)
{
    OpenRideUIToolbarLayout layout = {0};
    if (!ui) return layout;

    OpenRideUIRect safe = openride_ui_safe_rect(ui);
    safe = openride_ui_inset(safe, OPENRIDE_UI_TOOLBAR_MARGIN);
    if (safe.w <= 0.0f || safe.h <= 0.0f) return layout;

    const float height = safe.h < OPENRIDE_UI_TOOLBAR_HEIGHT
        ? safe.h : OPENRIDE_UI_TOOLBAR_HEIGHT;
    layout.bar = openride_ui_rect(safe.x,
                                  safe.y + safe.h - height,
                                  safe.w,
                                  height);

    const float item_width = layout.bar.w / (float)OPENRIDE_UI_TOOLBAR_ITEMS;
    for (uint32_t i = 0U; i < OPENRIDE_UI_TOOLBAR_ITEMS; ++i) {
        layout.items[i] = openride_ui_rect(layout.bar.x + item_width * (float)i,
                                           layout.bar.y,
                                           item_width,
                                           layout.bar.h);
    }
    return layout;
}

OpenRideToolbarAction openride_ui_toolbar_hit_test(const OpenRideUIContext *ui,
                                                   double x_px,
                                                   double y_px)
{
    if (!ui) return OPENRIDE_TOOLBAR_NONE;
    const double scale = ui->scale > 0.0f ? (double)ui->scale : 1.0;
    const double x = x_px / scale;
    const double y = y_px / scale;
    const OpenRideUIToolbarLayout layout = openride_ui_toolbar_layout(ui);
    for (uint32_t i = 0U; i < OPENRIDE_UI_TOOLBAR_ITEMS; ++i) {
        if (openride_ui_point_in_rect(x, y, layout.items[i])) {
            return (OpenRideToolbarAction)(OPENRIDE_TOOLBAR_MENU + (int)i);
        }
    }
    return OPENRIDE_TOOLBAR_NONE;
}

OpenRideToolbarAction openride_ui_toolbar_draw(OpenRideUIContext *ui,
                                               bool route_ready)
{
    if (!ui || !ui->renderer) return OPENRIDE_TOOLBAR_NONE;
    const OpenRideUIToolbarLayout layout = openride_ui_toolbar_layout(ui);
    if (layout.bar.w <= 0.0f || layout.bar.h <= 0.0f) {
        return OPENRIDE_TOOLBAR_NONE;
    }

    openride_ui_panel(ui, layout.bar, true);

    OpenRideToolbarAction clicked = OPENRIDE_TOOLBAR_NONE;
    for (OpenRideToolbarAction action = OPENRIDE_TOOLBAR_MENU;
         action <= OPENRIDE_TOOLBAR_GPS;
         action = (OpenRideToolbarAction)(action + 1)) {
        const uint32_t index = (uint32_t)(action - OPENRIDE_TOOLBAR_MENU);
        if (openride_ui_button(ui,
                               toolbar_id(action),
                               layout.items[index],
                               toolbar_label(action, route_ready),
                               OPENRIDE_UI_BUTTON_GHOST,
                               true,
                               action == OPENRIDE_TOOLBAR_ROUTE && route_ready)) {
            clicked = action;
        }
    }
    return clicked;
}

#endif /* OPENRIDE_UI_TOOLBAR_STANDALONE */
