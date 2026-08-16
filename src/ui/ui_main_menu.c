#include "openride/ui_main_menu.h"
#include "openride/ui_icon.h"

#include <stddef.h>

#define OPENRIDE_UI_MAIN_MENU_ITEMS 6U
#define OPENRIDE_UI_MAIN_MENU_MARGIN 12.0f
#define OPENRIDE_UI_MAIN_MENU_GAP 8.0f
#define OPENRIDE_UI_MAIN_MENU_HEADER 70.0f
#define OPENRIDE_UI_MAIN_MENU_CLOSE_HEIGHT 52.0f
#define OPENRIDE_UI_MAIN_MENU_MAX_ROW_HEIGHT 60.0f
#define OPENRIDE_UI_MAIN_MENU_MAX_WIDTH 430.0f
#define OPENRIDE_UI_MAIN_MENU_MAX_HEIGHT 610.0f

static float main_menu_minf(float a, float b)
{
    return a < b ? a : b;
}

static OpenRideUIID main_menu_id(uint32_t index)
{
    static const char *ids[OPENRIDE_UI_MAIN_MENU_ITEMS] = {
        "main-menu-search",
        "main-menu-favorites",
        "main-menu-history",
        "main-menu-regions",
        "main-menu-settings",
        "main-menu-map-zoom-test"
    };
    if (index >= OPENRIDE_UI_MAIN_MENU_ITEMS) return 0U;
    return OPENRIDE_UI_ID(ids[index]);
}

static const char *main_menu_label(uint32_t index)
{
    static const char *labels[OPENRIDE_UI_MAIN_MENU_ITEMS] = {
        "Rechercher",
        "Favoris",
        "Historique",
        "Cartes hors ligne",
        "Parametres",
        "Test zoom carte [DEV]"
    };
    return index < OPENRIDE_UI_MAIN_MENU_ITEMS ? labels[index] : "";
}

static OpenRideUIIcon main_menu_icon(uint32_t index)
{
    static const OpenRideUIIcon icons[OPENRIDE_UI_MAIN_MENU_ITEMS] = {
        OPENRIDE_UI_ICON_SEARCH,
        OPENRIDE_UI_ICON_FAVORITE,
        OPENRIDE_UI_ICON_HISTORY,
        OPENRIDE_UI_ICON_DOWNLOAD,
        OPENRIDE_UI_ICON_SETTINGS,
        OPENRIDE_UI_ICON_MAP
    };
    return index < OPENRIDE_UI_MAIN_MENU_ITEMS
        ? icons[index]
        : OPENRIDE_UI_ICON_MENU;
}

OpenRideUIMainMenuLayout openride_ui_main_menu_layout(
    const OpenRideUIContext *ui)
{
    OpenRideUIMainMenuLayout layout = {0};
    if (!ui) return layout;

    OpenRideUIRect safe = openride_ui_safe_rect(ui);
    safe = openride_ui_inset(safe, OPENRIDE_UI_MAIN_MENU_MARGIN);
    if (safe.w < 120.0f || safe.h < 180.0f) return layout;

    const float panel_w = main_menu_minf(safe.w, OPENRIDE_UI_MAIN_MENU_MAX_WIDTH);
    const float panel_h = main_menu_minf(safe.h, OPENRIDE_UI_MAIN_MENU_MAX_HEIGHT);
    layout.panel = openride_ui_rect(
        safe.x + (safe.w - panel_w) * 0.5f,
        safe.y + (safe.h - panel_h) * 0.5f,
        panel_w,
        panel_h);
    layout.title = openride_ui_rect(layout.panel.x + 18.0f,
                                    layout.panel.y + 11.0f,
                                    layout.panel.w - 36.0f,
                                    28.0f);
    layout.subtitle = openride_ui_rect(layout.panel.x + 18.0f,
                                       layout.panel.y + 38.0f,
                                       layout.panel.w - 36.0f,
                                       18.0f);

    layout.close = openride_ui_rect(
        layout.panel.x + OPENRIDE_UI_MAIN_MENU_GAP,
        layout.panel.y + layout.panel.h - OPENRIDE_UI_MAIN_MENU_CLOSE_HEIGHT
            - OPENRIDE_UI_MAIN_MENU_GAP,
        layout.panel.w - OPENRIDE_UI_MAIN_MENU_GAP * 2.0f,
        OPENRIDE_UI_MAIN_MENU_CLOSE_HEIGHT);

    const float rows_top = layout.panel.y + OPENRIDE_UI_MAIN_MENU_HEADER;
    float available = layout.close.y - OPENRIDE_UI_MAIN_MENU_GAP - rows_top;
    available -= OPENRIDE_UI_MAIN_MENU_GAP
        * (float)(OPENRIDE_UI_MAIN_MENU_ITEMS - 1U);
    float row_height = available / (float)OPENRIDE_UI_MAIN_MENU_ITEMS;
    if (row_height > OPENRIDE_UI_MAIN_MENU_MAX_ROW_HEIGHT) {
        row_height = OPENRIDE_UI_MAIN_MENU_MAX_ROW_HEIGHT;
    }
    if (row_height < 1.0f) row_height = 1.0f;

    for (uint32_t i = 0U; i < OPENRIDE_UI_MAIN_MENU_ITEMS; ++i) {
        layout.items[i] = openride_ui_rect(
            layout.panel.x + OPENRIDE_UI_MAIN_MENU_GAP,
            rows_top + (row_height + OPENRIDE_UI_MAIN_MENU_GAP) * (float)i,
            layout.panel.w - OPENRIDE_UI_MAIN_MENU_GAP * 2.0f,
            row_height);
    }
    return layout;
}

