#!/usr/bin/env python3
"""OpenRide Ride Planner hotfix: generate loops with the start region graph.

Apply on top of the locally migrated V3.1 async Ride Planner tree, after the
feedback/snap diagnostics hotfixes. The loop worker resolves the region that
contains the selected start point, reuses the active graph when possible, and
otherwise loads that installed region's .orgraph inside the worker.

Transactional: validates all source fragments before writing files.
"""

from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    "header": ROOT / "src/app_planner_async_runtime.h",
    "worker": ROOT / "src/app_planner_async_runtime.c",
    "event": ROOT / "src/app_event_runtime.c",
}


def fail(message: str) -> None:
    raise RuntimeError(message)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def patch_header(text: str) -> str:
    text = replace_once(
        text,
        '#include "app_route_runtime.h"\n#include "openride/ride_planner.h"\n',
        '#include "app_route_runtime.h"\n#include "openride/platform_paths.h"\n#include "openride/region_manager.h"\n#include "openride/ride_planner.h"\n',
        "planner region includes",
    )
    text = replace_once(
        text,
        "    const OpenRideRoutingGraph *graph;\n    bool graph_loaded;\n",
        "    const OpenRideRoutingGraph *graph;\n    bool graph_loaded;\n    OpenRidePlatformPaths paths;\n    const OpenRideRegionDefinition *active_region;\n",
        "planner region context",
    )
    text = replace_once(
        text,
        "SDL_Thread *openride_app_planner_async_start_loops(\n"
        "    OpenRidePlannerAsyncContext *context,\n"
        "    const OpenRideRoutingGraph *graph,\n",
        "SDL_Thread *openride_app_planner_async_start_loops(\n"
        "    OpenRidePlannerAsyncContext *context,\n"
        "    const OpenRidePlatformPaths *paths,\n"
        "    const OpenRideRegionDefinition *active_region,\n"
        "    const OpenRideRoutingGraph *graph,\n",
        "planner loop start signature",
    )
    return text


