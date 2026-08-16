#include "openride/ui_loop_proposals_panel.h"
#include "openride/ui_icon.h"

#include <stdio.h>

static OpenRideUIID proposal_id(uint32_t index)
{
    static const char *ids[OPENRIDE_LOOP_MAX_PROPOSALS] = {
        "loop-proposal-0", "loop-proposal-1", "loop-proposal-2"
    };
    return index < OPENRIDE_LOOP_MAX_PROPOSALS ? OPENRIDE_UI_ID(ids[index]) : 0U;
}

OpenRideUILoopProposalsLayout openride_ui_loop_proposals_layout(
    const OpenRideUIContext *ui,
    uint32_t count)
{
    OpenRideUILoopProposalsLayout layout = {0};
    if (!ui) return layout;
    if (count > OPENRIDE_LOOP_MAX_PROPOSALS) count = OPENRIDE_LOOP_MAX_PROPOSALS;

    OpenRideUIRect safe = openride_ui_inset(openride_ui_safe_rect(ui), 8.0f);
    if (safe.w < 220.0f || safe.h < 340.0f) return layout;

    const float panel_w = safe.w < 430.0f ? safe.w : 430.0f;
    const float row_h = 56.0f;
    const float row_gap = 6.0f;
    const float header_h = 64.0f;
    const float confirm_h = 56.0f;
    const float actions_h = 52.0f;
    const float panel_h =
        header_h
        + (count > 0U ? (float)count * row_h + (float)(count - 1U) * row_gap : 0.0f)
        + 10.0f
        + confirm_h
        + 8.0f
        + actions_h
        + 12.0f;

    if (panel_h > safe.h) return layout;

    const float x = safe.x + (safe.w - panel_w) * 0.5f;
    const float y = safe.y + safe.h - panel_h;
    layout.panel = openride_ui_rect(x, y, panel_w, panel_h);
    layout.title = openride_ui_rect(x + 50.0f, y + 9.0f, panel_w - 66.0f, 28.0f);
    layout.subtitle = openride_ui_rect(x + 18.0f, y + 38.0f, panel_w - 36.0f, 18.0f);
    layout.count = count;

    float row_y = y + header_h;
    for (uint32_t i = 0U; i < count; ++i) {
        layout.items[i] = openride_ui_rect(x + 12.0f,
                                           row_y,
                                           panel_w - 24.0f,
                                           row_h);
        row_y += row_h + row_gap;
    }
    if (count > 0U) row_y -= row_gap;

    layout.confirm = openride_ui_rect(x + 12.0f,
                                      row_y + 10.0f,
                                      panel_w - 24.0f,
                                      confirm_h);

    const float action_y = layout.confirm.y + layout.confirm.h + 8.0f;
    const float action_gap = 8.0f;
    const float action_w = (panel_w - 24.0f - action_gap) * 0.5f;
    layout.regenerate = openride_ui_rect(x + 12.0f,
                                         action_y,
                                         action_w,
                                         actions_h);
    layout.back = openride_ui_rect(layout.regenerate.x + action_w + action_gap,
                                   action_y,
                                   action_w,
                                   actions_h);
    return layout;
}

OpenRideUILoopProposalsHit openride_ui_loop_proposals_hit_test(
    const OpenRideUIContext *ui,
    uint32_t count,
    double x_px,
    double y_px)
{
    OpenRideUILoopProposalsHit hit = {OPENRIDE_UI_LOOP_PROPOSALS_NONE, -1};
    if (!ui) return hit;
    const double scale = ui->scale > 0.0f ? (double)ui->scale : 1.0;
    const double x = x_px / scale;
    const double y = y_px / scale;
    const OpenRideUILoopProposalsLayout layout =
        openride_ui_loop_proposals_layout(ui, count);
    for (uint32_t i = 0U; i < layout.count; ++i) {
        if (openride_ui_point_in_rect(x, y, layout.items[i])) {
            hit.action = OPENRIDE_UI_LOOP_PROPOSALS_SELECT;
            hit.index = (int)i;
            return hit;
        }
    }
    if (openride_ui_point_in_rect(x, y, layout.confirm)) {
        hit.action = OPENRIDE_UI_LOOP_PROPOSALS_CONFIRM;
    } else if (openride_ui_point_in_rect(x, y, layout.regenerate)) {
        hit.action = OPENRIDE_UI_LOOP_PROPOSALS_REGENERATE;
    } else if (openride_ui_point_in_rect(x, y, layout.back)) {
        hit.action = OPENRIDE_UI_LOOP_PROPOSALS_BACK;
    }
    return hit;
}

