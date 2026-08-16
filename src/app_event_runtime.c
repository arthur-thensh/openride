#include "app_event_runtime.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define OPENRIDE_CLICK_DRAG_THRESHOLD 5.0
#define OPENRIDE_LOOP_DISTANCE_STEP_M 25000.0
#define OPENRIDE_LOOP_DISTANCE_MIN_M 25000.0
#define OPENRIDE_LOOP_DISTANCE_MAX_M 300000.0

void openride_app_events_fit_route_choice_preview(OpenRideAppEventContext *context)
{
    if (!context || !context->renderer || !context->camera || !context->route_choice) {
        return;
    }
    const OpenRideRoute *preview =
        openride_route_choice_preview_route(context->route_choice);
    if (!preview) return;

    int width = 0;
    int height = 0;
    if (!SDL_GetCurrentRenderOutputSize(context->renderer, &width, &height)
        || width <= 0 || height <= 0) {
        return;
    }

    const OpenRideAppUIMapInsets insets =
        openride_app_ui_loop_proposals_map_insets(
            context->renderer,
            context->route_choice->proposal_count,
            width,
            height);

    const OpenRideMBTilesMetadata *metadata =
        context->metadata ? (*context->metadata) : NULL;
    const bool scalable =
        context->scalable_map && (*context->scalable_map);
    const double min_zoom =
        context->map_world && scalable
            ? OPENRIDE_MAP_WORLD_MIN_ZOOM
            : metadata ? (double)metadata->min_zoom : 1.0;
    const double max_zoom =
        scalable
            ? 18.0
            : metadata ? (double)metadata->max_zoom : 18.0;

    openride_app_route_fit_camera_to_route(context->camera,
                                           preview,
                                           width,
                                           height,
                                           min_zoom,
                                           max_zoom,
                                           insets.left,
                                           insets.top,
                                           insets.right,
                                           insets.bottom);
}


