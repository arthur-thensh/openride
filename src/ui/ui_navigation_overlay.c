#include "openride/ui_navigation_overlay.h"

#define OPENRIDE_UI_NAV_OVERLAY_MARGIN 8.0f
#define OPENRIDE_UI_NAV_OVERLAY_GAP 4.0f
#define OPENRIDE_UI_NAV_OVERLAY_MAX_WIDTH 540.0f
#define OPENRIDE_UI_NAV_OVERLAY_LINE_HEIGHT 18.0f
#define OPENRIDE_UI_NAV_OVERLAY_TOOLBAR_CLEARANCE 90.0f

static float minf_openride(float a, float b)
{
    return a < b ? a : b;
}

void openride_ui_navigation_overlay_draw(
    OpenRideUIContext *ui,
    const OpenRideUINavigationOverlayState *state)
{
    if (!ui || !ui->renderer || !state) return;

    OpenRideUIRect safe = openride_ui_safe_rect(ui);
    safe = openride_ui_inset(safe, OPENRIDE_UI_NAV_OVERLAY_MARGIN);
    if (safe.w <= 0.0f || safe.h <= 0.0f) return;

    uint32_t count = state->line_count;
    if (count > OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES) {
        count = OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES;
    }

    const float width = minf_openride(
        safe.w,
        OPENRIDE_UI_NAV_OVERLAY_MAX_WIDTH);
    const float height = 38.0f
        + (OPENRIDE_UI_NAV_OVERLAY_LINE_HEIGHT + OPENRIDE_UI_NAV_OVERLAY_GAP)
            * (float)count
        + 8.0f;

    /*
     * Keep this below the normal map-status overlay and above the map toolbar.
     * The drive HUD replaces this component entirely once active navigation
     * mode takes over.
     */
    const float bottom = safe.y + safe.h
        - OPENRIDE_UI_NAV_OVERLAY_TOOLBAR_CLEARANCE;
    float y = safe.y + 235.0f;
    if (y + height > bottom) {
        y = bottom - height;
    }
    if (y < safe.y) y = safe.y;

    const OpenRideUIRect panel = openride_ui_rect(safe.x, y, width, height);
    openride_ui_panel(ui, panel, true);

    openride_ui_text(ui,
                     openride_ui_rect(panel.x + 12.0f,
                                      panel.y + 8.0f,
                                      panel.w - 24.0f,
                                      22.0f),
                     state->title ? state->title : "NAVIGATION",
                     OPENRIDE_UI_TEXT_TITLE,
                     OPENRIDE_UI_TEXT_ALIGN_LEFT);

    for (uint32_t i = 0U; i < count; ++i) {
        const char *line = state->lines[i];
        if (!line || !line[0]) continue;
        openride_ui_text(ui,
                         openride_ui_rect(
                             panel.x + 12.0f,
                             panel.y + 34.0f
                                 + (OPENRIDE_UI_NAV_OVERLAY_LINE_HEIGHT
                                    + OPENRIDE_UI_NAV_OVERLAY_GAP)
                                     * (float)i,
                             panel.w - 24.0f,
                             OPENRIDE_UI_NAV_OVERLAY_LINE_HEIGHT),
                         line,
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_LEFT);
    }
}
