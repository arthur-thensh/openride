#include "openride/ui_main_menu.h"

#include <stddef.h>

#define OPENRIDE_UI_MAIN_MENU_ITEMS 6U
#define OPENRIDE_UI_MAIN_MENU_MARGIN 8.0f
#define OPENRIDE_UI_MAIN_MENU_GAP 8.0f
#define OPENRIDE_UI_MAIN_MENU_HEADER 56.0f
#define OPENRIDE_UI_MAIN_MENU_CLOSE_HEIGHT 54.0f
#define OPENRIDE_UI_MAIN_MENU_MAX_ROW_HEIGHT 64.0f

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

OpenRideUIMainMenuLayout openride_ui_main_menu_layout(
    const OpenRideUIContext *ui)
{
    OpenRideUIMainMenuLayout layout = {0};
    if (!ui) return layout;

    OpenRideUIRect safe = openride_ui_safe_rect(ui);
    safe = openride_ui_inset(safe, OPENRIDE_UI_MAIN_MENU_MARGIN);
    if (safe.w < 120.0f || safe.h < 180.0f) return layout;

    layout.panel = safe;
    layout.title = openride_ui_rect(safe.x + 14.0f,
                                    safe.y + 8.0f,
                                    safe.w - 28.0f,
                                    28.0f);
    layout.subtitle = openride_ui_rect(safe.x + 14.0f,
                                       safe.y + 34.0f,
                                       safe.w - 28.0f,
                                       18.0f);

    layout.close = openride_ui_rect(
        safe.x + OPENRIDE_UI_MAIN_MENU_GAP,
        safe.y + safe.h - OPENRIDE_UI_MAIN_MENU_CLOSE_HEIGHT
            - OPENRIDE_UI_MAIN_MENU_GAP,
        safe.w - OPENRIDE_UI_MAIN_MENU_GAP * 2.0f,
        OPENRIDE_UI_MAIN_MENU_CLOSE_HEIGHT);

    const float rows_top = safe.y + OPENRIDE_UI_MAIN_MENU_HEADER
        + OPENRIDE_UI_MAIN_MENU_GAP;
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
            safe.x + OPENRIDE_UI_MAIN_MENU_GAP,
            rows_top + (row_height + OPENRIDE_UI_MAIN_MENU_GAP) * (float)i,
            safe.w - OPENRIDE_UI_MAIN_MENU_GAP * 2.0f,
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
    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 115);
    SDL_RenderFillRect(ui->renderer, &screen);

    openride_ui_panel(ui, layout.panel, true);
    openride_ui_text(ui,
                     layout.title,
                     "OPENRIDE",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);
    openride_ui_text(ui,
                     layout.subtitle,
                     "Navigation moto hors ligne",
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    OpenRideUIMainMenuAction clicked = OPENRIDE_UI_MAIN_MENU_NONE;
    for (uint32_t i = 0U; i < OPENRIDE_UI_MAIN_MENU_ITEMS; ++i) {
        if (openride_ui_button(ui,
                               main_menu_id(i),
                               layout.items[i],
                               main_menu_label(i),
                               OPENRIDE_UI_BUTTON_SECONDARY,
                               true,
                               false)) {
            clicked = (OpenRideUIMainMenuAction)(
                OPENRIDE_UI_MAIN_MENU_SEARCH + (int)i);
        }
    }

    if (openride_ui_button(ui,
                           OPENRIDE_UI_ID("main-menu-close"),
                           layout.close,
                           "Fermer",
                           OPENRIDE_UI_BUTTON_GHOST,
                           true,
                           false)) {
        clicked = OPENRIDE_UI_MAIN_MENU_CLOSE;
    }
    return clicked;
}