static void format_duration(double seconds, char *buffer, size_t size)
{
    const unsigned minutes = seconds > 0.0 ? (unsigned)(seconds / 60.0 + 0.5) : 0U;
    if (minutes >= 60U) {
        snprintf(buffer, size, "%u h %02u", minutes / 60U, minutes % 60U);
    } else {
        snprintf(buffer, size, "%u min", minutes);
    }
}

OpenRideUILoopProposalsHit openride_ui_loop_proposals_draw(
    OpenRideUIContext *ui,
    const OpenRideLoopProposalSet *proposals,
    double target_distance_m,
    int preview_index)
{
    OpenRideUILoopProposalsHit hit = {OPENRIDE_UI_LOOP_PROPOSALS_NONE, -1};
    if (!ui || !ui->renderer || !proposals) return hit;
    uint32_t count = proposals->count;
    if (count > OPENRIDE_LOOP_MAX_PROPOSALS) count = OPENRIDE_LOOP_MAX_PROPOSALS;
    const OpenRideUILoopProposalsLayout layout =
        openride_ui_loop_proposals_layout(ui, count);
    if (layout.panel.w <= 0.0f) return hit;

    /* Keep the map fully readable: no fullscreen scrim on the preview sheet. */
    openride_ui_panel(ui, layout.panel, true);
    openride_ui_icon_draw(ui, OPENRIDE_UI_ICON_LOOP,
                          openride_ui_rect(layout.panel.x + 17.0f,
                                           layout.panel.y + 12.0f,
                                           24.0f, 24.0f),
                          ui->theme.primary, 1.8f);
    openride_ui_text(ui, layout.title, "Balades proposées",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    char subtitle[96];
    snprintf(subtitle, sizeof(subtitle), "%u itinéraires autour de %.0f km",
             count, target_distance_m / 1000.0);
    openride_ui_text(ui, layout.subtitle, subtitle,
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    for (uint32_t i = 0U; i < count; ++i) {
        const OpenRideLoopProposal *proposal = &proposals->items[i];
        const bool selected = preview_index >= 0 && (uint32_t)preview_index == i;

        if (openride_ui_button(ui, proposal_id(i), layout.items[i], "",
                               OPENRIDE_UI_BUTTON_GHOST,
                               true, selected)) {
            hit.action = OPENRIDE_UI_LOOP_PROPOSALS_SELECT;
            hit.index = (int)i;
        }

        char rank[56];
        char duration[32];
        char metrics[128];
        snprintf(rank, sizeof(rank), "Option %u%s%s",
                 i + 1U,
                 i == 0U ? " · recommandée" : "",
                 selected ? " · aperçu" : "");
        format_duration(proposal->route.estimated_time_s, duration, sizeof(duration));
        snprintf(metrics, sizeof(metrics),
                 "%.1f km  ·  %s  ·  score %.0f  ·  répétition %.0f %%",
                 proposal->route.distance_m / 1000.0,
                 duration,
                 proposal->stats.score,
                 proposal->stats.overlap_ratio * 100.0);

        openride_ui_text_color(ui,
                               openride_ui_rect(layout.items[i].x + 14.0f,
                                                layout.items[i].y + 5.0f,
                                                layout.items[i].w - 28.0f,
                                                22.0f),
                               rank,
                               OPENRIDE_UI_TEXT_BODY,
                               OPENRIDE_UI_TEXT_ALIGN_LEFT,
                               selected ? ui->theme.primary : ui->theme.text);
        openride_ui_text(ui,
                         openride_ui_rect(layout.items[i].x + 14.0f,
                                          layout.items[i].y + 29.0f,
                                          layout.items[i].w - 28.0f,
                                          20.0f),
                         metrics,
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
    }

    const bool can_confirm =
        count > 0U && preview_index >= 0 && (uint32_t)preview_index < count;
    if (openride_ui_button(ui, OPENRIDE_UI_ID("loop-proposals-confirm"),
                           layout.confirm, "Choisir cette balade",
                           OPENRIDE_UI_BUTTON_PRIMARY,
                           can_confirm, false)) {
        hit.action = OPENRIDE_UI_LOOP_PROPOSALS_CONFIRM;
        hit.index = preview_index;
    }

    if (openride_ui_button(ui, OPENRIDE_UI_ID("loop-proposals-regenerate"),
                           layout.regenerate, "Autres balades",
                           OPENRIDE_UI_BUTTON_GHOST, true, false)) {
        hit.action = OPENRIDE_UI_LOOP_PROPOSALS_REGENERATE;
        hit.index = -1;
    }
    if (openride_ui_button(ui, OPENRIDE_UI_ID("loop-proposals-back"),
                           layout.back, "Retour planner",
                           OPENRIDE_UI_BUTTON_GHOST, true, false)) {
        hit.action = OPENRIDE_UI_LOOP_PROPOSALS_BACK;
        hit.index = -1;
    }
    return hit;
}
