#include "openride/touch_input.h"

#include <math.h>
#include <string.h>

void openride_touch_input_init(OpenRideTouchInput *input, double drag_threshold_px)
{
    if (!input) return;
    memset(input, 0, sizeof(*input));
    input->drag_threshold_px = drag_threshold_px > 0.0 ? drag_threshold_px : 5.0;
}

void openride_touch_input_cancel(OpenRideTouchInput *input)
{
    if (!input) return;
    const double threshold = input->drag_threshold_px;
    memset(input, 0, sizeof(*input));
    input->drag_threshold_px = threshold;
}

void openride_touch_input_begin(OpenRideTouchInput *input,
                                uint64_t finger_id,
                                double x,
                                double y)
{
    if (!input || input->active) return;
    input->active = true;
    input->moved = false;
    input->finger_id = finger_id;
    input->start_x = x;
    input->start_y = y;
    input->last_x = x;
    input->last_y = y;
}

OpenRideTouchAction openride_touch_input_motion(OpenRideTouchInput *input,
                                                uint64_t finger_id,
                                                double x,
                                                double y)
{
    OpenRideTouchAction action = {0};
    if (!input || !input->active || input->finger_id != finger_id) return action;

    const double total_dx = x - input->start_x;
    const double total_dy = y - input->start_y;
    if (!input->moved
        && hypot(total_dx, total_dy) >= input->drag_threshold_px) {
        input->moved = true;
    }

    if (input->moved) {
        action.type = OPENRIDE_TOUCH_ACTION_PAN;
        action.x = x;
        action.y = y;
        action.dx = x - input->last_x;
        action.dy = y - input->last_y;
    }

    input->last_x = x;
    input->last_y = y;
    return action;
}

OpenRideTouchAction openride_touch_input_end(OpenRideTouchInput *input,
                                             uint64_t finger_id,
                                             double x,
                                             double y)
{
    OpenRideTouchAction action = {0};
    if (!input || !input->active || input->finger_id != finger_id) return action;

    if (!input->moved) {
        action.type = OPENRIDE_TOUCH_ACTION_TAP;
        action.x = x;
        action.y = y;
    }

    openride_touch_input_cancel(input);
    return action;
}

double openride_touch_pinch_zoom_delta(double scale)
{
    if (!(scale > 0.0) || !isfinite(scale)) return 0.0;
    /* One zoom level doubles the Web Mercator world size. */
    return log(scale) / log(2.0);
}
