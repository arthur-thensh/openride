#include "openride/app_ui_bridge.h"

#include "openride/ui.h"
#include "openride/ui_toolbar.h"
#include "openride/ui_main_menu.h"
#include "openride/ui_route_panel.h"
#include "openride/ui_loop_proposals_panel.h"
#include "openride/ui_settings_panel.h"
#include "openride/ui_regions_panel.h"
#include "openride/ui_places_panel.h"
#include "openride/ui_search_overlay.h"
#include "openride/ui_route_downloads_panel.h"
#include "openride/ui_drive_hud.h"
#include "openride/ui_map_overlay.h"
#include "openride/ui_navigation_overlay.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static double clampd(double value, double min_value, double max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

OpenRideToolbarAction openride_app_ui_toolbar_hit_test(SDL_Renderer *renderer,
                                                       double x,
                                                       double y,
                                                       int viewport_width,
                                                       int viewport_height)
{
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return OPENRIDE_TOOLBAR_NONE;
    }
    const OpenRideToolbarAction action =
        openride_ui_toolbar_hit_test(&ui, x, y);
    openride_ui_end(&ui);
    return action;
}

static void format_duration(double seconds, char *text, size_t text_size)
{
    if (!text || text_size == 0U) return;
    if (!isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const unsigned total_minutes = (unsigned)llround(seconds / 60.0);
    const unsigned hours = total_minutes / 60U;
    const unsigned minutes = total_minutes % 60U;
    if (hours > 0U) {
        snprintf(text, text_size, "%u h %02u", hours, minutes);
    } else {
        snprintf(text, text_size, "%u min", minutes);
    }
}

void openride_app_ui_draw_map_status_overlay(
    SDL_Renderer *renderer,
    const OpenRideMapCamera *camera,
    const OpenRideMapSelection *selection,
    const OpenRideMBTilesMetadata *metadata,
    bool scalable_map,
    bool graph_loaded,
    OpenRideRoutingProfile profile,
    OpenRideMapStyle map_style,
    const OpenRideRoute *route,
    bool route_valid,
    const char *route_status,
    const OpenRideRoutingSnap *start_snap,
    const OpenRideRoutingSnap *destination_snap,
    bool loop_active,
    double loop_target_distance_m,
    OpenRideLoopDirection loop_direction,
    const OpenRideLoopStats *loop_stats,
    const OpenRideGPXDocument *gpx_document,
    bool gpx_loaded,
    bool gpx_recording,
    bool gpx_navigation,
    bool compact,
    int viewport_width,
    int viewport_height)
{
    if (!renderer || !camera || !selection) return;

    OpenRideUIMapOverlayState state = {
        .compact = compact,
        .title = compact ? "OpenRide" : "OpenRide v0.23",
        .route_ready = route_valid,
        .route_ready_text = "TRAJET PRET - touche DEMARRER"
    };

    char summary[128] = {0};
    char lines[OPENRIDE_UI_MAP_OVERLAY_MAX_LINES][192] = {{0}};
    char distance_title[40] = {0};
    char distance_text[32] = {0};
    char duration_text[32] = {0};

    if (compact) {
        if (route_valid && route) {
            snprintf(summary,
                     sizeof(summary),
                     "%.1f km | %.0f min | %s",
                     route->distance_m / 1000.0,
                     route->estimated_time_s / 60.0,
                     openride_routing_profile_name(profile));
        } else {
            snprintf(summary,
                     sizeof(summary),
                     "%.80s",
                     route_status && route_status[0]
                         ? route_status
                         : "pret");
        }
        state.summary = summary;
        state.attribution = metadata && metadata->attribution[0]
            ? "(c) OpenStreetMap contributors | ODbL"
            : NULL;
    } else {
        uint32_t line_count = 0U;

        snprintf(lines[line_count],
                 sizeof(lines[line_count]),
                 "centre %.5f %.5f | z %.1f | %s",
                 camera->center_lat,
                 camera->center_lon,
                 camera->zoom,
                 scalable_map
                     ? openride_map_style_name(map_style)
                     : "raster offline");
        state.lines[line_count] = lines[line_count];
        ++line_count;

        if (selection->has_start) {
            snprintf(lines[line_count],
                     sizeof(lines[line_count]),
                     "Depart %.5f %.5f",
                     selection->start.lat,
                     selection->start.lon);
        } else {
            snprintf(lines[line_count],
                     sizeof(lines[line_count]),
                     "Clique sur la carte pour choisir le depart");
        }
        state.lines[line_count] = lines[line_count];
        ++line_count;

        if (selection->has_destination) {
            snprintf(lines[line_count],
                     sizeof(lines[line_count]),
                     "Destination %.5f %.5f",
                     selection->destination.lat,
                     selection->destination.lon);
            state.lines[line_count] = lines[line_count];
            ++line_count;
        } else if (selection->has_start) {
            snprintf(lines[line_count],
                     sizeof(lines[line_count]),
                     "%s",
                     loop_active
                         ? "Boucle generee depuis ce depart"
                         : "Clique destination ou B pour generer une boucle");
            state.lines[line_count] = lines[line_count];
            ++line_count;
        }

        snprintf(lines[line_count],
                 sizeof(lines[line_count]),
                 "Routage: %.120s | profil: %s",
                 route_status ? route_status : "-",
                 openride_routing_profile_name(profile));
        state.lines[line_count] = lines[line_count];
        ++line_count;

        if (route_valid && loop_active && loop_stats) {
            snprintf(lines[line_count],
                     sizeof(lines[line_count]),
                     "Boucle: cible %.0f km | %s | score %.0f | repetition %.0f%%",
                     loop_target_distance_m / 1000.0,
                     openride_loop_direction_name(loop_direction),
                     loop_stats->score,
                     loop_stats->overlap_ratio * 100.0);
        } else if (route_valid && gpx_navigation) {
            snprintf(lines[line_count],
                     sizeof(lines[line_count]),
                     "navigation sur trace GPX | %.1f km",
                     route ? route->distance_m / 1000.0 : 0.0);
        } else if (route_valid) {
            snprintf(lines[line_count],
                     sizeof(lines[line_count]),
                     "accroche segment: depart %.1f m | arrivee %.1f m",
                     start_snap ? start_snap->distance_m : 0.0,
                     destination_snap ? destination_snap->distance_m : 0.0);
        } else {
            snprintf(lines[line_count],
                     sizeof(lines[line_count]),
                     "1 rapide | 2 balade | 3 trail");
        }
        state.lines[line_count] = lines[line_count];
        ++line_count;

        snprintf(lines[line_count],
                 sizeof(lines[line_count]),
                 "B: generer boucle | +/-: %.0f km | O: direction %s",
                 loop_target_distance_m / 1000.0,
                 openride_loop_direction_name(loop_direction));
        state.lines[line_count] = lines[line_count];
        ++line_count;

        state.lines[line_count++] =
            "M: style carte | 1 rapide | 2 balade | 3 trail";
        state.lines[line_count++] =
            "S: GPS | F: suivi | A: recalcul auto | X: ecart test | R: manuel";
        state.lines[line_count++] =
            "glisser: deplacer | clic droit: supprimer | C: effacer";

        snprintf(lines[line_count],
                 sizeof(lines[line_count]),
                 "GPX: %s | track %u | route %u | wpt %u%s",
                 gpx_loaded ? "charge" : "aucun",
                 gpx_document ? gpx_document->track_points.count : 0U,
                 gpx_document ? gpx_document->route_points.count : 0U,
                 gpx_document ? gpx_document->waypoints.count : 0U,
                 gpx_recording ? " | ENREG" : "");
        state.lines[line_count] = lines[line_count];
        ++line_count;

        state.lines[line_count++] =
            "I: importer GPX | N: naviguer GPX | E: exporter | G: enregistrer";
        state.lines[line_count++] = "/: recherche hors ligne";
        state.line_count = line_count;

        if (route_valid
            || (selection->has_start && selection->has_destination)) {
            double distance_m = selection->has_start
                    && selection->has_destination
                ? openride_geo_distance_m(selection->start.lat,
                                          selection->start.lon,
                                          selection->destination.lat,
                                          selection->destination.lon)
                : 0.0;
            const char *title = "DISTANCE DIRECTE";

            if (route_valid && route) {
                distance_m = route->distance_m;
                title = loop_active
                    ? "BOUCLE HORS LIGNE"
                    : "ITINERAIRE HORS LIGNE";
                format_duration(route->estimated_time_s,
                                duration_text,
                                sizeof(duration_text));
            }

            snprintf(distance_title,
                     sizeof(distance_title),
                     "%s",
                     title);
            if (distance_m >= 1000.0) {
                snprintf(distance_text,
                         sizeof(distance_text),
                         "%.1f km",
                         distance_m / 1000.0);
            } else {
                snprintf(distance_text,
                         sizeof(distance_text),
                         "%.0f m",
                         distance_m);
            }

            state.show_distance = true;
            state.distance_title = distance_title;
            state.distance_text = distance_text;
            state.duration_text = duration_text;
        }

        state.attribution = metadata && metadata->attribution[0]
            ? metadata->attribution
            : NULL;
    }

    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui,
                           renderer,
                           viewport_width,
                           viewport_height)) {
        return;
    }
    openride_ui_map_overlay_draw(&ui, &state);
    openride_ui_end(&ui);
}

