#include "openride/app_ui_bridge.h"

#include "map/drive_perspective_renderer.h"

/* app_ui_bridge.c is compiled with a source-local rename so this wrapper can
 * finish the map compositor immediately before the ordinary HUD is drawn. */
void openride_app_ui_draw_drive_mode_flat(
    SDL_Renderer *renderer,
    const OpenRideMBTilesMetadata *metadata,
    const OpenRideNavigationState *navigation,
    const OpenRideNavigationInstructionList *instructions,
    const OpenRideRoute *route,
    const OpenRideNavigationSession *session,
    const OpenRideDriveModeState *drive,
    bool auto_reroute,
    bool simulated_gps,
    bool simulated_gps_deviation,
    double simulated_gps_time_scale,
    bool simulated_missed_turn_armed,
    bool simulated_missed_turn_active,
    int viewport_width,
    int viewport_height);

void openride_app_ui_draw_drive_mode(
    SDL_Renderer *renderer,
    const OpenRideMBTilesMetadata *metadata,
    const OpenRideNavigationState *navigation,
    const OpenRideNavigationInstructionList *instructions,
    const OpenRideRoute *route,
    const OpenRideNavigationSession *session,
    const OpenRideDriveModeState *drive,
    bool auto_reroute,
    bool simulated_gps,
    bool simulated_gps_deviation,
    double simulated_gps_time_scale,
    bool simulated_missed_turn_armed,
    bool simulated_missed_turn_active,
    int viewport_width,
    int viewport_height)
{
    openride_drive_perspective_present(renderer,
                                       viewport_width,
                                       viewport_height);

    openride_app_ui_draw_drive_mode_flat(renderer,
                                         metadata,
                                         navigation,
                                         instructions,
                                         route,
                                         session,
                                         drive,
                                         auto_reroute,
                                         simulated_gps,
                                         simulated_gps_deviation,
                                         simulated_gps_time_scale,
                                         simulated_missed_turn_armed,
                                         simulated_missed_turn_active,
                                         viewport_width,
                                         viewport_height);
}