def patch_worker(text: str) -> str:
    if '#include "openride/france_regions_lite.h"\n' not in text:
        text = replace_once(
            text,
            '#include "app_planner_async_runtime.h"\n\n',
            '#include "app_planner_async_runtime.h"\n\n'
            '#include "openride/france_regions_lite.h"\n'
            '#include "openride/region_manager.h"\n\n',
            "planner regional worker includes",
        )

    helper_marker = "static int SDLCALL planner_thread_main(void *userdata)\n"
    if text.count(helper_marker) != 1:
        fail("planner thread helper marker is not unique")

    helper = r'''static bool same_region(const OpenRideRegionDefinition *a,
                        const OpenRideRegionDefinition *b)
{
    return a && b && a->id && b->id && strcmp(a->id, b->id) == 0;
}

static const OpenRideRoutingGraph *resolve_loop_graph(
    OpenRidePlannerAsyncContext *context,
    OpenRideRoutingGraph *owned_graph,
    bool *owned_loaded)
{
    if (owned_loaded) *owned_loaded = false;
    if (!context || !owned_graph || !owned_loaded) return NULL;

    const char *region_id = openride_france_regions_lite_region_id(
        context->selection.start.lat,
        context->selection.start.lon);
    if (!region_id || !region_id[0]) {
        snprintf(context->status,
                 sizeof(context->status),
                 "Aucune region OpenRide ne couvre ce point de depart");
        SDL_Log("RidePlanner: no region coverage for loop start lat=%.7f lon=%.7f",
                context->selection.start.lat,
                context->selection.start.lon);
        return NULL;
    }

    const OpenRideRegionDefinition *start_region = openride_region_find(region_id);
    if (!start_region) {
        snprintf(context->status,
                 sizeof(context->status),
                 "Region de depart inconnue: %.80s",
                 region_id);
        SDL_Log("RidePlanner: unknown loop start region id=%s", region_id);
        return NULL;
    }

    if (same_region(start_region, context->active_region)
        && context->graph_loaded
        && context->graph) {
        SDL_Log("RidePlanner: loop region=%s uses active routing graph",
                start_region->id);
        return context->graph;
    }

    OpenRideRegionStatus status;
    char region_error[256] = {0};
    if (!openride_region_get_status(&context->paths,
                                    start_region,
                                    &status,
                                    region_error,
                                    sizeof(region_error))) {
        snprintf(context->status,
                 sizeof(context->status),
                 "Impossible de verifier la region %.80s",
                 start_region->name ? start_region->name : start_region->id);
        SDL_Log("RidePlanner: region status failed id=%s error=%s",
                start_region->id,
                region_error[0] ? region_error : "unknown error");
        return NULL;
    }

    if (!status.routing_installed || !status.routing_path[0]) {
        snprintf(context->status,
                 sizeof(context->status),
                 "Installe la region %.90s pour generer cette balade",
                 start_region->name ? start_region->name : start_region->id);
        SDL_Log("RidePlanner: routing graph not installed for loop region=%s",
                start_region->id);
        return NULL;
    }

    char graph_error[256] = {0};
    if (!openride_routing_graph_load(owned_graph,
                                     status.routing_path,
                                     graph_error,
                                     sizeof(graph_error))) {
        snprintf(context->status,
                 sizeof(context->status),
                 "Impossible de charger le routage de %.80s",
                 start_region->name ? start_region->name : start_region->id);
        SDL_Log("RidePlanner: failed loading loop graph region=%s path=%s error=%s",
                start_region->id,
                status.routing_path,
                graph_error[0] ? graph_error : "unknown error");
        return NULL;
    }

    *owned_loaded = true;
    SDL_Log("RidePlanner: loop start region=%s active=%s -> temporary graph loaded "
            "nodes=%u segments=%u",
            start_region->id,
            context->active_region && context->active_region->id
                ? context->active_region->id : "none",
            owned_graph->node_count,
            owned_graph->segment_index.segment_count);
    return owned_graph;
}

'''
    text = text.replace(helper_marker, helper + helper_marker, 1)

    start = text.find(
        "    } else if (context->kind == OPENRIDE_RIDE_PLANNER_GENERATING_LOOPS) {\n")
    if start < 0:
        fail("planner loop worker branch start not found")
    end_marker = "    }\n\n    SDL_SetAtomicInt(&context->success, ok ? 1 : 0);\n"
    end = text.find(end_marker, start)
    if end < 0:
        fail("planner loop worker branch end not found")

    new_branch = r'''    } else if (context->kind == OPENRIDE_RIDE_PLANNER_GENERATING_LOOPS) {
        OpenRideRoutingGraph owned_graph = {0};
        bool owned_loaded = false;
        const OpenRideRoutingGraph *loop_graph =
            resolve_loop_graph(context, &owned_graph, &owned_loaded);

        if (loop_graph) {
            /*
             * A single random seed can occasionally fail to produce a routable
             * shape. Retry only that transient case; validation/snap errors
             * remain immediate and visible.
             */
            for (uint32_t attempt = 0U; attempt < 3U; ++attempt) {
                context->status[0] = '\0';
                ok = openride_app_route_generate_loop_proposals(
                    loop_graph,
                    true,
                    &context->selection,
                    context->profile,
                    context->loop_target_distance_m,
                    context->loop_direction,
                    context->loop_seed + attempt * 0x9e3779b9U,
                    &context->proposals,
                    &context->start_snap,
                    context->status,
                    sizeof(context->status));
                if (ok) break;
                if (!strstr(context->status, "no loop candidate")) break;
            }
            if (!ok && strstr(context->status, "no loop candidate")) {
                snprintf(context->status,
                         sizeof(context->status),
                         "Aucune balade trouvee avec ces reglages. Essaie 50 km ou le profil Balade.");
            }
        }

        if (owned_loaded) {
            openride_routing_graph_destroy(&owned_graph);
        }
        if (!ok) {
            SDL_Log("RidePlanner: loop generation failed: %s",
                    context->status[0] ? context->status : "unknown error");
        }
'''
    text = text[:start] + new_branch + text[end:]

    text = replace_once(
        text,
        "SDL_Thread *openride_app_planner_async_start_loops(\n"
        "    OpenRidePlannerAsyncContext *context,\n"
        "    const OpenRideRoutingGraph *graph,\n",
        "SDL_Thread *openride_app_planner_async_start_loops(\n"
        "    OpenRidePlannerAsyncContext *context,\n"
        "    const OpenRidePlatformPaths *paths,\n"
        "    const OpenRideRegionDefinition *active_region,\n"
        "    const OpenRideRoutingGraph *graph,\n",
        "worker loop start signature",
    )
    text = replace_once(
        text,
        "{\n    if (!selection || !selection->has_start) return NULL;\n"
        "    if (!prepare_job(context,\n"
        "                     OPENRIDE_RIDE_PLANNER_GENERATING_LOOPS,\n",
        "{\n    if (!paths || !selection || !selection->has_start) return NULL;\n"
        "    if (!prepare_job(context,\n"
        "                     OPENRIDE_RIDE_PLANNER_GENERATING_LOOPS,\n",
        "worker loop start validation",
    )
    text = replace_once(
        text,
        "    context->loop_target_distance_m = target_distance_m;\n"
        "    context->loop_direction = direction;\n",
        "    context->paths = *paths;\n"
        "    context->active_region = active_region;\n"
        "    context->loop_target_distance_m = target_distance_m;\n"
        "    context->loop_direction = direction;\n",
        "worker loop regional state",
    )
    return text