void openride_app_ui_draw_place_search_overlay(SDL_Renderer *renderer,
                                      bool active,
                                      bool available,
                                      const char *title,
                                      const char *query,
                                      const OpenRidePlaceSearchResult *results,
                                      uint32_t result_count,
                                      uint32_t selected_result,
                                      int viewport_width)
{
    if (!active) return;

    int viewport_height = 0;
    int queried_width = viewport_width;
    SDL_GetCurrentRenderOutputSize(renderer, &queried_width, &viewport_height);
    if (queried_width > 0) viewport_width = queried_width;
    if (viewport_height <= 0) return;

    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return;
    }

    uint32_t count = result_count;
    if (count > OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS) {
        count = OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS;
    }
    OpenRideUISearchOverlayState state = {
        .available = available,
        .title = title,
        .query = query,
        .count = count,
        .selected = selected_result
    };
    char secondary[OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS][96];
    for (uint32_t i = 0U; i < count; ++i) {
        const OpenRideRegionDefinition *result_region =
            results[i].region_id[0] != '\0'
                ? openride_region_find(results[i].region_id)
                : NULL;
        snprintf(secondary[i],
                 sizeof(secondary[i]),
                 "%s%s%s%s",
                 openride_place_kind_name(results[i].kind),
                 result_region ? " - " : "",
                 result_region ? result_region->name : "",
                 results[i].bundled_lite ? " - France" : "");
        state.items[i].name = results[i].name;
        state.items[i].secondary = secondary[i];
    }
    openride_ui_search_overlay_draw(&ui, &state);
    openride_ui_end(&ui);
}


