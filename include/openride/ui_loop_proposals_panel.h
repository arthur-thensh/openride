#ifndef OPENRIDE_UI_LOOP_PROPOSALS_PANEL_H
#define OPENRIDE_UI_LOOP_PROPOSALS_PANEL_H

#include "openride/loop_generator.h"
#include "openride/ui.h"

#include <stdint.h>

typedef enum OpenRideUILoopProposalsAction {
    OPENRIDE_UI_LOOP_PROPOSALS_NONE = 0,
    OPENRIDE_UI_LOOP_PROPOSALS_SELECT,
    OPENRIDE_UI_LOOP_PROPOSALS_CONFIRM,
    OPENRIDE_UI_LOOP_PROPOSALS_REGENERATE,
    OPENRIDE_UI_LOOP_PROPOSALS_BACK
} OpenRideUILoopProposalsAction;

typedef struct OpenRideUILoopProposalsHit {
    OpenRideUILoopProposalsAction action;
    int index;
} OpenRideUILoopProposalsHit;

typedef struct OpenRideUILoopProposalsLayout {
    OpenRideUIRect panel;
    OpenRideUIRect title;
    OpenRideUIRect subtitle;
    OpenRideUIRect items[OPENRIDE_LOOP_MAX_PROPOSALS];
    uint32_t count;
    OpenRideUIRect confirm;
    OpenRideUIRect regenerate;
    OpenRideUIRect back;
} OpenRideUILoopProposalsLayout;

OpenRideUILoopProposalsLayout openride_ui_loop_proposals_layout(
    const OpenRideUIContext *ui,
    uint32_t count);

OpenRideUILoopProposalsHit openride_ui_loop_proposals_hit_test(
    const OpenRideUIContext *ui,
    uint32_t count,
    double x_px,
    double y_px);

OpenRideUILoopProposalsHit openride_ui_loop_proposals_draw(
    OpenRideUIContext *ui,
    const OpenRideLoopProposalSet *proposals,
    double target_distance_m,
    int preview_index);

#endif