def patch_event(text: str) -> str:
    old = (
        "openride_app_planner_async_start_loops(\n"
        "                                                context->planner_async_context,\n"
        "                                                &(*context->routing_graph),\n"
    )
    new = (
        "openride_app_planner_async_start_loops(\n"
        "                                                context->planner_async_context,\n"
        "                                                &(*context->platform_paths),\n"
        "                                                (*context->active_region),\n"
        "                                                &(*context->routing_graph),\n"
    )
    count = text.count(old)
    if count != 2:
        fail(f"planner loop starts: expected 2 matches, found {count}")
    return text.replace(old, new)


def main() -> int:
    for path in FILES.values():
        if not path.exists():
            fail(f"missing required file: {path.relative_to(ROOT)}")

    original = {key: path.read_text(encoding="utf-8") for key, path in FILES.items()}
    if "resolve_loop_graph" in original["worker"]:
        fail("loop region graph hotfix already applied")
    if "RidePlanner: loop generation failed" not in original["worker"]:
        fail("expected V3.1 feedback hotfix before regional graph fix")
    if "app_route_snap_loop_start" not in (ROOT / "src/app_route_runtime.c").read_text(encoding="utf-8"):
        fail("expected V3.1 loop snap diagnostics before regional graph fix")

    changed = {
        "header": patch_header(original["header"]),
        "worker": patch_worker(original["worker"]),
        "event": patch_event(original["event"]),
    }

    required = {
        "header": ["OpenRidePlatformPaths paths", "active_region"],
        "worker": [
            "openride_france_regions_lite_region_id",
            "temporary graph loaded",
            "Installe la region",
            "openride_routing_graph_destroy(&owned_graph)",
        ],
        "event": ["&(*context->platform_paths)", "(*context->active_region)"],
    }
    for key, tokens in required.items():
        if changed[key] == original[key]:
            fail(f"{key}: regional graph hotfix produced no change")
        for token in tokens:
            if token not in changed[key]:
                fail(f"{key}: generated output missing {token}")

    # Transactional write point.
    for key, path in FILES.items():
        path.write_text(changed[key], encoding="utf-8")

    print("OK: OpenRide loop regional-graph hotfix applied")
    print("Loop start: resolves the region containing the selected/GPS start")
    print("Routing: reuses active graph or loads that installed region in the worker")
    print("Navigation: loop proposals remain geometry-first, so regional node ids do not leak")
    print("UX: missing regional routing data is reported explicitly in the planner")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