int openride_app_ui_place_search_result_at(SDL_Renderer *renderer,
                                  double x,
                                  double y,
                                  int viewport_width,
                                  int viewport_height,
                                  uint32_t result_count)
{
    if (result_count == 0U) return -1;

    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return -1;
    }
    const int result = openride_ui_search_overlay_result_at(
        &ui,
        result_count,
        x,
        y);
    openride_ui_end(&ui);
    return result;
}

OpenRideAppPanel openride_app_ui_panel_main_at(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height)
{
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return OPENRIDE_APP_PANEL_NONE;
    }
    const OpenRideUIMainMenuAction action =
        openride_ui_main_menu_hit_test(&ui, x, y);
    openride_ui_end(&ui);

    switch (action) {
        case OPENRIDE_UI_MAIN_MENU_FAVORITES:
            return OPENRIDE_APP_PANEL_FAVORITES;
        case OPENRIDE_UI_MAIN_MENU_HISTORY:
            return OPENRIDE_APP_PANEL_HISTORY;
        case OPENRIDE_UI_MAIN_MENU_REGIONS:
            return OPENRIDE_APP_PANEL_REGIONS;
        case OPENRIDE_UI_MAIN_MENU_SETTINGS:
            return OPENRIDE_APP_PANEL_SETTINGS;
        case OPENRIDE_UI_MAIN_MENU_NONE:
        case OPENRIDE_UI_MAIN_MENU_SEARCH:
        case OPENRIDE_UI_MAIN_MENU_MAP_ZOOM_TEST:
        case OPENRIDE_UI_MAIN_MENU_CLOSE:
        default:
            return OPENRIDE_APP_PANEL_NONE;
    }
}

bool openride_app_ui_panel_main_search_at(
    SDL_Renderer *renderer,
    double x,
    double y,
    int viewport_width,
    int viewport_height)
{
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return false;
    }
    const OpenRideUIMainMenuAction action =
        openride_ui_main_menu_hit_test(&ui, x, y);
    openride_ui_end(&ui);
    return action == OPENRIDE_UI_MAIN_MENU_SEARCH;
}

int openride_app_ui_panel_place_at(SDL_Renderer *renderer,
                              double x,
                              double y,
                              int viewport_width,
                              int viewport_height,
                              uint32_t count)
{
    if (count == 0U) return -1;

    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return -1;
    }
    const OpenRideUIPlacesPanelHit hit =
        openride_ui_places_panel_hit_test(&ui, count, x, y);
    openride_ui_end(&ui);
    return hit.action == OPENRIDE_UI_PLACES_PANEL_PLACE ? hit.index : -1;
}