void openride_app_events_poll(OpenRideAppEventContext *context,
                              uint64_t *map_zoom_loop_started_ns)
{
    if (!context || !context->window || !context->renderer
        || !context->running || !map_zoom_loop_started_ns) return;

            SDL_Event event;

            while (SDL_PollEvent(&event)) {
                SDL_ConvertEventToRenderCoordinates(context->renderer, &event);

                switch (event.type) {
                    case SDL_EVENT_QUIT:
                        (*context->running) = false;
                        break;

                    case SDL_EVENT_KEY_DOWN:
                        if ((*context->app_panel) != OPENRIDE_APP_PANEL_NONE) {
                            if (event.key.key == SDLK_TAB) {
                                if ((*context->app_panel) == OPENRIDE_APP_PANEL_LOOP_PROPOSALS) {
                                    openride_route_choice_reset(context->route_choice);
                                }
                                (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                            } else if (event.key.key == SDLK_ESCAPE) {
                                if ((*context->app_panel) == OPENRIDE_APP_PANEL_LOOP_PROPOSALS) {
                                    openride_route_choice_reset(context->route_choice);
                                }
                                (*context->app_panel) = (*context->app_panel) == OPENRIDE_APP_PANEL_MAIN
                                    ? OPENRIDE_APP_PANEL_NONE
                                    : OPENRIDE_APP_PANEL_MAIN;
                                (*context->app_panel_selected) = 0U;
                            } else if ((*context->app_panel) == OPENRIDE_APP_PANEL_MAIN) {
                                if (event.key.key == SDLK_R) {
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                    openride_app_search_open(context->window,
                                                      context->place_world,
                                                      &(*context->place_search_active),
                                                      context->place_search_query,
                                                      &(*context->place_search_result_count),
                                                      &(*context->place_search_selected),
                                                      context->route_status,
                                                      context->route_status_size);
                                } else if (event.key.key == SDLK_F) {
                                    openride_app_search_refresh_stored_places(context->app_storage, true, context->favorite_places, &(*context->favorite_count));
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_FAVORITES;
                                    (*context->app_panel_selected) = 0U;
                                } else if (event.key.key == SDLK_H) {
                                    openride_app_search_refresh_stored_places(context->app_storage, false, context->history_places, &(*context->history_count));
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_HISTORY;
                                    (*context->app_panel_selected) = 0U;
                                } else if (event.key.key == SDLK_C) {
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_REGIONS;
                                } else if (event.key.key == SDLK_P) {
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_SETTINGS;
                                } else if (event.key.key == SDLK_Z) {
                                    if ((*context->map_zoom_test).active) {
                                        openride_map_zoom_test_cancel(&(*context->map_zoom_test));
                                        snprintf(context->route_status, context->route_status_size,
                                                 "test zoom carte annule");
                                    } else {
                                        openride_map_zoom_test_start(&(*context->map_zoom_test), &(*context->camera), &(*context->platform_paths));
                                        (*map_zoom_loop_started_ns) = SDL_GetTicksNS();
                                        (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                        snprintf(context->route_status, context->route_status_size,
                                                 "test zoom 9.000 -> 17.000 -> 9.000 | log data/map-zoom-test.csv");
                                    }
                                }
                            } else if ((*context->app_panel) == OPENRIDE_APP_PANEL_ROUTE) {
                                if (event.key.key == SDLK_D) {
                                    (*context->place_search_purpose) =
                                        OPENRIDE_PLACE_SEARCH_ROUTE_START;
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                    openride_app_search_open(context->window,
                                                      context->place_world,
                                                      &(*context->place_search_active),
                                                      context->place_search_query,
                                                      &(*context->place_search_result_count),
                                                      &(*context->place_search_selected),
                                                      context->route_status,
                                                      context->route_status_size);
                                } else if (event.key.key == SDLK_A) {
                                    (*context->place_search_purpose) =
                                        OPENRIDE_PLACE_SEARCH_ROUTE_DESTINATION;
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                    openride_app_search_open(context->window,
                                                      context->place_world,
                                                      &(*context->place_search_active),
                                                      context->place_search_query,
                                                      &(*context->place_search_result_count),
                                                      &(*context->place_search_selected),
                                                      context->route_status,
                                                      context->route_status_size);
                                } else if ((event.key.key == SDLK_RETURN
                                            || event.key.key == SDLK_KP_ENTER)
                                           && openride_map_selection_complete(&(*context->selection))) {
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                    (*context->route_dirty) = true;
                                }
                            } else if ((*context->app_panel) == OPENRIDE_APP_PANEL_FAVORITES
                                       || (*context->app_panel) == OPENRIDE_APP_PANEL_HISTORY) {
                                const bool favorites_panel = (*context->app_panel) == OPENRIDE_APP_PANEL_FAVORITES;
                                const uint32_t count = favorites_panel ? (*context->favorite_count) : (*context->history_count);
                                OpenRideStoredPlace *items = favorites_panel ? context->favorite_places : context->history_places;
                                if (event.key.key == SDLK_UP && count > 0U) {
                                    (*context->app_panel_selected) = (*context->app_panel_selected) == 0U ? count - 1U : (*context->app_panel_selected) - 1U;
                                } else if (event.key.key == SDLK_DOWN && count > 0U) {
                                    (*context->app_panel_selected) = ((*context->app_panel_selected) + 1U) % count;
                                } else if ((event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)
                                           && count > 0U) {
                                    (*context->camera).center_lat = items[(*context->app_panel_selected)].lat;
                                    (*context->camera).center_lon = items[(*context->app_panel_selected)].lon;
                                    if ((*context->camera).zoom < 14.0) (*context->camera).zoom = 14.0;
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                } else if (favorites_panel
                                           && (event.key.key == SDLK_DELETE || event.key.key == SDLK_BACKSPACE)
                                           && count > 0U && context->app_storage) {
                                    openride_app_storage_remove_favorite(context->app_storage,
                                                                         items[(*context->app_panel_selected)].id,
                                                                         context->error,
                                                                         context->error_size);
                                    openride_app_search_refresh_stored_places(context->app_storage, true, context->favorite_places, &(*context->favorite_count));
                                    if ((*context->app_panel_selected) >= (*context->favorite_count) && (*context->app_panel_selected) > 0U) {
                                        --(*context->app_panel_selected);
                                    }
                                }
                            } else if ((*context->app_panel) == OPENRIDE_APP_PANEL_REGIONS) {
                                if ((event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) && !(*context->region_busy)) {
                                    (*context->region) = openride_app_region_step((*context->region), event.key.key == SDLK_LEFT ? -1 : 1);
                                    openride_region_get_status(&(*context->platform_paths),
                                                               (*context->region),
                                                               &(*context->region_status),
                                                               context->error,
                                                               context->error_size);
                                    context->region_work_status[0] = '\0';
                                } else if (event.key.key == SDLK_D
                                           || event.key.key == SDLK_RETURN
                                           || event.key.key == SDLK_KP_ENTER) {
                                    if (openride_region_status_ready(&(*context->region_status))
    #ifdef __ANDROID__
                                        && (*context->region_status).poly_present
    #endif
                                    ) {
                                        if ((*context->region) == (*context->active_region)) {
                                            snprintf(context->region_work_status, context->region_work_status_size,
                                                     "Cette region est deja active");
                                        } else {
                                            (*context->region_activation_requested) = true;
                                        }
                                    } else {
    #ifdef __ANDROID__
                                        openride_app_region_begin_android_install(&(*context->platform_paths),
                                                                     (*context->region),
                                                                     &(*context->region_status),
                                                                     &(*context->region_prepare_context),
                                                                     &(*context->region_prepare_thread),
                                                                     &(*context->region_download_started),
                                                                     &(*context->region_download_is_poly),
                                                                     &(*context->region_busy),
                                                                     &(*context->region_progress),
                                                                     context->region_work_status,
                                                                     context->region_work_status_size,
                                                                     context->error,
                                                                     context->error_size);
    #else
                                        if ((*context->region_status).source_pbf_present) {
                                            snprintf(context->region_work_status, context->region_work_status_size,
                                                     "Prepare cette region depuis le Terminal macOS");
                                        } else {
                                            snprintf(context->region_work_status, context->region_work_status_size,
                                                     "PBF regional absent sur macOS");
                                        }
    #endif
                                    }
                                } else if (event.key.key == SDLK_S && !(*context->region_busy)) {
                                    if ((*context->region) == (*context->active_region)) {
                                        snprintf(context->region_work_status, context->region_work_status_size,
                                                 "Impossible de supprimer la region active");
                                    } else if (openride_region_remove_generated(&(*context->platform_paths),
                                                                                (*context->region),
                                                                                context->error,
                                                                                context->error_size)) {
                                        openride_region_get_status(&(*context->platform_paths), (*context->region),
                                                                   &(*context->region_status), context->error, context->error_size);
                                        openride_app_region_refresh_map_world_overview(context->map_world, &(*context->platform_paths));
                                        snprintf(context->region_work_status, context->region_work_status_size,
                                                 "Donnees de la region supprimees");
                                    } else {
                                        snprintf(context->region_work_status, context->region_work_status_size,
                                                 "Suppression impossible: %.120s", context->error);
                                    }
                                }
                            } else if ((*context->app_panel) == OPENRIDE_APP_PANEL_SETTINGS) {
                                if (event.key.key == SDLK_M && (*context->scalable_map)) {
                                    (*context->map_style) = openride_map_style_next((*context->map_style));
                                    if ((*context->ormap_map)) openride_ormap_renderer_set_style(&(*context->ormap_renderer), (*context->map_style));
                                    else if ((*context->vector_map)) openride_vector_map_renderer_set_style(&(*context->vector_renderer), (*context->map_style));
                                    if (context->app_storage) openride_app_storage_set_int(context->app_storage, "map_style", (int)(*context->map_style), context->error, context->error_size);
                                } else if (event.key.key == SDLK_1 || event.key.key == SDLK_2 || event.key.key == SDLK_3) {
                                    (*context->routing_profile) = event.key.key == SDLK_1 ? OPENRIDE_ROUTING_PROFILE_FASTEST
                                                      : event.key.key == SDLK_2 ? OPENRIDE_ROUTING_PROFILE_TOURING
                                                                               : OPENRIDE_ROUTING_PROFILE_TRAIL;
                                    if (context->app_storage) openride_app_storage_set_int(context->app_storage, "routing_profile", (int)(*context->routing_profile), context->error, context->error_size);
                                } else if (event.key.key == SDLK_F) {
                                    (*context->follow_gps) = !(*context->follow_gps);
                                    if (context->app_storage) openride_app_storage_set_int(context->app_storage, "follow_gps", (*context->follow_gps) ? 1 : 0, context->error, context->error_size);
                                } else if (event.key.key == SDLK_A) {
                                    (*context->auto_reroute) = !(*context->auto_reroute);
                                    openride_navigation_session_set_auto_reroute(&(*context->navigation_session), (*context->auto_reroute));
                                    if (context->app_storage) openride_app_storage_set_int(context->app_storage, "auto_reroute", (*context->auto_reroute) ? 1 : 0, context->error, context->error_size);
                                } else if (event.key.key == SDLK_V) {
                                    (*context->voice_enabled) = !(*context->voice_enabled);
                                    openride_voice_guidance_set_enabled(&(*context->voice_guidance), (*context->voice_enabled));
    #ifdef __ANDROID__
                                    if ((*context->voice_enabled)) openride_android_voice_guidance_init();
    #endif
                                    if (context->app_storage) openride_app_storage_set_int(context->app_storage, "voice_enabled", (*context->voice_enabled) ? 1 : 0, context->error, context->error_size);
                                }
                            }
                            break;
                        }

                        if ((*context->place_search_active)) {
                            if (event.key.key == SDLK_ESCAPE) {
                                (*context->place_search_active) = false;
                                if ((*context->place_search_purpose)
                                    != OPENRIDE_PLACE_SEARCH_BROWSE) {
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_ROUTE;
                                }
                                (*context->place_search_purpose) =
                                    OPENRIDE_PLACE_SEARCH_BROWSE;
                                SDL_StopTextInput(context->window);
                            } else if (event.key.key == SDLK_BACKSPACE) {
                                openride_app_search_utf8_backspace(context->place_search_query);
                                openride_app_search_refresh(context->place_world,
                                                     context->place_search_query,
                                                     context->place_search_results,
                                                     &(*context->place_search_result_count),
                                                     &(*context->place_search_selected),
                                                     context->route_status,
                                                     context->route_status_size);
                            } else if (event.key.key == SDLK_UP && (*context->place_search_result_count) > 0U) {
                                if ((*context->place_search_selected) == 0U) {
                                    (*context->place_search_selected) = (*context->place_search_result_count) - 1U;
                                } else {
                                    --(*context->place_search_selected);
                                }
                            } else if (event.key.key == SDLK_DOWN && (*context->place_search_result_count) > 0U) {
                                (*context->place_search_selected) = ((*context->place_search_selected) + 1U) % (*context->place_search_result_count);
                            } else if (event.key.key == SDLK_D
                                       && (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0
                                       && (*context->place_search_result_count) > 0U && context->app_storage) {
                                const OpenRidePlaceSearchResult *chosen = &context->place_search_results[(*context->place_search_selected)];
                                if (openride_app_storage_add_favorite(context->app_storage, chosen->name, chosen->lat, chosen->lon, (int)chosen->kind, context->error, context->error_size)) {
                                    openride_app_search_refresh_stored_places(context->app_storage, true, context->favorite_places, &(*context->favorite_count));
                                    snprintf(context->route_status, context->route_status_size, "favori ajoute: %.120s", chosen->name);
                                }
                            } else if ((event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)
                                       && (*context->place_search_result_count) > 0U) {
                                const OpenRidePlaceSearchResult *chosen =
                                    &context->place_search_results[(*context->place_search_selected)];
                                (*context->camera).center_lat = chosen->lat;
                                (*context->camera).center_lon = chosen->lon;
                                if ((*context->camera).zoom < 14.0) (*context->camera).zoom = 14.0;

                                if ((*context->place_search_purpose)
                                    == OPENRIDE_PLACE_SEARCH_ROUTE_START) {
                                    openride_map_selection_set(&(*context->selection),
                                                               OPENRIDE_MARKER_START,
                                                               chosen->lat,
                                                               chosen->lon);
                                    openride_map_selection_set_region_hint(
                                        &(*context->selection),
                                        OPENRIDE_MARKER_START,
                                        chosen->region_id);
                                    (*context->start_snap).segment_id =
                                        OPENRIDE_ROUTING_SEGMENT_NONE;
                                    openride_route_destroy(&(*context->route));
                                    (*context->route_valid) = false;
                                    (*context->route_dirty) = false;
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "depart: %.120s",
                                             chosen->name);
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_ROUTE;
                                } else if ((*context->place_search_purpose)
                                           == OPENRIDE_PLACE_SEARCH_ROUTE_DESTINATION) {
                                    openride_map_selection_set(&(*context->selection),
                                                               OPENRIDE_MARKER_DESTINATION,
                                                               chosen->lat,
                                                               chosen->lon);
                                    openride_map_selection_set_region_hint(
                                        &(*context->selection),
                                        OPENRIDE_MARKER_DESTINATION,
                                        chosen->region_id);
                                    (*context->destination_snap).segment_id =
                                        OPENRIDE_ROUTING_SEGMENT_NONE;
                                    openride_route_destroy(&(*context->route));
                                    (*context->route_valid) = false;
                                    (*context->route_dirty) = false;
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "arrivee: %.120s",
                                             chosen->name);
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_ROUTE;
                                } else {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "recherche: %.120s (%s)",
                                             chosen->name,
                                             openride_place_kind_name(chosen->kind));
                                }

                                if (context->app_storage) {
                                    openride_app_storage_add_history(context->app_storage,
                                                                     chosen->name,
                                                                     chosen->lat,
                                                                     chosen->lon,
                                                                     (int)chosen->kind,
                                                                     context->error,
                                                                     context->error_size);
                                    openride_app_search_refresh_stored_places(context->app_storage,
                                                          false,
                                                          context->history_places,
                                                          &(*context->history_count));
                                }
                                (*context->place_search_active) = false;
                                (*context->place_search_purpose) =
                                    OPENRIDE_PLACE_SEARCH_BROWSE;
                                SDL_StopTextInput(context->window);
                            }
                            break;
                        }

                        if (event.key.key == SDLK_TAB) {
                            (*context->app_panel) = OPENRIDE_APP_PANEL_MAIN;
                            (*context->app_panel_selected) = 0U;
                        } else if (event.key.key == SDLK_SLASH
                                   || (event.key.key == SDLK_COLON && (event.key.mod & SDL_KMOD_SHIFT) != 0)
                                   || (event.key.key == SDLK_SEMICOLON && (event.key.mod & SDL_KMOD_SHIFT) != 0)
                                   || (event.key.key == SDLK_F
                                       && (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0)) {
                            (*context->place_search_purpose) =
                                OPENRIDE_PLACE_SEARCH_BROWSE;
                            openride_app_search_open(context->window, context->place_world, &(*context->place_search_active),
                                              context->place_search_query, &(*context->place_search_result_count),
                                              &(*context->place_search_selected), context->route_status, context->route_status_size);
                        } else if (event.key.key == SDLK_V) {
                            if (context->app_storage) {
                                const double lat = (*context->selection).has_destination ? (*context->selection).destination.lat : (*context->camera).center_lat;
                                const double lon = (*context->selection).has_destination ? (*context->selection).destination.lon : (*context->camera).center_lon;
                                const char *name = (*context->selection).has_destination ? "Destination" : "Position carte";
                                if (openride_app_storage_add_favorite(context->app_storage, name, lat, lon, 0, context->error, context->error_size)) {
                                    openride_app_search_refresh_stored_places(context->app_storage, true, context->favorite_places, &(*context->favorite_count));
                                    snprintf(context->route_status, context->route_status_size, "favori ajoute");
                                }
                            }
                        } else if (event.key.key == SDLK_ESCAPE) {
                            (*context->running) = false;
                        } else if (event.key.key == SDLK_C) {
                            openride_map_selection_clear(&(*context->selection));
                            openride_app_route_clear_navigation_session(&(*context->navigation),
                                                     &(*context->gps_simulator),
                                                     &(*context->navigation_state),
                                                     &(*context->gps_sample),
                                                     &(*context->gps_sample_valid));
                            (*context->simulator_deviation) = false;
                            (*context->gpx_navigation_active) = false;
                            openride_navigation_session_reset(&(*context->navigation_session));
                            openride_location_filter_reset(&(*context->location_filter));
                            memset(&(*context->filtered_location), 0, sizeof((*context->filtered_location)));
                            (*context->gpx_recording_active) = false;
                            openride_gpx_document_clear(&(*context->gpx_recording));
                            (*context->gpx_last_recorded_position_m) = -1.0;
                            openride_route_destroy(&(*context->route));
                            (*context->route_valid) = false;
                            (*context->route_dirty) = false;
                            (*context->loop_active) = false;
                            memset(&(*context->loop_stats), 0, sizeof((*context->loop_stats)));
                            (*context->loop_waypoint_count) = 0U;
                            (*context->start_snap).segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                            (*context->destination_snap).segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                            snprintf(context->route_status,
                                     context->route_status_size,
                                     "%s",
                                     (*context->graph_loaded) ? "pret" : "graphe non installe");
                        } else if (event.key.key == SDLK_B) {
                            if ((*context->selection).has_destination) {
                                openride_map_selection_remove(&(*context->selection),
                                                              OPENRIDE_MARKER_DESTINATION);
                            }
                            (*context->destination_snap).segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                            (*context->route_dirty) = false;
                            openride_app_route_clear_navigation_session(&(*context->navigation),
                                                     &(*context->gps_simulator),
                                                     &(*context->navigation_state),
                                                     &(*context->gps_sample),
                                                     &(*context->gps_sample_valid));
                            (*context->simulator_deviation) = false;
                            (*context->gpx_navigation_active) = false;
                            openride_navigation_session_reset(&(*context->navigation_session));
                            openride_location_filter_reset(&(*context->location_filter));
                            memset(&(*context->filtered_location), 0, sizeof((*context->filtered_location)));
                            (*context->route_valid) = openride_app_route_generate_loop(&(*context->routing_graph),
                                                              (*context->graph_loaded),
                                                              &(*context->selection),
                                                              (*context->routing_profile),
                                                              (*context->loop_target_distance_m),
                                                              (*context->loop_direction),
                                                              (*context->loop_seed)++,
                                                              &(*context->route),
                                                              &(*context->loop_stats),
                                                              context->loop_waypoints,
                                                              &(*context->loop_waypoint_count),
                                                              &(*context->start_snap),
                                                              context->route_status,
                                                              context->route_status_size);
                            (*context->loop_active) = (*context->route_valid);
                            if ((*context->route_valid)) {
                                openride_app_route_prepare_navigation_session(&(*context->navigation),
                                                           &(*context->gps_simulator),
                                                           &(*context->navigation_instructions),
                                                           &(*context->routing_graph),
                                                           &(*context->route),
                                                           context->route_status,
                                                           context->route_status_size);
                            }
                        } else if (event.key.key == SDLK_S) {
    #ifdef __ANDROID__
                            if ((*context->simulated_gps_active)) {
                                openride_location_provider_stop(
                                    &(*context->simulated_location_provider));
                                (*context->simulated_gps_active) = false;
                                (*context->simulator_deviation) = false;
                                openride_gps_simulator_set_lateral_offset_m(
                                    &(*context->gps_simulator), 0.0);
                                openride_drive_mode_set_active(&(*context->drive_mode), false);
                                snprintf(context->route_status,
                                         context->route_status_size,
                                         "simulation GPS [DEV] arretee");
                            } else if ((*context->real_gps_active)) {
                                openride_location_provider_stop(&(*context->location_provider));
                                (*context->real_gps_active) = false;
                                snprintf(context->route_status, context->route_status_size, "GPS reel arrete");
                            } else if (openride_location_provider_start(&(*context->location_provider))) {
                                (*context->real_gps_active) = true;
                                snprintf(context->route_status, context->route_status_size, "GPS reel actif");
                            } else {
                                snprintf(context->route_status, context->route_status_size,
                                         "GPS indisponible: autorise la localisation puis reessaie");
                            }
    #else
                            if ((*context->route_valid) && (*context->gps_simulator).route) {
                                const bool active = openride_gps_simulator_toggle(&(*context->gps_simulator));
                                if (openride_gps_simulator_sample(&(*context->gps_simulator), &(*context->gps_sample))) {
                                    (*context->gps_sample_valid) = true;
                                    openride_navigation_engine_update(&(*context->navigation),
                                                                      (*context->gps_sample).lat,
                                                                      (*context->gps_sample).lon,
                                                                      (*context->gps_sample).speed_mps,
                                                                      (*context->gps_sample).heading_deg,
                                                                      &(*context->navigation_state));
                                }
                                snprintf(context->route_status,
                                         context->route_status_size,
                                         "simulation GPS %s",
                                         active ? "en cours" : "en pause");
                            } else {
                                snprintf(context->route_status,
                                         context->route_status_size,
                                         "calcule un itineraire avant de lancer le GPS");
                            }
    #endif
                        } else if (event.key.key == SDLK_X) {
    #ifdef __ANDROID__
                            snprintf(context->route_status, context->route_status_size,
                                     "test deviation X disponible sur macOS");
    #else
                            (*context->simulator_deviation) = !(*context->simulator_deviation);
                            openride_gps_simulator_set_lateral_offset_m(
                                &(*context->gps_simulator),
                                (*context->simulator_deviation) ? 80.0 : 0.0);
                            snprintf(context->route_status,
                                     context->route_status_size,
                                     "deviation GPS test: %s",
                                     (*context->simulator_deviation) ? "80 m" : "desactivee");
    #endif
                        } else if (event.key.key == SDLK_F) {
                            (*context->follow_gps) = !(*context->follow_gps);
                            if (context->app_storage) openride_app_storage_set_int(context->app_storage, "follow_gps", (*context->follow_gps) ? 1 : 0, context->error, context->error_size);
                            snprintf(context->route_status,
                                     context->route_status_size,
                                     "suivi camera GPS: %s",
                                     (*context->follow_gps) ? "actif" : "inactif");
                        } else if (event.key.key == SDLK_A) {
                            (*context->auto_reroute) = !(*context->auto_reroute);
                            openride_navigation_session_set_auto_reroute(&(*context->navigation_session), (*context->auto_reroute));
                            if (context->app_storage) openride_app_storage_set_int(context->app_storage, "auto_reroute", (*context->auto_reroute) ? 1 : 0, context->error, context->error_size);
                            snprintf(context->route_status,
                                     context->route_status_size,
                                     "recalcul automatique: %s",
                                     (*context->auto_reroute) ? "actif" : "inactif");
                        } else if (event.key.key == SDLK_R) {
                            if ((*context->gps_sample_valid) && (*context->selection).has_destination && !(*context->loop_active)
                                && !(*context->gpx_navigation_active)) {
                                const bool resume_simulator = (*context->gps_simulator).active;
                                const double reroute_lat = (*context->filtered_location).valid
                                    ? (*context->filtered_location).lat : (*context->gps_sample).lat;
                                const double reroute_lon = (*context->filtered_location).valid
                                    ? (*context->filtered_location).lon : (*context->gps_sample).lon;
                                (*context->route_valid) = openride_app_route_reroute_from_position(
                                    &(*context->routing_graph),
                                    (*context->graph_loaded),
                                    &(*context->selection),
                                    (*context->routing_profile),
                                    reroute_lat,
                                    reroute_lon,
                                    &(*context->route),
                                    &(*context->start_snap),
                                    &(*context->destination_snap),
                                    &(*context->navigation),
                                    &(*context->gps_simulator),
                                    &(*context->navigation_instructions),
                                    &(*context->navigation_session),
                                    &(*context->location_filter),
                                    resume_simulator,
                                    context->route_status,
                                    context->route_status_size);
                                (*context->simulator_deviation) = false;
                                memset(&(*context->navigation_state), 0, sizeof((*context->navigation_state)));
                                memset(&(*context->filtered_location), 0, sizeof((*context->filtered_location)));
                                if ((*context->route_valid)) {
                                    snprintf(context->route_status, context->route_status_size, "itineraire recalcule depuis GPS");
                                } else if (openride_map_selection_complete(&(*context->selection))) {
                                    (*context->routing_world_pending_reroute) = true;
                                    (*context->routing_world_pending_resume_simulator) = resume_simulator;
                                    (*context->route_dirty) = true;
                                }
                            } else {
                                snprintf(context->route_status,
                                         context->route_status_size,
                                         "R necessite une navigation routiere active");
                            }
                        } else if (event.key.key == SDLK_O) {
                            (*context->loop_direction) = openride_loop_direction_next((*context->loop_direction));
                            if ((*context->loop_active)) {
                                openride_app_route_clear_navigation_session(&(*context->navigation),
                                                         &(*context->gps_simulator),
                                                         &(*context->navigation_state),
                                                         &(*context->gps_sample),
                                                         &(*context->gps_sample_valid));
                                openride_route_destroy(&(*context->route));
                                (*context->route_valid) = false;
                                (*context->loop_active) = false;
                                (*context->loop_waypoint_count) = 0U;
                            }
                            snprintf(context->route_status,
                                     context->route_status_size,
                                     "direction boucle: %s | B pour generer",
                                     openride_loop_direction_name((*context->loop_direction)));
                        } else if (event.key.key == SDLK_PLUS
                                   || event.key.key == SDLK_KP_PLUS
                                   || event.key.key == SDLK_EQUALS) {
                            (*context->loop_target_distance_m) = openride_app_support_clampd(
                                (*context->loop_target_distance_m) + OPENRIDE_LOOP_DISTANCE_STEP_M,
                                OPENRIDE_LOOP_DISTANCE_MIN_M,
                                OPENRIDE_LOOP_DISTANCE_MAX_M);
                            if ((*context->loop_active)) {
                                openride_app_route_clear_navigation_session(&(*context->navigation),
                                                         &(*context->gps_simulator),
                                                         &(*context->navigation_state),
                                                         &(*context->gps_sample),
                                                         &(*context->gps_sample_valid));
                                openride_route_destroy(&(*context->route));
                                (*context->route_valid) = false;
                                (*context->loop_active) = false;
                                (*context->loop_waypoint_count) = 0U;
                            }
                            snprintf(context->route_status,
                                     context->route_status_size,
                                     "boucle cible %.0f km | B pour generer",
                                     (*context->loop_target_distance_m) / 1000.0);
                        } else if (event.key.key == SDLK_MINUS
                                   || event.key.key == SDLK_KP_MINUS) {
                            (*context->loop_target_distance_m) = openride_app_support_clampd(
                                (*context->loop_target_distance_m) - OPENRIDE_LOOP_DISTANCE_STEP_M,
                                OPENRIDE_LOOP_DISTANCE_MIN_M,
                                OPENRIDE_LOOP_DISTANCE_MAX_M);
                            if ((*context->loop_active)) {
                                openride_app_route_clear_navigation_session(&(*context->navigation),
                                                         &(*context->gps_simulator),
                                                         &(*context->navigation_state),
                                                         &(*context->gps_sample),
                                                         &(*context->gps_sample_valid));
                                openride_route_destroy(&(*context->route));
                                (*context->route_valid) = false;
                                (*context->loop_active) = false;
                                (*context->loop_waypoint_count) = 0U;
                            }
                            snprintf(context->route_status,
                                     context->route_status_size,
                                     "boucle cible %.0f km | B pour generer",
                                     (*context->loop_target_distance_m) / 1000.0);
                        } else if (event.key.key == SDLK_I) {
                            (*context->gpx_loaded) = openride_app_route_load_gpx_overlay(context->gpx_import_path,
                                                          &(*context->gpx_overlay),
                                                          context->route_status,
                                                          context->route_status_size);
                            if ((*context->gpx_loaded)) {
                                int gpx_width = 0;
                                int gpx_height = 0;
                                SDL_GetCurrentRenderOutputSize(context->renderer, &gpx_width, &gpx_height);
                                openride_app_route_fit_camera_to_gpx(&(*context->camera),
                                                  &(*context->gpx_overlay),
                                                  gpx_width,
                                                  gpx_height,
                                                  (double)(*context->metadata)->min_zoom,
                                                  (*context->scalable_map) ? 18.0 : (double)(*context->metadata)->max_zoom);
                            }
                        } else if (event.key.key == SDLK_N) {
                            if (!(*context->gpx_loaded)) {
                                snprintf(context->route_status,
                                         context->route_status_size,
                                         "importe d'abord une trace GPX avec I");
                            } else {
                                memset(&(*context->navigation_state), 0, sizeof((*context->navigation_state)));
                                memset(&(*context->gps_sample), 0, sizeof((*context->gps_sample)));
                                (*context->gps_sample_valid) = false;
                                (*context->simulator_deviation) = false;
                                (*context->loop_active) = false;
                                (*context->loop_waypoint_count) = 0U;
                                (*context->route_dirty) = false;
                                (*context->start_snap).segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                                (*context->destination_snap).segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                                (*context->route_valid) = openride_app_route_prepare_gpx_navigation(&(*context->gpx_overlay),
                                                                     &(*context->routing_graph),
                                                                     &(*context->selection),
                                                                     &(*context->route),
                                                                     &(*context->navigation),
                                                                     &(*context->gps_simulator),
                                                                     &(*context->navigation_instructions),
                                                                     &(*context->navigation_session),
                                                                     &(*context->location_filter),
                                                                     context->route_status,
                                                                     context->route_status_size);
                                (*context->gpx_navigation_active) = (*context->route_valid);
                            }
                        } else if (event.key.key == SDLK_E) {
                            if (!(*context->route_valid)) {
                                snprintf(context->route_status,
                                         context->route_status_size,
                                         "aucun itineraire a exporter en GPX");
                            } else {
                                char gpx_error[192] = {0};
                                if (openride_gpx_save_route(context->gpx_route_export_path,
                                                            &(*context->route),
                                                            (*context->loop_active) ? "OpenRide boucle" : "OpenRide itineraire",
                                                            gpx_error,
                                                            sizeof(gpx_error))) {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "GPX exporte: %s",
                                             context->gpx_route_export_path);
                                } else {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "export GPX impossible: %.145s",
                                             gpx_error[0] ? gpx_error : "erreur inconnue");
                                }
                            }
                        } else if (event.key.key == SDLK_G) {
                            if (!(*context->gpx_recording_active)) {
                                if (!(*context->route_valid) || !(*context->gps_simulator).route) {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "G necessite un itineraire avec GPS simule");
                                } else {
                                    openride_gpx_document_clear(&(*context->gpx_recording));
                                    snprintf((*context->gpx_recording).name,
                                             sizeof((*context->gpx_recording).name),
                                             "OpenRide GPS recording");
                                    (*context->gpx_recording_active) = true;
                                    (*context->gpx_last_recorded_position_m) = -1.0;
                                    if ((*context->gps_sample_valid)) {
                                        openride_app_route_record_gps_sample(&(*context->gpx_recording),
                                                          &(*context->gps_sample),
                                                          &(*context->gpx_last_recorded_position_m));
                                    }
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "enregistrement GPX demarre");
                                }
                            } else {
                                (*context->gpx_recording_active) = false;
                                if ((*context->gpx_recording).track_points.count >= 2U) {
                                    char gpx_error[192] = {0};
                                    if (openride_gpx_save_document(context->gpx_recording_export_path,
                                                                   &(*context->gpx_recording),
                                                                   "OpenRide",
                                                                   gpx_error,
                                                                   sizeof(gpx_error))) {
                                        snprintf(context->route_status,
                                                 context->route_status_size,
                                                 "trace GPX enregistree: %s",
                                                 context->gpx_recording_export_path);
                                    } else {
                                        snprintf(context->route_status,
                                                 context->route_status_size,
                                                 "trace GPX non ecrite: %.140s",
                                                 gpx_error[0] ? gpx_error : "erreur inconnue");
                                    }
                                } else {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "trace GPX trop courte pour etre enregistree");
                                }
                            }
                        } else if (event.key.key == SDLK_M && (*context->scalable_map)) {
                            (*context->map_style) = openride_map_style_next((*context->map_style));
                            if ((*context->ormap_map)) openride_ormap_renderer_set_style(&(*context->ormap_renderer), (*context->map_style));
                            else if ((*context->vector_map)) openride_vector_map_renderer_set_style(&(*context->vector_renderer), (*context->map_style));
                            if (context->app_storage) openride_app_storage_set_int(context->app_storage, "map_style", (int)(*context->map_style), context->error, context->error_size);
                        } else if (event.key.key == SDLK_1
                                   || event.key.key == SDLK_2
                                   || event.key.key == SDLK_3) {
                            if (event.key.key == SDLK_1) {
                                (*context->routing_profile) = OPENRIDE_ROUTING_PROFILE_FASTEST;
                            } else if (event.key.key == SDLK_2) {
                                (*context->routing_profile) = OPENRIDE_ROUTING_PROFILE_TOURING;
                            } else {
                                (*context->routing_profile) = OPENRIDE_ROUTING_PROFILE_TRAIL;
                            }
                            if (context->app_storage) openride_app_storage_set_int(context->app_storage, "routing_profile", (int)(*context->routing_profile), context->error, context->error_size);
                            if ((*context->gpx_navigation_active)) {
                                snprintf(context->route_status,
                                         context->route_status_size,
                                         "profil %s enregistre | la trace GPX reste active",
                                         openride_routing_profile_name((*context->routing_profile)));
                            } else if ((*context->loop_active)) {
                                openride_app_route_clear_navigation_session(&(*context->navigation),
                                                         &(*context->gps_simulator),
                                                         &(*context->navigation_state),
                                                         &(*context->gps_sample),
                                                         &(*context->gps_sample_valid));
                                openride_navigation_session_reset(&(*context->navigation_session));
                                openride_location_filter_reset(&(*context->location_filter));
                                openride_route_destroy(&(*context->route));
                                (*context->route_valid) = false;
                                (*context->loop_active) = false;
                                (*context->loop_waypoint_count) = 0U;
                                snprintf(context->route_status,
                                         context->route_status_size,
                                         "profil %s | B pour regenerer la boucle",
                                         openride_routing_profile_name((*context->routing_profile)));
                            } else {
                                (*context->route_dirty) = openride_map_selection_complete(&(*context->selection));
                            }
                        }
                        break;

                    case SDL_EVENT_TEXT_INPUT:
                        if ((*context->place_search_active) && context->place_world) {
                            const size_t current = strlen(context->place_search_query);
                            const size_t incoming = strlen(event.text.text);
                            if (current + incoming < context->place_search_query_size) {
                                memcpy(context->place_search_query + current,
                                       event.text.text,
                                       incoming + 1U);
                                openride_app_search_refresh(context->place_world,
                                                     context->place_search_query,
                                                     context->place_search_results,
                                                     &(*context->place_search_result_count),
                                                     &(*context->place_search_selected),
                                                     context->route_status,
                                                     context->route_status_size);
                            }
                        }
                        break;

                    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                        if (event.button.which == SDL_TOUCH_MOUSEID) break;
                        int width = 0;
                        int height = 0;
                        SDL_GetCurrentRenderOutputSize(context->renderer, &width, &height);
                        if ((*context->place_search_active) || (*context->app_panel) != OPENRIDE_APP_PANEL_NONE) break;

                        if (event.button.button == SDL_BUTTON_LEFT) {
                            const OpenRideToolbarAction toolbar_action = openride_app_ui_toolbar_hit_test(
                                context->renderer,
                                (double)event.button.x,
                                (double)event.button.y,
                                width,
                                height);
                            if (toolbar_action != OPENRIDE_TOOLBAR_NONE) {
                                (*context->pending_toolbar_action) = toolbar_action;
                                break;
                            }
                            (*context->mouse_down_x) = (double)event.button.x;
                            (*context->mouse_down_y) = (double)event.button.y;
                            (*context->map_drag_moved) = false;
                            (*context->dragging_marker) = openride_app_render_marker_at_screen(&(*context->camera),
                                                               &(*context->selection),
                                                               (*context->mouse_down_x),
                                                               (*context->mouse_down_y),
                                                               width,
                                                               height);
                            (*context->dragging_map) = (*context->dragging_marker) == OPENRIDE_MARKER_NONE;
                            if ((*context->dragging_marker) != OPENRIDE_MARKER_NONE) {
                                openride_app_route_clear_navigation_session(&(*context->navigation),
                                                         &(*context->gps_simulator),
                                                         &(*context->navigation_state),
                                                         &(*context->gps_sample),
                                                         &(*context->gps_sample_valid));
                                openride_route_destroy(&(*context->route));
                                (*context->route_valid) = false;
                                (*context->loop_active) = false;
                                (*context->gpx_navigation_active) = false;
                                openride_navigation_session_reset(&(*context->navigation_session));
                                openride_location_filter_reset(&(*context->location_filter));
                                (*context->loop_waypoint_count) = 0U;
                            }
                        } else if (event.button.button == SDL_BUTTON_RIGHT) {
                            const OpenRideSelectionMarker marker = openride_app_render_marker_at_screen(
                                &(*context->camera),
                                &(*context->selection),
                                (double)event.button.x,
                                (double)event.button.y,
                                width,
                                height);
                            if (marker != OPENRIDE_MARKER_NONE) {
                                openride_map_selection_remove(&(*context->selection), marker);
                                openride_app_route_clear_navigation_session(&(*context->navigation),
                                                         &(*context->gps_simulator),
                                                         &(*context->navigation_state),
                                                         &(*context->gps_sample),
                                                         &(*context->gps_sample_valid));
                                openride_route_destroy(&(*context->route));
                                (*context->route_valid) = false;
                                (*context->loop_active) = false;
                                (*context->gpx_navigation_active) = false;
                                openride_navigation_session_reset(&(*context->navigation_session));
                                openride_location_filter_reset(&(*context->location_filter));
                                (*context->loop_waypoint_count) = 0U;
                                (*context->route_dirty) = openride_map_selection_complete(&(*context->selection));
                                if (!(*context->route_dirty)) {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "choisis un depart et une destination");
                                }
                            }
                        }
                        break;
                    }

                    case SDL_EVENT_MOUSE_BUTTON_UP:
                        if (event.button.which == SDL_TOUCH_MOUSEID) break;
                        if ((*context->place_search_active) || (*context->app_panel) != OPENRIDE_APP_PANEL_NONE) break;
                        if (event.button.button == SDL_BUTTON_LEFT) {
                            if ((*context->dragging_marker) != OPENRIDE_MARKER_NONE) {
                                (*context->dragging_marker) = OPENRIDE_MARKER_NONE;
                                (*context->loop_active) = false;
                                (*context->loop_waypoint_count) = 0U;
                                (*context->route_dirty) = openride_map_selection_complete(&(*context->selection));
                            } else if ((*context->dragging_map) && !(*context->map_drag_moved)) {
                                int width = 0;
                                int height = 0;
                                SDL_GetCurrentRenderOutputSize(context->renderer, &width, &height);
                                openride_app_route_add_selection_from_screen(&(*context->selection),
                                                          &(*context->camera),
                                                          (double)event.button.x,
                                                          (double)event.button.y,
                                                          width,
                                                          height,
                                                          &(*context->route_dirty),
                                                          &(*context->loop_active),
                                                          &(*context->loop_waypoint_count),
                                                          context->route_status,
                                                          context->route_status_size);
                            }
                            (*context->dragging_map) = false;
                            (*context->map_drag_moved) = false;
                        }
                        break;

                    case SDL_EVENT_MOUSE_MOTION:
                        if (event.motion.which == SDL_TOUCH_MOUSEID) break;
                        if ((*context->place_search_active) || (*context->app_panel) != OPENRIDE_APP_PANEL_NONE) break;
                        if ((*context->dragging_marker) != OPENRIDE_MARKER_NONE) {
                            int width = 0;
                            int height = 0;
                            double lat = 0.0;
                            double lon = 0.0;
                            SDL_GetCurrentRenderOutputSize(context->renderer, &width, &height);
                            openride_screen_to_geo(&(*context->camera),
                                                   (double)event.motion.x,
                                                   (double)event.motion.y,
                                                   width,
                                                   height,
                                                   &lat,
                                                   &lon);
                            openride_map_selection_set(&(*context->selection),
                                                       (*context->dragging_marker),
                                                       lat,
                                                       lon);
                        } else if ((*context->dragging_map)) {
                            const double dx = (double)event.motion.x - (*context->mouse_down_x);
                            const double dy = (double)event.motion.y - (*context->mouse_down_y);
                            const double movement = sqrt(dx * dx + dy * dy);

                            if (movement >= OPENRIDE_CLICK_DRAG_THRESHOLD) {
                                (*context->map_drag_moved) = true;
                            }

                            if ((*context->map_drag_moved)) {
                                openride_camera_pan(&(*context->camera),
                                                    (double)event.motion.xrel,
                                                    (double)event.motion.yrel);
                            }
                        }
                        break;

                    case SDL_EVENT_MOUSE_WHEEL: {
                        if (event.wheel.which == SDL_TOUCH_MOUSEID) break;
                        if ((*context->place_search_active) || (*context->app_panel) != OPENRIDE_APP_PANEL_NONE) break;
                        int width = 0;
                        int height = 0;
                        SDL_GetCurrentRenderOutputSize(context->renderer, &width, &height);

                        const double requested_delta = (double)event.wheel.y * 0.5;
                        const double max_zoom = (*context->scalable_map) ? 18.0 : (double)(*context->metadata)->max_zoom;
                        const double min_zoom = context->map_world && (*context->scalable_map)
                            ? OPENRIDE_MAP_WORLD_MIN_ZOOM
                            : (double)(*context->metadata)->min_zoom;
                        const double target_zoom = openride_app_support_clampd(
                            (*context->camera).zoom + requested_delta,
                            min_zoom,
                            max_zoom);

                        openride_camera_zoom_at(&(*context->camera),
                                                target_zoom - (*context->camera).zoom,
                                                (double)event.wheel.mouse_x,
                                                (double)event.wheel.mouse_y,
                                                width,
                                                height);
                        break;
                    }

                    case SDL_EVENT_FINGER_DOWN: {
                        int width = 0;
                        int height = 0;
                        SDL_GetCurrentRenderOutputSize(context->renderer, &width, &height);
                        const double x = (double)event.tfinger.x;
                        const double y = (double)event.tfinger.y;

    #ifdef __ANDROID__
                        if (!(*context->place_search_active) && (*context->app_panel) != OPENRIDE_APP_PANEL_NONE) {
                            if ((*context->planner_busy) != OPENRIDE_RIDE_PLANNER_IDLE
                                && ((*context->app_panel) == OPENRIDE_APP_PANEL_ROUTE
                                    || (*context->app_panel) == OPENRIDE_APP_PANEL_LOOP_PROPOSALS)) {
                                openride_touch_input_cancel(&(*context->touch_input));
                                break;
                            }
                            const uint32_t mobile_place_count =
                                (*context->app_panel) == OPENRIDE_APP_PANEL_FAVORITES ? (*context->favorite_count)
                                : (*context->app_panel) == OPENRIDE_APP_PANEL_HISTORY ? (*context->history_count)
                                : 0U;
                            const OpenRideAppUIAction mobile_hit =
                                (*context->app_panel) == OPENRIDE_APP_PANEL_MAIN
                                    ? openride_app_ui_main_menu_hit_test(context->renderer,
                                                                x,
                                                                y,
                                                                width,
                                                                height)
                                : (*context->app_panel) == OPENRIDE_APP_PANEL_ROUTE
                                    ? openride_app_ui_route_panel_hit_test(context->renderer,
                                                                  (int)(*context->planner_mode),
                                                                  x,
                                                                  y,
                                                                  width,
                                                                  height)
                                : (*context->app_panel) == OPENRIDE_APP_PANEL_LOOP_PROPOSALS
                                    ? openride_app_ui_loop_proposals_hit_test(context->renderer,
                                                                      (*context->loop_proposals).count,
                                                                      x, y, width, height)
                                : (*context->app_panel) == OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS
                                    ? openride_app_ui_route_downloads_panel_hit_test(context->renderer,
                                                                            x,
                                                                            y,
                                                                            width,
                                                                            height)
                                : (*context->app_panel) == OPENRIDE_APP_PANEL_SETTINGS
                                    ? openride_app_ui_settings_panel_hit_test(context->renderer,
                                                                     x,
                                                                     y,
                                                                     width,
                                                                     height)
                                : (*context->app_panel) == OPENRIDE_APP_PANEL_REGIONS
                                    ? openride_app_ui_regions_panel_hit_test(context->renderer,
                                                                    x,
                                                                    y,
                                                                    width,
                                                                    height)
                                : (*context->app_panel) == OPENRIDE_APP_PANEL_FAVORITES
                                  || (*context->app_panel) == OPENRIDE_APP_PANEL_HISTORY
                                    ? openride_app_ui_places_panel_hit_test(context->renderer,
                                                                   x,
                                                                   y,
                                                                   width,
                                                                   height,
                                                                   mobile_place_count)
                                    : (OpenRideAppUIAction){
                                          OPENRIDE_APP_UI_NONE,
                                          -1
                                      };

                            if (mobile_hit.action == OPENRIDE_APP_UI_CLOSE) {
                                (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                (*context->app_panel_selected) = 0U;
                            } else if (mobile_hit.action == OPENRIDE_APP_UI_BACK) {
                                if ((*context->app_panel) == OPENRIDE_APP_PANEL_LOOP_PROPOSALS) {
                                    openride_route_choice_reset(context->route_choice);
                                }
                                (*context->app_panel) =
                                    ((*context->app_panel) == OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS
                                     || (*context->app_panel) == OPENRIDE_APP_PANEL_LOOP_PROPOSALS)
                                        ? OPENRIDE_APP_PANEL_ROUTE
                                        : OPENRIDE_APP_PANEL_MAIN;
                                (*context->app_panel_selected) = 0U;
                            } else if (mobile_hit.action == OPENRIDE_APP_UI_SEARCH) {
                                (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                (*context->place_search_purpose) =
                                    OPENRIDE_PLACE_SEARCH_BROWSE;
                                openride_app_search_open(context->window,
                                                  context->place_world,
                                                  &(*context->place_search_active),
                                                  context->place_search_query,
                                                  &(*context->place_search_result_count),
                                                  &(*context->place_search_selected),
                                                  context->route_status,
                                                  context->route_status_size);
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_ROUTE_MODE_ROUTE) {
                                (*context->planner_mode) = OPENRIDE_RIDE_PLANNER_ROUTE;
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_ROUTE_MODE_LOOP) {
                                (*context->planner_mode) = OPENRIDE_RIDE_PLANNER_LOOP;
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_ROUTE_PROFILE_FASTEST
                                       || mobile_hit.action == OPENRIDE_APP_UI_ROUTE_PROFILE_TOURING
                                       || mobile_hit.action == OPENRIDE_APP_UI_ROUTE_PROFILE_TRAIL) {
                                (*context->routing_profile) =
                                    mobile_hit.action == OPENRIDE_APP_UI_ROUTE_PROFILE_FASTEST
                                        ? OPENRIDE_ROUTING_PROFILE_FASTEST
                                        : mobile_hit.action == OPENRIDE_APP_UI_ROUTE_PROFILE_TRAIL
                                            ? OPENRIDE_ROUTING_PROFILE_TRAIL
                                            : OPENRIDE_ROUTING_PROFILE_TOURING;
                                openride_loop_proposal_set_destroy(context->loop_proposals);
                                openride_route_choice_reset(context->route_choice);
                                if (context->app_storage) {
                                    openride_app_storage_set_int(context->app_storage,
                                                                 "routing_profile",
                                                                 (int)(*context->routing_profile),
                                                                 context->error,
                                                                 context->error_size);
                                }
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_ROUTE_LOOP_DISTANCE_DOWN
                                       || mobile_hit.action == OPENRIDE_APP_UI_ROUTE_LOOP_DISTANCE_UP) {
                                const double delta = mobile_hit.action
                                        == OPENRIDE_APP_UI_ROUTE_LOOP_DISTANCE_UP
                                    ? OPENRIDE_LOOP_DISTANCE_STEP_M
                                    : -OPENRIDE_LOOP_DISTANCE_STEP_M;
                                (*context->loop_target_distance_m) = openride_app_support_clampd(
                                    (*context->loop_target_distance_m) + delta,
                                    OPENRIDE_LOOP_DISTANCE_MIN_M,
                                    OPENRIDE_LOOP_DISTANCE_MAX_M);
                                openride_loop_proposal_set_destroy(context->loop_proposals);
                                openride_route_choice_reset(context->route_choice);
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_ROUTE_LOOP_DIRECTION) {
                                (*context->loop_direction) =
                                    openride_loop_direction_next((*context->loop_direction));
                                openride_loop_proposal_set_destroy(context->loop_proposals);
                                openride_route_choice_reset(context->route_choice);
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_ROUTE_GPS_START) {
                                (*context->route_map_pick_marker) = OPENRIDE_MARKER_NONE;
    #ifdef __ANDROID__
                                if ((*context->gps_sample_valid)) {
                                    openride_map_selection_set(&(*context->selection),
                                                               OPENRIDE_MARKER_START,
                                                               (*context->gps_sample).lat,
                                                               (*context->gps_sample).lon);
                                    (*context->start_snap).segment_id =
                                        OPENRIDE_ROUTING_SEGMENT_NONE;
                                    openride_route_destroy(&(*context->route));
                                    (*context->route_valid) = false;
                                    (*context->route_dirty) = false;
                                    (*context->route_start_gps_pending) = false;
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "depart: position GPS actuelle");
                                } else {
                                    (*context->real_gps_requested) = true;
                                    (*context->route_start_gps_pending) = true;
                                    if (!(*context->real_gps_active)) {
                                        (*context->real_gps_active) =
                                            openride_location_provider_start(
                                                &(*context->location_provider));
                                        (*context->android_gps_sample_age_s) = INFINITY;
                                    }
                                    if ((*context->real_gps_active)) {
                                        snprintf(context->route_status,
                                                 context->route_status_size,
                                                 "recherche de la position GPS...");
                                    } else {
                                        (*context->route_start_gps_pending) = false;
                                        snprintf(context->route_status,
                                                 context->route_status_size,
                                                 "GPS indisponible: autorise la localisation");
                                    }
                                }
    #endif
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_ROUTE_SEARCH_START) {
                                (*context->route_map_pick_marker) = OPENRIDE_MARKER_NONE;
                                (*context->place_search_purpose) =
                                    OPENRIDE_PLACE_SEARCH_ROUTE_START;
                                (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                openride_app_search_open(context->window,
                                                  context->place_world,
                                                  &(*context->place_search_active),
                                                  context->place_search_query,
                                                  &(*context->place_search_result_count),
                                                  &(*context->place_search_selected),
                                                  context->route_status,
                                                  context->route_status_size);
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_ROUTE_MAP_START) {
                                (*context->route_map_pick_marker) = OPENRIDE_MARKER_START;
                                (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                snprintf(context->route_status,
                                         context->route_status_size,
                                         "Touchez la carte pour choisir le depart");
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_ROUTE_SEARCH_DESTINATION) {
                                (*context->route_map_pick_marker) = OPENRIDE_MARKER_NONE;
                                (*context->place_search_purpose) =
                                    OPENRIDE_PLACE_SEARCH_ROUTE_DESTINATION;
                                (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                openride_app_search_open(context->window,
                                                  context->place_world,
                                                  &(*context->place_search_active),
                                                  context->place_search_query,
                                                  &(*context->place_search_result_count),
                                                  &(*context->place_search_selected),
                                                  context->route_status,
                                                  context->route_status_size);
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_ROUTE_MAP_DESTINATION) {
                                (*context->route_map_pick_marker) =
                                    OPENRIDE_MARKER_DESTINATION;
                                (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                snprintf(context->route_status,
                                         context->route_status_size,
                                         "Touchez la carte pour choisir l'arrivee");
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_ROUTE_CALCULATE) {
                                if ((*context->planner_busy) != OPENRIDE_RIDE_PLANNER_IDLE
                                    || (*context->planner_async_thread)) {
                                    /* A planner job is already running. */
                                } else if ((*context->planner_mode) == OPENRIDE_RIDE_PLANNER_LOOP) {
                                    if (!(*context->selection).has_start) {
                                        snprintf(context->route_status,
                                                 context->route_status_size,
                                                 "choisis un depart pour la balade");
                                    } else {
                                        if ((*context->selection).has_destination) {
                                            openride_map_selection_remove(&(*context->selection),
                                                                          OPENRIDE_MARKER_DESTINATION);
                                        }
                                        (*context->destination_snap).segment_id =
                                            OPENRIDE_ROUTING_SEGMENT_NONE;
                                        (*context->planner_async_thread) =
                                            openride_app_planner_async_start_loops(
                                                context->planner_async_context,
                                                &(*context->platform_paths),
                                                (*context->active_region),
                                                &(*context->routing_graph),
                                                (*context->graph_loaded),
                                                &(*context->selection),
                                                (*context->routing_profile),
                                                (*context->loop_target_distance_m),
                                                (*context->loop_direction),
                                                (*context->loop_seed)++);
                                        if ((*context->planner_async_thread)) {
                                            (*context->planner_busy) =
                                                OPENRIDE_RIDE_PLANNER_GENERATING_LOOPS;
                                            (*context->route_dirty) = false;
                                            snprintf(context->route_status,
                                                     context->route_status_size,
                                                     "Recherche de balades en cours...");
                                        } else {
                                            snprintf(context->route_status,
                                                     context->route_status_size,
                                                     "Impossible de lancer la recherche de balades");
                                        }
                                    }
                                } else if (openride_map_selection_complete(&(*context->selection))) {
                                    openride_loop_proposal_set_destroy(context->loop_proposals);
                                    openride_route_choice_reset(context->route_choice);
                                    (*context->loop_active) = false;
                                    (*context->route_dirty) = false;
                                    (*context->planner_async_thread) =
                                        openride_app_planner_async_start_route(
                                            context->planner_async_context,
                                            &(*context->routing_graph),
                                            (*context->graph_loaded),
                                            &(*context->selection),
                                            (*context->routing_profile));
                                    if ((*context->planner_async_thread)) {
                                        (*context->planner_busy) =
                                            OPENRIDE_RIDE_PLANNER_CALCULATING_ROUTE;
                                        snprintf(context->route_status,
                                                 context->route_status_size,
                                                 "Calcul de l'itineraire en cours...");
                                    } else {
                                        snprintf(context->route_status,
                                                 context->route_status_size,
                                                 "Impossible de lancer le calcul de l'itineraire");
                                    }
                                } else {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "choisis un depart et une arrivee");
                                }
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_LOOP_PROPOSALS_REGENERATE) {
                                if ((*context->planner_busy) == OPENRIDE_RIDE_PLANNER_IDLE
                                    && !(*context->planner_async_thread)) {
                                    (*context->planner_async_thread) =
                                        openride_app_planner_async_start_loops(
                                            context->planner_async_context,
                                            &(*context->platform_paths),
                                            (*context->active_region),
                                            &(*context->routing_graph),
                                            (*context->graph_loaded),
                                            &(*context->selection),
                                            (*context->routing_profile),
                                            (*context->loop_target_distance_m),
                                            (*context->loop_direction),
                                            (*context->loop_seed)++);
                                    if ((*context->planner_async_thread)) {
                                        (*context->planner_busy) =
                                            OPENRIDE_RIDE_PLANNER_GENERATING_LOOPS;
                                        openride_route_choice_reset(context->route_choice);
                                        (*context->app_panel) = OPENRIDE_APP_PANEL_ROUTE;
                                        snprintf(context->route_status,
                                                 context->route_status_size,
                                                 "Recherche de nouvelles balades...");
                                    } else {
                                        snprintf(context->route_status,
                                                 context->route_status_size,
                                                 "Impossible de relancer la recherche de balades");
                                    }
                                }
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_LOOP_PROPOSAL_SELECT
                                       && mobile_hit.index >= 0) {
                                if (openride_route_choice_select_preview(
                                        context->route_choice,
                                        (uint32_t)mobile_hit.index)) {
                                    openride_app_events_fit_route_choice_preview(context);
                                    SDL_Log("RidePlanner: preview loop option %u/%u",
                                            (uint32_t)mobile_hit.index + 1U,
                                            context->route_choice->proposal_count);
                                }
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_LOOP_PROPOSAL_CONFIRM) {
                                uint32_t confirmed_index = 0U;
                                if (!openride_route_choice_confirm_preview(
                                        context->route_choice,
                                        &confirmed_index)) {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "Aucune balade selectionnee");
                                } else {
                                    openride_app_route_clear_navigation_session(
                                        &(*context->navigation),
                                        &(*context->gps_simulator),
                                        &(*context->navigation_state),
                                        &(*context->gps_sample),
                                        &(*context->gps_sample_valid));
                                    openride_navigation_session_reset(&(*context->navigation_session));
                                    openride_location_filter_reset(&(*context->location_filter));
                                    (*context->route_valid) = openride_app_route_take_loop_proposal(
                                        context->loop_proposals,
                                        confirmed_index,
                                        &(*context->route),
                                        &(*context->loop_stats),
                                        context->loop_waypoints,
                                        &(*context->loop_waypoint_count),
                                        context->route_status,
                                        context->route_status_size);
                                    (*context->loop_active) = (*context->route_valid);
                                    if ((*context->route_valid)) {
                                        openride_route_choice_reset(context->route_choice);
                                        openride_app_route_prepare_navigation_session(
                                            &(*context->navigation),
                                            &(*context->gps_simulator),
                                            &(*context->navigation_instructions),
                                            &(*context->routing_graph),
                                            &(*context->route),
                                            context->route_status,
                                            context->route_status_size);
                                        (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                        SDL_Log("RidePlanner: confirmed loop option %u",
                                                confirmed_index + 1U);
                                    }
                                }
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_ROUTE_DOWNLOAD_REQUIRED) {
    #ifdef __ANDROID__
                                if ((*context->route_download_plan).available
                                    && !(*context->route_download_plan).downloading
                                    && (*context->route_download_plan).count > 0U
                                    && !(*context->region_busy)) {
                                    (*context->route_download_plan).downloading = true;
                                    (*context->route_download_plan).index = 0U;
                                    (*context->route_download_plan).selection = (*context->selection);
                                    (*context->route_download_plan).profile = (*context->routing_profile);

                                    const OpenRideRegionDefinition *required =
                                        openride_region_find(
                                            (*context->route_download_plan).region_ids[0]);
                                    if (!required) {
                                        (*context->route_download_plan).downloading = false;
                                        snprintf(context->route_status,
                                                 context->route_status_size,
                                                 "region requise inconnue");
                                    } else {
                                        (*context->region) = required;
                                        openride_region_get_status(
                                            &(*context->platform_paths),
                                            (*context->region),
                                            &(*context->region_status),
                                            context->error,
                                            context->error_size);
                                        openride_app_region_begin_android_install(
                                            &(*context->platform_paths),
                                            (*context->region),
                                            &(*context->region_status),
                                            &(*context->region_prepare_context),
                                            &(*context->region_prepare_thread),
                                            &(*context->region_download_started),
                                            &(*context->region_download_is_poly),
                                            &(*context->region_busy),
                                            &(*context->region_progress),
                                            context->region_work_status,
                                            context->region_work_status_size,
                                            context->error,
                                            context->error_size);
                                        if (!(*context->region_busy)
                                            && !(*context->region_download_started)
                                            && !(*context->region_prepare_thread)) {
                                            (*context->route_download_plan).downloading = false;
                                        }
                                    }
                                }
    #endif
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_ROUTE_USE_INSTALLED) {
                                if ((*context->route_download_plan).available
                                    && (*context->route_download_plan).has_installed_alternative
                                    && !(*context->route_download_plan).downloading
                                    && !(*context->region_busy)
                                    && !(*context->routing_world_thread)) {
                                    (*context->selection) = (*context->route_download_plan).selection;
                                    (*context->routing_profile) = (*context->route_download_plan).profile;
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;

                                    (*context->routing_world_thread) =
                                        openride_app_route_start_world_installed_alternative_thread(
                                            &(*context->routing_world_context),
                                            &(*context->platform_paths),
                                            (*context->active_region),
                                            (*context->graph_loaded) ? &(*context->routing_graph) : NULL,
                                            &(*context->routing_world_cache),
                                            &(*context->selection),
                                            (*context->routing_profile));

                                    if ((*context->routing_world_thread)) {
                                        snprintf(context->route_status,
                                                 context->route_status_size,
                                                 "Calcul avec les cartes installees...");
                                    } else {
                                        (*context->app_panel) =
                                            OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS;
                                        snprintf(context->route_status,
                                                 context->route_status_size,
                                                 "Impossible de lancer l'alternative");
                                    }
                                }
                            } else if (mobile_hit.action == OPENRIDE_APP_UI_FAVORITES) {
                                openride_app_search_refresh_stored_places(context->app_storage, true, context->favorite_places, &(*context->favorite_count));
                                (*context->app_panel) = OPENRIDE_APP_PANEL_FAVORITES;
                                (*context->app_panel_selected) = 0U;
                            } else if (mobile_hit.action == OPENRIDE_APP_UI_HISTORY) {
                                openride_app_search_refresh_stored_places(context->app_storage, false, context->history_places, &(*context->history_count));
                                (*context->app_panel) = OPENRIDE_APP_PANEL_HISTORY;
                                (*context->app_panel_selected) = 0U;
                            } else if (mobile_hit.action == OPENRIDE_APP_UI_REGIONS) {
                                openride_region_get_status(&(*context->platform_paths),
                                                           (*context->region),
                                                           &(*context->region_status),
                                                           context->error,
                                                           context->error_size);
                                (*context->app_panel) = OPENRIDE_APP_PANEL_REGIONS;
                                (*context->app_panel_selected) = 0U;
                            } else if (mobile_hit.action == OPENRIDE_APP_UI_SETTINGS) {
                                (*context->app_panel) = OPENRIDE_APP_PANEL_SETTINGS;
                                (*context->app_panel_selected) = 0U;
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_MAP_ZOOM_TEST) {
                                if ((*context->map_zoom_test).active) {
                                    openride_map_zoom_test_cancel(&(*context->map_zoom_test));
                                    snprintf(context->route_status, context->route_status_size,
                                             "test zoom carte annule");
                                } else {
                                    openride_map_zoom_test_start(&(*context->map_zoom_test), &(*context->camera), &(*context->platform_paths));
                                        (*map_zoom_loop_started_ns) = SDL_GetTicksNS();
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                    (*context->app_panel_selected) = 0U;
                                    snprintf(context->route_status, context->route_status_size,
                                             "test zoom 9.000 -> 17.000 -> 9.000 | log data/map-zoom-test.csv");
                                }
                            } else if (mobile_hit.action == OPENRIDE_APP_UI_PLACE
                                       && mobile_hit.index >= 0) {
                                const bool favorites_panel = (*context->app_panel) == OPENRIDE_APP_PANEL_FAVORITES;
                                const uint32_t count = favorites_panel ? (*context->favorite_count) : (*context->history_count);
                                OpenRideStoredPlace *items = favorites_panel ? context->favorite_places : context->history_places;
                                if ((uint32_t)mobile_hit.index < count) {
                                    const OpenRideStoredPlace *chosen = &items[mobile_hit.index];
                                    (*context->app_panel_selected) = (uint32_t)mobile_hit.index;
                                    (*context->camera).center_lat = chosen->lat;
                                    (*context->camera).center_lon = chosen->lon;
                                    if ((*context->camera).zoom < 14.0) (*context->camera).zoom = 14.0;
                                    openride_app_search_set_destination_from_place(&(*context->selection),
                                                               &(*context->gps_sample),
                                                               (*context->gps_sample_valid),
                                                               chosen->lat,
                                                               chosen->lon,
                                                               chosen->name,
                                                               &(*context->route_dirty),
                                                               context->route_status,
                                                               context->route_status_size);
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                }
                            } else if (mobile_hit.action == OPENRIDE_APP_UI_REGION_PREVIOUS
                                       || mobile_hit.action == OPENRIDE_APP_UI_REGION_NEXT) {
                                if (!(*context->region_busy)) {
                                    (*context->region) = openride_app_region_step((*context->region),
                                                         mobile_hit.action == OPENRIDE_APP_UI_REGION_PREVIOUS ? -1 : 1);
                                    openride_region_get_status(&(*context->platform_paths),
                                                               (*context->region),
                                                               &(*context->region_status),
                                                               context->error,
                                                               context->error_size);
                                    context->region_work_status[0] = '\0';
                                }
                            } else if (mobile_hit.action == OPENRIDE_APP_UI_REGION_INSTALL) {
                                if (!(*context->region_busy)) {
                                    if (openride_region_status_ready(&(*context->region_status))
    #ifdef __ANDROID__
                                        && (*context->region_status).poly_present
    #endif
                                    ) {
                                        if ((*context->region) == (*context->active_region)) {
                                            snprintf(context->region_work_status,
                                                     context->region_work_status_size,
                                                     "Cette region est deja active");
                                        } else {
                                            (*context->region_activation_requested) = true;
                                        }
                                    } else {
                                        openride_app_region_begin_android_install(&(*context->platform_paths),
                                                                     (*context->region),
                                                                     &(*context->region_status),
                                                                     &(*context->region_prepare_context),
                                                                     &(*context->region_prepare_thread),
                                                                     &(*context->region_download_started),
                                                                     &(*context->region_download_is_poly),
                                                                     &(*context->region_busy),
                                                                     &(*context->region_progress),
                                                                     context->region_work_status,
                                                                     context->region_work_status_size,
                                                                     context->error,
                                                                     context->error_size);
                                    }
                                }
                            } else if (mobile_hit.action == OPENRIDE_APP_UI_REGION_REMOVE) {
                                if (!(*context->region_busy)) {
                                    if ((*context->region) == (*context->active_region)) {
                                        snprintf(context->region_work_status,
                                                 context->region_work_status_size,
                                                 "Impossible de supprimer la region active");
                                    } else if (openride_region_remove_generated(&(*context->platform_paths),
                                                                                (*context->region),
                                                                                context->error,
                                                                                context->error_size)) {
                                        openride_region_get_status(&(*context->platform_paths),
                                                                   (*context->region),
                                                                   &(*context->region_status),
                                                                   context->error,
                                                                   context->error_size);
                                        openride_app_region_refresh_map_world_overview(context->map_world, &(*context->platform_paths));
                                        snprintf(context->region_work_status,
                                                 context->region_work_status_size,
                                                 "Donnees de la region supprimees");
                                    } else {
                                        snprintf(context->region_work_status,
                                                 context->region_work_status_size,
                                                 "Suppression impossible: %.120s",
                                                 context->error);
                                    }
                                }
                            } else if (mobile_hit.action == OPENRIDE_APP_UI_SETTINGS_STYLE) {
                                if ((*context->scalable_map)) {
                                    (*context->map_style) = openride_map_style_next((*context->map_style));
                                    if ((*context->ormap_map)) openride_ormap_renderer_set_style(&(*context->ormap_renderer), (*context->map_style));
                                    else if ((*context->vector_map)) openride_vector_map_renderer_set_style(&(*context->vector_renderer), (*context->map_style));
                                    if (context->app_storage) {
                                        openride_app_storage_set_int(context->app_storage,
                                                                     "map_style",
                                                                     (int)(*context->map_style),
                                                                     context->error,
                                                                     context->error_size);
                                    }
                                }
                            } else if (mobile_hit.action == OPENRIDE_APP_UI_SETTINGS_PROFILE) {
                                (*context->routing_profile) = (*context->routing_profile) == OPENRIDE_ROUTING_PROFILE_FASTEST
                                    ? OPENRIDE_ROUTING_PROFILE_TOURING
                                    : (*context->routing_profile) == OPENRIDE_ROUTING_PROFILE_TOURING
                                        ? OPENRIDE_ROUTING_PROFILE_TRAIL
                                        : OPENRIDE_ROUTING_PROFILE_FASTEST;
                                if (context->app_storage) {
                                    openride_app_storage_set_int(context->app_storage,
                                                                 "routing_profile",
                                                                 (int)(*context->routing_profile),
                                                                 context->error,
                                                                 context->error_size);
                                }
                                if (!(*context->gpx_navigation_active)) {
                                    if ((*context->loop_active)) {
                                        openride_app_route_clear_navigation_session(&(*context->navigation),
                                                                 &(*context->gps_simulator),
                                                                 &(*context->navigation_state),
                                                                 &(*context->gps_sample),
                                                                 &(*context->gps_sample_valid));
                                        openride_navigation_session_reset(&(*context->navigation_session));
                                        openride_location_filter_reset(&(*context->location_filter));
                                        openride_route_destroy(&(*context->route));
                                        (*context->route_valid) = false;
                                        (*context->loop_active) = false;
                                        (*context->loop_waypoint_count) = 0U;
                                        snprintf(context->route_status,
                                                 context->route_status_size,
                                                 "profil %s | regenere la boucle",
                                                 openride_routing_profile_name((*context->routing_profile)));
                                    } else if (openride_map_selection_complete(&(*context->selection))) {
                                        (*context->route_dirty) = true;
                                    }
                                }
                            } else if (mobile_hit.action == OPENRIDE_APP_UI_SETTINGS_FOLLOW) {
                                (*context->follow_gps) = !(*context->follow_gps);
                                if (context->app_storage) {
                                    openride_app_storage_set_int(context->app_storage,
                                                                 "follow_gps",
                                                                 (*context->follow_gps) ? 1 : 0,
                                                                 context->error,
                                                                 context->error_size);
                                }
                            } else if (mobile_hit.action == OPENRIDE_APP_UI_SETTINGS_REROUTE) {
                                (*context->auto_reroute) = !(*context->auto_reroute);
                                openride_navigation_session_set_auto_reroute(&(*context->navigation_session), (*context->auto_reroute));
                                if (context->app_storage) {
                                    openride_app_storage_set_int(context->app_storage,
                                                                 "auto_reroute",
                                                                 (*context->auto_reroute) ? 1 : 0,
                                                                 context->error,
                                                                 context->error_size);
                                }
                            } else if (mobile_hit.action == OPENRIDE_APP_UI_SETTINGS_VOICE) {
                                (*context->voice_enabled) = !(*context->voice_enabled);
                                openride_voice_guidance_set_enabled(&(*context->voice_guidance),
                                                                    (*context->voice_enabled));
                                if ((*context->voice_enabled)) {
                                    openride_android_voice_guidance_init();
                                }
                                if (context->app_storage) {
                                    openride_app_storage_set_int(context->app_storage,
                                                                 "voice_enabled",
                                                                 (*context->voice_enabled) ? 1 : 0,
                                                                 context->error,
                                                                 context->error_size);
                                }
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_SETTINGS_GPS_SIMULATION) {
                                if ((*context->simulated_gps_active)) {
                                    openride_location_provider_stop(
                                        &(*context->simulated_location_provider));
                                    (*context->simulated_gps_active) = false;
                                    (*context->simulator_deviation) = false;
                                    openride_gps_simulator_set_lateral_offset_m(
                                        &(*context->gps_simulator), 0.0);
                                    openride_drive_mode_set_active(&(*context->drive_mode), false);
                                    (*context->camera).bearing_deg = 0.0;
                                    (*context->gps_sample_valid) = false;
                                    memset(&(*context->gps_sample), 0, sizeof((*context->gps_sample)));
                                    memset(&(*context->navigation_state), 0, sizeof((*context->navigation_state)));
                                    memset(&(*context->filtered_location), 0, sizeof((*context->filtered_location)));
                                    openride_location_filter_reset(&(*context->location_filter));
                                    openride_voice_guidance_reset(&(*context->voice_guidance));
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "simulation GPS [DEV] arretee");
                                } else if (!(*context->route_valid) || !(*context->gps_simulator).route) {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "calcule un itineraire avant la simulation GPS");
                                } else {
                                    (*context->real_gps_requested) = false;
                                    if ((*context->real_gps_active)) {
                                        openride_location_provider_stop(
                                            &(*context->location_provider));
                                        (*context->real_gps_active) = false;
                                    }

                                    openride_navigation_session_reset(
                                        &(*context->navigation_session));
                                    openride_navigation_session_set_auto_reroute(
                                        &(*context->navigation_session),
                                        (*context->auto_reroute));
                                    openride_location_filter_reset(&(*context->location_filter));
                                    memset(&(*context->filtered_location), 0, sizeof((*context->filtered_location)));
                                    memset(&(*context->navigation_state), 0, sizeof((*context->navigation_state)));
                                    memset(&(*context->gps_sample), 0, sizeof((*context->gps_sample)));
                                    (*context->gps_sample_valid) = false;

                                    (*context->simulator_deviation) = false;
                                    openride_gps_simulator_set_lateral_offset_m(
                                        &(*context->gps_simulator), 0.0);
                                    openride_android_missed_turn_dev_reset(
                                        &(*context->missed_turn_dev),
                                        &(*context->simulated_location_context),
                                        &(*context->gps_simulator));
                                    (*context->simulated_gps_active) =
                                        openride_location_provider_start(
                                            &(*context->simulated_location_provider));
                                    if ((*context->simulated_gps_active)) {
                                        (*context->android_gps_sample_age_s) = INFINITY;
                                        (*context->android_gps_accuracy_m) = 3.0;
                                        openride_drive_mode_set_active(
                                            &(*context->drive_mode), true);
                                        openride_drive_mode_set_auto_zoom(
                                            &(*context->drive_mode), true);
                                        (*context->follow_gps) = true;
                                        openride_voice_guidance_reset(
                                            &(*context->voice_guidance));
                                        (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                        snprintf(context->route_status,
                                                 context->route_status_size,
                                                 "SIMULATION GPS [DEV] x%.0f",
                                                 (*context->simulated_location_context).time_scale);
                                    } else {
                                        snprintf(context->route_status,
                                                 context->route_status_size,
                                                 "simulation GPS indisponible");
                                    }
                                }
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_SETTINGS_GPS_DEVIATION) {
                                if ((*context->missed_turn_dev).armed
                                    || (*context->missed_turn_dev).active) {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "annule d'abord Virage rate reel [DEV]");
                                } else if (!(*context->simulated_gps_active)
                                    || !(*context->route_valid)
                                    || !(*context->gps_simulator).route) {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "active d'abord le GPS simule [DEV]");
                                } else if (!(*context->auto_reroute)) {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "active Recalcul auto avant le test DEV");
                                } else if ((*context->simulator_deviation)) {
                                    (*context->simulator_deviation) = false;
                                    openride_gps_simulator_set_lateral_offset_m(
                                        &(*context->gps_simulator), 0.0);
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "deviation GPS [DEV] annulee");
                                } else {
                                    (*context->simulator_deviation) = true;
                                    openride_gps_simulator_set_lateral_offset_m(
                                        &(*context->gps_simulator), 80.0);
                                    openride_drive_mode_set_active(&(*context->drive_mode), true);
                                    openride_drive_mode_set_auto_zoom(&(*context->drive_mode), true);
                                    (*context->follow_gps) = true;
                                    openride_voice_guidance_reset(&(*context->voice_guidance));
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "DEV +80 m: attente hors itineraire / recalcul");
                                }
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_SETTINGS_GPS_SPEED) {
                                const double current =
                                    (*context->simulated_location_context).time_scale;
                                const double next =
                                    current < 1.5 ? 2.0
                                    : current < 3.5 ? 5.0
                                    : 1.0;
                                openride_simulated_location_provider_set_time_scale(
                                    &(*context->simulated_location_context),
                                    next);
                                snprintf(context->route_status,
                                         context->route_status_size,
                                         "vitesse simulation [DEV] x%.0f",
                                         (*context->simulated_location_context).time_scale);
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_SETTINGS_GPS_MISSED_TURN) {
                                if ((*context->missed_turn_dev).armed
                                    || (*context->missed_turn_dev).active) {
                                    openride_android_missed_turn_dev_reset(
                                        &(*context->missed_turn_dev),
                                        &(*context->simulated_location_context),
                                        &(*context->gps_simulator));
                                    openride_location_filter_reset(&(*context->location_filter));
                                    memset(&(*context->filtered_location),
                                           0,
                                           sizeof((*context->filtered_location)));
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "virage rate reel [DEV] annule");
                                } else if (!(*context->simulated_gps_active)
                                           || !(*context->route_valid)
                                           || !(*context->gps_simulator).route) {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "active d'abord le GPS simule [DEV]");
                                } else if (!(*context->auto_reroute)) {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "active Recalcul auto avant le test DEV");
                                } else if ((*context->simulator_deviation)) {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "annule d'abord Deviation 80 m [DEV]");
                                } else if (!(*context->graph_loaded)
                                           || !(*context->route).nodes
                                           || (*context->route).node_count < 3U) {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "virage rate reel: itineraire mono-region requis");
                                } else if (!openride_dev_missed_turn_plan_build(
                                               &(*context->routing_graph),
                                               &(*context->route),
                                               (*context->gps_simulator).position_m,
                                               100.0,
                                               2500.0,
                                               80.0,
                                               &(*context->missed_turn_dev).plan,
                                               context->error,
                                               context->error_size)) {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "virage rate DEV impossible: %.140s",
                                             context->error[0] ? context->error : "aucune branche adaptee");
                                } else {
                                    const double speed_kph =
                                        (*context->gps_simulator).speed_mps > 0.1
                                            ? (*context->gps_simulator).speed_mps * 3.6
                                            : 60.0;
                                    if (!openride_gps_simulator_set_route(
                                            &(*context->missed_turn_dev).simulator,
                                            &(*context->missed_turn_dev).plan.branch_route,
                                            speed_kph,
                                            context->error,
                                            context->error_size)) {
                                        openride_dev_missed_turn_plan_destroy(
                                            &(*context->missed_turn_dev).plan);
                                        snprintf(context->route_status,
                                                 context->route_status_size,
                                                 "simulateur virage rate indisponible: %.120s",
                                                 context->error[0] ? context->error : "erreur");
                                    } else {
                                        (*context->missed_turn_dev).armed = true;
                                        (*context->missed_turn_dev).active = false;
                                        openride_drive_mode_set_active(
                                            &(*context->drive_mode), true);
                                        openride_drive_mode_set_auto_zoom(
                                            &(*context->drive_mode), true);
                                        (*context->follow_gps) = true;
                                        (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                        snprintf(
                                            context->route_status,
                                            context->route_status_size,
                                            "virage rate [DEV] arme dans %.0f m",
                                            (*context->missed_turn_dev).plan.trigger_position_m
                                                - (*context->gps_simulator).position_m);
                                    }
                                }
                            }
                            break;
                        }
    #endif

                        if ((*context->place_search_active)) {
                            const int result = openride_app_ui_place_search_result_at(context->renderer,
                                                                     x,
                                                                     y,
                                                                     width,
                                                                     height,
                                                                     (*context->place_search_result_count));
                            if (result >= 0) {
                                (*context->place_search_selected) = (uint32_t)result;
                                const OpenRidePlaceSearchResult *chosen = &context->place_search_results[(*context->place_search_selected)];
                                (*context->camera).center_lat = chosen->lat;
                                (*context->camera).center_lon = chosen->lon;
                                if ((*context->camera).zoom < 14.0) (*context->camera).zoom = 14.0;
                                if (context->app_storage) {
                                    openride_app_storage_add_history(context->app_storage,
                                                                     chosen->name,
                                                                     chosen->lat,
                                                                     chosen->lon,
                                                                     (int)chosen->kind,
                                                                     context->error,
                                                                     context->error_size);
                                    openride_app_search_refresh_stored_places(context->app_storage, false, context->history_places, &(*context->history_count));
                                }
                                if ((*context->place_search_purpose)
                                    == OPENRIDE_PLACE_SEARCH_ROUTE_START) {
                                    openride_map_selection_set(&(*context->selection),
                                                               OPENRIDE_MARKER_START,
                                                               chosen->lat,
                                                               chosen->lon);
                                    openride_map_selection_set_region_hint(
                                        &(*context->selection),
                                        OPENRIDE_MARKER_START,
                                        chosen->region_id);
                                    (*context->start_snap).segment_id =
                                        OPENRIDE_ROUTING_SEGMENT_NONE;
                                    openride_route_destroy(&(*context->route));
                                    (*context->route_valid) = false;
                                    (*context->route_dirty) = false;
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "depart: %.120s",
                                             chosen->name);
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_ROUTE;
                                } else if ((*context->place_search_purpose)
                                           == OPENRIDE_PLACE_SEARCH_ROUTE_DESTINATION) {
                                    openride_map_selection_set(&(*context->selection),
                                                               OPENRIDE_MARKER_DESTINATION,
                                                               chosen->lat,
                                                               chosen->lon);
                                    openride_map_selection_set_region_hint(
                                        &(*context->selection),
                                        OPENRIDE_MARKER_DESTINATION,
                                        chosen->region_id);
                                    (*context->destination_snap).segment_id =
                                        OPENRIDE_ROUTING_SEGMENT_NONE;
                                    openride_route_destroy(&(*context->route));
                                    (*context->route_valid) = false;
                                    (*context->route_dirty) = false;
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "arrivee: %.120s",
                                             chosen->name);
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_ROUTE;
                                } else {
                                    openride_app_search_set_destination_from_place(&(*context->selection),
                                                               &(*context->gps_sample),
                                                               (*context->gps_sample_valid),
                                                               chosen->lat,
                                                               chosen->lon,
                                                               chosen->name,
                                                               &(*context->route_dirty),
                                                               context->route_status,
                                                               context->route_status_size);
                                }
                                (*context->place_search_active) = false;
                                (*context->place_search_purpose) =
                                    OPENRIDE_PLACE_SEARCH_BROWSE;
                                SDL_StopTextInput(context->window);
                            }
                            break;
                        }

                        if ((*context->app_panel) == OPENRIDE_APP_PANEL_MAIN) {
                            if (openride_app_ui_panel_main_search_at(context->renderer, x, y, width, height)) {
                                (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                openride_app_search_open(context->window,
                                                  context->place_world,
                                                  &(*context->place_search_active),
                                                  context->place_search_query,
                                                  &(*context->place_search_result_count),
                                                  &(*context->place_search_selected),
                                                  context->route_status,
                                                  context->route_status_size);
                            } else {
                                const OpenRideAppPanel selected_panel = openride_app_ui_panel_main_at(context->renderer, x, y, width, height);
                                if (selected_panel != OPENRIDE_APP_PANEL_NONE) {
                                    (*context->app_panel) = selected_panel;
                                    (*context->app_panel_selected) = 0U;
                                    if (selected_panel == OPENRIDE_APP_PANEL_FAVORITES) {
                                        openride_app_search_refresh_stored_places(context->app_storage, true, context->favorite_places, &(*context->favorite_count));
                                    } else if (selected_panel == OPENRIDE_APP_PANEL_HISTORY) {
                                        openride_app_search_refresh_stored_places(context->app_storage, false, context->history_places, &(*context->history_count));
                                    }
                                }
                            }
                            break;
                        }
                        if ((*context->app_panel) == OPENRIDE_APP_PANEL_FAVORITES
                            || (*context->app_panel) == OPENRIDE_APP_PANEL_HISTORY) {
                            const bool favorites_panel = (*context->app_panel) == OPENRIDE_APP_PANEL_FAVORITES;
                            const uint32_t count = favorites_panel ? (*context->favorite_count) : (*context->history_count);
                            OpenRideStoredPlace *items = favorites_panel ? context->favorite_places : context->history_places;
                            const int chosen_index = openride_app_ui_panel_place_at(context->renderer, x, y, width, height, count);
                            if (chosen_index >= 0) {
                                const OpenRideStoredPlace *chosen = &items[chosen_index];
                                (*context->app_panel_selected) = (uint32_t)chosen_index;
                                (*context->camera).center_lat = chosen->lat;
                                (*context->camera).center_lon = chosen->lon;
                                if ((*context->camera).zoom < 14.0) (*context->camera).zoom = 14.0;
                                openride_app_search_set_destination_from_place(&(*context->selection),
                                                           &(*context->gps_sample),
                                                           (*context->gps_sample_valid),
                                                           chosen->lat,
                                                           chosen->lon,
                                                           chosen->name,
                                                           &(*context->route_dirty),
                                                           context->route_status,
                                                           context->route_status_size);
                                (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                            }
                            break;
                        }
                        if ((*context->app_panel) == OPENRIDE_APP_PANEL_REGIONS) {
                            const int region_action = openride_app_ui_panel_region_action_at(context->renderer, x, y, width, height);
                            if (region_action == 1 && !(*context->region_busy)) {
                                if (openride_region_status_ready(&(*context->region_status))
    #ifdef __ANDROID__
                                        && (*context->region_status).poly_present
    #endif
                                    ) {
                                    if ((*context->region) == (*context->active_region)) {
                                        snprintf(context->region_work_status, context->region_work_status_size,
                                                 "Cette region est deja active");
                                    } else {
                                        (*context->region_activation_requested) = true;
                                    }
                                } else {
    #ifdef __ANDROID__
                                    openride_app_region_begin_android_install(&(*context->platform_paths),
                                                                 (*context->region),
                                                                 &(*context->region_status),
                                                                 &(*context->region_prepare_context),
                                                                 &(*context->region_prepare_thread),
                                                                 &(*context->region_download_started),
                                                                 &(*context->region_download_is_poly),
                                                                 &(*context->region_busy),
                                                                 &(*context->region_progress),
                                                                 context->region_work_status,
                                                                 context->region_work_status_size,
                                                                 context->error,
                                                                 context->error_size);
    #else
                                    snprintf(context->region_work_status, context->region_work_status_size,
                                             "Preparation depuis le Terminal sur macOS");
    #endif
                                }
                            } else if (region_action == 2 && !(*context->region_busy)) {
                                if ((*context->region) == (*context->active_region)) {
                                    snprintf(context->region_work_status, context->region_work_status_size,
                                             "Impossible de supprimer la region active");
                                } else if (openride_region_remove_generated(&(*context->platform_paths),
                                                                            (*context->region),
                                                                            context->error,
                                                                            context->error_size)) {
                                    openride_region_get_status(&(*context->platform_paths), (*context->region),
                                                               &(*context->region_status), context->error, context->error_size);
                                    openride_app_region_refresh_map_world_overview(context->map_world, &(*context->platform_paths));
                                    snprintf(context->region_work_status, context->region_work_status_size,
                                             "Donnees de la region supprimees");
                                } else {
                                    snprintf(context->region_work_status, context->region_work_status_size,
                                             "Suppression impossible: %.120s", context->error);
                                }
                            }
                            break;
                        }
                        if ((*context->app_panel) != OPENRIDE_APP_PANEL_NONE) break;

                        if ((*context->drive_mode).active) {
                            const OpenRideDriveAction drive_action = openride_app_ui_drive_controls_hit_test(
                                context->renderer, x, y, width, height);
                            if (drive_action != OPENRIDE_DRIVE_ACTION_NONE) {
                                (*context->pending_drive_action) = drive_action;
                                openride_touch_input_cancel(&(*context->touch_input));
                                break;
                            }
                        } else {
                            const OpenRideToolbarAction toolbar_action = openride_app_ui_toolbar_hit_test(
                                context->renderer, x, y, width, height);
                            if (toolbar_action != OPENRIDE_TOOLBAR_NONE) {
                                (*context->route_map_pick_marker) = OPENRIDE_MARKER_NONE;
                                (*context->pending_toolbar_action) = toolbar_action;
                                openride_touch_input_cancel(&(*context->touch_input));
                                break;
                            }
                        }

                        /*
                         * Android route-point editing mirrors the desktop mouse
                         * interaction: touch a marker to edit it. A tap deletes
                         * it; a drag moves it. Route calculation is restarted only
                         * after the finger is released.
                         */
                        (*context->dragging_marker) =
                            ((*context->route_map_pick_marker) != OPENRIDE_MARKER_NONE
                             || (*context->drive_mode).active)
                                ? OPENRIDE_MARKER_NONE
                                : openride_app_render_marker_at_screen(&(*context->camera),
                                               &(*context->selection),
                                               x,
                                               y,
                                               width,
                                               height);
                        if ((*context->dragging_marker) != OPENRIDE_MARKER_NONE) {
                            openride_app_route_clear_navigation_session(&(*context->navigation),
                                                     &(*context->gps_simulator),
                                                     &(*context->navigation_state),
                                                     &(*context->gps_sample),
                                                     &(*context->gps_sample_valid));
                            openride_route_destroy(&(*context->route));
                            (*context->route_valid) = false;
                            (*context->route_dirty) = false;
                            (*context->loop_active) = false;
                            (*context->gpx_navigation_active) = false;
                            openride_navigation_session_reset(&(*context->navigation_session));
                            openride_location_filter_reset(&(*context->location_filter));
                            (*context->loop_waypoint_count) = 0U;
                        }

                        openride_touch_input_begin(&(*context->touch_input),
                                                   (uint64_t)event.tfinger.fingerID,
                                                   x,
                                                   y);
                        break;
                    }

                    case SDL_EVENT_FINGER_MOTION: {
                        if ((*context->place_search_active) || (*context->app_panel) != OPENRIDE_APP_PANEL_NONE) break;
                        int width = 0;
                        int height = 0;
                        SDL_GetCurrentRenderOutputSize(context->renderer, &width, &height);
                        const double x = (double)event.tfinger.x;
                        const double y = (double)event.tfinger.y;
                        const OpenRideTouchAction action = openride_touch_input_motion(
                            &(*context->touch_input),
                            (uint64_t)event.tfinger.fingerID,
                            x,
                            y);
                        if ((*context->dragging_marker) != OPENRIDE_MARKER_NONE) {
                            if (action.type == OPENRIDE_TOUCH_ACTION_PAN) {
                                double lat = 0.0;
                                double lon = 0.0;
                                openride_screen_to_geo(&(*context->camera),
                                                       x,
                                                       y,
                                                       width,
                                                       height,
                                                       &lat,
                                                       &lon);
                                openride_map_selection_set(&(*context->selection),
                                                           (*context->dragging_marker),
                                                           lat,
                                                           lon);
                            }
                        } else if (action.type == OPENRIDE_TOUCH_ACTION_PAN) {
                            openride_camera_pan(&(*context->camera), action.dx, action.dy);
                            if ((*context->drive_mode).active) {
                                (*context->follow_gps) = false;
                                openride_drive_mode_set_auto_zoom(&(*context->drive_mode), false);
                            }
                        }
                        break;
                    }

                    case SDL_EVENT_FINGER_UP: {
                        if ((*context->place_search_active) || (*context->app_panel) != OPENRIDE_APP_PANEL_NONE) {
                            openride_touch_input_cancel(&(*context->touch_input));
                            break;
                        }
                        int width = 0;
                        int height = 0;
                        SDL_GetCurrentRenderOutputSize(context->renderer, &width, &height);
                        const double x = (double)event.tfinger.x;
                        const double y = (double)event.tfinger.y;
                        const OpenRideTouchAction action = openride_touch_input_end(
                            &(*context->touch_input),
                            (uint64_t)event.tfinger.fingerID,
                            x,
                            y);

                        if ((*context->dragging_marker) != OPENRIDE_MARKER_NONE) {
                            const OpenRideSelectionMarker edited_marker = (*context->dragging_marker);
                            (*context->dragging_marker) = OPENRIDE_MARKER_NONE;

                            if (action.type == OPENRIDE_TOUCH_ACTION_TAP) {
                                openride_map_selection_remove(&(*context->selection), edited_marker);
                                if (edited_marker == OPENRIDE_MARKER_START) {
                                    (*context->start_snap).segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                                } else {
                                    (*context->destination_snap).segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                                }
                                (*context->route_dirty) = openride_map_selection_complete(&(*context->selection));
                                snprintf(context->route_status,
                                         context->route_status_size,
                                         "%s supprime - touche la carte pour le replacer",
                                         edited_marker == OPENRIDE_MARKER_START
                                             ? "Depart"
                                             : "Destination");
                            } else {
                                double lat = 0.0;
                                double lon = 0.0;
                                openride_screen_to_geo(&(*context->camera),
                                                       x,
                                                       y,
                                                       width,
                                                       height,
                                                       &lat,
                                                       &lon);
                                openride_map_selection_set(&(*context->selection),
                                                           edited_marker,
                                                           lat,
                                                           lon);
                                if (edited_marker == OPENRIDE_MARKER_START) {
                                    (*context->start_snap).segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                                } else {
                                    (*context->destination_snap).segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                                }
                                (*context->route_dirty) = openride_map_selection_complete(&(*context->selection));
                                snprintf(context->route_status,
                                         context->route_status_size,
                                         "%s deplace%s",
                                         edited_marker == OPENRIDE_MARKER_START
                                             ? "Depart"
                                             : "Destination",
                                         (*context->route_dirty) ? " - recalcul..." : "");
                            }

                            (*context->loop_active) = false;
                            (*context->loop_waypoint_count) = 0U;
                        } else if (action.type == OPENRIDE_TOUCH_ACTION_TAP
                                   && !(*context->drive_mode).active) {
                            if ((*context->route_map_pick_marker) != OPENRIDE_MARKER_NONE) {
                                double picked_lat = 0.0;
                                double picked_lon = 0.0;
                                openride_screen_to_geo(&(*context->camera),
                                                       action.x,
                                                       action.y,
                                                       width,
                                                       height,
                                                       &picked_lat,
                                                       &picked_lon);

                                const OpenRideSelectionMarker picked_marker =
                                    (*context->route_map_pick_marker);
                                (*context->route_map_pick_marker) = OPENRIDE_MARKER_NONE;

                                openride_map_selection_set(&(*context->selection),
                                                           picked_marker,
                                                           picked_lat,
                                                           picked_lon);
                                if (picked_marker == OPENRIDE_MARKER_START) {
                                    (*context->start_snap).segment_id =
                                        OPENRIDE_ROUTING_SEGMENT_NONE;
                                } else {
                                    (*context->destination_snap).segment_id =
                                        OPENRIDE_ROUTING_SEGMENT_NONE;
                                }

                                openride_app_route_clear_navigation_session(&(*context->navigation),
                                                         &(*context->gps_simulator),
                                                         &(*context->navigation_state),
                                                         &(*context->gps_sample),
                                                         &(*context->gps_sample_valid));
                                openride_navigation_session_reset(
                                    &(*context->navigation_session));
                                openride_location_filter_reset(&(*context->location_filter));
                                openride_route_destroy(&(*context->route));
                                (*context->route_valid) = false;
                                (*context->route_dirty) = false;
                                (*context->loop_active) = false;
                                (*context->gpx_navigation_active) = false;
                                (*context->loop_waypoint_count) = 0U;

                                snprintf(context->route_status,
                                         context->route_status_size,
                                         picked_marker == OPENRIDE_MARKER_START
                                             ? "Depart choisi sur la carte"
                                             : "Arrivee choisie sur la carte");
                                (*context->app_panel) = OPENRIDE_APP_PANEL_ROUTE;
                                (*context->app_panel_selected) = 0U;
                            } else {
                                openride_app_route_add_selection_from_screen(&(*context->selection),
                                                          &(*context->camera),
                                                          action.x,
                                                          action.y,
                                                          width,
                                                          height,
                                                          &(*context->route_dirty),
                                                          &(*context->loop_active),
                                                          &(*context->loop_waypoint_count),
                                                          context->route_status,
                                                          context->route_status_size);
                            }
                        }
                        break;
                    }

                    case SDL_EVENT_FINGER_CANCELED:
                        if ((*context->dragging_marker) != OPENRIDE_MARKER_NONE) {
                            (*context->dragging_marker) = OPENRIDE_MARKER_NONE;
                            (*context->route_dirty) = openride_map_selection_complete(&(*context->selection));
                        }
                        openride_touch_input_cancel(&(*context->touch_input));
                        break;

                    case SDL_EVENT_PINCH_BEGIN:
                        if ((*context->dragging_marker) != OPENRIDE_MARKER_NONE) {
                            (*context->dragging_marker) = OPENRIDE_MARKER_NONE;
                            (*context->route_dirty) = openride_map_selection_complete(&(*context->selection));
                        }
                        openride_touch_input_cancel(&(*context->touch_input));
                        break;

                    case SDL_EVENT_PINCH_UPDATE: {
                        if ((*context->place_search_active) || (*context->app_panel) != OPENRIDE_APP_PANEL_NONE) break;
                        int width = 0;
                        int height = 0;
                        SDL_GetCurrentRenderOutputSize(context->renderer, &width, &height);                    const double max_zoom = (*context->scalable_map) ? 18.0 : (double)(*context->metadata)->max_zoom;
                        const double min_zoom = context->map_world && (*context->scalable_map)
                            ? OPENRIDE_MAP_WORLD_MIN_ZOOM
                            : (double)(*context->metadata)->min_zoom;
                        if ((*context->drive_mode).active) {
                            openride_drive_mode_set_auto_zoom(&(*context->drive_mode), false);
                        }
                        double zoom_delta = openride_touch_pinch_zoom_delta((double)event.pinch.scale);
                        zoom_delta = openride_app_support_clampd(zoom_delta, -1.0, 1.0);
                        const double target_zoom = openride_app_support_clampd((*context->camera).zoom + zoom_delta,
                                                           min_zoom,
                                                           max_zoom);
                        const double focus_x = (double)width * 0.5;
                        const double focus_y = (double)height * 0.5;
                        openride_camera_zoom_at(&(*context->camera),
                                                target_zoom - (*context->camera).zoom,
                                                focus_x,
                                                focus_y,
                                                width,
                                                height);
                        break;
                    }

                    default:
                        break;
                }
            }
}

