#!/usr/bin/env python3
"""One-shot guarded migration for UI Engine V1.13 map status overlay.

Routes the normal map-screen status overlay through ui_map_overlay while
preserving two presentation modes:
- compact on Android, above the map toolbar;
- diagnostic on desktop, retaining development/status information.

Map/routing/GPS/GPX/loop business logic remains owned by main.c. The UI
component receives only preformatted display strings and simple flags.

This script does not build, test, commit, or push anything.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
CMAKE = ROOT / "CMakeLists.txt"
MAIN = ROOT / "src" / "main.c"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def replace_block(text: str,
                  start_marker: str,
                  end_marker: str,
                  replacement: str,
                  label: str) -> str:
    starts = text.count(start_marker)
    if starts != 1:
        raise RuntimeError(
            f"{label}: expected exactly one start marker, found {starts}"
        )
    start = text.find(start_marker)
    end = text.find(end_marker, start + len(start_marker))
    if end < 0:
        raise RuntimeError(f"{label}: end marker not found")
    if end <= start:
        raise RuntimeError(f"{label}: invalid marker order")
    return text[:start] + replacement + text[end:]


def prepare_cmake(text: str) -> str:
    return replace_once(
        text,
        "    src/ui/ui_route_downloads_panel.c\n"
        "    src/ui/ui_drive_hud.c\n"
        ")",
        "    src/ui/ui_route_downloads_panel.c\n"
        "    src/ui/ui_drive_hud.c\n"
        "    src/ui/ui_map_overlay.c\n"
        ")",
        "CMake V1.13 UI source",
    )


def prepare_main(text: str) -> str:
    text = replace_once(
        text,
        '#include "openride/ui_route_downloads_panel.h"\n'
        '#include "openride/ui_drive_hud.h"\n'
        '#include "openride/drive_mode.h"',
        '#include "openride/ui_route_downloads_panel.h"\n'
        '#include "openride/ui_drive_hud.h"\n'
        '#include "openride/ui_map_overlay.h"\n'
        '#include "openride/drive_mode.h"',
        "V1.13 UI include",
    )

    overlay_replacement = r'''static void draw_map_status_overlay(
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

'''
    text = replace_block(
        text,
        "static void draw_overlay(",
        "static void utf8_backspace(",
        overlay_replacement,
        "V1.13 map status overlay",
    )

    text = replace_block(
        text,
        "#ifdef __ANDROID__\nstatic void draw_android_status_overlay(",
        "static void draw_navigation_overlay(",
        "",
        "V1.13 remove Android status renderer",
    )

    old_render = r'''#ifdef __ANDROID__
        if (!drive_mode.active) {
            draw_android_status_overlay(renderer,
                                    metadata,
                                    &route,
                                    route_valid,
                                    route_status,
                                    routing_profile,
                                    width,
                                    height);
        }
#else
        draw_overlay(renderer,
                     &camera,
                     &selection,
                     metadata,
                     scalable_map,
                     graph_loaded,
                     routing_profile,
                     map_style,
                     &route,
                     route_valid,
                     route_status,
                     &start_snap,
                     &destination_snap,
                     loop_active,
                     loop_target_distance_m,
                     loop_direction,
                     &loop_stats,
                     &gpx_overlay,
                     gpx_loaded,
                     gpx_recording_active,
                     gpx_navigation_active,
                     width,
                     height);
#endif
'''
    new_render = r'''        if (!drive_mode.active) {
            draw_map_status_overlay(renderer,
                                    &camera,
                                    &selection,
                                    metadata,
                                    scalable_map,
                                    graph_loaded,
                                    routing_profile,
                                    map_style,
                                    &route,
                                    route_valid,
                                    route_status,
                                    &start_snap,
                                    &destination_snap,
                                    loop_active,
                                    loop_target_distance_m,
                                    loop_direction,
                                    &loop_stats,
                                    &gpx_overlay,
                                    gpx_loaded,
                                    gpx_recording_active,
                                    gpx_navigation_active,
#ifdef __ANDROID__
                                    true,
#else
                                    false,
#endif
                                    width,
                                    height);
        }
'''
    text = replace_once(
        text,
        old_render,
        new_render,
        "V1.13 render loop map overlay",
    )

    if "draw_android_status_overlay(" in text:
        raise RuntimeError("V1.13: old Android status overlay remains")
    if "static void draw_overlay(" in text:
        raise RuntimeError("V1.13: old desktop overlay remains")
    if text.count("openride_ui_map_overlay_draw(&ui, &state);") != 1:
        raise RuntimeError("V1.13: expected one UI map overlay draw call")
    if text.count("draw_map_status_overlay(") != 2:
        raise RuntimeError(
            "V1.13: expected definition + render call for map status overlay"
        )

    return text


def main() -> int:
    originals = {
        CMAKE: CMAKE.read_text(encoding="utf-8"),
        MAIN: MAIN.read_text(encoding="utf-8"),
    }
    prepared = {
        CMAKE: prepare_cmake(originals[CMAKE]),
        MAIN: prepare_main(originals[MAIN]),
    }

    for path, content in prepared.items():
        if content == originals[path]:
            raise RuntimeError(f"{path}: migration produced no change")

    removed = len(originals[MAIN]) - len(prepared[MAIN])
    if removed < 4000 or removed > 18000:
        raise RuntimeError(
            f"src/main.c: unexpected V1.13 size delta ({removed} bytes removed net)"
        )

    for path, content in prepared.items():
        path.write_text(content, encoding="utf-8")

    print("OK: UI Engine V1.13 map status overlay migration applied")
    print("Changed: CMakeLists.txt, src/main.c")
    print("Android keeps compact status; desktop keeps diagnostic status")
    print("Map/routing/GPX/loop business logic remains in main.c")
    print("Next: git diff --check && git diff -- CMakeLists.txt src/main.c")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