static void draw_ui_app_panel(SDL_Renderer *renderer,
                                  OpenRideAppPanel panel,
                                  const OpenRideStoredPlace *favorites,
                                  uint32_t favorite_count,
                                  const OpenRideStoredPlace *history,
                                  uint32_t history_count,
                                  uint32_t selected,
                                  OpenRideMapStyle map_style,
                                  OpenRideRoutingProfile profile,
                                  bool follow_gps,
                                  bool auto_reroute,
                                  bool voice_enabled,
                                  bool simulated_gps_active,
                                  bool simulated_gps_deviation,
                                  double simulated_gps_time_scale,
                                  bool simulated_missed_turn_armed,
                                  bool simulated_missed_turn_active,
                                  const OpenRideRegionDefinition *region,
                                  const OpenRideRegionStatus *region_status,
                                  bool region_is_active,
                                  bool region_busy,
                                  double region_progress,
                                  const char *region_work_status,
                                  const OpenRideMapSelection *selection,
                                  bool gps_valid,
                                  double gps_accuracy_m,
                                  OpenRideRidePlannerMode planner_mode,
                                  OpenRideRidePlannerBusy planner_busy,
                                  const char *planner_feedback,
                                  double loop_target_distance_m,
                                  OpenRideLoopDirection loop_direction,
                                  const OpenRideRouteChoice *route_choice,
                                  const OpenRideLoopProposalSet *loop_proposals,
                                  const OpenRideRouteDownloadPlan *route_download_plan_state,
                                  int viewport_width)
{
    /*
     * The state itself is owned by main(). Rendering only needs a read-only
     * snapshot. Keeping the existing dot syntax below also makes the UI block
     * independent of the pointer lifetime.
     */
    const OpenRideRouteDownloadPlan route_download_plan =
        route_download_plan_state
            ? *route_download_plan_state
            : (OpenRideRouteDownloadPlan){0};

    int width = viewport_width;
    int height = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
    if (width <= 0 || height <= 0) return;

    if (panel == OPENRIDE_APP_PANEL_MAIN) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            (void)openride_ui_main_menu_draw(&ui);
            openride_ui_end(&ui);
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_ROUTE) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            const OpenRideUIRoutePanelState state = {
                .has_start = selection && selection->has_start,
                .has_destination = selection && selection->has_destination,
                .mode = planner_mode,
                .busy = planner_busy,
                .feedback = planner_feedback,
                .gps_valid = gps_valid,
                .gps_accuracy_m = gps_accuracy_m,
                .profile = profile,
                .loop_target_distance_m = loop_target_distance_m,
                .loop_direction = loop_direction
            };
            (void)openride_ui_route_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }


    if (panel == OPENRIDE_APP_PANEL_LOOP_PROPOSALS) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            (void)openride_ui_loop_proposals_draw(&ui,
                                                  loop_proposals,
                                                  loop_target_distance_m,
                                                  route_choice
                                                      ? route_choice->preview_index
                                                      : -1);
            openride_ui_end(&ui);
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            OpenRideUIRouteDownloadsPanelState state = {
                .downloading = route_download_plan.downloading,
                .has_installed_alternative =
                    route_download_plan.has_installed_alternative,
                .count = route_download_plan.count,
                .current_index = route_download_plan.index,
                .progress = region_progress,
                .work_status = region_work_status
            };
            uint32_t count = route_download_plan.count;
            if (count > OPENRIDE_UI_ROUTE_DOWNLOADS_MAX_REGIONS) {
                count = OPENRIDE_UI_ROUTE_DOWNLOADS_MAX_REGIONS;
            }
            state.count = count;
            for (uint32_t i = 0U; i < count; ++i) {
                const OpenRideRegionDefinition *required =
                    openride_region_find(route_download_plan.region_ids[i]);
                state.region_names[i] = required
                    ? required->name
                    : route_download_plan.region_ids[i];
            }
            (void)openride_ui_route_downloads_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_SETTINGS) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            const OpenRideUISettingsPanelState state = {
                .map_style_name = openride_map_style_name(map_style),
                .routing_profile_name = openride_routing_profile_name(profile),
                .follow_gps = follow_gps,
                .auto_reroute = auto_reroute,
                .voice_enabled = voice_enabled,
                .simulated_gps_active = simulated_gps_active,
                .simulated_gps_deviation = simulated_gps_deviation,
                .simulated_gps_time_scale = simulated_gps_time_scale,
                .simulated_missed_turn_armed = simulated_missed_turn_armed,
                .simulated_missed_turn_active = simulated_missed_turn_active
            };
            (void)openride_ui_settings_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_REGIONS) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            const OpenRideUIRegionsPanelState state = {
                .region_name = region ? region->name : "Region",
                .region_is_active = region_is_active,
                .ormap_installed = region_status && region_status->ormap_installed,
                .routing_installed = region_status && region_status->routing_installed,
                .search_installed = region_status && region_status->search_installed,
                .source_pbf_present = region_status && region_status->source_pbf_present,
                .poly_present = region_status && region_status->poly_present,
                .ready = openride_region_status_ready(region_status),
                .total_size_mb = region_status ? region_status->total_size_mb : 0.0,
                .busy = region_busy,
                .progress = region_progress,
                .work_status = region_work_status
            };
            (void)openride_ui_regions_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

    if (panel == OPENRIDE_APP_PANEL_FAVORITES
        || panel == OPENRIDE_APP_PANEL_HISTORY) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            const bool favorites_panel =
                panel == OPENRIDE_APP_PANEL_FAVORITES;
            const OpenRideStoredPlace *items =
                favorites_panel ? favorites : history;
            uint32_t count = favorites_panel ? favorite_count : history_count;
            if (count > OPENRIDE_UI_PLACES_PANEL_MAX_ITEMS) {
                count = OPENRIDE_UI_PLACES_PANEL_MAX_ITEMS;
            }

            OpenRideUIPlacesPanelState state = {
                .mode = favorites_panel
                    ? OPENRIDE_UI_PLACES_PANEL_FAVORITES
                    : OPENRIDE_UI_PLACES_PANEL_HISTORY,
                .count = count,
                .selected = selected
            };
            for (uint32_t i = 0U; i < count; ++i) {
                state.items[i] = items[i].name;
            }
            (void)openride_ui_places_panel_draw(&ui, &state);
            openride_ui_end(&ui);
        }
        return;
    }

}

