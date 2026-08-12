#include "openride/touch_input.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    OpenRideTouchInput input;
    openride_touch_input_init(&input, 5.0);

    openride_touch_input_begin(&input, 7U, 100.0, 100.0);
    OpenRideTouchAction action = openride_touch_input_motion(&input, 7U, 102.0, 102.0);
    assert(action.type == OPENRIDE_TOUCH_ACTION_NONE);
    action = openride_touch_input_end(&input, 7U, 102.0, 102.0);
    assert(action.type == OPENRIDE_TOUCH_ACTION_TAP);

    openride_touch_input_begin(&input, 8U, 10.0, 10.0);
    action = openride_touch_input_motion(&input, 8U, 30.0, 20.0);
    assert(action.type == OPENRIDE_TOUCH_ACTION_PAN);
    assert(fabs(action.dx - 20.0) < 1e-9);
    assert(fabs(action.dy - 10.0) < 1e-9);
    action = openride_touch_input_end(&input, 8U, 30.0, 20.0);
    assert(action.type == OPENRIDE_TOUCH_ACTION_NONE);

    assert(fabs(openride_touch_pinch_zoom_delta(2.0) - 1.0) < 1e-9);
    assert(fabs(openride_touch_pinch_zoom_delta(0.5) + 1.0) < 1e-9);

    puts("Touch input tests: OK");
    return 0;
}