void openride_app_events_dispatch_pending(OpenRideAppEventContext *context)
{
    if (!context || !context->window || !context->renderer) return;

            if ((*context->pending_drive_action) != OPENRIDE_DRIVE_ACTION_NONE) {
                const OpenRideDriveAction action = (*context->pending_drive_action);
                (*context->pending_drive_action) = OPENRIDE_DRIVE_ACTION_NONE;
                if (action == OPENRIDE_DRIVE_ACTION_EXIT) {
                    openride_drive_mode_set_active(&(*context->drive_mode), false);
                    (*context->camera).bearing_deg = 0.0;
                    (*context->follow_gps) = false;
                    snprintf(context->route_status, context->route_status_size, "mode conduite ferme");
                } else if (action == OPENRIDE_DRIVE_ACTION_RECENTER) {
                    (*context->follow_gps) = true;
                    openride_drive_mode_set_auto_zoom(&(*context->drive_mode), true);
                    snprintf(context->route_status, context->route_status_size, "suivi GPS actif");
                } else if (action == OPENRIDE_DRIVE_ACTION_ORIENTATION) {
                    openride_drive_mode_set_heading_up(&(*context->drive_mode), !(*context->drive_mode).heading_up);
                    if (!(*context->drive_mode).heading_up) (*context->camera).bearing_deg = 0.0;
                    snprintf(context->route_status,
                             context->route_status_size,
                             "orientation %s",
                             (*context->drive_mode).heading_up ? "cap en haut" : "nord en haut");
                } else if (action == OPENRIDE_DRIVE_ACTION_GPS) {
    #ifdef __ANDROID__
                    (*context->real_gps_requested) = false;
                    if ((*context->simulated_gps_active)) {
                        openride_location_provider_stop(
                            &(*context->simulated_location_provider));
                        (*context->simulated_gps_active) = false;
                        (*context->simulator_deviation) = false;
                        openride_gps_simulator_set_lateral_offset_m(
                            &(*context->gps_simulator), 0.0);
                    }
                    if ((*context->real_gps_active)) {
                        openride_location_provider_stop(&(*context->location_provider));
                        (*context->real_gps_active) = false;
                    }
    #else
                    openride_gps_simulator_stop(&(*context->gps_simulator));
    #endif
                    openride_drive_mode_set_active(&(*context->drive_mode), false);
                    (*context->camera).bearing_deg = 0.0;
                    snprintf(context->route_status, context->route_status_size, "GPS arrete");
                }
            }

            if ((*context->pending_toolbar_action) != OPENRIDE_TOOLBAR_NONE) {
                const OpenRideToolbarAction action = (*context->pending_toolbar_action);
                (*context->pending_toolbar_action) = OPENRIDE_TOOLBAR_NONE;
                if (action == OPENRIDE_TOOLBAR_MENU) {
                    (*context->app_panel) = OPENRIDE_APP_PANEL_MAIN;
                    (*context->app_panel_selected) = 0U;
                } else if (action == OPENRIDE_TOOLBAR_SEARCH) {
                    (*context->place_search_purpose) =
                        OPENRIDE_PLACE_SEARCH_BROWSE;
                    openride_app_search_open(context->window,
                                      context->place_world,
                                      &(*context->place_search_active),
                                      context->place_search_query,
                                      &(*context->place_search_result_count),
                                      &(*context->place_search_selected),
                                      context->route_status,
                                      context->route_status_size);
                } else if (action == OPENRIDE_TOOLBAR_ROUTE) {
    #ifdef __ANDROID__
                    if ((*context->route_valid)) {
                        if ((*context->simulated_gps_active)) {
                            openride_drive_mode_set_active(&(*context->drive_mode), true);
                            openride_drive_mode_set_auto_zoom(&(*context->drive_mode), true);
                            (*context->follow_gps) = true;
                            snprintf(context->route_status,
                                     context->route_status_size,
                                     "navigation GPS simulee [DEV]");
                        } else {
                            (*context->real_gps_requested) = true;
                            if (!(*context->real_gps_active)) {
                                (*context->real_gps_active) =
                                    openride_location_provider_start(
                                        &(*context->location_provider));
                                (*context->android_gps_sample_age_s) = INFINITY;
                            }
                            if ((*context->real_gps_active)) {
                                openride_drive_mode_set_active(&(*context->drive_mode), true);
                                openride_drive_mode_set_auto_zoom(&(*context->drive_mode), true);
                                (*context->follow_gps) = true;
                                snprintf(context->route_status,
                                         context->route_status_size,
                                         "navigation demarree");
                            } else {
                                snprintf(context->route_status, context->route_status_size,
                                         "autorise la localisation Android puis retouche Demarrer");
                            }
                        }
                    } else {
                        (*context->planner_mode) = OPENRIDE_RIDE_PLANNER_ROUTE;
                        (*context->app_panel) = OPENRIDE_APP_PANEL_ROUTE;
                        (*context->app_panel_selected) = 0U;
                    }
    #else
                    (*context->planner_mode) = OPENRIDE_RIDE_PLANNER_ROUTE;
                    (*context->app_panel) = OPENRIDE_APP_PANEL_ROUTE;
                    (*context->app_panel_selected) = 0U;
    #endif
                } else if (action == OPENRIDE_TOOLBAR_LOOP) {
                    (*context->planner_mode) = OPENRIDE_RIDE_PLANNER_LOOP;
                    (*context->app_panel) = OPENRIDE_APP_PANEL_ROUTE;
                    (*context->app_panel_selected) = 0U;
                    (*context->route_map_pick_marker) = OPENRIDE_MARKER_NONE;
                } else if (action == OPENRIDE_TOOLBAR_GPS) {
    #ifdef __ANDROID__
                    if ((*context->simulated_gps_active)) {
                        openride_location_provider_stop(
                            &(*context->simulated_location_provider));
                        (*context->simulated_gps_active) = false;
                        (*context->simulator_deviation) = false;
                        openride_gps_simulator_set_lateral_offset_m(
                            &(*context->gps_simulator), 0.0);
                        openride_drive_mode_set_active(&(*context->drive_mode), false);
                        (*context->camera).bearing_deg = 0.0;
                        (*context->gps_sample_valid) = false;
                        memset(&(*context->filtered_location), 0, sizeof((*context->filtered_location)));
                        snprintf(context->route_status,
                                 context->route_status_size,
                                 "simulation GPS [DEV] arretee");
                    } else if ((*context->real_gps_active)) {
                        if ((*context->route_valid)) {
                            openride_drive_mode_set_active(&(*context->drive_mode), true);
                            openride_drive_mode_set_auto_zoom(&(*context->drive_mode), true);
                            (*context->follow_gps) = true;
                            snprintf(context->route_status, context->route_status_size, "mode conduite actif");
                        } else {
                            (*context->real_gps_requested) = false;
                            openride_location_provider_stop(&(*context->location_provider));
                            (*context->real_gps_active) = false;
                            openride_drive_mode_set_active(&(*context->drive_mode), false);
                            (*context->camera).bearing_deg = 0.0;
                            snprintf(context->route_status, context->route_status_size, "GPS reel arrete");
                        }
                    } else {
                        (*context->real_gps_requested) = true;
                        if (openride_location_provider_start(&(*context->location_provider))) {
                            (*context->real_gps_active) = true;
                            (*context->android_gps_sample_age_s) = INFINITY;
                            if ((*context->route_valid)) {
                                openride_drive_mode_set_active(&(*context->drive_mode), true);
                                openride_drive_mode_set_auto_zoom(&(*context->drive_mode), true);
                                (*context->follow_gps) = true;
                            }
                            snprintf(context->route_status,
                                     context->route_status_size,
                                     (*context->route_valid) ? "GPS reel + mode conduite" : "GPS reel actif");
                        } else {
                            snprintf(context->route_status, context->route_status_size,
                                     "autorise la localisation Android puis retouche GPS");
                        }
                    }
    #else
                    if ((*context->route_valid) && (*context->gps_simulator).route) {
                        const bool active = openride_gps_simulator_toggle(&(*context->gps_simulator));
                        snprintf(context->route_status,
                                 context->route_status_size,
                                 "simulation GPS %s",
                                 active ? "en cours" : "en pause");
                    } else {
                        snprintf(context->route_status, context->route_status_size, "calcule un itineraire avant le GPS");
                    }
    #endif
                }
            }
}