int openride_app_ui_panel_region_action_at(SDL_Renderer *renderer,
                                      double x,
                                      double y,
                                      int viewport_width,
                                      int viewport_height)
{
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return 0;
    }
    const OpenRideUIRegionsPanelAction action =
        openride_ui_regions_panel_hit_test(&ui, x, y);
    openride_ui_end(&ui);
    if (action == OPENRIDE_UI_REGIONS_PANEL_INSTALL) return 1;
    if (action == OPENRIDE_UI_REGIONS_PANEL_REMOVE) return 2;
    return 0;
}

void openride_app_ui_draw_panel(SDL_Renderer *renderer,
                           OpenRideAppPanel panel,
                           const OpenRideStoredPlace *favorites,
                           uint32_t favorite_count,
                           const OpenRideStoredPlace *history,
                           uint32_t history_count,
                           uint32_t selected,
                           OpenRideMapStyle map_style,
                           OpenRideRoutingProfile profile,
                           bool follow_gps,
                           bool auto_reroute,
                           bool voice_enabled,
                           bool simulated_gps_active,
                           bool simulated_gps_deviation,
                           double simulated_gps_time_scale,
                           bool simulated_missed_turn_armed,
                           bool simulated_missed_turn_active,
                           const OpenRideRegionDefinition *region,
                           const OpenRideRegionStatus *region_status,
                           bool region_is_active,
                           bool region_busy,
                           double region_progress,
                           const char *region_work_status,
                           const OpenRideMapSelection *selection,
                           bool gps_valid,
                           double gps_accuracy_m,
                           OpenRideRidePlannerMode planner_mode,
                           OpenRideRidePlannerBusy planner_busy,
                           const char *planner_feedback,
                           double loop_target_distance_m,
                           OpenRideLoopDirection loop_direction,
                           const OpenRideRouteChoice *route_choice,
                           const OpenRideLoopProposalSet *loop_proposals,
                           const OpenRideRouteDownloadPlan *route_download_plan_state,
                           int viewport_width)
{
    if (panel == OPENRIDE_APP_PANEL_NONE) return;
    draw_ui_app_panel(renderer,
                      panel,
                      favorites,
                      favorite_count,
                      history,
                      history_count,
                      selected,
                      map_style,
                      profile,
                      follow_gps,
                      auto_reroute,
                      voice_enabled,
                      simulated_gps_active,
                      simulated_gps_deviation,
                      simulated_gps_time_scale,
                      simulated_missed_turn_armed,
                      simulated_missed_turn_active,
                      region,
                      region_status,
                      region_is_active,
                      region_busy,
                      region_progress,
                      region_work_status,
                      selection,
                      gps_valid,
                      gps_accuracy_m,
                      planner_mode,
                      planner_busy,
                      planner_feedback,
                      loop_target_distance_m,
                      loop_direction,
                      route_choice,
                      loop_proposals,
                      route_download_plan_state,
                      viewport_width);
}

