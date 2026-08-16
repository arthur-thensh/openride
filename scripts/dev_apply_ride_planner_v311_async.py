#!/usr/bin/env python3
"""OpenRide V3.1 final — Ride Planner + animated async calculations.

This migrator composes the existing V3.1 Ride Planner migration entirely in
memory, then adds the async worker/loading layer before a single transactional
write. It is therefore still one migration/test cycle from the V3.0.5 tree.
"""

from __future__ import annotations

from pathlib import Path
import importlib.util
import math
import sys

ROOT = Path(__file__).resolve().parents[1]
BASE_PATH = ROOT / "scripts" / "dev_apply_ride_planner_v310.py"


def fail(message: str) -> None:
    raise RuntimeError(message)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def replace_between_once(text: str,
                         start_token: str,
                         end_token: str,
                         replacement: str,
                         label: str) -> str:
    start = text.find(start_token)
    if start < 0:
        fail(f"{label}: start token not found")
    if text.find(start_token, start + 1) >= 0:
        fail(f"{label}: start token is not unique")
    end = text.find(end_token, start)
    if end < 0:
        fail(f"{label}: end token not found")
    return text[:start] + replacement + text[end:]


def load_base_module():
    spec = importlib.util.spec_from_file_location("openride_v310_base", BASE_PATH)
    if not spec or not spec.loader:
        fail("unable to load V3.1 base migrator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fix_base_proposal_move(text: str) -> str:
    old = '''    memset(proposal, 0, sizeof(*proposal));\n    proposal->route = candidate->route;\n    memset(&candidate->route, 0, sizeof(candidate->route));\n    memcpy(proposal->waypoints, candidate->waypoints, sizeof(proposal->waypoints));\n    proposal->waypoint_count = OPENRIDE_LOOP_MAX_WAYPOINTS;\n    proposal->source_candidate_index = source_index;\n    proposal_fill_stats(&proposal->stats, candidate);\n'''
    new = '''    memset(proposal, 0, sizeof(*proposal));\n    proposal_fill_stats(&proposal->stats, candidate);\n    proposal->route = candidate->route;\n    memset(&candidate->route, 0, sizeof(candidate->route));\n    memcpy(proposal->waypoints, candidate->waypoints, sizeof(proposal->waypoints));\n    proposal->waypoint_count = OPENRIDE_LOOP_MAX_WAYPOINTS;\n    proposal->source_candidate_index = source_index;\n'''
    return replace_once(text, old, new, "proposal stats ownership fix")


def patch_cmake(text: str) -> str:
    return replace_once(
        text,
        "    src/app_async_runtime.c\n",
        "    src/app_async_runtime.c\n    src/app_planner_async_runtime.c\n",
        "CMake async planner runtime",
    )


def patch_runtime(text: str) -> str:
    text = replace_once(
        text,
        "    OpenRideRidePlannerMode planner_mode = OPENRIDE_RIDE_PLANNER_ROUTE;\n"
        "    OpenRideLoopProposalSet loop_proposals = {0};\n",
        "    OpenRideRidePlannerMode planner_mode = OPENRIDE_RIDE_PLANNER_ROUTE;\n"
        "    OpenRideRidePlannerBusy planner_busy = OPENRIDE_RIDE_PLANNER_IDLE;\n"
        "    OpenRidePlannerAsyncContext planner_async_context = {0};\n"
        "    SDL_Thread *planner_async_thread = NULL;\n"
        "    OpenRideLoopProposalSet loop_proposals = {0};\n",
        "runtime async planner state",
    )
    text = replace_once(
        text,
        "        .planner_mode = &planner_mode,\n"
        "        .loop_proposals = &loop_proposals,\n",
        "        .planner_mode = &planner_mode,\n"
        "        .planner_busy = &planner_busy,\n"
        "        .planner_async_context = &planner_async_context,\n"
        "        .planner_async_thread = &planner_async_thread,\n"
        "        .loop_proposals = &loop_proposals,\n",
        "event async planner context",
    )
    text = replace_once(
        text,
        "                       planner_mode,\n"
        "                       loop_target_distance_m,\n",
        "                       planner_mode,\n"
        "                       planner_busy,\n"
        "                       loop_target_distance_m,\n",
        "runtime planner busy draw arg",
    )
    text = replace_once(
        text,
        "    if (routing_world_thread) {\n"
        "        SDL_WaitThread(routing_world_thread, NULL);\n"
        "        routing_world_thread = NULL;\n"
        "    }\n"
        "    openride_route_destroy(&routing_world_context.route);\n",
        "    if (planner_async_thread) {\n"
        "        SDL_WaitThread(planner_async_thread, NULL);\n"
        "        planner_async_thread = NULL;\n"
        "    }\n"
        "    openride_app_planner_async_reset(&planner_async_context);\n"
        "    planner_busy = OPENRIDE_RIDE_PLANNER_IDLE;\n\n"
        "    if (routing_world_thread) {\n"
        "        SDL_WaitThread(routing_world_thread, NULL);\n"
        "        routing_world_thread = NULL;\n"
        "    }\n"
        "    openride_route_destroy(&routing_world_context.route);\n",
        "runtime async planner shutdown",
    )
    return text


def patch_bridge(text: str) -> str:
    text = replace_once(
        text,
        "                                  OpenRideRidePlannerMode planner_mode,\n"
        "                                  double loop_target_distance_m,\n",
        "                                  OpenRideRidePlannerMode planner_mode,\n"
        "                                  OpenRideRidePlannerBusy planner_busy,\n"
        "                                  double loop_target_distance_m,\n",
        "bridge private busy arg",
    )
    text = replace_once(
        text,
        "                .mode = planner_mode,\n"
        "                .gps_valid = gps_valid,\n",
        "                .mode = planner_mode,\n"
        "                .busy = planner_busy,\n"
        "                .gps_valid = gps_valid,\n",
        "bridge route panel busy state",
    )
    text = replace_once(
        text,
        "                           OpenRideRidePlannerMode planner_mode,\n"
        "                           double loop_target_distance_m,\n",
        "                           OpenRideRidePlannerMode planner_mode,\n"
        "                           OpenRideRidePlannerBusy planner_busy,\n"
        "                           double loop_target_distance_m,\n",
        "bridge public busy arg",
    )
    text = replace_once(
        text,
        "                      planner_mode,\n"
        "                      loop_target_distance_m,\n",
        "                      planner_mode,\n"
        "                      planner_busy,\n"
        "                      loop_target_distance_m,\n",
        "bridge busy forwarding",
    )
    return text


def patch_event(text: str) -> str:
    panel_entry = (
        "                        if (!(*context->place_search_active) "
        "&& (*context->app_panel) != OPENRIDE_APP_PANEL_NONE) {\n"
    )
    panel_guard = panel_entry + (
        "                            if ((*context->planner_busy) != OPENRIDE_RIDE_PLANNER_IDLE\n"
        "                                && ((*context->app_panel) == OPENRIDE_APP_PANEL_ROUTE\n"
        "                                    || (*context->app_panel) == OPENRIDE_APP_PANEL_LOOP_PROPOSALS)) {\n"
        "                                openride_touch_input_cancel(&(*context->touch_input));\n"
        "                                break;\n"
        "                            }\n"
    )
    text = replace_once(text, panel_entry, panel_guard,
                        "event busy input guard")

    start_token = (
        "                            } else if (mobile_hit.action\n"
        "                                       == OPENRIDE_APP_UI_ROUTE_CALCULATE) {\n"
    )
    end_token = (
        "                            } else if (mobile_hit.action\n"
        "                                       == OPENRIDE_APP_UI_ROUTE_DOWNLOAD_REQUIRED) {\n"
    )
    async_actions = r'''                            } else if (mobile_hit.action
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
                                    (uint32_t)mobile_hit.index,
                                    &(*context->route),
                                    &(*context->loop_stats),
                                    context->loop_waypoints,
                                    &(*context->loop_waypoint_count),
                                    context->route_status,
                                    context->route_status_size);
                                (*context->loop_active) = (*context->route_valid);
                                if ((*context->route_valid)) {
                                    openride_app_route_prepare_navigation_session(
                                        &(*context->navigation),
                                        &(*context->gps_simulator),
                                        &(*context->navigation_instructions),
                                        &(*context->routing_graph),
                                        &(*context->route),
                                        context->route_status,
                                        context->route_status_size);
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                }
'''
    text = replace_between_once(text, start_token, end_token,
                                async_actions, "event async planner actions")
    return text


def patch_async_runtime(text: str) -> str:
    marker = '''            /*
             * RoutingWorld borrows the currently active routing graph read-only.
'''
    handler = r'''            if ((*context->events->planner_async_thread)
                && SDL_GetAtomicInt(&(*context->events->planner_async_context).done)) {
                const OpenRideRidePlannerBusy completed_kind =
                    (*context->events->planner_async_context).kind;
                SDL_WaitThread((*context->events->planner_async_thread), NULL);
                (*context->events->planner_async_thread) = NULL;

                const bool calculation_ok =
                    SDL_GetAtomicInt(&(*context->events->planner_async_context).success) != 0;

                if (completed_kind == OPENRIDE_RIDE_PLANNER_GENERATING_LOOPS) {
                    const bool request_current = openride_app_planner_async_loop_request_matches(
                        &(*context->events->planner_async_context),
                        &(*context->events->selection),
                        (*context->events->routing_profile),
                        (*context->events->loop_target_distance_m),
                        (*context->events->loop_direction));
                    if (!request_current) {
                        snprintf(context->events->route_status,
                                 context->events->route_status_size,
                                 "Parametres modifies - relance la recherche de balades");
                    } else if (!calculation_ok) {
                        snprintf(context->events->route_status,
                                 context->events->route_status_size,
                                 "%s",
                                 (*context->events->planner_async_context).status[0]
                                     ? (*context->events->planner_async_context).status
                                     : "Recherche de balades impossible");
                    } else {
                        openride_loop_proposal_set_destroy(context->events->loop_proposals);
                        (*context->events->loop_proposals) =
                            (*context->events->planner_async_context).proposals;
                        memset(&(*context->events->planner_async_context).proposals,
                               0,
                               sizeof((*context->events->planner_async_context).proposals));
                        (*context->events->start_snap) =
                            (*context->events->planner_async_context).start_snap;
                        (*context->events->app_panel) = OPENRIDE_APP_PANEL_LOOP_PROPOSALS;
                        snprintf(context->events->route_status,
                                 context->events->route_status_size,
                                 "%s",
                                 (*context->events->planner_async_context).status[0]
                                     ? (*context->events->planner_async_context).status
                                     : "Balades pretes");
                    }
                    (*context->events->planner_busy) = OPENRIDE_RIDE_PLANNER_IDLE;
                    openride_app_planner_async_reset(
                        &(*context->events->planner_async_context));
                } else if (completed_kind == OPENRIDE_RIDE_PLANNER_CALCULATING_ROUTE) {
                    const bool request_current = openride_app_planner_async_route_request_matches(
                        &(*context->events->planner_async_context),
                        &(*context->events->selection),
                        (*context->events->routing_profile));
                    if (!request_current) {
                        (*context->events->planner_busy) = OPENRIDE_RIDE_PLANNER_IDLE;
                        snprintf(context->events->route_status,
                                 context->events->route_status_size,
                                 "Points modifies - relance le calcul");
                    } else if (calculation_ok) {
                        openride_app_route_clear_navigation_session(
                            &(*context->events->navigation),
                            &(*context->events->gps_simulator),
                            &(*context->events->navigation_state),
                            &(*context->events->gps_sample),
                            &(*context->events->gps_sample_valid));
                        openride_navigation_session_reset(
                            &(*context->events->navigation_session));
                        openride_location_filter_reset(&(*context->events->location_filter));
                        memset(&(*context->events->filtered_location),
                               0,
                               sizeof((*context->events->filtered_location)));
                        openride_route_destroy(&(*context->events->route));
                        (*context->events->route) =
                            (*context->events->planner_async_context).route;
                        memset(&(*context->events->planner_async_context).route,
                               0,
                               sizeof((*context->events->planner_async_context).route));
                        (*context->events->start_snap) =
                            (*context->events->planner_async_context).start_snap;
                        (*context->events->destination_snap) =
                            (*context->events->planner_async_context).destination_snap;
                        (*context->events->loop_active) = false;
                        (*context->events->loop_waypoint_count) = 0U;
                        (*context->events->gpx_navigation_active) = false;
                        (*context->events->simulator_deviation) = false;
                        (*context->events->route_valid) =
                            openride_app_route_prepare_navigation_session(
                                &(*context->events->navigation),
                                &(*context->events->gps_simulator),
                                &(*context->events->navigation_instructions),
                                &(*context->events->routing_graph),
                                &(*context->events->route),
                                context->events->route_status,
                                context->events->route_status_size);
                        (*context->events->planner_busy) = OPENRIDE_RIDE_PLANNER_IDLE;
                        if ((*context->events->route_valid)) {
                            (*context->events->app_panel) = OPENRIDE_APP_PANEL_NONE;
                            snprintf(context->events->route_status,
                                     context->events->route_status_size,
                                     "itineraire pret | %.1f km",
                                     (*context->events->route).distance_m / 1000.0);
#ifdef __ANDROID__
                            if ((*context->events->real_gps_active)
                                || (*context->events->simulated_gps_active)) {
                                openride_drive_mode_set_active(
                                    &(*context->events->drive_mode), true);
                                openride_drive_mode_set_auto_zoom(
                                    &(*context->events->drive_mode), true);
                                (*context->events->follow_gps) = true;
                            }
#endif
                            if (context->events->app_storage
                                && (*context->events->selection).has_destination) {
                                openride_app_storage_add_history(
                                    context->events->app_storage,
                                    "Destination",
                                    (*context->events->selection).destination.lat,
                                    (*context->events->selection).destination.lon,
                                    0,
                                    context->events->error,
                                    context->events->error_size);
                                openride_app_search_refresh_stored_places(
                                    context->events->app_storage,
                                    false,
                                    context->events->history_places,
                                    &(*context->events->history_count));
                            }
                        }
                    } else {
                        (*context->events->routing_world_thread) =
                            openride_app_route_start_world_thread(
                                &(*context->events->routing_world_context),
                                &(*context->events->platform_paths),
                                (*context->events->active_region),
                                (*context->events->graph_loaded)
                                    ? &(*context->events->routing_graph) : NULL,
                                &(*context->events->routing_world_cache),
                                &(*context->events->selection),
                                (*context->events->routing_profile),
                                false,
                                false);
                        if ((*context->events->routing_world_thread)) {
                            (*context->events->planner_busy) =
                                OPENRIDE_RIDE_PLANNER_CALCULATING_ROUTE;
                            snprintf(context->events->route_status,
                                     context->events->route_status_size,
                                     "Calcul de l'itineraire inter-region...");
                        } else {
                            (*context->events->planner_busy) = OPENRIDE_RIDE_PLANNER_IDLE;
                            snprintf(context->events->route_status,
                                     context->events->route_status_size,
                                     "%s",
                                     (*context->events->planner_async_context).status[0]
                                         ? (*context->events->planner_async_context).status
                                         : "Calcul de l'itineraire impossible");
                        }
                    }
                    openride_app_planner_async_reset(
                        &(*context->events->planner_async_context));
                } else {
                    (*context->events->planner_busy) = OPENRIDE_RIDE_PLANNER_IDLE;
                    openride_app_planner_async_reset(
                        &(*context->events->planner_async_context));
                }
            }

'''
    text = replace_once(text, marker, handler + marker,
                        "async planner completion handler")
    text = replace_once(
        text,
        "            if ((*context->events->region_activation_requested) && !(*context->events->region_busy) && !(*context->events->routing_world_thread)) {\n",
        "            if ((*context->events->region_activation_requested)\n"
        "                && !(*context->events->region_busy)\n"
        "                && !(*context->events->routing_world_thread)\n"
        "                && !(*context->events->planner_async_thread)) {\n",
        "region activation planner guard",
    )
    text = replace_once(
        text,
        "                SDL_WaitThread((*context->events->routing_world_thread), NULL);\n"
        "                (*context->events->routing_world_thread) = NULL;\n\n"
        "                const bool request_current = openride_app_route_world_request_matches(\n",
        "                SDL_WaitThread((*context->events->routing_world_thread), NULL);\n"
        "                (*context->events->routing_world_thread) = NULL;\n"
        "                const bool planner_world_request =\n"
        "                    (*context->events->planner_busy) ==\n"
        "                        OPENRIDE_RIDE_PLANNER_CALCULATING_ROUTE;\n"
        "                if (planner_world_request) {\n"
        "                    (*context->events->planner_busy) = OPENRIDE_RIDE_PLANNER_IDLE;\n"
        "                }\n\n"
        "                const bool request_current = openride_app_route_world_request_matches(\n",
        "routing world clears planner busy",
    )
    text = replace_once(
        text,
        "                    if ((*context->events->route_valid)) {\n"
        "                        if ((*context->events->routing_world_context).reroute) {\n",
        "                    if ((*context->events->route_valid)) {\n"
        "                        if (planner_world_request) {\n"
        "                            (*context->events->app_panel) = OPENRIDE_APP_PANEL_NONE;\n"
        "                        }\n"
        "                        if ((*context->events->routing_world_context).reroute) {\n",
        "routing world closes planner on success",
    )
    text = replace_once(
        text,
        "            if ((*context->events->route_dirty) && !(*context->events->routing_world_thread)) {\n",
        "            if ((*context->events->route_dirty)\n"
        "                && !(*context->events->routing_world_thread)\n"
        "                && !(*context->events->planner_async_thread)) {\n",
        "route dirty planner worker guard",
    )
    return text


def patch_icon(text: str) -> str:
    old_tail = '''    [OPENRIDE_UI_ICON_LOCATION] =
        "<svg viewBox=\\"0 0 24 24\\" fill=\\"none\\" stroke=\\"currentColor\\">"
        "<circle cx=\\"12\\" cy=\\"9\\" r=\\"3\\"/>"
        "<polyline points=\\"12,21 6,13 5,9 6,5 9,3 12,2 15,3 18,5 19,9 18,13 12,21\\"/>"
        "</svg>"
};
'''
    new_tail = '''    [OPENRIDE_UI_ICON_LOCATION] =
        "<svg viewBox=\\"0 0 24 24\\" fill=\\"none\\" stroke=\\"currentColor\\">"
        "<circle cx=\\"12\\" cy=\\"9\\" r=\\"3\\"/>"
        "<polyline points=\\"12,21 6,13 5,9 6,5 9,3 12,2 15,3 18,5 19,9 18,13 12,21\\"/>"
        "</svg>",
    [OPENRIDE_UI_ICON_LOADING] =
        "<svg viewBox=\\"0 0 24 24\\" fill=\\"none\\" stroke=\\"currentColor\\">"
        "<polyline points=\\"12,3 15.5,3.8 18.2,6.2 20,9.5 20.5,12 19.8,15.5 17.5,18.3 14.2,20 10.5,20.5 7.2,19 4.8,16.5\\"/>"
        "</svg>"
};
'''
    text = replace_once(text, old_tail, new_tail, "loading SVG source")
    text = replace_once(
        text,
        "static OpenRideUISVGDocument icon_cache[OPENRIDE_UI_ICON_COUNT];\n",
        "static OpenRideUISVGDocument icon_cache[OPENRIDE_UI_ICON_COUNT];\n"
        "static float icon_rotation_radians = 0.0f;\n",
        "icon rotation state",
    )
    old_point = '''    OpenRideUISVGPoint point = {
        origin_x + x * scale,
        origin_y + y * scale
    };
    return point;
}
'''
    new_point = '''    OpenRideUISVGPoint point = {
        origin_x + x * scale,
        origin_y + y * scale
    };
    if (fabsf(icon_rotation_radians) > 0.0001f) {
        const float center_x = target->x + target->w * 0.5f;
        const float center_y = target->y + target->h * 0.5f;
        const float dx = point.x - center_x;
        const float dy = point.y - center_y;
        const float c = cosf(icon_rotation_radians);
        const float s = sinf(icon_rotation_radians);
        point.x = center_x + dx * c - dy * s;
        point.y = center_y + dx * s + dy * c;
    }
    return point;
}
'''
    text = replace_once(text, old_point, new_point, "SVG point rotation")
    append = '''

bool openride_ui_icon_draw_rotated(OpenRideUIContext *ui,
                                   OpenRideUIIcon icon,
                                   OpenRideUIRect rect,
                                   OpenRideUIColor color,
                                   float stroke_width,
                                   float angle_degrees)
{
    const float previous = icon_rotation_radians;
    icon_rotation_radians = angle_degrees * 0.01745329251994329577f;
    const bool ok = openride_ui_icon_draw(ui, icon, rect, color, stroke_width);
    icon_rotation_radians = previous;
    return ok;
}
'''
    if "openride_ui_icon_draw_rotated(" in text:
        fail("rotated icon renderer already present")
    return text.rstrip() + append + "\n"


def patch_route_ui(text: str) -> str:
    text = replace_once(
        text,
        "    OpenRideUIRoutePanelAction clicked = OPENRIDE_UI_ROUTE_PANEL_NONE;\n",
        "    OpenRideUIRoutePanelAction clicked = OPENRIDE_UI_ROUTE_PANEL_NONE;\n"
        "    const bool planner_busy = state->busy != OPENRIDE_RIDE_PLANNER_IDLE;\n"
        "    const bool interactive = !planner_busy;\n",
        "planner UI busy state",
    )
    text = text.replace(
        "                           OPENRIDE_UI_BUTTON_GHOST, true,\n"
        "                           state->mode == OPENRIDE_RIDE_PLANNER_ROUTE)",
        "                           OPENRIDE_UI_BUTTON_GHOST, interactive,\n"
        "                           state->mode == OPENRIDE_RIDE_PLANNER_ROUTE)",
    )
    text = text.replace(
        "                           OPENRIDE_UI_BUTTON_GHOST, true,\n"
        "                           state->mode == OPENRIDE_RIDE_PLANNER_LOOP)",
        "                           OPENRIDE_UI_BUTTON_GHOST, interactive,\n"
        "                           state->mode == OPENRIDE_RIDE_PLANNER_LOOP)",
    )
    for old in [
        'OPENRIDE_UI_ICON_GPS, gps_label, true,',
        'OPENRIDE_UI_ICON_SEARCH, "Rechercher un lieu", true,',
        'OPENRIDE_UI_ICON_MAP, "Choisir sur la carte", true,',
    ]:
        text = text.replace(old, old[:-5] + "interactive,")
    text = text.replace(
        "                               OPENRIDE_UI_BUTTON_GHOST, true,\n"
        "                               state->profile == profile_values[i])",
        "                               OPENRIDE_UI_BUTTON_GHOST, interactive,\n"
        "                               state->profile == profile_values[i])",
    )
    text = text.replace(
        "                               OPENRIDE_UI_BUTTON_GHOST, true, false)) {",
        "                               OPENRIDE_UI_BUTTON_GHOST, interactive, false)) {",
    )
    text = text.replace(
        "                               OPENRIDE_UI_BUTTON_GHOST, true, false)) {\n"
        "            clicked = OPENRIDE_UI_ROUTE_PANEL_LOOP_DIRECTION;",
        "                               OPENRIDE_UI_BUTTON_GHOST, interactive, false)) {\n"
        "            clicked = OPENRIDE_UI_ROUTE_PANEL_LOOP_DIRECTION;",
    )

    old_primary = '''    const bool can_generate = state->mode == OPENRIDE_RIDE_PLANNER_LOOP
        ? state->has_start
        : state->has_start && state->has_destination;
    const char *primary_label = state->mode == OPENRIDE_RIDE_PLANNER_LOOP
        ? "Proposer des balades" : "Calculer l’itinéraire";
    if (openride_ui_button(ui, planner_id("planner-primary"), layout.primary,
                           primary_label,
                           can_generate ? OPENRIDE_UI_BUTTON_PRIMARY
                                        : OPENRIDE_UI_BUTTON_SECONDARY,
                           can_generate, false)) {
        clicked = OPENRIDE_UI_ROUTE_PANEL_CALCULATE;
    }

    openride_ui_text(ui, layout.hint,
                     state->mode == OPENRIDE_RIDE_PLANNER_LOOP
                         ? "3 propositions seront comparées avant de partir"
                         : "Le trajet sera affiché sur la carte avant le départ",
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_CENTER);

    if (openride_ui_button(ui, planner_id("planner-back"), layout.back,
                           "Retour", OPENRIDE_UI_BUTTON_GHOST, true, false)) {
'''
    new_primary = '''    const bool can_generate = state->mode == OPENRIDE_RIDE_PLANNER_LOOP
        ? state->has_start
        : state->has_start && state->has_destination;
    const char *primary_label = state->mode == OPENRIDE_RIDE_PLANNER_LOOP
        ? "Proposer des balades" : "Calculer l’itinéraire";
    if (planner_busy) {
        (void)openride_ui_button(ui,
                                 planner_id("planner-primary"),
                                 layout.primary,
                                 "",
                                 OPENRIDE_UI_BUTTON_PRIMARY,
                                 false,
                                 false);
        const float angle = (float)(SDL_GetTicks() % 1200U) * (360.0f / 1200.0f);
        openride_ui_icon_draw_rotated(
            ui,
            OPENRIDE_UI_ICON_LOADING,
            openride_ui_rect(layout.primary.x + 18.0f,
                             layout.primary.y + (layout.primary.h - 22.0f) * 0.5f,
                             22.0f,
                             22.0f),
            ui->theme.text,
            1.8f,
            angle);
        openride_ui_text_color(
            ui,
            openride_ui_rect(layout.primary.x + 50.0f,
                             layout.primary.y,
                             layout.primary.w - 64.0f,
                             layout.primary.h),
            state->busy == OPENRIDE_RIDE_PLANNER_GENERATING_LOOPS
                ? "Recherche de balades..."
                : "Calcul de l’itinéraire...",
            OPENRIDE_UI_TEXT_BODY,
            OPENRIDE_UI_TEXT_ALIGN_LEFT,
            ui->theme.text);
    } else if (openride_ui_button(ui, planner_id("planner-primary"), layout.primary,
                                  primary_label,
                                  can_generate ? OPENRIDE_UI_BUTTON_PRIMARY
                                               : OPENRIDE_UI_BUTTON_SECONDARY,
                                  can_generate, false)) {
        clicked = OPENRIDE_UI_ROUTE_PANEL_CALCULATE;
    }

    openride_ui_text(ui, layout.hint,
                     planner_busy
                         ? "Le calcul continue en arrière-plan"
                         : state->mode == OPENRIDE_RIDE_PLANNER_LOOP
                             ? "3 propositions seront comparées avant de partir"
                             : "Le trajet sera affiché sur la carte avant le départ",
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_CENTER);

    if (openride_ui_button(ui, planner_id("planner-back"), layout.back,
                           "Retour", OPENRIDE_UI_BUTTON_GHOST, interactive, false)) {
'''
    text = replace_once(text, old_primary, new_primary, "planner loading CTA")
    if text.count("interactive") < 8:
        fail("planner UI: too few controls were disabled while busy")
    return text


def main() -> int:
    base = load_base_module()
    files = dict(base.FILES)
    files.update({
        "async": ROOT / "src" / "app_async_runtime.c",
        "icon": ROOT / "src" / "ui" / "ui_icon.c",
        "route_ui": ROOT / "src" / "ui" / "ui_route_panel.c",
    })
    for path in files.values():
        if not path.exists():
            fail(f"missing required file: {path.relative_to(ROOT)}")
    required_small = [
        ROOT / "src/app_planner_async_runtime.h",
        ROOT / "src/app_planner_async_runtime.c",
        ROOT / "include/openride/ride_planner.h",
        ROOT / "include/openride/ui_icon.h",
        ROOT / "include/openride/ui_route_panel.h",
        ROOT / "include/openride/ui_loop_proposals_panel.h",
        ROOT / "src/ui/ui_loop_proposals_panel.c",
    ]
    for path in required_small:
        if not path.exists():
            fail(f"missing async Ride Planner component: {path.relative_to(ROOT)}")

    original = {key: path.read_text(encoding="utf-8") for key, path in files.items()}

    # Compose the original V3.1 migration in memory first.
    changed = {
        "cmake": base.patch_cmake(original["cmake"]),
        "loop": fix_base_proposal_move(base.patch_loop_generator(original["loop"])),
        "route_runtime": base.patch_route_runtime(original["route_runtime"]),
        "event": base.patch_event(original["event"]),
        "runtime": base.patch_runtime(original["runtime"]),
        "bridge": base.patch_bridge(original["bridge"]),
        "async": original["async"],
        "icon": original["icon"],
        "route_ui": original["route_ui"],
    }

    # Add the V3.1 async/loading layer before anything is written.
    changed["cmake"] = patch_cmake(changed["cmake"])
    changed["event"] = patch_event(changed["event"])
    changed["runtime"] = patch_runtime(changed["runtime"])
    changed["bridge"] = patch_bridge(changed["bridge"])
    changed["async"] = patch_async_runtime(changed["async"])
    changed["icon"] = patch_icon(changed["icon"])
    changed["route_ui"] = patch_route_ui(changed["route_ui"])

    required_tokens = {
        "cmake": ["app_planner_async_runtime.c", "ui_loop_proposals_panel.c"],
        "loop": ["openride_loop_generator_generate_proposals", "proposal_insert_ranked"],
        "route_runtime": ["openride_app_route_generate_loop_proposals"],
        "event": ["openride_app_planner_async_start_route", "openride_app_planner_async_start_loops"],
        "runtime": ["OpenRidePlannerAsyncContext planner_async_context", "planner_async_thread"],
        "bridge": [".busy = planner_busy", "ui_loop_proposals_panel.h"],
        "async": ["planner_async_thread", "openride_app_planner_async_loop_request_matches"],
        "icon": ["OPENRIDE_UI_ICON_LOADING", "openride_ui_icon_draw_rotated"],
        "route_ui": ["Recherche de balades...", "Calcul de l’itinéraire...", "OPENRIDE_UI_ICON_LOADING"],
    }
    for key, tokens in required_tokens.items():
        if changed[key] == original[key]:
            fail(f"{key}: migration produced no change")
        for token in tokens:
            if token not in changed[key]:
                fail(f"{key}: generated output missing {token}")

    # Transactional write point: no project file changes before this loop.
    for key, path in files.items():
        path.write_text(changed[key], encoding="utf-8")

    print("OK: OpenRide Ride Planner V3.1 + async loading migration applied")
    print("Changed: planner, loop proposals, async worker, UI bridge, SVG loader, CMake")
    print("Route: calculation runs outside the UI thread with animated loading state")
    print("Loops: proposal generation runs outside the UI thread with animated loading state")
    print("Input: planner controls locked while a calculation is running")
    print("Fallback: inter-region RoutingWorld keeps the same loading state until completion")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
