#ifndef OPENRIDE_TOUCH_INPUT_H
#define OPENRIDE_TOUCH_INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum OpenRideTouchActionType {
    OPENRIDE_TOUCH_ACTION_NONE = 0,
    OPENRIDE_TOUCH_ACTION_TAP,
    OPENRIDE_TOUCH_ACTION_PAN
} OpenRideTouchActionType;

typedef struct OpenRideTouchAction {
    OpenRideTouchActionType type;
    double x;
    double y;
    double dx;
    double dy;
} OpenRideTouchAction;

typedef struct OpenRideTouchInput {
    bool active;
    bool moved;
    uint64_t finger_id;
    double start_x;
    double start_y;
    double last_x;
    double last_y;
    double drag_threshold_px;
} OpenRideTouchInput;

void openride_touch_input_init(OpenRideTouchInput *input, double drag_threshold_px);
void openride_touch_input_cancel(OpenRideTouchInput *input);
void openride_touch_input_begin(OpenRideTouchInput *input,
                                uint64_t finger_id,
                                double x,
                                double y);
OpenRideTouchAction openride_touch_input_motion(OpenRideTouchInput *input,
                                                uint64_t finger_id,
                                                double x,
                                                double y);
OpenRideTouchAction openride_touch_input_end(OpenRideTouchInput *input,
                                             uint64_t finger_id,
                                             double x,
                                             double y);

double openride_touch_pinch_zoom_delta(double scale);

#endif