void openride_app_ui_draw_navigation_overlay(SDL_Renderer *renderer,
                                    const OpenRideNavigationState *navigation,
                                    const OpenRideNavigationInstructionList *instructions,
                                    const OpenRideGPSSimulator *simulator,
                                    const OpenRideRoute *route,
                                    const OpenRideNavigationSession *session,
                                    bool gps_sample_valid,
                                    bool follow_gps,
                                    bool auto_reroute,
                                    bool deviation_enabled,
                                    bool gpx_navigation,
                                    int viewport_height)
{
#ifdef __ANDROID__
    /* Android: keep the normal map clear until ui_drive_hud takes over. */
    (void)renderer;
    (void)navigation;
    (void)instructions;
    (void)simulator;
    (void)route;
    (void)session;
    (void)gps_sample_valid;
    (void)follow_gps;
    (void)auto_reroute;
    (void)deviation_enabled;
    (void)gpx_navigation;
    (void)viewport_height;
    return;
#else
    if (!gps_sample_valid || !navigation || !navigation->valid) return;

    int viewport_width = 0;
    int queried_height = viewport_height;
    SDL_GetCurrentRenderOutputSize(renderer, &viewport_width, &queried_height);
    if (viewport_width <= 0 || queried_height <= 0) return;
    viewport_height = queried_height;

    OpenRideUINavigationOverlayState state = {0};
    char title[64];
    char lines[OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES][160] = {{0}};
    uint32_t line_count = 0U;

    snprintf(title,
             sizeof(title),
             "NAVIGATION GPS SIMULEE%s",
             gpx_navigation ? " | GPX" : " | ROUTAGE");
    state.title = title;

    snprintf(lines[line_count],
             sizeof(lines[line_count]),
             "%s%s",
             openride_navigation_status_name(navigation->status),
             simulator && simulator->active ? " | lecture" : " | pause");
    state.lines[line_count] = lines[line_count];
    ++line_count;

    double instruction_distance_m = 0.0;
    const OpenRideNavigationInstruction *next_instruction =
        openride_navigation_instructions_next(instructions,
                                              navigation->traveled_m,
                                              &instruction_distance_m);
    if (next_instruction
        && line_count < OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES) {
        char maneuver_text[128];
        char distance_text[32];
        openride_navigation_instruction_text_fr(next_instruction,
                                                maneuver_text,
                                                sizeof(maneuver_text));
        openride_navigation_distance_text_fr(instruction_distance_m,
                                             distance_text,
                                             sizeof(distance_text));
        if (next_instruction->maneuver == OPENRIDE_MANEUVER_ARRIVE) {
            snprintf(lines[line_count],
                     sizeof(lines[line_count]),
                     "ARRIVEE dans %s",
                     distance_text);
        } else {
            snprintf(lines[line_count],
                     sizeof(lines[line_count]),
                     "Dans %s | %.110s",
                     distance_text,
                     maneuver_text);
        }
        state.lines[line_count] = lines[line_count];
        ++line_count;
    }

    char eta_text[32] = "--";
    if (route && route->distance_m > 0.0 && route->estimated_time_s > 0.0) {
        const double ratio = clampd(navigation->remaining_m / route->distance_m,
                                    0.0,
                                    1.0);
        format_duration(route->estimated_time_s * ratio,
                        eta_text,
                        sizeof(eta_text));
    }

    if (line_count < OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES) {
        snprintf(lines[line_count],
                 sizeof(lines[line_count]),
                 "reste %.1f km | ETA %s | progression %.1f%%",
                 navigation->remaining_m / 1000.0,
                 eta_text,
                 navigation->progress_ratio * 100.0);
        state.lines[line_count] = lines[line_count];
        ++line_count;
    }

    if (line_count < OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES) {
        snprintf(lines[line_count],
                 sizeof(lines[line_count]),
                 "ecart %.1f m | vitesse %.0f km/h",
                 navigation->distance_from_route_m,
                 navigation->speed_mps * 3.6);
        state.lines[line_count] = lines[line_count];
        ++line_count;
    }

    const OpenRideNavigationTripStats *stats =
        openride_navigation_session_stats(session);
    if (stats && line_count < OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES) {
        char elapsed_text[32];
        format_duration(stats->elapsed_s,
                        elapsed_text,
                        sizeof(elapsed_text));
        snprintf(lines[line_count],
                 sizeof(lines[line_count]),
                 "trajet %.1f km | %s | moy %.0f | max %.0f km/h",
                 stats->gps_distance_m / 1000.0,
                 elapsed_text,
                 stats->average_speed_mps * 3.6,
                 stats->max_speed_mps * 3.6);
        state.lines[line_count] = lines[line_count];
        ++line_count;
    }

    if (stats && line_count < OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES) {
        snprintf(lines[line_count],
                 sizeof(lines[line_count]),
                 "recalcul auto %s | recalculs %u",
                 auto_reroute ? "ON" : "OFF",
                 stats->reroute_count);
        state.lines[line_count] = lines[line_count];
        ++line_count;
    }

    if (line_count < OPENRIDE_UI_NAVIGATION_OVERLAY_MAX_LINES) {
        snprintf(lines[line_count],
                 sizeof(lines[line_count]),
                 "S lecture | F suivi %s | A auto %s | X deviation %s | R manuel",
                 follow_gps ? "ON" : "OFF",
                 auto_reroute ? "ON" : "OFF",
                 deviation_enabled ? "ON" : "OFF");
        state.lines[line_count] = lines[line_count];
        ++line_count;
    }

    state.line_count = line_count;

    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui,
                           renderer,
                           viewport_width,
                           viewport_height)) {
        return;
    }
    openride_ui_navigation_overlay_draw(&ui, &state);
    openride_ui_end(&ui);
#endif
}

void openride_app_ui_draw_toolbar(SDL_Renderer *renderer, int viewport_width, int viewport_height, bool route_ready)
{
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return;
    }
    (void)openride_ui_toolbar_draw(&ui, route_ready);
    openride_ui_end(&ui);
}


