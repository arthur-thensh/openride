#include "openride/ui_toolbar.h"
#include "openride/ui_icon.h"

#include <stddef.h>

#define OPENRIDE_UI_TOOLBAR_MARGIN_X 12.0f
#define OPENRIDE_UI_TOOLBAR_HEIGHT 76.0f
#define OPENRIDE_UI_TOOLBAR_BOTTOM_CLEARANCE 18.0f
#define OPENRIDE_UI_TOOLBAR_ITEMS 5U
#define OPENRIDE_UI_TOOLBAR_ITEM_INSET 2.0f

static const char *toolbar_label(OpenRideToolbarAction action, bool route_ready)
{
    switch (action) {
        case OPENRIDE_TOOLBAR_MENU:
            return "Menu";
        case OPENRIDE_TOOLBAR_SEARCH:
            return "Chercher";
        case OPENRIDE_TOOLBAR_ROUTE:
            return route_ready ? "Demarrer" : "Trajet";
        case OPENRIDE_TOOLBAR_LOOP:
            return "Balade";
        case OPENRIDE_TOOLBAR_GPS:
            return "GPS";
        case OPENRIDE_TOOLBAR_NONE:
        default:
            return "";
    }
}

static OpenRideUIIcon toolbar_icon(OpenRideToolbarAction action)
{
    switch (action) {
        case OPENRIDE_TOOLBAR_MENU:
            return OPENRIDE_UI_ICON_MENU;
        case OPENRIDE_TOOLBAR_SEARCH:
            return OPENRIDE_UI_ICON_SEARCH;
        case OPENRIDE_TOOLBAR_ROUTE:
            return OPENRIDE_UI_ICON_ROUTE;
        case OPENRIDE_TOOLBAR_LOOP:
            return OPENRIDE_UI_ICON_LOOP;
        case OPENRIDE_TOOLBAR_GPS:
            return OPENRIDE_UI_ICON_GPS;
        case OPENRIDE_TOOLBAR_NONE:
        default:
            return OPENRIDE_UI_ICON_MENU;
    }
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

    const OpenRideUIRect safe = openride_ui_safe_rect(ui);
    if (safe.w <= OPENRIDE_UI_TOOLBAR_MARGIN_X * 2.0f
        || safe.h <= OPENRIDE_UI_TOOLBAR_BOTTOM_CLEARANCE) {
        return layout;
    }

    const float available_height = safe.h - OPENRIDE_UI_TOOLBAR_BOTTOM_CLEARANCE;
    const float height = available_height < OPENRIDE_UI_TOOLBAR_HEIGHT
        ? available_height : OPENRIDE_UI_TOOLBAR_HEIGHT;
    layout.bar = openride_ui_rect(
        safe.x + OPENRIDE_UI_TOOLBAR_MARGIN_X,
        safe.y + safe.h - OPENRIDE_UI_TOOLBAR_BOTTOM_CLEARANCE - height,
        safe.w - OPENRIDE_UI_TOOLBAR_MARGIN_X * 2.0f,
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
        const OpenRideUIID id = toolbar_id(action);
        const bool selected = action == OPENRIDE_TOOLBAR_ROUTE && route_ready;
        const OpenRideUIRect item = openride_ui_inset(layout.items[index],
                                                      OPENRIDE_UI_TOOLBAR_ITEM_INSET);
        const bool inside = openride_ui_point_in_rect(ui->pointer_x,
                                                      ui->pointer_y,
                                                      item);
        if (inside) ui->hot_id = id;
        if (inside && ui->pointer_pressed) {
            ui->active_id = id;
            ui->pointer_consumed = true;
        }
        const bool active = ui->active_id == id;
        if (active && ui->pointer_down) ui->pointer_consumed = true;
        if (active && ui->pointer_released) {
            ui->pointer_consumed = true;
            if (inside) clicked = action;
        }

        /* One continuous bar: only selected/pressed state gets a pill. */
        if (selected || active) {
            const OpenRideUIColor saved_surface = ui->theme.surface;
            const OpenRideUIColor saved_border = ui->theme.border;
            ui->theme.surface = selected
                ? ui->theme.primary_soft
                : ui->theme.surface_elevated;
            ui->theme.surface.a = selected ? 205U : 120U;
            ui->theme.border = selected ? ui->theme.primary : saved_border;
            ui->theme.border.a = selected ? 42U : 18U;
            openride_ui_panel(ui, openride_ui_inset(item, 4.0f), false);
            ui->theme.surface = saved_surface;
            ui->theme.border = saved_border;
        }

        float icon_size = 24.0f;
        if (icon_size > item.h * 0.40f) icon_size = item.h * 0.40f;
        const OpenRideUIRect icon_rect = openride_ui_rect(
            item.x + (item.w - icon_size) * 0.5f,
            item.y + 8.0f,
            icon_size,
            icon_size);
        const OpenRideUIColor icon_color = selected
            ? ui->theme.primary
            : (inside ? ui->theme.text : ui->theme.text_secondary);
        openride_ui_icon_draw(ui,
                              toolbar_icon(action),
                              icon_rect,
                              icon_color,
                              selected ? 2.0f : 1.7f);

        const OpenRideUIRect label_rect = openride_ui_rect(
            item.x + 2.0f,
            item.y + item.h - 22.0f,
            item.w - 4.0f,
            16.0f);
        openride_ui_text_color(ui,
                               label_rect,
                               toolbar_label(action, route_ready),
                               OPENRIDE_UI_TEXT_CAPTION,
                               OPENRIDE_UI_TEXT_ALIGN_CENTER,
                               selected
                                   ? ui->theme.primary
                                   : (inside ? ui->theme.text
                                             : ui->theme.text_secondary));
    }
    return clicked;
}

