#!/usr/bin/env python3
"""OpenRide Ride Planner hotfix: resilient loop retry + visible feedback.

Apply on top of the already-migrated V3.1 async Ride Planner tree.
Transactional: validates every expected source fragment before writing files.
"""

from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    "worker": ROOT / "src/app_planner_async_runtime.c",
    "async": ROOT / "src/app_async_runtime.c",
    "route_header": ROOT / "include/openride/ui_route_panel.h",
    "route_ui": ROOT / "src/ui/ui_route_panel.c",
    "bridge_header": ROOT / "include/openride/app_ui_bridge.h",
    "bridge": ROOT / "src/app_ui_bridge.c",
    "runtime": ROOT / "src/app_runtime.c",
}


def fail(message: str) -> None:
    raise RuntimeError(message)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def patch_worker(text: str) -> str:
    if "#include <stdio.h>\n" not in text:
        text = replace_once(text,
                            '#include "app_planner_async_runtime.h"\n\n#include <string.h>\n',
                            '#include "app_planner_async_runtime.h"\n\n#include <stdio.h>\n#include <string.h>\n',
                            "planner worker stdio include")
    old = '''    } else if (context->kind == OPENRIDE_RIDE_PLANNER_GENERATING_LOOPS) {
        ok = openride_app_route_generate_loop_proposals(
            context->graph,
            context->graph_loaded,
            &context->selection,
            context->profile,
            context->loop_target_distance_m,
            context->loop_direction,
            context->loop_seed,
            &context->proposals,
            &context->start_snap,
            context->status,
            sizeof(context->status));
    }

    SDL_SetAtomicInt(&context->success, ok ? 1 : 0);
'''
    new = '''    } else if (context->kind == OPENRIDE_RIDE_PLANNER_GENERATING_LOOPS) {
        /*
         * A single random seed can occasionally fail to produce a routable
         * shape even though the start/profile are valid. Retry only that
         * transient case; validation/snap errors remain immediate and visible.
         */
        for (uint32_t attempt = 0U; attempt < 3U; ++attempt) {
            context->status[0] = '\\0';
            ok = openride_app_route_generate_loop_proposals(
                context->graph,
                context->graph_loaded,
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
        if (!ok) {
            SDL_Log("RidePlanner: loop generation failed: %s",
                    context->status[0] ? context->status : "unknown error");
        }
    }

    SDL_SetAtomicInt(&context->success, ok ? 1 : 0);
'''
    return replace_once(text, old, new, "planner worker retry")


def patch_async(text: str) -> str:
    old = '''                    (*context->events->planner_busy) = OPENRIDE_RIDE_PLANNER_IDLE;
                    openride_app_planner_async_reset(
                        &(*context->events->planner_async_context));
                } else if (completed_kind == OPENRIDE_RIDE_PLANNER_CALCULATING_ROUTE) {
'''
    new = '''                    (*context->events->planner_busy) = OPENRIDE_RIDE_PLANNER_IDLE;
                    if (calculation_ok && request_current) {
                        openride_app_planner_async_reset(
                            &(*context->events->planner_async_context));
                    } else {
                        SDL_Log("RidePlanner: keeping loop failure feedback: %s",
                                (*context->events->planner_async_context).status[0]
                                    ? (*context->events->planner_async_context).status
                                    : context->events->route_status);
                    }
                } else if (completed_kind == OPENRIDE_RIDE_PLANNER_CALCULATING_ROUTE) {
'''
    return replace_once(text, old, new, "preserve loop feedback")


def patch_route_header(text: str) -> str:
    return replace_once(
        text,
        "    OpenRideRidePlannerBusy busy;\n    bool has_start;\n",
        "    OpenRideRidePlannerBusy busy;\n    const char *feedback;\n    bool has_start;\n",
        "route panel feedback state",
    )


def patch_route_ui(text: str) -> str:
    old = '''    openride_ui_text(ui, layout.hint,
                     planner_busy
                         ? "Le calcul continue en arrière-plan"
                         : state->mode == OPENRIDE_RIDE_PLANNER_LOOP
                             ? "3 propositions seront comparées avant de partir"
                             : "Le trajet sera affiché sur la carte avant le départ",
                     OPENRIDE_UI_TEXT_CAPTION,
                     OPENRIDE_UI_TEXT_ALIGN_CENTER);
'''
    new = '''    if (!planner_busy && state->feedback && state->feedback[0]) {
        openride_ui_text_color(ui,
                               layout.hint,
                               state->feedback,
                               OPENRIDE_UI_TEXT_CAPTION,
                               OPENRIDE_UI_TEXT_ALIGN_CENTER,
                               ui->theme.danger);
    } else {
        openride_ui_text(ui, layout.hint,
                         planner_busy
                             ? "Le calcul continue en arrière-plan"
                             : state->mode == OPENRIDE_RIDE_PLANNER_LOOP
                                 ? "3 propositions seront comparées avant de partir"
                                 : "Le trajet sera affiché sur la carte avant le départ",
                         OPENRIDE_UI_TEXT_CAPTION,
                         OPENRIDE_UI_TEXT_ALIGN_CENTER);
    }
'''
    return replace_once(text, old, new, "route panel feedback draw")