OpenRideUIMainMenuAction openride_ui_main_menu_hit_test(
    const OpenRideUIContext *ui,
    double x_px,
    double y_px)
{
    if (!ui) return OPENRIDE_UI_MAIN_MENU_NONE;
    const double scale = ui->scale > 0.0f ? (double)ui->scale : 1.0;
    const double x = x_px / scale;
    const double y = y_px / scale;
    const OpenRideUIMainMenuLayout layout = openride_ui_main_menu_layout(ui);

    for (uint32_t i = 0U; i < OPENRIDE_UI_MAIN_MENU_ITEMS; ++i) {
        if (openride_ui_point_in_rect(x, y, layout.items[i])) {
            return (OpenRideUIMainMenuAction)(OPENRIDE_UI_MAIN_MENU_SEARCH
                + (int)i);
        }
    }
    if (openride_ui_point_in_rect(x, y, layout.close)) {
        return OPENRIDE_UI_MAIN_MENU_CLOSE;
    }
    return OPENRIDE_UI_MAIN_MENU_NONE;
}

OpenRideUIMainMenuAction openride_ui_main_menu_draw(OpenRideUIContext *ui)
{
    if (!ui || !ui->renderer) return OPENRIDE_UI_MAIN_MENU_NONE;
    const OpenRideUIMainMenuLayout layout = openride_ui_main_menu_layout(ui);
    if (layout.panel.w <= 0.0f || layout.panel.h <= 0.0f) {
        return OPENRIDE_UI_MAIN_MENU_NONE;
    }

    SDL_FRect screen = {
        0.0f,
        0.0f,
        (float)ui->viewport_width,
        (float)ui->viewport_height
    };
    SDL_SetRenderDrawColor(ui->renderer, 4, 7, 8, 154);
    SDL_RenderFillRect(ui->renderer, &screen);

    openride_ui_panel(ui, layout.panel, true);
    openride_ui_text(ui,
                     layout.title,
                     "OpenRide",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);
    openride_ui_text(ui,
                     layout.subtitle,
                     "Navigation moto hors ligne",
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    OpenRideUIMainMenuAction clicked = OPENRIDE_UI_MAIN_MENU_NONE;
    for (uint32_t i = 0U; i < OPENRIDE_UI_MAIN_MENU_ITEMS; ++i) {
        const OpenRideUIID id = main_menu_id(i);
        if (openride_ui_button(ui,
                               id,
                               layout.items[i],
                               "",
                               OPENRIDE_UI_BUTTON_SECONDARY,
                               true,
                               false)) {
            clicked = (OpenRideUIMainMenuAction)(
                OPENRIDE_UI_MAIN_MENU_SEARCH + (int)i);
        }

        const float icon_size = 24.0f;
        const OpenRideUIRect icon_rect = openride_ui_rect(
            layout.items[i].x + 15.0f,
            layout.items[i].y + (layout.items[i].h - icon_size) * 0.5f,
            icon_size,
            icon_size);
        const OpenRideUIColor tint = ui->hot_id == id
            ? ui->theme.primary
            : (i == OPENRIDE_UI_MAIN_MENU_ITEMS - 1U
                ? ui->theme.text_secondary
                : ui->theme.text);
        openride_ui_icon_draw(ui,
                              main_menu_icon(i),
                              icon_rect,
                              tint,
                              1.75f);

        openride_ui_text_color(ui,
                               openride_ui_rect(layout.items[i].x + 52.0f,
                                                layout.items[i].y,
                                                layout.items[i].w - 66.0f,
                                                layout.items[i].h),
                               main_menu_label(i),
                               OPENRIDE_UI_TEXT_BODY,
                               OPENRIDE_UI_TEXT_ALIGN_LEFT,
                               i == OPENRIDE_UI_MAIN_MENU_ITEMS - 1U
                                   ? ui->theme.text_secondary
                                   : ui->theme.text);
    }

    const OpenRideUIID close_id = OPENRIDE_UI_ID("main-menu-close");
    if (openride_ui_button(ui,
                           close_id,
                           layout.close,
                           "",
                           OPENRIDE_UI_BUTTON_GHOST,
                           true,
                           false)) {
        clicked = OPENRIDE_UI_MAIN_MENU_CLOSE;
    }
    openride_ui_icon_draw(ui,
                          OPENRIDE_UI_ICON_CLOSE,
                          openride_ui_rect(layout.close.x + 16.0f,
                                           layout.close.y + (layout.close.h - 22.0f) * 0.5f,
                                           22.0f,
                                           22.0f),
                          ui->theme.text_secondary,
                          1.75f);
    openride_ui_text(ui,
                     layout.close,
                     "Fermer",
                     OPENRIDE_UI_TEXT_BODY,
                     OPENRIDE_UI_TEXT_ALIGN_CENTER);
    return clicked;
}