OpenRideDriveAction openride_app_ui_drive_controls_hit_test(SDL_Renderer *renderer,
                                                   double x,
                                                   double y,
                                                   int viewport_width,
                                                   int viewport_height)
{
    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return OPENRIDE_DRIVE_ACTION_NONE;
    }

    const OpenRideUIDriveHUDAction action =
        openride_ui_drive_hud_hit_test(&ui, x, y);
    openride_ui_end(&ui);

    switch (action) {
        case OPENRIDE_UI_DRIVE_HUD_EXIT:
            return OPENRIDE_DRIVE_ACTION_EXIT;
        case OPENRIDE_UI_DRIVE_HUD_RECENTER:
            return OPENRIDE_DRIVE_ACTION_RECENTER;
        case OPENRIDE_UI_DRIVE_HUD_ORIENTATION:
            return OPENRIDE_DRIVE_ACTION_ORIENTATION;
        case OPENRIDE_UI_DRIVE_HUD_GPS:
            return OPENRIDE_DRIVE_ACTION_GPS;
        case OPENRIDE_UI_DRIVE_HUD_NONE:
        default:
            return OPENRIDE_DRIVE_ACTION_NONE;
    }
}

static OpenRideUIDriveHUDManeuver drive_hud_maneuver(
    OpenRideManeuverType maneuver)
{
    switch (maneuver) {
        case OPENRIDE_MANEUVER_DEPART:
            return OPENRIDE_UI_DRIVE_MANEUVER_DEPART;
        case OPENRIDE_MANEUVER_SLIGHT_LEFT:
            return OPENRIDE_UI_DRIVE_MANEUVER_SLIGHT_LEFT;
        case OPENRIDE_MANEUVER_LEFT:
            return OPENRIDE_UI_DRIVE_MANEUVER_LEFT;
        case OPENRIDE_MANEUVER_SHARP_LEFT:
            return OPENRIDE_UI_DRIVE_MANEUVER_SHARP_LEFT;
        case OPENRIDE_MANEUVER_SLIGHT_RIGHT:
            return OPENRIDE_UI_DRIVE_MANEUVER_SLIGHT_RIGHT;
        case OPENRIDE_MANEUVER_RIGHT:
            return OPENRIDE_UI_DRIVE_MANEUVER_RIGHT;
        case OPENRIDE_MANEUVER_SHARP_RIGHT:
            return OPENRIDE_UI_DRIVE_MANEUVER_SHARP_RIGHT;
        case OPENRIDE_MANEUVER_UTURN:
            return OPENRIDE_UI_DRIVE_MANEUVER_UTURN;
        case OPENRIDE_MANEUVER_ROUNDABOUT:
            return OPENRIDE_UI_DRIVE_MANEUVER_ROUNDABOUT;
        case OPENRIDE_MANEUVER_ARRIVE:
            return OPENRIDE_UI_DRIVE_MANEUVER_ARRIVE;
        case OPENRIDE_MANEUVER_CONTINUE:
        default:
            return OPENRIDE_UI_DRIVE_MANEUVER_CONTINUE;
    }
}

static OpenRideUIDriveHUDGPSQuality drive_hud_gps_quality(
    OpenRideGPSQuality quality)
{
    switch (quality) {
        case OPENRIDE_GPS_GOOD:
            return OPENRIDE_UI_DRIVE_GPS_GOOD;
        case OPENRIDE_GPS_FAIR:
            return OPENRIDE_UI_DRIVE_GPS_FAIR;
        case OPENRIDE_GPS_POOR:
            return OPENRIDE_UI_DRIVE_GPS_POOR;
        case OPENRIDE_GPS_LOST:
            return OPENRIDE_UI_DRIVE_GPS_LOST;
        case OPENRIDE_GPS_UNAVAILABLE:
        default:
            return OPENRIDE_UI_DRIVE_GPS_UNAVAILABLE;
    }
}

static void format_arrival_clock(double remaining_seconds, char *text, size_t text_size)
{
    if (!text || text_size == 0U) return;
    if (!isfinite(remaining_seconds) || remaining_seconds < 0.0) {
        snprintf(text, text_size, "--:--");
        return;
    }
    time_t now = time(NULL);
    time_t arrival = now + (time_t)llround(remaining_seconds);
    struct tm *local = localtime(&arrival);
    if (!local || strftime(text, text_size, "%H:%M", local) == 0U) {
        snprintf(text, text_size, "--:--");
    }
}