def patch_bridge_header(text: str) -> str:
    return replace_once(
        text,
        "    OpenRideRidePlannerMode planner_mode,\n    OpenRideRidePlannerBusy planner_busy,\n    double loop_target_distance_m,\n",
        "    OpenRideRidePlannerMode planner_mode,\n    OpenRideRidePlannerBusy planner_busy,\n    const char *planner_feedback,\n    double loop_target_distance_m,\n",
        "bridge header feedback arg",
    )


def patch_bridge(text: str) -> str:
    text = replace_once(
        text,
        "                                  OpenRideRidePlannerMode planner_mode,\n                                  OpenRideRidePlannerBusy planner_busy,\n                                  double loop_target_distance_m,\n",
        "                                  OpenRideRidePlannerMode planner_mode,\n                                  OpenRideRidePlannerBusy planner_busy,\n                                  const char *planner_feedback,\n                                  double loop_target_distance_m,\n",
        "bridge private feedback arg",
    )
    text = replace_once(
        text,
        "                .mode = planner_mode,\n                .busy = planner_busy,\n                .gps_valid = gps_valid,\n",
        "                .mode = planner_mode,\n                .busy = planner_busy,\n                .feedback = planner_feedback,\n                .gps_valid = gps_valid,\n",
        "bridge feedback state",
    )
    text = replace_once(
        text,
        "                           OpenRideRidePlannerMode planner_mode,\n                           OpenRideRidePlannerBusy planner_busy,\n                           double loop_target_distance_m,\n",
        "                           OpenRideRidePlannerMode planner_mode,\n                           OpenRideRidePlannerBusy planner_busy,\n                           const char *planner_feedback,\n                           double loop_target_distance_m,\n",
        "bridge public feedback arg",
    )
    text = replace_once(
        text,
        "                      planner_mode,\n                      planner_busy,\n                      loop_target_distance_m,\n",
        "                      planner_mode,\n                      planner_busy,\n                      planner_feedback,\n                      loop_target_distance_m,\n",
        "bridge feedback forwarding",
    )
    return text


def patch_runtime(text: str) -> str:
    return replace_once(
        text,
        "                       planner_mode,\n                       planner_busy,\n                       loop_target_distance_m,\n",
        "                       planner_mode,\n                       planner_busy,\n                       planner_async_context.status[0]\n                           ? planner_async_context.status : NULL,\n                       loop_target_distance_m,\n",
        "runtime planner feedback draw arg",
    )


def main() -> int:
    for path in FILES.values():
        if not path.exists():
            fail(f"missing required file: {path.relative_to(ROOT)}")

    original = {key: path.read_text(encoding="utf-8") for key, path in FILES.items()}
    if "Aucune balade trouvee avec ces reglages" in original["worker"]:
        fail("Ride Planner feedback hotfix already applied")

    changed = {
        "worker": patch_worker(original["worker"]),
        "async": patch_async(original["async"]),
        "route_header": patch_route_header(original["route_header"]),
        "route_ui": patch_route_ui(original["route_ui"]),
        "bridge_header": patch_bridge_header(original["bridge_header"]),
        "bridge": patch_bridge(original["bridge"]),
        "runtime": patch_runtime(original["runtime"]),
    }

    required = {
        "worker": ["#include <stdio.h>", "attempt < 3U", "RidePlanner: loop generation failed"],
        "async": ["keeping loop failure feedback"],
        "route_header": ["const char *feedback;"],
        "route_ui": ["state->feedback", "ui->theme.danger"],
        "bridge_header": ["const char *planner_feedback"],
        "bridge": [".feedback = planner_feedback"],
        "runtime": ["planner_async_context.status[0]"],
    }
    for key, tokens in required.items():
        if changed[key] == original[key]:
            fail(f"{key}: hotfix produced no change")
        for token in tokens:
            if token not in changed[key]:
                fail(f"{key}: generated output missing {token}")

    for key, path in FILES.items():
        path.write_text(changed[key], encoding="utf-8")

    print("OK: OpenRide Ride Planner feedback hotfix applied")
    print("Loops: retry up to 3 seeds only for no-candidate failures")
    print("Failures: remain visible in the planner instead of silently disappearing")
    print("Diagnostics: adb logcat now contains RidePlanner failure messages")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
