#include "app_async_runtime.h"

#include <stdio.h>
#include <string.h>

void openride_app_async_update(OpenRideAppAsyncContext *context)
{
    if (!context || !context->events || !context->events->renderer
        || !context->map || !context->ormap || !context->metadata_storage
        || !context->raster_renderer || !context->place_index) return;

    #ifdef __ANDROID__
            if ((*context->events->region_download_started)) {
                if (!openride_android_region_download_poll(&(*context->region_download_status))) {
                    (*context->events->region_download_started) = false;
                    (*context->events->region_download_is_poly) = false;
                    (*context->events->region_busy) = false;
                    (*context->events->region_progress) = -1.0;
                    if ((*context->events->route_download_plan).downloading) {
                        (*context->events->route_download_plan).downloading = false;
                    }
                    snprintf(context->events->region_work_status, context->events->region_work_status_size,
                             "Etat du telechargement indisponible");
                } else if ((*context->region_download_status).state == OPENRIDE_ANDROID_DOWNLOAD_RUNNING) {
                    (*context->events->region_busy) = true;
                    if ((*context->region_download_status).total_bytes > 0U) {
                        (*context->events->region_progress) = (double)(*context->region_download_status).bytes_downloaded
                            / (double)(*context->region_download_status).total_bytes;
                    } else {
                        (*context->events->region_progress) = -1.0;
                    }
                    if ((*context->events->region_download_is_poly)) {
                        snprintf(context->events->region_work_status, context->events->region_work_status_size,
                                 "Telechargement contour: %.0f / %.0f Ko",
                                 (double)(*context->region_download_status).bytes_downloaded / 1024.0,
                                 (double)(*context->region_download_status).total_bytes / 1024.0);
                    } else {
                        snprintf(context->events->region_work_status, context->events->region_work_status_size,
                                 "Telechargement OSM: %.1f / %.1f Mo",
                                 (double)(*context->region_download_status).bytes_downloaded / (1024.0 * 1024.0),
                                 (double)(*context->region_download_status).total_bytes / (1024.0 * 1024.0));
                    }
                } else if ((*context->region_download_status).state == OPENRIDE_ANDROID_DOWNLOAD_COMPLETE) {
                    const bool completed_poly = (*context->events->region_download_is_poly);
                    (*context->events->region_download_started) = false;
                    (*context->events->region_download_is_poly) = false;
                    openride_region_get_status(&(*context->events->platform_paths), (*context->events->region),
                                               &(*context->events->region_status), context->events->error, context->events->error_size);
                    if (completed_poly) {
                        openride_app_region_refresh_map_world_overview(context->events->map_world, &(*context->events->platform_paths));
                        if (openride_region_status_ready(&(*context->events->region_status))) {
                            (*context->events->region_busy) = false;
                            (*context->events->region_progress) = 1.0;
                                if ((*context->events->route_download_plan).downloading) {
                                    openride_app_region_refresh_map_world_overview(context->events->map_world,
                                                               &(*context->events->platform_paths));
                                    if (context->events->place_world) {
                                        openride_place_world_refresh(context->events->place_world,
                                                                     context->events->error,
                                                                     context->events->error_size);
                                    }

                                    ++(*context->events->route_download_plan).index;
                                    if ((*context->events->route_download_plan).index
                                        < (*context->events->route_download_plan).count) {
                                        const OpenRideRegionDefinition *next_required =
                                            openride_region_find(
                                                (*context->events->route_download_plan).region_ids[
                                                    (*context->events->route_download_plan).index]);
                                        if (!next_required) {
                                            (*context->events->route_download_plan).downloading = false;
                                            snprintf(context->events->region_work_status,
                                                     context->events->region_work_status_size,
                                                     "Region requise suivante inconnue");
                                        } else {
                                            (*context->events->region) = next_required;
                                            openride_region_get_status(
                                                &(*context->events->platform_paths),
                                                (*context->events->region),
                                                &(*context->events->region_status),
                                                context->events->error,
                                                context->events->error_size);
                                            openride_app_region_begin_android_install(
                                                &(*context->events->platform_paths),
                                                (*context->events->region),
                                                &(*context->events->region_status),
                                                &(*context->events->region_prepare_context),
                                                &(*context->events->region_prepare_thread),
                                                &(*context->events->region_download_started),
                                                &(*context->events->region_download_is_poly),
                                                &(*context->events->region_busy),
                                                &(*context->events->region_progress),
                                                context->events->region_work_status,
                                                context->events->region_work_status_size,
                                                context->events->error,
                                                context->events->error_size);
                                            if (!(*context->events->region_busy)
                                                && !(*context->events->region_download_started)
                                                && !(*context->events->region_prepare_thread)) {
                                                (*context->events->route_download_plan).downloading =
                                                    false;
                                            }
                                        }
                                    } else {
                                        (*context->events->route_download_plan).downloading = false;
                                        (*context->events->route_download_plan).available = false;
                                        (*context->events->selection) = (*context->events->route_download_plan).selection;
                                        (*context->events->routing_profile) =
                                            (*context->events->route_download_plan).profile;
                                        (*context->events->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                        (*context->events->route_dirty) =
                                            openride_map_selection_complete(
                                                &(*context->events->selection));
                                        snprintf(context->events->route_status,
                                                 context->events->route_status_size,
                                                 "cartes pretes - recalcul itineraire...");
                                        snprintf(context->events->region_work_status,
                                                 context->events->region_work_status_size,
                                                 "Cartes requises installees");
                                    }
                                } else if ((*context->events->region) != (*context->events->active_region)) {
                                    snprintf(context->events->region_work_status,
                                             context->events->region_work_status_size,
                                             "Contour pret - activation en cours");
                                    (*context->events->region_activation_requested) = true;
                                } else {
                                    snprintf(context->events->region_work_status,
                                             context->events->region_work_status_size,
                                             "Contour de region ajoute a MapWorld");
                                }
                        } else if ((*context->events->region_status).source_pbf_present) {
                            (*context->events->region_prepare_thread) = openride_app_region_start_prepare_thread(&(*context->events->region_prepare_context),
                                                                                 &(*context->events->platform_paths),
                                                                                 (*context->events->region));
                            if ((*context->events->region_prepare_thread)) {
                                (*context->events->region_busy) = true;
                                (*context->events->region_progress) = openride_app_region_prepare_stage_progress(
                                    OPENRIDE_REGION_PREPARE_ROUTING);
                                snprintf(context->events->region_work_status, context->events->region_work_status_size,
                                         "Contour pret - preparation 1/3: routage");
                            } else {
                                (*context->events->region_busy) = false;
                                (*context->events->region_progress) = -1.0;
                                snprintf(context->events->region_work_status, context->events->region_work_status_size,
                                         "Impossible de lancer la preparation");
                            }
                        } else if (!openride_app_region_start_android_file_download((*context->events->region),
                                                                       false,
                                                                       &(*context->events->region_download_started),
                                                                       &(*context->events->region_download_is_poly),
                                                                       &(*context->events->region_busy),
                                                                       &(*context->events->region_progress),
                                                                       context->events->region_work_status,
                                                                       context->events->region_work_status_size)) {
                            (*context->events->region_busy) = false;
                            (*context->events->region_progress) = -1.0;
                        }
                    } else {
                        (*context->events->region_prepare_thread) = openride_app_region_start_prepare_thread(&(*context->events->region_prepare_context),
                                                                             &(*context->events->platform_paths),
                                                                             (*context->events->region));
                        if ((*context->events->region_prepare_thread)) {
                            (*context->events->region_busy) = true;
                            (*context->events->region_progress) = openride_app_region_prepare_stage_progress(
                                OPENRIDE_REGION_PREPARE_ROUTING);
                            snprintf(context->events->region_work_status, context->events->region_work_status_size,
                                     "Telechargement termine - preparation 1/3: routage");
                        } else {
                            (*context->events->region_busy) = false;
                            (*context->events->region_progress) = -1.0;
                            snprintf(context->events->region_work_status, context->events->region_work_status_size,
                                     "Impossible de lancer la preparation");
                        }
                    }
                } else if ((*context->region_download_status).state == OPENRIDE_ANDROID_DOWNLOAD_ERROR
                           || (*context->region_download_status).state == OPENRIDE_ANDROID_DOWNLOAD_CANCELLED) {
                    const bool failed_poly = (*context->events->region_download_is_poly);
                    (*context->events->region_download_started) = false;
                    (*context->events->region_download_is_poly) = false;
                    (*context->events->region_busy) = false;
                    (*context->events->region_progress) = -1.0;
                    if ((*context->events->route_download_plan).downloading) {
                        (*context->events->route_download_plan).downloading = false;
                    }
                    snprintf(context->events->region_work_status, context->events->region_work_status_size,
                             "%s%s%s",
                             (*context->region_download_status).state == OPENRIDE_ANDROID_DOWNLOAD_CANCELLED
                                 ? "Telechargement annule"
                                 : (failed_poly ? "Erreur contour" : "Erreur telechargement OSM"),
                             (*context->region_download_status).error[0] ? ": " : "",
                             (*context->region_download_status).error);
                }
            }
            if ((*context->events->region_prepare_thread)) {
                const int stage = SDL_GetAtomicInt(&(*context->events->region_prepare_context).stage);
                (*context->events->region_busy) = true;
                (*context->events->region_progress) = openride_app_region_prepare_stage_progress(stage);
                snprintf(context->events->region_work_status, context->events->region_work_status_size, "%s",
                         openride_app_region_prepare_stage_text(stage));
                if (SDL_GetAtomicInt(&(*context->events->region_prepare_context).done)) {
                    SDL_WaitThread((*context->events->region_prepare_thread), NULL);
                    (*context->events->region_prepare_thread) = NULL;
                    const bool prepared = SDL_GetAtomicInt(&(*context->events->region_prepare_context).success) != 0;
                    (*context->events->region_busy) = false;
                    (*context->events->region_progress) = prepared ? 1.0 : -1.0;
                    openride_region_get_status(&(*context->events->platform_paths), (*context->events->region),
                                               &(*context->events->region_status), context->events->error, context->events->error_size);
                    if (prepared) {
                        if ((*context->events->route_download_plan).downloading) {
                            openride_app_region_refresh_map_world_overview(context->events->map_world, &(*context->events->platform_paths));
                            if (context->events->place_world) {
                                openride_place_world_refresh(context->events->place_world,
                                                             context->events->error,
                                                             context->events->error_size);
                            }

                            ++(*context->events->route_download_plan).index;
                            if ((*context->events->route_download_plan).index
                                < (*context->events->route_download_plan).count) {
                                const OpenRideRegionDefinition *next_required =
                                    openride_region_find(
                                        (*context->events->route_download_plan).region_ids[
                                            (*context->events->route_download_plan).index]);
                                if (!next_required) {
                                    (*context->events->route_download_plan).downloading = false;
                                    snprintf(context->events->region_work_status,
                                             context->events->region_work_status_size,
                                             "Region requise suivante inconnue");
                                } else {
                                    (*context->events->region) = next_required;
                                    openride_region_get_status(&(*context->events->platform_paths),
                                                               (*context->events->region),
                                                               &(*context->events->region_status),
                                                               context->events->error,
                                                               context->events->error_size);
                                    openride_app_region_begin_android_install(
                                        &(*context->events->platform_paths),
                                        (*context->events->region),
                                        &(*context->events->region_status),
                                        &(*context->events->region_prepare_context),
                                        &(*context->events->region_prepare_thread),
                                        &(*context->events->region_download_started),
                                        &(*context->events->region_download_is_poly),
                                        &(*context->events->region_busy),
                                        &(*context->events->region_progress),
                                        context->events->region_work_status,
                                        context->events->region_work_status_size,
                                        context->events->error,
                                        context->events->error_size);
                                    if (!(*context->events->region_busy)
                                        && !(*context->events->region_download_started)
                                        && !(*context->events->region_prepare_thread)) {
                                        (*context->events->route_download_plan).downloading = false;
                                    }
                                }
                            } else {
                                (*context->events->route_download_plan).downloading = false;
                                (*context->events->route_download_plan).available = false;
                                (*context->events->selection) = (*context->events->route_download_plan).selection;
                                (*context->events->routing_profile) = (*context->events->route_download_plan).profile;
                                (*context->events->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                (*context->events->route_dirty) =
                                    openride_map_selection_complete(&(*context->events->selection));
                                snprintf(context->events->route_status,
                                         context->events->route_status_size,
                                         "cartes pretes - recalcul itineraire...");
                                snprintf(context->events->region_work_status,
                                         context->events->region_work_status_size,
                                         "Cartes requises installees");
                            }
                        } else {
                            snprintf(context->events->region_work_status,
                                     context->events->region_work_status_size,
                                     "Region prete - activation en cours");
                            (*context->events->region_activation_requested) = true;
                        }
                    } else {
                        if ((*context->events->route_download_plan).downloading) {
                            (*context->events->route_download_plan).downloading = false;
                        }
                        snprintf(context->events->region_work_status, context->events->region_work_status_size,
                                 "Preparation impossible: %.150s",
                                 (*context->events->region_prepare_context).error[0]
                                     ? (*context->events->region_prepare_context).error : "erreur inconnue");
                    }
                }
            }
    #endif
            /*
             * RoutingWorld borrows the currently active routing graph read-only.
             * Region activation can destroy/reload that graph, so defer activation
             * until the worker has joined on the main thread.
             */
            if ((*context->events->region_activation_requested) && !(*context->events->region_busy) && !(*context->events->routing_world_thread)) {
                (*context->events->region_activation_requested) = false;
                if ((*context->events->region) == (*context->events->active_region)) {
                    snprintf(context->events->region_work_status, context->events->region_work_status_size,
                             "Cette region est deja active");
                } else if (openride_app_region_activate_runtime(context->events->renderer,
                                                   &(*context->events->platform_paths),
                                                   (*context->events->region),
                                                   (*context->events->map_style),
                                                   &(*context->map),
                                                   &(*context->ormap),
                                                   &(*context->events->ormap_map),
                                                   &(*context->events->vector_map),
                                                   &(*context->events->scalable_map),
                                                   &(*context->metadata_storage),
                                                   &(*context->events->metadata),
                                                   &(*context->raster_renderer),
                                                   &(*context->events->vector_renderer),
                                                   &(*context->events->ormap_renderer),
                                                   &(*context->events->routing_graph),
                                                   &(*context->events->graph_loaded),
                                                   &(*context->place_index),
                                                   &(*context->events->camera),
                                                   &(*context->events->region_status),
                                                   context->events->error,
                                                   context->events->error_size)) {
                    (*context->events->active_region) = (*context->events->region);
                    openride_app_region_refresh_map_world_overview(context->events->map_world, &(*context->events->platform_paths));
                    if (context->events->place_world) {
                        openride_place_world_refresh(context->events->place_world,
                                                     context->events->error,
                                                     context->events->error_size);
                    }
                    if (context->events->app_storage) {
                        openride_app_storage_set_text(context->events->app_storage,
                                                      "active_region_id",
                                                      (*context->events->active_region)->id,
                                                      context->events->error,
                                                      context->events->error_size);
                    }
                    openride_route_destroy(&(*context->events->route));
                    openride_app_route_clear_navigation_session(&(*context->events->navigation),
                                             &(*context->events->gps_simulator),
                                             &(*context->events->navigation_state),
                                             &(*context->events->gps_sample),
                                             &(*context->events->gps_sample_valid));
                    openride_navigation_instructions_destroy(&(*context->events->navigation_instructions));
                    openride_navigation_session_reset(&(*context->events->navigation_session));
                    openride_location_filter_reset(&(*context->events->location_filter));
                    openride_map_selection_clear(&(*context->events->selection));
                    memset(&(*context->events->filtered_location), 0, sizeof((*context->events->filtered_location)));
                    (*context->events->route_valid) = false;
                    (*context->events->route_dirty) = false;
                    (*context->events->loop_active) = false;
                    (*context->events->loop_waypoint_count) = 0U;
                    (*context->events->gpx_navigation_active) = false;
                    (*context->events->simulator_deviation) = false;
                    (*context->events->start_snap).segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                    (*context->events->destination_snap).segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                    openride_drive_mode_set_active(&(*context->events->drive_mode), false);
                    (*context->events->camera).bearing_deg = 0.0;
                    snprintf(context->events->route_status, context->events->route_status_size,
                             "region active: %s", (*context->events->active_region)->name);
                    snprintf(context->events->region_work_status, context->events->region_work_status_size,
                             "%s active - carte, routage et recherche recharges",
                             (*context->events->active_region)->name);
                } else {
                    snprintf(context->events->region_work_status,
                             context->events->region_work_status_size,
                             "Activation impossible: %.145s",
                             context->events->error[0] ? context->events->error : "erreur inconnue");
                }
            }

            if ((*context->events->routing_world_thread)
                && SDL_GetAtomicInt(&(*context->events->routing_world_context).done)) {
                SDL_WaitThread((*context->events->routing_world_thread), NULL);
                (*context->events->routing_world_thread) = NULL;

                const bool request_current = openride_app_route_world_request_matches(
                    &(*context->events->routing_world_context),
                    (*context->events->active_region),
                    &(*context->events->selection),
                    (*context->events->routing_profile));
                const bool calculation_ok =
                    SDL_GetAtomicInt(&(*context->events->routing_world_context).success) != 0;

                if (!request_current) {
                    openride_route_destroy(&(*context->events->routing_world_context).route);
                    (*context->events->route_dirty) = openride_map_selection_complete(&(*context->events->selection));
                    (*context->events->routing_world_pending_reroute) = (*context->events->routing_world_context).reroute;
                    (*context->events->routing_world_pending_resume_simulator) =
                        (*context->events->routing_world_context).resume_simulator;
                } else if (!calculation_ok) {
                    openride_route_destroy(&(*context->events->routing_world_context).route);
                    (*context->events->route_valid) = false;

                    if ((*context->events->routing_world_context).result.download_required
                        && (*context->events->routing_world_context).result.missing_region_count > 0U) {
                        memset(&(*context->events->route_download_plan),
                               0,
                               sizeof((*context->events->route_download_plan)));
                        (*context->events->route_download_plan).available = true;
                        (*context->events->route_download_plan).count =
                            (*context->events->routing_world_context).result.missing_region_count;
                        if ((*context->events->route_download_plan).count
                            > OPENRIDE_ROUTING_WORLD_MAX_CORRIDOR_REGIONS) {
                            (*context->events->route_download_plan).count =
                                OPENRIDE_ROUTING_WORLD_MAX_CORRIDOR_REGIONS;
                        }
                        (*context->events->route_download_plan).has_installed_alternative =
                            (*context->events->routing_world_context).result.has_installed_alternative;
                        (*context->events->route_download_plan).selection = (*context->events->selection);
                        (*context->events->route_download_plan).profile = (*context->events->routing_profile);
                        for (uint32_t i = 0U;
                             i < (*context->events->route_download_plan).count;
                             ++i) {
                            snprintf((*context->events->route_download_plan).region_ids[i],
                                     sizeof((*context->events->route_download_plan).region_ids[i]),
                                     "%s",
                                     (*context->events->routing_world_context).result.missing_region_ids[i]);
                        }
    #ifdef __ANDROID__
                        (*context->events->app_panel) = OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS;
                        (*context->events->app_panel_selected) = 0U;
    #endif

                        const char *missing_id =
                            (*context->events->routing_world_context).result.missing_region_ids[0];
                        const OpenRideRegionDefinition *missing_region =
                            openride_region_find(missing_id);
                        const char *missing_name =
                            missing_region ? missing_region->name : missing_id;

                        if ((*context->events->routing_world_context).result.missing_region_count == 1U) {
                            snprintf(context->events->route_status,
                                     context->events->route_status_size,
                                     "carte requise: %.150s%s",
                                     missing_name,
                                     (*context->events->routing_world_context).result.has_installed_alternative
                                         ? " | alternative dispo"
                                         : "");
                        } else {
                            snprintf(context->events->route_status,
                                     context->events->route_status_size,
                                     "%u cartes requises, dont %.120s%s",
                                     (*context->events->routing_world_context).result.missing_region_count,
                                     missing_name,
                                     (*context->events->routing_world_context).result.has_installed_alternative
                                         ? " | alternative dispo"
                                         : "");
                        }

                        SDL_Log("RoutingWorld plan: %s -> %s | corridor=%u regions | "
                                "missing=%u | first_missing=%s | installed_alternative=%s",
                                (*context->events->routing_world_context).result.start_region_id,
                                (*context->events->routing_world_context).result.destination_region_id,
                                (*context->events->routing_world_context).result.recommended_corridor.count,
                                (*context->events->routing_world_context).result.missing_region_count,
                                missing_name,
                                (*context->events->routing_world_context).result.has_installed_alternative
                                    ? "yes"
                                    : "no");
                    } else if ((*context->events->routing_world_context).result.corridor_planned
                               && (*context->events->routing_world_context).result.recommended_corridor.count > 2U
                               && strcmp((*context->events->routing_world_context).error,
                                         "multi-hop regional corridor ready") == 0) {
                        snprintf(context->events->route_status,
                                 context->events->route_status_size,
                                 "corridor multi-region pret: %u regions",
                                 (*context->events->routing_world_context).result.recommended_corridor.count);
                        SDL_Log("RoutingWorld multi-hop corridor ready: %s -> %s | %u regions",
                                (*context->events->routing_world_context).result.start_region_id,
                                (*context->events->routing_world_context).result.destination_region_id,
                                (*context->events->routing_world_context).result.recommended_corridor.count);
                    } else {
                        snprintf(context->events->route_status,
                                 context->events->route_status_size,
                                 "itineraire impossible: %.180s",
                                 (*context->events->routing_world_context).error[0]
                                     ? (*context->events->routing_world_context).error
                                     : "aucune continuite inter-region");
                    }
                } else {
                    if ((*context->events->routing_world_context).result.used_installed_alternative) {
                        SDL_Log("RoutingWorld installed alternative: %s -> %s | corridor=%u regions",
                                (*context->events->routing_world_context).result.start_region_id,
                                (*context->events->routing_world_context).result.destination_region_id,
                                (*context->events->routing_world_context).result.installed_alternative.count);
                    }
                    memset(&(*context->events->route_download_plan), 0, sizeof((*context->events->route_download_plan)));
                    openride_route_destroy(&(*context->events->route));
                    (*context->events->route) = (*context->events->routing_world_context).route;
                    memset(&(*context->events->routing_world_context).route, 0, sizeof((*context->events->routing_world_context).route));

                    memset(&(*context->events->start_snap), 0, sizeof((*context->events->start_snap)));
                    memset(&(*context->events->destination_snap), 0, sizeof((*context->events->destination_snap)));
                    (*context->events->start_snap).segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
                    (*context->events->destination_snap).segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;

                    (*context->events->route_valid) = openride_app_route_prepare_navigation_session(&(*context->events->navigation),
                                                             &(*context->events->gps_simulator),
                                                             &(*context->events->navigation_instructions),
                                                             &(*context->events->routing_graph),
                                                             &(*context->events->route),
                                                             context->events->route_status,
                                                             context->events->route_status_size);
                    if ((*context->events->route_valid)) {
                        if ((*context->events->routing_world_context).reroute) {
                            openride_navigation_session_mark_rerouted(&(*context->events->navigation_session));
                            openride_location_filter_reset(&(*context->events->location_filter));
                            openride_voice_guidance_reset(&(*context->events->voice_guidance));
                            if ((*context->events->routing_world_context).resume_simulator) {
                                openride_gps_simulator_start(&(*context->events->gps_simulator));
                            }
                        }

                        if ((*context->events->routing_world_context).result.used_installed_alternative) {
                            snprintf(context->events->route_status,
                                     context->events->route_status_size,
                                     "alternative avec cartes installees | %.1f km",
                                     (*context->events->route).distance_m / 1000.0);
                        } else if ((*context->events->routing_world_context).result.multi_region) {
                            snprintf(context->events->route_status,
                                     context->events->route_status_size,
                                     "itineraire multi-region %s -> %s | %.1f km",
                                     (*context->events->routing_world_context).result.start_region_id,
                                     (*context->events->routing_world_context).result.destination_region_id,
                                     (*context->events->route).distance_m / 1000.0);
                        } else {
                            snprintf(context->events->route_status,
                                     context->events->route_status_size,
                                     "itineraire sur region installee %s | %.1f km",
                                     (*context->events->routing_world_context).result.start_region_id,
                                     (*context->events->route).distance_m / 1000.0);
                        }

    #ifdef __ANDROID__
                        if ((*context->events->real_gps_active) || (*context->events->simulated_gps_active)) {
                            openride_drive_mode_set_active(&(*context->events->drive_mode), true);
                            openride_drive_mode_set_auto_zoom(&(*context->events->drive_mode), true);
                            (*context->events->follow_gps) = true;
                        }
    #endif
                        if (!(*context->events->routing_world_context).reroute
                            && context->events->app_storage
                            && (*context->events->selection).has_destination) {
                            openride_app_storage_add_history(context->events->app_storage,
                                                             "Destination",
                                                             (*context->events->selection).destination.lat,
                                                             (*context->events->selection).destination.lon,
                                                             0,
                                                             context->events->error,
                                                             context->events->error_size);
                            openride_app_search_refresh_stored_places(context->events->app_storage,
                                                  false,
                                                  context->events->history_places,
                                                  &(*context->events->history_count));
                        }
                    }
                }

                memset(&(*context->events->routing_world_context).result, 0, sizeof((*context->events->routing_world_context).result));
                (*context->events->routing_world_context).error[0] = '\0';
            }

            if ((*context->events->route_dirty) && !(*context->events->routing_world_thread)) {
                (*context->events->loop_active) = false;
                (*context->events->gpx_navigation_active) = false;
                (*context->events->loop_waypoint_count) = 0U;
                openride_navigation_session_reset(&(*context->events->navigation_session));
                openride_location_filter_reset(&(*context->events->location_filter));
                memset(&(*context->events->filtered_location), 0, sizeof((*context->events->filtered_location)));
                openride_app_route_clear_navigation_session(&(*context->events->navigation),
                                         &(*context->events->gps_simulator),
                                         &(*context->events->navigation_state),
                                         &(*context->events->gps_sample),
                                         &(*context->events->gps_sample_valid));
                (*context->events->simulator_deviation) = false;

                (*context->events->route_valid) = openride_app_route_recalculate(&(*context->events->routing_graph),
                                                (*context->events->graph_loaded),
                                                &(*context->events->selection),
                                                (*context->events->routing_profile),
                                                &(*context->events->route),
                                                &(*context->events->start_snap),
                                                &(*context->events->destination_snap),
                                                context->events->route_status,
                                                context->events->route_status_size);

                bool world_route_started = false;
                if (!(*context->events->route_valid) && openride_map_selection_complete(&(*context->events->selection))) {
                    (*context->events->routing_world_thread) = openride_app_route_start_world_thread(
                        &(*context->events->routing_world_context),
                        &(*context->events->platform_paths),
                        (*context->events->active_region),
                        (*context->events->graph_loaded) ? &(*context->events->routing_graph) : NULL,
                        &(*context->events->routing_world_cache),
                        &(*context->events->selection),
                        (*context->events->routing_profile),
                        (*context->events->routing_world_pending_reroute),
                        (*context->events->routing_world_pending_resume_simulator));
                    world_route_started = (*context->events->routing_world_thread) != NULL;
                    if (world_route_started) {
                        snprintf(context->events->route_status,
                                 context->events->route_status_size,
                                 "%s",
                                 (*context->events->routing_world_pending_reroute)
                                     ? "Recalcul inter-region en cours..."
                                     : "Calcul de l'itineraire inter-region...");
                    } else {
                        snprintf(context->events->route_status,
                                 context->events->route_status_size,
                                 "Impossible de lancer le calcul inter-region");
                    }
                }

                if ((*context->events->route_valid)) {
                    openride_app_route_prepare_navigation_session(&(*context->events->navigation),
                                               &(*context->events->gps_simulator),
                                               &(*context->events->navigation_instructions),
                                               &(*context->events->routing_graph),
                                               &(*context->events->route),
                                               context->events->route_status,
                                               context->events->route_status_size);
                    if ((*context->events->routing_world_pending_reroute)) {
                        openride_navigation_session_mark_rerouted(&(*context->events->navigation_session));
                        openride_voice_guidance_reset(&(*context->events->voice_guidance));
                        if ((*context->events->routing_world_pending_resume_simulator)) {
                            openride_gps_simulator_start(&(*context->events->gps_simulator));
                        }
                    }
    #ifdef __ANDROID__
                    if ((*context->events->real_gps_active) || (*context->events->simulated_gps_active)) {
                        openride_drive_mode_set_active(&(*context->events->drive_mode), true);
                        openride_drive_mode_set_auto_zoom(&(*context->events->drive_mode), true);
                        (*context->events->follow_gps) = true;
                    }
    #endif
                    if (!(*context->events->routing_world_pending_reroute)
                        && context->events->app_storage
                        && (*context->events->selection).has_destination) {
                        openride_app_storage_add_history(context->events->app_storage,
                                                         "Destination",
                                                         (*context->events->selection).destination.lat,
                                                         (*context->events->selection).destination.lon,
                                                         0,
                                                         context->events->error,
                                                         context->events->error_size);
                        openride_app_search_refresh_stored_places(context->events->app_storage,
                                              false,
                                              context->events->history_places,
                                              &(*context->events->history_count));
                    }
                }

                (*context->events->routing_world_pending_reroute) = false;
                (*context->events->routing_world_pending_resume_simulator) = false;
                (*context->events->route_dirty) = false;
                (void)world_route_started;
            }

}
