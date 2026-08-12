#include "openride/app_toolbar.h"

#define OPENRIDE_TOOLBAR_HEIGHT 72.0
#define OPENRIDE_TOOLBAR_MARGIN 10.0
#define OPENRIDE_TOOLBAR_ITEM_COUNT 5.0

OpenRideToolbarRect openride_toolbar_bounds(int viewport_width, int viewport_height)
{
    OpenRideToolbarRect rect = {0};
    if (viewport_width <= 0 || viewport_height <= 0) return rect;
    rect.x = OPENRIDE_TOOLBAR_MARGIN;
    rect.w = (double)viewport_width - OPENRIDE_TOOLBAR_MARGIN * 2.0;
    if (rect.w < 0.0) rect.w = 0.0;
    rect.h = OPENRIDE_TOOLBAR_HEIGHT;
    rect.y = (double)viewport_height - rect.h - OPENRIDE_TOOLBAR_MARGIN;
    return rect;
}

OpenRideToolbarRect openride_toolbar_item_bounds(OpenRideToolbarAction action,
                                                  int viewport_width,
                                                  int viewport_height)
{
    OpenRideToolbarRect item = {0};
    if (action < OPENRIDE_TOOLBAR_MENU || action > OPENRIDE_TOOLBAR_GPS) return item;
    const OpenRideToolbarRect bar = openride_toolbar_bounds(viewport_width, viewport_height);
    const double item_width = bar.w / OPENRIDE_TOOLBAR_ITEM_COUNT;
    item.x = bar.x + item_width * (double)(action - OPENRIDE_TOOLBAR_MENU);
    item.y = bar.y;
    item.w = item_width;
    item.h = bar.h;
    return item;
}

OpenRideToolbarAction openride_toolbar_hit_test(double x,
                                                double y,
                                                int viewport_width,
                                                int viewport_height)
{
    const OpenRideToolbarRect bar = openride_toolbar_bounds(viewport_width, viewport_height);
    if (x < bar.x || y < bar.y || x >= bar.x + bar.w || y >= bar.y + bar.h || bar.w <= 0.0) {
        return OPENRIDE_TOOLBAR_NONE;
    }
    const double item_width = bar.w / OPENRIDE_TOOLBAR_ITEM_COUNT;
    int index = (int)((x - bar.x) / item_width);
    if (index < 0) index = 0;
    if (index > 4) index = 4;
    return (OpenRideToolbarAction)(OPENRIDE_TOOLBAR_MENU + index);
}

const char *openride_toolbar_action_label(OpenRideToolbarAction action)
{
    switch (action) {
        case OPENRIDE_TOOLBAR_MENU: return "Menu";
        case OPENRIDE_TOOLBAR_SEARCH: return "Chercher";
        case OPENRIDE_TOOLBAR_ROUTE: return "Trajet";
        case OPENRIDE_TOOLBAR_LOOP: return "Boucle";
        case OPENRIDE_TOOLBAR_GPS: return "GPS";
        default: return "";
    }
}
