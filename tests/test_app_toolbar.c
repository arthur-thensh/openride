#include "openride/app_toolbar.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const OpenRideToolbarRect bar = openride_toolbar_bounds(1000, 800);
    assert(bar.w > 900.0);
    assert(bar.y > 700.0);

    for (OpenRideToolbarAction action = OPENRIDE_TOOLBAR_MENU;
         action <= OPENRIDE_TOOLBAR_GPS;
         action = (OpenRideToolbarAction)(action + 1)) {
        const OpenRideToolbarRect item = openride_toolbar_item_bounds(action, 1000, 800);
        assert(openride_toolbar_hit_test(item.x + item.w * 0.5,
                                        item.y + item.h * 0.5,
                                        1000,
                                        800) == action);
        assert(strlen(openride_toolbar_action_label(action)) > 0U);
    }

    puts("App toolbar tests: OK");
    return 0;
}
