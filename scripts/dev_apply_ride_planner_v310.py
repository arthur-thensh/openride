#!/usr/bin/env python3
"""OpenRide V3.1 — Ride Planner V2 integration.

Guarded one-shot migration for the large orchestration files. Small public
headers/components are committed separately so this script only performs the
mechanical wiring that would otherwise require replacing large source files.

Changes:
- retain the best three routed loop candidates;
- add app runtime helpers to generate/take loop proposals;
- wire planner mode/proposal state through app runtime + UI bridge;
- route Android planner actions and toolbar Balade into the new flow;
- add the proposal comparison panel to CMake.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    "cmake": ROOT / "CMakeLists.txt",
    "loop": ROOT / "src" / "core" / "loop_generator.c",
    "route_runtime": ROOT / "src" / "app_route_runtime.c",
    "event": ROOT / "src" / "app_event_runtime.c",
    "runtime": ROOT / "src" / "app_runtime.c",
    "bridge": ROOT / "src" / "app_ui_bridge.c",
}


def fail(message: str) -> None:
    raise RuntimeError(message)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def replace_c_function(text: str, name: str, replacement: str) -> str:
    pattern = re.compile(rf"(?m)^[A-Za-z_][^\n]*\b{re.escape(name)}\s*\(")
    matches = list(pattern.finditer(text))
    if len(matches) != 1:
        fail(f"{name}: expected one function signature, found {len(matches)}")
    start = matches[0].start()
    brace = text.find("{", matches[0].end())
    if brace < 0:
        fail(f"{name}: opening brace not found")
    depth = 0
    i = brace
    in_string = in_char = in_line = in_block = escaped = False
    end = -1
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ""
        if in_line:
            if c == "\n": in_line = False
        elif in_block:
            if c == "*" and n == "/": in_block = False; i += 1
        elif in_string:
            if escaped: escaped = False
            elif c == "\\": escaped = True
            elif c == '"': in_string = False
        elif in_char:
            if escaped: escaped = False
            elif c == "\\": escaped = True
            elif c == "'": in_char = False
        else:
            if c == "/" and n == "/": in_line = True; i += 1
            elif c == "/" and n == "*": in_block = True; i += 1
            elif c == '"': in_string = True
            elif c == "'": in_char = True
            elif c == "{": depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    end = i + 1
                    break
        i += 1
    if end < 0:
        fail(f"{name}: closing brace not found")
    while end < len(text) and text[end] in " \t": end += 1
    if end < len(text) and text[end] == "\n": end += 1
    return text[:start] + replacement.rstrip() + "\n\n" + text[end:]


def patch_cmake(text: str) -> str:
    if "src/ui/ui_loop_proposals_panel.c" in text:
        fail("CMake: loop proposals panel already wired")
    return replace_once(
        text,
        "    src/ui/ui_route_panel.c\n",
        "    src/ui/ui_route_panel.c\n    src/ui/ui_loop_proposals_panel.c\n",
        "CMake loop proposal UI",
    )


def patch_loop_generator(text: str) -> str:
    destroy = r'''void openride_loop_proposal_set_destroy(OpenRideLoopProposalSet *proposals)
{
    if (!proposals) return;
    for (uint32_t i = 0U; i < OPENRIDE_LOOP_MAX_PROPOSALS; ++i) {
        openride_route_destroy(&proposals->items[i].route);
    }
    memset(proposals, 0, sizeof(*proposals));
}

'''
    text = replace_once(
        text,
        "static bool validate_request(const OpenRideRoutingGraph *graph,\n",
        destroy + "static bool validate_request(const OpenRideRoutingGraph *graph,\n",
        "loop proposal destroy insertion",
    )

    replacement = r'''static void proposal_fill_stats(OpenRideLoopCandidateStats *stats,
                                const OpenRideLoopCandidate *candidate)
{
    stats->successful = true;
    stats->distance_m = candidate->route.distance_m;
    stats->score = candidate->score;
    stats->distance_error_ratio = candidate->distance_error_ratio;
    stats->overlap_ratio = candidate->overlap_ratio;
    stats->max_waypoint_snap_distance_m = candidate->max_waypoint_snap_distance_m;
    stats->shape_score = candidate->shape_score;
    stats->waypoint_quality_score = candidate->waypoint_quality_score;
}

static void proposal_move_candidate(OpenRideLoopProposal *proposal,
                                    OpenRideLoopCandidate *candidate,
                                    uint32_t source_index)
{
    memset(proposal, 0, sizeof(*proposal));
    proposal->route = candidate->route;
    memset(&candidate->route, 0, sizeof(candidate->route));
    memcpy(proposal->waypoints, candidate->waypoints, sizeof(proposal->waypoints));
    proposal->waypoint_count = OPENRIDE_LOOP_MAX_WAYPOINTS;
    proposal->source_candidate_index = source_index;
    proposal_fill_stats(&proposal->stats, candidate);
}

static void proposal_insert_ranked(OpenRideLoopProposalSet *proposals,
                                   OpenRideLoopCandidate *candidate,
                                   uint32_t source_index)
{
    uint32_t position = proposals->count;
    if (position > OPENRIDE_LOOP_MAX_PROPOSALS) position = OPENRIDE_LOOP_MAX_PROPOSALS;
    for (uint32_t i = 0U; i < proposals->count; ++i) {
        if (candidate->score > proposals->items[i].stats.score) {
            position = i;
            break;
        }
    }

    if (proposals->count >= OPENRIDE_LOOP_MAX_PROPOSALS
        && position >= OPENRIDE_LOOP_MAX_PROPOSALS) {
        return;
    }

    uint32_t new_count = proposals->count;
    if (new_count < OPENRIDE_LOOP_MAX_PROPOSALS) {
        ++new_count;
    } else {
        openride_route_destroy(&proposals->items[OPENRIDE_LOOP_MAX_PROPOSALS - 1U].route);
        memset(&proposals->items[OPENRIDE_LOOP_MAX_PROPOSALS - 1U],
               0,
               sizeof(proposals->items[0]));
    }

    for (uint32_t i = new_count - 1U; i > position; --i) {
        proposals->items[i] = proposals->items[i - 1U];
        memset(&proposals->items[i - 1U], 0, sizeof(proposals->items[i - 1U]));
    }
    proposal_move_candidate(&proposals->items[position], candidate, source_index);
    proposals->count = new_count;
}

bool openride_loop_generator_generate_proposals(
    const OpenRideRoutingGraph *graph,
    const OpenRideLoopRequest *request,
    OpenRideLoopProposalSet *proposals,
    char *error,
    size_t error_size)
{
    if (!proposals || !validate_request(graph, request, error, error_size)) return false;

    OpenRideLoopProposalSet generated;
    memset(&generated, 0, sizeof(generated));
    generated.generation_stats.attempted_candidates = request->candidate_count;
    generated.generation_stats.selected_candidate_index = UINT32_MAX;
    generated.generation_stats.candidate_stat_count = request->candidate_count;

    uint32_t random_state = request->seed == 0U ? 0x4f70656eU : request->seed;
    for (uint32_t i = 0U; i < request->candidate_count; ++i) {
        OpenRideLoopCandidate candidate;
        memset(&candidate, 0, sizeof(candidate));
        OpenRideLoopCandidateStats *candidate_stats =
            &generated.generation_stats.candidates[i];
        if (!generate_candidate(graph, request, i, &random_state, &candidate)) {
            candidate_stats->successful = false;
            continue;
        }
        ++generated.generation_stats.successful_candidates;
        proposal_fill_stats(candidate_stats, &candidate);
        proposal_insert_ranked(&generated, &candidate, i);
        openride_route_destroy(&candidate.route);
    }

    if (generated.count == 0U) {
        openride_loop_proposal_set_destroy(&generated);
        set_error(error, error_size, "no loop candidate could be routed");
        return false;
    }

    const OpenRideLoopProposal *best = &generated.items[0];
    generated.generation_stats.score = best->stats.score;
    generated.generation_stats.distance_error_ratio = best->stats.distance_error_ratio;
    generated.generation_stats.overlap_ratio = best->stats.overlap_ratio;
    generated.generation_stats.max_waypoint_snap_distance_m =
        best->stats.max_waypoint_snap_distance_m;
    generated.generation_stats.shape_score = best->stats.shape_score;
    generated.generation_stats.waypoint_quality_score = best->stats.waypoint_quality_score;
    generated.generation_stats.selected_candidate_index = best->source_candidate_index;

    openride_loop_proposal_set_destroy(proposals);
    *proposals = generated;
    set_error(error, error_size, "");
    return true;
}

bool openride_loop_proposal_set_take(OpenRideLoopProposalSet *proposals,
                                     uint32_t index,
                                     OpenRideRoute *route,
                                     OpenRideRoutePoint waypoints[OPENRIDE_LOOP_MAX_WAYPOINTS],
                                     uint32_t *waypoint_count,
                                     OpenRideLoopCandidateStats *stats,
                                     uint32_t *source_candidate_index)
{
    if (!proposals || !route || index >= proposals->count) return false;
    OpenRideLoopProposal *chosen = &proposals->items[index];
    openride_route_destroy(route);
    *route = chosen->route;
    memset(&chosen->route, 0, sizeof(chosen->route));
    if (waypoints) memcpy(waypoints, chosen->waypoints, sizeof(chosen->waypoints));
    if (waypoint_count) *waypoint_count = chosen->waypoint_count;
    if (stats) *stats = chosen->stats;
    if (source_candidate_index) *source_candidate_index = chosen->source_candidate_index;
    openride_loop_proposal_set_destroy(proposals);
    return true;
}

bool openride_loop_generator_generate(const OpenRideRoutingGraph *graph,
                                      const OpenRideLoopRequest *request,
                                      OpenRideLoopResult *result,
                                      char *error,
                                      size_t error_size)
{
    if (!result) return false;
    OpenRideLoopProposalSet proposals = {0};
    if (!openride_loop_generator_generate_proposals(graph,
                                                    request,
                                                    &proposals,
                                                    error,
                                                    error_size)) {
        return false;
    }

    OpenRideLoopStats generation_stats = proposals.generation_stats;
    OpenRideLoopCandidateStats selected_stats = {0};
    uint32_t source_index = UINT32_MAX;
    OpenRideRoute route = {0};
    OpenRideRoutePoint waypoints[OPENRIDE_LOOP_MAX_WAYPOINTS] = {{0}};
    uint32_t waypoint_count = 0U;
    if (!openride_loop_proposal_set_take(&proposals,
                                         0U,
                                         &route,
                                         waypoints,
                                         &waypoint_count,
                                         &selected_stats,
                                         &source_index)) {
        openride_loop_proposal_set_destroy(&proposals);
        set_error(error, error_size, "unable to select generated loop");
        return false;
    }

    openride_loop_result_destroy(result);
    result->route = route;
    memcpy(result->waypoints, waypoints, sizeof(result->waypoints));
    result->waypoint_count = waypoint_count;
    result->stats = generation_stats;
    result->stats.score = selected_stats.score;
    result->stats.distance_error_ratio = selected_stats.distance_error_ratio;
    result->stats.overlap_ratio = selected_stats.overlap_ratio;
    result->stats.max_waypoint_snap_distance_m = selected_stats.max_waypoint_snap_distance_m;
    result->stats.shape_score = selected_stats.shape_score;
    result->stats.waypoint_quality_score = selected_stats.waypoint_quality_score;
    result->stats.selected_candidate_index = source_index;
    set_error(error, error_size, "");
    return true;
}'''
    return replace_c_function(text, "openride_loop_generator_generate", replacement)


def patch_route_runtime(text: str) -> str:
    insertion = r'''bool openride_app_route_generate_loop_proposals(
    const OpenRideRoutingGraph *graph,
    bool graph_loaded,
    const OpenRideMapSelection *selection,
    OpenRideRoutingProfile profile,
    double target_distance_m,
    OpenRideLoopDirection direction,
    uint32_t seed,
    OpenRideLoopProposalSet *proposals,
    OpenRideRoutingSnap *start_snap,
    char *status,
    size_t status_size)
{
    if (!proposals) return false;
    openride_loop_proposal_set_destroy(proposals);
    if (start_snap) {
        memset(start_snap, 0, sizeof(*start_snap));
        start_snap->segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    }
    if (!graph_loaded) {
        snprintf(status, status_size, "graphe routier non installe");
        return false;
    }
    if (!selection || !selection->has_start) {
        snprintf(status, status_size, "choisis d'abord un point de depart");
        return false;
    }

    OpenRideRoutingSnap local_start = {0};
    local_start.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    if (!openride_routing_graph_snap_to_segment(graph,
                                                selection->start.lat,
                                                selection->start.lon,
                                                OPENRIDE_MAX_SNAP_DISTANCE_M,
                                                &local_start)) {
        snprintf(status, status_size, "depart trop loin du reseau routier");
        return false;
    }

    OpenRideLoopRequest request = openride_loop_request_default();
    request.start = local_start;
    request.profile = profile;
    request.direction = direction;
    request.target_distance_m = target_distance_m;
    request.candidate_count = 9U;
    request.seed = seed;

    char loop_error[256] = {0};
    if (!openride_loop_generator_generate_proposals(graph,
                                                    &request,
                                                    proposals,
                                                    loop_error,
                                                    sizeof(loop_error))) {
        snprintf(status, status_size, "boucle impossible: %.180s",
                 loop_error[0] ? loop_error : "erreur inconnue");
        return false;
    }
    if (start_snap) *start_snap = local_start;
    snprintf(status, status_size,
             "%u propositions de balade autour de %.0f km",
             proposals->count,
             target_distance_m / 1000.0);
    return true;
}

bool openride_app_route_take_loop_proposal(
    OpenRideLoopProposalSet *proposals,
    uint32_t index,
    OpenRideRoute *route,
    OpenRideLoopStats *stats,
    OpenRideRoutePoint waypoints[OPENRIDE_LOOP_MAX_WAYPOINTS],
    uint32_t *waypoint_count,
    char *status,
    size_t status_size)
{
    if (!proposals || index >= proposals->count || !route) return false;
    OpenRideLoopStats generation = proposals->generation_stats;
    OpenRideLoopCandidateStats chosen = {0};
    uint32_t source_index = UINT32_MAX;
    if (!openride_loop_proposal_set_take(proposals,
                                         index,
                                         route,
                                         waypoints,
                                         waypoint_count,
                                         &chosen,
                                         &source_index)) {
        return false;
    }
    if (stats) {
        *stats = generation;
        stats->score = chosen.score;
        stats->distance_error_ratio = chosen.distance_error_ratio;
        stats->overlap_ratio = chosen.overlap_ratio;
        stats->max_waypoint_snap_distance_m = chosen.max_waypoint_snap_distance_m;
        stats->shape_score = chosen.shape_score;
        stats->waypoint_quality_score = chosen.waypoint_quality_score;
        stats->selected_candidate_index = source_index;
    }
    snprintf(status, status_size,
             "balade choisie: %.1f km | score %.0f | repetition %.0f%%",
             route->distance_m / 1000.0,
             chosen.score,
             chosen.overlap_ratio * 100.0);
    return true;
}

'''
    return replace_once(
        text,
        "void openride_app_route_clear_navigation_session(OpenRideNavigationEngine *navigation,\n",
        insertion + "void openride_app_route_clear_navigation_session(OpenRideNavigationEngine *navigation,\n",
        "route runtime proposal helpers",
    )


def patch_bridge(text: str) -> str:
    text = replace_once(
        text,
        '#include "openride/ui_route_panel.h"\n',
        '#include "openride/ui_route_panel.h"\n#include "openride/ui_loop_proposals_panel.h"\n',
        "bridge proposals include",
    )
    text = replace_once(
        text,
        "                                  bool gps_valid,\n                                  double gps_accuracy_m,\n                                  const OpenRideRouteDownloadPlan *route_download_plan_state,\n",
        "                                  bool gps_valid,\n                                  double gps_accuracy_m,\n                                  OpenRideRidePlannerMode planner_mode,\n                                  double loop_target_distance_m,\n                                  OpenRideLoopDirection loop_direction,\n                                  const OpenRideLoopProposalSet *loop_proposals,\n                                  const OpenRideRouteDownloadPlan *route_download_plan_state,\n",
        "bridge private planner args",
    )
    text = replace_once(
        text,
        "                .gps_valid = gps_valid,\n                .gps_accuracy_m = gps_accuracy_m\n",
        "                .mode = planner_mode,\n                .gps_valid = gps_valid,\n                .gps_accuracy_m = gps_accuracy_m,\n                .profile = profile,\n                .loop_target_distance_m = loop_target_distance_m,\n                .loop_direction = loop_direction\n",
        "bridge planner state",
    )
    proposal_branch = r'''
    if (panel == OPENRIDE_APP_PANEL_LOOP_PROPOSALS) {
        OpenRideUIContext ui;
        openride_ui_init(&ui);
        if (openride_ui_begin(&ui, renderer, width, height)) {
            (void)openride_ui_loop_proposals_draw(&ui,
                                                  loop_proposals,
                                                  loop_target_distance_m);
            openride_ui_end(&ui);
        }
        return;
    }

'''
    text = replace_once(
        text,
        "    if (panel == OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS) {\n",
        proposal_branch + "    if (panel == OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS) {\n",
        "bridge proposal panel draw",
    )
    text = replace_once(
        text,
        "                           bool gps_valid,\n                           double gps_accuracy_m,\n                           const OpenRideRouteDownloadPlan *route_download_plan_state,\n",
        "                           bool gps_valid,\n                           double gps_accuracy_m,\n                           OpenRideRidePlannerMode planner_mode,\n                           double loop_target_distance_m,\n                           OpenRideLoopDirection loop_direction,\n                           const OpenRideLoopProposalSet *loop_proposals,\n                           const OpenRideRouteDownloadPlan *route_download_plan_state,\n",
        "bridge public planner args",
    )
    text = replace_once(
        text,
        "                      gps_valid,\n                      gps_accuracy_m,\n                      route_download_plan_state,\n",
        "                      gps_valid,\n                      gps_accuracy_m,\n                      planner_mode,\n                      loop_target_distance_m,\n                      loop_direction,\n                      loop_proposals,\n                      route_download_plan_state,\n",
        "bridge planner forwarding",
    )
    return text


def patch_runtime(text: str) -> str:
    text = replace_once(
        text,
        "    bool loop_active = false;\n    bool route_valid = false;\n",
        "    bool loop_active = false;\n    OpenRideRidePlannerMode planner_mode = OPENRIDE_RIDE_PLANNER_ROUTE;\n    OpenRideLoopProposalSet loop_proposals = {0};\n    bool route_valid = false;\n",
        "runtime planner state",
    )
    text = replace_once(
        text,
        "        .loop_target_distance_m = &loop_target_distance_m,\n",
        "        .planner_mode = &planner_mode,\n        .loop_proposals = &loop_proposals,\n        .loop_target_distance_m = &loop_target_distance_m,\n",
        "event planner context",
    )
    text = replace_once(
        text,
        "                       gps_sample_valid ? 5.0 : 0.0,\n#endif\n                       &route_download_plan,\n",
        "                       gps_sample_valid ? 5.0 : 0.0,\n#endif\n                       planner_mode,\n                       loop_target_distance_m,\n                       loop_direction,\n                       &loop_proposals,\n                       &route_download_plan,\n",
        "runtime planner draw args",
    )
    text = replace_once(
        text,
        "    openride_navigation_engine_destroy(&navigation);\n    openride_route_destroy(&route);\n",
        "    openride_navigation_engine_destroy(&navigation);\n    openride_loop_proposal_set_destroy(&loop_proposals);\n    openride_route_destroy(&route);\n",
        "runtime proposal cleanup",
    )
    return text


def patch_event(text: str) -> str:
    text = replace_once(
        text,
        "                                    ? openride_app_ui_route_panel_hit_test(context->renderer,\n                                                                  x,\n                                                                  y,\n                                                                  width,\n                                                                  height)\n",
        "                                    ? openride_app_ui_route_panel_hit_test(context->renderer,\n                                                                  (int)(*context->planner_mode),\n                                                                  x,\n                                                                  y,\n                                                                  width,\n                                                                  height)\n",
        "planner hit-test mode",
    )
    text = replace_once(
        text,
        "                                : (*context->app_panel) == OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS\n",
        "                                : (*context->app_panel) == OPENRIDE_APP_PANEL_LOOP_PROPOSALS\n                                    ? openride_app_ui_loop_proposals_hit_test(context->renderer,\n                                                                      (*context->loop_proposals).count,\n                                                                      x, y, width, height)\n                                : (*context->app_panel) == OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS\n",
        "proposal panel hit-test",
    )
    text = replace_once(
        text,
        "                                    (*context->app_panel) == OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS\n                                        ? OPENRIDE_APP_PANEL_ROUTE\n                                        : OPENRIDE_APP_PANEL_MAIN;\n",
        "                                    ((*context->app_panel) == OPENRIDE_APP_PANEL_ROUTE_DOWNLOADS\n                                     || (*context->app_panel) == OPENRIDE_APP_PANEL_LOOP_PROPOSALS)\n                                        ? OPENRIDE_APP_PANEL_ROUTE\n                                        : OPENRIDE_APP_PANEL_MAIN;\n",
        "proposal back navigation",
    )

    planner_actions = r'''                            } else if (mobile_hit.action
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
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_ROUTE_LOOP_DIRECTION) {
                                (*context->loop_direction) =
                                    openride_loop_direction_next((*context->loop_direction));
                                openride_loop_proposal_set_destroy(context->loop_proposals);
'''
    text = replace_once(
        text,
        "                            } else if (mobile_hit.action\n                                       == OPENRIDE_APP_UI_ROUTE_GPS_START) {\n",
        planner_actions + "                            } else if (mobile_hit.action\n                                       == OPENRIDE_APP_UI_ROUTE_GPS_START) {\n",
        "planner semantic actions",
    )

    old_calculate = r'''                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_ROUTE_CALCULATE) {
                                if (openride_map_selection_complete(&(*context->selection))) {
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                    (*context->route_dirty) = true;
                                } else {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "choisis un depart et une arrivee");
                                }
'''
    new_calculate = r'''                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_ROUTE_CALCULATE) {
                                if ((*context->planner_mode) == OPENRIDE_RIDE_PLANNER_LOOP) {
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
                                        openride_app_route_clear_navigation_session(
                                            &(*context->navigation),
                                            &(*context->gps_simulator),
                                            &(*context->navigation_state),
                                            &(*context->gps_sample),
                                            &(*context->gps_sample_valid));
                                        openride_navigation_session_reset(&(*context->navigation_session));
                                        openride_location_filter_reset(&(*context->location_filter));
                                        openride_route_destroy(&(*context->route));
                                        (*context->route_valid) = false;
                                        (*context->route_dirty) = false;
                                        (*context->loop_active) = false;
                                        (*context->loop_waypoint_count) = 0U;
                                        if (openride_app_route_generate_loop_proposals(
                                                &(*context->routing_graph),
                                                (*context->graph_loaded),
                                                &(*context->selection),
                                                (*context->routing_profile),
                                                (*context->loop_target_distance_m),
                                                (*context->loop_direction),
                                                (*context->loop_seed)++,
                                                context->loop_proposals,
                                                &(*context->start_snap),
                                                context->route_status,
                                                context->route_status_size)) {
                                            (*context->app_panel) = OPENRIDE_APP_PANEL_LOOP_PROPOSALS;
                                        }
                                    }
                                } else if (openride_map_selection_complete(&(*context->selection))) {
                                    openride_loop_proposal_set_destroy(context->loop_proposals);
                                    (*context->loop_active) = false;
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_NONE;
                                    (*context->route_dirty) = true;
                                } else {
                                    snprintf(context->route_status,
                                             context->route_status_size,
                                             "choisis un depart et une arrivee");
                                }
                            } else if (mobile_hit.action
                                       == OPENRIDE_APP_UI_LOOP_PROPOSALS_REGENERATE) {
                                if (openride_app_route_generate_loop_proposals(
                                        &(*context->routing_graph),
                                        (*context->graph_loaded),
                                        &(*context->selection),
                                        (*context->routing_profile),
                                        (*context->loop_target_distance_m),
                                        (*context->loop_direction),
                                        (*context->loop_seed)++,
                                        context->loop_proposals,
                                        &(*context->start_snap),
                                        context->route_status,
                                        context->route_status_size)) {
                                    (*context->app_panel) = OPENRIDE_APP_PANEL_LOOP_PROPOSALS;
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
    text = replace_once(text, old_calculate, new_calculate, "planner calculate/proposals")

    text = replace_once(
        text,
        "                    } else {\n                        (*context->app_panel) = OPENRIDE_APP_PANEL_ROUTE;\n                        (*context->app_panel_selected) = 0U;\n                    }\n    #else\n                    (*context->app_panel) = OPENRIDE_APP_PANEL_ROUTE;\n                    (*context->app_panel_selected) = 0U;\n    #endif\n                } else if (action == OPENRIDE_TOOLBAR_LOOP) {\n",
        "                    } else {\n                        (*context->planner_mode) = OPENRIDE_RIDE_PLANNER_ROUTE;\n                        (*context->app_panel) = OPENRIDE_APP_PANEL_ROUTE;\n                        (*context->app_panel_selected) = 0U;\n                    }\n    #else\n                    (*context->planner_mode) = OPENRIDE_RIDE_PLANNER_ROUTE;\n                    (*context->app_panel) = OPENRIDE_APP_PANEL_ROUTE;\n                    (*context->app_panel_selected) = 0U;\n    #endif\n                } else if (action == OPENRIDE_TOOLBAR_LOOP) {\n",
        "toolbar route planner mode",
    )

    loop_pattern = re.compile(
        r"                \} else if \(action == OPENRIDE_TOOLBAR_LOOP\) \{\n.*?\n                \} else if \(action == OPENRIDE_TOOLBAR_GPS\) \{",
        re.DOTALL,
    )
    matches = list(loop_pattern.finditer(text))
    if len(matches) != 1:
        fail(f"toolbar loop block: expected one match, found {len(matches)}")
    loop_replacement = r'''                } else if (action == OPENRIDE_TOOLBAR_LOOP) {
                    (*context->planner_mode) = OPENRIDE_RIDE_PLANNER_LOOP;
                    (*context->app_panel) = OPENRIDE_APP_PANEL_ROUTE;
                    (*context->app_panel_selected) = 0U;
                    (*context->route_map_pick_marker) = OPENRIDE_MARKER_NONE;
                } else if (action == OPENRIDE_TOOLBAR_GPS) {'''
    text = text[:matches[0].start()] + loop_replacement + text[matches[0].end():]
    return text


def main() -> int:
    for path in FILES.values():
        if not path.exists(): fail(f"missing required file: {path.relative_to(ROOT)}")
    required_small = [
        ROOT / "include/openride/ride_planner.h",
        ROOT / "include/openride/ui_loop_proposals_panel.h",
        ROOT / "src/ui/ui_loop_proposals_panel.c",
    ]
    for path in required_small:
        if not path.exists(): fail(f"missing Ride Planner component: {path.relative_to(ROOT)}")

    original = {key: path.read_text(encoding="utf-8") for key, path in FILES.items()}
    changed = {
        "cmake": patch_cmake(original["cmake"]),
        "loop": patch_loop_generator(original["loop"]),
        "route_runtime": patch_route_runtime(original["route_runtime"]),
        "event": patch_event(original["event"]),
        "runtime": patch_runtime(original["runtime"]),
        "bridge": patch_bridge(original["bridge"]),
    }

    required_tokens = {
        "cmake": ["ui_loop_proposals_panel.c"],
        "loop": ["openride_loop_generator_generate_proposals", "proposal_insert_ranked"],
        "route_runtime": ["openride_app_route_generate_loop_proposals", "openride_app_route_take_loop_proposal"],
        "event": ["OPENRIDE_APP_PANEL_LOOP_PROPOSALS", "OPENRIDE_APP_UI_LOOP_PROPOSAL_SELECT", "planner_mode"],
        "runtime": ["OpenRideLoopProposalSet loop_proposals", ".planner_mode = &planner_mode"],
        "bridge": ["ui_loop_proposals_panel.h", "openride_ui_loop_proposals_draw", ".mode = planner_mode"],
    }
    for key, tokens in required_tokens.items():
        if changed[key] == original[key]: fail(f"{key}: migration produced no change")
        for token in tokens:
            if token not in changed[key]: fail(f"{key}: generated output missing {token}")

    # Transactional write point.
    for key, path in FILES.items():
        path.write_text(changed[key], encoding="utf-8")

    print("OK: OpenRide Ride Planner V2 / V3.1 migration applied")
    print("Changed: loop generator, route runtime, app event/runtime/UI bridge, CMake")
    print("Planner: Trajet/Boucle + Rapide/Balade/Trail + distance + direction")
    print("Loops: best 3 routed candidates retained and selectable")
    print("Navigation path after selection remains unchanged")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