void openride_app_ui_draw_drive_mode(SDL_Renderer *renderer,
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
    if (!renderer || !drive || !drive->active) return;

    OpenRideUIDriveHUDStatus hud_status = OPENRIDE_UI_DRIVE_HUD_ACTIVE;
    if (navigation && navigation->status == OPENRIDE_NAVIGATION_OFF_ROUTE) {
        hud_status = OPENRIDE_UI_DRIVE_HUD_OFF_ROUTE;
    } else if (navigation && navigation->status == OPENRIDE_NAVIGATION_ARRIVED) {
        hud_status = OPENRIDE_UI_DRIVE_HUD_ARRIVED;
    }

    double instruction_distance_m = INFINITY;
    const OpenRideNavigationInstruction *next_instruction = NULL;
    if (navigation && navigation->valid) {
        next_instruction = openride_navigation_instructions_next(
            instructions,
            navigation->traveled_m,
            &instruction_distance_m);
    }

    char distance_text[32] = "--";
    char maneuver_text[128] = "Suivre l'itineraire";
    char primary_text[64] = "DANS --";
    OpenRideUIDriveHUDManeuver maneuver =
        OPENRIDE_UI_DRIVE_MANEUVER_CONTINUE;
    if (next_instruction) {
        openride_navigation_distance_text_fr(instruction_distance_m,
                                             distance_text,
                                             sizeof(distance_text));
        openride_navigation_instruction_text_fr(next_instruction,
                                                maneuver_text,
                                                sizeof(maneuver_text));
        maneuver = drive_hud_maneuver(next_instruction->maneuver);
        if (next_instruction->maneuver == OPENRIDE_MANEUVER_ARRIVE) {
            snprintf(primary_text,
                     sizeof(primary_text),
                     "ARRIVEE %s",
                     distance_text);
        } else {
            snprintf(primary_text,
                     sizeof(primary_text),
                     "DANS %s",
                     distance_text);
        }
    }

    double following_gap_m = INFINITY;
    const OpenRideNavigationInstruction *following_instruction = NULL;
    if (next_instruction
        && next_instruction->maneuver != OPENRIDE_MANEUVER_ARRIVE) {
        following_instruction = openride_navigation_instructions_after(
            instructions,
            next_instruction->distance_from_start_m,
            &following_gap_m);
    }
    const bool show_following =
        following_instruction
        && isfinite(following_gap_m)
        && following_gap_m <= 300.0
        && hud_status == OPENRIDE_UI_DRIVE_HUD_ACTIVE;

    char following_text[180] = {0};
    if (show_following) {
        char following_distance_text[32];
        char following_maneuver_text[128];
        openride_navigation_distance_text_fr(
            following_gap_m,
            following_distance_text,
            sizeof(following_distance_text));
        openride_navigation_instruction_text_fr(
            following_instruction,
            following_maneuver_text,
            sizeof(following_maneuver_text));
        snprintf(following_text,
                 sizeof(following_text),
                 "PUIS %s | %.120s",
                 following_distance_text,
                 following_maneuver_text);
    }

    char simulation_prefix[40] = {0};
    if (simulated_gps) {
        const char *format =
            simulated_missed_turn_active
                ? "SIM x%.0f RATE | "
                : simulated_missed_turn_armed
                    ? "SIM x%.0f ARME | "
                    : simulated_gps_deviation
                        ? "SIM x%.0f +80m | "
                        : "SIM x%.0f DEV | ";
        snprintf(simulation_prefix,
                 sizeof(simulation_prefix),
                 format,
                 simulated_gps_time_scale);
    }

    char gps_text[80];
    if (drive->gps_quality == OPENRIDE_GPS_GOOD
        || drive->gps_quality == OPENRIDE_GPS_FAIR) {
        snprintf(gps_text,
                 sizeof(gps_text),
                 "%s%s %.0f m",
                 simulation_prefix,
                 openride_drive_mode_gps_quality_name(drive->gps_quality),
                 drive->gps_accuracy_m);
    } else {
        snprintf(gps_text,
                 sizeof(gps_text),
                 "%s%s",
                 simulation_prefix,
                 openride_drive_mode_gps_quality_name(drive->gps_quality));
    }

    const double speed_kph = navigation && navigation->valid
        ? navigation->speed_mps * 3.6
        : 0.0;
    const double remaining_m = navigation && navigation->valid
        ? navigation->remaining_m
        : (route ? route->distance_m : 0.0);
    double remaining_s = 0.0;
    if (route && route->distance_m > 0.0 && route->estimated_time_s > 0.0) {
        remaining_s = route->estimated_time_s
            * clampd(remaining_m / route->distance_m, 0.0, 1.0);
    }
    char arrival_text[16];
    format_arrival_clock(remaining_s,
                         arrival_text,
                         sizeof(arrival_text));

    uint32_t reroute_count = 0U;
    if (session) {
        const OpenRideNavigationTripStats *trip =
            openride_navigation_session_stats(session);
        if (trip) reroute_count = trip->reroute_count;
    }

    const OpenRideUIDriveHUDState state = {
        .status = hud_status,
        .maneuver = maneuver,
        .primary_text = primary_text,
        .maneuver_text = maneuver_text,
        .show_following = show_following,
        .following_text = following_text,
        .auto_reroute = auto_reroute,
        .gps_quality = drive_hud_gps_quality(drive->gps_quality),
        .gps_text = gps_text,
        .speed_kph = speed_kph,
        .remaining_m = remaining_m,
        .arrival_text = arrival_text,
        .reroute_count = reroute_count,
        .heading_up = drive->heading_up,
        .show_attribution = metadata && metadata->attribution[0] != '\0'
    };

    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui,
                           renderer,
                           viewport_width,
                           viewport_height)) {
        return;
    }
    openride_ui_drive_hud_draw(&ui, &state);
    openride_ui_end(&ui);
}

